// fh2-radio-native-helper: tiny native Linux process that runs yt-dlp / ffmpeg
// for FH2UniversalRadio.dll when the game runs under Wine/Proton.
//
// Why: under Wine every CreateProcess of yt-dlp.exe (PyInstaller) / ffmpeg.exe
// boots a full Wine process (wineserver round-trips, PE loader, PyInstaller
// unpack through Wine file I/O) and every PCM byte crosses two Wine named
// pipes, i.e. wineserver. The game shares that wineserver. Running the
// native Linux builds and streaming over a loopback TCP socket keeps all of
// that off Wine entirely.
//
// Why a helper instead of CreateProcess("/path/to/elf") from the DLL: Wine
// can exec a Unix binary from CreateProcess (ntdll fork_and_exec), but only
// hands it stdin/stdout when they map to real Unix fds. Wine pipes are
// wineserver pseudo-fds, so the child's stdio silently becomes /dev/null, and
// CREATE_NO_WINDOW also forces /dev/null. No process handle comes back either.
// So the DLL launches this helper once (output not needed) and talks TCP.
//
// Invocation (done by the DLL):
//   fh2-radio-native-helper --port N --token HEX --yt-dlp PATH --ffmpeg PATH
//                           [--log PATH]
//
// Protocol (one request per connection, 127.0.0.1 only):
//   "FH2N/1\n" <token>"\n" <op>"\n" <nstages>"\n"
//   per stage: <tool>"\n" <argc>"\n" then argc x (<len>"\n" <len bytes>)
//   op "hello": reply "OK\n" and keep the connection open. This is the
//               keepalive: when it closes (game exit/crash) the helper kills
//               every child and exits. No hello within 20 s -> exit.
//   op "run":   spawn the stages as a pipeline (stage i stdout -> stage i+1
//               stdin); the LAST stage's stdout IS the socket, so data flows
//               child -> DLL with no copy through the helper. When the client
//               closes the socket the whole pipeline is SIGKILLed.
// Tools are an allowlist ("yt-dlp", "ffmpeg") mapped to the paths given on
// the command line; the client can never name an arbitrary binary.
//
// Build (static, no runtime deps; see native/build.sh):
//   zig cc -target x86_64-linux-musl -static -fno-pie -no-pie -O2
//          -o fh2-radio-native-helper fh2_native_helper.c
// It must NOT be a static-PIE (ET_DYN without PT_INTERP): Wine loads those as
// builtin libraries instead of exec()ing them.

#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

// macOS shims: only so the protocol can be smoke-tested on a dev Mac. The
// shipped binary is Linux-only.
#ifdef __APPLE__
#define SOCK_CLOEXEC 0
#define POLLRDHUP 0
static int accept4(int s, struct sockaddr* a, socklen_t* l, int f) {
    (void)f;
    int c = accept(s, a, l);
    if (c >= 0) fcntl(c, F_SETFD, FD_CLOEXEC);
    return c;
}
static int pipe2(int p[2], int f) {
    (void)f;
    if (pipe(p) < 0) return -1;
    fcntl(p[0], F_SETFD, FD_CLOEXEC);
    fcntl(p[1], F_SETFD, FD_CLOEXEC);
    return 0;
}
#endif

#define MAX_STAGES 4
#define MAX_ARGS 64
#define MAX_ARG_LEN 8192
#define MAX_JOBS 32

static const char* g_token;
static const char* g_ytdlp;
static const char* g_ffmpeg;
static int g_log_fd = 2;

static void logf_(const char* fmt, ...) {
    char buf[1024];
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    int n = (int)strftime(buf, sizeof buf, "[%H:%M:%S] native-helper: ", &tm);
    va_list ap;
    va_start(ap, fmt);
    n += vsnprintf(buf + n, sizeof buf - (size_t)n, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof buf - 2) n = (int)sizeof buf - 2;
    buf[n++] = '\n';
    (void)!write(g_log_fd, buf, (size_t)n);
}

// --- one running pipeline ---------------------------------------------------
struct job {
    int used;
    int sock;    // our copy of the client socket (watched for hang-up)
    pid_t pgid;  // process group of the pipeline
    int nprocs;  // children still alive
    pid_t pids[MAX_STAGES];
};
static struct job g_jobs[MAX_JOBS];

