#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DEVICE_IO
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#include "miniaudio.h"

#include "kew_test.h"
#include "sound/video_decoder.h"

/* ---- fake player ---- */
static int loads = 0, releases = 0;
static bool load_ok = true;
static VideoIpcStatus fake_status;
static double last_seek = -1;

bool video_backend_load(const char *path, const void *owner) { (void)path; (void)owner; loads++; return load_ok; }
void video_backend_release(const void *owner) { (void)owner; releases++; }
void video_backend_poll(VideoIpcStatus *out, bool *gone) { *out = fake_status; *gone = false; }
void video_backend_seek(double seconds) { last_seek = seconds; }
double video_backend_probe_duration(const char *path) { (void)path; return 10.0; }

int main(void)
{
        ma_video v;
        ma_decoding_backend_config cfg = {0};
        static float buf[48000 * 2];

        CHECK(ma_video_init_file("/x/clip.mp4", &cfg, &v) == MA_SUCCESS);
        CHECK(loads == 0); /* init must not launch mpv */

        ma_uint64 len = 0;
        CHECK(ma_video_get_length_in_pcm_frames(&v, &len) == MA_SUCCESS);
        CHECK(len == 480000);

        ma_format f; ma_uint32 ch, sr; ma_channel map[8];
        CHECK(ma_video_get_data_format(&v, &f, &ch, &sr, map, 8) == MA_SUCCESS);
        CHECK(f == ma_format_f32 && ch == 2 && sr == 48000);

        /* first read starts mpv and returns silence */
        ma_uint64 read = 0;
        buf[0] = 1.0f;
        CHECK(ma_video_read_pcm_frames(&v, buf, 1024, &read) == MA_SUCCESS);
        CHECK(loads == 1);
        CHECK(read == 1024);
        CHECK(buf[0] == 0.0f);

        ma_uint64 cur = 0;
        CHECK(ma_video_get_cursor_in_pcm_frames(&v, &cur) == MA_SUCCESS);
        CHECK(cur == 1024);

        /* second read does not relaunch */
        CHECK(ma_video_read_pcm_frames(&v, buf, 1024, &read) == MA_SUCCESS);
        CHECK(loads == 1);

        /* seek moves the cursor and reaches mpv in seconds */
        CHECK(ma_video_seek_to_pcm_frame(&v, 96000) == MA_SUCCESS);
        CHECK(last_seek > 1.99 && last_seek < 2.01);
        ma_video_get_cursor_in_pcm_frames(&v, &cur);
        CHECK(cur == 96000);

        /* past the probed length, silence keeps flowing for the grace period */
        ma_video_seek_to_pcm_frame(&v, 480000 - 10);
        CHECK(ma_video_read_pcm_frames(&v, buf, 100, &read) == MA_SUCCESS);
        CHECK(read == 100);

        /* mpv reports eof: end of stream */
        fake_status.eof = true;
        CHECK(ma_video_read_pcm_frames(&v, buf, 100, &read) == MA_AT_END);
        CHECK(read == 0);
        fake_status.eof = false;

        /* grace exhausted: end of stream even without eof */
        ma_video_seek_to_pcm_frame(&v, 480000 + 48000 * 5);
        CHECK(ma_video_read_pcm_frames(&v, buf, 100, &read) == MA_AT_END);

        /* the generic data source API goes through the vtable */
        ma_video_seek_to_pcm_frame(&v, 0);
        CHECK(ma_data_source_read_pcm_frames(&v, buf, 50, &read) == MA_SUCCESS);
        CHECK(read == 50);
        CHECK(ma_data_source_get_length_in_pcm_frames(&v, &len) == MA_SUCCESS);
        CHECK(len == 480000);

        ma_video_uninit(&v);
        CHECK(releases == 1);

        /* launch failure ends the stream at once */
        load_ok = false;
        CHECK(ma_video_init_file("/x/clip2.mkv", &cfg, &v) == MA_SUCCESS);
        CHECK(ma_video_read_pcm_frames(&v, buf, 100, &read) == MA_AT_END);
        ma_video_uninit(&v);

        KT_MAIN_END();
}
