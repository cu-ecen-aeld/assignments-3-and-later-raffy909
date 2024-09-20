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
#include <pthread.h>

#include "read_line.h"
#include "queue.h"

#define USE_AESD_CHAR_DEVICE

#define LOG_IDENTITY            "aesdsocketd"
#define SERVER_PORT             9000
#define BACKLOG                 10
#define CONNECTION_BUFFER_SIZE  65536

#ifndef USE_AESD_CHAR_DEVICE
#define CONNECTION_DATA_FILE    "/var/tmp/aesdsocketdata"
#define TIMER_SLEEP             10
#else
#define CONNECTION_DATA_FILE    "/dev/aesdchar"
#endif

/* Thread list types */
typedef struct {
    pthread_t thread;
    bool thread_complete;
    int connection_fd;
    struct sockaddr_in client_addr;
} thread_data_t;

typedef struct list_data_s list_data_t;
struct list_data_s {
    thread_data_t* item;
    LIST_ENTRY(list_data_s) entries;
};

/* Filestore types */
typedef struct file_store_s {
    int fd;
    pthread_mutex_t file_mutex; 
} file_store_t;


int server_sock = -1;
file_store_t filestore;
bool should_terminate = false;

void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        syslog(LOG_INFO, "Caught signal, exiting");
        close(server_sock); // this to avoid -> Accept failed: Bad file descriptor, not very graceful
        should_terminate = true;
    }
}

/* Filestore functions */
int init_filestore() {
    filestore.fd = open(CONNECTION_DATA_FILE, O_RDWR | O_CREAT | O_APPEND, 0644);
    if (filestore.fd == -1) {
        return -1;
    }

    if (pthread_mutex_init(&(filestore.file_mutex), NULL) == -1) {
        close(filestore.fd);
        return -1;
    }

    return 0;
}

int filestore_write(char* data, size_t len) {

    int rc = pthread_mutex_lock(&(filestore.file_mutex));
    if ( rc != 0 ) {
        syslog(LOG_ERR, "Failed to acquire filestore mutex");
        return -1;
    }
    if (write(filestore.fd, data, len) == -1) {
        syslog(LOG_ERR, "Failed to write to file: %s", strerror(errno));
        return -1;
    }
    rc = pthread_mutex_unlock(&(filestore.file_mutex));
    if ( rc != 0 ) {
        syslog(LOG_ERR, "Failed to release filestore mutex");
        return -1;
    }
    
    return 0;
}

int filestore_read_to_dest(int dest_fd) {
    int ret = 0;
    size_t bytes_read;
    char file_buffer[1024];
    
    int rc = pthread_mutex_lock(&(filestore.file_mutex));
    if ( rc != 0 ) {
        syslog(LOG_ERR, "Failed to acquire filestore mutex");
        ret = -1;
    } else {
        lseek(filestore.fd, 0, SEEK_SET);
        while ((bytes_read = read(filestore.fd, file_buffer, 1024)) > 0 && ret != -1) {
            syslog(LOG_DEBUG, "Sending %ld", bytes_read);
            if (send(dest_fd, file_buffer, bytes_read, 0) == -1) {
                syslog(LOG_ERR, "Failed to send data to client: %s", strerror(errno));
                ret = -1;
            }
        }
        if (bytes_read == -1UL) {
            syslog(LOG_ERR, "Failed to read from file: %s", strerror(errno));
            ret = -1;
        }
        rc = pthread_mutex_unlock(&(filestore.file_mutex));
        if ( rc != 0 ) {
            syslog(LOG_ERR, "Failed to release filestore mutex");
            ret =  -1;
        }
    }

    fclose(filestore.fd);
    return ret;
}   

void filestore_close() {

    if (remove(CONNECTION_DATA_FILE) == 0) {
        syslog(LOG_INFO, "Deleted file %s", CONNECTION_DATA_FILE);
    } else {
        syslog(LOG_ERR, "Failed to delete file %s: %s", CONNECTION_DATA_FILE, strerror(errno));
    }
    //free(filestore);
}

