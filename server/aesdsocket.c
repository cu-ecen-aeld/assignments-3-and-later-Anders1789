#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/socket.h>
#include <unistd.h>

static const char* FILENAME = "/var/tmp/aesdsocketdata";
bool caught_sigint = false;
bool caught_sigterm = false;

static void signal_handler(int signal) {
    if (signal == SIGINT) {
        caught_sigint = true;
    }
    else if (signal == SIGTERM) {
        caught_sigterm = true;
    }
}

static void setup_signal_handler() {
    struct sigaction new_action;
    memset(&new_action, 0, sizeof(new_action));
    new_action.sa_handler = signal_handler;
    if (sigaction(SIGINT, &new_action, NULL)) {
        perror("sigaction SIGINT error");
    }
    if (sigaction(SIGTERM, &new_action, NULL)) {
        perror("sigaction SIGTERM error");
    }
}

static int setup_server_socket() {
    struct addrinfo hints;
    struct addrinfo *serv_info;

    // Create addrinfo struct according to specs
    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = AI_PASSIVE;
    int status = getaddrinfo(NULL, "9000", &hints, &serv_info);
    if (status != 0) {
        syslog(LOG_ERR, "getaddrinfo error: %s\n", gai_strerror(status));
        return -1;
    }

    // Create socket fd using addrinfo
    int serv_fd = socket(
        serv_info->ai_family, serv_info->ai_socktype, serv_info->ai_protocol);
    if (serv_fd == -1) {
        perror("socket error");
        freeaddrinfo(serv_info);
        return -1;
    }

    // Set SO_REUSEADDR to avoid "Already in use" errors when doing heavy testing.
    const int enable = 1;
    if (setsockopt(serv_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) == -1) {
        perror("setsockopt error");
        if(close(serv_fd)) {
            perror("close serv error");
        }
        freeaddrinfo(serv_info);
        return -1;
    }

    // Bind socket to address using the rest of addrinfo 
    if (bind(serv_fd, serv_info->ai_addr, serv_info->ai_addrlen) == -1) {
        perror("bind error");
        if(close(serv_fd)) {
            perror("close serv error");
        }
        freeaddrinfo(serv_info);
        return -1;
    }

    // We are now done with serv_info so it may be freed.
    freeaddrinfo(serv_info);
    serv_info = NULL;

    // Indicate willingness to accept incoming connections and set queuelength
    if (listen(serv_fd, 1) == -1) {
        perror("listen error");
        if(close(serv_fd)) {
            perror("close serv error");
        }
        return -1;
    }

    return serv_fd;
}

static int accept_connection(int serv_fd, struct sockaddr *conn, socklen_t *conn_len) {
    // Accept connection
    int conn_fd = accept(serv_fd, conn, conn_len);
    if (conn_fd == -1) {
        perror("accept error");
        if(close(serv_fd)) {
            perror("close serv error");
        }
        return -1;
    }

    return conn_fd;
}

static int recv_from_socket(int conn_fd, char** data, size_t* data_len) {
    const size_t CHUNK_SIZE = 1000;
    size_t buf_len = CHUNK_SIZE;
    *data = malloc(buf_len);
    *data_len = 0;
    while(1) {
        ssize_t count = recv(conn_fd, &((*data)[*data_len]), buf_len - *data_len, 0);
        if (count == -1) {
            perror("recv error");
            if(close(conn_fd)) {
                perror("close conn error");
            }
            return -1;
        }
        else if(count == 0) {
            if(close(conn_fd)) {
                perror("close conn error");
            }
            return 0;
        }
        // New data received, update data_len
        *data_len += count;
 
        // Check count last bytes for newline.
        // If no newline found, continue receiving from socket
        if (memchr(&((*data)[*data_len - count]), '\n', count) == NULL) {
            // If buffer is full,try to reallocate it
            if (*data_len == buf_len) {
                buf_len += CHUNK_SIZE;
                char* new_buf = realloc(*data, buf_len);
                if (new_buf == NULL) {
                    syslog(LOG_ERR, "Error allocating a bugger buffer, returning received data for best effort.");
                    return -1;
                }
                // New buffer was allocated, we are ready to receive more data..
                *data = new_buf;
            }
            // Go and receive some more data from conn.
            continue;
        }
        // Newline found, return buffer
        return 0;
    }
    // Dummy return, we should never end up here.
    assert(0);
    return -1;
}

static int write_to_file(const char* buf, size_t len) {
    int fd = open(FILENAME, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd == -1) {
        perror("open error");
        return -1;
    }
    if (write(fd, buf, len) == -1) {
        perror("write error");
        if (close(fd)) {
            perror("close file error");
        }
        return -1;
    }
    if (close(fd) == -1) {
        perror("close file error");
        return -1;
    }

    return 0;
} 

