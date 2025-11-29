#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>

#define DEFAULT_PORT 12345
#define BACKLOG 10
#define BUFFER_SIZE 4096
#define NAME_LEN 32

typedef struct {
    int fd;
    char name[NAME_LEN];
} client_t;

static client_t *clients = NULL;
static size_t client_count = 0;
static size_t client_capacity = 0;
static FILE *log_fp = NULL;






void log_message(const char *fmt, ...)
{
    if (!log_fp) return;

    va_list ap;
    va_start(ap, fmt);
    vfprintf(log_fp, fmt, ap);
    fprintf(log_fp, "\n");
    fflush(log_fp);
    va_end(ap);
}





int add_client(int fd, const char *name) {
    if (client_count == client_capacity) {
        size_t newcap = client_capacity == 0 ? 8 : client_capacity * 2;
        client_t *tmp = realloc(clients, newcap * sizeof(client_t));
        if (!tmp) return -1;
        clients = tmp;
        client_capacity = newcap;
    }
    clients[client_count].fd = fd;
    strncpy(clients[client_count].name, name, NAME_LEN-1);
    clients[client_count].name[NAME_LEN-1] = '\0';
    client_count++;
    return 0;
}




void remove_client_index(size_t idx) {
    if (idx >= client_count) return;
    close(clients[idx].fd);
    if (idx != client_count - 1) {
        clients[idx] = clients[client_count - 1];
    }
    client_count--;
}

ssize_t send_all(int fd, const char *buf, size_t len) {
    ssize_t total = 0;
    while (total < (ssize_t)len) {
        ssize_t n = send(fd, buf + total, len - total, 0);
        if (n <= 0) return n;
        total += n;
    }
    return total;
}




void send_to_client_idx(size_t idx, const char *msg) {
    if (idx >= client_count) return;
    send_all(clients[idx].fd, msg, strlen(msg));
}




void broadcast(const char *msg, int exclude_fd) {
    for (size_t i = 0; i < client_count; ++i) {
        if (clients[i].fd == exclude_fd) continue;
        send_all(clients[i].fd, msg, strlen(msg));
    }
}




ssize_t find_client_by_name(const char *name) {
    for (size_t i = 0; i < client_count; ++i) {
        if (strcmp(clients[i].name, name) == 0) return (ssize_t)i;
    }
    return -1;
}




ssize_t find_client_by_fd(int fd) {
    for (size_t i = 0; i < client_count; ++i) {
        if (clients[i].fd == fd) return (ssize_t)i;
    }
    return -1;
}


void send_user_list(int to_fd) {
    char buf[BUFFER_SIZE];
    int offset = snprintf(buf, sizeof(buf), "Connected users (%zu):\n", client_count);
    for (size_t i = 0; i < client_count; ++i) {
        offset += snprintf(buf + offset, sizeof(buf) - offset, "%s\n", clients[i].name);
        if (offset >= (int)sizeof(buf) - 1) break;
    }
    send_all(to_fd, buf, strlen(buf));
}


void handle_client_command(int fd, char *line) {
    
    size_t ln = strlen(line);
    if (ln && (line[ln-1] == '\n' || line[ln-1] == '\r')) line[ln-1] = '\0';
    if (ln > 1 && (line[ln-2] == '\r')) line[ln-2] = '\0';

    if (strncmp(line, "/nick ", 6) == 0) {
        char *newname = line + 6;
        ssize_t idx = find_client_by_fd(fd);
        if (idx >= 0) {
            char old[NAME_LEN];
            strncpy(old, clients[idx].name, NAME_LEN);
            strncpy(clients[idx].name, newname, NAME_LEN-1);
            clients[idx].name[NAME_LEN-1] = '\0';
            char msg[256];
            snprintf(msg, sizeof(msg), "%s is now known as %s\n", old, clients[idx].name);
            broadcast(msg, -1);
            log_message("NICK %s -> %s", old, clients[idx].name);
        }
        return;
    }

    if (strncmp(line, "/msg ", 5) == 0) {
        
        char *p = line + 5;
        char *target = strtok(p, " ");
        char *rest = strtok(NULL, "");
        if (!target || !rest) {
            send_all(fd, "Usage: /msg <user> <message>\n", 29);
            return;
        }
        ssize_t tidx = find_client_by_name(target);
        ssize_t sidx = find_client_by_fd(fd);
        if (tidx >= 0 && sidx >= 0) {
            char buf[BUFFER_SIZE];
            snprintf(buf, sizeof(buf), "(private) %s: %s\n", clients[sidx].name, rest);
            send_all(clients[tidx].fd, buf, strlen(buf));
            send_all(fd, "(sent)\n", 7);
            log_message("PM %s -> %s: %s", clients[sidx].name, clients[tidx].name, rest);
        } else {
            send_all(fd, "User not found.\n", 16);
        }
        return;
    }

    if (strcmp(line, "/list") == 0) {
        send_user_list(fd);
        return;
    }

    if (strcmp(line, "/quit") == 0) {
        ssize_t idx = find_client_by_fd(fd);
        if (idx >= 0) {
            char msg[128];
            snprintf(msg, sizeof(msg), "%s has left the chat.\n", clients[idx].name);
            broadcast(msg, fd);
            log_message("QUIT %s", clients[idx].name);
            remove_client_index((size_t)idx);
        }
        return;
    }

    


    ssize_t sidx = find_client_by_fd(fd);
    if (sidx >= 0) {
        char buf[BUFFER_SIZE];
        snprintf(buf, sizeof(buf), "%s: %s\n", clients[sidx].name, line);
        broadcast(buf, -1);
        log_message("MSG %s: %s", clients[sidx].name, line);
    }
}





