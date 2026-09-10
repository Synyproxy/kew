#include "video_decoder.h"

#include <stdio.h>
#include <string.h>

#ifndef KEW_VIDEO_DECODER_TEST
#include "video_player.h"

bool video_backend_load(const char *path, const void *owner)
{
        return video_player_load(path, owner);
}

void video_backend_release(const void *owner)
{
        video_player_release(owner);
}

void video_backend_poll(VideoIpcStatus *out, bool *gone)
{
        video_player_poll(out);
        /* mpv killed, or shut down by the engine: the track is over. */
        *gone = !video_player_is_active() || video_player_is_gone();
}

void video_backend_seek(double seconds)
{
        video_player_seek(seconds);
}

double video_backend_probe_duration(const char *path)
{
        return video_player_probe_duration(path);
}
#endif

/* ---- ma_data_source vtable ---- */

static ma_result ds_read(ma_data_source *ds, void *out, ma_uint64 count, ma_uint64 *read)
{
        return ma_video_read_pcm_frames((ma_video *)ds, out, count, read);
}

static ma_result ds_seek(ma_data_source *ds, ma_uint64 frame)
{
        return ma_video_seek_to_pcm_frame((ma_video *)ds, frame);
}

static ma_result ds_get_data_format(ma_data_source *ds, ma_format *f, ma_uint32 *ch, ma_uint32 *sr,
                                    ma_channel *map, size_t cap)
{
        return ma_video_get_data_format((ma_video *)ds, f, ch, sr, map, cap);
}

static ma_result ds_get_cursor(ma_data_source *ds, ma_uint64 *cursor)
{
        return ma_video_get_cursor_in_pcm_frames((ma_video *)ds, cursor);
}

static ma_result ds_get_length(ma_data_source *ds, ma_uint64 *length)
{
        return ma_video_get_length_in_pcm_frames((ma_video *)ds, length);
}

static ma_data_source_vtable g_video_vtable = {
    ds_read, ds_seek, ds_get_data_format, ds_get_cursor, ds_get_length, NULL, 0};

/* ---- implementation ---- */

ma_result ma_video_init_file(const char *path, const ma_decoding_backend_config *cfg, ma_video *v)
{
        (void)cfg;
        if (!path || !v)
                return MA_INVALID_ARGS;

        memset(v, 0, sizeof(*v));

        ma_data_source_config ds_config = ma_data_source_config_init();
        ds_config.vtable = &g_video_vtable;
        ma_result r = ma_data_source_init(&ds_config, &v->ds);
        if (r != MA_SUCCESS)
                return r;

        v->channels = VIDEO_CHANNELS;
        v->sample_rate = VIDEO_SAMPLE_RATE;
        snprintf(v->path, sizeof(v->path), "%s", path);

        double seconds = video_backend_probe_duration(path);
        if (seconds <= 0.0)
                seconds = 1.0; /* unknown: rely on mpv's eof */
        v->length = (ma_uint64)(seconds * VIDEO_SAMPLE_RATE);
        return MA_SUCCESS;
}

void ma_video_uninit(ma_video *v)
{
        if (!v)
                return;
        if (v->started)
                video_backend_release(v);
        ma_data_source_uninit(&v->ds);
}

ma_result ma_video_read_pcm_frames(ma_video *v, void *out, ma_uint64 count, ma_uint64 *read)
{
        if (read)
                *read = 0;
        if (!v || !out)
                return MA_INVALID_ARGS;

        if (v->failed)
                return MA_AT_END;

        if (!v->started) {
                v->started = true;
                if (!video_backend_load(v->path, v)) {
                        v->failed = true;
                        return MA_AT_END;
                }
        }

        VideoIpcStatus st;
        bool gone = false;
        video_backend_poll(&st, &gone);
        if (gone || st.eof)
                return MA_AT_END;

        ma_uint64 hard_end = v->length + (ma_uint64)VIDEO_END_GRACE_SECONDS * v->sample_rate;
        if (v->cursor >= hard_end)
                return MA_AT_END;

        if (count > hard_end - v->cursor)
                count = hard_end - v->cursor;

        memset(out, 0, (size_t)(count * v->channels * sizeof(float)));
        v->cursor += count;
        if (read)
                *read = count;
        return MA_SUCCESS;
}

ma_result ma_video_seek_to_pcm_frame(ma_video *v, ma_uint64 frame)
{
        if (!v)
                return MA_INVALID_ARGS;
        v->cursor = frame;
        if (v->started && !v->failed)
                video_backend_seek((double)frame / (double)v->sample_rate);
        return MA_SUCCESS;
}

ma_result ma_video_get_data_format(ma_video *v, ma_format *f, ma_uint32 *ch, ma_uint32 *sr,
                                   ma_channel *map, size_t cap)
{
        if (!v)
                return MA_INVALID_ARGS;
        if (f)
                *f = ma_format_f32;
        if (ch)
                *ch = v->channels;
        if (sr)
                *sr = v->sample_rate;
        if (map && cap >= 2) {
                map[0] = MA_CHANNEL_FRONT_LEFT;
                map[1] = MA_CHANNEL_FRONT_RIGHT;
        }
        return MA_SUCCESS;
}

ma_result ma_video_get_cursor_in_pcm_frames(ma_video *v, ma_uint64 *cursor)
{
        if (!v || !cursor)
                return MA_INVALID_ARGS;
        *cursor = v->cursor;
        return MA_SUCCESS;
}

ma_result ma_video_get_length_in_pcm_frames(ma_video *v, ma_uint64 *length)
{
        if (!v || !length)
                return MA_INVALID_ARGS;
        *length = v->length;
        return MA_SUCCESS;
}

void get_video_file_info(const char *filename, ma_format *format, ma_uint32 *channels,
                         ma_uint32 *sample_rate, ma_channel *channel_map)
{
        (void)filename;
        if (format)
                *format = ma_format_f32;
        if (channels)
                *channels = VIDEO_CHANNELS;
        if (sample_rate)
                *sample_rate = VIDEO_SAMPLE_RATE;
        if (channel_map) {
                channel_map[0] = MA_CHANNEL_FRONT_LEFT;
                channel_map[1] = MA_CHANNEL_FRONT_RIGHT;
        }
}
