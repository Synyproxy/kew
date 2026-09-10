# mpv Video Overlay Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Play video files from kew's library in an mpv window that Hyprland pins over kew's cover-art square, while kew keeps its waveform, playhead, queue and keys.

**Architecture:** Video is one more codec in kew's decoder table: a miniaudio data source that outputs silence at 48 kHz stereo while a single mpv process, driven over its JSON IPC socket, plays the real file. The engine's loading, preloading, EOF, clock, MPRIS and repeat logic stay untouched. A shell script (`videoCommand` in kewrc) starts mpv and places its window; kew only passes cell coordinates in environment variables.

**Tech Stack:** C11, miniaudio, glib (already linked), POSIX sockets, mpv 0.41 JSON IPC, ffprobe/ffmpeg CLIs, Hyprland `hyprctl`, bash.

**Spec:** `docs/superpowers/specs/2026-09-10-mpv-video-overlay-design.md`

## Global Constraints

- Build and install after every code change, exactly as `CLAUDE.md` says: `make -j$(nproc) PREFIX=$HOME/.local && make install PREFIX=$HOME/.local`. Run `make clean` first whenever a header changed (Tasks 1, 3, 4, 5 all change headers).
- The only installed kew is `~/.local/bin/kew`. Never install elsewhere. Verify with `cmp ~/.local/bin/kew ./kew`.
- Commits are plain. No `Co-Authored-By`, no `Claude-Session`, no AI mention anywhere in the repo or scripts.
- Do not touch `~/.config/kew/kewstaterc` without backing it up first.
- Indentation in `src/` is 8-space tabs-as-spaces, K&R braces, as in the surrounding code.
- `webm` stays in the audio list. Video default list is `mp4|mkv|mov|avi|m4v`.
- With `videoCommand` empty, kew must behave exactly as before: no video files scanned, no new processes.
- Nothing is stored in `FileSystemEntry` or `SongData`; `DB_VERSION` does not change.

---

## File map

| File | Responsibility |
| --- | --- |
| `src/utils/video_ext.h/.c` (new) | Configurable video extension list, `is_video_path`, combined scan regexes. Pure, testable. |
| `src/sound/video_ipc.h/.c` (new) | mpv JSON IPC: line scanner (pure) and Unix socket client. |
| `src/sound/video_player.h/.c` (new) | The one mpv instance: launch via script, status, pause/seek/volume mirroring, placement rectangle, ffprobe duration. |
| `src/sound/video_decoder.h/.c` (new) | `ma_video` miniaudio data source that outputs silence and drives the player. |
| `src/sound/decoders.c`, `sound.c`, `audiotypes.h` | Register the `VIDEO` codec. |
| `src/sound/playback.c`, `volume.c` | Mirror pause/resume/stop/volume to the player. |
| `src/sound/waveform.c` | ffmpeg audio extraction fallback for video files. |
| `src/utils/utils.h/.c` | `run_detached_env`. |
| `src/ui/settings.c`, `src/common/model.h` | `videoCommand`, `videoExtensions`. |
| `src/data/directorytree.c`, `playlist.c`, `src/ops/playlist_ops.c` | Use the combined regexes; `is_music_file` accepts video. |
| `src/ui/components.c` | Cover component hands its rectangle to the player; video names keep their extension. |
| `tests/` (new), `Makefile` | `make test` builds and runs unit tests for the pure modules. |
| `~/.config/hypr/hyprland/scripts/kew-video.sh` (new), `music.sh`, `rules.lua`, `~/.config/kew/kewrc` | Hyprland side. Outside the repo. |

---

### Task 1: Video extension module, settings keys, test harness

**Files:**
- Create: `src/utils/video_ext.h`, `src/utils/video_ext.c`
- Create: `tests/test_video_ext.c`, `tests/kew_test.h`
- Modify: `Makefile` (SRCS list at line 236-248; add a `test` target after `clean:` at line 449)
- Modify: `src/common/model.h:694` (AppSettings, next to `hideCommand`)
- Modify: `src/ui/settings.c:911` (defaults), `:1526` (parse), `:2352` (write), end of `construct_app_settings` (starts line 1197)
- Modify: `src/data/directorytree.c:730,819`, `src/data/playlist.c:857`, `src/ops/playlist_ops.c:1025`, `src/data/playlist.c:1366`

**Interfaces:**
- Produces:
  - `void video_ext_configure(const char *alternatives);` empty string disables video.
  - `bool video_enabled(void);`
  - `bool is_video_path(const char *path);`
  - `const char *library_extensions_regex(void);` audio+playlist+video, form `(a|b|...)$`
  - `const char *music_extensions_regex(void);` audio+video, no playlist extensions
  - `AppSettings.videoCommand[512]`, `AppSettings.videoExtensions[128]`

- [ ] **Step 1: Write the test header and the failing test**

`tests/kew_test.h`:

```c
#ifndef KEW_TEST_H
#define KEW_TEST_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int kt_failures = 0;
static int kt_checks = 0;

#define CHECK(cond)                                                             \
        do {                                                                    \
                kt_checks++;                                                    \
                if (!(cond)) {                                                  \
                        kt_failures++;                                          \
                        fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,  \
                                __LINE__, #cond);                               \
                }                                                               \
        } while (0)

#define CHECK_STR(a, b)                                                         \
        do {                                                                    \
                kt_checks++;                                                    \
                if (strcmp((a), (b)) != 0) {                                    \
                        kt_failures++;                                          \
                        fprintf(stderr, "%s:%d: CHECK_STR failed: \"%s\" != \"%s\"\n", \
                                __FILE__, __LINE__, (a), (b));                  \
                }                                                               \
        } while (0)

#define KT_MAIN_END()                                                           \
        do {                                                                    \
                fprintf(stderr, "%s: %d checks, %d failures\n", __FILE__,       \
                        kt_checks, kt_failures);                                \
                return kt_failures ? 1 : 0;                                     \
        } while (0)

#endif
```

`tests/test_video_ext.c`:

```c
#include "kew_test.h"
#include "utils/video_ext.h"

int main(void)
{
        /* Disabled: nothing is video, regexes are the old audio ones. */
        video_ext_configure("");
        CHECK(!video_enabled());
        CHECK(!is_video_path("/a/b/clip.mp4"));
        CHECK_STR(library_extensions_regex(),
                  "(m4a|aac|mp3|ogg|flac|wav|aiff|opus|webm|m3u|m3u8)$");
        CHECK_STR(music_extensions_regex(),
                  "(m4a|aac|mp3|ogg|flac|wav|aiff|opus|webm)$");

        /* Default list. */
        video_ext_configure("mp4|mkv|mov|avi|m4v");
        CHECK(video_enabled());
        CHECK(is_video_path("/a/b/clip.mp4"));
        CHECK(is_video_path("/a/b/CLIP.MKV"));
        CHECK(is_video_path("clip.m4v"));
        CHECK(!is_video_path("/a/b/song.m4a"));
        CHECK(!is_video_path("/a/b/song.webm"));
        CHECK(!is_video_path("noextension"));
        CHECK(!is_video_path("/dir.mp4/song.flac"));
        CHECK(!is_video_path(NULL));
        CHECK_STR(library_extensions_regex(),
                  "(m4a|aac|mp3|ogg|flac|wav|aiff|opus|webm|m3u|m3u8|mp4|mkv|mov|avi|m4v)$");
        CHECK_STR(music_extensions_regex(),
                  "(m4a|aac|mp3|ogg|flac|wav|aiff|opus|webm|mp4|mkv|mov|avi|m4v)$");

        /* User adds webm as video; it is still in the audio list, so the
         * regex must not repeat it. */
        video_ext_configure("mp4|webm");
        CHECK(is_video_path("x.webm"));
        CHECK_STR(music_extensions_regex(),
                  "(m4a|aac|mp3|ogg|flac|wav|aiff|opus|webm|mp4)$");

        /* Whitespace and stray separators are tolerated. */
        video_ext_configure(" mp4 | mkv ||");
        CHECK(is_video_path("x.mkv"));
        CHECK_STR(music_extensions_regex(),
                  "(m4a|aac|mp3|ogg|flac|wav|aiff|opus|webm|mp4|mkv)$");

        KT_MAIN_END();
}
```

- [ ] **Step 2: Add the `test` target to the Makefile and run it to see it fail**

Append after the `clean:` rule (line 449 onward, keep its body intact):

