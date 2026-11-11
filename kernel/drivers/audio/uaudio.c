#include <kernel/driver_manager.h>
#include <kernel/drivers/audio/uaudio.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/drivers/pci/pci.h>
#include <kernel/drivers/usb/usb_host.h>
#include <kernel/hal/io.h>
#include <kernel/kernel_string.h>
#include <kernel/lock.h>
#include <kernel/memory/heap.h>

#define UAUDIO_SET_INTERFACE            0x0bu
#define UAUDIO_REQTYPE_HOST_TO_DEV      0x00u
#define UAUDIO_REQTYPE_STANDARD         0x00u
#define UAUDIO_REQTYPE_INTERFACE        0x01u
#define UAUDIO_REQTYPE_ENDPOINT         0x02u

struct uaudio_device {
    uint8_t present;
    uint8_t probe_attempted;

    uint32_t snapshot_index;

    uint8_t assigned_address;
    uint8_t configuration_value;
    uint8_t control_interface;
    uint8_t streaming_interface;
    uint8_t streaming_alt_setting;
    uint8_t streaming_interface_count;

    uint8_t endpoint_address;
    uint8_t endpoint_attributes;
    uint16_t endpoint_max_packet;

    uint8_t channels;
    uint8_t subframe_size;
    uint8_t bit_resolution;
    uint32_t sample_rate;

    uint8_t transport_kind;

    uint8_t *ring_buffer;
    uint32_t ring_size;
    uint32_t ring_head;
    uint32_t ring_tail;
    uint32_t ring_fill;

    uint8_t output_running;
    uint8_t stream_started;

    struct kernel_usb_host_controller_info controller_info;
    spinlock_t lock;
};

static struct uaudio_device g_uaudio;
static int g_registered = 0;

static const uint32_t uaudio_supported_rates[] = {
    8000u, 11025u, 12000u, 16000u, 22050u, 24000u,
    32000u, 44100u, 48000u, 64000u, 88200u, 96000u,
    128000u, 176400u, 192000u
};

static uint32_t uaudio_choose_rate(uint32_t candidate) {
    uint32_t best_rate = UAUDIO_DEFAULT_SAMPLE_RATE;
    uint32_t best_delta = 0xFFFFFFFFu;

    if (candidate == 0u) {
        return best_rate;
    }

    for (uint32_t i = 0u; i < sizeof(uaudio_supported_rates) / sizeof(uaudio_supported_rates[0]); i++) {
        uint32_t delta;
        if (uaudio_supported_rates[i] >= candidate) {
            delta = uaudio_supported_rates[i] - candidate;
        } else {
            delta = candidate - uaudio_supported_rates[i];
        }
        if (delta < best_delta) {
            best_delta = delta;
            best_rate = uaudio_supported_rates[i];
            if (delta == 0u) {
                break;
            }
        }
    }

    return best_rate;
}

static uint32_t uaudio_frame_bytes(void) {
    uint32_t channels = g_uaudio.channels;
    uint32_t bps = g_uaudio.subframe_size;

    if (channels == 0u || channels > 8u) {
        channels = UAUDIO_DEFAULT_CHANNELS;
    }
    if (bps == 0u || bps > 4u) {
        uint32_t bits = g_uaudio.bit_resolution;
        bps = (bits + 7u) / 8u;
    }
    if (bps == 0u) {
        bps = UAUDIO_DEFAULT_BPS;
    }
    return channels * bps;
}

static int uaudio_set_interface(void) {
    int status;

    status = kernel_usb_no_data_request(
        g_uaudio.snapshot_index,
        UAUDIO_REQTYPE_HOST_TO_DEV | UAUDIO_REQTYPE_STANDARD | UAUDIO_REQTYPE_INTERFACE,
        UAUDIO_SET_INTERFACE,
        (uint16_t)(((uint16_t)g_uaudio.streaming_alt_setting << 8) |
                   (uint16_t)g_uaudio.streaming_interface),
        (uint16_t)g_uaudio.streaming_interface);

    if (status != 0) {
        kernel_debug_printf("uaudio: set_interface if=%u alt=%u failed\n",
                            (unsigned int)g_uaudio.streaming_interface,
                            (unsigned int)g_uaudio.streaming_alt_setting);
        return -1;
    }

    kernel_debug_printf("uaudio: set_interface if=%u alt=%u ok\n",
                        (unsigned int)g_uaudio.streaming_interface,
                        (unsigned int)g_uaudio.streaming_alt_setting);
    return 0;
}