void* handle_connection(void *thread_args) {
    ssize_t bytes_received;
    char conn_buffer[CONNECTION_BUFFER_SIZE];
    char client_ip[INET_ADDRSTRLEN];
    
    thread_data_t* thread_func_args = (thread_data_t *) thread_args;
    bool operation_failed = false;

    /* Logging connection ip address */
    inet_ntop(AF_INET, &(thread_func_args->client_addr), client_ip, INET_ADDRSTRLEN);
    syslog(LOG_INFO, "[Thread-%ld] Accepted connection from %s", thread_func_args->thread, client_ip);

    /* Handling data */
    while(((bytes_received = read_line(thread_func_args->connection_fd, conn_buffer, CONNECTION_BUFFER_SIZE)) > 0 && !should_terminate) && !operation_failed) {
        syslog(LOG_DEBUG, "[Thread-%ld] Newline found", thread_func_args->thread); 
    
        if(filestore_write(conn_buffer, bytes_received) == -1) {
            syslog(LOG_ERR, "[Thread-%ld] Write failed to filestore\n", thread_func_args->thread);
            operation_failed = true;
        }

        if(filestore_read_to_dest(thread_func_args->connection_fd) == -1) {
            syslog(LOG_ERR, "[Thread-%ld] Read failed to filestore\n", thread_func_args->thread);
            operation_failed = true;
        }
    }

    if (bytes_received == -1) {
        syslog(LOG_ERR, "[Thread-%ld] Failed to receive data: %s", thread_func_args->thread, strerror(errno));
    }

    /* Closing connection */
    close(thread_func_args->connection_fd);
    thread_func_args->thread_complete = true;
    /* Logging closed connection */
    syslog(LOG_INFO, "[Thread-%ld] Closed connection from %s", thread_func_args->thread, client_ip);

    return thread_func_args;
}

/* Simplest solution*/
#ifndef USE_AESD_CHAR_DEVICE
void* timer_thread(void *args) {
    char timestamp[100];
    while (true) {
        sleep(TIMER_SLEEP);

        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        strftime(timestamp, sizeof(timestamp), "timestamp:%a, %d %b %Y %H:%M:%S %z\n", tm_info);
        filestore_write(timestamp, strlen(timestamp));
        
    }

    syslog(LOG_INFO, "Timer thread terminating...\n");
    
    return args;
}
#endif

list_data_t* init_conn_list_item(int conn_fd, struct sockaddr_in client_addr) {
     thread_data_t* thread_data = (thread_data_t*)malloc(sizeof(thread_data_t));
    if (thread_data == NULL) {
        return NULL;
    }

    thread_data->thread_complete = false;
    thread_data->connection_fd = conn_fd;
    thread_data->client_addr = client_addr;

    list_data_t* list_data = (list_data_t*)malloc(sizeof(list_data_t));
    if (list_data == NULL) {
        if (thread_data != NULL) free(thread_data);
        return NULL;
    }
    
    list_data->item = thread_data;

    return list_data;
}

void free_conn_list_item(list_data_t* list_data) {
    if (list_data == NULL) return;

    if (list_data->item != NULL) {
        free(list_data->item);
        list_data->item = NULL;
    }
    free(list_data);        
    list_data = NULL;
}

int main(int argc, char const *argv[]) {
    bool run_as_daemon = false;
    
    struct sockaddr_in server_addr;
    socklen_t addr_len = sizeof(server_addr);

    /* Init threads list */
    LIST_HEAD(listhead, list_data_s) head;
    LIST_INIT(&head);

    /* timer thread */
#ifndef USE_AESD_CHAR_DEVICE
    pthread_t timer_thread_id;
#endif
    openlog(LOG_IDENTITY, LOG_PID, LOG_USER);

    /* Init filestore */
    int err = init_filestore(CONNECTION_DATA_FILE);
    if(err == -1) {
        syslog(LOG_ERR, "Filestore init failed: %s", strerror(errno));
        closelog();
    }

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
        filestore_close();
        return EXIT_FAILURE;
    }
    if (signal(SIGINT, signal_handler) == SIG_ERR) {
        syslog(LOG_ERR, "Failed to register SIGINT: %s", strerror(errno));
        closelog();
        filestore_close();
        return EXIT_FAILURE;
    }

    /* Init timer thread*/