```make
# Unit tests for the pure modules. They link only the sources they name,
# so they stay independent of the audio stack.
TEST_CFLAGS = -std=c11 -Wall -Wextra -Isrc -Iinclude -Iinclude/miniaudio -D_GNU_SOURCE
TEST_BIN_DIR = tests/bin

$(TEST_BIN_DIR):
	mkdir -p $(TEST_BIN_DIR)

$(TEST_BIN_DIR)/test_video_ext: tests/test_video_ext.c src/utils/video_ext.c tests/kew_test.h | $(TEST_BIN_DIR)
	$(CC) $(TEST_CFLAGS) -o $@ tests/test_video_ext.c src/utils/video_ext.c

TEST_BINS = $(TEST_BIN_DIR)/test_video_ext

.PHONY: test
test: $(TEST_BINS)
	@set -e; for t in $(TEST_BINS); do ./$$t; done
```

Add `tests/bin` to `.gitignore` (create the file if missing, one line).

Run: `make test`
Expected: FAIL, compiler cannot find `utils/video_ext.h`.

- [ ] **Step 3: Implement `video_ext`**

`src/utils/video_ext.h`:

```c
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
```

`src/utils/video_ext.c`:

```c
#include "video_ext.h"

#include <ctype.h>
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
                snprintf(extra + used, sizeof(extra) - used, "|%s", exts[i]);
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
```

- [ ] **Step 4: Run the unit test**

Run: `make test`
Expected: `tests/test_video_ext.c: 24 checks, 0 failures`, exit 0.

- [ ] **Step 5: Add the settings fields, defaults, parse and write**

`src/common/model.h`, directly after line 694 (`char hideCommand[512];`):

```c
        char videoCommand[512]; /**< Shell command that starts and places the mpv overlay. Empty disables video. */
        char videoExtensions[128]; /**< Regex alternatives treated as video, e.g. "mp4|mkv". */
```

`src/ui/settings.c` defaults, directly after line 911 (`settings->hideCommand[0] = '\0';`):

```c
        settings->videoCommand[0] = '\0';
        c_strcpy(settings->videoExtensions, "mp4|mkv|mov|avi|m4v",
                 sizeof(settings->videoExtensions));
```

Parse, directly after the `hidecommand` branch (line 1526-1529), before `} else if (strcmp(lowercase_key, "clearlistclearsall") == 0) {`:

```c
                } else if (strcmp(lowercase_key, "videocommand") == 0) {
                        snprintf(settings->videoCommand,
                                 sizeof(settings->videoCommand), "%s",
                                 pair->value);
                } else if (strcmp(lowercase_key, "videoextensions") == 0) {
                        snprintf(settings->videoExtensions,
                                 sizeof(settings->videoExtensions), "%s",
                                 pair->value);
```

Write, directly after line 2352 (`fprintf(file, "hideCommand=%s\n\n", settings->hideCommand);`):

```c
        fprintf(file, "# Shell command that starts and positions a video overlay window\n");
        fprintf(file, "# (mpv) over the cover art. Empty means video files are ignored.\n");
        fprintf(file, "# See kew-video.sh for the environment variables it receives.\n");
        fprintf(file, "videoCommand=%s\n\n", settings->videoCommand);

        fprintf(file, "# File extensions treated as video, separated by |.\n");
        fprintf(file, "videoExtensions=%s\n\n", settings->videoExtensions);
```

At the very end of `construct_app_settings` (the function starting at line 1197; find its closing brace), add as the last statement, and add `#include "utils/video_ext.h"` to the includes at the top of `settings.c`:

```c
        video_ext_configure(settings->videoCommand[0] != '\0'
                                ? settings->videoExtensions
                                : "");
```

- [ ] **Step 6: Point the four scan sites at the combined regexes**

Add `#include "utils/video_ext.h"` to `src/data/directorytree.c`, `src/data/playlist.c` and `src/ops/playlist_ops.c`.

`src/data/directorytree.c:730` and `:819`, replace `AUDIO_EXTENSIONS` in both `regcomp` calls:

```c
        regcomp(&regex, library_extensions_regex(), REG_EXTENDED | REG_ICASE);
```

`src/data/playlist.c:857` and `src/ops/playlist_ops.c:1025`, replace:

```c
        const char *allowed_extensions = music_extensions_regex();
```

`src/data/playlist.c:1366` `is_music_file`: add, right after the NULL check at the top:

```c
        if (is_video_path(filename))
                return 1;
```

- [ ] **Step 7: Add the source to the Makefile, clean-build, install, smoke-test**

In `SRCS` (Makefile line 237), append `src/utils/video_ext.c` after `src/utils/k_log.c`.

Run:

```sh
make clean && make -j$(nproc) PREFIX=$HOME/.local && make install PREFIX=$HOME/.local && cmp ~/.local/bin/kew ./kew && echo OK
```

Expected: build succeeds, `OK`. `~/.config/kew/kewrc` must not yet contain `videoCommand`, so behaviour is unchanged. Start `./kew` in tmux for two seconds (back up `~/.config/kew/kewstaterc` first), quit with Shift+Q, restore the state file, and confirm that `~/.config/kew/kewrc` now lists `videoCommand=` and `videoExtensions=mp4|mkv|mov|avi|m4v`.

- [ ] **Step 8: Commit**

```sh
git add Makefile .gitignore tests src/utils/video_ext.c src/utils/video_ext.h src/common/model.h src/ui/settings.c src/data/directorytree.c src/data/playlist.c src/ops/playlist_ops.c
git commit -m "Add a configurable video extension list and scan for it"
```

---

### Task 2: Detached runner with environment

**Files:**
- Modify: `src/utils/utils.h:65`, `src/utils/utils.c:669-705` (`run_detached`)

**Interfaces:**
- Produces: `void run_detached_env(const char *cmd, const char *const *env);` `env` is a NULL-terminated array of `"KEY=VALUE"` strings set in the child before `sh -c cmd`.

- [ ] **Step 1: Declare it**

`src/utils/utils.h`, after line 65:

```c
// Like run_detached, but first sets each "KEY=VALUE" string in env
// (NULL-terminated) in the child's environment.
void run_detached_env(const char *cmd, const char *const *env);
```

- [ ] **Step 2: Implement it by refactoring `run_detached`**

Replace the whole `run_detached` function in `src/utils/utils.c` with:

```c
void run_detached_env(const char *cmd, const char *const *env)
{
#ifndef _WIN32
    if (cmd == NULL || cmd[0] == '\0')
        return;

    // Double fork so the command is reparented to init and never leaves a
    // zombie behind; the middle child exits at once.
    pid_t pid = fork();
    if (pid < 0)
        return;
    if (pid == 0) {
        if (fork() == 0) {
            setsid();
            int devnull = open("/dev/null", O_RDWR);
            if (devnull >= 0) {
                dup2(devnull, STDIN_FILENO);
                dup2(devnull, STDOUT_FILENO);
                dup2(devnull, STDERR_FILENO);
                if (devnull > STDERR_FILENO)
                    close(devnull);
            }
            for (const char *const *e = env; e && *e; e++) {
                char *copy = strdup(*e);
                if (copy)
                    putenv(copy); // the child execs, so the leak is moot
            }
            execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        }
        _exit(0);
    }
    while (waitpid(pid, NULL, 0) < 0 && errno == EINTR)
        ;
#else
    (void)cmd;
    (void)env;
#endif
}

void run_detached(const char *cmd)
{
    run_detached_env(cmd, NULL);
}
```

- [ ] **Step 3: Build and check the hide action still works**

Run: `make -j$(nproc) PREFIX=$HOME/.local && make install PREFIX=$HOME/.local`
Expected: builds. Manual: Shift+Q the running kew, Super+F5, press `q`: the window hides as before.

- [ ] **Step 4: Commit**

```sh
git add src/utils/utils.c src/utils/utils.h
git commit -m "Let detached commands receive extra environment variables"
```

---

### Task 3: mpv IPC scanner and socket client

**Files:**
- Create: `src/sound/video_ipc.h`, `src/sound/video_ipc.c`
- Create: `tests/test_video_ipc.c`
- Modify: `Makefile` (SRCS, test target)

**Interfaces:**
- Produces:

```c
typedef struct {
        double time_pos;     /* seconds, valid when have_time_pos */
        double duration;     /* seconds, 0 until known */
        bool have_time_pos;
        bool paused;
        bool eof;            /* eof-reached true, or end-file event */
        bool idle;           /* idle-active true */
} VideoIpcStatus;

typedef struct {
        int fd;              /* -1 when not connected */
        char buf[4096];
        size_t len;
} VideoIpc;

void video_ipc_status_apply_line(VideoIpcStatus *st, const char *line); /* pure */
void video_ipc_init(VideoIpc *ipc);
bool video_ipc_connect(VideoIpc *ipc, const char *socket_path, int timeout_ms);
bool video_ipc_send(VideoIpc *ipc, const char *json_line);  /* appends '\n' */
bool video_ipc_poll(VideoIpc *ipc, VideoIpcStatus *st);     /* false when disconnected */
void video_ipc_close(VideoIpc *ipc);
```

- [ ] **Step 1: Write the failing test**

`tests/test_video_ipc.c`:

