#ifndef KERNEL_DRIVERS_AUDIO_UAUDIO_H
#define KERNEL_DRIVERS_AUDIO_UAUDIO_H

#include <stdint.h>

#define UAUDIO_CLASS_AUDIO              0x01u
#define UAUDIO_SUBCLASS_AUDIOCONTROL    0x01u
#define UAUDIO_SUBCLASS_AUDIOSTREAMING  0x02u

#define UAUDIO_CS_INTERFACE             0x24u
#define UAUDIO_CS_ENDPOINT              0x25u

#define UAUDIO_AC_HEADER                0x01u
#define UAUDIO_AC_INPUT_TERMINAL        0x02u
#define UAUDIO_AC_OUTPUT_TERMINAL       0x03u
#define UAUDIO_AC_MIXER_UNIT            0x04u
#define UAUDIO_AC_SELECTOR_UNIT         0x05u
#define UAUDIO_AC_FEATURE_UNIT          0x06u
#define UAUDIO_AC_PROCESSING_UNIT       0x08u
#define UAUDIO_AC_EXTENSION_UNIT        0x09u
#define UAUDIO_AC_CLOCK_SOURCE          0x0au
#define UAUDIO_AC_CLOCK_SELECTOR        0x0bu
#define UAUDIO_AC_CLOCK_MULTIPLIER      0x0cu

#define UAUDIO_AS_GENERAL               0x01u
#define UAUDIO_AS_FORMAT_TYPE           0x02u
#define UAUDIO_AS_FORMAT_SPECIFIC       0x03u

#define UAUDIO_EP_GENERAL               0x01u

#define UAUDIO_FORMAT_TYPE_I            0x01u
#define UAUDIO_FORMAT_TYPE_II           0x02u
#define UAUDIO_FORMAT_TYPE_III          0x03u

#define UAUDIO_V1_FMT_PCM               0x0001u
#define UAUDIO_V1_FMT_PCM8              0x0002u
#define UAUDIO_V1_FMT_IEEE_FLOAT        0x0003u
#define UAUDIO_V1_FMT_ALAW              0x0004u
#define UAUDIO_V1_FMT_MULAW             0x0005u

#define UAUDIO_V2_FMT_PCM               0x00000001u
#define UAUDIO_V2_FMT_PCM8              0x00000002u
#define UAUDIO_V2_FMT_IEEE_FLOAT        0x00000004u
#define UAUDIO_V2_FMT_ALAW              0x00000008u
#define UAUDIO_V2_FMT_MULAW             0x00000010u

#define UAUDIO_REQ_SET_CUR              0x01u
#define UAUDIO_REQ_GET_CUR              0x81u
#define UAUDIO_REQ_SET_MIN              0x02u
#define UAUDIO_REQ_GET_MIN              0x82u
#define UAUDIO_REQ_SET_MAX              0x03u
#define UAUDIO_REQ_GET_MAX              0x83u
#define UAUDIO_REQ_SET_RES              0x04u
#define UAUDIO_REQ_GET_RES              0x84u
#define UAUDIO_REQ_V2_CUR               0x01u
#define UAUDIO_REQ_V2_RANGE             0x02u

#define UAUDIO_V2_REQSEL_CLOCK_FREQ     0x01u
#define UAUDIO_V2_REQSEL_CLOCK_SEL      0x01u

#define UAUDIO_REQSEL_MUTE              0x01u
#define UAUDIO_REQSEL_VOLUME            0x02u
#define UAUDIO_REQSEL_BASS              0x03u
#define UAUDIO_REQSEL_MID               0x04u
#define UAUDIO_REQSEL_TREBLE            0x05u
#define UAUDIO_REQSEL_EQ                0x06u
#define UAUDIO_REQSEL_AGC               0x07u
#define UAUDIO_REQSEL_DELAY             0x08u
#define UAUDIO_REQSEL_BASSBOOST         0x09u
#define UAUDIO_REQSEL_LOUDNESS          0x0au
#define UAUDIO_REQSEL_GAIN              0x0bu
#define UAUDIO_REQSEL_PHASE_INVERT      0x0du

