#include "video_ext.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#define AUDIO_LIST "m4a|aac|mp3|ogg|flac|wav|aiff|opus|webm"
#define PLAYLIST_LIST "m3u|m3u8"

#define MAX_EXTS 32
#define EXT_LEN 16

static char exts[MAX_EXTS][EXT_LEN];
static int num_exts = 0;
static char library_regex[512] = "(" AUDIO_LIST "|" PLAYLIST_LIST ")$";
static char music_regex[512] = "(" AUDIO_LIST ")$";

static bool in_audio_list(const char *ext)
{
        const char *list = AUDIO_LIST "|" PLAYLIST_LIST;
        size_t n = strlen(ext);
        const char *p = list;
        while (*p) {
                const char *bar = strchr(p, '|');
                size_t len = bar ? (size_t)(bar - p) : strlen(p);
                if (len == n && strncasecmp(p, ext, n) == 0)
                        return true;
                if (!bar)
                        break;
                p = bar + 1;
        }
        return false;
}

static void build_regexes(void)
{
        char extra[256] = "";
        for (int i = 0; i < num_exts; i++) {
                if (in_audio_list(exts[i]))
                        continue;
                size_t used = strlen(extra);
                snprintf(extra + used, sizeof(extra) - used, "|%.15s", exts[i]);
        }
        snprintf(library_regex, sizeof(library_regex), "(" AUDIO_LIST "|" PLAYLIST_LIST "%s)$", extra);
        snprintf(music_regex, sizeof(music_regex), "(" AUDIO_LIST "%s)$", extra);
}

void video_ext_configure(const char *alternatives)
{
        num_exts = 0;
        const char *p = alternatives ? alternatives : "";

        while (*p && num_exts < MAX_EXTS) {
                while (*p == ' ' || *p == '\t' || *p == '|')
                        p++;
                if (!*p)
                        break;
                const char *start = p;
                while (*p && *p != '|')
                        p++;
                const char *end = p;
                while (end > start && (end[-1] == ' ' || end[-1] == '\t'))
                        end--;
                size_t len = (size_t)(end - start);
                if (len == 0 || len >= EXT_LEN)
                        continue;
                memcpy(exts[num_exts], start, len);
                exts[num_exts][len] = '\0';
                num_exts++;
        }

        build_regexes();
}

bool video_enabled(void)
{
        return num_exts > 0;
}

bool is_video_path(const char *path)
{
        if (!path || num_exts == 0)
                return false;

        const char *slash = strrchr(path, '/');
        const char *base = slash ? slash + 1 : path;
        const char *dot = strrchr(base, '.');
        if (!dot || dot[1] == '\0')
                return false;

        for (int i = 0; i < num_exts; i++) {
                if (strcasecmp(dot + 1, exts[i]) == 0)
                        return true;
        }
        return false;
}

const char *library_extensions_regex(void)
{
        return library_regex;
}

const char *music_extensions_regex(void)
{
        return music_regex;
}
