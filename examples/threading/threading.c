#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{
    // TODO: wait, obtain mutex, wait, release mutex as described by thread_data structure
    // hint: use a cast like the one below to obtain thread arguments from your parameter
    //struct thread_data* thread_func_args = (struct thread_data *) thread_param;
    struct thread_data *td = (struct thread_data*) thread_param;
    struct timeval wait_to_obtain = {
            .tv_sec = td->wait_to_obtain_ms / 1000,
            .tv_usec = (td->wait_to_obtain_ms % 1000)*1000};
    struct timeval wait_to_release = {
            .tv_sec = td->wait_to_release_ms / 1000,
            .tv_usec = (td->wait_to_release_ms % 1000)*1000};

    int res = select(0, NULL, NULL, NULL, &wait_to_obtain);
    if (res != 0) {
        ERROR_LOG("select/sleep failed:");
        perror("select");
        td->thread_complete_success = false;
        return thread_param;
    }

    res = pthread_mutex_lock(td->mutex);
    if (res != 0) {
        ERROR_LOG("pthread_mutex_lock failed:%d", res);
        td->thread_complete_success = false;
        return thread_param;
    }

    res = select(0, NULL, NULL, NULL, &wait_to_release);
    if (res != 0) {
        ERROR_LOG("select/sleep failed:");
        perror("select");
        td->thread_complete_success = false;
        return thread_param;
    }

    res = pthread_mutex_unlock(td->mutex);
    if (res != 0) {
        ERROR_LOG("pthread_mutex_unlock failed:%d", res);
        td->thread_complete_success = false;
        return thread_param;
    }

    td->thread_complete_success = true;
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
    struct thread_data *td = (struct thread_data*) malloc(sizeof(struct thread_data));
    td->mutex = mutex;
    td->wait_to_obtain_ms = wait_to_obtain_ms;
    td->wait_to_release_ms = wait_to_release_ms;
    td->thread_complete_success = false;

    int res = pthread_create(thread, NULL, threadfunc, td);
    if (res == 0) {
        DEBUG_LOG("pthread_create successful");
        return true;
    }
    ERROR_LOG("pthread_create failed with:%d",res);
    return false;
}

