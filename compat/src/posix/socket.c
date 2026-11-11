#include "compat_fd_state.h"

#include <compat/posix/errno.h>
#include <compat/posix/fcntl.h>
#include <compat/posix/unistd.h>
#include <compat/libc/string.h>
#include <lang/include/vibe_app_runtime.h>
#include <poll.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <include/userland_api.h>

#define COMPAT_SOCKET_STATE_BOUND     0x01
#define COMPAT_SOCKET_STATE_CONNECTED 0x02
#define COMPAT_SOCKET_STATE_LISTENING 0x04

static int compat_syscall5(int num, int a, int b, int c, int d, int e) {
    int ret;

    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(num), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e)
                     : "memory", "cc");
    return ret;
}

static int compat_socket_validate_fd(int sockfd, struct compat_fd_entry **entry_out) {
    struct compat_fd_entry *entry = compat_fd_get(sockfd);

    if (entry == 0) {
        errno = EBADF;
        return -1;
    }
    if (entry->kind != COMPAT_FD_KIND_SOCKET) {
        errno = ENOTSOCK;
        return -1;
    }
    if (entry_out != 0) {
        *entry_out = entry;
    }
    return 0;
}

static int compat_socket_is_nonblocking(const struct compat_fd_entry *entry, int flags) {
    return entry != 0 &&
           (((entry->flags & O_NONBLOCK) != 0) || ((flags & SOCK_NONBLOCK) != 0));
}

static int compat_socket_wait_ready(int sockfd, short events) {
    struct pollfd pfd;

    pfd.fd = sockfd;
    pfd.events = events;
    pfd.revents = 0;
    return poll(&pfd, 1u, -1);
}

static int compat_socket_validate_msghdr(const struct msghdr *msg, int allow_name) {
    int i;

    if (msg == 0 || msg->msg_iov == 0 || msg->msg_iovlen < 0) {
        errno = EINVAL;
        return -1;
    }
    if (!allow_name && msg->msg_name != 0 && msg->msg_namelen != 0u) {
        errno = EISCONN;
        return -1;
    }
    for (i = 0; i < msg->msg_iovlen; ++i) {
        if (msg->msg_iov[i].iov_len != 0u && msg->msg_iov[i].iov_base == 0) {
            errno = EFAULT;
            return -1;
        }
    }
    return 0;
}

static int compat_socket_iov_length(const struct iovec *iov, int iovlen, size_t *total_out) {
    size_t total = 0u;
    int i;

    if (iovlen < 0) {
        errno = EINVAL;
        return -1;
    }
    for (i = 0; i < iovlen; ++i) {
        if (iov[i].iov_len > ((size_t)-1) - total) {
            errno = EMSGSIZE;
            return -1;
        }
        total += iov[i].iov_len;
    }
    if (total_out != 0) {
        *total_out = total;
    }
    return 0;
}

static void compat_socket_gather_iov(void *dst, const struct iovec *iov, int iovlen) {
    uint8_t *cursor = (uint8_t *)dst;
    int i;

    for (i = 0; i < iovlen; ++i) {
        if (iov[i].iov_len == 0u) {
            continue;
        }
        memcpy(cursor, iov[i].iov_base, iov[i].iov_len);
        cursor += iov[i].iov_len;
    }
}

static void compat_socket_scatter_iov(struct iovec *iov, int iovlen, const void *src, size_t total) {
    const uint8_t *cursor = (const uint8_t *)src;
    size_t remaining = total;
    int i;

    for (i = 0; i < iovlen && remaining != 0u; ++i) {
        size_t chunk = iov[i].iov_len;

        if (chunk == 0u) {
            continue;
        }
        if (chunk > remaining) {
            chunk = remaining;
        }
        memcpy(iov[i].iov_base, cursor, chunk);
        cursor += chunk;
        remaining -= chunk;
    }
}