```c
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
```

Makefile: add after the `test_video_ext` rule:

```make
$(TEST_BIN_DIR)/test_video_ipc: tests/test_video_ipc.c src/sound/video_ipc.c tests/kew_test.h | $(TEST_BIN_DIR)
	$(CC) $(TEST_CFLAGS) -o $@ tests/test_video_ipc.c src/sound/video_ipc.c
```

and extend `TEST_BINS = $(TEST_BIN_DIR)/test_video_ext $(TEST_BIN_DIR)/test_video_ipc`.

Run: `make test`
Expected: FAIL, missing `sound/video_ipc.h`.

- [ ] **Step 2: Implement the header and the scanner**

`src/sound/video_ipc.h`:

```c
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
```

`src/sound/video_ipc.c`:

```c
#include "video_ipc.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

/* ---------- scanner ---------- */

/* Finds "key": and returns a pointer to the first character of the value. */
static const char *find_value(const char *line, const char *key)
{
        char needle[64];
        snprintf(needle, sizeof(needle), "\"%s\":", key);
        const char *p = strstr(line, needle);
        if (!p)
                return NULL;
        p += strlen(needle);
        while (*p == ' ')
                p++;
        return p;
}

static bool value_string_equals(const char *line, const char *key, const char *expected)
{
        const char *v = find_value(line, key);
        if (!v || *v != '"')
                return false;
        v++;
        size_t n = strlen(expected);
        return strncmp(v, expected, n) == 0 && v[n] == '"';
}

/* Returns 1 for true, 0 for false, -1 when absent, null or not a bool. */
static int value_bool(const char *line, const char *key)
{
        const char *v = find_value(line, key);
        if (!v)
                return -1;
        if (strncmp(v, "true", 4) == 0)
                return 1;
        if (strncmp(v, "false", 5) == 0)
                return 0;
        return -1;
}

static bool value_double(const char *line, const char *key, double *out)
{
        const char *v = find_value(line, key);
        if (!v || strncmp(v, "null", 4) == 0)
                return false;
        char *end = NULL;
        double d = strtod(v, &end);
        if (end == v)
                return false;
        *out = d;
        return true;
}

void video_ipc_status_apply_line(VideoIpcStatus *st, const char *line)
{
        if (!st || !line || line[0] != '{')
                return;

        if (value_string_equals(line, "event", "start-file")) {
                st->eof = false;
                st->idle = false;
                st->have_time_pos = false;
                st->time_pos = 0.0;
                st->duration = 0.0;
                return;
        }
        if (value_string_equals(line, "event", "end-file")) {
                st->eof = true;
                return;
        }
        if (!value_string_equals(line, "event", "property-change"))
                return;

        if (value_string_equals(line, "name", "time-pos")) {
                double d;
                if (value_double(line, "data", &d)) {
                        st->time_pos = d;
                        st->have_time_pos = true;
                }
        } else if (value_string_equals(line, "name", "duration")) {
                double d;
                if (value_double(line, "data", &d))
                        st->duration = d;
        } else if (value_string_equals(line, "name", "pause")) {
                int b = value_bool(line, "data");
                if (b >= 0)
                        st->paused = (b == 1);
        } else if (value_string_equals(line, "name", "eof-reached")) {
                int b = value_bool(line, "data");
                if (b == 1)
                        st->eof = true;
        } else if (value_string_equals(line, "name", "idle-active")) {
                int b = value_bool(line, "data");
                if (b >= 0)
                        st->idle = (b == 1);
        }
}

/* ---------- socket ---------- */

void video_ipc_init(VideoIpc *ipc)
{
        ipc->fd = -1;
        ipc->len = 0;
}

bool video_ipc_connect(VideoIpc *ipc, const char *socket_path, int timeout_ms)
{
        video_ipc_close(ipc);

        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", socket_path);

        struct timespec deadline;
        clock_gettime(CLOCK_MONOTONIC, &deadline);
        deadline.tv_sec += timeout_ms / 1000;
        deadline.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
        if (deadline.tv_nsec >= 1000000000L) {
                deadline.tv_sec++;
                deadline.tv_nsec -= 1000000000L;
        }

        for (;;) {
                int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
                if (fd < 0)
                        return false;
                if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
                        int flags = fcntl(fd, F_GETFL, 0);
                        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
                        ipc->fd = fd;
                        ipc->len = 0;
                        return true;
                }
                close(fd);

                struct timespec now;
                clock_gettime(CLOCK_MONOTONIC, &now);
                if (now.tv_sec > deadline.tv_sec ||
                    (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec))
                        return false;

                struct timespec nap = {0, 50 * 1000000L};
                nanosleep(&nap, NULL);
        }
}

bool video_ipc_send(VideoIpc *ipc, const char *json_line)
{
        if (ipc->fd < 0)
                return false;

        char line[1024];
        int n = snprintf(line, sizeof(line), "%s\n", json_line);
        if (n <= 0 || (size_t)n >= sizeof(line))
                return false;

        size_t off = 0;
        while (off < (size_t)n) {
                ssize_t w = write(ipc->fd, line + off, (size_t)n - off);
                if (w < 0) {
                        if (errno == EINTR || errno == EAGAIN)
                                continue;
                        return false;
                }
                off += (size_t)w;
        }
        return true;
}

bool video_ipc_poll(VideoIpc *ipc, VideoIpcStatus *st)
{
        if (ipc->fd < 0)
                return false;

        for (;;) {
                if (ipc->len >= sizeof(ipc->buf) - 1)
                        ipc->len = 0; /* pathological line, drop it */

                ssize_t r = read(ipc->fd, ipc->buf + ipc->len, sizeof(ipc->buf) - 1 - ipc->len);
                if (r == 0)
                        return false;
                if (r < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                                break;
                        if (errno == EINTR)
                                continue;
                        return false;
                }
                ipc->len += (size_t)r;
                ipc->buf[ipc->len] = '\0';

                char *start = ipc->buf;
                char *nl;
                while ((nl = strchr(start, '\n')) != NULL) {
                        *nl = '\0';
                        video_ipc_status_apply_line(st, start);
                        start = nl + 1;
                }
                size_t rest = ipc->len - (size_t)(start - ipc->buf);
                memmove(ipc->buf, start, rest);
                ipc->len = rest;
        }
        return true;
}

void video_ipc_close(VideoIpc *ipc)
{
        if (ipc->fd >= 0)
                close(ipc->fd);
        ipc->fd = -1;
        ipc->len = 0;
}
```

- [ ] **Step 3: Run the unit tests**

Run: `make test`
Expected: both binaries report 0 failures.

- [ ] **Step 4: Check against a real mpv**

```sh
S=/tmp/kewipc-test.sock
mpv --no-terminal --really-quiet --idle=yes --input-ipc-server=$S --force-window=no --ao=null &
sleep 0.5
printf '{"command":["get_property","idle-active"]}\n' | socat - UNIX-CONNECT:$S
kill %1
```

Expected: a line like `{"data":true,"request_id":0,"error":"success"}`. (If `socat` is missing, skip this step; the unit test covers the scanner.)

- [ ] **Step 5: Add to SRCS and build**

Makefile `SRCS`: append `src/sound/video_ipc.c` after `src/sound/volume.c`. Run `make -j$(nproc) PREFIX=$HOME/.local && make install PREFIX=$HOME/.local`.

- [ ] **Step 6: Commit**

```sh
git add Makefile tests/test_video_ipc.c src/sound/video_ipc.c src/sound/video_ipc.h
git commit -m "Add a small client for mpv's JSON IPC socket"
```

---

### Task 4: Video player (the one mpv instance)

**Files:**
- Create: `src/sound/video_player.h`, `src/sound/video_player.c`
- Create: `tests/test_video_player_env.c`
- Modify: `Makefile`

**Interfaces:**
- Consumes: `run_detached_env` (Task 2), `video_ipc_*` (Task 3), and the kewrc string `get_model()->settings.videoCommand` (`Model.settings` is the `AppSettings` struct; `Model.state.settings` is the unrelated `UISettings`). `get_model` is declared in `common/appstate.h`.
- Produces:

```c
typedef struct {
        int row, col, rows, cols;       /* cover area in cells, 0-based */
        int term_rows, term_cols;       /* terminal in cells */
        int term_px_w, term_px_h;       /* text area in pixels, -1 unknown */
} VideoRect;

int  video_player_build_env(const VideoRect *r, const char *action, const char *file,
                            const char *socket_path, char out[][160], int max); /* pure; returns count */
double video_player_probe_duration(const char *path);        /* ffprobe, <= 0 on failure */
bool video_player_load(const char *path, const void *owner); /* start mpv or loadfile */
void video_player_release(const void *owner);                /* quit mpv if owner is current */
bool video_player_is_active(void);
void video_player_poll(VideoIpcStatus *out);                 /* out may be NULL */
bool video_player_is_gone(void);                             /* socket lost */
void video_player_set_paused(bool paused);
void video_player_seek(double seconds);
void video_player_set_volume(int percent);
void video_player_set_rect(const VideoRect *r);              /* re-places if changed */
void video_player_shutdown(void);
```

