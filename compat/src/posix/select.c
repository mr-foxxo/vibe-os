#include "compat_fd_state.h"
#include "compat_signal_state.h"

#include <compat/posix/errno.h>
#include <compat/posix/unistd.h>
#include <include/userland_api.h>
#include <poll.h>
#include <sys/select.h>
struct mk_network_info;
struct mk_network_status;
struct mk_network_scan_info;
struct mk_network_connect_request;
#include <lang/include/vibe_app_runtime.h>

static int g_compat_network_events_subscribed = 0;

static int compat_syscall5(int num, int a, int b, int c, int d, int e) {
    int ret;

    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(num), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e)
                     : "memory", "cc");
    return ret;
}

static void compat_network_events_subscribe_once(void) {
    if (g_compat_network_events_subscribed) {
        return;
    }
    if (compat_syscall5(SYSCALL_NETWORK_EVENT_SUBSCRIBE, 0, 0, 0, 0, 0) == 0) {
        g_compat_network_events_subscribed = 1;
    }
}

static void compat_network_apply_event(const struct mk_network_event *event) {
    if (event == 0 || event->handle <= 0) {
        return;
    }

    switch (event->event_type) {
    case MK_NETWORK_EVENT_SOCKET_RECV:
        if (event->byte_count > 0u) {
            compat_fd_socket_update_ready_by_host(event->handle,
                                                 COMPAT_SOCKET_READY_RECV,
                                                 0);
        }
        break;
    case MK_NETWORK_EVENT_SOCKET_SEND:
        compat_fd_socket_update_ready_by_host(event->handle,
                                             COMPAT_SOCKET_READY_SEND,
                                             0);
        break;
    case MK_NETWORK_EVENT_SOCKET_ACCEPT:
        compat_fd_socket_update_ready_by_host(event->handle,
                                             COMPAT_SOCKET_READY_ACCEPT,
                                             0);
        break;
    case MK_NETWORK_EVENT_SOCKET_CLOSED:
        compat_fd_socket_update_ready_by_host(event->handle,
                                             0,
                                             COMPAT_SOCKET_READY_RECV |
                                                 COMPAT_SOCKET_READY_SEND |
                                                 COMPAT_SOCKET_READY_ACCEPT |
                                                 COMPAT_SOCKET_READY_CONNECT);
        compat_fd_socket_set_error_by_host(event->handle, ENOTCONN);
        break;
    default:
        break;
    }
}

static void compat_network_drain_events(void) {
    struct mk_network_event event;

    compat_network_events_subscribe_once();
    if (!g_compat_network_events_subscribed) {
        return;
    }

    while (compat_syscall5(SYSCALL_NETWORK_EVENT_RECV,
                           (int)(uintptr_t)&event,
                           0,
                           0,
                           0,
                           0) == 0) {
        compat_network_apply_event(&event);
    }
}

static unsigned int compat_timeval_to_ms(const struct timeval *timeout) {
    unsigned long long ms;

    if (timeout == 0) {
        return 0u;
    }
    if (timeout->tv_sec < 0 || timeout->tv_usec < 0 || timeout->tv_usec >= 1000000l) {
        return 0u;
    }
    ms = (unsigned long long)timeout->tv_sec * 1000ull;
    ms += (unsigned long long)(timeout->tv_usec / 1000l);
    if (timeout->tv_usec > 0 && (timeout->tv_usec % 1000l) != 0) {
        ms += 1ull;
    }
    if (ms > 0xffffffffull) {
        ms = 0xffffffffull;
    }
    return (unsigned int)ms;
}

static int compat_fd_ready_for_read(int fd) {
    const struct compat_fd_entry *entry;

    if (!compat_fd_is_valid(fd)) {
        return -1;
    }
    if (fd == STDIN_FILENO) {
        return 1;
    }
    entry = compat_fd_get_const(fd);
    if (entry != 0 && entry->kind == COMPAT_FD_KIND_SOCKET) {
        int ready = compat_fd_socket_readiness(fd);

        if ((ready & (COMPAT_SOCKET_READY_RECV | COMPAT_SOCKET_READY_ACCEPT)) != 0) {
            return 1;
        }
        return 0;
    }
    return 1;
}