int socket(int domain, int type, int protocol) {
    int fd;
    int handle;
    int base_type = type & ~(SOCK_NONBLOCK | SOCK_CLOEXEC);

    if (domain != AF_INET && domain != AF_INET6 && domain != AF_UNIX) {
        errno = EAFNOSUPPORT;
        return -1;
    }
    if (base_type != SOCK_STREAM && base_type != SOCK_DGRAM && base_type != SOCK_RAW) {
        errno = ESOCKTNOSUPPORT;
        return -1;
    }

    compat_fd_bootstrap();
    fd = compat_fd_allocate(3);
    if (fd < 0) {
        errno = EMFILE;
        return -1;
    }

    handle = compat_syscall5(SYSCALL_NETWORK_SOCKET, domain, base_type, protocol, 0, 0);
    if (handle < 0) {
        errno = ENOSYS;
        return -1;
    }

    if (compat_fd_bind_socket(fd, handle, domain, type, protocol) != 0) {
        (void)compat_syscall5(SYSCALL_NETWORK_CLOSE, handle, 0, 0, 0, 0);
        errno = EMFILE;
        return -1;
    }
    if ((type & SOCK_CLOEXEC) != 0) {
        struct compat_fd_entry *entry = compat_fd_get(fd);
        if (entry != 0) {
            entry->fd_flags |= FD_CLOEXEC;
        }
    }
    if ((type & SOCK_NONBLOCK) != 0) {
        struct compat_fd_entry *entry = compat_fd_get(fd);
        if (entry != 0) {
            entry->flags |= O_NONBLOCK;
        }
    }
    return fd;
}

int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    struct compat_fd_entry *entry;
    int rc;

    if (addr == 0 || addrlen == 0u) {
        errno = EINVAL;
        return -1;
    }
    if (compat_socket_validate_fd(sockfd, &entry) != 0) {
        return -1;
    }

    rc = compat_syscall5(SYSCALL_NETWORK_BIND,
                         entry->host_fd,
                         (int)(uintptr_t)addr,
                         (int)addrlen,
                         0,
                         0);
    if (rc != 0) {
        entry->socket_error = EINVAL;
        errno = EINVAL;
        return -1;
    }
    entry->socket_state_flags |= COMPAT_SOCKET_STATE_BOUND;
    entry->socket_error = 0;
    return 0;
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    struct compat_fd_entry *entry;
    int rc;

    if (addr == 0 || addrlen == 0u) {
        errno = EINVAL;
        return -1;
    }
    if (compat_socket_validate_fd(sockfd, &entry) != 0) {
        return -1;
    }

    rc = compat_syscall5(SYSCALL_NETWORK_CONNECT,
                         entry->host_fd,
                         (int)(uintptr_t)addr,
                         (int)addrlen,
                         0,
                         0);
    if (rc != 0) {
        entry->socket_error = ECONNREFUSED;
        errno = ECONNREFUSED;
        return -1;
    }
    entry->socket_state_flags |= COMPAT_SOCKET_STATE_CONNECTED;
    entry->socket_state_flags &= ~COMPAT_SOCKET_STATE_LISTENING;
    entry->socket_error = 0;
    return 0;
}

int listen(int sockfd, int backlog) {
    struct compat_fd_entry *entry;
    int rc;

    if (backlog < 0) {
        errno = EINVAL;
        return -1;
    }
    if (compat_socket_validate_fd(sockfd, &entry) != 0) {
        return -1;
    }

    rc = compat_syscall5(SYSCALL_NETWORK_LISTEN, entry->host_fd, backlog, 0, 0, 0);
    if (rc != 0) {
        entry->socket_error = EOPNOTSUPP;
        errno = EOPNOTSUPP;
        return -1;
    }
    entry->socket_state_flags |= COMPAT_SOCKET_STATE_LISTENING | COMPAT_SOCKET_STATE_BOUND;
    entry->socket_error = 0;
    return 0;
}

int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    struct compat_fd_entry *entry;
    int newfd;
    int handle;

    (void)addr;
    if (addrlen != 0) {
        *addrlen = 0;
    }
    if (compat_socket_validate_fd(sockfd, &entry) != 0) {
        return -1;
    }

    for (;;) {
        handle = compat_syscall5(SYSCALL_NETWORK_ACCEPT, entry->host_fd, 0, 0, 0, 0);
        if (handle >= 0) {
            break;
        }
        compat_fd_socket_update_ready_by_host(entry->host_fd, 0, COMPAT_SOCKET_READY_ACCEPT);
        entry->socket_error = EAGAIN;
        if (compat_socket_is_nonblocking(entry, 0)) {
            errno = EAGAIN;
            return -1;
        }
        if (compat_socket_wait_ready(sockfd, POLLIN) < 0) {
            errno = EINTR;
            return -1;
        }
    }

    newfd = compat_fd_allocate(3);
    if (newfd < 0) {
        (void)compat_syscall5(SYSCALL_NETWORK_CLOSE, handle, 0, 0, 0, 0);
        errno = EMFILE;
        return -1;
    }
    if (compat_fd_bind_socket(newfd, handle, entry->socket_domain, entry->socket_type, entry->socket_protocol) != 0) {
        (void)compat_syscall5(SYSCALL_NETWORK_CLOSE, handle, 0, 0, 0, 0);
        errno = EMFILE;
        return -1;
    }
    {
        struct compat_fd_entry *accepted = compat_fd_get(newfd);
        if (accepted != 0) {
            accepted->socket_state_flags = COMPAT_SOCKET_STATE_CONNECTED;
            accepted->socket_options = entry->socket_options;
            accepted->socket_sndbuf = entry->socket_sndbuf;
            accepted->socket_rcvbuf = entry->socket_rcvbuf;
            accepted->socket_ready_flags |= COMPAT_SOCKET_READY_SEND;
        }
    }
    return newfd;
}

