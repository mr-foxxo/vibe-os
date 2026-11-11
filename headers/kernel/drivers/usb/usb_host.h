#ifndef KERNEL_DRIVERS_USB_HOST_H
#define KERNEL_DRIVERS_USB_HOST_H

#include <stdint.h>

enum kernel_usb_host_kind {
    KERNEL_USB_HOST_KIND_UNKNOWN = 0,
    KERNEL_USB_HOST_KIND_UHCI = 1,
    KERNEL_USB_HOST_KIND_OHCI = 2,
    KERNEL_USB_HOST_KIND_EHCI = 3,
    KERNEL_USB_HOST_KIND_XHCI = 4
};

struct kernel_usb_host_controller_info {
    uint8_t kind;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t irq_line;
    uint8_t prog_if;
    uint8_t bar_index;
    uint8_t bar_is_mmio;
    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t bar_base;
    uint32_t bar_size;
    uint32_t port_count;
};

struct kernel_usb_host_port_status {
    uint32_t raw;
    uint8_t connected;
    uint8_t enabled;
    uint8_t powered;
    uint8_t owner;
    uint8_t reset;
    uint8_t speed;
};

enum kernel_usb_host_port_speed {
    KERNEL_USB_HOST_PORT_SPEED_UNKNOWN = 0,
    KERNEL_USB_HOST_PORT_SPEED_LOW = 1,
    KERNEL_USB_HOST_PORT_SPEED_FULL = 2,
    KERNEL_USB_HOST_PORT_SPEED_HIGH = 3,
    KERNEL_USB_HOST_PORT_SPEED_SUPER = 4
};

enum kernel_usb_host_root_device_flags {
    KERNEL_USB_HOST_ROOT_DEVICE_FLAG_CONNECTED = 0x01u,
    KERNEL_USB_HOST_ROOT_DEVICE_FLAG_ENABLED = 0x02u,
    KERNEL_USB_HOST_ROOT_DEVICE_FLAG_POWERED = 0x04u,
    KERNEL_USB_HOST_ROOT_DEVICE_FLAG_OWNER = 0x08u
};

enum kernel_usb_device_flags {
    KERNEL_USB_DEVICE_FLAG_CONNECTED = 0x01u,
    KERNEL_USB_DEVICE_FLAG_ENABLED = 0x02u,
    KERNEL_USB_DEVICE_FLAG_POWERED = 0x04u,
    KERNEL_USB_DEVICE_FLAG_AUDIO_CANDIDATE = 0x08u,
    KERNEL_USB_DEVICE_FLAG_ENUM_READY = 0x10u,
    KERNEL_USB_DEVICE_FLAG_CONTROL_READY = 0x20u,
    KERNEL_USB_DEVICE_FLAG_NEEDS_COMPANION = 0x40u,
    KERNEL_USB_DEVICE_FLAG_COMPANION_PRESENT = 0x80u,
    KERNEL_USB_DEVICE_FLAG_HANDOFF_READY = 0x100u,
    KERNEL_USB_DEVICE_FLAG_CONTROL_PATH_READY = 0x200u
};

enum kernel_usb_device_state {
    KERNEL_USB_DEVICE_STATE_NONE = 0,
    KERNEL_USB_DEVICE_STATE_ATTACHED = 1,
    KERNEL_USB_DEVICE_STATE_READY_FOR_ENUM = 2,
    KERNEL_USB_DEVICE_STATE_READY_FOR_CONTROL = 3,
    KERNEL_USB_DEVICE_STATE_NEEDS_COMPANION = 4,
    KERNEL_USB_DEVICE_STATE_READY_FOR_HANDOFF = 5
};

struct kernel_usb_host_root_device_info {
    uint8_t controller_index;
    uint8_t port_index;
    uint8_t controller_kind;
    uint8_t speed;
    uint8_t flags;
};

struct kernel_usb_device_info {
    uint8_t address;
    uint8_t controller_index;
    uint8_t companion_controller_index;
    uint8_t effective_controller_index;
    uint8_t port_index;
    uint8_t controller_kind;
    uint8_t companion_controller_kind;
    uint8_t effective_controller_kind;
    uint8_t speed;
    uint8_t state;
    uint16_t flags;
};

struct kernel_usb_probe_plan {
    struct kernel_usb_device_info device;
    uint8_t request_type;
    uint8_t request;
    uint16_t value;
    uint16_t index;
    uint16_t length;
};

