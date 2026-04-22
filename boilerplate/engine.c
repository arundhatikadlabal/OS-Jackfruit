#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "monitor_ioctl.h"

#define STACK_SIZE (1024 * 1024)
#define CONTAINER_ID_LEN 32
#define CONTROL_PATH "/tmp/mini_runtime.sock"
#define LOG_DIR "logs"
#define LOG_CHUNK_SIZE 4096
#define LOG_BUFFER_CAPACITY 16
//check buffer

typedef enum {
    CMD_SUPERVISOR = 0,
    CMD_START,
    CMD_RUN,
    CMD_PS,
    CMD_LOGS,
    CMD_STOP
} command_kind_t;

typedef enum {
    CONTAINER_RUNNING = 0,
    CONTAINER_STOPPED
} container_state_t;

typedef struct container_record {
    char id[CONTAINER_ID_LEN];
    pid_t host_pid;
    time_t started_at;
    container_state_t state;
    unsigned long soft_limit_bytes;
    unsigned long hard_limit_bytes;
    char log_path[256];
    struct container_record *next;
} container_record_t;

typedef struct {
    command_kind_t kind;
    char container_id[32];
    char rootfs[256];
    char command[256];
    unsigned long soft_limit_bytes;
    unsigned long hard_limit_bytes;
    int nice_value;
} control_request_t;

typedef struct {
    int status;
    char message[256];
} control_response_t;

typedef struct {
    char id[32];
    char rootfs[256];
    char command[256];
    int log_fd;
} child_config_t;

typedef struct {
    int server_fd;
    int monitor_fd;
    container_record_t *head;
    pthread_mutex_t lock;
} supervisor_t;

static supervisor_t g;

int child_fn(void *arg)
{
    child_config_t *cfg = arg;
    sethostname(cfg->id, strlen(cfg->id));
    chroot(cfg->rootfs);
    chdir("/");
    mkdir("/proc", 0555);
    mount("proc", "/proc", "proc", 0, NULL);
    dup2(cfg->log_fd, STDOUT_FILENO);
    dup2(cfg->log_fd, STDERR_FILENO);
    execl("/bin/sh", "sh", "-c", cfg->command, NULL);
    perror("exec");
    return 1;
}

void add_container(const char *id, pid_t pid,
                   unsigned long soft,
                   unsigned long hard)
{
    container_record_t *n = malloc(sizeof(*n));
    memset(n, 0, sizeof(*n));
    strcpy(n->id, id);
    n->host_pid = pid;
    n->started_at = time(NULL);
    n->state = CONTAINER_RUNNING;
    n->soft_limit_bytes = soft;
    n->hard_limit_bytes = hard;
    snprintf(n->log_path, sizeof(n->log_path),
             "logs/%s.log", id);
    pthread_mutex_lock(&g.lock);
    n->next = g.head;
    g.head = n;
    pthread_mutex_unlock(&g.lock);
}

container_record_t *find_container(const char *id)
{
    container_record_t *cur = g.head;
    while (cur) {
        if (strcmp(cur->id, id) == 0)
            return cur;
        cur = cur->next;
    }
    return NULL;
}

void send_resp(int fd, int status, const char *msg)
{
    control_response_t r;
    memset(&r, 0, sizeof(r));
    r.status = status;
    strncpy(r.message, msg, sizeof(r.message) - 1);
    write(fd, &r, sizeof(r));
}

void print_ps_to_buffer(char *buf, size_t sz)
{
    container_record_t *cur = g.head;
    char line[256];
    snprintf(buf, sz, "ID\tPID\tSTATE\n");
    while (cur) {
        snprintf(line, sizeof(line),
                 "%s\t%d\t%s\n",
                 cur->id,
                 cur->host_pid,
                 cur->state == CONTAINER_RUNNING ? "running" : "stopped");
        strncat(buf, line, sz - strlen(buf) - 1);
        cur = cur->next;
    }
}

void reap_children(void)
{
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        container_record_t *cur = g.head;
        while (cur) {
            if (cur->host_pid == pid)
                cur->state = CONTAINER_STOPPED;
            cur = cur->next;
        }
    }
}