ssize_t send(int sockfd, const void *buf, size_t len, int flags) {
    struct compat_fd_entry *entry;
    int rc;

    (void)flags;
    if (buf == 0 && len != 0u) {
        errno = EINVAL;
        return -1;
    }
    if (len == 0u) {
        return 0;
    }
    if (compat_socket_validate_fd(sockfd, &entry) != 0) {
        return -1;
    }

    rc = compat_syscall5(SYSCALL_NETWORK_SEND,
                         entry->host_fd,
                         (int)(uintptr_t)buf,
                         (int)len,
                         0,
                         0);
    if (rc < 0) {
        entry->socket_error = EPIPE;
        errno = EPIPE;
        return -1;
    }
    compat_fd_socket_update_ready_by_host(entry->host_fd, COMPAT_SOCKET_READY_SEND, 0);
    entry->socket_error = 0;
    return (ssize_t)rc;
}

ssize_t recv(int sockfd, void *buf, size_t len, int flags) {
    struct compat_fd_entry *entry;
    int rc;

    (void)flags;
    if (buf == 0 && len != 0u) {
        errno = EINVAL;
        return -1;
    }
    if (len == 0u) {
        return 0;
    }
    if (compat_socket_validate_fd(sockfd, &entry) != 0) {
        return -1;
    }

    for (;;) {
        rc = compat_syscall5(SYSCALL_NETWORK_RECV,
                             entry->host_fd,
                             (int)(uintptr_t)buf,
                             (int)len,
                             0,
                             0);
        if (rc > 0) {
            break;
        }
        compat_fd_socket_update_ready_by_host(entry->host_fd, 0, COMPAT_SOCKET_READY_RECV);
        entry->socket_error = EAGAIN;
        if (compat_socket_is_nonblocking(entry, flags)) {
            errno = EAGAIN;
            return -1;
        }
        if (compat_socket_wait_ready(sockfd, POLLIN) < 0) {
            errno = EINTR;
            return -1;
        }
    }
    entry->socket_error = 0;
    return (ssize_t)rc;
}

ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, socklen_t addrlen) {
    struct compat_fd_entry *entry;
    int rc;

    (void)flags;
    if (buf == 0 && len != 0u) {
        errno = EINVAL;
        return -1;
    }
    if (len == 0u) {
        return 0;
    }
    if (compat_socket_validate_fd(sockfd, &entry) != 0) {
        return -1;
    }

    rc = compat_syscall5(SYSCALL_NETWORK_SENDTO,
                         entry->host_fd,
                         (int)(uintptr_t)buf,
                         (int)len,
                         (int)(uintptr_t)dest_addr,
                         (int)addrlen);
    if (rc < 0) {
        entry->socket_error = EPIPE;
        errno = EPIPE;
        return -1;
    }
    compat_fd_socket_update_ready_by_host(entry->host_fd, COMPAT_SOCKET_READY_SEND, 0);
    entry->socket_error = 0;
    return (ssize_t)rc;
}

ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
                 struct sockaddr *src_addr, socklen_t *addrlen) {
    struct compat_fd_entry *entry;
    int rc;

    (void)flags;
    if (buf == 0 && len != 0u) {
        errno = EINVAL;
        return -1;
    }
    if (len == 0u) {
        return 0;
    }
    if (compat_socket_validate_fd(sockfd, &entry) != 0) {
        return -1;
    }

    for (;;) {
        rc = compat_syscall5(SYSCALL_NETWORK_RECVFROM,
                             entry->host_fd,
                             (int)(uintptr_t)buf,
                             (int)len,
                             (int)(uintptr_t)src_addr,
                             (int)(uintptr_t)addrlen);
        if (rc > 0) {
            break;
        }
        compat_fd_socket_update_ready_by_host(entry->host_fd, 0, COMPAT_SOCKET_READY_RECV);
        entry->socket_error = EAGAIN;
        if (compat_socket_is_nonblocking(entry, flags)) {
            errno = EAGAIN;
            return -1;
        }
        if (compat_socket_wait_ready(sockfd, POLLIN) < 0) {
            errno = EINTR;
            return -1;
        }
    }
    compat_fd_socket_update_ready_by_host(entry->host_fd, 0, COMPAT_SOCKET_READY_RECV);
    entry->socket_error = 0;
    return (ssize_t)rc;
}

ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags) {
    struct compat_fd_entry *entry;
    void *buffer = 0;
    size_t total = 0u;
    ssize_t sent;

    if (compat_socket_validate_fd(sockfd, &entry) != 0) {
        return -1;
    }
    if (compat_socket_validate_msghdr(msg, 0) != 0) {
        return -1;
    }
    if (compat_socket_iov_length(msg->msg_iov, msg->msg_iovlen, &total) != 0) {
        return -1;
    }
    if (total == 0u) {
        return 0;
    }

    buffer = malloc(total);
    if (buffer == 0) {
        errno = ENOMEM;
        return -1;
    }
    compat_socket_gather_iov(buffer, msg->msg_iov, msg->msg_iovlen);
    sent = send(sockfd, buffer, total, flags);
    free(buffer);
    return sent;
}

ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags) {
    struct compat_fd_entry *entry;
    void *buffer = 0;
    size_t total = 0u;
    ssize_t received;

    if (msg == 0) {
        errno = EINVAL;
        return -1;
    }
    if (compat_socket_validate_fd(sockfd, &entry) != 0) {
        return -1;
    }
    if (compat_socket_validate_msghdr(msg, 1) != 0) {
        return -1;
    }
    if (compat_socket_iov_length(msg->msg_iov, msg->msg_iovlen, &total) != 0) {
        return -1;
    }

    msg->msg_flags = 0;
    if (msg->msg_name != 0 && msg->msg_namelen != 0u) {
        msg->msg_namelen = 0u;
    }
    if (total == 0u) {
        return 0;
    }

    buffer = malloc(total);
    if (buffer == 0) {
        errno = ENOMEM;
        return -1;
    }
    received = recv(sockfd, buffer, total, flags);
    if (received >= 0) {
        compat_socket_scatter_iov(msg->msg_iov, msg->msg_iovlen, buffer, (size_t)received);
    }
    free(buffer);
    return received;
}

