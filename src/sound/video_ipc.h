/**
 * @file video_ipc.h
 * @brief Minimal client for mpv's JSON IPC socket.
 *
 * kew only needs a handful of observed properties and two events, so the
 * scanner looks for those shapes directly instead of parsing JSON.
 */
#ifndef VIDEO_IPC_H
#define VIDEO_IPC_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
        double time_pos;
        double duration;
        bool have_time_pos;
        bool paused;
        bool eof;
        bool idle;
} VideoIpcStatus;

typedef struct {
        int fd;
        char buf[4096];
        size_t len;
} VideoIpc;

/** @brief Updates st from one line of mpv output. Unknown lines are ignored. */
void video_ipc_status_apply_line(VideoIpcStatus *st, const char *line);

void video_ipc_init(VideoIpc *ipc);

/** @brief Connects to a Unix socket, retrying until timeout_ms elapses. */
bool video_ipc_connect(VideoIpc *ipc, const char *socket_path, int timeout_ms);

/** @brief Writes one command line. json_line must not include the newline. */
bool video_ipc_send(VideoIpc *ipc, const char *json_line);

/**
 * @brief Reads everything pending and applies complete lines to st.
 * @return false once the peer has closed the socket or an error occurred.
 */
bool video_ipc_poll(VideoIpc *ipc, VideoIpcStatus *st);

void video_ipc_close(VideoIpc *ipc);

#endif
