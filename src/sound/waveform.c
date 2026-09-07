/**
 * @file waveform.c
 * @brief Whole-track loudness envelope for the waveform progress view.
 */

#include "waveform.h"

#include "decoders.h"
#include "common/path_max.h"
#include "utils/file.h"
#include "utils/k_log.h"
#include "utils/utils.h"

#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define CACHE_MAGIC "KEWWF1"
#define READ_CHUNK_FRAMES 4096

typedef struct {
        char path[KEW_PATH_MAX];
        float *raw;      /* linear RMS per window */
        size_t count;
        size_t capacity;
        float peak;
        float floor_db;  /* quiet end of the track, dB below peak */
        bool complete;
} Envelope;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static Envelope env = {0};
static pthread_t worker;
static bool worker_running = false;
static atomic_int cancel_flag = 0;

/* ---------- cache ---------- */

static bool cache_file_for(const char *file_path, char *out, size_t out_size)
{
        struct stat st;
        if (stat(file_path, &st) != 0)
                return false;

        uint64_t h = 1469598103934665603ULL;
        for (const unsigned char *p = (const unsigned char *)file_path; *p; p++) {
                h ^= *p;
                h *= 1099511628211ULL;
        }
        uint64_t extra[2] = {(uint64_t)st.st_size, (uint64_t)st.st_mtime};
        for (size_t i = 0; i < sizeof(extra); i++) {
                h ^= ((const unsigned char *)extra)[i];
                h *= 1099511628211ULL;
        }

        const char *base = getenv("XDG_CACHE_HOME");
        char dir[KEW_PATH_MAX];
        if (base && *base) {
                snprintf(dir, sizeof(dir), "%s/kew/waveform", base);
        } else {
                const char *home = get_home_path();
                if (!home)
                        return false;
                snprintf(dir, sizeof(dir), "%s/.cache/kew/waveform", home);
        }

        char parent[KEW_PATH_MAX];
        snprintf(parent, sizeof(parent), "%s", dir);
        char *slash = strrchr(parent, '/');
        if (slash) {
                *slash = '\0';
                create_directory(parent);
        }
        create_directory(dir);

        int n = snprintf(out, out_size, "%s/%016llx.wf", dir, (unsigned long long)h);
        return n > 0 && (size_t)n < out_size;
}

static bool cache_load(const char *file_path, float **raw, size_t *count)
{
        char cache_path[KEW_PATH_MAX];
        if (!cache_file_for(file_path, cache_path, sizeof(cache_path)))
                return false;

        FILE *f = fopen(cache_path, "rb");
        if (!f)
                return false;

        char magic[sizeof(CACHE_MAGIC)] = {0};
        uint32_t n = 0;
        bool ok = fread(magic, 1, sizeof(CACHE_MAGIC) - 1, f) == sizeof(CACHE_MAGIC) - 1 &&
                  memcmp(magic, CACHE_MAGIC, sizeof(CACHE_MAGIC) - 1) == 0 &&
                  fread(&n, sizeof(n), 1, f) == 1 && n > 0 && n < (1u << 24);

        float *buf = NULL;
        if (ok) {
                buf = malloc(sizeof(float) * n);
                ok = buf && fread(buf, sizeof(float), n, f) == n;
        }
        fclose(f);

        if (!ok) {
                free(buf);
                return false;
        }
        *raw = buf;
        *count = n;
        return true;
}

static void cache_store(const char *file_path, const float *raw, size_t count)
{
        char cache_path[KEW_PATH_MAX];
        if (count == 0 || !cache_file_for(file_path, cache_path, sizeof(cache_path)))
                return;

        char tmp_path[KEW_PATH_MAX + 8];
        snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", cache_path);

        FILE *f = fopen(tmp_path, "wb");
        if (!f)
                return;

        uint32_t n = (uint32_t)count;
        bool ok = fwrite(CACHE_MAGIC, 1, sizeof(CACHE_MAGIC) - 1, f) == sizeof(CACHE_MAGIC) - 1 &&
                  fwrite(&n, sizeof(n), 1, f) == 1 &&
                  fwrite(raw, sizeof(float), count, f) == count;
        fclose(f);

        if (ok)
                rename(tmp_path, cache_path);
        else
                remove(tmp_path);
}

/* ---------- envelope building ---------- */

static void env_free_locked(void)
{
        free(env.raw);
        memset(&env, 0, sizeof(env));
        env.floor_db = -30.0f;
}

static int cmp_float(const void *a, const void *b)
{
        float x = *(const float *)a, y = *(const float *)b;
        return (x > y) - (x < y);
}