static int uaudio_set_sample_rate_v1(uint32_t rate) {
    uint8_t rate_buf[3];
    uint8_t ep_addr = g_uaudio.endpoint_address;
    uint32_t actual_length;
    int status;

    rate_buf[0] = (uint8_t)(rate & 0xffu);
    rate_buf[1] = (uint8_t)((rate >> 8) & 0xffu);
    rate_buf[2] = (uint8_t)((rate >> 16) & 0xffu);

    status = kernel_usb_control_read(g_uaudio.snapshot_index,
                                     UAUDIO_UT_WRITE_CLASS_ENDPOINT,
                                     UAUDIO_REQ_SET_CUR,
                                     (uint16_t)(((uint16_t)UAUDIO_REQSEL_RATE << 8) | 0x00u),
                                     (uint16_t)(((uint16_t)(ep_addr & 0x0fu) << 8) |
                                                (uint16_t)g_uaudio.streaming_interface),
                                     rate_buf,
                                     3u,
                                     &actual_length);
    if (status != 0) {
        status = kernel_usb_control_read(g_uaudio.snapshot_index,
                                         UAUDIO_UT_WRITE_CLASS_INTERFACE,
                                         UAUDIO_REQ_SET_CUR,
                                         (uint16_t)(((uint16_t)UAUDIO_REQSEL_RATE << 8) | 0x00u),
                                         (uint16_t)((0u << 8) |
                                                    (uint16_t)g_uaudio.streaming_interface),
                                         rate_buf,
                                         3u,
                                         &actual_length);
    }

    if (status != 0) {
        kernel_debug_printf("uaudio: set_sample_rate(%u) failed\n", (unsigned int)rate);
        return -1;
    }

    kernel_debug_printf("uaudio: set_sample_rate(%u) ok\n", (unsigned int)rate);
    return 0;
}

static int uaudio_set_sample_rate(uint32_t rate) {
    kernel_debug_printf("uaudio: configuring rate=%u channels=%u bits=%u\n",
                        (unsigned int)rate,
                        (unsigned int)g_uaudio.channels,
                        (unsigned int)g_uaudio.bit_resolution);

    return uaudio_set_sample_rate_v1(rate);
}

static void uaudio_reset_runtime(void) {
    g_uaudio.output_running = 0u;
    g_uaudio.stream_started = 0u;
    g_uaudio.ring_head = 0u;
    g_uaudio.ring_tail = 0u;
    g_uaudio.ring_fill = 0u;
}

