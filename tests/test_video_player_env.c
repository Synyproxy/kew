#include "kew_test.h"
#include "sound/video_player.h"

static const char *find(char env[][160], int n, const char *prefix)
{
        size_t len = strlen(prefix);
        for (int i = 0; i < n; i++)
                if (strncmp(env[i], prefix, len) == 0)
                        return env[i] + len;
        return NULL;
}

int main(void)
{
        char env[16][160];
        VideoRect r = {.row = 2, .col = 4, .rows = 18, .cols = 40,
                       .term_rows = 45, .term_cols = 120,
                       .term_px_w = 1080, .term_px_h = 680};

        int n = video_player_build_env(&r, "start", "/v/clip.mp4", "/run/user/1000/kew-mpv.sock", env, 16);
        CHECK(n == 11);
        CHECK_STR(find(env, n, "KEW_VIDEO_ACTION="), "start");
        CHECK_STR(find(env, n, "KEW_VIDEO_FILE="), "/v/clip.mp4");
        CHECK_STR(find(env, n, "KEW_VIDEO_SOCKET="), "/run/user/1000/kew-mpv.sock");
        CHECK_STR(find(env, n, "KEW_VIDEO_ROW="), "2");
        CHECK_STR(find(env, n, "KEW_VIDEO_COL="), "4");
        CHECK_STR(find(env, n, "KEW_VIDEO_ROWS="), "18");
        CHECK_STR(find(env, n, "KEW_VIDEO_COLS="), "40");
        CHECK_STR(find(env, n, "KEW_TERM_ROWS="), "45");
        CHECK_STR(find(env, n, "KEW_TERM_COLS="), "120");
        CHECK_STR(find(env, n, "KEW_TERM_PX_W="), "1080");
        CHECK_STR(find(env, n, "KEW_TERM_PX_H="), "680");

        /* place mode has no file */
        n = video_player_build_env(&r, "place", NULL, "/tmp/s.sock", env, 16);
        CHECK(n == 10);
        CHECK(find(env, n, "KEW_VIDEO_FILE=") == NULL);
        CHECK_STR(find(env, n, "KEW_VIDEO_ACTION="), "place");

        /* unknown pixel size is passed as -1 */
        r.term_px_w = -1;
        r.term_px_h = 0;
        n = video_player_build_env(&r, "place", NULL, "/tmp/s.sock", env, 16);
        CHECK_STR(find(env, n, "KEW_TERM_PX_W="), "-1");
        CHECK_STR(find(env, n, "KEW_TERM_PX_H="), "-1");

        /* too small an array truncates safely */
        n = video_player_build_env(&r, "start", "/x", "/s", env, 3);
        CHECK(n == 3);

        KT_MAIN_END();
}