- [ ] **Step 1: Write the failing env-builder test**

`tests/test_video_player_env.c`:

```c
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
```

Makefile rule (add next to the others, and add to `TEST_BINS`):

```make
$(TEST_BIN_DIR)/test_video_player_env: tests/test_video_player_env.c src/sound/video_player.c src/sound/video_ipc.c tests/kew_test.h | $(TEST_BIN_DIR)
	$(CC) $(TEST_CFLAGS) -DKEW_VIDEO_PLAYER_TEST -o $@ tests/test_video_player_env.c src/sound/video_player.c src/sound/video_ipc.c
```

`KEW_VIDEO_PLAYER_TEST` compiles out the parts that need `get_model()` and `run_detached_env`, see the implementation.

Run: `make test`
Expected: FAIL, missing header.

- [ ] **Step 2: Implement the header**

`src/sound/video_player.h`:

```c
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
```

- [ ] **Step 3: Implement the player**

`src/sound/video_player.c`:

```c
#include "video_player.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef KEW_VIDEO_PLAYER_TEST
#include "common/appstate.h"
#include "utils/k_log.h"
#include "utils/utils.h"
#endif

#define CONNECT_TIMEOUT_MS 3000

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static VideoIpc ipc = {.fd = -1};
static VideoIpcStatus status;
static const void *current_owner = NULL;
static bool active = false;
static bool gone = false;
static VideoRect rect;
static bool have_rect = false;
static char socket_path[256];
static int last_volume = -1;

int video_player_build_env(const VideoRect *r, const char *action, const char *file,
                           const char *socket_path_in, char out[][160], int max)
{
        int n = 0;
#define PUT(...)                                                        \
        do {                                                            \
                if (n < max) {                                          \
                        snprintf(out[n], 160, __VA_ARGS__);             \
                        n++;                                            \
                }                                                       \
        } while (0)

        PUT("KEW_VIDEO_ACTION=%s", action);
        if (file)
                PUT("KEW_VIDEO_FILE=%s", file);
        PUT("KEW_VIDEO_SOCKET=%s", socket_path_in);
        PUT("KEW_VIDEO_ROW=%d", r->row);
        PUT("KEW_VIDEO_COL=%d", r->col);
        PUT("KEW_VIDEO_ROWS=%d", r->rows);
        PUT("KEW_VIDEO_COLS=%d", r->cols);
        PUT("KEW_TERM_ROWS=%d", r->term_rows);
        PUT("KEW_TERM_COLS=%d", r->term_cols);
        PUT("KEW_TERM_PX_W=%d", r->term_px_w > 0 ? r->term_px_w : -1);
        PUT("KEW_TERM_PX_H=%d", r->term_px_h > 0 ? r->term_px_h : -1);
#undef PUT
        return n;
}

double video_player_probe_duration(const char *path)
{
        if (!path || !*path)
                return -1.0;

        int pipefd[2];
        if (pipe(pipefd) != 0)
                return -1.0;

        pid_t pid = fork();
        if (pid < 0) {
                close(pipefd[0]);
                close(pipefd[1]);
                return -1.0;
        }
        if (pid == 0) {
                dup2(pipefd[1], STDOUT_FILENO);
                close(pipefd[0]);
                close(pipefd[1]);
                int devnull = open("/dev/null", O_RDWR);
                if (devnull >= 0) {
                        dup2(devnull, STDIN_FILENO);
                        dup2(devnull, STDERR_FILENO);
                }
                execlp("ffprobe", "ffprobe", "-v", "error", "-show_entries",
                       "format=duration", "-of", "csv=p=0", "--", path, (char *)NULL);
                _exit(127);
        }
        close(pipefd[1]);

        char buf[64] = {0};
        size_t got = 0;
        for (;;) {
                ssize_t r = read(pipefd[0], buf + got, sizeof(buf) - 1 - got);
                if (r <= 0)
                        break;
                got += (size_t)r;
                if (got >= sizeof(buf) - 1)
                        break;
        }
        close(pipefd[0]);

        int wstatus = 0;
        waitpid(pid, &wstatus, 0);

        char *end = NULL;
        double d = strtod(buf, &end);
        if (end == buf || d <= 0.0)
                return -1.0;
        return d;
}

#ifndef KEW_VIDEO_PLAYER_TEST

static void socket_path_init(void)
{
        if (socket_path[0])
                return;
        const char *rt = getenv("XDG_RUNTIME_DIR");
        if (rt && *rt)
                snprintf(socket_path, sizeof(socket_path), "%s/kew-mpv.sock", rt);
        else
                snprintf(socket_path, sizeof(socket_path), "/tmp/kew-mpv-%d.sock", (int)getuid());
}

static const VideoRect *rect_or_default(void)
{
        static const VideoRect fallback = {.row = 0, .col = 0, .rows = 10, .cols = 20,
                                           .term_rows = 24, .term_cols = 80,
                                           .term_px_w = -1, .term_px_h = -1};
        return have_rect ? &rect : &fallback;
}

/* Runs videoCommand with the given action. Caller holds lock. */
static bool run_script_locked(const char *action, const char *file)
{
        Model *model = get_model();
        const char *cmd = model->settings.videoCommand;
        if (!cmd || !*cmd)
                return false;

        char env[16][160];
        const char *envp[17];
        int n = video_player_build_env(rect_or_default(), action, file, socket_path, env, 16);
        for (int i = 0; i < n; i++)
                envp[i] = env[i];
        envp[n] = NULL;

        run_detached_env(cmd, envp);
        return true;
}

static void send_locked(const char *json)
{
        if (ipc.fd >= 0 && !video_ipc_send(&ipc, json))
                gone = true;
}

static void observe_locked(void)
{
        send_locked("{\"command\":[\"observe_property\",1,\"time-pos\"]}");
        send_locked("{\"command\":[\"observe_property\",2,\"duration\"]}");
        send_locked("{\"command\":[\"observe_property\",3,\"pause\"]}");
        send_locked("{\"command\":[\"observe_property\",4,\"eof-reached\"]}");
        send_locked("{\"command\":[\"observe_property\",5,\"idle-active\"]}");
}

/* Escapes a path for use inside a JSON string. */
static void json_escape(const char *in, char *out, size_t out_size)
{
        size_t o = 0;
        for (const char *p = in; *p && o + 2 < out_size; p++) {
                if (*p == '"' || *p == '\\') {
                        out[o++] = '\\';
                        out[o++] = *p;
                } else if ((unsigned char)*p < 0x20) {
                        continue;
                } else {
                        out[o++] = *p;
                }
        }
        out[o] = '\0';
}

bool video_player_load(const char *path, const void *owner)
{
        if (!path || !*path)
                return false;

        pthread_mutex_lock(&lock);
        socket_path_init();

        if (active && !gone && ipc.fd >= 0) {
                char esc[1024];
                json_escape(path, esc, sizeof(esc));
                char cmd[1200];
                snprintf(cmd, sizeof(cmd), "{\"command\":[\"loadfile\",\"%s\",\"replace\"]}", esc);
                memset(&status, 0, sizeof(status));
                send_locked(cmd);
                if (!gone) {
                        current_owner = owner;
                        pthread_mutex_unlock(&lock);
                        k_log("video_player: loadfile '%s'\n", path);
                        return true;
                }
                /* fall through to a fresh start */
                video_ipc_close(&ipc);
        }

        unlink(socket_path);
        memset(&status, 0, sizeof(status));
        gone = false;
        active = false;

        if (!run_script_locked("start", path)) {
                pthread_mutex_unlock(&lock);
                k_log("video_player: videoCommand is empty\n");
                return false;
        }

        if (!video_ipc_connect(&ipc, socket_path, CONNECT_TIMEOUT_MS)) {
                pthread_mutex_unlock(&lock);
                k_log("video_player: could not connect to '%s'\n", socket_path);
                return false;
        }

        observe_locked();
        if (last_volume >= 0) {
                char cmd[96];
                snprintf(cmd, sizeof(cmd), "{\"command\":[\"set_property\",\"volume\",%d]}", last_volume);
                send_locked(cmd);
        }
        active = true;
        current_owner = owner;
        pthread_mutex_unlock(&lock);
        k_log("video_player: started '%s'\n", path);
        return true;
}

static void stop_locked(void)
{
        if (ipc.fd >= 0) {
                send_locked("{\"command\":[\"quit\"]}");
                video_ipc_close(&ipc);
        }
        active = false;
        gone = false;
        current_owner = NULL;
        memset(&status, 0, sizeof(status));
}

void video_player_release(const void *owner)
{
        pthread_mutex_lock(&lock);
        if (active && current_owner == owner)
                stop_locked();
        pthread_mutex_unlock(&lock);
}

bool video_player_is_active(void)
{
        pthread_mutex_lock(&lock);
        bool a = active;
        pthread_mutex_unlock(&lock);
        return a;
}

bool video_player_is_gone(void)
{
        pthread_mutex_lock(&lock);
        bool g = active && gone;
        pthread_mutex_unlock(&lock);
        return g;
}

void video_player_poll(VideoIpcStatus *out)
{
        pthread_mutex_lock(&lock);
        if (active && !gone && ipc.fd >= 0) {
                if (!video_ipc_poll(&ipc, &status)) {
                        gone = true;
                        video_ipc_close(&ipc);
                        k_log("video_player: mpv went away\n");
                }
        }
        if (out)
                *out = status;
        pthread_mutex_unlock(&lock);
}

void video_player_set_paused(bool paused)
{
        pthread_mutex_lock(&lock);
        if (active && !gone)
                send_locked(paused ? "{\"command\":[\"set_property\",\"pause\",true]}"
                                   : "{\"command\":[\"set_property\",\"pause\",false]}");
        pthread_mutex_unlock(&lock);
}

void video_player_seek(double seconds)
{
        pthread_mutex_lock(&lock);
        if (active && !gone) {
                char cmd[96];
                snprintf(cmd, sizeof(cmd), "{\"command\":[\"seek\",%.3f,\"absolute\"]}", seconds);
                send_locked(cmd);
        }
        pthread_mutex_unlock(&lock);
}

void video_player_set_volume(int percent)
{
        if (percent < 0)
                percent = 0;
        if (percent > 100)
                percent = 100;
        pthread_mutex_lock(&lock);
        last_volume = percent;
        if (active && !gone) {
                char cmd[96];
                snprintf(cmd, sizeof(cmd), "{\"command\":[\"set_property\",\"volume\",%d]}", percent);
                send_locked(cmd);
        }
        pthread_mutex_unlock(&lock);
}

void video_player_set_rect(const VideoRect *r)
{
        if (!r)
                return;
        pthread_mutex_lock(&lock);
        bool changed = !have_rect || memcmp(&rect, r, sizeof(rect)) != 0;
        rect = *r;
        have_rect = true;
        if (changed && active && !gone)
                run_script_locked("place", NULL);
        pthread_mutex_unlock(&lock);
}

void video_player_shutdown(void)
{
        pthread_mutex_lock(&lock);
        stop_locked();
        pthread_mutex_unlock(&lock);
}

#endif /* KEW_VIDEO_PLAYER_TEST */
```