#define UAUDIO_REQSEL_RATE              0x01u
#define UAUDIO_REQSEL_CLOCK             0x01u

#define UAUDIO_EP_SYNC_ADAPTIVE         0x00u
#define UAUDIO_EP_SYNC_ASYNC            0x01u
#define UAUDIO_EP_SYNC_SYNC             0x02u
#define UAUDIO_EP_SYNC_MASK             0x0cu

#define UAUDIO_EP_USAGE_DATA            0x00u
#define UAUDIO_EP_USAGE_FEEDBACK        0x10u
#define UAUDIO_EP_USAGE_IMPLICIT        0x20u
#define UAUDIO_EP_USAGE_MASK            0x30u

#define UAUDIO_TERMINAL_USB_STREAMING   0x0101u
#define UAUDIO_TERMINAL_UNDEFINED       0x0200u
#define UAUDIO_TERMINAL_MICROPHONE      0x0201u
#define UAUDIO_TERMINAL_DESKTOP_MIC     0x0202u
#define UAUDIO_TERMINAL_PERSONAL_MIC    0x0203u
#define UAUDIO_TERMINAL_OMNI_MIC        0x0204u
#define UAUDIO_TERMINAL_ARRAY_MIC       0x0205u
#define UAUDIO_TERMINAL_PROC_ARRAY_MIC  0x0206u
#define UAUDIO_TERMINAL_SPEAKER         0x0301u
#define UAUDIO_TERMINAL_HEADPHONES      0x0302u
#define UAUDIO_TERMINAL_HEAD_MATRIX     0x0303u
#define UAUDIO_TERMINAL_REAR_SPEAKER    0x0304u
#define UAUDIO_TERMINAL_SUBWOOFER       0x0305u
#define UAUDIO_TERMINAL_LINE_OUT        0x0601u
#define UAUDIO_TERMINAL_LINE_IN         0x0602u
#define UAUDIO_TERMINAL_SPDIF_OUT       0x0603u
#define UAUDIO_TERMINAL_SPDIF_IN        0x0604u
#define UAUDIO_TERMINAL_DIGITAL_OUT     0x0605u
#define UAUDIO_TERMINAL_DIGITAL_IN      0x0606u
#define UAUDIO_TERMINAL_HEADPHONE_OUT   0x0701u
#define UAUDIO_TERMINAL_CD_IN           0x0702u

#define UAUDIO_BMREQ_DIR_HOST_TO_DEVICE 0x00u
#define UAUDIO_BMREQ_DIR_DEVICE_TO_HOST 0x80u
#define UAUDIO_BMREQ_TYPE_CLASS         0x20u
#define UAUDIO_BMREQ_RECIPIENT_INTERFACE 0x01u
#define UAUDIO_BMREQ_RECIPIENT_ENDPOINT  0x02u

#define UAUDIO_UT_READ_CLASS_INTERFACE \
    (UAUDIO_BMREQ_DIR_DEVICE_TO_HOST | UAUDIO_BMREQ_TYPE_CLASS | UAUDIO_BMREQ_RECIPIENT_INTERFACE)
#define UAUDIO_UT_WRITE_CLASS_INTERFACE \
    (UAUDIO_BMREQ_DIR_HOST_TO_DEVICE | UAUDIO_BMREQ_TYPE_CLASS | UAUDIO_BMREQ_RECIPIENT_INTERFACE)
#define UAUDIO_UT_READ_CLASS_ENDPOINT \
    (UAUDIO_BMREQ_DIR_DEVICE_TO_HOST | UAUDIO_BMREQ_TYPE_CLASS | UAUDIO_BMREQ_RECIPIENT_ENDPOINT)
#define UAUDIO_UT_WRITE_CLASS_ENDPOINT \
    (UAUDIO_BMREQ_DIR_HOST_TO_DEVICE | UAUDIO_BMREQ_TYPE_CLASS | UAUDIO_BMREQ_RECIPIENT_ENDPOINT)

