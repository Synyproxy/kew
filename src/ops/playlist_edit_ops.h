/**
 * @file playlist_edit_ops.h
 * @brief Editing of M3U playlist files on disk.
 *
 * Adds songs to, removes songs from and deletes .m3u files that live in the
 * music library. The queue is never touched here; callers decide what to do
 * with enqueued songs.
 */

#ifndef PLAYLIST_EDIT_OPS_H
#define PLAYLIST_EDIT_OPS_H

#include "data/directorytree.h"

#include <stdbool.h>
#include <stddef.h>

#define MAX_LIBRARY_PLAYLISTS 256

/**
 * @brief Collects every .m3u / .m3u8 entry in the library tree.
 *
 * @param root Root of the library tree.
 * @param out  Array that receives the entries.
 * @param max  Capacity of @p out.
 *
 * @return Number of entries written to @p out.
 */
int collect_library_playlists(FileSystemEntry *root, FileSystemEntry **out, int max);

/**
 * @brief Copies the playlist name without directory and extension.
 */
void playlist_stem(const char *path, char *out, size_t size);

/**
 * @brief Appends the song, or every song below a directory, to an M3U file.
 *
 * Songs already listed in the file are skipped. The file is created when it
 * does not exist.
 *
 * @return Number of songs appended, or -1 if the file could not be written.
 */
int playlist_file_add_entry(const char *m3u_path, FileSystemEntry *entry);

/**
 * @brief Rewrites the M3U file without the first line matching @p song_path.
 *
 * @return true if a line was removed.
 */
bool playlist_file_remove_path(const char *m3u_path, const char *song_path);

/**
 * @brief Swaps the lines matching @p path_a and @p path_b in the M3U file.
 *
 * Lines between them (comments, missing tracks) keep their place.
 *
 * @return true if both lines were found and the file was rewritten.
 */
bool playlist_file_swap_paths(const char *m3u_path, const char *path_a, const char *path_b);

/**
 * @brief Deletes the M3U file from disk.
 *
 * @return true on success.
 */
bool playlist_file_delete(const char *m3u_path);

#endif