Add these includes at the top of the file, above the test guard, since `probe_duration` uses them: `#include <fcntl.h>`, `#include <sys/wait.h>`.

- [ ] **Step 4: Run the tests**

Run: `make test`
Expected: three binaries, 0 failures each.

- [ ] **Step 5: Add to SRCS, add the shutdown hook, build**

Makefile `SRCS`: append `src/sound/video_player.c` after `src/sound/video_ipc.c`.

`src/kew.c`, in `kew_shutdown()` (line 252), insert as the first line of the body, and add `#include "sound/video_player.h"` near the other `sound/` includes:

```c
        video_player_shutdown();
```

Run: `make -j$(nproc) PREFIX=$HOME/.local && make install PREFIX=$HOME/.local`
Expected: builds cleanly with no warnings from the new files.

- [ ] **Step 6: Commit**

```sh
git add Makefile tests/test_video_player_env.c src/sound/video_player.c src/sound/video_player.h src/kew.c
git commit -m "Add the mpv video player that drives a placement script"
```

---

### Task 5: Video codec backend

**Files:**
- Create: `src/sound/video_decoder.h`, `src/sound/video_decoder.c`
- Create: `tests/test_video_decoder.c`
- Modify: `src/sound/audiotypes.h:95-103` (enum), `src/sound/decoders.c` (wrappers, table, `find_codec_ops` at 427), `src/sound/sound.c:1201-1266` (`init_audio_data_from_codec_decoder`), `src/sound/playback.c:163-220`, `src/sound/volume.c:19-30`, `Makefile`

**Interfaces:**
- Consumes: `video_player_*` (Task 4), `is_video_path` (Task 1).
- Produces: `enum decoder_type_t` gains `VIDEO` before `NONE`; codec entry with `.extension = "kewvideo"` (never matched by suffix; `find_codec_ops` routes by `is_video_path`).

```c
typedef struct {
        ma_data_source_base ds;
        ma_uint32 channels;
        ma_uint32 sample_rate;
        ma_uint64 cursor;
        ma_uint64 length;        /* frames, from probed duration */
        char path[4096];
        bool started;
        bool failed;
} ma_video;

ma_result ma_video_init_file(const char *path, const ma_decoding_backend_config *cfg, ma_video *v);
void      ma_video_uninit(ma_video *v);
ma_result ma_video_read_pcm_frames(ma_video *v, void *out, ma_uint64 count, ma_uint64 *read);
ma_result ma_video_seek_to_pcm_frame(ma_video *v, ma_uint64 frame);
ma_result ma_video_get_data_format(ma_video *v, ma_format *f, ma_uint32 *ch, ma_uint32 *sr, ma_channel *map, size_t cap);
ma_result ma_video_get_cursor_in_pcm_frames(ma_video *v, ma_uint64 *cursor);
ma_result ma_video_get_length_in_pcm_frames(ma_video *v, ma_uint64 *length);
void      get_video_file_info(const char *filename, ma_format *format, ma_uint32 *channels, ma_uint32 *sample_rate, ma_channel *channel_map);
```

The decoder calls the player through four seams that the test replaces: `video_backend_load`, `video_backend_release`, `video_backend_poll`, `video_backend_seek`. In production they are thin wrappers around `video_player_*`; under `KEW_VIDEO_DECODER_TEST` the test file defines them.

- [ ] **Step 1: Write the failing test**

`tests/test_video_decoder.c`:

```c
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
        float buf[48000 * 2];

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

        ma_video_uninit(&v);
        CHECK(releases == 1);

        /* launch failure ends the stream at once */
        load_ok = false;
        CHECK(ma_video_init_file("/x/clip2.mkv", &cfg, &v) == MA_SUCCESS);
        CHECK(ma_video_read_pcm_frames(&v, buf, 100, &read) == MA_AT_END);
        ma_video_uninit(&v);

        KT_MAIN_END();
}
```

Makefile rule (add to `TEST_BINS` too):

```make
$(TEST_BIN_DIR)/test_video_decoder: tests/test_video_decoder.c src/sound/video_decoder.c tests/kew_test.h | $(TEST_BIN_DIR)
	$(CC) $(TEST_CFLAGS) -DKEW_VIDEO_DECODER_TEST -o $@ tests/test_video_decoder.c src/sound/video_decoder.c -lm -lpthread -ldl
```

Run: `make test`
Expected: FAIL, missing header.

- [ ] **Step 2: Add `VIDEO` to the decoder type enum**

`src/sound/audiotypes.h:95-103`:

```c
enum decoder_type_t {
        PCM,
        BUILTIN,
        VORBIS,
        OPUS,
        M4A,
        WEBM,
        VIDEO,
        NONE
};
```

(`NONE` must stay last: code compares against it.)

- [ ] **Step 3: Implement the decoder**

`src/sound/video_decoder.h`:

```c
/**
 * @file video_decoder.h
 * @brief A miniaudio data source that plays silence while mpv shows a video.
 *
 * The sound engine treats it like any other codec: it is initialised when a
 * track is probed or preloaded, read from the decode thread, seeked, and
 * uninitialised when the track is dropped. mpv is only started on the first
 * read, so preloading the next track does not open a window early.
 */
#ifndef VIDEO_DECODER_H
#define VIDEO_DECODER_H

#include "video_ipc.h"

#include <miniaudio.h>
#include <stdbool.h>

#define VIDEO_SAMPLE_RATE 48000
#define VIDEO_CHANNELS 2
/* Seconds of silence allowed past the probed length before giving up on eof. */
#define VIDEO_END_GRACE_SECONDS 5

typedef struct {
        ma_data_source_base ds;
        ma_uint32 channels;
        ma_uint32 sample_rate;
        ma_uint64 cursor;
        ma_uint64 length;
        char path[4096];
        bool started;
        bool failed;
} ma_video;

ma_result ma_video_init_file(const char *path, const ma_decoding_backend_config *cfg, ma_video *v);
void ma_video_uninit(ma_video *v);
ma_result ma_video_read_pcm_frames(ma_video *v, void *out, ma_uint64 count, ma_uint64 *read);
ma_result ma_video_seek_to_pcm_frame(ma_video *v, ma_uint64 frame);
ma_result ma_video_get_data_format(ma_video *v, ma_format *f, ma_uint32 *ch, ma_uint32 *sr,
                                   ma_channel *map, size_t cap);
ma_result ma_video_get_cursor_in_pcm_frames(ma_video *v, ma_uint64 *cursor);
ma_result ma_video_get_length_in_pcm_frames(ma_video *v, ma_uint64 *length);

/** @brief Same shape as the other get_*_file_info helpers in audio_file_info.h. */
void get_video_file_info(const char *filename, ma_format *format, ma_uint32 *channels,
                         ma_uint32 *sample_rate, ma_channel *channel_map);

/* Seams to the player; tests provide their own. */
bool video_backend_load(const char *path, const void *owner);
void video_backend_release(const void *owner);
void video_backend_poll(VideoIpcStatus *out, bool *gone);
void video_backend_seek(double seconds);
double video_backend_probe_duration(const char *path);

#endif
```

