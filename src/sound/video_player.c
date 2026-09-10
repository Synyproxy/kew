#include "video_player.h"

#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef KEW_VIDEO_PLAYER_TEST
#include "common/appstate.h"
#include "utils/k_log.h"
#include "utils/utils.h"
#endif

#define CONNECT_TIMEOUT_MS 3000

int video_player_build_env(const VideoRect *r, const char *action, const char *file,
                           const char *socket_path_in, bool visible, char out[][160], int max)
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
        PUT("KEW_VIDEO_VISIBLE=%d", visible ? 1 : 0);
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

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static VideoIpc ipc = {.fd = -1};
static VideoIpcStatus status;
static const void *current_owner = NULL;
static bool active = false;
static bool gone = false;
static VideoRect rect;
static bool have_rect = false;
static char socket_path[256];
static char current_path[1024];
static bool visible = true;
static int last_volume = -1;

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
        int n = video_player_build_env(rect_or_default(), action, file, socket_path, visible, env, 16);
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
                        snprintf(current_path, sizeof(current_path), "%s", path);
                        /* The new file may have another aspect ratio. */
                        run_script_locked("place", current_path);
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

        snprintf(current_path, sizeof(current_path), "%s", path);
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
                /* Hanging up while mpv is mid-event makes it drop the quit. */
                if (!video_ipc_wait_closed(&ipc, 1000))
                        k_log("video_player: mpv did not close after quit\n");
                video_ipc_close(&ipc);
        }
        active = false;
        gone = false;
        current_owner = NULL;
        memset(&status, 0, sizeof(status));
}

void video_player_release(const void *owner)
{
        /* The engine drops the old decoder before the next one starts, so
         * keep mpv around: a following video reuses it with loadfile, and
         * the engine calls video_player_shutdown() when audio follows. */
        pthread_mutex_lock(&lock);
        if (active && current_owner == owner)
                current_owner = NULL;
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
        if (changed)
                k_log("video_player: rect row=%d col=%d rows=%d cols=%d term=%dx%d px=%dx%d active=%d\n",
                      r->row, r->col, r->rows, r->cols, r->term_cols, r->term_rows,
                      r->term_px_w, r->term_px_h, (int)active);
        if (changed && active && !gone && visible)
                run_script_locked("place", current_path[0] ? current_path : NULL);
        pthread_mutex_unlock(&lock);
}

void video_player_raise(void)
{
        static struct timespec last;
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long ms = (now.tv_sec - last.tv_sec) * 1000 + (now.tv_nsec - last.tv_nsec) / 1000000;
        if (last.tv_sec != 0 && ms >= 0 && ms < 300)
                return;
        last = now;

        pthread_mutex_lock(&lock);
        if (active && !gone && visible && have_rect)
                run_script_locked("raise", current_path[0] ? current_path : NULL);
        pthread_mutex_unlock(&lock);
}

void video_player_set_visible(bool v)
{
        pthread_mutex_lock(&lock);
        bool changed = visible != v;
        visible = v;
        /* Showing waits for a real rectangle: the track view reports one as
         * soon as it draws and set_rect places then. Placing from the
         * placeholder first would race that and could win. */
        if (changed && active && !gone && (!v || have_rect))
                run_script_locked(v ? "place" : "hide", current_path[0] ? current_path : NULL);
        pthread_mutex_unlock(&lock);
}

void video_player_shutdown(void)
{
        pthread_mutex_lock(&lock);
        stop_locked();
        pthread_mutex_unlock(&lock);
}

#endif /* KEW_VIDEO_PLAYER_TEST */
