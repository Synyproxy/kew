/**
 * @file video_ext.h
 * @brief Which file extensions count as video, and the combined scan regexes.
 *
 * kew's library scan and playlist loaders match file names against a regex
 * of allowed extensions. Video support adds a user-configurable list on top
 * of the audio one. Everything here is derived from the configured list at
 * runtime; nothing about video is stored on disk.
 */
#ifndef VIDEO_EXT_H
#define VIDEO_EXT_H

#include <stdbool.h>

/**
 * @brief Sets the video extension list, e.g. "mp4|mkv|mov". Empty or NULL
 * disables video entirely: is_video_path() is always false and the regexes
 * are the plain audio ones.
 */
void video_ext_configure(const char *alternatives);

/** @brief True when a non-empty list has been configured. */
bool video_enabled(void);

/** @brief True when the path's extension is in the configured video list. */
bool is_video_path(const char *path);

/** @brief Audio + playlist + video extensions, as "(a|b|c)$". */
const char *library_extensions_regex(void);

/** @brief Audio + video extensions, no playlist files, as "(a|b|c)$". */
const char *music_extensions_regex(void);

#endif
