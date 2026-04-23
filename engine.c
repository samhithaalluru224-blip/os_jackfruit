#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <pthread.h>

#include "monitor_ioctl.h"

#define SOCK_PATH "/tmp/engine.sock"
#define LOG_DIR "./logs"
#define STACK_SIZE (1024 * 1024)

/* ================= LOGGING ================= */

#define LOG_BUF_SIZE 64
#define LOG_LINE 256

typedef struct {
    char data[LOG_BUF_SIZE][LOG_LINE];
    int head, tail, count;
    int done;
    pthread_mutex_t lock;
    pthread_cond_t not_full;
    pthread_cond_t not_empty;
} log_buffer_t;

typedef struct {
    int pipe_fd;
    log_buffer_t *buf;
} producer_arg_t;

typedef struct {
    FILE *file;
    log_buffer_t *buf;
} consumer_arg_t;

/* ---------- buffer ---------- */

static void buffer_init(log_buffer_t *b) {
    b->head = b->tail = b->count = 0;
    b->done = 0;
    pthread_mutex_init(&b->lock, NULL);
    pthread_cond_init(&b->not_full, NULL);
    pthread_cond_init(&b->not_empty, NULL);
}

static void buffer_push(log_buffer_t *b, const char *line) {
    pthread_mutex_lock(&b->lock);

    while (b->count == LOG_BUF_SIZE)
        pthread_cond_wait(&b->not_full, &b->lock);

    strncpy(b->data[b->tail], line, LOG_LINE - 1);
    b->data[b->tail][LOG_LINE - 1] = '\0';

    b->tail = (b->tail + 1) % LOG_BUF_SIZE;
    b->count++;

    pthread_cond_signal(&b->not_empty);
    pthread_mutex_unlock(&b->lock);
}

static int buffer_pop(log_buffer_t *b, char *out) {
    pthread_mutex_lock(&b->lock);

    while (b->count == 0 && !b->done)
        pthread_cond_wait(&b->not_empty, &b->lock);

    if (b->count == 0 && b->done) {
        pthread_mutex_unlock(&b->lock);
        return 0;
    }

    strcpy(out, b->data[b->head]);
    b->head = (b->head + 1) % LOG_BUF_SIZE;
    b->count--;

    pthread_cond_signal(&b->not_full);
    pthread_mutex_unlock(&b->lock);
    return 1;
}

/* ---------- threads ---------- */

static void* producer_thread(void *arg) {
    producer_arg_t *p = arg;
    FILE *fp = fdopen(p->pipe_fd, "r");
    char line[LOG_LINE];

    while (fgets(line, sizeof(line), fp)) {
        buffer_push(p->buf, line);
    }

    pthread_mutex_lock(&p->buf->lock);
    p->buf->done = 1;
    pthread_cond_broadcast(&p->buf->not_empty);
    pthread_mutex_unlock(&p->buf->lock);

    fclose(fp);
    return NULL;
}

static void* consumer_thread(void *arg) {
    consumer_arg_t *c = arg;
    char line[LOG_LINE];

    while (buffer_pop(c->buf, line)) {
        fputs(line, c->file);
        fflush(c->file);
    }

    fclose(c->file);
    return NULL;
}

/* ================= MONITOR ================= */

static int monitor_fd = -1;

static void init_monitor() {
    monitor_fd = open("/dev/container_monitor", O_RDWR);
}

static void register_container_kernel(const char *id, pid_t pid,
                                      unsigned long soft,
                                      unsigned long hard) {
    if (monitor_fd < 0) return;

    struct monitor_request req = {0};
    req.pid = pid;
    req.soft_limit_bytes = soft;
    req.hard_limit_bytes = hard;
    strncpy(req.container_id, id, MONITOR_NAME_LEN - 1);

    ioctl(monitor_fd, MONITOR_REGISTER, &req);
}

/* ================= CHILD ================= */

struct child_args {
    char rootfs[128];
    char *argv[16];
    int write_fd;
    int nice_val;
};

static int container_main(void *arg) {
    struct child_args *cargs = arg;

    dup2(cargs->write_fd, STDOUT_FILENO);
    dup2(cargs->write_fd, STDERR_FILENO);
    close(cargs->write_fd);

    sethostname("container", 9);

    if (chroot(cargs->rootfs) != 0) {
        perror("chroot");
        return 1;
    }

    chdir("/");
    mount("proc", "/proc", "proc", 0, NULL);

    nice(cargs->nice_val);

    execvp(cargs->argv[0], cargs->argv);

    perror("exec failed");
    return 1;
}