/* Picks the dB floor for the display: the 5th percentile of the windows so
   quiet intros and breakdowns sit low while the body of the track keeps its
   shape. Clamped so very flat or very dynamic material still reads. */
static void env_update_floor_locked(void)
{
        env.floor_db = -30.0f;
        if (env.count < 8 || env.peak <= 0.0f)
                return;

        float *sorted = malloc(sizeof(float) * env.count);
        if (!sorted)
                return;
        memcpy(sorted, env.raw, sizeof(float) * env.count);
        qsort(sorted, env.count, sizeof(float), cmp_float);

        float p05 = sorted[env.count / 20];
        free(sorted);

        if (p05 <= 0.0f)
                return;

        float db = 20.0f * log10f(p05 / env.peak);
        if (db > -12.0f)
                db = -12.0f;
        if (db < -40.0f)
                db = -40.0f;
        env.floor_db = db;
}

static bool env_push_locked(float rms)
{
        if (env.count == env.capacity) {
                size_t cap = env.capacity ? env.capacity * 2 : 1024;
                float *nr = realloc(env.raw, sizeof(float) * cap);
                if (!nr)
                        return false;
                env.raw = nr;
                env.capacity = cap;
        }
        env.raw[env.count++] = rms;
        if (rms > env.peak)
                env.peak = rms;
        return true;
}

static float sample_to_float(const void *frames, ma_format format, size_t index)
{
        switch (format) {
        case ma_format_f32:
                return ((const float *)frames)[index];
        case ma_format_s16:
                return ((const int16_t *)frames)[index] / 32768.0f;
        case ma_format_s32:
                return ((const int32_t *)frames)[index] / 2147483648.0f;
        case ma_format_s24: {
                const uint8_t *p = (const uint8_t *)frames + index * 3;
                int32_t v = (int32_t)((p[0] << 8) | (p[1] << 16) | (p[2] << 24)) >> 8;
                return v / 8388608.0f;
        }
        case ma_format_u8:
                return (((const uint8_t *)frames)[index] - 128) / 128.0f;
        default:
                return 0.0f;
        }
}

static void *worker_main(void *arg)
{
        char *path = arg;

        float *cached = NULL;
        size_t cached_count = 0;
        if (cache_load(path, &cached, &cached_count)) {
                pthread_mutex_lock(&lock);
                if (strcmp(env.path, path) == 0) {
                        for (size_t i = 0; i < cached_count; i++)
                                env_push_locked(cached[i]);
                        env_update_floor_locked();
                        env.complete = true;
                }
                pthread_mutex_unlock(&lock);
                free(cached);
                free(path);
                return NULL;
        }

        const CodecOps *ops = find_codec_ops(path);
        if (!ops) {
                free(path);
                return NULL;
        }

        void *decoder = malloc(ops->decoderSize);
        if (!decoder) {
                free(path);
                return NULL;
        }

        ma_decoding_backend_config config = {0};
        config.preferredFormat = ma_format_f32;
        config.seekPointCount = 0;

        if (ops->init(path, &config, decoder) != MA_SUCCESS) {
                k_log("waveform: decoder init failed for '%s'\n", path);
                free(decoder);
                free(path);
                return NULL;
        }

        ma_format format = ma_format_f32;
        ma_uint32 channels = 2;
        ma_uint32 sample_rate = 44100;
        ma_channel channel_map[MA_MAX_CHANNELS];
        ops->get_decoder_format(decoder, &format, &channels, &sample_rate, channel_map, MA_MAX_CHANNELS);
        if (channels == 0)
                channels = 1;
        if (sample_rate == 0)
                sample_rate = 44100;

        size_t bytes_per_sample = ma_get_bytes_per_sample(format);
        void *chunk = malloc(READ_CHUNK_FRAMES * channels * bytes_per_sample);
        if (!chunk) {
                ops->uninit(decoder);
                free(decoder);
                free(path);
                return NULL;
        }

        size_t window_frames = (size_t)(sample_rate * WAVEFORM_WINDOW_SECONDS);
        if (window_frames == 0)
                window_frames = 1;

        double sum_sq = 0.0;
        size_t frames_in_window = 0;
        size_t windows_since_publish = 0;
        bool aborted = false;

        for (;;) {
                if (atomic_load(&cancel_flag)) {
                        aborted = true;
                        break;
                }

                ma_uint64 read = 0;
                ma_result r = ma_data_source_read_pcm_frames(decoder, chunk, READ_CHUNK_FRAMES, &read);
                if (read == 0 || (r != MA_SUCCESS && r != MA_AT_END))
                        break;

                for (ma_uint64 f = 0; f < read; f++) {
                        float mono = 0.0f;
                        for (ma_uint32 c = 0; c < channels; c++)
                                mono += sample_to_float(chunk, format, (size_t)(f * channels + c));
                        mono /= (float)channels;
                        sum_sq += (double)mono * mono;

                        if (++frames_in_window == window_frames) {
                                float rms = (float)sqrt(sum_sq / (double)window_frames);
                                sum_sq = 0.0;
                                frames_in_window = 0;

                                pthread_mutex_lock(&lock);
                                bool same = strcmp(env.path, path) == 0;
                                if (same && !env_push_locked(rms))
                                        aborted = true;
                                /* Refresh the display floor now and then while decoding. */
                                if (same && ++windows_since_publish >= 200) {
                                        env_update_floor_locked();
                                        windows_since_publish = 0;
                                }
                                pthread_mutex_unlock(&lock);

                                if (!same || aborted) {
                                        aborted = true;
                                        break;
                                }
                        }
                }

                if (aborted || r == MA_AT_END)
                        break;
        }

        if (!aborted && frames_in_window > 0) {
                float rms = (float)sqrt(sum_sq / (double)frames_in_window);
                pthread_mutex_lock(&lock);
                if (strcmp(env.path, path) == 0)
                        env_push_locked(rms);
                pthread_mutex_unlock(&lock);
        }

        float *to_cache = NULL;
        size_t to_cache_count = 0;

        pthread_mutex_lock(&lock);
        if (!aborted && strcmp(env.path, path) == 0) {
                env_update_floor_locked();
                env.complete = true;
                to_cache = malloc(sizeof(float) * env.count);
                if (to_cache) {
                        memcpy(to_cache, env.raw, sizeof(float) * env.count);
                        to_cache_count = env.count;
                }
        }
        pthread_mutex_unlock(&lock);

        if (to_cache) {
                cache_store(path, to_cache, to_cache_count);
                free(to_cache);
        }

        free(chunk);
        ops->uninit(decoder);
        free(decoder);
        free(path);
        return NULL;
}