enum kernel_usb_probe_status {
    KERNEL_USB_PROBE_STATUS_NONE = 0,
    KERNEL_USB_PROBE_STATUS_PLANNED = 1,
    KERNEL_USB_PROBE_STATUS_DISPATCH_READY = 2,
    KERNEL_USB_PROBE_STATUS_DEFERRED_NO_TRANSPORT = 3,
    KERNEL_USB_PROBE_STATUS_DISPATCH_SELECTED = 4,
    KERNEL_USB_PROBE_STATUS_EXEC_NO_TRANSPORT = 5,
    KERNEL_USB_PROBE_STATUS_EXEC_READY = 6,
    KERNEL_USB_PROBE_STATUS_DESCRIPTOR_READY = 7,
    KERNEL_USB_PROBE_STATUS_ADDRESS_READY = 8,
    KERNEL_USB_PROBE_STATUS_CONFIG_READY = 9,
    KERNEL_USB_PROBE_STATUS_CONFIGURED_READY = 10,
    KERNEL_USB_PROBE_STATUS_ATTACHED_READY = 11
};

struct kernel_usb_probe_snapshot {
    uint32_t device_index;
    uint8_t audio_candidate;
    uint8_t status;
    uint8_t actual_length;
    uint8_t descriptor_valid;
    uint8_t config_valid;
    uint8_t audio_class_detected;
    uint8_t mass_storage_detected;
    uint8_t hid_boot_keyboard_detected;
    uint8_t hid_boot_mouse_detected;
    uint8_t hid_interface_number;
    uint8_t hid_interface_protocol;
    uint8_t hid_endpoint_address;
    uint8_t hid_endpoint_attributes;
    uint8_t hid_poll_interval;
    uint8_t assigned_address;
    uint16_t hid_endpoint_max_packet;
    uint16_t max_packet_size0;
    uint16_t config_total_length;
    struct kernel_usb_probe_plan plan;
    uint8_t descriptor_prefix[18];
    uint8_t config_descriptor_prefix[9];
    uint8_t configuration_value;
    uint8_t interface_count;
    uint8_t endpoint_count;
    uint8_t mass_storage_interface_number;
    uint8_t mass_storage_subclass;
    uint8_t mass_storage_protocol;
    uint8_t mass_storage_bulk_in_endpoint_address;
    uint8_t mass_storage_bulk_out_endpoint_address;
    uint16_t mass_storage_bulk_in_max_packet;
    uint16_t mass_storage_bulk_out_max_packet;
    uint8_t audio_control_interface_number;
    uint8_t audio_streaming_interface_number;
    uint8_t audio_streaming_interface_count;
    uint8_t audio_streaming_alt_setting;
    uint8_t audio_playback_channel_count;
    uint8_t audio_playback_subframe_size;
    uint8_t audio_playback_bit_resolution;
    uint8_t audio_playback_endpoint_address;
    uint8_t audio_playback_endpoint_attributes;
    uint16_t audio_playback_endpoint_max_packet;
    uint32_t audio_playback_sample_rate;
};

struct kernel_usb_probe_dispatch_context {
    uint32_t snapshot_index;
    struct kernel_usb_probe_snapshot snapshot;
    struct kernel_usb_host_controller_info effective_controller;
    struct kernel_usb_host_port_status effective_port_status;
    uint8_t transport_kind;
    uint8_t transport_available;
};

