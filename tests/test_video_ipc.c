#include "kew_test.h"
#include "sound/video_ipc.h"

int main(void)
{
        VideoIpcStatus st = {0};

        video_ipc_status_apply_line(&st,
            "{\"event\":\"property-change\",\"id\":1,\"name\":\"time-pos\",\"data\":12.5}");
        CHECK(st.have_time_pos);
        CHECK(st.time_pos > 12.49 && st.time_pos < 12.51);

        video_ipc_status_apply_line(&st,
            "{\"event\":\"property-change\",\"id\":2,\"name\":\"duration\",\"data\":301.04}");
        CHECK(st.duration > 301.0 && st.duration < 301.1);

        video_ipc_status_apply_line(&st,
            "{\"event\":\"property-change\",\"id\":3,\"name\":\"pause\",\"data\":true}");
        CHECK(st.paused);
        video_ipc_status_apply_line(&st,
            "{\"event\":\"property-change\",\"id\":3,\"name\":\"pause\",\"data\":false}");
        CHECK(!st.paused);

        video_ipc_status_apply_line(&st,
            "{\"event\":\"property-change\",\"id\":4,\"name\":\"eof-reached\",\"data\":true}");
        CHECK(st.eof);

        /* A new file resets eof. */
        video_ipc_status_apply_line(&st, "{\"event\":\"start-file\",\"playlist_entry_id\":2}");
        CHECK(!st.eof);
        CHECK(!st.have_time_pos);

        video_ipc_status_apply_line(&st, "{\"event\":\"end-file\",\"reason\":\"eof\",\"playlist_entry_id\":2}");
        CHECK(st.eof);

        video_ipc_status_apply_line(&st,
            "{\"event\":\"property-change\",\"id\":5,\"name\":\"idle-active\",\"data\":true}");
        CHECK(st.idle);

        /* Null data (property unavailable) leaves the value alone. */
        st.have_time_pos = true;
        st.time_pos = 3.0;
        video_ipc_status_apply_line(&st,
            "{\"event\":\"property-change\",\"id\":1,\"name\":\"time-pos\",\"data\":null}");
        CHECK(st.time_pos == 3.0);

        /* Replies and unknown events are ignored without crashing. */
        video_ipc_status_apply_line(&st, "{\"request_id\":7,\"error\":\"success\"}");
        video_ipc_status_apply_line(&st, "{\"event\":\"file-loaded\"}");
        video_ipc_status_apply_line(&st, "");
        video_ipc_status_apply_line(&st, "not json at all");
        CHECK(st.time_pos == 3.0);

        KT_MAIN_END();
}
