#ifndef _NET_IF_H_
#define _NET_IF_H_

#include <sys/socket.h>
#include <sys/types.h>

#define IFNAMSIZ 16
#define IF_NAMESIZE IFNAMSIZ
#define IFDESCRSIZE 64

struct if_nameindex {
    unsigned int if_index;
    char *if_name;
};

/* Value-member layout matching headers/net/if.h, the canonical published
 * shape used by the kernel-side SIOCGIF and ioctl handlers. */
struct ifreq {
    char ifr_name[IFNAMSIZ];
    union {
        struct sockaddr ifru_addr;
        struct sockaddr ifru_dstaddr;
        struct sockaddr ifru_broadaddr;
        struct sockaddr ifru_netmask;
        short ifru_flags;
        int ifru_metric;
        char *ifru_data;
    } ifr_ifru;
};

#define ifr_addr       ifr_ifru.ifru_addr
#define ifr_dstaddr    ifr_ifru.ifru_dstaddr
#define ifr_broadaddr  ifr_ifru.ifru_broadaddr
#define ifr_netmask    ifr_ifru.ifru_netmask
#define ifr_flags      ifr_ifru.ifru_flags
#define ifr_metric     ifr_ifru.ifru_metric
#define ifr_data       ifr_ifru.ifru_data

struct ifconf {
    int ifc_len;
    union {
        char *ifcu_buf;
        struct ifreq *ifcu_req;
    } ifc_ifcu;
};

#define ifc_buf ifc_ifcu.ifcu_buf
#define ifc_req ifc_ifcu.ifcu_req

#define IFF_UP 0x1
#define IFF_BROADCAST 0x2
#define IFF_DEBUG 0x4
#define IFF_LOOPBACK 0x8
#define IFF_POINTOPOINT 0x10
#define IFF_RUNNING 0x40
#define IFF_NOARP 0x80
#define IFF_PROMISC 0x100
#define IFF_ALLMULTI 0x200
#define IFF_OACTIVE 0x400
#define IFF_SIMPLEX 0x800
#define IFF_LINK0 0x1000
#define IFF_LINK1 0x2000
#define IFF_LINK2 0x4000
#define IFF_MULTICAST 0x8000

unsigned int if_nametoindex(const char *name);
char *if_indextoname(unsigned int ifindex, char *name);
struct if_nameindex *if_nameindex(void);
void if_freenameindex(struct if_nameindex *ptr);

#endif