static void kill_job(struct job* j, const char* why) {
    if (!j->used) return;
    if (j->nprocs > 0 && j->pgid > 0) {
        kill(-j->pgid, SIGKILL);
        logf_("pipeline pgid %d killed (%s)", (int)j->pgid, why);
    }
    // Children are reaped in reap_children(); just forget the socket now.
    if (j->sock >= 0) close(j->sock);
    j->sock = -1;
    if (j->nprocs <= 0) j->used = 0;
}

static void reap_children(void) {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < MAX_JOBS; ++i) {
            struct job* j = &g_jobs[i];
            if (!j->used) continue;
            for (int k = 0; k < MAX_STAGES; ++k) {
                if (j->pids[k] != pid) continue;
                j->pids[k] = 0;
                if (--j->nprocs <= 0) {
                    // Pipeline finished: drop our socket copy so the client
                    // sees EOF as soon as the last writer is gone.
                    if (j->sock >= 0) close(j->sock);
                    j->sock = -1;
                    j->used = 0;
                }
            }
        }
    }
}

// --- request parsing --------------------------------------------------------
// Whole-request deadline: a client that trickles bytes can't stall the
// (single-threaded) accept loop for longer than this.
#define REQ_TIMEOUT_S 3
static time_t g_req_deadline;

static int read_full(int fd, char* p, size_t n) {
    while (n) {
        if (g_req_deadline && time(NULL) > g_req_deadline) return -1;
        ssize_t r = read(fd, p, n);
        if (r <= 0) {
            if (r < 0 && errno == EINTR) continue;
            return -1;
        }
        p += r;
        n -= (size_t)r;
    }
    return 0;
}

// Reads one '\n'-terminated line (without the '\n') of at most cap-1 bytes.
static int read_line(int fd, char* out, size_t cap) {
    size_t n = 0;
    for (;;) {
        char c;
        if (read_full(fd, &c, 1) < 0) return -1;
        if (c == '\n') break;
        if (n + 1 >= cap) return -1;
        out[n++] = c;
    }
    out[n] = 0;
    return 0;
}

// Constant-time compare so the token can't be guessed byte by byte.
static int token_eq(const char* got) {
    size_t a = strlen(got), b = strlen(g_token);
    unsigned char d = (unsigned char)(a != b);
    for (size_t i = 0; i < b; ++i) d |= (unsigned char)(got[i < a ? i : 0] ^ g_token[i]);
    return d == 0;
}

static long read_num(int fd, long max) {
    char line[32];
    if (read_line(fd, line, sizeof line) < 0 || !line[0]) return -1;
    char* end = NULL;
    long v    = strtol(line, &end, 10);
    if (*end || v < 0 || v > max) return -1;
    return v;
}

struct stage {
    const char* exe;
    int argc;
    char* argv[MAX_ARGS + 2];
};

static void free_stages(struct stage* st, int n) {
    for (int i = 0; i < n; ++i)
        for (int a = 1; a <= st[i].argc; ++a) free(st[i].argv[a]);
}

// Returns number of stages parsed, or -1. *op receives the op name.
static int parse_request(int fd, char* op, size_t opcap, struct stage* st) {
    char line[256];
    if (read_line(fd, line, sizeof line) < 0 || strcmp(line, "FH2N/1") != 0) return -1;
    if (read_line(fd, line, sizeof line) < 0 || !token_eq(line)) return -1;
    if (read_line(fd, op, opcap) < 0) return -1;
    long nst = read_num(fd, MAX_STAGES);
    if (nst < 0) return -1;
    for (int i = 0; i < nst; ++i) {
        st[i].argc = 0;
        if (read_line(fd, line, sizeof line) < 0) goto fail_partial;
        if (!strcmp(line, "yt-dlp")) st[i].exe = g_ytdlp;
        else if (!strcmp(line, "ffmpeg")) st[i].exe = g_ffmpeg;
        else {
            logf_("rejected unknown tool '%s'", line);
            goto fail_partial;
        }
        long argc = read_num(fd, MAX_ARGS);
        if (argc < 0) goto fail_partial;
        st[i].argv[0] = (char*)st[i].exe;
        for (long a = 0; a < argc; ++a) {
            long len = read_num(fd, MAX_ARG_LEN);
            if (len < 0) goto fail_partial;
            char* s = malloc((size_t)len + 1);
            if (!s || read_full(fd, s, (size_t)len) < 0) {
                free(s);
                goto fail_partial;
            }
            s[len] = 0;
            if (memchr(s, 0, (size_t)len)) { // embedded NUL: refuse
                free(s);
                goto fail_partial;
            }
            st[i].argv[++st[i].argc] = s;
        }
        st[i].argv[st[i].argc + 1] = NULL;
        continue;
    fail_partial:
        free_stages(st, i + 1);
        return -1;
    }
    return (int)nst;
}