static int uaudio_probe_device(void) {
    struct kernel_usb_probe_snapshot snapshot;
    uint32_t snapshot_index;
    uint32_t chosen_rate;
    uint32_t frame_bytes;

    kernel_debug_puts("uaudio: probing for usb audio devices\n");

    (void)kernel_usb_audio_refresh_probe_state();

    if (kernel_usb_audio_probe_first_configured(&snapshot, &snapshot_index) != 0) {
        kernel_debug_puts("uaudio: no configured audio device found\n");
        return -1;
    }

    if (snapshot.audio_control_interface_number == 0xffu ||
        snapshot.audio_streaming_interface_number == 0xffu ||
        snapshot.audio_playback_endpoint_address == 0xffu ||
        snapshot.audio_playback_endpoint_max_packet == 0u) {
        kernel_debug_puts("uaudio: audio device lacks required endpoints\n");
        return -1;
    }

    g_uaudio.snapshot_index = snapshot_index;

    g_uaudio.control_interface = snapshot.audio_control_interface_number;
    g_uaudio.streaming_interface = snapshot.audio_streaming_interface_number;
    g_uaudio.streaming_interface_count = snapshot.audio_streaming_interface_count;
    g_uaudio.streaming_alt_setting = snapshot.audio_streaming_alt_setting == 0xffu ?
        1u : snapshot.audio_streaming_alt_setting;

    g_uaudio.endpoint_address = snapshot.audio_playback_endpoint_address;
    g_uaudio.endpoint_attributes = snapshot.audio_playback_endpoint_attributes;
    g_uaudio.endpoint_max_packet = snapshot.audio_playback_endpoint_max_packet;

    g_uaudio.channels = snapshot.audio_playback_channel_count;
    g_uaudio.subframe_size = snapshot.audio_playback_subframe_size;
    g_uaudio.bit_resolution = snapshot.audio_playback_bit_resolution;

    chosen_rate = snapshot.audio_playback_sample_rate;
    if (chosen_rate == 0u) {
        chosen_rate = UAUDIO_DEFAULT_SAMPLE_RATE;
    }
    chosen_rate = uaudio_choose_rate(chosen_rate);
    g_uaudio.sample_rate = chosen_rate;

    g_uaudio.assigned_address = snapshot.assigned_address;
    g_uaudio.configuration_value = snapshot.configuration_value;

    g_uaudio.transport_kind = kernel_usb_audio_playback_transport_kind();

    (void)kernel_usb_audio_playback_controller_info(&g_uaudio.controller_info);

    frame_bytes = uaudio_frame_bytes();
    kernel_debug_printf("uaudio: device addr=%u cfg=%u ac_if=%u as_if=%u as_alt=%u\n",
                        (unsigned int)g_uaudio.assigned_address,
                        (unsigned int)g_uaudio.configuration_value,
                        (unsigned int)g_uaudio.control_interface,
                        (unsigned int)g_uaudio.streaming_interface,
                        (unsigned int)g_uaudio.streaming_alt_setting);
    kernel_debug_printf("uaudio: endpoint 0x%02x maxpkt=%u attr=0x%02x\n",
                        (unsigned int)g_uaudio.endpoint_address,
                        (unsigned int)g_uaudio.endpoint_max_packet,
                        (unsigned int)g_uaudio.endpoint_attributes);
    kernel_debug_printf("uaudio: format ch=%u bps=%u bits=%u rate=%u frame=%u\n",
                        (unsigned int)g_uaudio.channels,
                        (unsigned int)g_uaudio.subframe_size,
                        (unsigned int)g_uaudio.bit_resolution,
                        (unsigned int)g_uaudio.sample_rate,
                        (unsigned int)frame_bytes);
    kernel_debug_printf("uaudio: transport=%u controller=%d:%d.%d\n",
                        (unsigned int)g_uaudio.transport_kind,
                        (int)g_uaudio.controller_info.bus,
                        (int)g_uaudio.controller_info.slot,
                        (int)g_uaudio.controller_info.function);

    if (uaudio_set_interface() != 0) {
        kernel_debug_puts("uaudio: failed to select alt interface\n");
        return -1;
    }

    uaudio_set_sample_rate(chosen_rate);

    if (g_uaudio.ring_buffer == 0) {
        g_uaudio.ring_buffer = (uint8_t *)kernel_malloc(UAUDIO_RING_BUF_SIZE);
        if (g_uaudio.ring_buffer == 0) {
            kernel_debug_puts("uaudio: failed to allocate ring buffer\n");
            return -1;
        }
    }
    g_uaudio.ring_size = UAUDIO_RING_BUF_SIZE;

    uaudio_reset_runtime();
    g_uaudio.present = 1;

    kernel_debug_printf("uaudio: usb audio device ready on %d:%d.%d\n",
                        (int)g_uaudio.controller_info.bus,
                        (int)g_uaudio.controller_info.slot,
                        (int)g_uaudio.controller_info.function);
    return 0;
}

static int uaudio_start_stream(void) {
    uint32_t flags;

    if (!g_uaudio.present) {
        return -1;
    }

    flags = spinlock_lock_irqsave(&g_uaudio.lock);

    if (!g_uaudio.stream_started) {
        g_uaudio.stream_started = 1u;
        kernel_debug_puts("uaudio: stream started\n");
    }

    g_uaudio.output_running = 1u;
    spinlock_unlock_irqrestore(&g_uaudio.lock, flags);
    return 0;
}

static int uaudio_stop_stream(void) {
    uint32_t flags;

    if (!g_uaudio.present) {
        return -1;
    }

    flags = spinlock_lock_irqsave(&g_uaudio.lock);
    g_uaudio.output_running = 0u;
    g_uaudio.stream_started = 0u;
    uaudio_reset_runtime();
    spinlock_unlock_irqrestore(&g_uaudio.lock, flags);

    kernel_debug_puts("uaudio: stream stopped\n");
    return 0;
}

int kernel_uaudio_present(void) {
    return g_uaudio.present;
}

