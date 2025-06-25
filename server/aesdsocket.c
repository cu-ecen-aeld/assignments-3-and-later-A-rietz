#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <syslog.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <pthread.h>
#include <time.h>

#define PORT "9000"
#define BACKLOG 10
#define FILE_PATH "/var/tmp/aesdsocketdata"
#define BUF_CHUNK 512

typedef struct thread_node {
    pthread_t thread_id;
    struct thread_node *next;
} thread_node_t;

sig_atomic_t exit_requested = 0;
int daemon_mode = 0;

thread_node_t *thread_list_head = NULL;
pthread_mutex_t list_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

void signal_handler(int signo) {
    (void)signo;
    syslog(LOG_INFO, "Caught signal, exiting");
    exit_requested = 1;
}

void *timestamp_writer(void *arg) {
    (void)arg;
    while (!exit_requested) {
        sleep(10);
        time_t now = time(NULL);
        char timebuf[100];
        struct tm *tm_info = localtime(&now);
        strftime(timebuf, sizeof(timebuf), "timestamp:%a, %d %b %Y %H:%M:%S %z\n", tm_info);

        pthread_mutex_lock(&file_mutex);
        FILE *fp = fopen(FILE_PATH, "a");
        if (fp) {
            fputs(timebuf, fp);
            fclose(fp);
        }
        pthread_mutex_unlock(&file_mutex);
    }
    return NULL;
}

void add_thread(pthread_t tid) {
    thread_node_t *node = malloc(sizeof(thread_node_t));
    if (!node) return;
    node->thread_id = tid;

    pthread_mutex_lock(&list_mutex);
    node->next = thread_list_head;
    thread_list_head = node;
    pthread_mutex_unlock(&list_mutex);
}

void join_and_cleanup_threads() {
    pthread_mutex_lock(&list_mutex);
    thread_node_t *tp = thread_list_head;
    thread_list_head = NULL;
    pthread_mutex_unlock(&list_mutex);

    while (tp) {
        pthread_join(tp->thread_id, NULL);
        thread_node_t *temp = tp;
        tp = tp->next;
        free(temp);
    }
}

void setup_signal_handlers() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

void *handle_connection(void *arg) {
    int client_fd = *(int *)arg;
    free(arg);

    char buf[BUF_CHUNK];
    char *packet = NULL;
    size_t packet_size = 0;
    ssize_t bytes_read;

    while ((bytes_read = recv(client_fd, buf, BUF_CHUNK, 0)) > 0) {
        char *new_packet = realloc(packet, packet_size + bytes_read + 1);
        if (!new_packet) {
            perror("realloc");
            free(packet);
            close(client_fd);
            return NULL;
        }

        packet = new_packet;
        memcpy(packet + packet_size, buf, bytes_read);
        packet_size += bytes_read;
        packet[packet_size] = '\0';

        char *line_end;
        while ((line_end = strchr(packet, '\n')) != NULL) {
            size_t line_len = line_end - packet + 1;

            pthread_mutex_lock(&file_mutex);
            FILE *fp = fopen(FILE_PATH, "a");
            if (fp) {
                fwrite(packet, 1, line_len, fp);
                fclose(fp);

                fp = fopen(FILE_PATH, "r");
                if (fp) {
                    char readbuf[BUF_CHUNK];
                    size_t n;
                    while ((n = fread(readbuf, 1, BUF_CHUNK, fp)) > 0) {
                        if (send(client_fd, readbuf, n, 0) < 0) {
                            perror("send failed");
                            break;
                        }
                    }
                    fclose(fp);
                }
            }
            pthread_mutex_unlock(&file_mutex);

            memmove(packet, line_end + 1, packet_size - line_len);
            packet_size -= line_len;
            packet[packet_size] = '\0';
        }
    }

    if (bytes_read == -1) {
        perror("recv failed");
    }

    free(packet);
    close(client_fd);
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc == 2 && strcmp(argv[1], "-d") == 0) {
        daemon_mode = 1;
    }

    setup_signal_handlers();
    openlog("aesdsocket", LOG_PID, LOG_USER);
    remove(FILE_PATH);

    struct addrinfo hints = {0}, *res;
    int sockfd;
    int yes = 1;

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, PORT, &hints, &res) != 0) {
        perror("getaddrinfo");
        return 1;
    }

    sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sockfd == -1) {
        perror("socket");
        freeaddrinfo(res);
        return 1;
    }

    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
    if (bind(sockfd, res->ai_addr, res->ai_addrlen) == -1) {
        perror("bind");
        close(sockfd);
        freeaddrinfo(res);
        return 1;
    }

    freeaddrinfo(res);

    if (listen(sockfd, BACKLOG) == -1) {
        perror("listen");
        close(sockfd);
        return 1;
    }

    if (daemon_mode) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            close(sockfd);
            return 1;
        }
        if (pid > 0) {
            close(sockfd);
            return 0;
        }

        if (setsid() < 0) {
            perror("setsid");
            close(sockfd);
            return 1;
        }

        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);

        int null_fd = open("/dev/null", O_RDWR);
        if (null_fd != -1) {
            dup2(null_fd, STDIN_FILENO);
            dup2(null_fd, STDOUT_FILENO);
            dup2(null_fd, STDERR_FILENO);
            if (null_fd > STDERR_FILENO) {
                close(null_fd);
            }
        }
    }

    pthread_t timestamp_thread;
    if (pthread_create(&timestamp_thread, NULL, timestamp_writer, NULL) != 0) {
        perror("pthread_create for timestamp");
        close(sockfd);
        return 1;
    }

    while (!exit_requested) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int *client_fd = malloc(sizeof(int));
        if (!client_fd) {
            perror("malloc failed");
            continue;
        }

        *client_fd = accept(sockfd, (struct sockaddr*)&client_addr, &client_len);
        if (*client_fd < 0) {
            free(client_fd);
            if (errno == EINTR && exit_requested) break;
            perror("accept failed");
            continue;
        }

        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_connection, client_fd) == 0) {
            add_thread(tid);
        } else {
            perror("pthread_create failed");
            close(*client_fd);
            free(client_fd);
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, sizeof(client_ip));
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);
    }

    close(sockfd);
    pthread_join(timestamp_thread, NULL);
    join_and_cleanup_threads();
    pthread_mutex_destroy(&file_mutex);
    pthread_mutex_destroy(&list_mutex);
    remove(FILE_PATH);
    syslog(LOG_INFO, "Server shutdown complete");
    closelog();
    return 0;
}
