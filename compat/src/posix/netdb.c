#include <compat/posix/errno.h>
#include <compat/libc/string.h>
#include <compat/libc/stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <poll.h>
#include <include/userland_api.h>

static const char *gai_err_table[16] = {
    "Success",
    "Invalid value for ai_flags",
    "Name or service not known",
    "Temporary failure in name resolution",
    "Non-recoverable failure in name resolution",
    "No address associated with name",
    "ai_family not supported",
    "ai_socktype not supported",
    "Service not supported for ai_socktype",
    "Address family for name not supported",
    "Memory allocation failure",
    "System error",
    "Invalid value for hints",
    "Resolved protocol is unknown",
    "Argument buffer overflow"
};

static int compat_parse_hosts(const char *name, struct in_addr *addr) {
    FILE *fp;
    char line[256];

    fp = fopen("/etc/hosts", "r");
    if (fp == 0) {
        return -1;
    }
    while (fgets(line, sizeof(line), fp) != 0) {
        char ip_str[64];
        char host[64];
        char *p = line;
        int i = 0;

        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '#' || *p == '\n' || *p == '\0') continue;
        while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\n' && i < (int)sizeof(ip_str) - 1) {
            ip_str[i++] = *p++;
        }
        ip_str[i] = '\0';
        while (*p == ' ' || *p == '\t') ++p;
        i = 0;
        while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\n' && i < (int)sizeof(host) - 1) {
            host[i++] = *p++;
        }
        host[i] = '\0';
        if (strcmp(host, name) == 0) {
            unsigned int a0, a1, a2, a3;
            if (sscanf(ip_str, "%u.%u.%u.%u", &a0, &a1, &a2, &a3) == 4) {
                addr->s_addr = htonl((a0 << 24) | (a1 << 16) | (a2 << 8) | a3);
                fclose(fp);
                return 0;
            }
        }
    }
    fclose(fp);
    return -1;
}

static int compat_dns_query(const char *name, struct in_addr *addr) {
    int fd;
    struct sockaddr_in dns_addr;
    uint8_t dns_pkt[512];
    uint8_t reply[512];
    int reply_len;
    int i;
    int name_len = (int)strlen(name);

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;

    memset(&dns_addr, 0, sizeof(dns_addr));
    dns_addr.sin_family = AF_INET;
    dns_addr.sin_port = htons(53);
    dns_addr.sin_addr.s_addr = htonl(0x0A000203u);

    memset(dns_pkt, 0, sizeof(dns_pkt));
    dns_pkt[0] = 0xaa;
    dns_pkt[1] = 0xaa;
    dns_pkt[2] = 0x01;
    dns_pkt[3] = 0x00;
    dns_pkt[4] = 0x00;
    dns_pkt[5] = 0x01;
    dns_pkt[6] = 0x00;
    dns_pkt[7] = 0x00;
    dns_pkt[8] = 0x00;
    dns_pkt[9] = 0x00;
    dns_pkt[10] = 0x00;
    dns_pkt[11] = 0x00;

    {
        int pos = 12;
        const char *p = name;
        const char *dot;

        while (*p != '\0') {
            dot = p;
            while (*dot != '\0' && *dot != '.') ++dot;
            dns_pkt[pos++] = (uint8_t)(dot - p);
            while (p < dot) {
                dns_pkt[pos++] = (uint8_t)(*p++);
            }
            if (*dot == '.') {
                p = dot + 1;
            }
        }
        dns_pkt[pos++] = 0x00;
        dns_pkt[pos++] = 0x00;
        dns_pkt[pos++] = 0x01;
        dns_pkt[pos++] = 0x00;
        dns_pkt[pos++] = 0x01;
    }

    if (sendto(fd, dns_pkt, (size_t)(12 + name_len + 2 + 4), 0,
               (const struct sockaddr *)&dns_addr, sizeof(dns_addr)) < 0) {
        close(fd);
        return -1;
    }

    {
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        if (poll(&pfd, 1, 2000) <= 0) {
            close(fd);
            return -1;
        }
    }

    reply_len = recvfrom(fd, reply, sizeof(reply), 0, 0, 0);
    close(fd);
    if (reply_len < 12) return -1;
    if ((reply[2] & 0x0f) != 0) return -1;
    {
        int ancount = (reply[6] << 8) | reply[7];
        int pos = 12;

        while (pos < reply_len && reply[pos] != 0) {
            if ((reply[pos] & 0xc0) == 0xc0) {
                pos += 2;
            } else {
                pos += reply[pos] + 1;
            }
        }
        if (pos >= reply_len) return -1;
        pos += 1;
        pos += 4;
        if (pos + 4 > reply_len) return -1;
        for (i = 0; i < ancount && pos + 12 <= reply_len; ++i) {
            if (pos + 10 > reply_len) return -1;
            {
                uint16_t type = (uint16_t)((reply[pos + 1] << 8) | reply[pos + 2]);
                uint16_t rdlength = (uint16_t)((reply[pos + 9] << 8) | reply[pos + 10]);

                if (type == 1 && rdlength >= 4 && pos + 12 + 4 <= reply_len) {
                    uint32_t ip;
                    memcpy(&ip, reply + pos + 12, 4);
                    addr->s_addr = ip;
                    return 0;
                }
                pos += 12 + rdlength;
            }
        }
    }
    return -1;
}