`src/sound/video_decoder.c`:

```c
#include "video_decoder.h"

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
        *gone = video_player_is_gone();
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
```

Add `#include <stdio.h>` for `snprintf`.

- [ ] **Step 4: Run the decoder test**

Run: `make test`
Expected: `tests/test_video_decoder.c: ... 0 failures`. If miniaudio fails to compile under those `MA_NO_*` defines, drop `MA_NO_GENERATION` first, then `MA_NO_ENCODING`; keep `MA_NO_DEVICE_IO` so the test needs no audio backend.

- [ ] **Step 5: Register the codec in `decoders.c`**

Add `#include "video_decoder.h"` and `#include "utils/video_ext.h"` at the top of `src/sound/decoders.c`.

Add wrappers near the webm wrappers (after `uninit_webm_decoder`, around line 260):

```c
static ma_result init_video_decoder(const char *filepath, const ma_decoding_backend_config *config, void *decoder)
{
        return ma_video_init_file(filepath, config, (ma_video *)decoder);
}

static void uninit_video_decoder(void *decoder)
{
        ma_video_uninit((ma_video *)decoder);
}

static ma_result video_get_data_format_wrapper(ma_data_source *p, ma_format *f, ma_uint32 *ch,
                                               ma_uint32 *sr, ma_channel *map, size_t cap)
{
        return ma_video_get_data_format((ma_video *)p, f, ch, sr, map, cap);
}

static ma_result video_seek_to_pcm_frame_wrapper(void *decoder, long long frame_index, ma_seek_origin origin)
{
        (void)origin;
        return ma_video_seek_to_pcm_frame((ma_video *)decoder, (ma_uint64)frame_index);
}

static ma_result video_get_cursor_wrapper(void *p, long long *cursor)
{
        ma_uint64 c = 0;
        ma_result r = ma_video_get_cursor_in_pcm_frames((ma_video *)p, &c);
        *cursor = (long long)c;
        return r;
}

static void setup_video(void *decoder, void *firstDecoder)
{
        (void)decoder;
        (void)firstDecoder;
}
```

Append a table entry at the end of `codec_ops_list[]` (after the `#endif` of the FAAD block, before the closing `};`):

```c
    {"kewvideo", {
        .get_file_info    = get_video_file_info,
        .get_decoder_format = (decoder_format_func)video_get_data_format_wrapper,
        .seek_to_pcm_frame  = video_seek_to_pcm_frame_wrapper,
        .get_cursor       = video_get_cursor_wrapper,
        .decoder_type         = VIDEO,
        .supportsGapless  = false,
        .setup_decoder    = setup_video,
        .decoderSize      = sizeof(ma_video),
        .init             = (init_func)init_video_decoder,
        .uninit           = (uninit_func)uninit_video_decoder
    }},
```

Change `find_codec_ops` (line 427) so video wins before any suffix match:

```c
const CodecOps *find_codec_ops(const char *file_path)
{
        if (is_video_path(file_path))
                return get_codec_ops(VIDEO);

        if (is_decoder_native(file_path))
                return &codec_ops_list[0].ops;

        for (size_t i = 1; i < sizeof(codec_ops_list) / sizeof(codec_ops_list[0]); i++) {
                if (path_ends_with(file_path, codec_ops_list[i].extension))
                        return &codec_ops_list[i].ops;
        }
        return NULL;
}
```

- [ ] **Step 6: Teach the engine the new type**

`src/sound/sound.c`, in `init_audio_data_from_codec_decoder` (line 1201), add a case before `default:` and `#include "video_decoder.h"` at the top:

```c
        case VIDEO: {
                ma_video *d = (ma_video *)decoder;
                ma_video_get_data_format(d, &sound->format, &sound->channels,
                                         &sound->sample_rate, channel_map, MA_MAX_CHANNELS);
                ((ma_data_source_base *)d)->pCurrent = d;
                break;
        }
```

- [ ] **Step 7: Mirror pause, resume, stop and volume**

`src/sound/playback.c`: add `#include "video_player.h"`. Then:

In `stop_playback` (line 163), add as the last line of the body:

```c
        video_player_set_paused(true);
```

In `sound_resume_playback` (line 173), directly before `sound_s->state = SOUND_STATE_PLAYING;`:

```c
        video_player_set_paused(false);
```

In `request_pause_playback` (line 199), add as the last line of the body:

```c
        video_player_set_paused(true);
```

In `pause_playback` (line 213), add as the last line of the body:

```c
        video_player_set_paused(true);
```

`src/sound/volume.c`, `set_current_volume` (line 19): add `#include "video_player.h"` and, as the last line of the body:

```c
        video_player_set_volume((int)(volume * 100.0f + 0.5f));
```

Note: `request_pause_playback` sets the volume to 0 and `sound_resume_playback` restores it, so mpv is muted while paused and restored on resume. That is the intended behaviour.

- [ ] **Step 8: Clean-build, install, and try a real file**

Makefile `SRCS`: append `src/sound/video_decoder.c` after `src/sound/video_player.c`.

```sh
make clean && make -j$(nproc) PREFIX=$HOME/.local && make install PREFIX=$HOME/.local && cmp ~/.local/bin/kew ./kew && echo OK
```

Then a throwaway end-to-end check without Hyprland placement. Put this script at `$HOME/.config/kew/video-test.sh` (delete it after the task):

```sh
#!/usr/bin/env bash
[[ "$KEW_VIDEO_ACTION" == "start" ]] || exit 0
exec mpv --input-ipc-server="$KEW_VIDEO_SOCKET" --no-terminal --really-quiet \
     --no-osc --no-osd-bar --input-default-bindings=no --input-vo-keyboard=no \
     --force-window=yes --keep-open=no --idle=yes -- "$KEW_VIDEO_FILE"
```

`chmod +x` it, set `videoCommand=$HOME/.config/kew/video-test.sh` in `~/.config/kew/kewrc`, put one `.mp4` with sound into the library directory, Shift+Q the running kew, Super+F5, find the file in the library, press Enter.

Expected: an mpv window opens and plays with sound; kew's time line advances; Space pauses both; `a`/`d` seek both; `l` (next) closes mpv when the next entry is audio, and audio plays through kew; Shift+Q leaves no `mpv` process (`pgrep -a mpv`). If any of these fail, fix before committing; `~/.config/kew/kew.log` (via `k_log`) shows the `video_player:` lines.

- [ ] **Step 9: Commit**

```sh
git add Makefile tests/test_video_decoder.c src/sound/video_decoder.c src/sound/video_decoder.h src/sound/audiotypes.h src/sound/decoders.c src/sound/sound.c src/sound/playback.c src/sound/volume.c
git commit -m "Play video files through a silent codec that drives mpv"
```

---

### Task 6: Waveform for video files

**Files:**
- Modify: `src/sound/waveform.c:243-395` (`worker_main`)

**Interfaces:**
- Consumes: `is_video_path`, `cache_file_for` (static in the same file), `find_codec_ops`.

- [ ] **Step 1: Add the extraction helper above `worker_main`**

```c
/* Extracts the audio track of a video into a small mono wav next to the
 * cache entry. Returns false when ffmpeg is missing or fails. */
static bool extract_audio_to_wav(const char *video_path, char *wav_out, size_t wav_size)
{
        char cache_path[KEW_PATH_MAX];
        if (!cache_file_for(video_path, cache_path, sizeof(cache_path)))
                return false;
        if (snprintf(wav_out, wav_size, "%s.wav", cache_path) >= (int)wav_size)
                return false;

        pid_t pid = fork();
        if (pid < 0)
                return false;
        if (pid == 0) {
                int devnull = open("/dev/null", O_RDWR);
                if (devnull >= 0) {
                        dup2(devnull, STDIN_FILENO);
                        dup2(devnull, STDOUT_FILENO);
                        dup2(devnull, STDERR_FILENO);
                }
                execlp("ffmpeg", "ffmpeg", "-nostdin", "-loglevel", "error", "-y",
                       "-i", video_path, "-vn", "-ac", "1", "-ar", "8000",
                       "-f", "wav", wav_out, (char *)NULL);
                _exit(127);
        }

        int wstatus = 0;
        while (waitpid(pid, &wstatus, 0) < 0 && errno == EINTR)
                ;
        if (!WIFEXITED(wstatus) || WEXITSTATUS(wstatus) != 0) {
                remove(wav_out);
                return false;
        }
        return true;
}
```