// --- spawning ---------------------------------------------------------------
static int start_pipeline(int sock, struct stage* st, int nst) {
    struct job* j = NULL;
    for (int i = 0; i < MAX_JOBS; ++i)
        if (!g_jobs[i].used) {
            j = &g_jobs[i];
            break;
        }
    if (!j || nst < 1) return -1;
    memset(j, 0, sizeof *j);
    j->used = 1;
    j->sock = -1;

    int devnull = open("/dev/null", O_RDWR | O_CLOEXEC);
    int prev_rd = devnull; // stage 0 reads /dev/null
    for (int i = 0; i < nst; ++i) {
        int p[2] = {-1, -1};
        const int last = (i == nst - 1);
        if (!last && pipe2(p, O_CLOEXEC) < 0) break;
        const int out = last ? sock : p[1];
        pid_t pid     = fork();
        if (pid == 0) {
            setpgid(0, j->pgid); // 0 for the first stage -> becomes the leader
            signal(SIGPIPE, SIG_DFL);
            dup2(prev_rd, 0);
            dup2(out, 1);
            dup2(g_log_fd, 2);
            execv(st[i].exe, st[i].argv);
            logf_("execv %s failed: %s", st[i].exe, strerror(errno));
            _exit(127);
        }
        if (pid < 0) {
            if (!last) {
                close(p[0]);
                close(p[1]);
            }
            break;
        }
        if (!j->pgid) j->pgid = pid;
        setpgid(pid, j->pgid); // also from the parent, avoids the race
        j->pids[i] = pid;
        j->nprocs++;
        if (prev_rd != devnull) close(prev_rd);
        if (!last) {
            close(p[1]);
            prev_rd = p[0];
        }
    }
    if (prev_rd != devnull && prev_rd >= 0) close(prev_rd);
    if (devnull >= 0) close(devnull);

    if (j->nprocs != nst) { // partial spawn: tear down what started
        j->sock = sock;
        kill_job(j, "spawn failed");
        return -1;
    }
    // Keep our copy only to notice the client hanging up (POLLRDHUP); it is
    // closed as soon as the pipeline exits so the client then sees EOF. Never
    // shutdown() it: that would act on the socket the child is writing to.
    j->sock = sock;
    logf_("pipeline pgid %d started: %d stage(s), first=%s", (int)j->pgid, nst, st[0].exe);
    return 0;
}

// --- main loop --------------------------------------------------------------
static void usage(void) {
    fprintf(stderr, "usage: fh2-radio-native-helper --port N --token HEX --yt-dlp PATH "
                    "--ffmpeg PATH [--log PATH]\n");
}

static void prepend_path(const char* exe) {
    // Let yt-dlp find a JS runtime (deno) placed next to it.
    char dir[PATH_MAX];
    snprintf(dir, sizeof dir, "%s", exe);
    char* slash = strrchr(dir, '/');
    if (!slash) return;
    *slash          = 0;
    const char* old = getenv("PATH");
    char* np        = malloc(strlen(dir) + (old ? strlen(old) : 0) + 2 + 32);
    if (!np) return;
    sprintf(np, "%s:%s", dir, old && *old ? old : "/usr/bin:/bin");
    setenv("PATH", np, 1);
    free(np);
}

