#include "video_ipc.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
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

        char line[1400];
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

bool video_ipc_wait_closed(VideoIpc *ipc, int timeout_ms)
{
        if (ipc->fd < 0)
                return true;

        struct timespec start;
        clock_gettime(CLOCK_MONOTONIC, &start);

        for (;;) {
                struct timespec now;
                clock_gettime(CLOCK_MONOTONIC, &now);
                long elapsed_ms = (now.tv_sec - start.tv_sec) * 1000L +
                                  (now.tv_nsec - start.tv_nsec) / 1000000L;
                int remaining = timeout_ms - (int)elapsed_ms;
                if (remaining <= 0)
                        return false;

                struct pollfd p = {.fd = ipc->fd, .events = POLLIN};
                int r = poll(&p, 1, remaining);
                if (r < 0 && errno == EINTR)
                        continue;
                if (r <= 0)
                        return false;

                VideoIpcStatus scratch = {0};
                if (!video_ipc_poll(ipc, &scratch))
                        return true;
        }
}

void video_ipc_close(VideoIpc *ipc)
{
        if (ipc->fd >= 0)
                close(ipc->fd);
        ipc->fd = -1;
        ipc->len = 0;
}
