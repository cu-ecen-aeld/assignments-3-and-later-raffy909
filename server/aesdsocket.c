#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <syslog.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>

#define LOG_IDENTITY            "aesdsocketd"
#define SERVER_PORT             9000
#define BACKLOG                 10
#define CONNECTION_BUFFER_SIZE  65536
#define CONNECTION_DATA_FILE    "/var/tmp/aesdsocketdata"

int server_sock = -1;
int data_fd = -1;

bool should_terminate = false;

void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        syslog(LOG_INFO, "Caught signal, exiting");
        close(server_sock); // this to avoid -> Accept failed: Bad file descriptor, not very graceful
        should_terminate = true;
    }
}

void start_daemon() {
    switch(fork()) {
        case -1: return;
        case 0: break; 
        default: exit(EXIT_SUCCESS);
    }

    if(setsid() == -1) {
        return;
    }

    switch(fork()) {
        case -1: return;
        case 0: break;
        default: exit(EXIT_SUCCESS);
    }
    
    umask(0);
    if(chdir("/") == -1) {
        exit(EXIT_FAILURE);
    }

    /* Close all open file descriptors */
    int x;
    for (x = sysconf(_SC_OPEN_MAX); x>=0; x--) {
        close (x);
    }
    
    openlog(LOG_IDENTITY, LOG_PID, LOG_DAEMON);
}

int write_data_to_file(int fd, char* data, size_t len) {   
    if (write(fd, data, len) == -1) {
        syslog(LOG_ERR, "Failed to write to file: %s", strerror(errno));
        return -1;
    }
    return 0;
}

int send_data_from_file(int fd, int client_fd) {
    ssize_t bytes_read;
    char file_buffer[1024];

    lseek(fd, 0, SEEK_SET);
    while ((bytes_read = read(fd, file_buffer, 1024)) > 0) {
        syslog(LOG_DEBUG, "Sending %ld", bytes_read);
        if (send(client_fd, file_buffer, bytes_read, 0) == -1) {
            syslog(LOG_ERR, "Failed to send data to client: %s", strerror(errno));
            return -1;
        }
    }

    if (bytes_read == -1) {
        syslog(LOG_ERR, "Failed to read from file: %s", strerror(errno));
        return -1;
    }

    return 0;
}

/* Read characters from 'fd' until a newline is encountered. If a newline
  character is not encountered in the first (n - 1) bytes, then the excess
  characters are discarded. The returned string placed in 'buf' is
  null-terminated and includes the newline character if it was read in the
  first (n - 1) bytes. The function return value is the number of bytes
  placed in buffer (which includes the newline character if encountered,
  but excludes the terminating null byte). */

ssize_t read_line(int fd, void *buffer, size_t n) {
    ssize_t numRead;                    /* # of bytes fetched by last read() */
    size_t totRead;                     /* Total bytes read so far */
    char *buf;
    char ch;

    if (n <= 0 || buffer == NULL) {
        errno = EINVAL;
        return -1;
    }

    buf = buffer;                       /* No pointer arithmetic on "void *" */

    totRead = 0;
    for (;;) {
        numRead = read(fd, &ch, 1);

        if (numRead == -1) {
            if (errno == EINTR)         /* Interrupted --> restart read() */
                continue;
            else
                return -1;              /* Some other error */

        } else if (numRead == 0) {      /* EOF */
            if (totRead == 0)           /* No bytes read; return 0 */
                return 0;
            else                        /* Some bytes read; add '\0' */
                break;

        } else {                        /* 'numRead' must be 1 if we get here */
            if (totRead < n - 1) {      /* Discard > (n - 1) bytes */
                totRead++;
                *buf++ = ch;
            }

            if (ch == '\n')
                break;
        }
    }

    *buf = '\0';
    return totRead;
}