int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen) {
    struct compat_fd_entry *entry;
    int value = 0;

    if (compat_socket_validate_fd(sockfd, &entry) != 0) {
        return -1;
    }
    if (optval == 0 || optlen < sizeof(int)) {
        errno = EINVAL;
        return -1;
    }

    value = *(const int *)optval;

    if (level == SOL_SOCKET) {
        switch (optname) {
        case SO_REUSEADDR:
        case SO_KEEPALIVE:
        case SO_BROADCAST:
        case SO_OOBINLINE:
            if (value != 0) {
                entry->socket_options |= optname;
            } else {
                entry->socket_options &= ~optname;
            }
            entry->socket_error = 0;
            return 0;
        case SO_SNDBUF:
            if (value <= 0) {
                errno = EINVAL;
                return -1;
            }
            entry->socket_sndbuf = value;
            entry->socket_error = 0;
            return 0;
        case SO_RCVBUF:
            if (value <= 0) {
                errno = EINVAL;
                return -1;
            }
            entry->socket_rcvbuf = value;
            entry->socket_error = 0;
            return 0;
        case SO_LINGER:
            if (optlen < (socklen_t)sizeof(struct linger)) {
                errno = EINVAL;
                return -1;
            }
            {
                const struct linger *l = (const struct linger *)optval;
                if (l->l_onoff) {
                    entry->socket_options |= SO_LINGER;
                } else {
                    entry->socket_options &= ~SO_LINGER;
                }
            }
            entry->socket_error = 0;
            return 0;
        case SO_SNDTIMEO:
        case SO_RCVTIMEO:
            entry->socket_error = 0;
            return 0;
        case SO_DEBUG:
        case SO_DONTROUTE:
        case SO_REUSEPORT:
            entry->socket_error = 0;
            return 0;
        default:
            errno = ENOPROTOOPT;
            return -1;
        }
    }

    if (level == IPPROTO_TCP) {
        switch (optname) {
        case TCP_NODELAY:
        case TCP_KEEPIDLE:
        case TCP_KEEPINTVL:
        case TCP_KEEPCNT:
            entry->socket_error = 0;
            return 0;
        default:
            errno = ENOPROTOOPT;
            return -1;
        }
    }

    if (level == IPPROTO_IP) {
        switch (optname) {
        case IP_TTL:
        case IP_MULTICAST_TTL:
        case IP_MULTICAST_LOOP:
            entry->socket_error = 0;
            return 0;
        default:
            errno = ENOPROTOOPT;
            return -1;
        }
    }

    errno = ENOPROTOOPT;
    return -1;
}