enum kernel_usb_probe_execution_result {
    KERNEL_USB_PROBE_EXEC_RESULT_NONE = 0,
    KERNEL_USB_PROBE_EXEC_RESULT_NO_TRANSPORT = 1,
    KERNEL_USB_PROBE_EXEC_RESULT_UHCI_UNAVAILABLE = 2,
    KERNEL_USB_PROBE_EXEC_RESULT_OHCI_UNAVAILABLE = 3,
    KERNEL_USB_PROBE_EXEC_RESULT_EHCI_UNAVAILABLE = 4,
    KERNEL_USB_PROBE_EXEC_RESULT_XHCI_UNAVAILABLE = 5,
    KERNEL_USB_PROBE_EXEC_RESULT_UHCI_PREFLIGHT_READY = 6,
    KERNEL_USB_PROBE_EXEC_RESULT_OHCI_PREFLIGHT_READY = 7,
    KERNEL_USB_PROBE_EXEC_RESULT_PORT_NOT_READY = 8,
    KERNEL_USB_PROBE_EXEC_RESULT_UHCI_DESCRIPTOR_OK = 9,
    KERNEL_USB_PROBE_EXEC_RESULT_UHCI_DESCRIPTOR_TIMEOUT = 10,
    KERNEL_USB_PROBE_EXEC_RESULT_UHCI_DESCRIPTOR_ERROR = 11,
    KERNEL_USB_PROBE_EXEC_RESULT_OHCI_DESCRIPTOR_OK = 12,
    KERNEL_USB_PROBE_EXEC_RESULT_OHCI_DESCRIPTOR_TIMEOUT = 13,
    KERNEL_USB_PROBE_EXEC_RESULT_OHCI_DESCRIPTOR_ERROR = 14,
    KERNEL_USB_PROBE_EXEC_RESULT_UHCI_DEVICE_DESCRIPTOR_OK = 15,
    KERNEL_USB_PROBE_EXEC_RESULT_OHCI_DEVICE_DESCRIPTOR_OK = 16,
    KERNEL_USB_PROBE_EXEC_RESULT_UHCI_SET_ADDRESS_OK = 17,
    KERNEL_USB_PROBE_EXEC_RESULT_OHCI_SET_ADDRESS_OK = 18,
    KERNEL_USB_PROBE_EXEC_RESULT_UHCI_CONFIG_DESCRIPTOR_OK = 19,
    KERNEL_USB_PROBE_EXEC_RESULT_OHCI_CONFIG_DESCRIPTOR_OK = 20,
    KERNEL_USB_PROBE_EXEC_RESULT_UHCI_SET_CONFIGURATION_OK = 21,
    KERNEL_USB_PROBE_EXEC_RESULT_OHCI_SET_CONFIGURATION_OK = 22,
    KERNEL_USB_PROBE_EXEC_RESULT_UHCI_SET_INTERFACE_OK = 23,
    KERNEL_USB_PROBE_EXEC_RESULT_OHCI_SET_INTERFACE_OK = 24,
    KERNEL_USB_PROBE_EXEC_RESULT_EHCI_DESCRIPTOR_OK = 25,
    KERNEL_USB_PROBE_EXEC_RESULT_EHCI_DESCRIPTOR_TIMEOUT = 26,
    KERNEL_USB_PROBE_EXEC_RESULT_EHCI_DESCRIPTOR_ERROR = 27,
    KERNEL_USB_PROBE_EXEC_RESULT_EHCI_SET_CONFIGURATION_OK = 28
};

struct kernel_usb_probe_execution {
    struct kernel_usb_probe_dispatch_context dispatch;
    uint8_t result;
    uint8_t actual_length;
    uint8_t descriptor_valid;
    uint8_t config_valid;
    uint8_t audio_class_detected;
    uint8_t mass_storage_detected;
    uint8_t hid_boot_keyboard_detected;
    uint8_t hid_boot_mouse_detected;
    uint8_t hid_interface_number;
    uint8_t hid_interface_protocol;
    uint8_t hid_endpoint_address;
    uint8_t hid_endpoint_attributes;
    uint8_t hid_poll_interval;
    uint8_t assigned_address;
    uint16_t hid_endpoint_max_packet;
    uint16_t max_packet_size0;
    uint16_t config_total_length;
    uint8_t descriptor_prefix[18];
    uint8_t config_descriptor_prefix[9];
    uint8_t configuration_value;
    uint8_t interface_count;
    uint8_t endpoint_count;
    uint8_t mass_storage_interface_number;
    uint8_t mass_storage_subclass;
    uint8_t mass_storage_protocol;
    uint8_t mass_storage_bulk_in_endpoint_address;
    uint8_t mass_storage_bulk_out_endpoint_address;
    uint16_t mass_storage_bulk_in_max_packet;
    uint16_t mass_storage_bulk_out_max_packet;
    uint8_t audio_control_interface_number;
    uint8_t audio_streaming_interface_number;
    uint8_t audio_streaming_interface_count;
    uint8_t audio_streaming_alt_setting;
    uint8_t audio_playback_channel_count;
    uint8_t audio_playback_subframe_size;
    uint8_t audio_playback_bit_resolution;
    uint8_t audio_playback_endpoint_address;
    uint8_t audio_playback_endpoint_attributes;
    uint16_t audio_playback_endpoint_max_packet;
    uint32_t audio_playback_sample_rate;
    int status_code;
};