void handle_server_stdin(char *cmd) {
    

    size_t ln = strlen(cmd);
    while (ln && (cmd[ln-1] == '\n' || cmd[ln-1] == '\r')) { cmd[--ln] = '\0'; }
    if (strcmp(cmd, "clients") == 0) {
        printf("Connected clients (%zu):\n", client_count);
        for (size_t i = 0; i < client_count; ++i) {
            printf(" - %s (fd=%d)\n", clients[i].name, clients[i].fd);
        }
    } else if (strncmp(cmd, "logs", 4) == 0) {
        int n = 20;
        char *p = strchr(cmd, ' ');
        if (p) n = atoi(p+1);
        if (!log_fp) { printf("no log file open\n"); return; }
        fflush(log_fp);
        FILE *f = fopen("chat.log", "r");
        if (!f) { perror("fopen"); return; }
        char *lines[10000];
        int idx = 0;
        char *line = NULL;
        size_t len = 0;
        ssize_t r;
        while ((r = getline(&line, &len, f)) != -1) {
            if (idx < 10000) lines[idx++] = strdup(line);
        }
        if (line) free(line);
        int start = idx - n; if (start < 0) start = 0;
        for (int i = start; i < idx; ++i) printf("%s", lines[i]);
        for (int i = 0; i < idx; ++i) free(lines[i]);
        fclose(f);
    } else if (strcmp(cmd, "shutdown") == 0) {
        printf("Shutting down server...\n");
        exit(0);
    } else {
        printf("Unknown admin command. Available: clients, logs [N], shutdown\n");
    }
}

int main(int argc, char **argv) {
    int port = DEFAULT_PORT;
    if (argc >= 2) port = atoi(argv[1]);

    log_fp = fopen("chat.log", "a+");
    if (!log_fp) { perror("fopen"); }

    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) { perror("socket"); return 1; }

    int yes = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(listener, (struct sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
    if (listen(listener, BACKLOG) < 0) { perror("listen"); return 1; }

    printf("Server listening on port %d\n", port);

    fd_set master_set, read_fds;
    FD_ZERO(&master_set);
    FD_SET(listener, &master_set);
    FD_SET(STDIN_FILENO, &master_set);
    int fdmax = listener > STDIN_FILENO ? listener : STDIN_FILENO;




    while (1) {
        read_fds = master_set;
        if (select(fdmax + 1, &read_fds, NULL, NULL, NULL) == -1) {
            perror("select"); break;
        }

        



        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            char cmdbuf[512];
            if (fgets(cmdbuf, sizeof(cmdbuf), stdin) != NULL) {
                handle_server_stdin(cmdbuf);
            }
        }

       


        if (FD_ISSET(listener, &read_fds)) {
            struct sockaddr_in client_addr;
            socklen_t addrlen = sizeof(client_addr);
            int newfd = accept(listener, (struct sockaddr*)&client_addr, &addrlen);
            if (newfd == -1) {
                perror("accept");
            } else {
                FD_SET(newfd, &master_set);
                if (newfd > fdmax) fdmax = newfd;
                char default_name[NAME_LEN];
                snprintf(default_name, sizeof(default_name), "User%d", newfd);
                add_client(newfd, default_name);
                char announce[256];
                snprintf(announce, sizeof(announce), "%s has joined the chat.\n", default_name);
                broadcast(announce, newfd);
                send_all(newfd, "Welcome! Use /nick to set name.\n", 32);
                log_message("CONNECT %s (%s:%d)", default_name, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
            }
        }
        for (int i = (int)client_count - 1; i >= 0; --i) {
            int fd = clients[i].fd;
            if (FD_ISSET(fd, &read_fds)) {
                char buf[BUFFER_SIZE];
                ssize_t nbytes = recv(fd, buf, sizeof(buf)-1, 0);
                if (nbytes <= 0) {
                    char leftname[NAME_LEN];
                    strncpy(leftname, clients[i].name, NAME_LEN);
                    if (nbytes == 0) {
                        char msg[128];
                        snprintf(msg, sizeof(msg), "%s has disconnected.\n", leftname);
                        broadcast(msg, fd);
                        log_message("DISCONNECT %s", leftname);
                    } else {
                        perror("recv");
                    }
                    FD_CLR(fd, &master_set);
                    remove_client_index(i);
                } else {
                    buf[nbytes] = '\0';
                    char *line = strtok(buf, "\n");
                    while (line) {
                        handle_client_command(fd, line);
                        line = strtok(NULL, "\n");
                    }
                }
            }
        }
    }

    if (log_fp) fclose(log_fp);
    

    for (size_t i = 0; i < client_count; ++i) close(clients[i].fd);
    free(clients);
    close(listener);
    return 0;
}