void handle_connection(int socket_fd, struct sockaddr_in *client_addr) {
    ssize_t bytes_received;
    char conn_buffer[CONNECTION_BUFFER_SIZE];
    char client_ip[INET_ADDRSTRLEN];
    
    /* Logging connection ip address */
    inet_ntop(AF_INET, &(client_addr->sin_addr), client_ip, INET_ADDRSTRLEN);
    syslog(LOG_INFO, "Accepted connection from %s", client_ip);

    /* Opening file for storing data */
    data_fd = open(CONNECTION_DATA_FILE, O_RDWR | O_CREAT | O_APPEND, 0644);
    if (data_fd == -1) {
        syslog(LOG_ERR, "Failed to open or create file: %s", strerror(errno));
        close(socket_fd);
        return;
    }

    /* Handling data */
    //while ((bytes_received = recv(socket_fd, (char*)(conn_buffer + curr_packet_len), CONNECTION_BUFFER_SIZE, 0)) > 0 && !should_terminate) {
    while((bytes_received = read_line(socket_fd, conn_buffer, CONNECTION_BUFFER_SIZE)) > 0 && !should_terminate) {
        syslog(LOG_DEBUG, "Newline found"); 
    
        if(write_data_to_file(data_fd, conn_buffer, bytes_received) == -1) {
            close(data_fd);
            close(socket_fd);
            return;
        }

        if(send_data_from_file(data_fd, socket_fd) == -1) {
            close(data_fd);
            close(socket_fd);
            return;
        }
    }

    if (bytes_received == -1) {
        syslog(LOG_ERR, "Failed to receive data: %s", strerror(errno));
    }

    /* Closing data file */
    syslog(LOG_DEBUG, "Closing data file for %s", client_ip);
    close(data_fd);
    data_fd = -1;

    /* Closing connection */
    close(socket_fd);
    /* Logging closed connection */
    syslog(LOG_INFO, "Closed connection from %s", client_ip);
}

int main(int argc, char const *argv[]) {
    bool run_as_daemon = false;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);

    openlog(LOG_IDENTITY, LOG_PID, LOG_USER);

    /* Checking for arguments*/
    int opt;
    while ((opt = getopt(argc, (char* const*)argv, "d")) != -1) {
        switch (opt) {
        case 'd': run_as_daemon = true; break;
        default:
            fprintf(stderr, "Usage: %s [-d]\n", argv[0]);
            return EXIT_FAILURE;
        }
    }

    /* Check if need to run as daemon */
    if (run_as_daemon) {
        pid_t pid;
        pid = fork();
       
        if (pid == -1) {
            return EXIT_FAILURE;
        }
        else if (pid != 0) {
            return EXIT_SUCCESS;
        }

        if (setsid() == -1) {
            return EXIT_FAILURE;
        }
        if(chdir("/") == -1) {
            return EXIT_FAILURE;
        }

        dup2(open("/dev/null", O_RDWR), STDIN_FILENO);
        dup2(STDIN_FILENO, STDOUT_FILENO);
        dup2(STDOUT_FILENO, STDERR_FILENO);

        openlog(LOG_IDENTITY, LOG_PID, LOG_DAEMON);
    }

    /* Adding signal handler */
    if (signal(SIGTERM, signal_handler) == SIG_ERR) {
        syslog(LOG_ERR, "Failed to register SIGTERM: %s", strerror(errno));
        closelog();

        return EXIT_FAILURE;
    }
    if (signal(SIGINT, signal_handler) == SIG_ERR) {
        syslog(LOG_ERR, "Failed to register SIGINT: %s", strerror(errno));
        closelog();

        return EXIT_FAILURE;
    }

    /* Create socket */
    if ((server_sock = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        syslog(LOG_ERR, "Socket creation failed: %s", strerror(errno));
        return EXIT_FAILURE;
    }

    int so_reuseaddr = 1;
    if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &so_reuseaddr, sizeof(int)) == -1) {
        syslog(LOG_ERR, "Setsockopt failed: %s", strerror(errno));
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(SERVER_PORT);

    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        syslog(LOG_ERR, "Bind failed: %s", strerror(errno));
        if (server_sock != -1) {
            close(server_sock);
        }
        syslog(LOG_INFO, "Server exiting");
        closelog();
        return EXIT_SUCCESS;
    }

    if (listen(server_sock, BACKLOG) == -1) {
        syslog(LOG_ERR, "Listen failed: %s", strerror(errno));
        if (server_sock != -1) {
            close(server_sock);
        }
        syslog(LOG_INFO, "Server exiting");
        closelog();
        return EXIT_SUCCESS;
    }

    syslog(LOG_INFO, "Listening on port %d", SERVER_PORT);

    while (!should_terminate) {
        if(server_sock != -1) {
            int client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &addr_len);
            if (client_sock == -1) {
                syslog(LOG_ERR, "Accept failed: %s", strerror(errno));
                continue;
            }

            handle_connection(client_sock, &client_addr);
        }
    }

    if (remove(CONNECTION_DATA_FILE) == 0) {
        syslog(LOG_INFO, "Deleted file %s", CONNECTION_DATA_FILE);
    } else {
        syslog(LOG_ERR, "Failed to delete file %s: %s", CONNECTION_DATA_FILE, strerror(errno));
    }

    if (server_sock != -1) {
        close(server_sock);
    }
    
    syslog(LOG_INFO, "Server exiting");
    closelog();
    
    return EXIT_SUCCESS;
}
