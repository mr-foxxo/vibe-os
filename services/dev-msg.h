#ifndef DEV_MSG_H
#define DEV_MSG_H

#include <stdint.h>

enum dev_cmd {
    DEV_CMD_PROBE      = 0,
    DEV_CMD_STATUS     = 1,
    DEV_CMD_IOCTL      = 2,
    DEV_CMD_LIST       = 3,
    DEV_CMD_HOTPLUG    = 4,
};

#define DEV_NAME_MAX 32
#define DEV_TYPE_MAX 16
#define DEV_LIST_MAX 16

struct dev_msg_probe {
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
};

struct dev_msg_ioctl {
    char name[DEV_NAME_MAX];
    int cmd;
    uint32_t arg_buf[8];
};

struct dev_entry {
    char name[DEV_NAME_MAX];
    char type[DEV_TYPE_MAX];
    uint32_t present;
    uint32_t active;
    uint32_t error;
    uint32_t link_up;
    uint32_t mtu;
    uint8_t mac[6];
};

typedef struct {
    enum dev_cmd cmd;
    union {
        struct dev_msg_probe probe;
        struct dev_msg_ioctl ioctl;
        struct dev_entry entries[DEV_LIST_MAX];
        int32_t result;
    };
} dev_msg_t;

#endif
