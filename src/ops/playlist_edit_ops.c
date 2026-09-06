#include "playlist_edit_ops.h"

#include "data/playlist.h"
#include "utils/file.h"
#include "utils/k_log.h"

#include <glib.h>
#include <stdio.h>
#include <string.h>

int collect_library_playlists(FileSystemEntry *root, FileSystemEntry **out, int max)
{
        int count = 0;

        if (root == NULL || out == NULL || max <= 0)
                return 0;

        if (is_m3u_file(root)) {
                out[0] = root;
                return 1;
        }

        for (FileSystemEntry *child = root->children; child != NULL && count < max; child = child->next)
                count += collect_library_playlists(child, out + count, max - count);

        return count;
}

void playlist_stem(const char *path, char *out, size_t size)
{
        if (out == NULL || size == 0)
                return;

        out[0] = '\0';

        if (path == NULL)
                return;

        const char *base = strrchr(path, '/');
        base = base ? base + 1 : path;

        const char *dot = strrchr(base, '.');
        size_t len = dot ? (size_t)(dot - base) : strlen(base);

        if (len >= size)
                len = size - 1;

        memcpy(out, base, len);
        out[len] = '\0';
}

static gchar **read_playlist_lines(const char *m3u_path)
{
        gchar *contents = NULL;

        if (!g_file_get_contents(m3u_path, &contents, NULL, NULL))
                return NULL;

        gchar **lines = g_strsplit(contents, "\n", -1);
        g_free(contents);

        for (gint i = 0; lines[i] != NULL; i++)
                g_strstrip(g_strdelimit(lines[i], "\r", '\0'));

        return lines;
}

static bool line_matches_path(const char *line, const char *directory, const char *song_path)
{
        if (line[0] == '#' || line[0] == '\0')
                return false;

        if (strcmp(line, song_path) == 0)
                return true;

        if (g_path_is_absolute(line))
                return false;

        gchar *joined = g_build_filename(directory, line, NULL);
        bool same = joined != NULL && strcmp(joined, song_path) == 0;
        g_free(joined);

        return same;
}

static bool playlist_contains(gchar **lines, const char *directory, const char *song_path)
{
        if (lines == NULL)
                return false;

        for (gint i = 0; lines[i] != NULL; i++)
                if (line_matches_path(lines[i], directory, song_path))
                        return true;

        return false;
}

static int append_songs(FILE *file, gchar **existing, const char *directory, FileSystemEntry *entry)
{
        int added = 0;

        if (entry == NULL)
                return 0;

        if (entry->is_directory) {
                for (FileSystemEntry *child = entry->children; child != NULL; child = child->next)
                        added += append_songs(file, existing, directory, child);
                return added;
        }

        if (is_m3u_file(entry))
                return 0;

        if (!is_music_file(entry->full_path))
                return 0;

        if (playlist_contains(existing, directory, entry->full_path))
                return 0;

        fprintf(file, "%s\n", entry->full_path);

        return 1;
}

int playlist_file_add_entry(const char *m3u_path, FileSystemEntry *entry)
{
        if (m3u_path == NULL || entry == NULL)
                return -1;

        char expanded[KEW_PATH_MAX];
        expand_path(m3u_path, expanded, sizeof(expanded));

        gchar **existing = read_playlist_lines(expanded);
        gchar *directory = g_path_get_dirname(expanded);

        bool needs_newline = false;
        if (existing != NULL) {
                gchar *contents = NULL;
                gsize length = 0;
                if (g_file_get_contents(expanded, &contents, &length, NULL)) {
                        needs_newline = length > 0 && contents[length - 1] != '\n';
                        g_free(contents);
                }
        }

        FILE *file = fopen(expanded, "a");
        if (file == NULL) {
                g_strfreev(existing);
                g_free(directory);
                return -1;
        }

        if (needs_newline)
                fputc('\n', file);

        int added = append_songs(file, existing, directory, entry);

        fclose(file);
        g_strfreev(existing);
        g_free(directory);

        return added;
}

bool playlist_file_remove_path(const char *m3u_path, const char *song_path)
{
        if (m3u_path == NULL || song_path == NULL)
                return false;

        char expanded[KEW_PATH_MAX];
        expand_path(m3u_path, expanded, sizeof(expanded));

        gchar *contents = NULL;
        if (!g_file_get_contents(expanded, &contents, NULL, NULL))
                return false;

        gchar **lines = g_strsplit(contents, "\n", -1);
        g_free(contents);

        gchar *directory = g_path_get_dirname(expanded);
        GString *out = g_string_new(NULL);
        bool removed = false;

        for (gint i = 0; lines[i] != NULL; i++) {
                gchar *stripped = g_strstrip(g_strdup(g_strdelimit(lines[i], "\r", '\0')));

                bool skip = !removed && line_matches_path(stripped, directory, song_path);
                bool trailing_empty = lines[i + 1] == NULL && lines[i][0] == '\0';

                if (skip)
                        removed = true;
                else if (!trailing_empty)
                        g_string_append_printf(out, "%s\n", lines[i]);

                g_free(stripped);
        }

        bool ok = removed && g_file_set_contents(expanded, out->str, -1, NULL);

        if (!ok && removed)
                k_log("playlist_file_remove_path: could not write %s\n", expanded);

        g_string_free(out, TRUE);
        g_strfreev(lines);
        g_free(directory);

        return ok;
}

bool playlist_file_delete(const char *m3u_path)
{
        if (m3u_path == NULL)
                return false;

        char expanded[KEW_PATH_MAX];
        expand_path(m3u_path, expanded, sizeof(expanded));

        return remove(expanded) == 0;
}