static void stop_worker(void)
{
        if (!worker_running)
                return;
        atomic_store(&cancel_flag, 1);
        pthread_join(worker, NULL);
        worker_running = false;
        atomic_store(&cancel_flag, 0);
}

/* ---------- public API ---------- */

void waveform_request(const char *file_path)
{
        if (!file_path || !*file_path)
                return;

        pthread_mutex_lock(&lock);
        bool same = strcmp(env.path, file_path) == 0;
        pthread_mutex_unlock(&lock);

        if (same)
                return;

        stop_worker();

        pthread_mutex_lock(&lock);
        env_free_locked();
        snprintf(env.path, sizeof(env.path), "%s", file_path);
        pthread_mutex_unlock(&lock);

        char *arg = strdup(file_path);
        if (!arg)
                return;

        if (pthread_create(&worker, NULL, worker_main, arg) == 0) {
                worker_running = true;
        } else {
                free(arg);
        }
}

bool waveform_acquire(const char *file_path, WaveformView *view)
{
        if (!file_path || !view)
                return false;

        pthread_mutex_lock(&lock);
        if (strcmp(env.path, file_path) != 0 || env.count == 0) {
                pthread_mutex_unlock(&lock);
                return false;
        }

        view->values = env.raw;
        view->count = env.count;
        view->complete = env.complete;
        view->peak = env.peak;
        view->floor_db = env.floor_db;
        return true;
}

float waveform_level(const WaveformView *view, float rms)
{
        if (!view || view->peak <= 0.0f || rms <= 0.0f)
                return 0.0f;

        float db = 20.0f * log10f(rms / view->peak);
        float floor_db = view->floor_db < -1.0f ? view->floor_db : -30.0f;
        float t = 1.0f - db / floor_db; /* floor -> 0, peak -> 1 */
        if (t < 0.0f)
                t = 0.0f;
        if (t > 1.0f)
                t = 1.0f;
        /* Keep a little bar even at the floor, like SoundCloud's baseline. */
        return 0.08f + 0.92f * t;
}

void waveform_release(void)
{
        pthread_mutex_unlock(&lock);
}

void waveform_shutdown(void)
{
        stop_worker();
        pthread_mutex_lock(&lock);
        env_free_locked();
        pthread_mutex_unlock(&lock);
}