Add includes at the top of `waveform.c` if missing: `<errno.h>`, `<fcntl.h>`, `<sys/wait.h>`, `<unistd.h>`, and `"utils/video_ext.h"`.

- [ ] **Step 2: Use it in `worker_main`**

Replace the two lines

```c
        const CodecOps *ops = find_codec_ops(path);
        if (!ops) {
```

with

```c
        char wav_path[KEW_PATH_MAX] = "";
        const char *decode_path = path;
        if (is_video_path(path)) {
                if (!extract_audio_to_wav(path, wav_path, sizeof(wav_path))) {
                        k_log("waveform: ffmpeg extraction failed for '%s'\n", path);
                        free(path);
                        return NULL;
                }
                decode_path = wav_path;
        }

        const CodecOps *ops = find_codec_ops(decode_path);
        if (!ops) {
                if (wav_path[0])
                        remove(wav_path);
```

Then, still in `worker_main`, change `ops->init(path, &config, decoder)` to `ops->init(decode_path, &config, decoder)`, and add `if (wav_path[0]) remove(wav_path);` before every `free(path); return NULL;` that follows, and before the final `free(path);` at the end of the function.

The wav path is a `.wav` file so `find_codec_ops` returns the builtin decoder; `cache_store(path, ...)` still keys the envelope on the video path.

- [ ] **Step 3: Build and verify**

```sh
make -j$(nproc) PREFIX=$HOME/.local && make install PREFIX=$HOME/.local
```

With the Task 5 test script still configured, relaunch kew and play an `.mkv`. Expected: the waveform appears within a few seconds; `ls ~/.cache/kew/waveform/` shows a new `.wf` and no leftover `.wav`. Play it again after restarting kew: the waveform is instant (cache hit, no ffmpeg run in `pgrep -a ffmpeg`).

- [ ] **Step 4: Commit**

```sh
git add src/sound/waveform.c
git commit -m "Build the waveform for video files from an ffmpeg audio extract"
```

---

### Task 7: UI: cover area becomes the placement rectangle

**Files:**
- Modify: `src/ui/components.c:1190-1226` (`component_cover_centered`), `:576-579` (library row suffix)

**Interfaces:**
- Consumes: `video_player_set_rect`, `VideoRect`, `is_video_path`, `model->term_size` (`TermSize` with `width_pixels`, `height_pixels`), `model->term_w`, `model->term_h`.

- [ ] **Step 1: Hand the rectangle to the player instead of drawing**

Add `#include "sound/video_player.h"` and `#include "utils/video_ext.h"` to `components.c`. In `component_cover_centered`, directly after the `if (!state->settings.coverEnabled || !songdata)` early return, insert:

```c
        if (is_video_path(songdata->file_path)) {
                VideoRect r = {
                    .row = region.row,
                    .col = region.col,
                    .rows = region.height,
                    .cols = region.width,
                    .term_rows = model->term_h,
                    .term_cols = model->term_w,
                    .term_px_w = model->term_size.width_pixels,
                    .term_px_h = model->term_size.height_pixels,
                };
                video_player_set_rect(&r);
                return (ComponentMsg){0};
        }
```

Do the same in `component_landscape_cover` (line 1228 onward), after its `coverEnabled` check, so the landscape layout also blanks and reports its region.

- [ ] **Step 2: Keep the extension on video names in the library**

`components.c:576`:

```c
                        // playlist icon, and videos keep their extension so
                        // they can be told from songs
                        if (is_m3u_file(entry) || is_video_path(entry->name)) {
                                strip_unneeded_chars = false;
                                strip_suffix = false;
                        }
```

Leave the `is_m3u_file`-only branches at lines 546 and 662 alone.

- [ ] **Step 3: Build, install, verify**

```sh
make -j$(nproc) PREFIX=$HOME/.local && make install PREFIX=$HOME/.local
```

Relaunch kew, play a video. Expected: the cover area is empty, `~/.config/kew/kew.log` shows nothing new (the test script ignores `place`), library rows show `clip.mp4` with its extension while songs still have theirs stripped. Resize the window: no crash, layout intact.

- [ ] **Step 4: Commit**

```sh
git add src/ui/components.c
git commit -m "Leave the cover area empty for videos and report it to the player"
```

---

### Task 8: Hyprland script, window rules, kewrc, hide/show

**Files (all outside the repo):**
- Create: `~/.config/hypr/hyprland/scripts/kew-video.sh`
- Modify: `~/.config/hypr/hyprland/rules.lua` (near lines 33 and 68)
- Modify: `~/.config/hypr/hyprland/scripts/music.sh`
- Modify: `~/.config/kew/kewrc`
- Modify (repo): `CLAUDE.md` (one paragraph), `docs/superpowers/specs/2026-09-10-mpv-video-overlay-design.md` (status line)

**Interfaces:**
- Consumes: the environment variables from `video_player_build_env`.

- [ ] **Step 1: Write `kew-video.sh`**

```bash
#!/usr/bin/env bash
# kew's videoCommand. Started by kew with KEW_VIDEO_ACTION=start (launch mpv
# on KEW_VIDEO_FILE) or =place (move the mpv window over kew's cover area).
# kew passes the cover area in terminal cells; this script turns it into
# pixels using the kew window geometry from Hyprland.
#
# Window classes: kew itself is syny.kew (music.sh), the overlay is
# syny.kewvideo. rules.lua makes the overlay float, borderless, unfocusable
# and fully opaque.
set -u

KEW_CLASS=syny.kew
VID_CLASS=syny.kewvideo
PAD=10   # window-padding-x/y in ~/.config/ghostty/kew/config
BG='#0e1511'

kew_json() { hyprctl clients -j | jq -c --arg c "$KEW_CLASS" '.[] | select(.class == $c)' | head -n1; }
vid_json() { hyprctl clients -j | jq -c --arg c "$VID_CLASS" '.[] | select(.class == $c)' | head -n1; }

place() {
  local kew vid
  kew=$(kew_json); vid=$(vid_json)
  [[ -n "$kew" && -n "$vid" ]] || exit 0

  local kx ky kw kh
  read -r kx ky kw kh < <(jq -r '"\(.at[0]) \(.at[1]) \(.size[0]) \(.size[1])"' <<<"$kew")

  # Cell size: prefer the pixel size kew reported, else derive from the window.
  local px_w=${KEW_TERM_PX_W:--1} px_h=${KEW_TERM_PX_H:--1}
  if (( px_w <= 0 || px_h <= 0 )); then
    px_w=$(( kw - 2 * PAD )); px_h=$(( kh - 2 * PAD ))
  fi
  local cols=${KEW_TERM_COLS:-80} rows=${KEW_TERM_ROWS:-24}
  local cell_w cell_h
  cell_w=$(awk -v a="$px_w" -v b="$cols" 'BEGIN { print a / b }')
  cell_h=$(awk -v a="$px_h" -v b="$rows" 'BEGIN { print a / b }')

  # The cover area is KEW_VIDEO_ROWS tall; use a square of that height,
  # centred horizontally inside KEW_VIDEO_COLS.
  local side x y
  side=$(awk -v r="$KEW_VIDEO_ROWS" -v ch="$cell_h" 'BEGIN { printf "%d", r * ch }')
  x=$(awk -v kx="$kx" -v pad="$PAD" -v c="$KEW_VIDEO_COL" -v n="$KEW_VIDEO_COLS" -v cw="$cell_w" -v s="$side" \
        'BEGIN { printf "%d", kx + pad + c * cw + (n * cw - s) / 2 }')
  y=$(awk -v ky="$ky" -v pad="$PAD" -v r="$KEW_VIDEO_ROW" -v ch="$cell_h" \
        'BEGIN { printf "%d", ky + pad + r * ch }')

  local ws
  ws=$(jq -r '.workspace.id' <<<"$kew")
  hyprctl dispatch "hl.dsp.window.set_prop({ window = \"class:$VID_CLASS\", prop = \"no_anim\", value = 1 })" >/dev/null
  hyprctl dispatch "hl.dsp.window.move({ workspace = $ws, window = \"class:$VID_CLASS\" })" >/dev/null
  hyprctl dispatch "hl.dsp.window.pin({ window = \"class:$VID_CLASS\", action = \"on\" })" >/dev/null
  hyprctl dispatch "hl.dsp.window.resize({ window = \"class:$VID_CLASS\", x = $side, y = $side, exact = true })" >/dev/null
  hyprctl dispatch "hl.dsp.window.move({ window = \"class:$VID_CLASS\", x = $x, y = $y, exact = true })" >/dev/null
  hyprctl dispatch "hl.dsp.window.alterzorder({ window = \"class:$VID_CLASS\", order = \"top\" })" >/dev/null
  hyprctl dispatch "hl.dsp.window.set_prop({ window = \"class:$VID_CLASS\", prop = \"no_anim\", value = 0 })" >/dev/null
  # Keep the keyboard on kew.
  hyprctl dispatch "hl.dsp.focus({ window = \"class:$KEW_CLASS\" })" >/dev/null
}

start() {
  pkill -f -- "--class=$VID_CLASS" 2>/dev/null
  rm -f -- "$KEW_VIDEO_SOCKET"
  mpv --input-ipc-server="$KEW_VIDEO_SOCKET" \
      --class="$VID_CLASS" --title=kew-video \
      --no-terminal --really-quiet --no-osc --no-osd-bar \
      --input-default-bindings=no --input-vo-keyboard=no --input-cursor=no \
      --no-border --force-window=yes --keep-open=no --idle=yes \
      --background-color="$BG" \
      -- "$KEW_VIDEO_FILE" &
  for _ in $(seq 1 40); do
    sleep 0.05
    [[ -n "$(vid_json)" ]] && break
  done
  place
}

case "${KEW_VIDEO_ACTION:-}" in
  start) start ;;
  place) place ;;
  *) echo "kew-video.sh: KEW_VIDEO_ACTION must be start or place" >&2; exit 2 ;;
esac
```