static int read_and_send(int conn_fd) {
    char buf[1000];
    const size_t buf_len = sizeof(buf) / sizeof(buf[0]);
    int fd = open(FILENAME, O_RDONLY);
    if (fd == -1) {
        perror("open error");
        return -1;
    }
    while (1) {
        int count = read(fd, buf, buf_len);
        // Read error
        if (count == -1) {
            perror("read error");
            if (close(fd)) {
                perror("close file error");
            }
            return -1;
        }
        // EOF
        else if (count == 0) {
            if (close(fd)) {
                perror("close file error");
                return -1;
            }
            return 0;
        }
        // Data read, now send it
        if (send(conn_fd, buf, count, 0) == -1) {
            perror("send error");
            if (close(fd)) {
                perror("close file error");
            }
            return -1;
        }
    }

    // Dummy return, we should never end up here.
    assert(0);
    return -1;
}

static int connection_loop(int conn_fd) {
    while (1) {
        // Receive data from socket
        char* data;
        size_t data_len;
        int res = recv_from_socket(conn_fd, &data, &data_len);
        if (res == -1) {
            free(data);
            if(close(conn_fd)) {
                perror("close conn error1");
            }
            return -1;
        }

        // If no data was read, connection closed, just cleanup and return.
        if(data_len == 0) {
            free(data);
            // EBADF is ok since socket may be closed already.
            if(close(conn_fd) && errno != EBADF) {
                perror("close conn error2");
                return -1;
            }
            return 0;
        }

        // Write data to file.
        if (write_to_file(data, data_len) == -1) {
            free(data);
            if(close(conn_fd)) {
                perror("close conn error3");
            }
            return -1;
        }
        free(data);

        // Read and send
        if (read_and_send(conn_fd) == -1) {
            if(close(conn_fd)) {
                perror("close conn error4");
            }
            return -1;
        }
    }

    // Dummy return, we should never end up here.
    assert(0);
    return -1;
}

static void server_loop(int serv_fd) {
    while (1) {

        // Accept new connection
        struct sockaddr conn_sockaddr;
        memset(&conn_sockaddr, 0, sizeof(conn_sockaddr));
        socklen_t conn_sockaddr_len = 0;
        int conn_fd = accept_connection(serv_fd, &conn_sockaddr, &conn_sockaddr_len);
        if (conn_fd == -1) {
            if(close(serv_fd)) {
                perror("close serv error1");
            }
            return;
        }

        // Connection received, now communicate with it until connection is closed.
        syslog(LOG_INFO, "New connection received from %s\n", conn_sockaddr.sa_data);
        if (connection_loop(conn_fd) == -1) {
            if(close(serv_fd)) {
                perror("close serv error2");
            }
            return;
        }
        syslog(LOG_INFO, "Connection closed from %s\n", conn_sockaddr.sa_data);
    }

    // Dummy return, we should never end up here.
    assert(0);
    return;
}


int main(int argc, char **argv) {
    setup_signal_handler();

    int serv_fd = setup_server_socket();
    if (serv_fd == -1) {
        return EXIT_FAILURE;
    }

    if (argc >= 2 && (strcmp(argv[1], "-d") == 0)) {
        pid_t daemon_pid;
        daemon_pid = fork();
        // If error, exit app with failure
        if (daemon_pid == -1) {
            perror("fork ereror");
            return EXIT_FAILURE;
        }
        // If parent, exit app with success
        else if (daemon_pid != 0) {
            return EXIT_SUCCESS;
        }
        // If child, setup daemon
        if (setsid() == -1) {
            perror("setsid error");
            return EXIT_FAILURE;
        }
        if(chdir("/") == -1) {
            perror("chdir error");
        }

        // Close all open file descriptors.. But there should be none except for
        // stdin, stdout and stderr, which we are redirecting to /dev/null.
        dup2(open("/dev/null", O_RDWR), STDIN_FILENO);
        dup2(STDIN_FILENO, STDOUT_FILENO);
        dup2(STDOUT_FILENO, STDERR_FILENO);
    }

    server_loop(serv_fd);

    // If server loop was terminated by SIGINT or SIGTERM, everything is ok.
    // Delete FILE and return EXIT_SUCCESS 
    if (caught_sigint || caught_sigterm) {
        syslog(LOG_INFO, "Caught signal, exiting\n");
        if (remove(FILENAME) == -1) {
            perror("remove error");
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }
    // If server loop was terminated for some other reason, just return EXIT_FAILURE.
    return EXIT_FAILURE;
}