#define UAUDIO_AC_HEADER_LENGTH_V1      9u
#define UAUDIO_AS_GENERAL_LENGTH_V1     7u
#define UAUDIO_AS_GENERAL_LENGTH_V2     16u
#define UAUDIO_FORMAT_TYPE_I_MIN        8u
#define UAUDIO_EP_GENERAL_LENGTH_V1     7u
#define UAUDIO_EP_GENERAL_LENGTH_V2     8u

#define UAUDIO_SAMPLE_RATE_48KHZ        48000u
#define UAUDIO_SAMPLE_RATE_44100HZ      44100u
#define UAUDIO_SAMPLE_RATE_32KHZ        32000u
#define UAUDIO_SAMPLE_RATE_22KHZ        22050u
#define UAUDIO_SAMPLE_RATE_16KHZ        16000u
#define UAUDIO_SAMPLE_RATE_11KHZ        11025u
#define UAUDIO_SAMPLE_RATE_8KHZ         8000u

#define UAUDIO_BIT_DEPTH_16             16u
#define UAUDIO_BIT_DEPTH_24             24u
#define UAUDIO_BIT_DEPTH_32             32u

#define UAUDIO_CHANNELS_MONO            1u
#define UAUDIO_CHANNELS_STEREO          2u
#define UAUDIO_CHANNELS_5_1             6u
#define UAUDIO_CHANNELS_7_1             8u

#define UAUDIO_SAMPLING_FREQ_CONTROL    (1u << 0)
#define UAUDIO_PITCH_CONTROL            (1u << 1)

struct uaudio_format_type_i {
    uint8_t length;
    uint8_t descriptor_type;
    uint8_t descriptor_subtype;
    uint8_t format_type;
    uint8_t channels;
    uint8_t subframe_size;
    uint8_t bit_resolution;
    uint8_t sample_freq_type;
} __attribute__((packed));

struct uaudio_ac_header_v1 {
    uint8_t length;
    uint8_t descriptor_type;
    uint8_t descriptor_subtype;
    uint16_t bcd_adc;
    uint16_t total_length;
    uint8_t in_collection;
} __attribute__((packed));

struct uaudio_input_terminal {
    uint8_t length;
    uint8_t descriptor_type;
    uint8_t descriptor_subtype;
    uint8_t terminal_id;
    uint16_t terminal_type;
    uint8_t assoc_terminal;
    uint8_t nr_channels;
    uint16_t channel_config;
    uint8_t channel_names;
    uint8_t terminal;
} __attribute__((packed));

struct uaudio_output_terminal {
    uint8_t length;
    uint8_t descriptor_type;
    uint8_t descriptor_subtype;
    uint8_t terminal_id;
    uint16_t terminal_type;
    uint8_t assoc_terminal;
    uint8_t source_id;
    uint8_t terminal;
} __attribute__((packed));

struct uaudio_as_general_v1 {
    uint8_t length;
    uint8_t descriptor_type;
    uint8_t descriptor_subtype;
    uint8_t terminal_link;
    uint8_t delay;
    uint16_t format_tag;
} __attribute__((packed));

struct uaudio_ep_general_v1 {
    uint8_t length;
    uint8_t descriptor_type;
    uint8_t descriptor_subtype;
    uint8_t attributes;
    uint8_t unit_id;
    uint8_t sync_rate;
} __attribute__((packed));

struct uaudio_ep_general_v2 {
    uint8_t length;
    uint8_t descriptor_type;
    uint8_t descriptor_subtype;
    uint8_t attributes;
    uint8_t bm_controls;
    uint16_t unit_id;
} __attribute__((packed));

#define UAUDIO_RING_BUF_SIZE            16384u
#define UAUDIO_FRAME_BYTES              4u
#define UAUDIO_DEFAULT_SAMPLE_RATE      48000u
#define UAUDIO_DEFAULT_CHANNELS         2u
#define UAUDIO_DEFAULT_BITS             16u
#define UAUDIO_DEFAULT_BPS              2u

int kernel_uaudio_init(void);
int kernel_uaudio_present(void);
int kernel_uaudio_playback_write(const uint8_t *data, uint32_t size);
void kernel_uaudio_irq_handler(void);
int kernel_uaudio_ioctl(int cmd, void *arg, size_t arg_size);

#endif