int getaddrinfo(const char *node, const char *service,
                const struct addrinfo *hints,
                struct addrinfo **res) {
    struct addrinfo *ai;
    struct sockaddr_in *sin;
    struct in_addr addr;
    int family = AF_INET;
    int socktype = SOCK_STREAM;
    int protocol = 0;
    int flags = 0;
    uint16_t port = 0;

    if (res == 0) {
        return EAI_FAIL;
    }
    *res = 0;

    if (hints != 0) {
        flags = hints->ai_flags;
        if (hints->ai_family != AF_UNSPEC && hints->ai_family != AF_INET) {
            return EAI_FAMILY;
        }
        family = hints->ai_family != 0 ? hints->ai_family : AF_INET;
        if (hints->ai_socktype != 0) {
            socktype = hints->ai_socktype;
            if (socktype != SOCK_STREAM && socktype != SOCK_DGRAM) {
                return EAI_SOCKTYPE;
            }
        }
        if (hints->ai_protocol != 0) {
            protocol = hints->ai_protocol;
        }
    }

    if (service != 0) {
        int isnum = 1;
        const char *p = service;
        while (*p != '\0') {
            if (*p < '0' || *p > '9') { isnum = 0; break; }
            ++p;
        }
        if (isnum) {
            long v = atol(service);
            if (v <= 0 || v > 65535) return EAI_SERVICE;
            port = htons((uint16_t)v);
        } else {
            if (strcmp(service, "http") == 0) port = htons(80);
            else if (strcmp(service, "https") == 0) port = htons(443);
            else if (strcmp(service, "ssh") == 0) port = htons(22);
            else if (strcmp(service, "ftp") == 0) port = htons(21);
            else if (strcmp(service, "smtp") == 0) port = htons(25);
            else if (strcmp(service, "dns") == 0) port = htons(53);
            else if (strcmp(service, "dhcp") == 0) port = htons(67);
            else if (strcmp(service, "echo") == 0) port = htons(7);
            else if (strcmp(service, "discard") == 0) port = htons(9);
            else if (strcmp(service, "daytime") == 0) port = htons(13);
            else if (strcmp(service, "telnet") == 0) port = htons(23);
            else return EAI_SERVICE;
        }
    }

    if (node != 0) {
        unsigned int a, b, c, d;
        if (sscanf(node, "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
            addr.s_addr = htonl((a << 24) | (b << 16) | (c << 8) | d);
        } else if ((flags & AI_NUMERICHOST) != 0) {
            return EAI_NONAME;
        } else {
            if (compat_parse_hosts(node, &addr) != 0) {
                if (compat_dns_query(node, &addr) != 0) {
                    return EAI_NONAME;
                }
            }
        }
    } else {
        addr.s_addr = (flags & AI_PASSIVE) ? htonl(INADDR_ANY) : htonl(INADDR_LOOPBACK);
    }

    ai = (struct addrinfo *)malloc(sizeof(struct addrinfo));
    if (ai == 0) return EAI_MEMORY;
    sin = (struct sockaddr_in *)malloc(sizeof(struct sockaddr_in));
    if (sin == 0) {
        free(ai);
        return EAI_MEMORY;
    }
    memset(ai, 0, sizeof(*ai));
    memset(sin, 0, sizeof(*sin));
    sin->sin_family = AF_INET;
    sin->sin_port = port;
    sin->sin_addr = addr;
    sin->sin_len = sizeof(struct sockaddr_in);

    ai->ai_flags = flags;
    ai->ai_family = AF_INET;
    ai->ai_socktype = socktype;
    ai->ai_protocol = protocol;
    ai->ai_addrlen = sizeof(struct sockaddr_in);
    ai->ai_addr = (struct sockaddr *)sin;
    ai->ai_canonname = 0;
    ai->ai_next = 0;
    if ((flags & AI_CANONNAME) != 0 && node != 0) {
        ai->ai_canonname = (char *)malloc(strlen(node) + 1);
        if (ai->ai_canonname != 0) {
            strcpy(ai->ai_canonname, node);
        }
    }
    *res = ai;
    return 0;
}

void freeaddrinfo(struct addrinfo *ai) {
    if (ai == 0) return;
    if (ai->ai_addr != 0) free(ai->ai_addr);
    if (ai->ai_canonname != 0) free(ai->ai_canonname);
    free(ai);
}

int getnameinfo(const struct sockaddr *sa, socklen_t salen,
                char *host, size_t hostlen,
                char *serv, size_t servlen,
                int flags) {
    const struct sockaddr_in *sin;
    uint32_t ip;
    unsigned int octets[4];

    if (sa == 0 || salen < (socklen_t)sizeof(struct sockaddr_in)) {
        return EAI_FAIL;
    }
    if (sa->sa_family != AF_INET) {
        return EAI_FAMILY;
    }
    sin = (const struct sockaddr_in *)sa;
    ip = ntohl(sin->sin_addr.s_addr);
    octets[0] = (ip >> 24) & 0xff;
    octets[1] = (ip >> 16) & 0xff;
    octets[2] = (ip >> 8) & 0xff;
    octets[3] = ip & 0xff;

    if (host != 0 && hostlen > 0) {
        if ((flags & NI_NUMERICHOST) != 0) {
            snprintf(host, hostlen, "%u.%u.%u.%u", octets[0], octets[1], octets[2], octets[3]);
        } else {
            snprintf(host, hostlen, "%u.%u.%u.%u", octets[0], octets[1], octets[2], octets[3]);
        }
    }
    if (serv != 0 && servlen > 0) {
        uint16_t port = ntohs(sin->sin_port);
        if ((flags & NI_NUMERICSERV) != 0 || port == 0) {
            snprintf(serv, servlen, "%u", (unsigned int)port);
        } else {
            const char *sname = 0;
            if (port == 80) sname = "http";
            else if (port == 443) sname = "https";
            else if (port == 22) sname = "ssh";
            else if (port == 21) sname = "ftp";
            else if (port == 53) sname = "dns";
            else if (port == 23) sname = "telnet";
            else if (port == 25) sname = "smtp";
            else if (port == 7) sname = "echo";
            if (sname != 0) {
                snprintf(serv, servlen, "%s", sname);
            } else {
                snprintf(serv, servlen, "%u", (unsigned int)port);
            }
        }
    }
    return 0;
}

const char *gai_strerror(int ecode) {
    int idx = -ecode;

    if (idx < 0 || idx > EAI_OVERFLOW) {
        return "Unknown error";
    }
    if (gai_err_table[idx] == 0) {
        return "Unknown error";
    }
    return gai_err_table[idx];
}