static int compat_fd_ready_for_write(int fd) {
    const struct compat_fd_entry *entry;

    if (!compat_fd_is_valid(fd)) {
        return -1;
    }
    entry = compat_fd_get_const(fd);
    if (entry != 0 && entry->kind == COMPAT_FD_KIND_SOCKET) {
        return (compat_fd_socket_readiness(fd) & COMPAT_SOCKET_READY_SEND) != 0 ? 1 : 0;
    }
    return 1;
}

static int compat_fd_ready_for_except(int fd) {
    const struct compat_fd_entry *entry;

    if (!compat_fd_is_valid(fd)) {
        return -1;
    }
    entry = compat_fd_get_const(fd);
    if (entry != 0 && entry->kind == COMPAT_FD_KIND_SOCKET) {
        return entry->socket_error != 0 ? 1 : 0;
    }
    return 0;
}

static int compat_select_scan(int nfds,
                              fd_set *readfds,
                              const fd_set *in_read,
                              fd_set *writefds,
                              const fd_set *in_write,
                              fd_set *exceptfds,
                              const fd_set *in_except) {
    int fd;
    int ready = 0;

    for (fd = 0; fd < nfds; ++fd) {
        if (readfds != 0 && in_read != 0 && FD_ISSET(fd, in_read)) {
            if (compat_fd_ready_for_read(fd) > 0) {
                FD_SET(fd, readfds);
                ready += 1;
            }
        }
        if (writefds != 0 && in_write != 0 && FD_ISSET(fd, in_write)) {
            if (compat_fd_ready_for_write(fd) > 0) {
                FD_SET(fd, writefds);
                ready += 1;
            }
        }
        if (exceptfds != 0 && in_except != 0 && FD_ISSET(fd, in_except)) {
            if (compat_fd_ready_for_except(fd) > 0) {
                FD_SET(fd, exceptfds);
                ready += 1;
            }
        }
    }

    return ready;
}

static int compat_select_wait_loop(int nfds,
                                   fd_set *readfds,
                                   const fd_set *in_read,
                                   fd_set *writefds,
                                   const fd_set *in_write,
                                   fd_set *exceptfds,
                                   const fd_set *in_except,
                                   unsigned int timeout_ms,
                                   int has_timeout) {
    unsigned long long deadline_ms = 0ull;

    if (has_timeout) {
        deadline_ms = vibe_app_millis() + timeout_ms;
    }

    for (;;) {
        int ready;

        compat_network_drain_events();
        ready = compat_select_scan(nfds,
                                   readfds,
                                   in_read,
                                   writefds,
                                   in_write,
                                   exceptfds,
                                   in_except);
        if (ready != 0) {
            return ready;
        }
        if (compat_signal_dispatch_for_wait() != 0) {
            errno = EINTR;
            return -1;
        }
        if (has_timeout) {
            unsigned long long now = vibe_app_millis();

            if (now >= deadline_ms) {
                return 0;
            }
            vibe_app_sleep_ms(1u);
            continue;
        }
        if (nfds == 0) {
            vibe_app_yield();
            return 0;
        }
        vibe_app_yield();
    }
}

int select(int nfds, fd_set *readfds, fd_set *writefds,
           fd_set *exceptfds, struct timeval *timeout) {
    fd_set in_read;
    fd_set in_write;
    fd_set in_except;
    unsigned int timeout_ms = 0u;
    int ready;

    if (nfds < 0 || nfds > FD_SETSIZE) {
        errno = EINVAL;
        return -1;
    }

    if (readfds != 0) {
        in_read = *readfds;
        FD_ZERO(readfds);
    }
    if (writefds != 0) {
        in_write = *writefds;
        FD_ZERO(writefds);
    }
    if (exceptfds != 0) {
        in_except = *exceptfds;
        FD_ZERO(exceptfds);
    }

    if (timeout != 0) {
        timeout_ms = compat_timeval_to_ms(timeout);
    }

    ready = compat_select_wait_loop(nfds,
                                    readfds,
                                    readfds != 0 ? &in_read : 0,
                                    writefds,
                                    writefds != 0 ? &in_write : 0,
                                    exceptfds,
                                    exceptfds != 0 ? &in_except : 0,
                                    timeout_ms,
                                    timeout != 0);
    if (timeout != 0) {
        timeout->tv_sec = 0;
        timeout->tv_usec = 0;
    }
    return ready;
}