#ifndef USE_AESD_CHAR_DEVICE
    if(pthread_create(&timer_thread_id, NULL, timer_thread, NULL) == -1) {
        syslog(LOG_ERR, "Failed to create timer thread: %s", strerror(errno));
        
        closelog();
        filestore_close();
        
        return EXIT_FAILURE;
    }
#endif

    /* Create socket */
    if ((server_sock = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        syslog(LOG_ERR, "Socket creation failed: %s", strerror(errno));
        closelog();
        filestore_close();
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
        filestore_close();
        return EXIT_SUCCESS;
    }

    if (listen(server_sock, BACKLOG) == -1) {
        syslog(LOG_ERR, "Listen failed: %s", strerror(errno));
        if (server_sock != -1) {
            close(server_sock);
        }
        syslog(LOG_INFO, "Server exiting");
        closelog();
        filestore_close();
        return EXIT_SUCCESS;
    }

    syslog(LOG_INFO, "Listening on port %d", SERVER_PORT);

    while (!should_terminate) {
        if(server_sock != -1) {
            struct sockaddr_in client_addr;
            int client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &addr_len);
            if (client_sock == -1) {
                syslog(LOG_ERR, "Accept failed: %s", strerror(errno));
            } else {
                /* Start connection handling thread */
                list_data_t* new_data = init_conn_list_item(client_sock, client_addr);
                if(new_data != NULL) {
                    LIST_INSERT_HEAD(&head, new_data, entries);
                    int rc = pthread_create(&new_data->item->thread,
                                    NULL, // Use default attributes
                                    handle_connection,
                                    new_data->item);
                    syslog(LOG_ERR, "Thread creation result %d \n", rc);
                } else {
                    syslog(LOG_ERR, "Failed to allocate item \n");
                }
            }  
        }
        /* Check and join threads, free memory */
        list_data_t* data;
        list_data_t* loop = NULL;
        LIST_FOREACH_SAFE(data, &head, entries, loop) {
            if (data->item->thread_complete) {
                if (pthread_join(data->item->thread, NULL) == 0) {
                    syslog(LOG_DEBUG, "Thread-%ld terminated\n", data->item->thread);
                }
                LIST_REMOVE(data, entries);
                free_conn_list_item(data);
            }
        }
    }

    syslog(LOG_DEBUG, "Starting cleanup procedure...\n");
    
    /* TODO: move to function */
    /* Do a check outside the loop */ 
    list_data_t* data;
    if(!LIST_EMPTY(&head)) {
        LIST_FOREACH(data, &head, entries) {
            if (data->item->thread_complete) {
                if (pthread_join(data->item->thread, NULL) == 0) {
                    syslog(LOG_DEBUG, "Thread-%ld terminated\n", data->item->thread);
                }
                LIST_REMOVE(data, entries);
                free_conn_list_item(data);
            }
        }
    } else {
        syslog(LOG_DEBUG, "No more running threads\n");
    }   
#ifndef USE_AESD_CHAR_DEVICE
    filestore_close(filestore);
#endif
    if (server_sock != -1) {
        close(server_sock);
    }
#ifndef USE_AESD_CHAR_DEVICE
    pthread_cancel(timer_thread_id);
    pthread_join(timer_thread_id, NULL);
#endif
    syslog(LOG_INFO, "Server exiting\n");
    closelog();
    
    return EXIT_SUCCESS;
}
