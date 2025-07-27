#define _POSIX_C_SOURCE 199309L

#include "run.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <sys/inotify.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <signal.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/select.h>

#define EVENT_SIZE      (sizeof(struct inotify_event))
#define INOTIFY_BUF_LEN (1024 * (EVENT_SIZE + NAME_MAX + 1))

HotReloadState hot_reload_state = {
    .watch_count = 0,
    .watch_mutex = PTHREAD_MUTEX_INITIALIZER,
    .inotify_fd = -1,
    .shutdown_requested = 0,
    .reload_needed = 0,
    .reload_pipe = {-1, -1},
    .last_change_time = 0
};

static void* file_watcher_thread(void* arg);
static void add_watch(const char* path);
static int is_temp_file(const char* filename);
static void shutdown_hot_reload(void);
static void signal_handler(int signum);

static void notify_reload(void) {
    hot_reload_state.last_change_time = time(NULL);
    printf("[DEV] File change detected - reload pending\n");
    
    char sig = 'R';
    write(hot_reload_state.reload_pipe[1], &sig, 1);
}

static void add_watch(const char* path) {
    if (!path || strlen(path) == 0) return;
    
    printf("[DEV] Adding watch for: %s\n", path);

    int wd = inotify_add_watch(hot_reload_state.inotify_fd, path,
                               IN_MODIFY | IN_CREATE | IN_DELETE | IN_MOVE);
    if (wd < 0) {
        printf("[ERR] Failed to add watch for %s: %s\n", path, strerror(errno));
        return;
    }

    pthread_mutex_lock(&hot_reload_state.watch_mutex);
    if (hot_reload_state.watch_count < MAX_WATCH_DESCRIPTORS) {
        hot_reload_state.watch_descriptors[hot_reload_state.watch_count].wd = wd;
        strncpy(hot_reload_state.watch_descriptors[hot_reload_state.watch_count].path,
                path, sizeof(hot_reload_state.watch_descriptors[0].path) - 1);
        hot_reload_state.watch_descriptors[hot_reload_state.watch_count].path[255] = '\0';
        hot_reload_state.watch_count++;
    }
    pthread_mutex_unlock(&hot_reload_state.watch_mutex);
}

static void add_watches_recursive(const char* path) {
    DIR* dir;
    struct dirent* entry;
    struct stat statbuf;
    char full_path[512];

    add_watch(path);

    dir = opendir(path);
    if (!dir) {
        printf("[ERR] Failed to open directory %s: %s\n", path, strerror(errno));
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        if (entry->d_name[0] == '.' ||
            strcmp(entry->d_name, "node_modules") == 0 ||
            strcmp(entry->d_name, ".git") == 0 ||
            strcmp(entry->d_name, "build") == 0 ||
            strcmp(entry->d_name, "dist") == 0)
            continue;

        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
        if (stat(full_path, &statbuf) == 0 && S_ISDIR(statbuf.st_mode)) {
            add_watches_recursive(full_path);
        }
    }
    closedir(dir);
}

static int is_temp_file(const char* filename) {
    if (!filename || strlen(filename) == 0) return 1;

    if (filename[0] == '.') return 1;
    if (strstr(filename, "~") != NULL) return 1;
    if (strstr(filename, ".swp") != NULL) return 1;
    if (strstr(filename, ".tmp") != NULL) return 1;
    if (strstr(filename, ".bak") != NULL) return 1;
    if (strstr(filename, "#") != NULL) return 1;

    return 0;
}