void kernel_usb_host_init(void);
uint32_t kernel_usb_host_controller_count(void);
uint32_t kernel_usb_host_uhci_count(void);
uint32_t kernel_usb_host_ohci_count(void);
uint32_t kernel_usb_host_ehci_count(void);
uint32_t kernel_usb_host_xhci_count(void);
uint32_t kernel_usb_host_connected_port_count(void);
uint32_t kernel_usb_host_connected_high_speed_port_count(void);
uint32_t kernel_usb_host_connected_super_speed_port_count(void);
uint32_t kernel_usb_host_root_device_count(void);
uint32_t kernel_usb_host_audio_candidate_count(void);
uint32_t kernel_usb_device_count(void);
uint32_t kernel_usb_device_ready_for_enum_count(void);
uint32_t kernel_usb_device_control_ready_count(void);
uint32_t kernel_usb_device_needs_companion_count(void);
uint32_t kernel_usb_device_companion_present_count(void);
uint32_t kernel_usb_device_handoff_ready_count(void);
uint32_t kernel_usb_device_control_path_ready_count(void);
uint32_t kernel_usb_device_probe_target_count(void);
uint32_t kernel_usb_audio_probe_target_count(void);
uint32_t kernel_usb_probe_snapshot_count(void);
uint32_t kernel_usb_audio_probe_snapshot_count(void);
uint32_t kernel_usb_probe_dispatch_ready_count(void);
uint32_t kernel_usb_audio_probe_dispatch_ready_count(void);
uint32_t kernel_usb_probe_exec_ready_count(void);
uint32_t kernel_usb_audio_probe_exec_ready_count(void);
uint32_t kernel_usb_probe_descriptor_ready_count(void);
uint32_t kernel_usb_audio_probe_descriptor_ready_count(void);
uint32_t kernel_usb_probe_configured_ready_count(void);
uint32_t kernel_usb_audio_probe_configured_ready_count(void);
uint32_t kernel_usb_probe_attached_ready_count(void);
uint32_t kernel_usb_audio_probe_attached_ready_count(void);
uint32_t kernel_usb_audio_class_probe_count(void);
uint32_t kernel_usb_mass_storage_probe_count(void);
int kernel_usb_audio_probe_first_configured(struct kernel_usb_probe_snapshot *info_out,
                                            uint32_t *match_index_out);
int kernel_usb_mass_storage_probe_first_configured(struct kernel_usb_probe_snapshot *info_out,
                                                   uint32_t *match_index_out);
int kernel_usb_audio_probe_attach_first_configured(struct kernel_usb_probe_snapshot *info_out,
                                                   uint32_t *match_index_out);
int kernel_usb_audio_refresh_probe_state(void);
int kernel_usb_audio_playback_controller_info(struct kernel_usb_host_controller_info *info_out);
uint8_t kernel_usb_audio_playback_transport_kind(void);
int kernel_usb_audio_playback_supported(void);
int kernel_usb_audio_playback_write(const uint8_t *data,
                                    uint32_t size,
                                    uint32_t *written_out);
int kernel_usb_probe_dispatch_context_for_index(uint32_t snapshot_index,
                                                struct kernel_usb_probe_dispatch_context *info_out);
int kernel_usb_control_read(uint32_t snapshot_index,
                            uint8_t request_type,
                            uint8_t request,
                            uint16_t request_value,
                            uint16_t request_index,
                            uint8_t *data_buffer,
                            uint32_t data_capacity,
                            uint32_t *actual_length_out);
int kernel_usb_no_data_request(uint32_t snapshot_index,
                               uint8_t request_type,
                               uint8_t request,
                               uint16_t request_value,
                               uint16_t request_index);
int kernel_usb_bulk_transfer(uint32_t snapshot_index,
                             uint8_t endpoint_address,
                             uint16_t max_packet_size,
                             uint8_t *data_toggle_io,
                             uint8_t *buffer,
                             uint32_t transfer_length,
                             uint32_t *actual_length_out);
int kernel_usb_probe_dispatch_next(uint8_t audio_only,
                                   struct kernel_usb_probe_snapshot *info_out,
                                   uint32_t *match_index_out);
int kernel_usb_probe_dispatch_context_next(uint8_t audio_only,
                                           struct kernel_usb_probe_dispatch_context *info_out);
int kernel_usb_probe_execute_next(uint8_t audio_only,
                                  struct kernel_usb_probe_execution *info_out);
int kernel_usb_host_controller_info(uint32_t index, struct kernel_usb_host_controller_info *info_out);
int kernel_usb_host_port_status(uint32_t controller_index,
                                uint32_t port_index,
                                struct kernel_usb_host_port_status *status_out);
int kernel_usb_host_root_device_info(uint32_t index, struct kernel_usb_host_root_device_info *info_out);
int kernel_usb_device_info(uint32_t index, struct kernel_usb_device_info *info_out);
int kernel_usb_device_probe_target(uint32_t start_index,
                                   uint8_t audio_only,
                                   struct kernel_usb_device_info *info_out,
                                   uint32_t *match_index_out);
int kernel_usb_device_probe_plan(uint32_t start_index,
                                 uint8_t audio_only,
                                 struct kernel_usb_probe_plan *plan_out,
                                 uint32_t *match_index_out);
int kernel_usb_probe_snapshot_info(uint32_t index, struct kernel_usb_probe_snapshot *info_out);
void kernel_usb_hid_poll(void);

#endif