`chmod +x` it. The dispatcher syntax (`hl.dsp.window.move`, `pin`, `set_prop`) mirrors what `music.sh` already uses; check `alterzorder` and `resize` with `hyprctl dispatch --help` or the existing configuration in `~/.config/hypr` before relying on them, and adapt the two lines if this Hyprland version spells them differently.

- [ ] **Step 2: Window rules**

`~/.config/hypr/hyprland/rules.lua`, after line 33 (`syny\\.kew` opacity rule):

```lua
-- mpv overlay that kew places over its cover art (kew-video.sh)
hl.window_rule({ match = { class = "^(syny\\.kewvideo)$" }, opacity = "1.0", no_blur = true })
```

and after line 68 (`syny\\.kew` float rule):

```lua
hl.window_rule({ match = { class = "^(syny\\.kewvideo)$" }, float = true, no_border = true, no_rounding = true, no_shadow = true, no_focus = true, no_initial_focus = true })
```

Reload Hyprland config (`hyprctl reload`). If a rule key is rejected in the log, check the key names in the existing `rules.lua` and adjust.

- [ ] **Step 3: music.sh moves both windows**

In `~/.config/hypr/hyprland/scripts/music.sh`:

After `KEW='class:syny.kew'` add:

```bash
VID='class:syny.kewvideo'
vid_present() { hyprctl clients -j | jq -e '.[] | select(.class == "syny.kewvideo")' >/dev/null 2>&1; }
```

In `hide()`, after the `pin off` line for kew, add:

```bash
  if vid_present; then
    hyprctl dispatch "hl.dsp.window.set_prop({ window = \"$VID\", prop = \"no_anim\", value = 1 })" >/dev/null
    hyprctl dispatch "hl.dsp.window.pin({ window = \"$VID\", action = \"off\" })"
    hyprctl dispatch "hl.dsp.window.move({ workspace = \"special:music\", follow = false, window = \"$VID\" })"
  fi
```

In `show()`, after `hyprctl dispatch "hl.dsp.focus({ window = \"$KEW\" })"`, add:

```bash
  if vid_present; then
    KEW_VIDEO_ACTION=place KEW_VIDEO_ROW=${KEW_VIDEO_ROW:-0} \
      "$HOME/.config/hypr/hyprland/scripts/kew-video.sh"
  fi
```

The `place` call from `show` lacks kew's cell variables, so make `kew-video.sh` remember them: in `place()`, at the top, add

```bash
  local state="$XDG_RUNTIME_DIR/kew-video-rect"
  if [[ -n "${KEW_VIDEO_ROWS:-}" ]]; then
    printf 'KEW_VIDEO_ROW=%s\nKEW_VIDEO_COL=%s\nKEW_VIDEO_ROWS=%s\nKEW_VIDEO_COLS=%s\nKEW_TERM_ROWS=%s\nKEW_TERM_COLS=%s\nKEW_TERM_PX_W=%s\nKEW_TERM_PX_H=%s\n' \
      "$KEW_VIDEO_ROW" "$KEW_VIDEO_COL" "$KEW_VIDEO_ROWS" "$KEW_VIDEO_COLS" \
      "$KEW_TERM_ROWS" "$KEW_TERM_COLS" "${KEW_TERM_PX_W:--1}" "${KEW_TERM_PX_H:--1}" > "$state"
  elif [[ -r "$state" ]]; then
    # shellcheck disable=SC1090
    source "$state"
  else
    exit 0
  fi
```

and simplify the `show()` snippet to `KEW_VIDEO_ACTION=place "$HOME/.config/hypr/hyprland/scripts/kew-video.sh"`.

- [ ] **Step 4: kewrc**

In `~/.config/kew/kewrc` set:

```
videoCommand=$HOME/.config/hypr/hyprland/scripts/kew-video.sh
videoExtensions=mp4|mkv|mov|avi|m4v
```

Delete `~/.config/kew/video-test.sh` from Task 5.

- [ ] **Step 5: Run the manual checklist**

Shift+Q the running kew, Super+F5. Then, ticking each:

- [ ] Play an mp4: the video appears inside the cover square, kew's metadata, waveform and time line stay visible around it, keys still go to kew.
- [ ] Play an mkv next (`l`): the same mpv window switches file without flicker; waveform appears.
- [ ] Next to an audio track: mpv window closes, sound comes through kew.
- [ ] Back to a video with `h`: window returns.
- [ ] Space pauses video and sound; Space resumes.
- [ ] `a` and `d` seek; the playhead and the video jump together.
- [ ] Volume keys change mpv's volume.
- [ ] `q` hides both windows; sound continues; Super+F5 brings both back on the square.
- [ ] Resize the kew window with the mouse edge: the overlay follows on the next redraw.
- [ ] `pkill mpv` while a video plays: kew moves to the next track within a second.
- [ ] Shift+Q: `pgrep -a mpv` is empty.
- [ ] Set `videoCommand=` empty, restart: videos are gone from the library, everything else as before.

Fix anything that fails before the final commit; script placement maths is the likely culprit, and `hyprctl clients -j` shows the actual window rectangles to compare against.

- [ ] **Step 6: Document and commit**

Add to the end of `CLAUDE.md` in the repo:

```markdown
## Video overlay

Video files play through mpv in a window that Hyprland pins over the
cover-art square. kew side: `src/sound/video_*.c`, kewrc `videoCommand`
and `videoExtensions`. Hyprland side:
`~/.config/hypr/hyprland/scripts/kew-video.sh` (started by kew with
`KEW_VIDEO_*` environment variables), window rules for class
`syny.kewvideo` in `rules.lua`, and `music.sh` hides and shows both
windows. Unit tests for the pure parts: `make test`.
```

Change the spec's status line to `Status: implemented.`

```sh
git add CLAUDE.md docs/superpowers/specs/2026-09-10-mpv-video-overlay-design.md docs/superpowers/plans/2026-09-10-mpv-video-overlay.md
git commit -m "Document the mpv video overlay"
```

---

## Self-review

**Spec coverage.** Classification and settings: Task 1. Codec backend and control mirroring: Task 5. mpv process and IPC: Tasks 3 and 4. Clock: nothing to do by design. Waveform: Task 6. UI: Task 7. Script, rules, hide/show, kewrc: Task 8. Error table: launch failure and socket loss end the stream (Task 5 decoder), empty `videoCommand` disables scanning (Task 1), ffmpeg missing leaves no waveform (Task 6), missing pixel size handled in the script (Task 8). Tests: `is_video_path` (Task 1), scanner (Task 3), env block (Task 4), silence decoder (Task 5), manual checklist (Task 8).

**Placeholders.** None; every code step carries its code.

**Type consistency.** `VideoRect` fields match between `video_player.h`, the env builder test and `components.c`. `VideoIpcStatus` is defined once in `video_ipc.h` and consumed by the player and decoder. Seam names `video_backend_*` match between `video_decoder.h`, `video_decoder.c` and the decoder test. `video_player_build_env` takes `char out[][160]` everywhere.