int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen) {
    struct compat_fd_entry *entry;
    int value = 0;

    if (compat_socket_validate_fd(sockfd, &entry) != 0) {
        return -1;
    }
    if (optval == 0 || optlen == 0 || *optlen < sizeof(int)) {
        errno = EINVAL;
        return -1;
    }

    if (level == SOL_SOCKET) {
        switch (optname) {
        case SO_TYPE:
            value = entry->socket_type & ~(SOCK_NONBLOCK | SOCK_CLOEXEC);
            break;
        case SO_DOMAIN:
            value = entry->socket_domain;
            break;
        case SO_PROTOCOL:
            value = entry->socket_protocol;
            break;
        case SO_ERROR:
            value = entry->socket_error;
            entry->socket_error = 0;
            break;
        case SO_ACCEPTCONN:
            value = (entry->socket_state_flags & COMPAT_SOCKET_STATE_LISTENING) != 0 ? 1 : 0;
            break;
        case SO_REUSEADDR:
        case SO_KEEPALIVE:
        case SO_BROADCAST:
        case SO_OOBINLINE:
        case SO_LINGER:
            value = (entry->socket_options & optname) != 0 ? 1 : 0;
            break;
        case SO_SNDBUF:
            value = entry->socket_sndbuf;
            break;
        case SO_RCVBUF:
            value = entry->socket_rcvbuf;
            break;
        case SO_SNDTIMEO:
        case SO_RCVTIMEO:
            value = 0;
            break;
        case SO_DEBUG:
        case SO_DONTROUTE:
        case SO_REUSEPORT:
            value = 0;
            break;
        default:
            errno = ENOPROTOOPT;
            return -1;
        }
        *(int *)optval = value;
        *optlen = (socklen_t)sizeof(int);
        return 0;
    }

    if (level == IPPROTO_TCP) {
        switch (optname) {
        case TCP_NODELAY:
        case TCP_KEEPIDLE:
        case TCP_KEEPINTVL:
        case TCP_KEEPCNT:
            *(int *)optval = 0;
            *optlen = (socklen_t)sizeof(int);
            return 0;
        default:
            errno = ENOPROTOOPT;
            return -1;
        }
    }

    if (level == IPPROTO_IP) {
        switch (optname) {
        case IP_TTL:
            *(int *)optval = 64;
            *optlen = (socklen_t)sizeof(int);
            return 0;
        case IP_MULTICAST_TTL:
            *(int *)optval = 1;
            *optlen = (socklen_t)sizeof(int);
            return 0;
        case IP_MULTICAST_LOOP:
            *(int *)optval = 1;
            *optlen = (socklen_t)sizeof(int);
            return 0;
        default:
            errno = ENOPROTOOPT;
            return -1;
        }
    }

    errno = ENOPROTOOPT;
    return -1;
}

int shutdown(int sockfd, int how) {
    struct compat_fd_entry *entry;

    if (how != SHUT_RD && how != SHUT_WR && how != SHUT_RDWR) {
        errno = EINVAL;
        return -1;
    }
    if (compat_socket_validate_fd(sockfd, &entry) != 0) {
        return -1;
    }

    entry->socket_state_flags &= ~COMPAT_SOCKET_STATE_CONNECTED;
    entry->socket_error = 0;
    return 0;
}

int sockatmark(int sockfd) {
    struct compat_fd_entry *entry;

    if (compat_socket_validate_fd(sockfd, &entry) != 0) {
        return -1;
    }
    errno = EINVAL;
    return -1;
}