static void* file_watcher_thread(void* arg) {
    const char* watch_dir = (const char*)arg;
    char buffer[INOTIFY_BUF_LEN];
    time_t last_reload = 0;

    printf("[DEV] Starting file watcher for: %s\n", watch_dir);

    hot_reload_state.inotify_fd = inotify_init1(IN_NONBLOCK); // Non-blocking
    if (hot_reload_state.inotify_fd < 0) {
        printf("[ERR] inotify_init failed: %s\n", strerror(errno));
        return NULL;
    }

    if (access(watch_dir, F_OK) != 0) {
        printf("[ERR] Watch directory %s does not exist\n", watch_dir);
        return NULL;
    }

    add_watches_recursive(watch_dir);
    printf("[DEV] File watcher started with %d watches\n", hot_reload_state.watch_count);

    while (!hot_reload_state.shutdown_requested) {
        fd_set read_fds;
        struct timeval timeout;
        
        FD_ZERO(&read_fds);
        FD_SET(hot_reload_state.inotify_fd, &read_fds);
        timeout.tv_sec = 0;  // Non-blocking timeout
        timeout.tv_usec = 500000; // 500ms

        int ready = select(hot_reload_state.inotify_fd + 1, &read_fds, NULL, NULL, &timeout);
        if (ready < 0) {
            if (errno == EINTR) continue;
            printf("[ERR] select failed: %s\n", strerror(errno));
            break;
        }
        
        if (ready == 0) {
            // Timeout
            usleep(100000); // 100ms
            continue; 
        }

        int length = read(hot_reload_state.inotify_fd, buffer, INOTIFY_BUF_LEN);
        if (length < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue; // No data available
            }
            if (errno == EINTR) continue;
            printf("[ERR] inotify read failed: %s\n", strerror(errno));
            break;
        }

        time_t current_time = time(NULL);
        int should_reload = 0;
        int i = 0;

        while (i < length) {
            struct inotify_event* event = (struct inotify_event*)&buffer[i];

            if (event->len > 0) {
                if (!is_temp_file(event->name)) {
                    if (current_time - last_reload >= 1) {
                        printf("[DEV] File changed: %s\n", event->name);
                        should_reload = 1;
                        last_reload = current_time;
                    }
                }
            }

            if ((event->mask & IN_CREATE) && (event->mask & IN_ISDIR)) {
                char new_dir[512] = {0};
                pthread_mutex_lock(&hot_reload_state.watch_mutex);
                for (int j = 0; j < hot_reload_state.watch_count; j++) {
                    if (hot_reload_state.watch_descriptors[j].wd == event->wd) {
                        snprintf(new_dir, sizeof(new_dir), "%s/%s",
                                 hot_reload_state.watch_descriptors[j].path, event->name);
                        break;
                    }
                }
                pthread_mutex_unlock(&hot_reload_state.watch_mutex);

                if (strlen(new_dir) > 0 && !is_temp_file(event->name)) {
                    add_watches_recursive(new_dir);
                }
            }

            i += EVENT_SIZE + event->len;
        }

        if (should_reload) {
            notify_reload();
        }
    }

    printf("[DEV] File watcher shutting down\n");
    if (hot_reload_state.inotify_fd >= 0) {
        close(hot_reload_state.inotify_fd);
        hot_reload_state.inotify_fd = -1;
    }
    return NULL;
}

static void signal_handler(int signum) {
    (void)signum;
    printf("[DEV] Shutdown signal received\n");
    hot_reload_state.shutdown_requested = 1;
    shutdown_hot_reload();
}

static void shutdown_hot_reload(void) {
    printf("[DEV] Shutting down hot reload system...\n");
    hot_reload_state.shutdown_requested = 1;

    if (hot_reload_state.inotify_fd >= 0) {
        close(hot_reload_state.inotify_fd);
        hot_reload_state.inotify_fd = -1;
    }

    // Close pipes
    if (hot_reload_state.reload_pipe[0] != -1) {
        close(hot_reload_state.reload_pipe[0]);
        close(hot_reload_state.reload_pipe[1]);
        hot_reload_state.reload_pipe[0] = -1;
        hot_reload_state.reload_pipe[1] = -1;
    }

    printf("[DEV] Hot reload system shut down\n");
}

void start_live_reload_server(int unused, const char* watch_dir) {
    (void)unused;

    pthread_t watcher_thread;
    static char dir_copy[512];

    printf("[DEV] Initializing live reload system...\n");
    printf("[DEV] Watch directory: %s\n", watch_dir);

    // Create pipe for signaling main thread
    if (pipe(hot_reload_state.reload_pipe)) {
        perror("[ERR] Failed to create reload pipe");
        return;
    }

    // Make pipe non-blocking
    for (int i = 0; i < 2; i++) {
        int flags = fcntl(hot_reload_state.reload_pipe[i], F_GETFL, 0);
        fcntl(hot_reload_state.reload_pipe[i], F_SETFL, flags | O_NONBLOCK);
    }

    strncpy(dir_copy, watch_dir, sizeof(dir_copy) - 1);
    dir_copy[sizeof(dir_copy) - 1] = '\0';

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    atexit(shutdown_hot_reload);

    if (pthread_create(&watcher_thread, NULL, file_watcher_thread, dir_copy) != 0) {
        perror("[ERR] Could not create file watcher thread");
        return;
    }
    pthread_detach(watcher_thread);

    printf("[DEV] Live reload system started\n");
}

int get_reload_pipe_fd(void) {
    return hot_reload_state.reload_pipe[0];
}
