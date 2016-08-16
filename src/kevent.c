#include "kevent.h"
#include <sys/types.h>
#include <error.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include<sys/eventfd.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>


static inline void  get_new_value(struct itimerspec* new_value,int tms,int iscycle)
{
//    printf("reset time\n");
    //    struct itimerspec new_value;
    struct timespec now;
    int32 s = tms / 1000;
    int32 ms = tms%1000;
    if (clock_gettime(CLOCK_REALTIME, &now) == -1)
        perror("clock_gettime");
    if(tms!=0)
    {
        new_value->it_value.tv_sec =now.tv_sec + s +((now.tv_nsec + ms*1000*1000)/(1000*1000*1000));
        new_value->it_value.tv_nsec = (now.tv_nsec + ms*1000*1000)%(1000*1000*1000);
    }
    else
    {
        new_value->it_value.tv_sec = 0;
        new_value->it_value.tv_nsec = 0;
    }
    if(iscycle)
    {
        new_value->it_interval.tv_sec = s;
        new_value->it_interval.tv_nsec =ms * 1000 * 1000;
    }
    else
    {
        new_value->it_interval.tv_sec =0;
        new_value->it_interval.tv_nsec = 0;
    }
}
/*
*
*
*/
int kevent_create_epollfd()
{
    int32 epollfd = epoll_create1(0);
      if (epollfd == -1) {
          perror("epoll_create1");
          exit(1);
      }
      return epollfd;
}

int kevent_bind( kevent* ket)
{
    struct epoll_event ev;
    if(ket != NULL && ket->fun !=NULL)
    {
        ev.events = EPOLLIN;
        ev.data.ptr = ket;
        if (epoll_ctl(ket->epoll_fd, EPOLL_CTL_ADD,ket->fd, &ev) == -1) {
            perror("epoll_ctl: ");
            return -1;
        }
        else
        {
            return 0;
        }
    }
    else
        return -1;
}

void kevent_null_callback( kevent *ket)
{
    //防止不断触发
    uint64_t value;
    read(ket->fd, &value, 8);
    printf("null_callback do nothing\n");
}
 kevent *kevent_new(int32 epoll_fd, int32 fd, kcallback fun)
{
     kevent* kt =  ( kevent*)malloc(sizeof( kevent));
    kt->epoll_fd = epoll_fd;
    kt->fd =fd;
    kt->fun = fun;
    kevent_bind(kt);
    return kt;
}
int32 kevent_break( kevent* ket)
{
    struct epoll_event ev;
    if(ket != NULL && ket->fun !=NULL)
    {
        ev.events = EPOLLIN;
        ev.data.ptr = ket;
        if (epoll_ctl(ket->epoll_fd, EPOLL_CTL_DEL,ket->fd, &ev) == -1) {
            perror("epoll_ctl: ");
            return -1;
        }
        else
        {
            return 0;
        }
    }
    else
        return -1;
}
 kevent*  kevent_efd_new(int32 epoll_fd,kcallback fun)
{
     kevent* kt =( kevent*)malloc(sizeof( kevent));
    kt->epoll_fd = epoll_fd;
    kt->fun = fun;
    kt->fd = eventfd(0,0);
    if (kt->fd == -1)
    {
        perror("eventfd create err!");
        goto label_err;
    }
    else
    {
        if(kevent_bind(kt) == -1)
        {
            perror("kevent_create err!");
            goto label_err;
        }
        else
            return kt;
    }
label_err:
    close(kt->fd);
    free(kt);
    return NULL;
}
void kevent_efd_delete( kevent* ket)
{
    kevent_break(ket);
    close(ket->fd);
    free(ket);
}
 kevent*  kevent_time_new(int32 epoll_fd,kcallback fun,int32 ms, int32 iscycle)
{
    struct itimerspec new_value;
    struct epoll_event ev;
     kevent* kt =( kevent*)malloc(sizeof( kevent));
    kt->epoll_fd = epoll_fd;
    kt->fun = fun;
    kt->fd = timerfd_create(CLOCK_REALTIME,0 );
    if(kt->fd==-1)
    {
          perror("timerfd create err!");
          goto label_err;
    }
    get_new_value(&new_value,ms,iscycle);
    if (timerfd_settime(kt->fd, TFD_TIMER_ABSTIME, &new_value, NULL) == -1)  {
        perror("timerfd_settime");
       goto label_err;
    }
    if(kevent_bind(kt) == -1)
    {
        perror("kevent_create");
       goto label_err;
    }
    else
        return kt;
label_err:
    close(kt->fd);
    free(kt);
    return NULL;
}
int32 kevent_time_stop( kevent* ket)
{
    struct itimerspec new_value;
    get_new_value(&new_value,0,0);
    if (timerfd_settime(ket->fd, TFD_TIMER_ABSTIME, &new_value, NULL) == -1)  {
        perror("timerfd_settime");
        return -1;
    }
}
void kevent_time_reset( kevent* ket,kcallback fun,int32 ms,int32 iscycle)
{
    struct itimerspec new_value;
    ket->fun = fun;
    get_new_value(&new_value,ms,iscycle);
    if (timerfd_settime(ket->fd, TFD_TIMER_ABSTIME, &new_value, NULL) == -1)  {
        perror("timerfd_settime");
        return -1;
    }
}
void kevent_time_delete( kevent* ket)
{
    kevent_break(ket);
    close(ket->fd);
    free(ket);
}
void kevent_time_free( kevent* ket)
{
    kevent_time_reset(ket,kevent_null_callback,0,0);
}
int kevent_time_isfree( kevent* ket)
{
    if(ket->fun == kevent_null_callback)
        return 1;
    else
        return 0;
}
void kevent_loop(int32 epollfd)
{
    int32 nfds, n;
    kevent*  ket;
    struct epoll_event  events[MAX_EVENTS];
    for(;;)
    {
        nfds = epoll_wait(epollfd,events,MAX_EVENTS,-1);
        if(nfds == -1)
        {
            perror("epoll_wait");
            exit(EXIT_FAILURE);
        }
        for (n = 0; n < nfds; ++n) {
            ket = ( kevent*)events[n].data.ptr;
            if(ket!=NULL&&ket->fun!=NULL)
                    (*(ket->fun))(ket);
        }
    }
}


void kevent_void_repeat( kevent *ket)
{
    uint64 value;
    read(ket->fd, &value, 8);
}

void kevent_efd_notice( kevent *ket)
{
    uint64  value;
    write(ket->fd, &value, 8);
}