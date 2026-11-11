#ifndef _DEV_USB_USBPCAP_H_
#define _DEV_USB_USBPCAP_H_

#define DLT_USBPCAP 0

struct usbpcap_pkt_hdr {
    uint16_t uph_hlen;
    uint16_t uph_dlen;
};
struct usbpcap_ctl_hdr {
    struct usbpcap_pkt_hdr uch_hdr;
    uint8_t uch_stage;
};
struct usbpcap_iso_hdr_full {
    struct usbpcap_pkt_hdr uih_hdr;
    uint32_t uih_startframe;
    uint32_t uih_nframes;
    uint32_t uih_errors;
    struct usbpcap_iso_pkt {
        uint32_t uip_offset;
        uint32_t uip_length;
        uint32_t uip_status;
    } uih_frames[1];
};

#define USBPCAP_TRANSFER_CONTROL 0
#define USBPCAP_TRANSFER_ISOCHRONOUS 1
#define USBPCAP_TRANSFER_BULK 2
#define USBPCAP_TRANSFER_INTERRUPT 3

#define USBPCAP_CONTROL_STAGE_SETUP 0
#define USBPCAP_CONTROL_STAGE_DATA 1
#define USBPCAP_CONTROL_STAGE_STATUS 2

#define USBPCAP_INFO_DIRECTION_IN 0x01

#endif