int kernel_uaudio_ioctl(int cmd, void *arg, size_t arg_size) {
    (void)arg_size;
    switch (cmd) {
    case DRV_IOCTL_GET_STATUS: {
        struct drv_device_status *st = (struct drv_device_status *)arg;
        if (!st) return -1;
        memset(st, 0, sizeof(*st));
        st->present = (uint32_t)g_uaudio.present;
        st->active = (uint32_t)(g_uaudio.present && g_uaudio.stream_started);
        return 0;
    }
    case AUDIO_IOCTL_GET_INFO: {
        struct { uint32_t rate; uint8_t channels; uint8_t bits; } *info = (void *)arg;
        if (!info || !g_uaudio.present) return -1;
        info->rate = g_uaudio.sample_rate;
        info->channels = g_uaudio.channels;
        info->bits = g_uaudio.bit_resolution;
        return 0;
    }
    default:
        return -1;
    }
}

int kernel_uaudio_playback_write(const uint8_t *data, uint32_t size) {
    struct uaudio_device *dev = &g_uaudio;
    uint32_t flags;
    uint32_t written = 0u;
    uint32_t frame_bytes;

    if (!dev->present || !dev->stream_started || data == 0 || size == 0u) {
        return -1;
    }

    frame_bytes = uaudio_frame_bytes();
    if (frame_bytes != 0u && (size % frame_bytes) != 0u) {
        kernel_debug_printf("uaudio: unaligned write size=%u frame=%u\n",
                            (unsigned int)size, (unsigned int)frame_bytes);
        return -1;
    }

    flags = spinlock_lock_irqsave(&dev->lock);

    while (written < size) {
        uint32_t space = dev->ring_size - dev->ring_fill;
        uint32_t chunk = size - written;

        if (space == 0u) {
            uint32_t sent = 0u;
            uint32_t flush_size = dev->ring_fill;

            if (frame_bytes != 0u && flush_size > frame_bytes) {
                flush_size -= flush_size % frame_bytes;
            }
            if (flush_size == 0u) {
                break;
            }

            spinlock_unlock_irqrestore(&dev->lock, flags);

            if (kernel_usb_audio_playback_write(dev->ring_buffer, flush_size, &sent) == 0 && sent > 0u) {
                flags = spinlock_lock_irqsave(&dev->lock);
                if (sent > dev->ring_fill) {
                    sent = dev->ring_fill;
                }
                dev->ring_fill -= sent;
                dev->ring_head = (dev->ring_head + sent) % dev->ring_size;
                space = dev->ring_size - dev->ring_fill;
            } else {
                flags = spinlock_lock_irqsave(&dev->lock);
                if (written == 0u) {
                    spinlock_unlock_irqrestore(&dev->lock, flags);
                    return -1;
                }
                break;
            }
        }

        if (chunk > space) {
            chunk = space;
        }
        if (chunk == 0u) {
            break;
        }

        {
            uint32_t tail = dev->ring_tail;
            uint32_t first_chunk = dev->ring_size - tail;

            if (first_chunk > chunk) {
                first_chunk = chunk;
            }
            memcpy(dev->ring_buffer + tail, data + written, first_chunk);
            if (first_chunk < chunk) {
                memcpy(dev->ring_buffer, data + written + first_chunk, chunk - first_chunk);
            }

            dev->ring_tail = (tail + chunk) % dev->ring_size;
            dev->ring_fill += chunk;
            written += chunk;
        }
    }

    spinlock_unlock_irqrestore(&dev->lock, flags);
    return (int)written;
}

void kernel_uaudio_irq_handler(void) {
}

int kernel_uaudio_init(void) {
    if (!g_registered) {
        register_driver("uaudio", "audio", (void (*)(void))kernel_uaudio_init);
        g_registered = 1;
        return 0;
    }

    kernel_debug_puts("audio: probing usb audio class devices\n");

    if (g_uaudio.present) {
        kernel_debug_puts("audio: uaudio already initialized\n");
        return 0;
    }

    memset(&g_uaudio, 0, sizeof(g_uaudio));
    g_uaudio.ring_buffer = 0;
    g_uaudio.present = 0;
    spinlock_init(&g_uaudio.lock);

    if (uaudio_probe_device() != 0) {
        kernel_debug_puts("audio: no usb audio class device found\n");
        return -1;
    }

    if (g_uaudio.controller_info.irq_line != 0u) {
        kernel_debug_printf("uaudio: controller irq=%u\n",
                            (unsigned int)g_uaudio.controller_info.irq_line);
    }

    if (uaudio_start_stream() != 0) {
        kernel_debug_puts("uaudio: stream start failed\n");
        uaudio_stop_stream();
        g_uaudio.present = 0;
        return -1;
    }

    kernel_debug_puts("audio: usb audio class driver ready\n");
    return 0;
}