int pselect(int nfds, fd_set *readfds, fd_set *writefds,
            fd_set *exceptfds, const struct timespec *timeout,
            const sigset_t *sigmask) {
    struct timeval tv;

    (void)sigmask;
    if (timeout == 0) {
        return select(nfds, readfds, writefds, exceptfds, 0);
    }

    tv.tv_sec = timeout->tv_sec;
    tv.tv_usec = (suseconds_t)(timeout->tv_nsec / 1000l);
    return select(nfds, readfds, writefds, exceptfds, &tv);
}

int ppoll(struct pollfd *fds, nfds_t nfds, const struct timespec *timeout_ts,
          const sigset_t *sigmask) {
    struct timeval tv;
    fd_set readfds;
    fd_set writefds;
    fd_set exceptfds;
    fd_set *read_ptr = 0;
    fd_set *write_ptr = 0;
    fd_set *except_ptr = 0;
    int maxfd = 0;
    nfds_t i;
    int selected;
    int ready = 0;

    (void)sigmask;
    if (fds == 0 && nfds != 0u) {
        errno = EFAULT;
        return -1;
    }

    FD_ZERO(&readfds);
    FD_ZERO(&writefds);
    FD_ZERO(&exceptfds);

    for (i = 0; i < nfds; ++i) {
        fds[i].revents = 0;
        if (!compat_fd_is_valid(fds[i].fd)) {
            fds[i].revents = POLLNVAL;
            ready += 1;
            continue;
        }
        if ((fds[i].events & (POLLIN | POLLPRI)) != 0) {
            FD_SET(fds[i].fd, &readfds);
            read_ptr = &readfds;
        }
        if ((fds[i].events & POLLOUT) != 0) {
            FD_SET(fds[i].fd, &writefds);
            write_ptr = &writefds;
        }
        if ((fds[i].events & (POLLERR | POLLHUP)) != 0) {
            FD_SET(fds[i].fd, &exceptfds);
            except_ptr = &exceptfds;
        }
        if (fds[i].fd + 1 > maxfd) {
            maxfd = fds[i].fd + 1;
        }
    }

    if (ready != 0) {
        return ready;
    }

    if (timeout_ts != 0) {
        tv.tv_sec = timeout_ts->tv_sec;
        tv.tv_usec = (suseconds_t)(timeout_ts->tv_nsec / 1000l);
        selected = select(maxfd, read_ptr, write_ptr, except_ptr, &tv);
    } else {
        selected = select(maxfd, read_ptr, write_ptr, except_ptr, 0);
    }
    if (selected < 0) {
        return -1;
    }

    for (i = 0; i < nfds; ++i) {
        if (read_ptr != 0 && FD_ISSET(fds[i].fd, read_ptr)) {
            fds[i].revents |= (short)(fds[i].events & (POLLIN | POLLPRI));
        }
        if (write_ptr != 0 && FD_ISSET(fds[i].fd, write_ptr)) {
            fds[i].revents |= (short)(fds[i].events & POLLOUT);
        }
        if (except_ptr != 0 && FD_ISSET(fds[i].fd, except_ptr)) {
            fds[i].revents |= (short)(fds[i].events & (POLLERR | POLLHUP));
        }
        if (fds[i].revents != 0) {
            ready += 1;
        }
    }
    return ready;
}

int poll(struct pollfd *fds, nfds_t nfds, int timeout) {
    struct timespec ts;

    if (timeout < 0) {
        return ppoll(fds, nfds, 0, 0);
    }

    ts.tv_sec = (time_t)(timeout / 1000);
    ts.tv_nsec = (long)(timeout % 1000) * 1000000l;
    return ppoll(fds, nfds, &ts, 0);
}