void handle_client(int cfd)
{
    control_request_t req;
    read(cfd, &req, sizeof(req));

    if (req.kind == CMD_START || req.kind == CMD_RUN) {
        mkdir("logs", 0755);
        char path[256];
        snprintf(path, sizeof(path), "logs/%s.log", req.container_id);
        int logfd = open(path,
                         O_CREAT | O_WRONLY | O_APPEND,
                         0644);

        child_config_t *cfg = malloc(sizeof(*cfg));
        memset(cfg, 0, sizeof(*cfg));
        strcpy(cfg->id, req.container_id);
        strcpy(cfg->rootfs, req.rootfs);
        strcpy(cfg->command, req.command);
        cfg->log_fd = logfd;

        char *stack = malloc(STACK_SIZE);
        pid_t pid = clone(child_fn,
                          stack + STACK_SIZE,
                          CLONE_NEWPID |
                          CLONE_NEWUTS |
                          CLONE_NEWNS |
                          SIGCHLD,
                          cfg);

        if (pid < 0) {
            send_resp(cfd, 1, "clone failed");
            close(logfd);
            return;
        }

        add_container(req.container_id,
                      pid,
                      req.soft_limit_bytes,
                      req.hard_limit_bytes);

        if (g.monitor_fd >= 0) {
            struct monitor_request mr;
            memset(&mr, 0, sizeof(mr));
            mr.pid = pid;
            mr.soft_limit_bytes = req.soft_limit_bytes;
            mr.hard_limit_bytes = req.hard_limit_bytes;
            strcpy(mr.container_id, req.container_id);
            ioctl(g.monitor_fd,
                  MONITOR_REGISTER,
                  &mr);
        }

        send_resp(cfd, 0, "container started");
        return;
    }

    if (req.kind == CMD_PS) {
        char buf[2048];
        memset(buf, 0, sizeof(buf));
        print_ps_to_buffer(buf, sizeof(buf));
        send_resp(cfd, 0, buf);
        return;
    }

    if (req.kind == CMD_LOGS) {
        char path[256];
        snprintf(path, sizeof(path), "logs/%s.log", req.container_id);
        FILE *fp = fopen(path, "r");
        if (!fp) {
            send_resp(cfd, 1, "no log file");
            return;
        }
        char buf[2048];
        size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
        buf[n] = 0;
        fclose(fp);
        send_resp(cfd, 0, buf);
        return;
    }

    if (req.kind == CMD_STOP) {
        container_record_t *c = find_container(req.container_id);
        if (!c) {
            send_resp(cfd, 1, "container not found");
            return;
        }
        kill(c->host_pid, SIGTERM);
        c->state = CONTAINER_STOPPED;
        send_resp(cfd, 0, "container stopped");
        return;
    }

    send_resp(cfd, 1, "unknown command");
}

int run_supervisor(const char *rootfs)
{
    (void)rootfs;
    struct sockaddr_un addr;
    memset(&g, 0, sizeof(g));
    pthread_mutex_init(&g.lock, NULL);

    g.monitor_fd = open("/dev/container_monitor", O_RDWR);

    unlink(CONTROL_PATH);
    g.server_fd = socket(AF_UNIX, SOCK_STREAM, 0);

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, CONTROL_PATH);

    bind(g.server_fd,
         (struct sockaddr *)&addr,
         sizeof(addr));
    listen(g.server_fd, 10);

    printf("Supervisor started...\n");

    while (1) {
        reap_children();
        int cfd = accept(g.server_fd, NULL, NULL);
        if (cfd >= 0) {
            handle_client(cfd);
            close(cfd);
        }
    }
    return 0;
}

int send_request(control_request_t *req)
{
    int fd;
    struct sockaddr_un addr;
    control_response_t resp;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, CONTROL_PATH);

    if (connect(fd,
                (struct sockaddr *)&addr,
                sizeof(addr)) < 0) {
        perror("connect");
        return 1;
    }

    write(fd, req, sizeof(*req));
    read(fd, &resp, sizeof(resp));
    printf("%s\n", resp.message);
    close(fd);
    return resp.status;
}

int main(int argc, char *argv[])
{
    control_request_t req;
    memset(&req, 0, sizeof(req));

    if (argc < 2) {
        printf("usage\n");
        return 1;
    }

    if (!strcmp(argv[1], "supervisor"))
        return run_supervisor(argv[2]);

    if (!strcmp(argv[1], "start")) {
        req.kind = CMD_START;
        strcpy(req.container_id, argv[2]);
        strcpy(req.rootfs, argv[3]);
        strcpy(req.command, argv[4]);
        req.soft_limit_bytes = 4UL << 20;
        req.hard_limit_bytes = 8UL << 20;
        return send_request(&req);
    }

    if (!strcmp(argv[1], "run")) {
        req.kind = CMD_RUN;
        strcpy(req.container_id, argv[2]);
        strcpy(req.rootfs, argv[3]);
        strcpy(req.command, argv[4]);
        req.soft_limit_bytes = 4UL << 20;
        req.hard_limit_bytes = 8UL << 20;
        return send_request(&req);
    }

    if (!strcmp(argv[1], "ps")) {
        req.kind = CMD_PS;
        return send_request(&req);
    }

    if (!strcmp(argv[1], "logs")) {
        req.kind = CMD_LOGS;
        strcpy(req.container_id, argv[2]);
        return send_request(&req);
    }

    if (!strcmp(argv[1], "stop")) {
        req.kind = CMD_STOP;
        strcpy(req.container_id, argv[2]);
        return send_request(&req);
    }

    return 0;
}
