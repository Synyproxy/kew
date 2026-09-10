/**
 * @file video_player.h
 * @brief The single mpv instance that shows video files.
 *
 * kew never draws video itself. A user script (kewrc `videoCommand`) starts
 * mpv and places its window over the cover-art area; this module launches
 * that script, talks to mpv over its IPC socket, and mirrors kew's pause,
 * seek and volume to it. All functions are safe to call from any thread and
 * are no-ops when no video is active.
 */
#ifndef VIDEO_PLAYER_H
#define VIDEO_PLAYER_H

#include "video_ipc.h"

#include <stdbool.h>

typedef struct {
        int row, col, rows, cols;
        int term_rows, term_cols;
        int term_px_w, term_px_h;
} VideoRect;

/** @brief Fills out[] with "KEY=VALUE" strings for the script. Returns the count. */
int video_player_build_env(const VideoRect *r, const char *action, const char *file,
                           const char *socket_path, char out[][160], int max);

/** @brief Duration in seconds via ffprobe, or a value <= 0 on failure. */
double video_player_probe_duration(const char *path);

/**
 * @brief Starts mpv on path, or loads path into the running mpv.
 * owner identifies the decoder asking; release() with the same pointer
 * stops mpv only if nobody else took over since.
 */
bool video_player_load(const char *path, const void *owner);
/** @brief Detaches owner. mpv keeps running until a new load reuses it or shutdown(). */
void video_player_release(const void *owner);

bool video_player_is_active(void);
bool video_player_is_gone(void);

/** @brief Drains the socket. Copies the latest status into out when non-NULL. */
void video_player_poll(VideoIpcStatus *out);

void video_player_set_paused(bool paused);
void video_player_seek(double seconds);
void video_player_set_volume(int percent);

/** @brief Records where the cover area is and re-places the window if it moved. */
void video_player_set_rect(const VideoRect *r);

void video_player_shutdown(void);

#endif