int main(int argc, char** argv) {
    int port         = 0;
    const char* logp = NULL;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--port")) port = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--token")) g_token = argv[i + 1];
        else if (!strcmp(argv[i], "--yt-dlp")) g_ytdlp = argv[i + 1];
        else if (!strcmp(argv[i], "--ffmpeg")) g_ffmpeg = argv[i + 1];
        else if (!strcmp(argv[i], "--log")) logp = argv[i + 1];
    }
    if (port <= 0 || port > 65535 || !g_token || strlen(g_token) < 16 || !g_ytdlp || !g_ffmpeg) {
        usage();
        return 2;
    }
    if (logp) {
        int fd = open(logp, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
        if (fd >= 0) g_log_fd = fd;
    }

    // We inherit the Wine process environment. Drop what would leak Proton /
    // Steam overlay libraries into the native children.
    unsetenv("LD_PRELOAD");
    unsetenv("LD_LIBRARY_PATH");
    unsetenv("PYTHONHOME");
    unsetenv("PYTHONPATH");
    prepend_path(g_ytdlp);
    signal(SIGPIPE, SIG_IGN);
    // Lower priority for us and every child: on a 15 W Deck the game's
    // threads should win the CPU when yt-dlp starts up on a track change.
    errno = 0;
    if (nice(10) == -1 && errno) logf_("nice(10) failed: %s", strerror(errno));

    int ls = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    int one = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in sa = {0};
    sa.sin_family         = AF_INET;
    sa.sin_port           = htons((unsigned short)port);
    sa.sin_addr.s_addr    = htonl(INADDR_LOOPBACK);
    if (ls < 0 || bind(ls, (struct sockaddr*)&sa, sizeof sa) < 0 || listen(ls, 16) < 0) {
        logf_("cannot listen on 127.0.0.1:%d: %s", port, strerror(errno));
        return 1;
    }
    logf_("listening on 127.0.0.1:%d (yt-dlp=%s ffmpeg=%s)", port, g_ytdlp, g_ffmpeg);

    int keepalive     = -1;
    time_t started    = time(NULL);
    for (;;) {
        struct pollfd pf[2 + MAX_JOBS];
        int map[2 + MAX_JOBS];
        int n = 0;
        pf[n] = (struct pollfd){ls, POLLIN, 0};
        map[n++] = -1;
        if (keepalive >= 0) {
            pf[n] = (struct pollfd){keepalive, POLLIN, 0};
            map[n++] = -2;
        }
        for (int i = 0; i < MAX_JOBS; ++i)
            if (g_jobs[i].used && g_jobs[i].sock >= 0) {
                pf[n] = (struct pollfd){g_jobs[i].sock, POLLIN | POLLRDHUP, 0};
                map[n++] = i;
            }

        int r = poll(pf, (nfds_t)n, 200);
        reap_children();
        if (keepalive < 0 && time(NULL) - started > 20) {
            logf_("no hello from the DLL within 20 s, exiting");
            break;
        }
        if (r <= 0) continue;

        for (int k = 0; k < n; ++k) {
            if (!pf[k].revents) continue;
            if (map[k] == -1) {
                int c = accept4(ls, NULL, NULL, SOCK_CLOEXEC);
                if (c < 0) continue;
                struct timeval tv = {3, 0};
                setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
                setsockopt(c, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
                char op[16];
                struct stage st[MAX_STAGES];
                g_req_deadline = time(NULL) + REQ_TIMEOUT_S;
                int nst = parse_request(c, op, sizeof op, st);
                g_req_deadline = 0;
                if (nst < 0) {
                    logf_("rejected malformed/unauthenticated request");
                    close(c);
                    continue;
                }
                struct timeval tv0 = {0, 0};
                setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv0, sizeof tv0);
                if (!strcmp(op, "hello")) {
                    if (keepalive >= 0) close(keepalive);
                    keepalive = c;
                    (void)!write(c, "OK\n", 3);
                    logf_("DLL connected (keepalive)");
                } else if (!strcmp(op, "run")) {
                    if (start_pipeline(c, st, nst) < 0) close(c);
                } else {
                    close(c);
                }
                free_stages(st, nst);
            } else if (map[k] == -2) {
                char b[64];
                ssize_t got = recv(keepalive, b, sizeof b, MSG_DONTWAIT);
                if (got == 0 || (got < 0 && errno != EAGAIN && errno != EINTR)) {
                    logf_("DLL went away, shutting down");
                    goto out;
                }
            } else {
                struct job* j = &g_jobs[map[k]];
                char b[64];
                ssize_t got = recv(j->sock, b, sizeof b, MSG_DONTWAIT);
                if (got == 0 || (pf[k].revents & (POLLRDHUP | POLLHUP | POLLERR)) ||
                    (got < 0 && errno != EAGAIN && errno != EINTR))
                    kill_job(j, "client closed");
            }
        }
    }
out:
    for (int i = 0; i < MAX_JOBS; ++i) kill_job(&g_jobs[i], "shutdown");
    reap_children();
    return 0;
}
