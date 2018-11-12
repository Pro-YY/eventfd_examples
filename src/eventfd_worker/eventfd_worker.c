#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>

struct timespec tp;

#define log_error(msg, ...)                                                 \
    do {                                                                    \
        clock_gettime(CLOCK_MONOTONIC, &tp);                                \
        if (errno > 0) {                                                    \
            fprintf(stderr, "[%ld.%ld] [ERROR](%s:%d) "msg" [%s:%d]\n",     \
                tp.tv_sec, tp.tv_nsec, __FILE__, __LINE__, ##__VA_ARGS__,   \
                strerror(errno), errno);                                    \
        }                                                                   \
        else {                                                              \
            fprintf(stderr, "[%ld.%ld] [ERROR](%s:%d)"msg"\n",              \
                tp.tv_sec, tp.tv_nsec, __FILE__, __LINE__, ##__VA_ARGS__);  \
        }                                                                   \
        fflush(stdout);                                                     \
    } while (0)


#define exit_error(...)                                                     \
    do { log_error(__VA_ARGS__); exit(EXIT_FAILURE); } while (0)


#define log_debug(msg, ...)                                                 \
    do {                                                                    \
        clock_gettime(CLOCK_MONOTONIC, &tp);                                \
        fprintf(stdout, "[%ld.%ld] [DEBUG] "msg"\n",                        \
            tp.tv_sec, tp.tv_nsec, ##__VA_ARGS__);                          \
        fflush(stdout);                                                     \
    } while (0)


#define NUM_PRODUCERS 4
#define NUM_CONSUMERS 2
#define MAX_EVENTS_SIZE 1024


typedef struct thread_info {
    pthread_t thread_id;
    int rank;
    int epfd;
} thread_info_t;


static void do_task() {
    return;
}


static void *consumer_routine(void *data) {
    struct thread_info *c = (struct thread_info *)data;
    struct epoll_event *events;
    int epfd = c->epfd;
    int nfds = -1;
    int i = -1;
    int ret = -1;
    uint64_t v;
    int num_done = 0;

    events = calloc(MAX_EVENTS_SIZE, sizeof(struct epoll_event));
    if (events == NULL) exit_error("calloc epoll events\n");

    for (;;) {
        nfds = epoll_wait(epfd, events, MAX_EVENTS_SIZE, 1000);
        for (i = 0; i < nfds; i++) {
            if (events[i].events & EPOLLIN) {
                log_debug("[consumer-%d] got event from fd-%d",
                        c->rank, events[i].data.fd);
                ret = read(events[i].data.fd, &v, sizeof(v));
                if (ret < 0) {
                    log_error("[consumer-%d] failed to read eventfd", c->rank);
                    continue;
                }
                close(events[i].data.fd);
                do_task();
                log_debug("[consumer-%d] tasks done: %d", c->rank, ++num_done);
            }
        }
    }
}


// infinite constant workload
static void *producer_routine(void *data) {
    struct thread_info *p = (struct thread_info *)data;
    struct epoll_event event;
    int epfd = p->epfd;
    int efd = -1;
    int ret = -1;
    int interval = 1;

    log_debug("[producer-%d] issues 1 task per %d second", p->rank, interval);
    while (1) {
        efd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        if (efd == -1) exit_error("eventfd create: %s", strerror(errno));
        event.data.fd = efd;
        event.events = EPOLLIN | EPOLLET;
        ret = epoll_ctl(epfd, EPOLL_CTL_ADD, efd, &event);
        if (ret != 0) exit_error("epoll_ctl");
        ret = write(efd, &(uint64_t){1}, sizeof(uint64_t));
        if (ret != 8) log_error("[producer-%d] failed to write eventfd", p->rank);
        sleep(interval);
    }
}


// instant spike workload
static void *producer_routine_spike(void *data) {
    struct thread_info *p = (struct thread_info *)data;
    struct epoll_event event;
    int epfd = p->epfd;
    int efd = -1;
    int ret = -1;
    int num_task = 1000000;

    log_debug("[producer-%d] will issue %d tasks", p->rank, num_task);
    for (int i = 0; i < num_task; i++) {
        efd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        if (efd == -1) exit_error("eventfd create: %s", strerror(errno));
        event.data.fd = efd;
        event.events = EPOLLIN | EPOLLET;
        ret = epoll_ctl(epfd, EPOLL_CTL_ADD, efd, &event);
        if (ret != 0) exit_error("epoll_ctl");
        ret = write(efd, &(uint64_t){1}, sizeof(uint64_t));
        if (ret != 8) log_error("[producer-%d] failed to write eventfd", p->rank);
    }
    return (void *)0;
}


int main(int argc, char *argv[]) {
    struct thread_info *p_list = NULL, *c_list = NULL;
    int epfd = -1;
    int ret = -1, i = -1;

    // create epoll fd
    epfd = epoll_create1(EPOLL_CLOEXEC);
    if (epfd == -1) exit_error("epoll_create1: %s", strerror(errno));

    // start consumers (as task worker)
    c_list = calloc(NUM_CONSUMERS, sizeof(struct thread_info));
    if (!c_list) exit_error("calloc");
    for (i = 0; i < NUM_CONSUMERS; i++) {
        c_list[i].rank = i;
        c_list[i].epfd = epfd;
        ret = pthread_create(&c_list[i].thread_id, NULL, consumer_routine, &c_list[i]);
        if (ret != 0) exit_error("pthread_create");
    }

    // start producers (as test load)
    p_list = calloc(NUM_PRODUCERS, sizeof(struct thread_info));
    if (!p_list) exit_error("calloc");
    for (i = 0; i < NUM_PRODUCERS; i++) {
        p_list[i].rank = i;
        p_list[i].epfd = epfd;
        ret = pthread_create(&p_list[i].thread_id, NULL, producer_routine, &p_list[i]);
        if (ret != 0) exit_error("pthread_create");
    }

    // join and exit
    for (i = 0; i < NUM_PRODUCERS; i++) {
        ret = pthread_join(p_list[i].thread_id, NULL);
        if (ret != 0) exit_error("pthread_join");
    }
    for (i = 0; i < NUM_CONSUMERS; i++) {
        ret = pthread_join(c_list[i].thread_id, NULL);
        if (ret != 0) exit_error("pthread_join");
    }

    free(p_list);
    free(c_list);

    return EXIT_SUCCESS;
}
