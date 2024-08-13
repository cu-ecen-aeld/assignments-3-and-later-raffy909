#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void sleep_ms(int milliseconds)
{
    usleep(milliseconds * 1000);
}

void* threadfunc(void* thread_param)
{  
    // TODO: wait, obtain mutex, wait, release mutex as described by thread_data structure
    // hint: use a cast like the one below to obtain thread arguments from your parameter
    //struct thread_data* thread_func_args = (struct thread_data *) thread_param;
    struct thread_data* thread_func_args = (struct thread_data *) thread_param;

    DEBUG_LOG("Begin thread sleep before mutex acquisition");
    sleep_ms(thread_func_args->wait_to_obtain_ms);
    DEBUG_LOG("Attempting to obtain mutex");
    int rc = pthread_mutex_lock(thread_func_args->mutex);
    if ( rc != 0 ) {
        ERROR_LOG("Failed to acquire mutex in thread: %d", rc);
        thread_func_args->thread_complete_success = false;
    } else {
        DEBUG_LOG("Begin thread sleep after mutex acquisition");
        sleep_ms(thread_func_args->wait_to_release_ms);
        DEBUG_LOG("Releasing mutex");
        rc = pthread_mutex_unlock(thread_func_args->mutex);
        if ( rc != 0 ) {
            ERROR_LOG("Failed to release mutex in thread: %d",rc);
            thread_func_args->thread_complete_success = false;
        } else {
            DEBUG_LOG("Mutex succesfully released. Done");
            thread_func_args->thread_complete_success = true;
        }
    }
    return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
    /**
     * TODO: allocate memory for thread_data, setup mutex and wait arguments, pass thread_data to created thread
     * using threadfunc() as entry point.
     *
     * return true if successful.
     *
     * See implementation details in threading.h file comment block
     */
    struct thread_data *thread_data = (struct thread_data *) malloc(sizeof(thread_data));
    
    thread_data->mutex = mutex;
    thread_data->wait_to_obtain_ms = wait_to_obtain_ms;
    thread_data->wait_to_release_ms = wait_to_release_ms;

    int rc = pthread_create(thread,
                            NULL, // Use default attributes
                            threadfunc,
                            thread_data);
    if ( rc == 0 ) {
        return true;
    }
    return false;
}

