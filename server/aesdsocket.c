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

#define PORT "9000"
#define BACKLOG 10
#define FILE_PATH "/var/tmp/aesdsocketdata"
#define BUF_CHUNK 512

sig_atomic_t exit_requested = 0;
int daemon_mode = 0;

void signal_handler() {
    syslog(LOG_INFO, "Caught signal, exiting");
    exit_requested = 1;
}

void setup_signal_handlers() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

void handle_connection(int client_fd) {
    char buf[BUF_CHUNK];
    char *packet = NULL;
    size_t packet_size = 0;
    ssize_t bytes_read;
    FILE *fp = fopen(FILE_PATH, "a+");

    if (!fp) {
        perror("fopen");
        close(client_fd);
        return;
    }

    while ((bytes_read = recv(client_fd, buf, BUF_CHUNK, 0)) > 0) {
        char *new_packet = realloc(packet, packet_size + bytes_read + 1);
        if (!new_packet) {
            perror("realloc");
            free(packet);
            fclose(fp);
            close(client_fd);
            return;
        }

        packet = new_packet;
        memcpy(packet + packet_size, buf, bytes_read);
        packet_size += bytes_read;
        packet[packet_size] = '\0';

        if (strchr(packet, '\n') != NULL) {
            fwrite(packet, 1, packet_size, fp);
            fflush(fp);

            fseek(fp, 0, SEEK_SET);
            char readbuf[BUF_CHUNK];
            size_t n;
            while ((n = fread(readbuf, 1, BUF_CHUNK, fp)) > 0) {
                send(client_fd, readbuf, n, 0);
            }
            break;
        }
    }

    if (bytes_read == -1) {
        perror("recv");
    }

    free(packet);
    fclose(fp);
    close(client_fd);
}

int main(int argc, char *argv[]) {
    if (argc == 2 && strcmp(argv[1], "-d") == 0) 
    {
        daemon_mode = 1;
    }

    setup_signal_handlers();
    openlog("aesdsocket", LOG_PID, LOG_USER);

    struct addrinfo hints = {0}, *res;
    int sockfd, client_fd;
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

    if (daemon_mode) 
    {
        pid_t pid = fork();
        if (pid < 0) 
        {
            perror("fork");
            close(sockfd);
            return 1;
        }
        if (pid > 0) 
        {
            close(sockfd);
            return 0;
        }

        if (setsid() < 0) 
        {
            perror("setsid");
            close(sockfd);
            return 1;
        }

        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);

        int null_fd = open("/dev/null", O_RDWR);
        if (null_fd != -1) 
        {
            dup2(null_fd, STDIN_FILENO);
            dup2(null_fd, STDOUT_FILENO);
            dup2(null_fd, STDERR_FILENO);
            if (null_fd > STDERR_FILENO) 
            {
                close(null_fd);
            }
        }
    }

    while (!exit_requested) {
        struct sockaddr_in client_addr;
        socklen_t addr_size = sizeof(client_addr);
        client_fd = accept(sockfd, (struct sockaddr*)&client_addr, &addr_size);
        if (client_fd == -1) {
            if (errno == EINTR && exit_requested) break;
            perror("accept");
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, sizeof(client_ip));
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        handle_connection(client_fd);
    }

    remove(FILE_PATH);
    close(sockfd);
    closelog();
    return 0;
}
