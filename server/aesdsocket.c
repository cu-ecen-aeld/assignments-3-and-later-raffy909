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

void cleanup_server() {
    if (server_sock != -1) {
        close(server_sock);
    }

    if (data_fd != -1) {
        close(data_fd);
    }

    if (remove(CONNECTION_DATA_FILE) == 0) {
        syslog(LOG_INFO, "Deleted file %s", CONNECTION_DATA_FILE);
    } else {
        syslog(LOG_ERR, "Failed to delete file %s: %s", CONNECTION_DATA_FILE, strerror(errno));
    }

    syslog(LOG_INFO, "Server exiting");
    closelog();
}

void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        syslog(LOG_INFO, "Caught signal, exiting");  
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
    chdir("/");

    /* Close all open file descriptors */
    int x;
    for (x = sysconf(_SC_OPEN_MAX); x>=0; x--) {
        close (x);
    }
    
    openlog(LOG_IDENTITY, LOG_PID, LOG_DAEMON);
}

void handle_connection(int socket_fd, struct sockaddr_in *client_addr) {
    ssize_t bytes_received;
    char conn_buffer[CONNECTION_BUFFER_SIZE];
    char file_buffer[CONNECTION_BUFFER_SIZE];
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
    while ((bytes_received = recv(socket_fd, conn_buffer, CONNECTION_BUFFER_SIZE, 0)) > 0 && !should_terminate) {
        char *newline = strchr(conn_buffer, '\n'); // Check for newline
        if (newline) {
            newline++;

            size_t packet_length = newline - conn_buffer;
            if (write(data_fd, conn_buffer, packet_length) == -1) {
                syslog(LOG_ERR, "Failed to write to file: %s", strerror(errno));
                close(data_fd);
                close(socket_fd);
                return;
            }

            lseek(data_fd, 0, SEEK_SET);
            ssize_t bytes_read;
            while ((bytes_read = read(data_fd, file_buffer, CONNECTION_BUFFER_SIZE)) > 0) {
                if (send(socket_fd, file_buffer, bytes_read, 0) == -1) {
                    syslog(LOG_ERR, "Failed to send data to client: %s", strerror(errno));
                    close(data_fd);
                    close(socket_fd);
                    return;
                }
            }

            if (bytes_read == -1) {
                syslog(LOG_ERR, "Failed to read from file: %s", strerror(errno));
                close(data_fd);
                close(socket_fd);
                return;
            }
        }
    }

    if (bytes_received == -1) {
        syslog(LOG_ERR, "Failed to receive data: %s", strerror(errno));
    }

    /* Closing data file */
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
        start_daemon();
    }

    /* Adding signal handler */
    if (signal(SIGTERM, signal_handler) == SIG_ERR) {
        syslog(LOG_ERR, "Failed to register SIGTERM: %s", strerror(errno));
        cleanup_server();
        return EXIT_FAILURE;
    }
    if (signal(SIGINT, signal_handler) == SIG_ERR) {
        syslog(LOG_ERR, "Failed to register SIGINT: %s", strerror(errno));
        cleanup_server();
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
        cleanup_server();
        return EXIT_SUCCESS;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(SERVER_PORT);

    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        syslog(LOG_ERR, "Bind failed: %s", strerror(errno));
        cleanup_server();
        return EXIT_SUCCESS;
    }

    if (listen(server_sock, BACKLOG) == -1) {
        syslog(LOG_ERR, "Listen failed: %s", strerror(errno));
        cleanup_server();
        return EXIT_SUCCESS;
    }

    syslog(LOG_INFO, "Listening on port %d", SERVER_PORT);

    while (!should_terminate) {
        int client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &addr_len);
        if (client_sock == -1) {
            syslog(LOG_ERR, "Accept failed: %s", strerror(errno));
            continue;
        }

        handle_connection(client_sock, &client_addr);
    }

    cleanup_server();
    
    return EXIT_SUCCESS;
}