/* ================= START ================= */

static void start_container(const char *id, char args[][64], int argc, int client_fd) {

    mkdir(LOG_DIR, 0777);

    int pipefd[2];
    pipe(pipefd);

    char *stack = malloc(STACK_SIZE);
    char *stack_top = stack + STACK_SIZE;

    struct child_args *cargs = calloc(1, sizeof(*cargs));

    /* ===== parse ===== */

    unsigned long soft = 40 * 1024 * 1024;
    unsigned long hard = 64 * 1024 * 1024;
    int nice_val = 0;

    int idx = 0;

    strcpy(cargs->rootfs, args[0]);

    for (int i = 1; i < argc; i++) {

        if (strcmp(args[i], "--nice") == 0 && i+1 < argc) {
            nice_val = atoi(args[++i]);
            continue;
        }

        if (strcmp(args[i], "--soft-mib") == 0 && i+1 < argc) {
            soft = atol(args[++i]) * 1024 * 1024;
            continue;
        }

        if (strcmp(args[i], "--hard-mib") == 0 && i+1 < argc) {
            hard = atol(args[++i]) * 1024 * 1024;
            continue;
        }

        cargs->argv[idx++] = args[i];
    }

    cargs->argv[idx] = NULL;
    cargs->nice_val = nice_val;
    cargs->write_fd = pipefd[1];

    if (idx == 0) {
        write(client_fd, "No command\n", 11);
        return;
    }

    pid_t pid = clone(container_main, stack_top,
        CLONE_NEWPID | CLONE_NEWUTS | CLONE_NEWNS | SIGCHLD, cargs);

    close(pipefd[1]);

    /* ===== logging ===== */

    log_buffer_t *buf = malloc(sizeof(log_buffer_t));
    buffer_init(buf);

    char logfile[128];
    snprintf(logfile, sizeof(logfile), "%s/%s.log", LOG_DIR, id);

    FILE *f = fopen(logfile, "w");

    pthread_t p, c;

    producer_arg_t *pa = malloc(sizeof(*pa));
    consumer_arg_t *ca = malloc(sizeof(*ca));

    pa->pipe_fd = pipefd[0];
    pa->buf = buf;

    ca->file = f;
    ca->buf = buf;

    pthread_create(&p, NULL, producer_thread, pa);
    pthread_create(&c, NULL, consumer_thread, ca);

    pthread_detach(p);
    pthread_detach(c);

    register_container_kernel(id, pid, soft, hard);

    write(client_fd, "Container started\n", 19);
}

/* ================= SUPERVISOR ================= */

static void supervisor() {

    unlink(SOCK_PATH);

    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, SOCK_PATH);

    bind(server_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 5);

    printf("Supervisor started\n");

    while (1) {
        int client_fd = accept(server_fd, NULL, NULL);

        struct {
            int cmd;
            char id[32];
            int argc;
            char args[10][64];
        } req = {0};

        read(client_fd, &req, sizeof(req));

        if (req.cmd == 1)
            start_container(req.id, req.args, req.argc, client_fd);

        close(client_fd);
    }
}

/* ================= CLIENT ================= */

static void send_req(int argc, char *argv[]) {

    int s = socket(AF_UNIX, SOCK_STREAM, 0);

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, SOCK_PATH);

    connect(s, (struct sockaddr*)&addr, sizeof(addr));

    struct {
        int cmd;
        char id[32];
        int argc;
        char args[10][64];
    } req = {0};

    req.cmd = 1;
    strcpy(req.id, argv[2]);

    req.argc = argc - 3;

    for (int i = 0; i < req.argc; i++)
        strcpy(req.args[i], argv[i + 3]);

    write(s, &req, sizeof(req));

    char buf[1024];
    int n = read(s, buf, sizeof(buf)-1);

    if (n > 0) {
        buf[n] = 0;
        printf("%s\n", buf);
    }

    close(s);
}

/* ================= MAIN ================= */

int main(int argc, char *argv[]) {

    if (strcmp(argv[1], "supervisor") == 0) {
        init_monitor();
        supervisor();
    }

    else if (strcmp(argv[1], "start") == 0) {
        send_req(argc, argv);
    }

    return 0;
}

