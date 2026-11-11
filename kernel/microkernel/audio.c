#include <kernel/kernel_string.h>
#include <kernel/drivers/pci/pci.h>
#include <kernel/hal/io.h>
#include <kernel/interrupt.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/drivers/timer/timer.h>
#include <kernel/drivers/usb/usb_host.h>
#include <kernel/drivers/audio/uaudio.h>
#include <kernel/drivers/video/video.h>
#include <kernel/memory/heap.h>
#include <kernel/memory/physmem.h>
#include <kernel/microkernel/audio.h>
#include <kernel/microkernel/launch.h>
#include <kernel/microkernel/message.h>
#include <kernel/microkernel/service.h>
#include <kernel/scheduler.h>
#include <kernel/userland_service.h>

#define MK_AUDIO_SOFT_BUFFER_SIZE 16384u
#define MK_AUDIO_ASYNC_WRITE_BUFFER_SIZE 65536u
#define MK_AUDIO_ASYNC_WRITE_CHUNK 4096u
#define MK_AUDIO_EVENT_SUBSCRIBERS 8u
#define MK_AUDIO_EVENT_QUEUE_SIZE 16u
#define PCI_CLASS_MULTIMEDIA 0x04u
#define PCI_SUBCLASS_MULTIMEDIA_AUDIO 0x01u
#define PCI_SUBCLASS_MULTIMEDIA_HDA 0x03u
#define PCI_VENDOR_INTEL 0x8086u
#define PCI_VENDOR_AMD 0x1022u
#define PCI_VENDOR_ATI 0x1002u
#define PCI_VENDOR_NVIDIA 0x10DEu
#define PCI_VENDOR_ALI 0x10B9u
#define PCI_VENDOR_SIS 0x1039u
#define PCI_VENDOR_VIATECH 0x1106u

#define PCI_PRODUCT_INTEL_82801AA_ACA 0x2415u
#define PCI_PRODUCT_INTEL_82801AB_ACA 0x2425u
#define PCI_PRODUCT_INTEL_82801BA_ACA 0x2445u
#define PCI_PRODUCT_INTEL_82801CA_ACA 0x2485u
#define PCI_PRODUCT_INTEL_82801DB_ACA 0x24c5u
#define PCI_PRODUCT_INTEL_82801EB_ACA 0x24d5u
#define PCI_PRODUCT_INTEL_6300ESB_ACA 0x25a6u
#define PCI_PRODUCT_INTEL_82801FB_ACA 0x266eu
#define PCI_PRODUCT_INTEL_6321ESB_ACA 0x2698u
#define PCI_PRODUCT_INTEL_82801GB_ACA 0x27deu
#define PCI_PRODUCT_INTEL_82440MX_ACA 0x7195u
#define PCI_PRODUCT_AMD_768_ACA 0x7445u
#define PCI_PRODUCT_AMD_8111_ACA 0x746du
#define PCI_PRODUCT_ATI_SB200_AUDIO 0x4341u
#define PCI_PRODUCT_ATI_SB300_AUDIO 0x4361u
#define PCI_PRODUCT_ATI_SB400_AUDIO 0x4370u
#define PCI_PRODUCT_ATI_SB600_AUDIO 0x4382u
#define PCI_PRODUCT_NVIDIA_MCP04_AC97 0x003au
#define PCI_PRODUCT_NVIDIA_NFORCE4_AC97 0x0059u
#define PCI_PRODUCT_NVIDIA_NFORCE2_AC97 0x006au
#define PCI_PRODUCT_NVIDIA_NFORCE2_400_AC97 0x008au
#define PCI_PRODUCT_NVIDIA_NFORCE3_AC97 0x00dau
#define PCI_PRODUCT_NVIDIA_NFORCE3_250_AC97 0x00eau
#define PCI_PRODUCT_NVIDIA_NFORCE_AC97 0x01b1u
#define PCI_PRODUCT_NVIDIA_MCP51_AC97 0x026bu
#define PCI_PRODUCT_ALI_M5455_AC97 0x5455u
#define PCI_PRODUCT_SIS_7012_AC97 0x7012u
#define PCI_PRODUCT_VIATECH_VT82C686A_AC97 0x3058u
#define PCI_PRODUCT_VIATECH_VT8233_AC97 0x3059u

#define AUICH_NAMBAR_INDEX 0u
#define AUICH_NABMBAR_INDEX 1u
#define AUICH_MMBAR_INDEX 2u
#define AUICH_MBBAR_INDEX 3u
#define AUICH_CFG 0x41u
#define AUICH_CFG_IOSE 0x01u
#define AUICH_GCTRL 0x2cu
#define AUICH_GSTS 0x30u
#define AUICH_CAS 0x34u
#define AUICH_PCMI 0x00u
#define AUICH_PCMO 0x10u
#define AUICH_BDBAR 0x00u
#define AUICH_CIV 0x04u
#define AUICH_LVI 0x05u
#define AUICH_LVI_MASK 0x1fu
#define AUICH_STS 0x06u
#define AUICH_PICB 0x08u
#define AUICH_PIV 0x0au
#define AUICH_CTRL 0x0bu
#define AUICH_PCR 0x00100u
#define AUICH_ACLSO 0x08u
#define AUICH_WRESET 0x04u
#define AUICH_CRESET 0x02u
#define AUICH_IOCE 0x10u
#define AUICH_FEIE 0x08u
#define AUICH_LVBIE 0x04u
#define AUICH_RR 0x02u
#define AUICH_RPBM 0x01u
#define AUICH_FIFOE 0x10u
#define AUICH_BCIS 0x08u
#define AUICH_LVBCI 0x04u
#define AUICH_CELV 0x02u
#define AUICH_DCH 0x01u
#define AUICH_SEMATIMO 1000u
#define AUICH_RESETIMO 500000u
#define AUICH_DMALIST_MAX 32u
#define AUICH_DMAF_IOC 0x80000000u
#define AUICH_DMA_SLOT_SIZE 512u
#define ICH_SIS_NV_CTL 0x4cu
#define ICH_SIS_CTL_UNMUTE 0x01u

#define AC97_REG_RESET 0x00u
#define AC97_REG_MASTER_VOLUME 0x02u
#define AC97_REG_HEADPHONE_VOLUME 0x04u
#define AC97_REG_MIC_VOLUME 0x0eu
#define AC97_REG_LINEIN_VOLUME 0x10u
#define AC97_REG_PCMOUT_VOLUME 0x18u
#define AC97_REG_RECORD_SELECT 0x1au
#define AC97_REG_RECORD_GAIN 0x1cu
#define AC97_REG_EXT_AUDIO_ID 0x28u
#define AC97_REG_EXT_AUDIO_CTRL 0x2au
#define AC97_REG_PCM_FRONT_DAC_RATE 0x2cu
#define AC97_REG_PCM_SURR_DAC_RATE 0x2eu
#define AC97_REG_PCM_LFE_DAC_RATE 0x30u
#define AC97_REG_PCM_LR_ADC_RATE 0x32u
#define AC97_REG_CENTER_LFE_MASTER 0x36u
#define AC97_REG_SURR_MASTER 0x38u
#define AC97_CAPS_MICIN 0x0001u
#define AC97_CAPS_HEADPHONES 0x0010u
#define AC97_EXT_AUDIO_VRA 0x0001u
#define AC97_EXT_AUDIO_CDAC 0x0040u
#define AC97_EXT_AUDIO_SDAC 0x0080u
#define AC97_EXT_AUDIO_LDAC 0x0100u
#define AC97_RECORD_SOURCE_MIC 0u
#define AC97_RECORD_SOURCE_LINEIN 4u

#define HDA_BAR_INDEX 0u
#define HDA_GCAP 0x00u
#define HDA_GCAP_ISS_SHIFT 8u
#define HDA_GCAP_ISS_MASK 0x0fu
#define HDA_GCAP_OSS_SHIFT 12u
#define HDA_GCAP_OSS_MASK 0x0fu
#define HDA_GCAP_BSS_SHIFT 3u
#define HDA_GCAP_BSS_MASK 0x1fu
#define HDA_VMIN 0x02u
#define HDA_VMAJ 0x03u
#define HDA_GCTL 0x08u
#define HDA_GCTL_CRST 0x00000001u
#define HDA_GCTL_UNSOL 0x00000100u
#define HDA_STATESTS 0x0eu
#define HDA_STATESTS_SDIWAKE 0x7fffu
#define HDA_GSTS 0x10u
#define HDA_GSTS_FSTS 0x0002u
#define HDA_INTCTL 0x20u
#define HDA_INTCTL_GIE 0x80000000u
#define HDA_INTCTL_CIE 0x40000000u
#define HDA_INTSTS 0x24u
#define HDA_CORBLBASE 0x40u
#define HDA_CORBUBASE 0x44u
#define HDA_CORBWP 0x48u
#define HDA_CORBWP_MASK 0x00ffu
#define HDA_CORBRP 0x4au
#define HDA_CORBRP_CORBRPRST 0x8000u
#define HDA_CORBCTL 0x4cu
#define HDA_CORBCTL_CORBRUN 0x02u
#define HDA_CORBSTS 0x4du
#define HDA_CORBSIZE 0x4eu
#define HDA_CORBSIZE_CAP_2 0x10u
#define HDA_CORBSIZE_CAP_16 0x20u
#define HDA_CORBSIZE_CAP_256 0x40u
#define HDA_CORBSIZE_SEL_2 0x00u
#define HDA_CORBSIZE_SEL_16 0x01u
#define HDA_CORBSIZE_SEL_256 0x02u
#define HDA_RIRBLBASE 0x50u
#define HDA_RIRBUBASE 0x54u
#define HDA_RIRBWP 0x58u
#define HDA_RIRBWP_MASK 0x00ffu
#define HDA_RIRBWP_RST 0x8000u
#define HDA_RINTCNT 0x5au
#define HDA_RIRBCTL 0x5cu
#define HDA_RIRBCTL_RINTCTL 0x01u
#define HDA_RIRBCTL_DMAEN 0x02u
#define HDA_RIRBSTS 0x5du
#define HDA_RIRBSTS_RINTFL 0x01u
#define HDA_RIRBSTS_RIRBOIS 0x04u
#define HDA_RIRB_RESP_UNSOL 0x10u
#define HDA_RIRB_RESP_CODEC_MASK 0x0fu
#define HDA_RIRBSIZE 0x5eu
#define HDA_RIRBSIZE_CAP_2 0x10u
#define HDA_RIRBSIZE_CAP_16 0x20u
#define HDA_RIRBSIZE_CAP_256 0x40u
#define HDA_RIRBSIZE_SEL_2 0x00u
#define HDA_RIRBSIZE_SEL_16 0x01u
#define HDA_RIRBSIZE_SEL_256 0x02u
#define HDA_SD_BASE 0x80u
#define HDA_SD_CTL 0x00u
#define HDA_SD_CTL_DEIE 0x0010u
#define HDA_SD_CTL_FEIE 0x0008u
#define HDA_SD_CTL_IOCE 0x0004u
#define HDA_SD_CTL_RUN 0x0002u
#define HDA_SD_CTL_SRST 0x0001u
#define HDA_SD_CTL2 0x02u
#define HDA_SD_CTL2_STRM 0xf0u
#define HDA_SD_CTL2_STRM_SHIFT 4u
#define HDA_SD_STS 0x03u
#define HDA_SD_STS_DESE 0x10u
#define HDA_SD_STS_FIFOE 0x08u
#define HDA_SD_STS_BCIS 0x04u
#define HDA_SD_LPIB 0x04u
#define HDA_SD_CBL 0x08u
#define HDA_SD_LVI 0x0cu
#define HDA_SD_LVI_MASK 0x00ffu
#define HDA_SD_FIFOS 0x10u
#define HDA_SD_FMT 0x12u
#define HDA_SD_FMT_BASE_48 0x0000u
#define HDA_SD_FMT_BASE_44 0x4000u
#define HDA_SD_FMT_MULT_X1 0x0000u
#define HDA_SD_FMT_MULT_X2 0x0800u
#define HDA_SD_FMT_MULT_X4 0x1800u
#define HDA_SD_FMT_DIV_BY1 0x0000u
#define HDA_SD_FMT_DIV_BY2 0x0100u
#define HDA_SD_FMT_DIV_BY3 0x0200u
#define HDA_SD_FMT_DIV_BY4 0x0300u
#define HDA_SD_FMT_DIV_BY6 0x0500u
#define HDA_SD_FMT_BITS_8_16 0x0000u
#define HDA_SD_FMT_BITS_16_16 0x0010u
#define HDA_SD_FMT_BITS_20_32 0x0020u
#define HDA_SD_FMT_BITS_24_32 0x0030u
#define HDA_SD_FMT_BITS_32_32 0x0040u
#define HDA_SD_BDPL 0x18u
#define HDA_SD_BDPU 0x1cu
#define HDA_SD_SIZE 0x20u
#define HDA_RESET_TIMEOUT 5000u
#define HDA_POLL_TIMEOUT 5000u
#define HDA_CORB_TIMEOUT 5000u
#define HDA_MAX_CODECS 15u
#define HDA_BDL_MAX 32u
#define HDA_UNSOL_QUEUE_SIZE 16u
#define HDA_IC 0x60u
#define HDA_IR 0x64u
#define HDA_IRS 0x68u
#define HDA_IRS_VALID 0x02u
#define HDA_IRS_BUSY 0x01u
#define HDA_DPLBASE 0x70u
#define HDA_DPUBASE 0x74u

#define HDA_VERB_GET_PARAMETER 0xf00u
#define HDA_VERB_GET_CONFIG_DEFAULT 0xf1cu
#define HDA_VERB_SET_POWER_STATE 0x705u
#define HDA_VERB_SET_AMP_GAIN_MUTE 0x300u
#define HDA_VERB_SET_CONVERTER_FORMAT 0x2u
#define HDA_VERB_GET_CONNECTION_SELECT 0xf01u
#define HDA_VERB_SET_STREAM_CHANNEL 0x706u
#define HDA_VERB_SET_PIN_WIDGET_CONTROL 0x707u
#define HDA_VERB_SET_EAPD_BTLENABLE 0x70cu
#define HDA_VERB_GET_EAPD_BTLENABLE 0xf0cu
#define HDA_PARAM_VENDOR_ID 0x00u
#define HDA_PARAM_REVISION_ID 0x02u
#define HDA_PARAM_SUB_NODE_COUNT 0x04u
#define HDA_PARAM_FUNCTION_GROUP_TYPE 0x05u
#define HDA_PARAM_AUDIO_WIDGET_CAP 0x09u
#define HDA_PARAM_PIN_CAP 0x0cu
#define HDA_PARAM_INPUT_AMP_CAP 0x0du
#define HDA_PARAM_CONN_LIST_LEN 0x0eu
#define HDA_PARAM_OUTPUT_AMP_CAP 0x12u
#define HDA_FGTYPE_AUDIO 0x01u
#define HDA_WCAP_INAMP 0x00000002u
#define HDA_WCAP_OUTAMP 0x00000004u
#define HDA_WCAP_AMPOV 0x00000008u
#define HDA_WCAP_STEREO 0x00000001u
#define HDA_WCAP_FORMATOV 0x00000010u
#define HDA_WCAP_POWER 0x00000400u
#define HDA_WCAP_UNSOL 0x00000080u
#define HDA_WCAP_TYPE_SHIFT 20u
#define HDA_WCAP_TYPE_MASK 0x0fu
#define HDA_WID_AUD_OUT 0x0u
#define HDA_WID_AUD_IN 0x1u
#define HDA_WID_AUD_MIXER 0x2u
#define HDA_WID_AUD_SELECTOR 0x3u
#define HDA_WID_PIN 0x4u
#define HDA_WID_BEEP_GENERATOR 0x7u
#define HDA_WCAP_DIGITAL 0x00000200u
#define HDA_WCAP_CONNLIST 0x00000100u
#define HDA_CONNLIST_LONG 0x00000080u
#define HDA_CONNLIST_LEN_MASK 0x0000007fu
#define HDA_PINCAP_PRESENCE 0x00000004u
#define HDA_PINCAP_HEADPHONE 0x00000008u
#define HDA_PINCAP_OUTPUT 0x00000010u
#define HDA_PINCAP_INPUT 0x00000020u
#define HDA_PINCAP_HDMI 0x00000080u
#define HDA_PINCAP_EAPD 0x00010000u
#define HDA_PINCAP_VREF_SHIFT 8u
#define HDA_PINCAP_VREF_MASK 0xffu
#define HDA_CONFIG_DEVICE_SHIFT 20u
#define HDA_CONFIG_DEVICE_MASK 0x0fu
#define HDA_CONFIG_PORT_SHIFT 30u
#define HDA_CONFIG_PORT_MASK 0x03u
#define HDA_CONFIG_MISC_SHIFT 8u
#define HDA_CONFIG_MISC_MASK 0x0fu
#define HDA_CONFIG_LOC_SHIFT 24u
#define HDA_CONFIG_LOC_MASK 0x3fu
#define HDA_CONFIG_ASSOC_SHIFT 4u
#define HDA_CONFIG_ASSOC_MASK 0x0fu
#define HDA_CONFIG_SEQ_SHIFT 0u
#define HDA_CONFIG_SEQ_MASK 0x0fu
#define HDA_CONFIG_MISC_PRESENCEOV 0x1u
#define HDA_CONFIG_DEVICE_LINEOUT 0x0u
#define HDA_CONFIG_DEVICE_SPEAKER 0x1u
#define HDA_CONFIG_DEVICE_HEADPHONE 0x2u
#define HDA_CONFIG_DEVICE_SPDIFOUT 0x4u
#define HDA_CONFIG_DEVICE_DIGITALOUT 0x5u
#define HDA_CONFIG_DEVICE_LINEIN 0x8u
#define HDA_QEMU_VENDOR_ID 0x1af40000u
#define HDA_QEMU_CODEC_OUTPUT 0x1af40010u
#define HDA_QEMU_CODEC_DUPLEX 0x1af40020u
#define HDA_CONFIG_DEVICE_MICIN 0xau
#define HDA_CONFIG_PORT_JACK 0x0u
#define HDA_CONFIG_PORT_NONE 0x1u
#define HDA_CONFIG_PORT_FIXED 0x2u
#define HDA_CONFIG_PORT_BOTH 0x3u
#define HDA_POWER_STATE_D0 0x00u
#define HDA_VERB_SET_CONNECTION_SELECT 0x701u
#define HDA_VERB_SET_UNSOLICITED_RESPONSE 0x708u
#define HDA_VERB_GET_CONNECTION_LIST_ENTRY 0xf02u
#define HDA_VERB_GET_PIN_SENSE 0xf09u
#define HDA_VERB_EXECUTE_PIN_SENSE 0x709u
#define HDA_PINCTL_OUT_EN 0x40u
#define HDA_PINCTL_IN_EN 0x20u
#define HDA_PINCTL_HP_EN 0x80u
#define HDA_PINCTL_VREF_50 0x01u
#define HDA_PINCTL_VREF_80 0x04u
#define HDA_EAPD_ENABLE 0x02u
#define HDA_UNSOL_ENABLE 0x80u
#define HDA_UNSOL_TAG_MASK 0x3fu
#define HDA_PIN_SENSE_PRESENCE 0x80000000u
#define HDA_STREAM_FORMAT_PCM 0x00000001u
#define HDA_VERB_GET_COEFFICIENT_INDEX 0xd00u
#define HDA_VERB_SET_COEFFICIENT_INDEX 0x500u
#define HDA_VERB_GET_PROCESSING_COEFFICIENT 0xc00u
#define HDA_VERB_SET_PROCESSING_COEFFICIENT 0x400u
#define HDA_VERB_GET_GPIO_DATA 0xf15u
#define HDA_VERB_SET_GPIO_DATA 0x715u
#define HDA_VERB_GET_GPIO_ENABLE_MASK 0xf16u
#define HDA_VERB_SET_GPIO_ENABLE_MASK 0x716u
#define HDA_VERB_GET_GPIO_DIRECTION 0xf17u
#define HDA_VERB_SET_GPIO_DIRECTION 0x717u
#define HDA_VERB_GET_GPIO_POLARITY 0xfe7u
#define HDA_VERB_SET_GPIO_POLARITY 0x7e7u
#define HDA_AMP_GAIN_MASK 0x7fu
#define HDA_AMP_GAIN_MUTE 0x80u
#define HDA_AMP_GAIN_INDEX_SHIFT 8u
#define HDA_AMP_GAIN_RIGHT 0x1000u
#define HDA_AMP_GAIN_LEFT 0x2000u
#define HDA_AMP_GAIN_INPUT 0x4000u
#define HDA_AMP_GAIN_OUTPUT 0x8000u
#define PCI_COMMAND_BACKTOBACK_ENABLE 0x00000200u
#define ICH_PCI_HDTCSEL 0x44u
#define ICH_PCI_HDTCSEL_MASK 0x07u
#define ICH_PCI_MMC 0x62u
#define ICH_PCI_MMC_ME 0x01u
#define ATI_PCIE_SNOOP_REG 0x42u
#define ATI_PCIE_SNOOP_MASK 0xf8u
#define ATI_PCIE_SNOOP_ENABLE 0x02u
#define NVIDIA_PCIE_SNOOP_REG 0x4eu
#define NVIDIA_PCIE_SNOOP_MASK 0xf0u
#define NVIDIA_PCIE_SNOOP_ENABLE 0x0fu
#define NVIDIA_HDA_ISTR_COH_REG 0x4du
#define NVIDIA_HDA_OSTR_COH_REG 0x4cu
#define NVIDIA_HDA_STR_COH_ENABLE 0x01u
#define INTEL_PCIE_NOSNOOP_REG 0x79u
#define INTEL_PCIE_NOSNOOP_MASK 0xf7u
#define PCI_SUBVENDOR_DELL 0x1028u
#define PCI_SUBVENDOR_HP 0x103Cu

#define MK_AUDIO_HDA_QRK_GPIO_UNMUTE_0 0x00000001u
#define MK_AUDIO_HDA_QRK_GPIO_UNMUTE_1 0x00000002u
#define MK_AUDIO_HDA_QRK_GPIO_UNMUTE_2 0x00000004u
#define MK_AUDIO_HDA_QRK_GPIO_UNMUTE_3 0x00000008u
#define MK_AUDIO_HDA_QRK_GPIO_POL_0    0x00000010u
#define MK_AUDIO_HDA_QRK_AD1981_OAMP   0x00000020u
#define MK_AUDIO_HDA_QRK_ROUTE_SPKR2_DAC 0x00000040u
#define MK_AUDIO_HDA_QRK_TPDOCK1       0x00000100u
#define MK_AUDIO_HDA_QRK_TPDOCK2       0x00000200u
#define MK_AUDIO_HDA_QRK_TPDOCK3       0x00000400u
#define MK_AUDIO_HDA_QRK_CLOSE_PCBEEP  0x00000800u

#define MK_AUDIO_HDA_SPKR_MUTE_NONE    0u
#define MK_AUDIO_HDA_SPKR_MUTE_PIN_AMP 1u
#define MK_AUDIO_HDA_SPKR_MUTE_PIN_CTL 2u
#define MK_AUDIO_HDA_SPKR_MUTE_DAC_AMP 3u

#define MK_AUDIO_STATUS_BACKEND_MASK 0x000000ffu
#define MK_AUDIO_STATUS_FLAG_IRQ_REGISTERED 0x00000100u
#define MK_AUDIO_STATUS_FLAG_IRQ_SEEN 0x00000200u
#define MK_AUDIO_AZALIA_PRESENCE_REFRESH_TICKS 8u
#define MK_AUDIO_AZALIA_PRESENCE_REFRESH_RUNNING_TICKS 64u
#define MK_AUDIO_STATUS_FLAG_NO_VALID_IRQ 0x00000400u
#define MK_AUDIO_STATUS_FLAG_STARVATION 0x00000800u
#define MK_AUDIO_STATUS_FLAG_UNDERRUN 0x00001000u
#define MK_AUDIO_STATUS_FLAG_CAPTURE_DATA 0x00002000u
#define MK_AUDIO_STATUS_FLAG_CAPTURE_XRUN 0x00004000u
#define MK_AUDIO_AZALIA_OUTPUT_PAGES \
    ((MK_AUDIO_SOFT_BUFFER_SIZE + PHYSMEM_PAGE_SIZE - 1u) / PHYSMEM_PAGE_SIZE)

enum mk_audio_backend_kind {
    MK_AUDIO_BACKEND_SOFT = 0,
    MK_AUDIO_BACKEND_COMPAT_AUICH = 1,
    MK_AUDIO_BACKEND_COMPAT_AZALIA = 2,
    MK_AUDIO_BACKEND_PCSPKR = 3,
    MK_AUDIO_BACKEND_COMPAT_UAUDIO = 4
};

struct mk_audio_backend_ops {
    int (*start)(void);
    int (*stop)(void);
    int (*write)(const uint8_t *data, uint32_t size);
};

struct mk_audio_auich_dmalist {
    uint32_t base;
    uint32_t len;
};

struct mk_audio_hda_rirb_entry {
    uint32_t response;
    uint32_t response_ex;
};

struct mk_audio_hda_bdl_entry {
    uint32_t low;
    uint32_t high;
    uint32_t length;
    uint32_t flags;
};

struct mk_audio_event_subscription {
    int pid;
    process_t *process;
    kernel_mailbox_t mailbox;
    struct mk_audio_event events[MK_AUDIO_EVENT_QUEUE_SIZE];
};

struct mk_audio_service_state {
    struct mk_audio_info info;
    enum mk_audio_backend_kind backend_kind;
    struct kernel_pci_device_info pci;
    uintptr_t compat_mix_base;
    uintptr_t compat_aud_base;
    uintptr_t azalia_base;
    uint16_t compat_caps;
    uint16_t compat_ext_audio_id;
    uint16_t azalia_gcap;
    uint16_t azalia_codec_mask;
    uint16_t azalia_corb_entries;
    uint16_t azalia_rirb_entries;
    uint16_t azalia_rirb_read_pos;
    uint16_t azalia_output_fmt;
    uint8_t compat_ready;
    uint8_t compat_codec_ready;
    uint8_t compat_irq_registered;
    uint8_t compat_mix_is_mmio;
    uint8_t compat_aud_is_mmio;
    uint8_t compat_ignore_codecready;
    uint8_t azalia_ready;
    uint8_t azalia_irq_registered;
    uint8_t azalia_vmaj;
    uint8_t azalia_vmin;
    uint8_t azalia_codec_address;
    uint8_t azalia_afg_nid;
    uint8_t azalia_codec_probed;
    uint8_t azalia_corb_ready;
    uint8_t azalia_widget_probed;
    uint8_t azalia_path_programmed;
    uint8_t azalia_output_dac_nid;
    uint8_t azalia_spkr_dac_nid;
    uint8_t azalia_output_pin_nid;
    uint8_t azalia_input_dac_nid;
    uint8_t azalia_input_pin_nid;
    uint8_t azalia_output_stream_index;
    uint8_t azalia_output_stream_number;
    uint8_t azalia_output_running;
    uint8_t azalia_output_pin_nids[4];
    uint8_t azalia_output_dac_nids[4];
    uint8_t azalia_input_pin_nids[2];
    uint8_t azalia_widget_powered[256];
    uint8_t azalia_speaker2_pin_nid;
    uint8_t azalia_speaker2_dac_nid;
    uint8_t azalia_fhp_pin_nid;
    uint8_t azalia_fhp_dac_nid;
    int8_t azalia_speaker2_priority;
    uint8_t azalia_widget_selected[256];
    uint8_t azalia_widget_selected_valid[256];
    uint8_t azalia_widget_disabled[256];
    int8_t azalia_output_priorities[4];
    uint8_t azalia_output_present_bits[4];
    uint8_t azalia_sense_pin_nids[4];
    uint8_t azalia_sense_pin_output_bits[4];
    uint8_t azalia_sense_pin_count;
    uint8_t azalia_spkr_muter_mask;
    uint8_t azalia_output_jack_count;
    uint8_t azalia_analog_dac_count;
    uint32_t azalia_output_config_defaults[4];
    uint32_t azalia_output_sort_keys[4];
    uint32_t azalia_speaker2_config_default;
    uint32_t azalia_presence_refresh_tick;
    uint8_t azalia_unsol_output_mask;
    uint8_t azalia_pin_policy_busy;
    uint8_t compat_output_running;
    uint8_t usb_audio_attach_ready;
    uint8_t usb_audio_attached_ready;
    uint8_t usb_audio_attach_attempted;
    uint8_t usb_audio_transport_kind;
    uint8_t usb_audio_interface_count;
    uint8_t usb_audio_endpoint_count;
    uint8_t usb_audio_configuration_value;
    uint8_t usb_audio_assigned_address;
    uint8_t usb_audio_control_interface_number;
    uint8_t usb_audio_streaming_interface_number;
    uint8_t usb_audio_streaming_interface_count;
    uint8_t usb_audio_streaming_alt_setting;
    uint8_t usb_audio_playback_channel_count;
    uint8_t usb_audio_playback_subframe_size;
    uint8_t usb_audio_playback_bit_resolution;
    uint8_t usb_audio_playback_endpoint_address;
    uint8_t usb_audio_playback_endpoint_attributes;
    uint16_t usb_audio_playback_endpoint_max_packet;
    uint32_t usb_audio_playback_sample_rate;
    uint8_t usb_audio_output_running;
    uint8_t usb_audio_stream_started;
    uint16_t usb_audio_staging_fill;
    uint8_t compat_output_producer;
    uint8_t compat_output_consumer;
    uint8_t compat_output_hw_civ;
    uint8_t compat_output_pending;
    uint8_t compat_input_running;
    uint8_t compat_input_producer;
    uint8_t compat_input_consumer;
    uint8_t compat_input_hw_civ;
    uint8_t compat_input_pending;
    uint8_t output_level;
    uint8_t input_level;
    uint8_t output_muted;
    uint8_t input_muted;
    uint8_t default_output;
    uint8_t default_input;
    uint8_t playback_buffer[MK_AUDIO_SOFT_BUFFER_SIZE];
    uint8_t capture_buffer[MK_AUDIO_SOFT_BUFFER_SIZE];
    uint8_t async_write_buffer[MK_AUDIO_ASYNC_WRITE_BUFFER_SIZE];
    uint32_t playback_head;
    uint32_t playback_tail;
    uint32_t playback_fill;
    uint32_t capture_head;
    uint32_t capture_tail;
    uint32_t capture_fill;
    uint32_t async_write_head;
    uint32_t async_write_tail;
    uint32_t async_write_fill;
    uint32_t playback_bytes_written;
    uint32_t playback_write_calls;
    uint32_t playback_bytes_consumed;
    uint32_t playback_xruns;
    uint32_t playback_starvations;
    uint32_t playback_underruns;
    uint32_t compat_irq_count;
    uint32_t azalia_irq_count;
    uint32_t azalia_vendor_id;
    uint32_t azalia_subsystem_id;
    uint32_t azalia_quirks;
    uint32_t azalia_fg_stream_formats;
    uint32_t azalia_fg_pcm;
    uint32_t azalia_fg_input_amp_cap;
    uint32_t azalia_fg_output_amp_cap;
    uint32_t azalia_output_mask;
    uint32_t azalia_input_mask;
    uint32_t azalia_output_regbase;
    uint32_t azalia_output_bytes;
    uint32_t azalia_output_blk;
    uint32_t azalia_output_swpos;
    uint32_t azalia_output_pos;
    uint32_t azalia_output_start_tick;
    uint32_t azalia_output_deadline_tick;
    uint32_t azalia_output_poll_tick;
    uint32_t status_snapshot_tick;
    uint32_t audio_event_last_queued_bytes;
    uint32_t audio_event_last_underruns;
    uint8_t audio_event_last_active;
    uint32_t *azalia_corb;
    struct mk_audio_hda_rirb_entry *azalia_rirb;
    struct mk_audio_hda_bdl_entry *azalia_bdl;
    struct mk_audio_hda_rirb_entry azalia_unsol_queue[HDA_UNSOL_QUEUE_SIZE];
    uint8_t azalia_unsol_rp;
    uint8_t azalia_unsol_wp;
    uint8_t azalia_unsol_kick;
    uint8_t *azalia_output_pages[MK_AUDIO_AZALIA_OUTPUT_PAGES];
    uint32_t usb_audio_output_bytes;
    uint32_t usb_audio_output_pos;
    uint32_t usb_audio_output_start_tick;
    uint32_t usb_audio_output_deadline_tick;
    uint32_t capture_bytes_captured;
    uint32_t capture_read_calls;
    uint32_t capture_bytes_read;
    uint32_t capture_xruns;
    uint8_t usb_audio_staging_buffer[MK_AUDIO_SOFT_BUFFER_SIZE];
    struct mk_audio_event_subscription event_subscribers[MK_AUDIO_EVENT_SUBSCRIBERS];
};

static const struct mk_audio_control_info g_audio_controls[] = {
    {MK_AUDIO_MIXER_OUTPUT_LEVEL, MK_AUDIO_CONTROL_LEVEL, MK_AUDIO_MIXER_OUTPUT_MUTE, 0u, AudioNoutput, AudioNvolume},
    {MK_AUDIO_MIXER_OUTPUT_MUTE, MK_AUDIO_CONTROL_TOGGLE, MK_AUDIO_MIXER_OUTPUT_LEVEL, 0u, AudioNoutput, AudioNmute},
    {MK_AUDIO_MIXER_INPUT_LEVEL, MK_AUDIO_CONTROL_LEVEL, MK_AUDIO_MIXER_INPUT_MUTE, 0u, AudioNinput, AudioNvolume},
    {MK_AUDIO_MIXER_INPUT_MUTE, MK_AUDIO_CONTROL_TOGGLE, MK_AUDIO_MIXER_INPUT_LEVEL, 0u, AudioNinput, AudioNmute},
    {MK_AUDIO_MIXER_OUTPUT_DEFAULT, MK_AUDIO_CONTROL_ENUM, MK_AUDIO_MIXER_OUTPUT_LEVEL, 0u, AudioNoutput, AudioNsource},
    {MK_AUDIO_MIXER_INPUT_DEFAULT, MK_AUDIO_CONTROL_ENUM, MK_AUDIO_MIXER_INPUT_LEVEL, 0u, AudioNinput, AudioNsource}
};

static struct mk_message g_last_audio_request;
static struct mk_message g_last_audio_reply;
static struct mk_audio_service_state g_audio_state;
static uint8_t g_audio_bootstrap_defer_probe = 0u;
static void mk_audio_event_init_subscribers(void);
static void mk_audio_publish_event(uint32_t event_type, uint32_t queued_bytes, uint32_t underruns);
static int mk_audio_current_underruns(void);
static int mk_audio_backend_soft_start(void);
static int mk_audio_backend_soft_stop(void);
static int mk_audio_backend_soft_write(const uint8_t *data, uint32_t size);
static int mk_audio_backend_pcspkr_start(void);
static int mk_audio_backend_pcspkr_stop(void);
static int mk_audio_backend_pcspkr_write(const uint8_t *data, uint32_t size);
static int mk_audio_backend_uaudio_start(void);
static int mk_audio_backend_uaudio_stop(void);
static int mk_audio_backend_uaudio_write(const uint8_t *data, uint32_t size);
static int mk_audio_backend_compat_start(void);
static int mk_audio_backend_compat_stop(void);
static int mk_audio_backend_compat_write(const uint8_t *data, uint32_t size);
static int mk_audio_backend_compat_read(uint8_t *data, uint32_t size);
static void mk_audio_compat_irq_handler(void);
static int mk_audio_backend_azalia_start(void);
static int mk_audio_backend_azalia_stop(void);
static int mk_audio_backend_azalia_write(const uint8_t *data, uint32_t size);
static void __attribute__((unused)) mk_audio_azalia_irq_handler(void);
static void mk_audio_azalia_set_irq_enabled(uint8_t enabled);
static int mk_audio_azalia_init_command_rings(void);
static uint16_t mk_audio_azalia_refresh_codec_mask(void);
static int mk_audio_azalia_command_raw(uint8_t codec, uint8_t nid, uint32_t verb_payload, uint32_t *response_out);
static int mk_audio_azalia_command(uint8_t codec, uint8_t nid, uint16_t verb, uint8_t payload, uint32_t *response_out);
static int mk_audio_azalia_command_retry(uint8_t codec,
                                         uint8_t nid,
                                         uint16_t verb,
                                         uint8_t payload,
                                         uint32_t *response_out,
                                         uint32_t attempts);
static int mk_audio_azalia_get_parameter_retry(uint8_t codec,
                                               uint8_t nid,
                                               uint8_t parameter,
                                               uint32_t *response_out,
                                               uint32_t attempts);
static void mk_audio_azalia_sync_fg_caps(void);
static int mk_audio_azalia_probe_codec(void);
static int mk_audio_azalia_probe_widgets(void);
static int mk_audio_azalia_reprobe_output_topology(void);
static int mk_audio_azalia_format_from_params(const struct audio_swpar *params, uint16_t *fmt_out);
static int mk_audio_azalia_stream_reset(void);
static int mk_audio_azalia_stream_prepare_fast(void);
static void mk_audio_azalia_finish_output_soft(void);
static void mk_audio_azalia_stream_halt(void);
static int mk_audio_azalia_stream_start_buffer(uint32_t bytes);
static void mk_audio_azalia_update_output_progress(void);
static int mk_audio_azalia_try_output_stream_candidate(uint8_t regindex, uint8_t stream_number);
static int mk_audio_azalia_select_output_stream_from(uint8_t start_regindex);
static int mk_audio_azalia_program_output_path(void);
static int mk_audio_azalia_try_program_output_candidate(uint8_t pin_nid,
                                                        uint8_t dac_nid,
                                                        int selected_bit,
                                                        uint32_t output_mask);
static int mk_audio_azalia_has_fatal_probe_failure(void);
static int mk_audio_azalia_rebind_output_stream(void);
static void mk_audio_debug_azalia_route(uint8_t pin_nid, uint8_t dac_nid, int selected_bit);
static uint8_t mk_audio_azalia_pin_present(uint8_t nid);
static void mk_audio_azalia_refresh_output_presence(void);
static int mk_audio_azalia_should_mute_speakers(int selected_bit);
static void mk_audio_azalia_power_widget(uint8_t nid);
static void mk_audio_azalia_enable_eapd(uint8_t nid, uint32_t pin_caps);
static uint8_t mk_audio_azalia_output_pin_ctl(int output_bit);
static void mk_audio_azalia_set_output_pin_enabled(uint8_t pin_nid,
                                                   int output_bit,
                                                   uint8_t enabled);
static void mk_audio_azalia_commit_output_routes(void);
static void mk_audio_azalia_apply_secondary_speaker_path(void);
static void mk_audio_azalia_disconnect_output_stream(void);
static void mk_audio_azalia_drain_rirb_irq(void);
static void mk_audio_azalia_queue_unsol_event(uint32_t response, uint32_t response_ex);
static void mk_audio_azalia_kick_unsol_events(void);
static int mk_audio_azalia_bind_dac_stream(uint8_t dac_nid, uint8_t channel);
static uint8_t mk_audio_azalia_speaker_mute_method(void);
static void mk_audio_azalia_apply_speaker_mute(uint8_t muted);
static int mk_audio_azalia_widget_has_effective_outamp(uint8_t nid, uint32_t caps);
static void mk_audio_azalia_program_widget_amp_state(uint8_t nid,
                                                     uint8_t input_amp,
                                                     uint8_t index,
                                                     uint8_t muted);
static void mk_audio_azalia_program_widget_amp(uint8_t nid, uint8_t input_amp, uint8_t index);
static void mk_audio_azalia_program_selector_defaults(uint8_t nid,
                                                      uint32_t connection_count,
                                                      uint32_t selected_index);
static void mk_audio_azalia_apply_output_pin_policy(int selected_bit);
static int mk_audio_azalia_query_widget_audio_caps(uint8_t nid,
                                                   uint32_t widget_caps,
                                                   uint32_t *encodings_out,
                                                   uint32_t *pcm_out);
static int mk_audio_azalia_get_selected_connection(uint8_t nid,
                                                   uint32_t connection_count,
                                                   uint32_t *selected_out);
static void mk_audio_azalia_cache_selected_connection(uint8_t nid,
                                                      uint32_t connection_count);
static int mk_audio_azalia_get_connections(uint8_t nid,
                                           uint8_t *connections,
                                           uint32_t max_connections,
                                           uint32_t *count_out);
static void mk_audio_azalia_repair_connection_select(uint8_t nid, uint32_t type);
static int mk_audio_azalia_find_connection_index_for_dac(uint8_t nid,
                                                         uint32_t type,
                                                         const uint8_t *connections,
                                                         uint32_t connection_count,
                                                         uint8_t target_dac,
                                                         uint32_t depth,
                                                         uint32_t *index_out);
static int mk_audio_azalia_retarget_pin_to_dac(uint8_t pin_nid, uint8_t target_dac);
static int mk_audio_azalia_resolve_output_path(uint8_t nid,
                                               uint8_t target_dac,
                                               uint32_t depth,
                                               uint8_t *path,
                                               uint8_t *path_indices,
                                               uint32_t max_path,
                                               uint32_t *path_len_out);
static int mk_audio_azalia_apply_output_path(const uint8_t *path,
                                             const uint8_t *path_indices,
                                             uint32_t path_len,
                                             uint8_t program_route,
                                             uint8_t power_widgets,
                                             uint8_t program_amps);
static int mk_audio_azalia_widget_enabled(uint8_t nid);
static int mk_audio_azalia_widget_check_connection(uint8_t nid, uint32_t depth);
static int mk_audio_azalia_widget_has_output_path(uint8_t nid, uint32_t depth);
static int mk_audio_azalia_find_output_dac(uint8_t nid, uint32_t depth);
static int mk_audio_azalia_find_alternate_output_dac(uint8_t pin_nid,
                                                     uint8_t current_dac,
                                                     const uint8_t *avoid_dacs,
                                                     uint32_t avoid_count,
                                                     uint8_t *dac_out);
static int mk_audio_output_bit_from_ord(uint32_t mask, uint8_t ord);
static int mk_audio_azalia_choose_output_path(uint8_t *pin_out, uint8_t *dac_out, int *selected_bit_out);
static int mk_audio_azalia_current_output_path_valid(void);
static int mk_audio_azalia_output_requires_presence(int bit);
static int mk_audio_azalia_output_is_speaker(int bit);
static int mk_audio_azalia_output_is_digital(int bit);
static int mk_audio_azalia_output_is_present_external(int bit);
static int mk_audio_azalia_have_present_external_output(void);
static uint32_t mk_audio_azalia_collect_primary_output_bits(uint8_t *bits_out, uint32_t max_bits);
static uint32_t mk_audio_azalia_collect_output_order(uint8_t *bits_out, uint32_t max_bits);
static int mk_audio_azalia_register_special_output_pin(uint8_t pin_nid,
                                                       uint8_t dac_nid,
                                                       int priority,
                                                       uint32_t config_default);
static int mk_audio_azalia_register_output_path(uint32_t output_mask,
                                                uint8_t pin_nid,
                                                uint8_t dac_nid,
                                                int priority,
                                                uint32_t config_default);
static void mk_audio_azalia_register_secondary_speaker(uint8_t pin_nid,
                                                       uint8_t dac_nid,
                                                       int priority,
                                                       uint32_t config_default);
static void mk_audio_azalia_rebalance_output_dacs(void);
static void mk_audio_azalia_select_primary_output_dacs(void);
static void mk_audio_azalia_select_speaker_dac(void);
static void mk_audio_azalia_detect_quirks(void);
static void mk_audio_azalia_apply_gpio_quirks(void);
static void mk_audio_azalia_apply_processing_quirks(void);
static void mk_audio_azalia_apply_widget_quirks(uint8_t nid, uint32_t *config_default);
static void mk_audio_azalia_register_input_path(uint32_t input_mask, uint8_t pin_nid);
static uint32_t mk_audio_hda_config_device(uint32_t config_default);
static uint32_t mk_audio_hda_config_port(uint32_t config_default);
static uint32_t mk_audio_hda_config_misc(uint32_t config_default);
static uint32_t mk_audio_hda_config_location(uint32_t config_default);
static uint32_t mk_audio_hda_config_association(uint32_t config_default);
static uint32_t mk_audio_hda_config_sequence(uint32_t config_default);
static uint32_t mk_audio_hda_output_sort_key(uint32_t config_default);
static uint32_t mk_audio_hda_output_mask(uint32_t pin_caps, uint32_t config_default);
static uint32_t mk_audio_hda_input_mask(uint32_t pin_caps, uint32_t config_default);
static int mk_audio_hda_output_priority(uint32_t pin_caps, uint32_t config_default);
static void mk_audio_normalize_params(struct audio_swpar *params);
static void mk_audio_normalize_uaudio_params(struct audio_swpar *params);
static void mk_audio_state_reset_async_write(void);
static uint32_t mk_audio_async_ring_write(const uint8_t *data, uint32_t size);
static uint32_t mk_audio_async_ring_peek(uint8_t *data, uint32_t size);
static void mk_audio_async_ring_consume(uint32_t size);
static uint32_t mk_audio_async_pump_chunk_limit(void);
static void mk_audio_service_tick(uint32_t tick);
static uint32_t mk_audio_uaudio_frame_bytes(void);
static void mk_audio_reset_uaudio_runtime(void);
static void mk_audio_uaudio_update_output_progress(void);
static uint32_t mk_audio_uaudio_pending_bytes(void);
static void mk_audio_uaudio_note_queued_output(uint32_t written);
static int mk_audio_uaudio_flush_staging(uint8_t drain_all);
static void mk_audio_build_usb_audio_reason(char *dst,
                                            size_t dst_size,
                                            const char *prefix,
                                            uint8_t transport_kind,
                                            const char *suffix);
static int mk_audio_backend_current_is_usable(void);
static void mk_audio_maybe_promote_uaudio_backend(void);
static void mk_audio_select_uaudio_backend(void);
static void mk_audio_set_uaudio_identity(const char *suffix);
static void mk_audio_failover_from_unusable_uaudio(void);
static void mk_audio_failover_from_unusable_compat(void);
static void mk_audio_compat_sync_codec_caps(void);
static void mk_audio_compat_apply_device_quirks(void);
static uint32_t mk_audio_output_presence_mask(void);
static void mk_audio_refresh_topology_snapshot(void);
static void mk_audio_compat_update_input_progress(void);
static void mk_audio_capture_ring_write(const uint8_t *data, uint32_t size);
static int mk_audio_capture_ring_read(uint8_t *data, uint32_t size);
static void mk_audio_capture_note_xrun(void);
static int mk_audio_compat_capture_block(uint32_t requested_bytes);
static uint32_t mk_audio_estimated_playback_ticks(const struct audio_swpar *params, uint32_t data_size);
static void mk_audio_event_init_subscribers(void);
static void mk_audio_publish_event(uint32_t event_type, uint32_t queued_bytes, uint32_t underruns);
static int mk_audio_current_underruns(void);
static void mk_audio_debug_backend_state(const char *prefix);
static void mk_audio_debug_append_text(char *dst, size_t dst_size, const char *src);
static void mk_audio_debug_append_hex(char *dst, size_t dst_size, uint32_t value, unsigned digits);
static void mk_audio_debug_append_uint(char *dst, size_t dst_size, uint32_t value);
static const struct mk_audio_backend_ops g_audio_backend_soft = {
    mk_audio_backend_soft_start,
    mk_audio_backend_soft_stop,
    mk_audio_backend_soft_write
};
static const struct mk_audio_backend_ops g_audio_backend_pcspkr = {
    mk_audio_backend_pcspkr_start,
    mk_audio_backend_pcspkr_stop,
    mk_audio_backend_pcspkr_write
};
static const struct mk_audio_backend_ops g_audio_backend_uaudio = {
    mk_audio_backend_uaudio_start,
    mk_audio_backend_uaudio_stop,
    mk_audio_backend_uaudio_write
};
static const struct mk_audio_backend_ops __attribute__((unused)) g_audio_backend_compat = {
    mk_audio_backend_compat_start,
    mk_audio_backend_compat_stop,
    mk_audio_backend_compat_write
};
static const struct mk_audio_backend_ops __attribute__((unused)) g_audio_backend_azalia = {
    mk_audio_backend_azalia_start,
    mk_audio_backend_azalia_stop,
    mk_audio_backend_azalia_write
};

static void mk_audio_event_init_subscribers(void) {
    uint32_t index;

    g_audio_state.audio_event_last_queued_bytes = 0u;
    g_audio_state.audio_event_last_underruns = 0u;
    g_audio_state.audio_event_last_active = 0u;
    for (index = 0; index < MK_AUDIO_EVENT_SUBSCRIBERS; ++index) {
        struct mk_audio_event_subscription *subscription = &g_audio_state.event_subscribers[index];

        memset(subscription, 0, sizeof(*subscription));
        kernel_mailbox_init(&subscription->mailbox,
                            subscription->events,
                            sizeof(subscription->events[0]),
                            MK_AUDIO_EVENT_QUEUE_SIZE,
                            KERNEL_MAILBOX_DROP_NEWEST,
                            TASK_WAIT_CLASS_AUDIO,
                            MK_SERVICE_AUDIO);
    }
}

static struct mk_audio_event_subscription *mk_audio_find_subscription(const process_t *subscriber) {
    uint32_t index;

    if (subscriber == 0 || subscriber->pid <= 0) {
        return 0;
    }

    for (index = 0; index < MK_AUDIO_EVENT_SUBSCRIBERS; ++index) {
        struct mk_audio_event_subscription *subscription = &g_audio_state.event_subscribers[index];

        if (subscription->pid == subscriber->pid && subscription->process == subscriber) {
            return subscription;
        }
    }

    return 0;
}

static struct mk_audio_event_subscription *mk_audio_alloc_subscription(process_t *subscriber) {
    uint32_t index;

    if (subscriber == 0 || subscriber->pid <= 0) {
        return 0;
    }

    for (index = 0; index < MK_AUDIO_EVENT_SUBSCRIBERS; ++index) {
        struct mk_audio_event_subscription *subscription = &g_audio_state.event_subscribers[index];

        if (subscription->pid <= 0 || subscription->process == 0 ||
            scheduler_find_task_by_pid(subscription->pid) == 0) {
            memset(subscription, 0, sizeof(*subscription));
            subscription->pid = subscriber->pid;
            subscription->process = subscriber;
            kernel_mailbox_init(&subscription->mailbox,
                                subscription->events,
                                sizeof(subscription->events[0]),
                                MK_AUDIO_EVENT_QUEUE_SIZE,
                                KERNEL_MAILBOX_DROP_NEWEST,
                                TASK_WAIT_CLASS_AUDIO,
                                MK_SERVICE_AUDIO);
            return subscription;
        }
    }

    return 0;
}

static void mk_audio_enqueue_event(struct mk_audio_event_subscription *subscription,
                                   uint32_t event_type,
                                   uint32_t queued_bytes,
                                   uint32_t underruns) {
    struct mk_audio_event event;

    if (subscription == 0 || event_type == MK_AUDIO_EVENT_NONE) {
        return;
    }

    memset(&event, 0, sizeof(event));
    event.abi_version = 1u;
    event.event_type = event_type;
    event.backend_kind = g_audio_state.backend_kind;
    event.queued_bytes = queued_bytes;
    event.underruns = underruns;
    event.dropped_events = kernel_mailbox_dropped(&subscription->mailbox);
    event.tick = kernel_timer_get_ticks();
    if (kernel_mailbox_try_send(&subscription->mailbox, &event) == 0) {
        kernel_mailbox_clear_dropped(&subscription->mailbox);
    }
}

static void mk_audio_publish_event(uint32_t event_type, uint32_t queued_bytes, uint32_t underruns) {
    uint32_t index;

    if (event_type == MK_AUDIO_EVENT_NONE) {
        return;
    }

    for (index = 0; index < MK_AUDIO_EVENT_SUBSCRIBERS; ++index) {
        struct mk_audio_event_subscription *subscription = &g_audio_state.event_subscribers[index];

        if (subscription->pid <= 0 || subscription->process == 0) {
            continue;
        }
        if (scheduler_find_task_by_pid(subscription->pid) == 0) {
            memset(subscription, 0, sizeof(*subscription));
            continue;
        }
        mk_audio_enqueue_event(subscription, event_type, queued_bytes, underruns);
    }
}

static void mk_audio_capture_note_xrun(void) {
    g_audio_state.capture_xruns++;
    mk_audio_publish_event(MK_AUDIO_EVENT_CAPTURE_XRUN,
                           g_audio_state.capture_fill,
                           g_audio_state.capture_xruns);
}

static int mk_audio_current_underruns(void) {
    int underruns = (int)g_audio_state.playback_underruns;

    if ((int)g_audio_state.playback_xruns > underruns) {
        underruns = (int)g_audio_state.playback_xruns;
    }
    return underruns;
}
static const struct mk_audio_backend_ops *g_audio_backend = &g_audio_backend_soft;
static struct mk_audio_auich_dmalist g_audio_auich_pcmo_dmalist[AUICH_DMALIST_MAX]
    __attribute__((aligned(8)));
static uint8_t g_audio_auich_pcmo_buffers[AUICH_DMALIST_MAX][AUICH_DMA_SLOT_SIZE]
    __attribute__((aligned(2)));
static uint16_t g_audio_auich_pcmo_bytes[AUICH_DMALIST_MAX];
static struct mk_audio_auich_dmalist g_audio_auich_pcmi_dmalist[AUICH_DMALIST_MAX]
    __attribute__((aligned(8)));
static uint8_t g_audio_auich_pcmi_buffers[AUICH_DMALIST_MAX][AUICH_DMA_SLOT_SIZE]
    __attribute__((aligned(2)));
static uint16_t g_audio_auich_pcmi_bytes[AUICH_DMALIST_MAX];
static uint8_t mk_audio_compat_read8(uintptr_t base, uint8_t mmio, uint16_t reg);
static uint16_t mk_audio_compat_read16(uintptr_t base, uint8_t mmio, uint16_t reg);
static uint32_t mk_audio_compat_read32(uintptr_t base, uint8_t mmio, uint16_t reg);
static void mk_audio_compat_write8(uintptr_t base, uint8_t mmio, uint16_t reg, uint8_t value);
static void mk_audio_compat_write16(uintptr_t base, uint8_t mmio, uint16_t reg, uint16_t value);
static void mk_audio_compat_write32(uintptr_t base, uint8_t mmio, uint16_t reg, uint32_t value);
static uint8_t mk_audio_azalia_read8(uintptr_t base, uint16_t reg);
static uint16_t mk_audio_azalia_read16(uintptr_t base, uint16_t reg);
static uint32_t mk_audio_azalia_read32(uintptr_t base, uint16_t reg);
static void mk_audio_azalia_write8(uintptr_t base, uint16_t reg, uint8_t value);
static void mk_audio_azalia_write16(uintptr_t base, uint16_t reg, uint16_t value);
static void mk_audio_azalia_write32(uintptr_t base, uint16_t reg, uint32_t value);

static void mk_audio_compat_debug_pcmi(const char *tag) {
    uint8_t civ;
    uint8_t lvi;
    uint8_t piv;
    uint8_t ctrl;
    uint16_t sts;
    uint16_t picb;

    if (g_audio_state.compat_aud_base == 0u) {
        return;
    }

    civ = mk_audio_compat_read8(g_audio_state.compat_aud_base, g_audio_state.compat_aud_is_mmio, AUICH_PCMI + AUICH_CIV) & AUICH_LVI_MASK;
    lvi = mk_audio_compat_read8(g_audio_state.compat_aud_base, g_audio_state.compat_aud_is_mmio, AUICH_PCMI + AUICH_LVI) & AUICH_LVI_MASK;
    piv = mk_audio_compat_read8(g_audio_state.compat_aud_base, g_audio_state.compat_aud_is_mmio, AUICH_PCMI + AUICH_PIV) & AUICH_LVI_MASK;
    ctrl = mk_audio_compat_read8(g_audio_state.compat_aud_base, g_audio_state.compat_aud_is_mmio, AUICH_PCMI + AUICH_CTRL);
    sts = mk_audio_compat_read16(g_audio_state.compat_aud_base, g_audio_state.compat_aud_is_mmio, AUICH_PCMI + AUICH_STS);
    picb = mk_audio_compat_read16(g_audio_state.compat_aud_base, g_audio_state.compat_aud_is_mmio, AUICH_PCMI + AUICH_PICB);
    kernel_debug_printf("audio: %s pcmi civ=%d lvi=%d piv=%d picb=%d sts=%x ctrl=%x run=%d pending=%d capfill=%d\n",
                        tag,
                        (int)civ,
                        (int)lvi,
                        (int)piv,
                        (int)picb,
                        (unsigned int)sts,
                        (unsigned int)ctrl,
                        (int)g_audio_state.compat_input_running,
                        (int)g_audio_state.compat_input_pending,
                        (int)g_audio_state.capture_fill);
}

static void mk_audio_compat_delay(void) {
    for (uint32_t i = 0u; i < 4u; ++i) {
        io_wait();
    }
}

static int mk_audio_azalia_alloc_dma_buffers(void) {
    if (g_audio_state.azalia_corb == 0) {
        g_audio_state.azalia_corb = (uint32_t *)alloc_phys_page();
    }
    if (g_audio_state.azalia_rirb == 0) {
        g_audio_state.azalia_rirb = (struct mk_audio_hda_rirb_entry *)alloc_phys_page();
    }
    if (g_audio_state.azalia_bdl == 0) {
        g_audio_state.azalia_bdl = (struct mk_audio_hda_bdl_entry *)alloc_phys_page();
    }
    for (uint32_t i = 0u; i < MK_AUDIO_AZALIA_OUTPUT_PAGES; ++i) {
        if (g_audio_state.azalia_output_pages[i] == 0) {
            g_audio_state.azalia_output_pages[i] = (uint8_t *)alloc_phys_page();
        }
    }
    if (g_audio_state.azalia_corb == 0 ||
        g_audio_state.azalia_rirb == 0 ||
        g_audio_state.azalia_bdl == 0) {
        return -1;
    }
    for (uint32_t i = 0u; i < MK_AUDIO_AZALIA_OUTPUT_PAGES; ++i) {
        if (g_audio_state.azalia_output_pages[i] == 0) {
            return -1;
        }
    }
    if ((((uintptr_t)g_audio_state.azalia_corb) & 127u) != 0u ||
        (((uintptr_t)g_audio_state.azalia_rirb) & 127u) != 0u ||
        (((uintptr_t)g_audio_state.azalia_bdl) & 127u) != 0u) {
        return -1;
    }
    return 0;
}

static uint8_t *mk_audio_azalia_output_ptr(uint32_t offset) {
    uint32_t page_index;
    uint32_t page_offset;

    if (offset >= MK_AUDIO_SOFT_BUFFER_SIZE) {
        return 0;
    }
    page_index = offset / PHYSMEM_PAGE_SIZE;
    if (page_index >= MK_AUDIO_AZALIA_OUTPUT_PAGES) {
        return 0;
    }
    page_offset = offset % PHYSMEM_PAGE_SIZE;
    if (g_audio_state.azalia_output_pages[page_index] == 0) {
        return 0;
    }
    return g_audio_state.azalia_output_pages[page_index] + page_offset;
}

static void mk_audio_azalia_copy_output_buffer(const uint8_t *data, uint32_t bytes) {
    uint32_t offset = 0u;

    while (offset < bytes) {
        uint32_t page_offset = offset % PHYSMEM_PAGE_SIZE;
        uint32_t chunk = PHYSMEM_PAGE_SIZE - page_offset;
        uint8_t *dst = mk_audio_azalia_output_ptr(offset);

        if (chunk > (bytes - offset)) {
            chunk = bytes - offset;
        }
        if (dst == 0) {
            break;
        }
        memcpy(dst, data + offset, chunk);
        offset += chunk;
    }
}

static void mk_audio_cooperative_delay(uint32_t iteration) {
    mk_audio_compat_delay();
    if ((iteration & 0x3fu) == 0x3fu) {
        yield();
    }
}

static void mk_audio_azalia_stream_delay(uint32_t iteration) {
    mk_audio_compat_delay();
    if ((iteration & 0x3ffu) == 0x3ffu) {
        yield();
    }
}

static void mk_audio_azalia_busy_delay(uint32_t count) {
    for (uint32_t i = 0u; i < count; ++i) {
        mk_audio_compat_delay();
    }
}

static int mk_audio_azalia_wait32(uint16_t reg, uint32_t mask, uint32_t value, uint32_t timeout) {
    for (uint32_t i = 0u; i < timeout; ++i) {
        if ((mk_audio_azalia_read32(g_audio_state.azalia_base, reg) & mask) == value) {
            return 0;
        }
        mk_audio_azalia_stream_delay(i);
    }
    return -1;
}

static int mk_audio_azalia_wait16(uint16_t reg, uint16_t mask, uint16_t value, uint32_t timeout) {
    for (uint32_t i = 0u; i < timeout; ++i) {
        if ((mk_audio_azalia_read16(g_audio_state.azalia_base, reg) & mask) == value) {
            return 0;
        }
        mk_audio_azalia_stream_delay(i);
    }
    return -1;
}

static int mk_audio_azalia_wait8(uint16_t reg, uint8_t mask, uint8_t value, uint32_t timeout) {
    for (uint32_t i = 0u; i < timeout; ++i) {
        if ((mk_audio_azalia_read8(g_audio_state.azalia_base, reg) & mask) == value) {
            return 0;
        }
        mk_audio_azalia_stream_delay(i);
    }
    return -1;
}

static uint8_t mk_audio_compat_read8(uintptr_t base, uint8_t mmio, uint16_t reg) {
    if (mmio) {
        volatile uint8_t *ptr = (volatile uint8_t *)(base + (uintptr_t)reg);
        return *ptr;
    }
    return inb((uint16_t)(base + (uintptr_t)reg));
}

static uint16_t mk_audio_compat_read16(uintptr_t base, uint8_t mmio, uint16_t reg) {
    if (mmio) {
        volatile uint16_t *ptr = (volatile uint16_t *)(base + (uintptr_t)reg);
        return *ptr;
    }
    return inw((uint16_t)(base + (uintptr_t)reg));
}

static uint32_t mk_audio_compat_read32(uintptr_t base, uint8_t mmio, uint16_t reg) {
    if (mmio) {
        volatile uint32_t *ptr = (volatile uint32_t *)(base + (uintptr_t)reg);
        return *ptr;
    }
    return inl((uint16_t)(base + (uintptr_t)reg));
}

static void mk_audio_compat_write8(uintptr_t base, uint8_t mmio, uint16_t reg, uint8_t value) {
    if (mmio) {
        volatile uint8_t *ptr = (volatile uint8_t *)(base + (uintptr_t)reg);
        *ptr = value;
        return;
    }
    outb((uint16_t)(base + (uintptr_t)reg), value);
}

static void mk_audio_compat_write16(uintptr_t base, uint8_t mmio, uint16_t reg, uint16_t value) {
    if (mmio) {
        volatile uint16_t *ptr = (volatile uint16_t *)(base + (uintptr_t)reg);
        *ptr = value;
        return;
    }
    outw((uint16_t)(base + (uintptr_t)reg), value);
}

static void mk_audio_compat_write32(uintptr_t base, uint8_t mmio, uint16_t reg, uint32_t value) {
    if (mmio) {
        volatile uint32_t *ptr = (volatile uint32_t *)(base + (uintptr_t)reg);
        *ptr = value;
        return;
    }
    outl((uint16_t)(base + (uintptr_t)reg), value);
}

static uint8_t mk_audio_azalia_read8(uintptr_t base, uint16_t reg) {
    volatile uint8_t *ptr = (volatile uint8_t *)(base + (uintptr_t)reg);
    return *ptr;
}

static uint16_t mk_audio_azalia_read16(uintptr_t base, uint16_t reg) {
    volatile uint16_t *ptr = (volatile uint16_t *)(base + (uintptr_t)reg);
    return *ptr;
}

static uint32_t mk_audio_azalia_read32(uintptr_t base, uint16_t reg) {
    volatile uint32_t *ptr = (volatile uint32_t *)(base + (uintptr_t)reg);
    return *ptr;
}

static void mk_audio_azalia_write8(uintptr_t base, uint16_t reg, uint8_t value) {
    volatile uint8_t *ptr = (volatile uint8_t *)(base + (uintptr_t)reg);
    *ptr = value;
}

static void mk_audio_azalia_write16(uintptr_t base, uint16_t reg, uint16_t value) {
    volatile uint16_t *ptr = (volatile uint16_t *)(base + (uintptr_t)reg);
    *ptr = value;
}

static void mk_audio_azalia_write32(uintptr_t base, uint16_t reg, uint32_t value) {
    volatile uint32_t *ptr = (volatile uint32_t *)(base + (uintptr_t)reg);
    *ptr = value;
}

static uint32_t mk_audio_current_pid(void) {
    return scheduler_current_pid();
}

static int mk_audio_current_process_is_service_worker(void) {
    process_t *current = scheduler_current();

    return current != 0 && current->service_type == MK_SERVICE_AUDIO;
}

static int mk_audio_current_prefers_kernel_hot_path(void) {
    process_t *current = scheduler_current();
    const struct mk_launch_context *context;

    if (current == 0) {
        return 0;
    }
    if (current->service_type == MK_SERVICE_AUDIO) {
        return 1;
    }

    context = mk_launch_context_current();
    if (context == 0) {
        return 1;
    }

    if ((context->flags & (MK_LAUNCH_FLAG_BOOTSTRAP |
                           MK_LAUNCH_FLAG_CRITICAL |
                           MK_LAUNCH_FLAG_USER_DESKTOP)) != 0u) {
        return 1;
    }

    return context->task_class == MK_TASK_CLASS_AUDIO_IO ||
           context->task_class == MK_TASK_CLASS_DESKTOP ||
           context->task_class == MK_TASK_CLASS_APP_RUNTIME;
}

static int mk_audio_prepare_request(struct mk_message *message,
                                    uint32_t type,
                                    const void *payload,
                                    size_t payload_size) {
    const struct mk_service_record *service;

    if (message == 0) {
        return -1;
    }

    service = mk_service_find_by_type(MK_SERVICE_AUDIO);
    if (service == 0) {
        return -1;
    }

    mk_message_init(message, type);
    message->source_pid = mk_audio_current_pid();
    message->target_pid = service->pid > 0 ? (uint32_t)service->pid : 0u;
    return mk_message_set_payload(message, payload, payload_size);
}

static int mk_audio_reply_result(struct mk_message *reply, int value) {
    struct mk_audio_result payload;

    payload.value = value;
    return mk_message_set_payload(reply, &payload, sizeof(payload));
}

static void mk_audio_copy_limited(char *dst, const char *src, size_t dst_size) {
    if (dst == 0 || dst_size == 0u) {
        return;
    }

    if (src == 0) {
        dst[0] = '\0';
        return;
    }

    strncpy(dst, src, dst_size - 1u);
    dst[dst_size - 1u] = '\0';
}

static void mk_audio_debug_backend_state(const char *prefix) {
    char line[192];
    const char *backend = g_audio_state.info.device.version;
    const char *config = g_audio_state.info.device.config;

    if (prefix == 0) {
        return;
    }

    line[0] = '\0';
    mk_audio_debug_append_text(line, sizeof(line), prefix);
    mk_audio_debug_append_text(line,
                               sizeof(line),
                               backend != 0 && backend[0] != '\0' ? backend : "unknown");
    mk_audio_debug_append_text(line, sizeof(line), " config=");
    mk_audio_debug_append_text(line,
                               sizeof(line),
                               config != 0 && config[0] != '\0' ? config : "unknown");
    mk_audio_debug_append_text(line, sizeof(line), "\n");
    kernel_debug_puts(line);
}

static void mk_audio_debug_append_text(char *dst, size_t dst_size, const char *src) {
    size_t len;

    if (dst == 0 || dst_size == 0u || src == 0) {
        return;
    }
    len = strlen(dst);
    if (len >= dst_size - 1u) {
        return;
    }
    strncpy(dst + len, src, dst_size - len - 1u);
    dst[dst_size - 1u] = '\0';
}

static void mk_audio_debug_append_hex(char *dst, size_t dst_size, uint32_t value, unsigned digits) {
    static const char hex_digits[] = "0123456789abcdef";
    char text[9];
    unsigned count = digits > 8u ? 8u : digits;

    if (dst == 0 || dst_size == 0u || count == 0u) {
        return;
    }
    for (unsigned i = 0u; i < count; ++i) {
        unsigned shift = (count - 1u - i) * 4u;
        text[i] = hex_digits[(value >> shift) & 0x0fu];
    }
    text[count] = '\0';
    mk_audio_debug_append_text(dst, dst_size, text);
}

static void mk_audio_debug_append_uint(char *dst, size_t dst_size, uint32_t value) {
    char text[12];
    size_t len = 0u;

    if (dst == 0 || dst_size == 0u) {
        return;
    }
    if (value == 0u) {
        mk_audio_debug_append_text(dst, dst_size, "0");
        return;
    }
    while (value != 0u && len < sizeof(text) - 1u) {
        text[len++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (len > 0u) {
        char ch[2];

        ch[0] = text[--len];
        ch[1] = '\0';
        mk_audio_debug_append_text(dst, dst_size, ch);
    }
}

static void mk_audio_debug_azalia_widget(uint8_t nid,
                                         uint32_t type,
                                         uint32_t caps,
                                         uint32_t pin_caps,
                                         uint32_t config_default,
                                         int candidate_dac) {
    char line[160];

    line[0] = '\0';
    mk_audio_debug_append_text(line, sizeof(line), "audio: hda widget nid=");
    mk_audio_debug_append_uint(line, sizeof(line), nid);
    mk_audio_debug_append_text(line, sizeof(line), " type=");
    mk_audio_debug_append_uint(line, sizeof(line), type);
    mk_audio_debug_append_text(line, sizeof(line), " caps=0x");
    mk_audio_debug_append_hex(line, sizeof(line), caps, 8u);
    if (pin_caps != 0u) {
        mk_audio_debug_append_text(line, sizeof(line), " pin=0x");
        mk_audio_debug_append_hex(line, sizeof(line), pin_caps, 8u);
        mk_audio_debug_append_text(line, sizeof(line), " cfg=0x");
        mk_audio_debug_append_hex(line, sizeof(line), config_default, 8u);
    }
    if (candidate_dac >= 0) {
        mk_audio_debug_append_text(line, sizeof(line), " dac=");
        mk_audio_debug_append_uint(line, sizeof(line), (uint32_t)candidate_dac);
    }
    mk_audio_debug_append_text(line, sizeof(line), "\n");
    kernel_debug_puts(line);
}

static void mk_audio_debug_azalia_route(uint8_t pin_nid, uint8_t dac_nid, int selected_bit) {
    char line[96];

    line[0] = '\0';
    mk_audio_debug_append_text(line, sizeof(line), "audio: hda route out=");
    if (selected_bit >= 0) {
        mk_audio_debug_append_uint(line, sizeof(line), (uint32_t)selected_bit);
    } else {
        mk_audio_debug_append_text(line, sizeof(line), "fallback");
    }
    mk_audio_debug_append_text(line, sizeof(line), " pin=");
    mk_audio_debug_append_uint(line, sizeof(line), pin_nid);
    mk_audio_debug_append_text(line, sizeof(line), " dac=");
    mk_audio_debug_append_uint(line, sizeof(line), dac_nid);
    mk_audio_debug_append_text(line, sizeof(line), "\n");
    kernel_debug_puts(line);
}

static uint8_t mk_audio_azalia_pin_present(uint8_t nid) {
    uint32_t response = 0u;

    if (nid == 0u || !g_audio_state.azalia_codec_probed) {
        return 0u;
    }
    (void)mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                  nid,
                                  HDA_VERB_EXECUTE_PIN_SENSE,
                                  0u,
                                  &response);
    if (mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                nid,
                                HDA_VERB_GET_PIN_SENSE,
                                0u,
                                &response) != 0) {
        return 0u;
    }
    return (response & HDA_PIN_SENSE_PRESENCE) != 0u ? 1u : 0u;
}

static void mk_audio_azalia_refresh_output_presence(void) {
    g_audio_state.azalia_presence_refresh_tick = kernel_timer_get_ticks();
    for (int bit = 0; bit < 4; ++bit) {
        uint8_t pin_nid = g_audio_state.azalia_output_pin_nids[bit];
        uint32_t config_default = g_audio_state.azalia_output_config_defaults[bit];
        uint32_t misc = (config_default >> HDA_CONFIG_MISC_SHIFT) & HDA_CONFIG_MISC_MASK;
        uint32_t port = mk_audio_hda_config_port(config_default);

        if (pin_nid == 0u || g_audio_state.azalia_output_dac_nids[bit] == 0u) {
            g_audio_state.azalia_output_present_bits[bit] = 0u;
            continue;
        }
        if ((misc & HDA_CONFIG_MISC_PRESENCEOV) != 0u) {
            g_audio_state.azalia_output_present_bits[bit] = 0u;
            continue;
        }
        if (port == HDA_CONFIG_PORT_JACK || port == HDA_CONFIG_PORT_BOTH) {
            g_audio_state.azalia_output_present_bits[bit] = mk_audio_azalia_pin_present(pin_nid);
        }
    }
}

static uint32_t mk_audio_azalia_presence_refresh_interval(void) {
    if (g_audio_state.azalia_output_running) {
        return MK_AUDIO_AZALIA_PRESENCE_REFRESH_RUNNING_TICKS;
    }
    return MK_AUDIO_AZALIA_PRESENCE_REFRESH_TICKS;
}

static uint8_t mk_audio_azalia_output_unsol_tag(int bit) {
    if (bit < 0 || bit >= 4) {
        return 0u;
    }
    return (uint8_t)(1u + (uint8_t)bit);
}

static int mk_audio_azalia_unsol_tag_to_output_bit(uint8_t tag) {
    if (tag >= 1u && tag <= 4u) {
        return (int)(tag - 1u);
    }
    return -1;
}

static int mk_audio_azalia_current_output_selected_bit(void) {
    uint8_t pin_nid = g_audio_state.azalia_output_pin_nid;
    uint8_t dac_nid = g_audio_state.azalia_output_dac_nid;

    for (int bit = 0; bit < 4; ++bit) {
        if (g_audio_state.azalia_output_dac_nids[bit] != dac_nid) {
            continue;
        }
        if (pin_nid == 0u || g_audio_state.azalia_output_pin_nids[bit] == pin_nid) {
            return bit;
        }
    }
    return -1;
}

static void mk_audio_azalia_sync_speaker_mute_policy(void) {
    int selected_bit;

    if (g_audio_state.azalia_pin_policy_busy != 0u) {
        return;
    }
    mk_audio_azalia_refresh_output_presence();
    selected_bit = mk_audio_azalia_current_output_selected_bit();
    if (selected_bit < 0 && g_audio_state.azalia_fhp_pin_nid != 0u) {
        for (int bit = 0; bit < 4; ++bit) {
            if (g_audio_state.azalia_output_pin_nids[bit] == g_audio_state.azalia_fhp_pin_nid) {
                selected_bit = bit;
                break;
            }
        }
    }
    mk_audio_azalia_apply_output_pin_policy(selected_bit);
}

static void mk_audio_azalia_note_unsol_event(uint8_t tag) {
    int bit = mk_audio_azalia_unsol_tag_to_output_bit((uint8_t)(tag & HDA_UNSOL_TAG_MASK));

    if (bit < 0 || bit >= 4) {
        return;
    }
    if ((g_audio_state.azalia_unsol_output_mask & (uint8_t)(1u << bit)) == 0u) {
        return;
    }
    if (g_audio_state.azalia_path_programmed != 0u &&
        mk_audio_azalia_current_output_path_valid()) {
        mk_audio_azalia_sync_speaker_mute_policy();
        return;
    }
    g_audio_state.azalia_presence_refresh_tick = 0u;
    if (g_audio_state.azalia_path_programmed != 0u) {
        g_audio_state.azalia_path_programmed = 0u;
    }
}

static void mk_audio_azalia_queue_unsol_event(uint32_t response, uint32_t response_ex) {
    uint8_t next_wp;

    next_wp = (uint8_t)((g_audio_state.azalia_unsol_wp + 1u) % HDA_UNSOL_QUEUE_SIZE);
    if (next_wp == g_audio_state.azalia_unsol_rp) {
        g_audio_state.azalia_unsol_rp =
            (uint8_t)((g_audio_state.azalia_unsol_rp + 1u) % HDA_UNSOL_QUEUE_SIZE);
    }
    g_audio_state.azalia_unsol_queue[g_audio_state.azalia_unsol_wp].response = response;
    g_audio_state.azalia_unsol_queue[g_audio_state.azalia_unsol_wp].response_ex = response_ex;
    g_audio_state.azalia_unsol_wp = next_wp;
}

static void mk_audio_azalia_kick_unsol_events(void) {
    if (g_audio_state.azalia_unsol_kick != 0u) {
        return;
    }

    g_audio_state.azalia_unsol_kick = 1u;
    while (g_audio_state.azalia_unsol_rp != g_audio_state.azalia_unsol_wp) {
        struct mk_audio_hda_rirb_entry *entry =
            &g_audio_state.azalia_unsol_queue[g_audio_state.azalia_unsol_rp];
        uint8_t codec = (uint8_t)(entry->response_ex & HDA_RIRB_RESP_CODEC_MASK);

        g_audio_state.azalia_unsol_rp =
            (uint8_t)((g_audio_state.azalia_unsol_rp + 1u) % HDA_UNSOL_QUEUE_SIZE);
        if (!g_audio_state.azalia_codec_probed ||
            codec != g_audio_state.azalia_codec_address) {
            continue;
        }
        mk_audio_azalia_note_unsol_event((uint8_t)((entry->response >> 26) & HDA_UNSOL_TAG_MASK));
    }
    g_audio_state.azalia_unsol_kick = 0u;
}

static void mk_audio_normalize_params(struct audio_swpar *params) {
    unsigned int rate;

    if (params == 0) {
        return;
    }

    params->sig = 1u;
    params->le = 1u;
    params->bits = 16u;
    params->bps = 2u;
    params->msb = 1u;

    if (params->pchan != 1u && params->pchan != 2u) {
        params->pchan = 2u;
    }
    params->rchan = params->pchan;

    rate = params->rate;
    if (rate <= 16537u) {
        params->rate = 11025u;
    } else if (rate <= 33075u) {
        params->rate = 22050u;
    } else if (rate <= 46050u) {
        params->rate = 44100u;
    } else {
        params->rate = 48000u;
    }

    if (params->round == 0u || params->round == 0xffffffffu) {
        params->round = 512u;
    }
    if (params->round > AUICH_DMA_SLOT_SIZE) {
        params->round = AUICH_DMA_SLOT_SIZE;
    }
    if ((params->round & 1u) != 0u) {
        params->round -= 1u;
    }
    if (params->round == 0u) {
        params->round = 512u;
    }

    if (params->nblks == 0u || params->nblks == 0xffffffffu) {
        params->nblks = 4u;
    }
    if (params->nblks > AUICH_DMALIST_MAX) {
        params->nblks = AUICH_DMALIST_MAX;
    }
}

static void mk_audio_normalize_uaudio_params(struct audio_swpar *params) {
    uint32_t frame_bytes;
    uint32_t round;
    uint32_t rate;
    uint8_t channels;
    uint8_t bits;
    uint8_t bytes_per_sample;

    if (params == 0) {
        return;
    }

    rate = g_audio_state.usb_audio_playback_sample_rate;
    channels = g_audio_state.usb_audio_playback_channel_count;
    bits = g_audio_state.usb_audio_playback_bit_resolution;
    bytes_per_sample = g_audio_state.usb_audio_playback_subframe_size;

    if (rate < 8000u || rate > 192000u) {
        rate = 48000u;
    }
    if (channels == 0u || channels > 8u) {
        channels = 2u;
    }
    if (bits == 0u || bits > 32u) {
        bits = 16u;
    }
    if (bytes_per_sample == 0u || bytes_per_sample > 4u) {
        bytes_per_sample = (uint8_t)((bits + 7u) / 8u);
    }
    if (bytes_per_sample == 0u) {
        bytes_per_sample = 2u;
    }

    params->rate = rate;
    params->bits = bits;
    params->bps = bytes_per_sample;
    params->sig = 1u;
    params->le = 1u;
    params->msb = 1u;
    params->pchan = channels;
    params->rchan = channels;

    frame_bytes = (uint32_t)bytes_per_sample * (uint32_t)channels;
    if (frame_bytes == 0u) {
        frame_bytes = 4u;
    }

    round = params->round;
    if (round == 0u || round == 0xffffffffu) {
        round = g_audio_state.usb_audio_playback_endpoint_max_packet != 0u ?
            ((uint32_t)g_audio_state.usb_audio_playback_endpoint_max_packet * 4u) :
            240u;
    }
    if (round > AUICH_DMA_SLOT_SIZE) {
        round = AUICH_DMA_SLOT_SIZE;
    }
    if (frame_bytes > 1u && round > frame_bytes) {
        round -= round % frame_bytes;
    }
    if (round == 0u && frame_bytes <= AUICH_DMA_SLOT_SIZE) {
        round = frame_bytes;
    }
    if (round == 0u) {
        round = AUICH_DMA_SLOT_SIZE;
    }
    params->round = round;

    if (params->nblks == 0u || params->nblks == 0xffffffffu) {
        params->nblks = 8u;
    }
    if (params->nblks > AUICH_DMALIST_MAX) {
        params->nblks = AUICH_DMALIST_MAX;
    }
}

static uint32_t mk_audio_uaudio_frame_bytes(void) {
    uint32_t channels = g_audio_state.usb_audio_playback_channel_count;
    uint32_t bytes_per_sample = g_audio_state.usb_audio_playback_subframe_size;
    uint32_t bits = g_audio_state.usb_audio_playback_bit_resolution;

    if (channels == 0u || channels > 8u) {
        channels = 2u;
    }
    if (bytes_per_sample == 0u || bytes_per_sample > 4u) {
        bytes_per_sample = (bits + 7u) / 8u;
    }
    if (bytes_per_sample == 0u) {
        bytes_per_sample = 2u;
    }
    return channels * bytes_per_sample;
}

static uint32_t mk_audio_estimated_playback_ticks(const struct audio_swpar *params, uint32_t data_size) {
    uint32_t bytes_per_second;
    uint32_t duration_ms;
    uint32_t ticks;

    if (params == 0 || params->rate == 0u || params->pchan == 0u || params->bps == 0u || data_size == 0u) {
        return 0u;
    }

    bytes_per_second = params->rate * params->pchan * params->bps;
    if (bytes_per_second == 0u) {
        return 0u;
    }

    duration_ms = (data_size * 1000u) / bytes_per_second;
    if ((data_size * 1000u) % bytes_per_second != 0u) {
        duration_ms += 1u;
    }

    ticks = (duration_ms + 9u) / 10u;
    if (ticks < 3u) {
        ticks = 3u;
    }
    return ticks;
}

static void mk_audio_reset_uaudio_runtime(void) {
    g_audio_state.usb_audio_output_running = 0u;
    g_audio_state.usb_audio_stream_started = 0u;
    g_audio_state.usb_audio_staging_fill = 0u;
    g_audio_state.usb_audio_output_bytes = 0u;
    g_audio_state.usb_audio_output_pos = 0u;
    g_audio_state.usb_audio_output_start_tick = 0u;
    g_audio_state.usb_audio_output_deadline_tick = 0u;
}

static void mk_audio_uaudio_update_output_progress(void) {
    uint32_t now_ticks;
    uint32_t start_tick;
    uint32_t deadline_tick;
    uint32_t elapsed_ticks;
    uint32_t total_ticks;
    uint32_t pos;

    if (!g_audio_state.usb_audio_output_running || g_audio_state.usb_audio_output_bytes == 0u) {
        return;
    }

    now_ticks = kernel_timer_get_ticks();
    start_tick = g_audio_state.usb_audio_output_start_tick;
    deadline_tick = g_audio_state.usb_audio_output_deadline_tick;

    if (deadline_tick == 0u || (int32_t)(now_ticks - deadline_tick) >= 0) {
        pos = g_audio_state.usb_audio_output_bytes;
    } else if ((int32_t)(now_ticks - start_tick) <= 0) {
        pos = 0u;
    } else {
        elapsed_ticks = now_ticks - start_tick;
        total_ticks = deadline_tick - start_tick;
        if (total_ticks == 0u) {
            pos = g_audio_state.usb_audio_output_bytes;
        } else {
            uint32_t whole = g_audio_state.usb_audio_output_bytes / total_ticks;
            uint32_t rem = g_audio_state.usb_audio_output_bytes % total_ticks;

            pos = whole * elapsed_ticks;
            pos += (rem * elapsed_ticks) / total_ticks;
        }
    }

    if (pos > g_audio_state.usb_audio_output_bytes) {
        pos = g_audio_state.usb_audio_output_bytes;
    }
    if (pos > g_audio_state.usb_audio_output_pos) {
        g_audio_state.playback_bytes_consumed += pos - g_audio_state.usb_audio_output_pos;
        g_audio_state.usb_audio_output_pos = pos;
    }
    if (g_audio_state.usb_audio_output_pos >= g_audio_state.usb_audio_output_bytes) {
        g_audio_state.usb_audio_output_running = 0u;
        g_audio_state.usb_audio_output_bytes = 0u;
        g_audio_state.usb_audio_output_pos = 0u;
        g_audio_state.usb_audio_output_start_tick = 0u;
        g_audio_state.usb_audio_output_deadline_tick = 0u;
    }
}

static uint32_t mk_audio_uaudio_pending_bytes(void) {
    uint32_t pending = (uint32_t)g_audio_state.usb_audio_staging_fill;

    mk_audio_uaudio_update_output_progress();
    if (g_audio_state.usb_audio_output_running &&
        g_audio_state.usb_audio_output_bytes > g_audio_state.usb_audio_output_pos) {
        pending += g_audio_state.usb_audio_output_bytes - g_audio_state.usb_audio_output_pos;
    }
    return pending;
}

static void mk_audio_uaudio_note_queued_output(uint32_t written) {
    if (written == 0u) {
        return;
    }
    /*
     * The current uaudio MVP pushes packets synchronously down to the host
     * controller; unlike DMA-backed PCI paths there isn't a real hardware queue
     * to model here yet. Treat successful writes as immediately consumed so
     * startup WAVs and cooperative playback don't stall behind fake deadlines.
     */
    g_audio_state.playback_bytes_consumed += written;
    g_audio_state.usb_audio_output_running = 0u;
    g_audio_state.usb_audio_output_bytes = 0u;
    g_audio_state.usb_audio_output_pos = 0u;
    g_audio_state.usb_audio_output_start_tick = 0u;
    g_audio_state.usb_audio_output_deadline_tick = 0u;
}

static int mk_audio_uaudio_flush_staging(uint8_t drain_all) {
    char device_config[MAX_AUDIO_DEV_LEN];
    uint32_t frame_bytes;
    uint32_t flush_bytes;
    uint32_t preferred_bytes;
    uint32_t written = 0u;

    if (g_audio_state.usb_audio_staging_fill == 0u) {
        return 0;
    }

    frame_bytes = mk_audio_uaudio_frame_bytes();
    if (frame_bytes == 0u) {
        frame_bytes = 4u;
    }

    preferred_bytes = g_audio_state.info.parameters.round;
    if (preferred_bytes == 0u || preferred_bytes == 0xffffffffu) {
        preferred_bytes = (uint32_t)g_audio_state.usb_audio_playback_endpoint_max_packet * 4u;
    }
    if (preferred_bytes == 0u) {
        preferred_bytes = frame_bytes;
    }
    if (preferred_bytes > MK_AUDIO_SOFT_BUFFER_SIZE) {
        preferred_bytes = MK_AUDIO_SOFT_BUFFER_SIZE;
    }
    if (preferred_bytes > frame_bytes) {
        preferred_bytes -= preferred_bytes % frame_bytes;
    }
    if (preferred_bytes == 0u) {
        preferred_bytes = frame_bytes;
    }

    if (!drain_all && g_audio_state.usb_audio_staging_fill < preferred_bytes) {
        return 0;
    }

    flush_bytes = (uint32_t)g_audio_state.usb_audio_staging_fill;
    if (!drain_all && flush_bytes > preferred_bytes) {
        flush_bytes = preferred_bytes;
    }
    if (flush_bytes > frame_bytes) {
        flush_bytes -= flush_bytes % frame_bytes;
    }
    if (flush_bytes == 0u) {
        return 0;
    }

    if (kernel_usb_audio_playback_write(g_audio_state.usb_audio_staging_buffer,
                                        flush_bytes,
                                        &written) != 0 || written == 0u) {
        memset(device_config, 0, sizeof(device_config));
        mk_audio_build_usb_audio_reason(device_config,
                                        sizeof(device_config),
                                        "usb-audio-",
                                        g_audio_state.usb_audio_transport_kind,
                                        "-write-failed");
        mk_audio_copy_limited(g_audio_state.info.device.config, device_config, MAX_AUDIO_DEV_LEN);
        g_audio_state.usb_audio_output_running = 0u;
        return -1;
    }

    if (written < (uint32_t)g_audio_state.usb_audio_staging_fill) {
        memmove(g_audio_state.usb_audio_staging_buffer,
                g_audio_state.usb_audio_staging_buffer + written,
                (uint32_t)g_audio_state.usb_audio_staging_fill - written);
    }
    g_audio_state.usb_audio_staging_fill = (uint16_t)((uint32_t)g_audio_state.usb_audio_staging_fill - written);
    mk_audio_uaudio_note_queued_output(written);

    if (written < flush_bytes) {
        memset(device_config, 0, sizeof(device_config));
        if (frame_bytes != 0u && (written % frame_bytes) != 0u) {
            mk_audio_build_usb_audio_reason(device_config,
                                            sizeof(device_config),
                                            "usb-audio-",
                                            g_audio_state.usb_audio_transport_kind,
                                            "-unaligned-short-write");
        } else {
            mk_audio_build_usb_audio_reason(device_config,
                                            sizeof(device_config),
                                            "usb-audio-",
                                            g_audio_state.usb_audio_transport_kind,
                                            "-short-write");
        }
        mk_audio_copy_limited(g_audio_state.info.device.config, device_config, MAX_AUDIO_DEV_LEN);
    } else {
        mk_audio_set_uaudio_identity("-attached");
    }

    return (int)written;
}

static int mk_audio_is_auich_device(uint16_t vendor_id, uint16_t device_id) {
    switch (vendor_id) {
    case PCI_VENDOR_INTEL:
        switch (device_id) {
        case PCI_PRODUCT_INTEL_82801AA_ACA:
        case PCI_PRODUCT_INTEL_82801AB_ACA:
        case PCI_PRODUCT_INTEL_82801BA_ACA:
        case PCI_PRODUCT_INTEL_82801CA_ACA:
        case PCI_PRODUCT_INTEL_82801DB_ACA:
        case PCI_PRODUCT_INTEL_82801EB_ACA:
        case PCI_PRODUCT_INTEL_6300ESB_ACA:
        case PCI_PRODUCT_INTEL_82801FB_ACA:
        case PCI_PRODUCT_INTEL_6321ESB_ACA:
        case PCI_PRODUCT_INTEL_82801GB_ACA:
        case PCI_PRODUCT_INTEL_82440MX_ACA:
            return 1;
        default:
            return 0;
        }
    case PCI_VENDOR_AMD:
        return device_id == PCI_PRODUCT_AMD_768_ACA ||
               device_id == PCI_PRODUCT_AMD_8111_ACA;
    case PCI_VENDOR_ATI:
        return device_id == PCI_PRODUCT_ATI_SB200_AUDIO ||
               device_id == PCI_PRODUCT_ATI_SB300_AUDIO ||
               device_id == PCI_PRODUCT_ATI_SB400_AUDIO ||
               device_id == PCI_PRODUCT_ATI_SB600_AUDIO;
    case PCI_VENDOR_NVIDIA:
        switch (device_id) {
        case PCI_PRODUCT_NVIDIA_MCP04_AC97:
        case PCI_PRODUCT_NVIDIA_NFORCE4_AC97:
        case PCI_PRODUCT_NVIDIA_NFORCE2_AC97:
        case PCI_PRODUCT_NVIDIA_NFORCE2_400_AC97:
        case PCI_PRODUCT_NVIDIA_NFORCE3_AC97:
        case PCI_PRODUCT_NVIDIA_NFORCE3_250_AC97:
        case PCI_PRODUCT_NVIDIA_NFORCE_AC97:
        case PCI_PRODUCT_NVIDIA_MCP51_AC97:
            return 1;
        default:
            return 0;
        }
    case PCI_VENDOR_ALI:
        return device_id == PCI_PRODUCT_ALI_M5455_AC97;
    case PCI_VENDOR_SIS:
        return device_id == PCI_PRODUCT_SIS_7012_AC97;
    case PCI_VENDOR_VIATECH:
        return device_id == PCI_PRODUCT_VIATECH_VT82C686A_AC97 ||
               device_id == PCI_PRODUCT_VIATECH_VT8233_AC97;
    default:
        return 0;
    }
}

static __attribute__((unused)) int mk_audio_auich_prefers_native_mmio(uint16_t vendor_id, uint16_t device_id) {
    if (vendor_id != PCI_VENDOR_INTEL) {
        return 0;
    }

    switch (device_id) {
    case PCI_PRODUCT_INTEL_82801DB_ACA:
    case PCI_PRODUCT_INTEL_82801EB_ACA:
    case PCI_PRODUCT_INTEL_82801FB_ACA:
    case PCI_PRODUCT_INTEL_82801GB_ACA:
        return 1;
    default:
        return 0;
    }
}

static __attribute__((unused)) int mk_audio_auich_quirk_ignore_codecready(uint16_t vendor_id, uint16_t device_id) {
    if (vendor_id != PCI_VENDOR_INTEL) {
        return 0;
    }

    switch (device_id) {
    case PCI_PRODUCT_INTEL_82801DB_ACA:
    case PCI_PRODUCT_INTEL_82801EB_ACA:
    case PCI_PRODUCT_INTEL_82801FB_ACA:
    case PCI_PRODUCT_INTEL_82801GB_ACA:
        return 1;
    default:
        return 0;
    }
}

static int mk_audio_compat_wait_cas_clear(void) {
    if (g_audio_state.compat_aud_base == 0u) {
        return -1;
    }

    for (uint32_t i = 0u; i < AUICH_SEMATIMO; ++i) {
        if ((mk_audio_compat_read8(g_audio_state.compat_aud_base, g_audio_state.compat_aud_is_mmio, AUICH_CAS) & 0x1u) == 0u) {
            return 0;
        }
        mk_audio_compat_delay();
    }
    return g_audio_state.compat_ignore_codecready ? 0 : -1;
}

static int mk_audio_compat_read_codec(uint8_t reg, uint16_t *value_out) {
    if (value_out == 0 || g_audio_state.compat_mix_base == 0u) {
        return -1;
    }
    if (mk_audio_compat_wait_cas_clear() != 0) {
        return -1;
    }

    *value_out = mk_audio_compat_read16(g_audio_state.compat_mix_base, g_audio_state.compat_mix_is_mmio, reg);
    return 0;
}

static int mk_audio_compat_write_codec(uint8_t reg, uint16_t value) {
    if (g_audio_state.compat_mix_base == 0u) {
        return -1;
    }
    if (mk_audio_compat_wait_cas_clear() != 0) {
        return -1;
    }

    mk_audio_compat_write16(g_audio_state.compat_mix_base, g_audio_state.compat_mix_is_mmio, reg, value);
    return 0;
}

static int mk_audio_compat_probe_codec_registers(uint16_t *caps_out) {
    uint16_t caps = 0xffffu;

    if (g_audio_state.compat_mix_base == 0u) {
        return -1;
    }

    for (uint32_t attempt = 0u; attempt < 32u; ++attempt) {
        if (mk_audio_compat_read_codec(AC97_REG_RESET, &caps) == 0 &&
            caps != 0xffffu) {
            if (caps_out != 0) {
                *caps_out = caps;
            }
            return 0;
        }
        mk_audio_compat_delay();
    }

    return -1;
}

static int mk_audio_compat_reset_codec(void) {
    uint32_t control;
    uint16_t caps = 0u;

    if (g_audio_state.compat_aud_base == 0u) {
        return -1;
    }

    control = mk_audio_compat_read32(g_audio_state.compat_aud_base, g_audio_state.compat_aud_is_mmio, AUICH_GCTRL);
    control &= ~(uint32_t)AUICH_ACLSO;
    control |= (control & AUICH_CRESET) != 0u ? AUICH_WRESET : AUICH_CRESET;
    mk_audio_compat_write32(g_audio_state.compat_aud_base, g_audio_state.compat_aud_is_mmio, AUICH_GCTRL, control);

    for (uint32_t i = 0u; i < AUICH_RESETIMO; ++i) {
        if ((mk_audio_compat_read32(g_audio_state.compat_aud_base, g_audio_state.compat_aud_is_mmio, AUICH_GSTS) & AUICH_PCR) != 0u) {
            g_audio_state.compat_codec_ready = 1u;
            return 0;
        }
        mk_audio_compat_delay();
    }

    if (mk_audio_compat_probe_codec_registers(&caps) == 0) {
        g_audio_state.compat_caps = caps;
        g_audio_state.compat_codec_ready = 1u;
        kernel_debug_puts("audio: ac97 codec ready via register probe\n");
        return 0;
    }

    if (g_audio_state.compat_ignore_codecready) {
        g_audio_state.compat_codec_ready = 1u;
        return 0;
    }

    g_audio_state.compat_codec_ready = 0u;
    return -1;
}

static __attribute__((unused)) void mk_audio_compat_apply_device_quirks(void) {
    if (!g_audio_state.compat_ready) {
        return;
    }

    if (g_audio_state.pci.vendor_id == PCI_VENDOR_SIS &&
        g_audio_state.pci.device_id == PCI_PRODUCT_SIS_7012_AC97) {
        uint32_t control =
            mk_audio_compat_read32(g_audio_state.compat_aud_base,
                                   g_audio_state.compat_aud_is_mmio,
                                   ICH_SIS_NV_CTL);
        control |= ICH_SIS_CTL_UNMUTE;
        mk_audio_compat_write32(g_audio_state.compat_aud_base,
                                g_audio_state.compat_aud_is_mmio,
                                ICH_SIS_NV_CTL,
                                control);
    }
}

static uint16_t mk_audio_encode_output_gain(uint8_t level) {
    uint16_t atten;

    atten = (uint16_t)((255u - (uint16_t)level) * 31u / 255u);
    if (atten > 31u) {
        atten = 31u;
    }
    return (uint16_t)((atten << 8) | atten);
}

static uint16_t mk_audio_encode_input_gain(uint8_t level) {
    uint16_t gain;

    gain = (uint16_t)((uint16_t)level * 15u / 255u);
    if (gain > 15u) {
        gain = 15u;
    }
    return (uint16_t)((gain << 8) | gain);
}

static uint16_t mk_audio_encode_record_source(uint8_t input_default) {
    uint16_t source;

    source = input_default == 1u ? AC97_RECORD_SOURCE_LINEIN : AC97_RECORD_SOURCE_MIC;
    return (uint16_t)(source | (source << 8));
}

static __attribute__((unused)) void mk_audio_compat_sync_codec_caps(void) {
    uint16_t caps = 0u;

    if (!g_audio_state.compat_codec_ready) {
        return;
    }

    if (mk_audio_compat_read_codec(AC97_REG_RESET, &caps) == 0) {
        g_audio_state.compat_caps = caps;
        mk_audio_refresh_topology_snapshot();
    }
}

static uint32_t mk_audio_output_count(void) {
    uint32_t mask = mk_audio_output_presence_mask();
    uint32_t count = 0u;

    while (mask != 0u) {
        count += mask & 0x1u;
        mask >>= 1;
    }
    return count != 0u ? count : 1u;
}

static uint32_t mk_audio_input_count(void) {
    if ((g_audio_state.info.flags & MK_AUDIO_CAPS_CAPTURE) == 0u) {
        return 0u;
    }
    if (g_audio_state.backend_kind != MK_AUDIO_BACKEND_COMPAT_AUICH || !g_audio_state.compat_ready) {
        return 1u;
    }
    return 2u;
}

static uint32_t mk_audio_output_presence_mask(void) {
    uint32_t mask = 0x1u;

    if (g_audio_state.backend_kind == MK_AUDIO_BACKEND_COMPAT_AZALIA &&
        g_audio_state.azalia_ready) {
        if (g_audio_state.azalia_output_mask != 0u) {
            return g_audio_state.azalia_output_mask;
        }
        return 0x1u;
    }
    if (g_audio_state.backend_kind == MK_AUDIO_BACKEND_COMPAT_AUICH &&
        g_audio_state.compat_ready) {
        if ((g_audio_state.compat_caps & AC97_CAPS_HEADPHONES) != 0u) {
            mask |= 0x2u;
        }
        if ((g_audio_state.compat_ext_audio_id & AC97_EXT_AUDIO_SDAC) != 0u) {
            mask |= 0x4u;
        }
        if ((g_audio_state.compat_ext_audio_id & (AC97_EXT_AUDIO_CDAC | AC97_EXT_AUDIO_LDAC)) != 0u) {
            mask |= 0x8u;
        }
    }
    return mask;
}

static uint32_t mk_audio_input_presence_mask(void) {
    if ((g_audio_state.info.flags & MK_AUDIO_CAPS_CAPTURE) == 0u) {
        return 0u;
    }
    if (g_audio_state.backend_kind == MK_AUDIO_BACKEND_COMPAT_AZALIA &&
        g_audio_state.azalia_ready &&
        g_audio_state.azalia_input_mask != 0u) {
        return g_audio_state.azalia_input_mask;
    }
    return 0x3u;
}

static int mk_audio_output_bit_from_ord(uint32_t mask, uint8_t ord) {
    uint8_t seen = 0u;

    for (int bit = 0; bit < 4; ++bit) {
        if ((mask & (1u << bit)) == 0u) {
            continue;
        }
        if (seen == ord) {
            return bit;
        }
        ++seen;
    }
    return -1;
}

static int mk_audio_azalia_output_candidate_available(int bit) {
    uint32_t config_default;
    uint32_t port;

    if (bit < 0 || bit >= 4 || g_audio_state.azalia_output_dac_nids[bit] == 0u) {
        return 0;
    }
    config_default = g_audio_state.azalia_output_config_defaults[bit];
    port = mk_audio_hda_config_port(config_default);
    if ((port == HDA_CONFIG_PORT_JACK || port == HDA_CONFIG_PORT_BOTH) &&
        mk_audio_azalia_output_requires_presence(bit)) {
        return g_audio_state.azalia_output_present_bits[bit] != 0u;
    }
    return 1;
}

static int mk_audio_azalia_output_requires_presence(int bit) {
    uint32_t config_default;
    uint32_t device;
    uint32_t misc;

    if (bit < 0 || bit >= 4) {
        return 0;
    }
    config_default = g_audio_state.azalia_output_config_defaults[bit];
    device = mk_audio_hda_config_device(config_default);
    misc = (config_default >> HDA_CONFIG_MISC_SHIFT) & HDA_CONFIG_MISC_MASK;
    if (device == HDA_CONFIG_DEVICE_SPEAKER) {
        return 0;
    }
    if ((misc & HDA_CONFIG_MISC_PRESENCEOV) != 0u) {
        return 0;
    }
    return 1;
}

static int mk_audio_azalia_output_is_speaker(int bit) {
    uint32_t config_default;

    if (bit < 0 || bit >= 4) {
        return 0;
    }
    config_default = g_audio_state.azalia_output_config_defaults[bit];
    return mk_audio_hda_config_device(config_default) == HDA_CONFIG_DEVICE_SPEAKER;
}

static int mk_audio_azalia_output_is_digital(int bit) {
    uint32_t config_default;
    uint32_t device;

    if (bit < 0 || bit >= 4) {
        return 0;
    }
    config_default = g_audio_state.azalia_output_config_defaults[bit];
    device = mk_audio_hda_config_device(config_default);
    return device == HDA_CONFIG_DEVICE_SPDIFOUT ||
           device == HDA_CONFIG_DEVICE_DIGITALOUT;
}

static int mk_audio_azalia_output_is_present_external(int bit) {
    if (bit < 0 || bit >= 4) {
        return 0;
    }
    if (mk_audio_azalia_output_is_speaker(bit)) {
        return 0;
    }
    return g_audio_state.azalia_output_present_bits[bit] != 0u &&
           mk_audio_azalia_output_candidate_available(bit);
}

static int mk_audio_azalia_have_present_external_output(void) {
    for (int bit = 0; bit < 4; ++bit) {
        if (mk_audio_azalia_output_is_present_external(bit)) {
            return 1;
        }
    }
    return 0;
}

static uint32_t mk_audio_azalia_collect_primary_output_bits(uint8_t *bits_out, uint32_t max_bits) {
    uint32_t count = 0u;

    if (bits_out == 0 || max_bits == 0u) {
        return 0u;
    }

    for (int bit = 1; bit < 4; ++bit) {
        uint32_t insert_at;

        if (g_audio_state.azalia_output_pin_nids[bit] == 0u ||
            g_audio_state.azalia_output_dac_nids[bit] == 0u ||
            mk_audio_azalia_output_is_speaker(bit) ||
            mk_audio_azalia_output_is_digital(bit)) {
            continue;
        }

        insert_at = count;
        while (insert_at > 0u) {
            uint8_t prev_bit = bits_out[insert_at - 1u];
            uint32_t prev_sort = g_audio_state.azalia_output_sort_keys[prev_bit];
            uint32_t sort_key = g_audio_state.azalia_output_sort_keys[bit];
            int prev_prio = (int)g_audio_state.azalia_output_priorities[prev_bit];
            int prio = (int)g_audio_state.azalia_output_priorities[bit];

            if (prev_sort < sort_key ||
                (prev_sort == sort_key && prev_prio >= prio)) {
                break;
            }
            if (insert_at < max_bits) {
                bits_out[insert_at] = bits_out[insert_at - 1u];
            }
            --insert_at;
        }
        if (insert_at < max_bits) {
            bits_out[insert_at] = (uint8_t)bit;
        }
        if (count < max_bits) {
            ++count;
        }
    }

    return count;
}

static uint32_t mk_audio_azalia_collect_output_order(uint8_t *bits_out, uint32_t max_bits) {
    uint8_t primary_bits[3];
    uint32_t count = 0u;
    uint32_t primary_count;

    if (bits_out == 0 || max_bits == 0u) {
        return 0u;
    }

    primary_count = mk_audio_azalia_collect_primary_output_bits(primary_bits, 3u);
    for (uint32_t i = 0u; i < primary_count && count < max_bits; ++i) {
        bits_out[count++] = primary_bits[i];
    }
    if (g_audio_state.azalia_output_pin_nids[0] != 0u &&
        g_audio_state.azalia_output_dac_nids[0] != 0u &&
        count < max_bits) {
        bits_out[count++] = 0u;
    }
    for (int bit = 1; bit < 4 && count < max_bits; ++bit) {
        uint8_t seen = 0u;

        if (g_audio_state.azalia_output_pin_nids[bit] == 0u ||
            g_audio_state.azalia_output_dac_nids[bit] == 0u) {
            continue;
        }
        for (uint32_t i = 0u; i < count; ++i) {
            if (bits_out[i] == (uint8_t)bit) {
                seen = 1u;
                break;
            }
        }
        if (!seen) {
            bits_out[count++] = (uint8_t)bit;
        }
    }

    return count;
}

static int mk_audio_azalia_should_mute_speakers(int selected_bit) {
    if (g_audio_state.azalia_spkr_muter_mask != 0u) {
        for (uint32_t i = 0u; i < g_audio_state.azalia_sense_pin_count; ++i) {
            int bit;

            if ((g_audio_state.azalia_spkr_muter_mask & (uint8_t)(1u << i)) == 0u) {
                continue;
            }
            bit = (int)g_audio_state.azalia_sense_pin_output_bits[i];
            if (bit < 0 || bit >= 4) {
                continue;
            }
            if (g_audio_state.azalia_output_present_bits[bit] != 0u &&
                mk_audio_azalia_output_candidate_available(bit)) {
                return 1;
            }
        }
    }
    if (selected_bit >= 0 && selected_bit < 4 &&
        !mk_audio_azalia_output_is_speaker(selected_bit) &&
        mk_audio_azalia_output_candidate_available(selected_bit) &&
        g_audio_state.azalia_output_present_bits[selected_bit] != 0u) {
        return 1;
    }
    return mk_audio_azalia_have_present_external_output();
}

static int mk_audio_azalia_choose_output_path(uint8_t *pin_out, uint8_t *dac_out, int *selected_bit_out) {
    uint32_t output_mask;
    uint8_t ordered_bits[4];
    uint32_t ordered_count;
    int preferred_bit;
    int have_present_external;
    int fhp_bit = -1;

    if (pin_out == 0 || dac_out == 0 || selected_bit_out == 0) {
        return -1;
    }

    *pin_out = 0u;
    *dac_out = 0u;
    *selected_bit_out = -1;

    output_mask = mk_audio_output_presence_mask();
    preferred_bit = mk_audio_output_bit_from_ord(output_mask, g_audio_state.default_output);
    have_present_external = mk_audio_azalia_have_present_external_output();
    if (g_audio_state.azalia_fhp_pin_nid != 0u) {
        for (int bit = 0; bit < 4; ++bit) {
            if (g_audio_state.azalia_output_pin_nids[bit] == g_audio_state.azalia_fhp_pin_nid) {
                fhp_bit = bit;
                break;
            }
        }
    }
    if (have_present_external &&
        fhp_bit >= 0 &&
        g_audio_state.azalia_output_present_bits[fhp_bit] != 0u &&
        mk_audio_azalia_output_candidate_available(fhp_bit)) {
        preferred_bit = fhp_bit;
    }
    ordered_count = mk_audio_azalia_collect_output_order(ordered_bits, 4u);

    for (uint8_t pass = 0u; pass < 4u; ++pass) {
        for (uint32_t order = 0u; order < ordered_count; ++order) {
            int bit = (int)ordered_bits[order];
            uint8_t pin_nid;
            uint8_t dac_nid;
            uint8_t pin_present;

            if (pass == 0u && (bit != preferred_bit || g_audio_state.azalia_output_present_bits[bit] == 0u)) {
                continue;
            }
            if (pass == 1u && g_audio_state.azalia_output_present_bits[bit] == 0u) {
                continue;
            }
            if (pass == 2u && (bit != preferred_bit || g_audio_state.azalia_output_present_bits[bit] != 0u)) {
                continue;
            }
            if (pass == 3u && (bit == preferred_bit || g_audio_state.azalia_output_present_bits[bit] != 0u)) {
                continue;
            }
            if (have_present_external &&
                mk_audio_azalia_output_is_speaker(bit) &&
                (pass == 0u || pass == 1u)) {
                continue;
            }

            pin_nid = g_audio_state.azalia_output_pin_nids[bit];
            dac_nid = g_audio_state.azalia_output_dac_nids[bit];
            pin_present = g_audio_state.azalia_output_present_bits[bit];
            if (!mk_audio_azalia_output_candidate_available(bit)) {
                continue;
            }
            if (pin_nid == 0u || mk_audio_azalia_find_output_dac(pin_nid, 0u) == (int)dac_nid) {
                *pin_out = pin_nid;
                *dac_out = dac_nid;
                *selected_bit_out = bit;
                kernel_debug_printf("audio: hda choose-output bit=%u present=%u pin=%u dac=%u\n",
                                    (unsigned int)bit,
                                    (unsigned int)pin_present,
                                    (unsigned int)pin_nid,
                                    (unsigned int)dac_nid);
                return 0;
            }
        }
    }

    if (g_audio_state.azalia_output_dac_nid != 0u) {
        *pin_out = g_audio_state.azalia_output_pin_nid;
        *dac_out = g_audio_state.azalia_output_dac_nid;
        *selected_bit_out = preferred_bit;
        return 0;
    }

    return -1;
}

static int mk_audio_azalia_current_output_path_valid(void) {
    uint8_t path[8];
    uint8_t path_indices[8];
    uint32_t path_len = 0u;

    for (int bit = 0; bit < 4; ++bit) {
        if (!mk_audio_azalia_output_candidate_available(bit)) {
            continue;
        }
        if (g_audio_state.azalia_output_dac_nids[bit] != g_audio_state.azalia_output_dac_nid) {
            continue;
        }
        if (g_audio_state.azalia_output_pin_nid == 0u ||
            g_audio_state.azalia_output_pin_nids[bit] == g_audio_state.azalia_output_pin_nid) {
            if (g_audio_state.azalia_output_pin_nid != 0u) {
                return mk_audio_azalia_resolve_output_path(g_audio_state.azalia_output_pin_nid,
                                                           g_audio_state.azalia_output_dac_nid,
                                                           0u,
                                                           path,
                                                           path_indices,
                                                           8u,
                                                           &path_len) == 0;
            }
            return 1;
        }
    }
    if (!g_audio_state.azalia_codec_probed ||
        !g_audio_state.azalia_path_programmed ||
        g_audio_state.azalia_output_dac_nid == 0u) {
        return 0;
    }
    if (g_audio_state.azalia_output_pin_nid == 0u) {
        return 1;
    }
    return mk_audio_azalia_resolve_output_path(g_audio_state.azalia_output_pin_nid,
                                               g_audio_state.azalia_output_dac_nid,
                                               0u,
                                               path,
                                               path_indices,
                                               8u,
                                               &path_len) == 0;
}

static int mk_audio_azalia_config_is_fatal(const char *config) {
    if (config == 0) {
        return 0;
    }
    if (strcmp(config, "hda-bar-unavailable") == 0 ||
        strcmp(config, "hda-reset-failed") == 0 ||
        strcmp(config, "hda-ring-init-failed") == 0 ||
        strcmp(config, "hda-no-output-stream") == 0 ||
        strcmp(config, "hda-no-audio-fg") == 0 ||
        strcmp(config, "hda-no-usable-output") == 0 ||
        strcmp(config, "hda-stream-reset-failed") == 0 ||
        strcmp(config, "hda-codec-connect-failed") == 0 ||
        strcmp(config, "hda-run-not-latched") == 0) {
        return 1;
    }
    return 0;
}

static int mk_audio_azalia_has_fatal_probe_failure(void) {
    const char *config = g_audio_state.info.device.config;

    if (!g_audio_state.azalia_codec_probed || g_audio_state.azalia_afg_nid == 0u) {
        return 1;
    }
    return mk_audio_azalia_config_is_fatal(config);
}

static int mk_audio_azalia_register_special_output_pin(uint8_t pin_nid,
                                                       uint8_t dac_nid,
                                                       int priority,
                                                       uint32_t config_default) {
    uint32_t device;
    uint32_t port;
    uint32_t location;
    uint32_t sort_key;

    if (pin_nid == 0u || dac_nid == 0u) {
        return 0;
    }

    device = mk_audio_hda_config_device(config_default);
    port = mk_audio_hda_config_port(config_default);
    location = mk_audio_hda_config_location(config_default);
    sort_key = mk_audio_hda_output_sort_key(config_default);

    if (port == HDA_CONFIG_PORT_FIXED &&
        device == HDA_CONFIG_DEVICE_SPEAKER) {
        if (g_audio_state.azalia_output_pin_nids[0] == 0u ||
            sort_key < g_audio_state.azalia_output_sort_keys[0] ||
            (sort_key == g_audio_state.azalia_output_sort_keys[0] &&
             priority > (int)g_audio_state.azalia_output_priorities[0])) {
            if (g_audio_state.azalia_output_pin_nids[0] != 0u &&
                g_audio_state.azalia_output_pin_nids[0] != pin_nid) {
                mk_audio_azalia_register_secondary_speaker(g_audio_state.azalia_output_pin_nids[0],
                                                           g_audio_state.azalia_output_dac_nids[0],
                                                           (int)g_audio_state.azalia_output_priorities[0],
                                                           g_audio_state.azalia_output_config_defaults[0]);
            }
            g_audio_state.azalia_output_pin_nids[0] = pin_nid;
            g_audio_state.azalia_output_dac_nids[0] = dac_nid;
            g_audio_state.azalia_output_priorities[0] = (int8_t)priority;
            g_audio_state.azalia_output_config_defaults[0] = config_default;
            g_audio_state.azalia_output_sort_keys[0] = sort_key;
        } else if (g_audio_state.azalia_output_pin_nids[0] != pin_nid) {
            mk_audio_azalia_register_secondary_speaker(pin_nid,
                                                       dac_nid,
                                                       priority,
                                                       config_default);
        }
        return 1;
    }

    if (port == HDA_CONFIG_PORT_JACK &&
        device == HDA_CONFIG_DEVICE_HEADPHONE &&
        location == 0x02u) {
        g_audio_state.azalia_fhp_pin_nid = pin_nid;
        g_audio_state.azalia_fhp_dac_nid = dac_nid;
        return 0;
    }

    return 0;
}

static int mk_audio_azalia_register_output_path(uint32_t output_mask,
                                                uint8_t pin_nid,
                                                uint8_t dac_nid,
                                                int priority,
                                                uint32_t config_default) {
    int bit;
    uint32_t sort_key;
    uint32_t device;
    uint32_t location;

    if (output_mask == 0u || pin_nid == 0u || dac_nid == 0u) {
        return -1;
    }
    device = mk_audio_hda_config_device(config_default);
    location = mk_audio_hda_config_location(config_default);
    if (device == HDA_CONFIG_DEVICE_HEADPHONE && location == 0x02u) {
        g_audio_state.azalia_fhp_pin_nid = pin_nid;
        g_audio_state.azalia_fhp_dac_nid = dac_nid;
    }
    sort_key = mk_audio_hda_output_sort_key(config_default);
    for (bit = 0; bit < 4; ++bit) {
        if ((output_mask & (1u << bit)) == 0u) {
            continue;
        }
        if (g_audio_state.azalia_output_pin_nids[bit] == 0u ||
            sort_key < g_audio_state.azalia_output_sort_keys[bit] ||
            (sort_key == g_audio_state.azalia_output_sort_keys[bit] &&
             priority > (int)g_audio_state.azalia_output_priorities[bit])) {
            if (bit == 0 &&
                device == HDA_CONFIG_DEVICE_SPEAKER &&
                g_audio_state.azalia_output_pin_nids[bit] != 0u &&
                g_audio_state.azalia_output_pin_nids[bit] != pin_nid) {
                mk_audio_azalia_register_secondary_speaker(g_audio_state.azalia_output_pin_nids[bit],
                                                           g_audio_state.azalia_output_dac_nids[bit],
                                                           (int)g_audio_state.azalia_output_priorities[bit],
                                                           g_audio_state.azalia_output_config_defaults[bit]);
            }
            g_audio_state.azalia_output_pin_nids[bit] = pin_nid;
            g_audio_state.azalia_output_dac_nids[bit] = dac_nid;
            g_audio_state.azalia_output_priorities[bit] = (int8_t)priority;
            g_audio_state.azalia_output_config_defaults[bit] = config_default;
            g_audio_state.azalia_output_sort_keys[bit] = sort_key;
        } else if (bit == 0 &&
                   device == HDA_CONFIG_DEVICE_SPEAKER &&
                   g_audio_state.azalia_output_pin_nids[bit] != pin_nid) {
            mk_audio_azalia_register_secondary_speaker(pin_nid,
                                                       dac_nid,
                                                       priority,
                                                       config_default);
        }
    }
    return 0;
}

static void mk_audio_azalia_register_secondary_speaker(uint8_t pin_nid,
                                                       uint8_t dac_nid,
                                                       int priority,
                                                       uint32_t config_default) {
    uint32_t sort_key;
    uint32_t current_sort_key;

    if (pin_nid == 0u || dac_nid == 0u || g_audio_state.azalia_output_pin_nids[0] == pin_nid) {
        return;
    }
    sort_key = mk_audio_hda_output_sort_key(config_default);
    current_sort_key = mk_audio_hda_output_sort_key(g_audio_state.azalia_speaker2_config_default);
    if (g_audio_state.azalia_speaker2_pin_nid == 0u ||
        sort_key < current_sort_key ||
        (sort_key == current_sort_key &&
         priority > (int)g_audio_state.azalia_speaker2_priority)) {
        g_audio_state.azalia_speaker2_pin_nid = pin_nid;
        g_audio_state.azalia_speaker2_dac_nid = dac_nid;
        g_audio_state.azalia_speaker2_priority = (int8_t)priority;
        g_audio_state.azalia_speaker2_config_default = config_default;
    }
}

static int mk_audio_azalia_reassign_output_bit_dac(uint8_t bit,
                                                   const uint8_t *avoid_dacs,
                                                   uint32_t avoid_count) {
    uint8_t pin_nid;
    uint8_t current_dac;
    uint8_t alternate_dac = 0u;

    if (bit >= 4u || !mk_audio_azalia_output_candidate_available((int)bit)) {
        return -1;
    }
    pin_nid = g_audio_state.azalia_output_pin_nids[bit];
    current_dac = g_audio_state.azalia_output_dac_nids[bit];
    if (pin_nid == 0u || current_dac == 0u) {
        return -1;
    }
    if (mk_audio_azalia_find_alternate_output_dac(pin_nid,
                                                  current_dac,
                                                  avoid_dacs,
                                                  avoid_count,
                                                  &alternate_dac) != 0 ||
        alternate_dac == 0u ||
        alternate_dac == current_dac) {
        return -1;
    }
    kernel_debug_printf("audio: hda rebalance output bit=%u pin=%u dac=%u->%u\n",
                        (unsigned int)bit,
                        (unsigned int)pin_nid,
                        (unsigned int)current_dac,
                        (unsigned int)alternate_dac);
    g_audio_state.azalia_output_dac_nids[bit] = alternate_dac;
    return 0;
}

static void mk_audio_azalia_select_primary_output_dacs(void) {
    uint8_t primary_bits[3];
    uint8_t claimed_dacs[3];
    uint32_t primary_count;
    uint32_t claimed_count = 0u;

    primary_count = mk_audio_azalia_collect_primary_output_bits(primary_bits, 3u);
    for (uint32_t i = 0u; i < primary_count; ++i) {
        uint8_t bit = primary_bits[i];
        uint8_t pin_nid = g_audio_state.azalia_output_pin_nids[bit];
        uint8_t dac_nid = g_audio_state.azalia_output_dac_nids[bit];
        uint8_t duplicate = 0u;

        if (pin_nid == 0u || dac_nid == 0u) {
            continue;
        }
        for (uint32_t j = 0u; j < claimed_count; ++j) {
            if (claimed_dacs[j] == dac_nid) {
                duplicate = 1u;
                break;
            }
        }
        if (duplicate &&
            mk_audio_azalia_reassign_output_bit_dac(bit, claimed_dacs, claimed_count) == 0) {
            dac_nid = g_audio_state.azalia_output_dac_nids[bit];
        }
        if (claimed_count < 3u) {
            claimed_dacs[claimed_count++] = dac_nid;
        }
        if (pin_nid == g_audio_state.azalia_fhp_pin_nid) {
            g_audio_state.azalia_fhp_dac_nid = dac_nid;
        }
    }
}

static void mk_audio_azalia_select_speaker_dac(void) {
    uint8_t primary_bits[3];
    uint8_t primary_dacs[3];
    uint32_t primary_count;
    uint32_t primary_dac_count = 0u;
    uint8_t speaker_pin = g_audio_state.azalia_output_pin_nids[0];
    uint8_t speaker_dac = g_audio_state.azalia_output_dac_nids[0];
    uint8_t shared_primary = 0xffu;

    if (speaker_pin == 0u || speaker_dac == 0u) {
        g_audio_state.azalia_spkr_dac_nid = 0u;
        return;
    }

    primary_count = mk_audio_azalia_collect_primary_output_bits(primary_bits, 3u);
    for (uint32_t i = 0u; i < primary_count; ++i) {
        uint8_t bit = primary_bits[i];
        uint8_t dac_nid = g_audio_state.azalia_output_dac_nids[bit];
        uint8_t seen = 0u;

        if (dac_nid == 0u) {
            continue;
        }
        if (dac_nid == speaker_dac && shared_primary == 0xffu) {
            shared_primary = bit;
        }
        for (uint32_t j = 0u; j < primary_dac_count; ++j) {
            if (primary_dacs[j] == dac_nid) {
                seen = 1u;
                break;
            }
        }
        if (!seen && primary_dac_count < 3u) {
            primary_dacs[primary_dac_count++] = dac_nid;
        }
    }

    if (shared_primary != 0xffu) {
        uint8_t alternate_dac = 0u;

        if (mk_audio_azalia_find_alternate_output_dac(speaker_pin,
                                                      speaker_dac,
                                                      primary_dacs,
                                                      primary_dac_count,
                                                      &alternate_dac) == 0 &&
            alternate_dac != 0u &&
            alternate_dac != speaker_dac) {
            kernel_debug_printf("audio: hda select speaker dac pin=%u dac=%u->%u\n",
                                (unsigned int)speaker_pin,
                                (unsigned int)speaker_dac,
                                (unsigned int)alternate_dac);
            if (mk_audio_azalia_retarget_pin_to_dac(speaker_pin, alternate_dac) == 0) {
                g_audio_state.azalia_output_dac_nids[0] = alternate_dac;
                speaker_dac = alternate_dac;
            }
        } else if (primary_count > 0u &&
                   g_audio_state.azalia_output_dac_nids[primary_bits[0]] == speaker_dac) {
            (void)mk_audio_azalia_reassign_output_bit_dac(primary_bits[0], &speaker_dac, 1u);
        }
    }

    if (g_audio_state.azalia_speaker2_pin_nid != 0u &&
        g_audio_state.azalia_output_dac_nids[0] != 0u) {
        uint8_t path[8];
        uint8_t path_indices[8];
        uint32_t path_len = 0u;
        uint8_t target_dac = g_audio_state.azalia_output_dac_nids[0];

        if ((g_audio_state.azalia_quirks & MK_AUDIO_HDA_QRK_ROUTE_SPKR2_DAC) != 0u) {
            uint8_t alternate_dac = 0u;

            if (mk_audio_azalia_find_alternate_output_dac(g_audio_state.azalia_speaker2_pin_nid,
                                                          target_dac,
                                                          &target_dac,
                                                          1u,
                                                          &alternate_dac) == 0 &&
                alternate_dac != 0u &&
                alternate_dac != target_dac) {
                target_dac = alternate_dac;
            }
        }

        if (mk_audio_azalia_resolve_output_path(g_audio_state.azalia_speaker2_pin_nid,
                                                target_dac,
                                                0u,
                                                path,
                                                path_indices,
                                                8u,
                                                &path_len) == 0) {
            (void)mk_audio_azalia_retarget_pin_to_dac(g_audio_state.azalia_speaker2_pin_nid, target_dac);
            g_audio_state.azalia_speaker2_dac_nid = target_dac;
        }
    }

    g_audio_state.azalia_spkr_dac_nid = speaker_dac;
}

static void mk_audio_azalia_rebalance_output_dacs(void) {
    mk_audio_azalia_select_primary_output_dacs();
    mk_audio_azalia_select_speaker_dac();

    if (g_audio_state.azalia_output_pin_nids[0] != 0u) {
        g_audio_state.azalia_output_pin_nid = g_audio_state.azalia_output_pin_nids[0];
    }
    if (g_audio_state.azalia_output_dac_nids[0] != 0u) {
        g_audio_state.azalia_output_dac_nid = g_audio_state.azalia_output_dac_nids[0];
    }
}

static void mk_audio_azalia_commit_output_routes(void) {
    uint8_t path[8];
    uint8_t path_indices[8];
    uint8_t ordered_bits[4];
    uint32_t ordered_count;
    uint32_t path_len = 0u;

    ordered_count = mk_audio_azalia_collect_output_order(ordered_bits, 4u);
    for (uint32_t order = 0u; order < ordered_count; ++order) {
        int bit = (int)ordered_bits[order];
        uint8_t pin_nid = g_audio_state.azalia_output_pin_nids[bit];
        uint8_t dac_nid = g_audio_state.azalia_output_dac_nids[bit];

        if (!mk_audio_azalia_output_candidate_available(bit) ||
            pin_nid == 0u || dac_nid == 0u) {
            continue;
        }
        if (mk_audio_azalia_resolve_output_path(pin_nid,
                                                dac_nid,
                                                0u,
                                                path,
                                                path_indices,
                                                8u,
                                                &path_len) != 0) {
            continue;
        }
        (void)mk_audio_azalia_apply_output_path(path, path_indices, path_len, 1u, 0u, 0u);
    }
    if (g_audio_state.azalia_speaker2_pin_nid != 0u &&
        g_audio_state.azalia_speaker2_dac_nid != 0u &&
        mk_audio_azalia_resolve_output_path(g_audio_state.azalia_speaker2_pin_nid,
                                            g_audio_state.azalia_speaker2_dac_nid,
                                            0u,
                                            path,
                                            path_indices,
                                            8u,
                                            &path_len) == 0) {
        (void)mk_audio_azalia_apply_output_path(path, path_indices, path_len, 1u, 0u, 0u);
    }
}

static void mk_audio_azalia_apply_secondary_speaker_path(void) {
    uint8_t path[8];
    uint8_t path_indices[8];
    uint32_t path_len = 0u;

    if (g_audio_state.azalia_speaker2_pin_nid == 0u || g_audio_state.azalia_speaker2_dac_nid == 0u) {
        return;
    }
    if (mk_audio_azalia_resolve_output_path(g_audio_state.azalia_speaker2_pin_nid,
                                            g_audio_state.azalia_speaker2_dac_nid,
                                            0u,
                                            path,
                                            path_indices,
                                            8u,
                                            &path_len) != 0) {
        return;
    }
    (void)mk_audio_azalia_apply_output_path(path, path_indices, path_len, 1u, 1u, 1u);
    (void)mk_audio_azalia_bind_dac_stream(g_audio_state.azalia_speaker2_dac_nid, 0u);
}

static void mk_audio_azalia_disconnect_output_stream(void) {
    uint8_t disconnected[8];
    uint32_t disconnected_count = 0u;
    uint8_t candidates[8];
    uint32_t candidate_count = 0u;

    if (!g_audio_state.azalia_codec_probed) {
        return;
    }

    for (uint32_t bit = 0u; bit < 4u && candidate_count < 8u; ++bit) {
        uint8_t dac_nid = g_audio_state.azalia_output_dac_nids[bit];

        if (dac_nid != 0u) {
            candidates[candidate_count++] = dac_nid;
        }
    }
    if (g_audio_state.azalia_output_dac_nid != 0u && candidate_count < 8u) {
        candidates[candidate_count++] = g_audio_state.azalia_output_dac_nid;
    }
    if (g_audio_state.azalia_speaker2_dac_nid != 0u && candidate_count < 8u) {
        candidates[candidate_count++] = g_audio_state.azalia_speaker2_dac_nid;
    }
    if (g_audio_state.azalia_fhp_dac_nid != 0u && candidate_count < 8u) {
        candidates[candidate_count++] = g_audio_state.azalia_fhp_dac_nid;
    }

    for (uint32_t i = 0u; i < candidate_count; ++i) {
        uint8_t dac_nid = candidates[i];
        uint8_t seen = 0u;
        uint32_t response = 0u;

        for (uint32_t j = 0u; j < disconnected_count; ++j) {
            if (disconnected[j] == dac_nid) {
                seen = 1u;
                break;
            }
        }
        if (seen) {
            continue;
        }
        disconnected[disconnected_count++] = dac_nid;
        (void)mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                      dac_nid,
                                      HDA_VERB_SET_STREAM_CHANNEL,
                                      0u,
                                      &response);
    }
}

static int mk_audio_azalia_bind_dac_stream(uint8_t dac_nid, uint8_t channel) {
    uint32_t response = 0u;

    if (dac_nid == 0u) {
        return -1;
    }
    mk_audio_azalia_power_widget(dac_nid);
    mk_audio_azalia_program_widget_amp(dac_nid, 0u, 0u);
    if (mk_audio_azalia_command_raw(g_audio_state.azalia_codec_address,
                                    dac_nid,
                                    ((uint32_t)HDA_VERB_SET_CONVERTER_FORMAT << 16) |
                                        (uint32_t)g_audio_state.azalia_output_fmt,
                                    &response) != 0) {
        return -1;
    }
    if (mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                dac_nid,
                                HDA_VERB_SET_STREAM_CHANNEL,
                                (uint8_t)((g_audio_state.azalia_output_stream_number << 4) |
                                          (channel & 0x0fu)),
                                &response) != 0) {
        return -1;
    }
    return 0;
}

static int mk_audio_azalia_is_ad1981_oamp_widget(uint8_t nid) {
    if ((g_audio_state.azalia_quirks & MK_AUDIO_HDA_QRK_AD1981_OAMP) == 0u) {
        return 0;
    }
    return nid == 0x05u || nid == 0x06u || nid == 0x07u || nid == 0x09u || nid == 0x18u;
}

static int mk_audio_azalia_widget_has_effective_outamp(uint8_t nid, uint32_t caps) {
    if ((caps & HDA_WCAP_OUTAMP) != 0u) {
        return 1;
    }
    return mk_audio_azalia_is_ad1981_oamp_widget(nid);
}

static uint8_t mk_audio_azalia_speaker_mute_method(void) {
    uint8_t pin_nid = g_audio_state.azalia_output_pin_nids[0];
    uint8_t dac_nid = g_audio_state.azalia_spkr_dac_nid;
    uint32_t caps = 0u;
    uint32_t pin_caps = 0u;

    if (pin_nid == 0u || dac_nid == 0u || g_audio_state.azalia_sense_pin_count == 0u) {
        return MK_AUDIO_HDA_SPKR_MUTE_NONE;
    }
    if (mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                pin_nid,
                                HDA_VERB_GET_PARAMETER,
                                HDA_PARAM_AUDIO_WIDGET_CAP,
                                &caps) == 0 &&
        mk_audio_azalia_widget_has_effective_outamp(pin_nid, caps)) {
        return MK_AUDIO_HDA_SPKR_MUTE_PIN_AMP;
    }
    if (mk_audio_azalia_get_parameter_retry(g_audio_state.azalia_codec_address,
                                            pin_nid,
                                            HDA_PARAM_PIN_CAP,
                                            &pin_caps,
                                            3u) == 0 &&
        (pin_caps & HDA_PINCAP_OUTPUT) != 0u &&
        (pin_caps & HDA_PINCAP_INPUT) != 0u) {
        return MK_AUDIO_HDA_SPKR_MUTE_PIN_CTL;
    }
    if (mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                dac_nid,
                                HDA_VERB_GET_PARAMETER,
                                HDA_PARAM_AUDIO_WIDGET_CAP,
                                &caps) == 0 &&
        mk_audio_azalia_widget_has_effective_outamp(dac_nid, caps)) {
        for (int bit = 1; bit < 4; ++bit) {
            if (g_audio_state.azalia_output_dac_nids[bit] == dac_nid) {
                return MK_AUDIO_HDA_SPKR_MUTE_NONE;
            }
        }
        return MK_AUDIO_HDA_SPKR_MUTE_DAC_AMP;
    }
    return MK_AUDIO_HDA_SPKR_MUTE_NONE;
}

static void mk_audio_azalia_apply_speaker_mute(uint8_t muted) {
    uint8_t pin_nid = g_audio_state.azalia_output_pin_nids[0];
    uint8_t dac_nid = g_audio_state.azalia_spkr_dac_nid;
    uint8_t speaker2_pin_nid = g_audio_state.azalia_speaker2_pin_nid;

    switch (mk_audio_azalia_speaker_mute_method()) {
    case MK_AUDIO_HDA_SPKR_MUTE_PIN_AMP:
        mk_audio_azalia_program_widget_amp_state(pin_nid, 0u, 0u, muted);
        if (speaker2_pin_nid != 0u) {
            mk_audio_azalia_program_widget_amp_state(speaker2_pin_nid, 0u, 0u, muted);
        }
        break;
    case MK_AUDIO_HDA_SPKR_MUTE_PIN_CTL:
        mk_audio_azalia_set_output_pin_enabled(pin_nid, 0, (uint8_t)(muted == 0u));
        if (speaker2_pin_nid != 0u) {
            mk_audio_azalia_set_output_pin_enabled(speaker2_pin_nid, 0, (uint8_t)(muted == 0u));
        }
        break;
    case MK_AUDIO_HDA_SPKR_MUTE_DAC_AMP:
        mk_audio_azalia_program_widget_amp_state(dac_nid, 0u, 0u, muted);
        break;
    default:
        break;
    }
}

static void mk_audio_azalia_detect_quirks(void) {
    uint32_t codec_id = g_audio_state.azalia_vendor_id;
    uint32_t subid = g_audio_state.azalia_subsystem_id;
    uint32_t quirks = 0u;

    switch (codec_id) {
    case 0x11d41981u:
        quirks |= MK_AUDIO_HDA_QRK_AD1981_OAMP;
        break;
    case 0x10134206u:
        if (subid == 0xcb8910deu || subid == 0x72708086u || subid == 0xcb7910deu) {
            quirks |= MK_AUDIO_HDA_QRK_GPIO_UNMUTE_1 | MK_AUDIO_HDA_QRK_GPIO_UNMUTE_3;
        }
        break;
    case 0x10134208u:
        if (subid == 0x72708086u) {
            quirks |= MK_AUDIO_HDA_QRK_GPIO_UNMUTE_0 | MK_AUDIO_HDA_QRK_GPIO_UNMUTE_1;
        }
        break;
    case 0x10ec0260u:
        if (subid == 0x008f1025u) {
            quirks |= MK_AUDIO_HDA_QRK_GPIO_UNMUTE_0;
        }
        break;
    case 0x10ec0269u:
        if (subid == 0x21f317aau || subid == 0x21f617aau || subid == 0x21fa17aau ||
            subid == 0x21fb17aau || subid == 0x220317aau || subid == 0x220817aau) {
            quirks |= MK_AUDIO_HDA_QRK_TPDOCK1;
        }
        break;
    case 0x10ec0285u:
        if (subid == 0x229217aau) {
            quirks |= MK_AUDIO_HDA_QRK_CLOSE_PCBEEP |
                      MK_AUDIO_HDA_QRK_ROUTE_SPKR2_DAC;
        } else if (subid == 0x22c017aau) {
            quirks |= MK_AUDIO_HDA_QRK_ROUTE_SPKR2_DAC;
        }
        break;
    case 0x10ec0292u:
        if (subid == 0x220c17aau || subid == 0x220e17aau || subid == 0x221017aau ||
            subid == 0x221217aau || subid == 0x221417aau || subid == 0x222617aau ||
            subid == 0x501e17aau || subid == 0x503417aau || subid == 0x503617aau ||
            subid == 0x503c17aau) {
            quirks |= MK_AUDIO_HDA_QRK_TPDOCK2;
        }
        break;
    case 0x10ec0660u:
        if (subid == 0x13391043u) {
            quirks |= MK_AUDIO_HDA_QRK_GPIO_UNMUTE_0;
        }
        break;
    case 0x10ec0880u:
        if (subid == 0x19931043u || subid == 0x13231043u) {
            quirks |= MK_AUDIO_HDA_QRK_GPIO_UNMUTE_0;
        }
        if (subid == 0x203d161fu) {
            quirks |= MK_AUDIO_HDA_QRK_GPIO_UNMUTE_1;
        }
        break;
    case 0x10ec0882u:
        if (subid == 0x13c21043u || subid == 0x19711043u) {
            quirks |= MK_AUDIO_HDA_QRK_GPIO_UNMUTE_0;
        }
        break;
    case 0x10ec0883u:
        if (subid == 0x00981025u) {
            quirks |= MK_AUDIO_HDA_QRK_GPIO_UNMUTE_0 | MK_AUDIO_HDA_QRK_GPIO_UNMUTE_1;
        }
        break;
    case 0x10ec0885u:
        if (subid == 0x00a1106bu || subid == 0xcb7910deu ||
            subid == 0x00a0106bu || subid == 0x00a3106bu) {
            quirks |= MK_AUDIO_HDA_QRK_GPIO_UNMUTE_0;
        }
        break;
    case 0x111d7603u:
    case 0x111d7608u:
        if ((subid & 0xffffu) == PCI_SUBVENDOR_HP) {
            quirks |= MK_AUDIO_HDA_QRK_GPIO_UNMUTE_0;
        }
        break;
    case 0x111d7675u:
        if ((subid & 0xffffu) == PCI_SUBVENDOR_DELL) {
            quirks |= MK_AUDIO_HDA_QRK_GPIO_UNMUTE_0;
        }
        break;
    case 0x111d76b2u:
        if ((subid & 0xffffu) == PCI_SUBVENDOR_DELL ||
            (subid & 0xffffu) == PCI_SUBVENDOR_HP) {
            quirks |= MK_AUDIO_HDA_QRK_GPIO_UNMUTE_0;
        }
        break;
    case 0x14f1506eu:
        if (subid == 0x20f217aau || subid == 0x215e17aau || subid == 0x215f17aau ||
            subid == 0x21ce17aau || subid == 0x21cf17aau || subid == 0x21da17aau ||
            subid == 0x21db17aau) {
            quirks |= MK_AUDIO_HDA_QRK_TPDOCK3;
        }
        break;
    case 0x83847616u:
        if (subid == 0x02271028u || subid == 0x01f31028u) {
            quirks |= MK_AUDIO_HDA_QRK_GPIO_UNMUTE_2;
        }
        break;
    case 0x83847680u:
        if (subid == 0x76808384u) {
            quirks |= MK_AUDIO_HDA_QRK_GPIO_POL_0 |
                      MK_AUDIO_HDA_QRK_GPIO_UNMUTE_0 |
                      MK_AUDIO_HDA_QRK_GPIO_UNMUTE_1;
        }
        break;
    case 0x838476a0u:
        if (subid == 0x01f91028u || subid == 0x02281028u) {
            quirks |= MK_AUDIO_HDA_QRK_GPIO_UNMUTE_0;
        }
        break;
    default:
        break;
    }

    g_audio_state.azalia_quirks = quirks;
    if (quirks != 0u) {
        kernel_debug_printf("audio: hda quirks codec=%x subid=%x flags=%x\n",
                            (unsigned int)codec_id,
                            (unsigned int)subid,
                            (unsigned int)quirks);
    }
}

static void mk_audio_azalia_apply_gpio_quirks(void) {
    uint32_t data = 0u;
    uint32_t mask = 0u;
    uint32_t dir = 0u;
    uint32_t response = 0u;

    if (!g_audio_state.azalia_codec_probed || g_audio_state.azalia_afg_nid == 0u ||
        g_audio_state.azalia_quirks == 0u) {
        return;
    }
    if ((g_audio_state.azalia_quirks & (MK_AUDIO_HDA_QRK_GPIO_UNMUTE_0 |
                                        MK_AUDIO_HDA_QRK_GPIO_UNMUTE_1 |
                                        MK_AUDIO_HDA_QRK_GPIO_UNMUTE_2 |
                                        MK_AUDIO_HDA_QRK_GPIO_UNMUTE_3 |
                                        MK_AUDIO_HDA_QRK_GPIO_POL_0)) == 0u) {
        return;
    }

    if ((g_audio_state.azalia_quirks & MK_AUDIO_HDA_QRK_GPIO_POL_0) != 0u) {
        (void)mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                      g_audio_state.azalia_afg_nid,
                                      HDA_VERB_SET_GPIO_POLARITY,
                                      0u,
                                      &response);
    }
    if (mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                g_audio_state.azalia_afg_nid,
                                HDA_VERB_GET_GPIO_DATA,
                                0u,
                                &data) != 0 ||
        mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                g_audio_state.azalia_afg_nid,
                                HDA_VERB_GET_GPIO_ENABLE_MASK,
                                0u,
                                &mask) != 0 ||
        mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                g_audio_state.azalia_afg_nid,
                                HDA_VERB_GET_GPIO_DIRECTION,
                                0u,
                                &dir) != 0) {
        return;
    }
    if ((g_audio_state.azalia_quirks & MK_AUDIO_HDA_QRK_GPIO_UNMUTE_0) != 0u) {
        data |= 1u << 0u;
        mask |= 1u << 0u;
        dir |= 1u << 0u;
    }
    if ((g_audio_state.azalia_quirks & MK_AUDIO_HDA_QRK_GPIO_UNMUTE_1) != 0u) {
        data |= 1u << 1u;
        mask |= 1u << 1u;
        dir |= 1u << 1u;
    }
    if ((g_audio_state.azalia_quirks & MK_AUDIO_HDA_QRK_GPIO_UNMUTE_2) != 0u) {
        data |= 1u << 2u;
        mask |= 1u << 2u;
        dir |= 1u << 2u;
    }
    if ((g_audio_state.azalia_quirks & MK_AUDIO_HDA_QRK_GPIO_UNMUTE_3) != 0u) {
        data |= 1u << 3u;
        mask |= 1u << 3u;
        dir |= 1u << 3u;
    }
    (void)mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                  g_audio_state.azalia_afg_nid,
                                  HDA_VERB_SET_GPIO_ENABLE_MASK,
                                  (uint8_t)(mask & 0xffu),
                                  &response);
    (void)mk_audio_azalia_command_raw(g_audio_state.azalia_codec_address,
                                      g_audio_state.azalia_afg_nid,
                                      ((uint32_t)HDA_VERB_SET_GPIO_ENABLE_MASK << 8) | (mask & 0xffu),
                                      &response);
    (void)mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                  g_audio_state.azalia_afg_nid,
                                  HDA_VERB_SET_GPIO_DIRECTION,
                                  (uint8_t)(dir & 0xffu),
                                  &response);
    (void)mk_audio_azalia_command_raw(g_audio_state.azalia_codec_address,
                                      g_audio_state.azalia_afg_nid,
                                      ((uint32_t)HDA_VERB_SET_GPIO_DIRECTION << 8) | (dir & 0xffu),
                                      &response);
    mk_audio_compat_delay();
    (void)mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                  g_audio_state.azalia_afg_nid,
                                  HDA_VERB_SET_GPIO_DATA,
                                  (uint8_t)(data & 0xffu),
                                  &response);
    (void)mk_audio_azalia_command_raw(g_audio_state.azalia_codec_address,
                                      g_audio_state.azalia_afg_nid,
                                      ((uint32_t)HDA_VERB_SET_GPIO_DATA << 8) | (data & 0xffu),
                                      &response);
}

static void mk_audio_azalia_apply_processing_quirks(void) {
    uint32_t response = 0u;

    if ((g_audio_state.azalia_quirks & MK_AUDIO_HDA_QRK_CLOSE_PCBEEP) == 0u) {
        return;
    }
    (void)mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                  0x20u,
                                  HDA_VERB_SET_COEFFICIENT_INDEX,
                                  0x36u,
                                  &response);
    (void)mk_audio_azalia_command_raw(g_audio_state.azalia_codec_address,
                                      0x20u,
                                      ((uint32_t)HDA_VERB_SET_PROCESSING_COEFFICIENT << 8) | 0x57d7u,
                                      &response);
}

static void mk_audio_azalia_apply_widget_quirks(uint8_t nid, uint32_t *config_default) {
    if (config_default == 0 || g_audio_state.azalia_quirks == 0u) {
        return;
    }

    if ((g_audio_state.azalia_quirks & MK_AUDIO_HDA_QRK_TPDOCK1) != 0u) {
        if (nid == 0x19u) {
            *config_default = 0x23a11040u;
            return;
        }
        if (nid == 0x1bu) {
            *config_default = 0x2121103fu;
            return;
        }
    }
    if ((g_audio_state.azalia_quirks & MK_AUDIO_HDA_QRK_TPDOCK2) != 0u) {
        if (nid == 0x16u) {
            *config_default = 0x21211010u;
            return;
        }
        if (nid == 0x19u) {
            *config_default = 0x21a11010u;
            return;
        }
    }
    if ((g_audio_state.azalia_quirks & MK_AUDIO_HDA_QRK_TPDOCK3) != 0u) {
        if (nid == 0x1au) {
            *config_default = 0x21a190f0u;
            return;
        }
        if (nid == 0x1cu) {
            *config_default = 0x212140ffu;
            return;
        }
    }
}

static void mk_audio_azalia_register_input_path(uint32_t input_mask, uint8_t pin_nid) {
    if ((input_mask & 0x1u) != 0u && g_audio_state.azalia_input_pin_nids[0] == 0u) {
        g_audio_state.azalia_input_pin_nids[0] = pin_nid;
    }
    if ((input_mask & 0x2u) != 0u && g_audio_state.azalia_input_pin_nids[1] == 0u) {
        g_audio_state.azalia_input_pin_nids[1] = pin_nid;
    }
}

static uint32_t mk_audio_hda_config_device(uint32_t config_default) {
    return (config_default >> HDA_CONFIG_DEVICE_SHIFT) & HDA_CONFIG_DEVICE_MASK;
}

static uint32_t mk_audio_hda_config_port(uint32_t config_default) {
    return (config_default >> HDA_CONFIG_PORT_SHIFT) & HDA_CONFIG_PORT_MASK;
}

static uint32_t mk_audio_hda_config_location(uint32_t config_default) {
    return (config_default >> HDA_CONFIG_LOC_SHIFT) & HDA_CONFIG_LOC_MASK;
}

static uint32_t mk_audio_hda_config_misc(uint32_t config_default) {
    return (config_default >> HDA_CONFIG_MISC_SHIFT) & HDA_CONFIG_MISC_MASK;
}

static uint32_t mk_audio_hda_config_association(uint32_t config_default) {
    return (config_default >> HDA_CONFIG_ASSOC_SHIFT) & HDA_CONFIG_ASSOC_MASK;
}

static uint32_t mk_audio_hda_config_sequence(uint32_t config_default) {
    return (config_default >> HDA_CONFIG_SEQ_SHIFT) & HDA_CONFIG_SEQ_MASK;
}

static uint32_t mk_audio_hda_output_sort_key(uint32_t config_default) {
    uint32_t device = mk_audio_hda_config_device(config_default);
    uint32_t location = mk_audio_hda_config_location(config_default);
    uint32_t association = mk_audio_hda_config_association(config_default);
    uint32_t sequence = mk_audio_hda_config_sequence(config_default);
    uint32_t key = ((association & 0x0fu) << 4) | (sequence & 0x0fu);
    uint32_t secondary_default = 0u;
    uint32_t loc = 0u;

    if (device == HDA_CONFIG_DEVICE_MICIN || device == HDA_CONFIG_DEVICE_LINEIN) {
        secondary_default = 1u;
    }
    if (g_audio_state.azalia_analog_dac_count >= 3u &&
        g_audio_state.azalia_output_jack_count < 3u) {
        loc = location & 0x3fu;
    }
    key |= secondary_default << 8;
    key |= loc << 9;
    return key;
}

static uint32_t mk_audio_hda_output_mask(uint32_t pin_caps, uint32_t config_default) {
    uint32_t device;
    uint32_t port;

    if ((pin_caps & HDA_PINCAP_OUTPUT) == 0u) {
        return 0u;
    }

    device = mk_audio_hda_config_device(config_default);
    port = mk_audio_hda_config_port(config_default);
    if (port == HDA_CONFIG_PORT_NONE) {
        return 0u;
    }
    switch (device) {
    case HDA_CONFIG_DEVICE_HEADPHONE:
        return 0x2u;
    case HDA_CONFIG_DEVICE_SPEAKER:
        return 0x1u;
    case HDA_CONFIG_DEVICE_LINEOUT:
        return 0x4u;
    case HDA_CONFIG_DEVICE_SPDIFOUT:
    case HDA_CONFIG_DEVICE_DIGITALOUT:
        return 0x8u;
    default:
        break;
    }

    if ((pin_caps & HDA_PINCAP_HDMI) != 0u) {
        return 0x8u;
    }
    if ((pin_caps & HDA_PINCAP_HEADPHONE) != 0u) {
        return 0x2u;
    }
    return 0x1u;
}

static uint32_t mk_audio_hda_input_mask(uint32_t pin_caps, uint32_t config_default) {
    uint32_t device;
    uint32_t port;

    if ((pin_caps & HDA_PINCAP_INPUT) == 0u) {
        return 0u;
    }

    device = mk_audio_hda_config_device(config_default);
    port = mk_audio_hda_config_port(config_default);
    if (port == HDA_CONFIG_PORT_NONE) {
        return 0u;
    }
    if (device == HDA_CONFIG_DEVICE_LINEIN) {
        return 0x2u;
    }
    if (device == HDA_CONFIG_DEVICE_MICIN) {
        return 0x1u;
    }
    return 0x1u;
}

static int mk_audio_hda_output_priority(uint32_t pin_caps, uint32_t config_default) {
    uint32_t device = mk_audio_hda_config_device(config_default);
    uint32_t port = mk_audio_hda_config_port(config_default);
    uint32_t association = mk_audio_hda_config_association(config_default);
    uint32_t sequence = mk_audio_hda_config_sequence(config_default);
    uint32_t location = mk_audio_hda_config_location(config_default);
    int priority;

    if ((pin_caps & HDA_PINCAP_OUTPUT) == 0u) {
        return -1;
    }
    if (port == HDA_CONFIG_PORT_NONE) {
        return -1;
    }
    switch (device) {
    case HDA_CONFIG_DEVICE_SPEAKER:
        priority = 50;
        break;
    case HDA_CONFIG_DEVICE_HEADPHONE:
        priority = 40;
        break;
    case HDA_CONFIG_DEVICE_LINEOUT:
        priority = 30;
        break;
    case HDA_CONFIG_DEVICE_SPDIFOUT:
    case HDA_CONFIG_DEVICE_DIGITALOUT:
        priority = 20;
        break;
    default:
        if ((pin_caps & HDA_PINCAP_HDMI) != 0u) {
            priority = 20;
        } else if ((pin_caps & HDA_PINCAP_HEADPHONE) != 0u) {
            priority = 40;
        } else {
            priority = 5;
        }
        break;
    }
    if (port == HDA_CONFIG_PORT_FIXED) {
        priority += 8;
    } else if (port == HDA_CONFIG_PORT_JACK) {
        priority += 4;
    } else if (port == HDA_CONFIG_PORT_BOTH) {
        priority += 2;
    }
    if (association != 0u) {
        priority += 6;
        if (association == 1u) {
            priority += 4;
        }
    }
    if (sequence < 4u) {
        priority += (int)(4u - sequence);
    }
    if (device == HDA_CONFIG_DEVICE_HEADPHONE && location == 0x02u) {
        priority += 3;
    }
    if (device == HDA_CONFIG_DEVICE_SPEAKER && location == 0x01u) {
        priority += 3;
    }
    return priority;
}

static uint32_t mk_audio_backend_feature_flags(void) {
    uint32_t flags = 0u;

    if (g_audio_state.backend_kind == MK_AUDIO_BACKEND_COMPAT_AUICH) {
        flags |= MK_AUDIO_FEATURE_COMPAT_AUICH;
    }
    if (g_audio_state.compat_irq_registered || g_audio_state.azalia_irq_registered) {
        flags |= MK_AUDIO_FEATURE_IRQ_REGISTERED;
    }
    if ((g_audio_state.compat_ready || g_audio_state.azalia_ready) &&
        (g_audio_state.pci.irq_line >= 16u ||
         (!g_audio_state.compat_irq_registered && !g_audio_state.azalia_irq_registered))) {
        flags |= MK_AUDIO_FEATURE_NO_VALID_IRQ;
    }
    if ((g_audio_state.info.flags & MK_AUDIO_CAPS_CAPTURE) != 0u) {
        flags |= MK_AUDIO_FEATURE_CAPTURE;
    }
    if (g_audio_state.compat_irq_count != 0u || g_audio_state.azalia_irq_count != 0u) {
        flags |= MK_AUDIO_FEATURE_IRQ_SEEN;
    }
    if (g_audio_state.playback_starvations != 0u) {
        flags |= MK_AUDIO_FEATURE_STARVATION;
    }
    if (g_audio_state.playback_underruns != 0u) {
        flags |= MK_AUDIO_FEATURE_UNDERRUN;
    }
    if (g_audio_state.compat_aud_is_mmio || g_audio_state.compat_mix_is_mmio || g_audio_state.azalia_ready) {
        flags |= MK_AUDIO_FEATURE_MMIO_TRANSPORT;
    }
    if (g_audio_state.compat_ignore_codecready) {
        flags |= MK_AUDIO_FEATURE_CODECREADY_QUIRK;
    }
    if ((g_audio_state.compat_ext_audio_id &
         (AC97_EXT_AUDIO_SDAC | AC97_EXT_AUDIO_CDAC | AC97_EXT_AUDIO_LDAC)) != 0u) {
        flags |= MK_AUDIO_FEATURE_MULTICHANNEL;
    }
    if (g_audio_state.compat_ready && g_audio_state.compat_codec_ready &&
        (g_audio_state.info.flags & MK_AUDIO_CAPS_CAPTURE) != 0u) {
        flags |= MK_AUDIO_FEATURE_CAPTURE_DMA;
    }
    if (g_audio_state.azalia_corb_ready) {
        flags |= MK_AUDIO_FEATURE_CORB_RIRB;
    }
    if (g_audio_state.azalia_codec_probed) {
        flags |= MK_AUDIO_FEATURE_CODEC_PROBE;
    }
    if (g_audio_state.azalia_widget_probed) {
        flags |= MK_AUDIO_FEATURE_WIDGET_PROBE;
    }
    if (g_audio_state.azalia_path_programmed) {
        flags |= MK_AUDIO_FEATURE_PATH_PROGRAMMED;
    }
    if (g_audio_state.backend_kind == MK_AUDIO_BACKEND_PCSPKR) {
        flags |= MK_AUDIO_FEATURE_MMIO_TRANSPORT;
    }
    if (kernel_usb_audio_class_probe_count() != 0u) {
        flags |= MK_AUDIO_FEATURE_USB_CLASS_DETECTED;
    }
    if (kernel_usb_audio_probe_configured_ready_count() != 0u) {
        flags |= MK_AUDIO_FEATURE_USB_CONFIGURED_READY;
    }
    if (g_audio_state.usb_audio_attach_ready) {
        flags |= MK_AUDIO_FEATURE_USB_ATTACH_READY;
    }
    if (g_audio_state.usb_audio_attached_ready) {
        flags |= MK_AUDIO_FEATURE_USB_ATTACHED_READY;
    }
    if (mk_audio_service_ready()) {
        flags |= MK_AUDIO_FEATURE_CONTROL_OWNER_AUDIOSVC;
    }
    flags |= MK_AUDIO_FEATURE_KERNEL_BACKEND_EXECUTOR;
    flags |= MK_AUDIO_FEATURE_UI_PROGRESS_DECOUPLED;
    return flags;
}

static void mk_audio_refresh_usb_attach_snapshot(void) {
    struct kernel_usb_probe_snapshot snapshot;
    uint32_t snapshot_index = 0u;
    uint8_t was_attach_ready = g_audio_state.usb_audio_attach_ready;
    uint8_t was_attached_ready = g_audio_state.usb_audio_attached_ready;
    uint8_t old_addr = g_audio_state.usb_audio_assigned_address;
    uint8_t old_cfg = g_audio_state.usb_audio_configuration_value;
    uint8_t old_transport = g_audio_state.usb_audio_transport_kind;
    uint8_t old_ifaces = g_audio_state.usb_audio_interface_count;
    uint8_t old_eps = g_audio_state.usb_audio_endpoint_count;
    uint8_t old_ac_if = g_audio_state.usb_audio_control_interface_number;
    uint8_t old_as_if = g_audio_state.usb_audio_streaming_interface_number;
    uint8_t old_as_count = g_audio_state.usb_audio_streaming_interface_count;
    uint8_t old_as_alt = g_audio_state.usb_audio_streaming_alt_setting;
    uint8_t old_out_channels = g_audio_state.usb_audio_playback_channel_count;
    uint8_t old_out_subframe = g_audio_state.usb_audio_playback_subframe_size;
    uint8_t old_out_bits = g_audio_state.usb_audio_playback_bit_resolution;
    uint8_t old_out_ep = g_audio_state.usb_audio_playback_endpoint_address;
    uint8_t old_out_attr = g_audio_state.usb_audio_playback_endpoint_attributes;
    uint16_t old_out_maxpkt = g_audio_state.usb_audio_playback_endpoint_max_packet;
    uint32_t old_out_rate = g_audio_state.usb_audio_playback_sample_rate;

    (void)kernel_usb_audio_refresh_probe_state();
    g_audio_state.usb_audio_attach_ready = 0u;
    g_audio_state.usb_audio_attached_ready = 0u;
    g_audio_state.usb_audio_transport_kind = KERNEL_USB_HOST_KIND_UNKNOWN;
    g_audio_state.usb_audio_interface_count = 0u;
    g_audio_state.usb_audio_endpoint_count = 0u;
    g_audio_state.usb_audio_configuration_value = 0u;
    g_audio_state.usb_audio_assigned_address = 0u;
    g_audio_state.usb_audio_control_interface_number = 0xffu;
    g_audio_state.usb_audio_streaming_interface_number = 0xffu;
    g_audio_state.usb_audio_streaming_interface_count = 0u;
    g_audio_state.usb_audio_streaming_alt_setting = 0xffu;
    g_audio_state.usb_audio_playback_channel_count = 0u;
    g_audio_state.usb_audio_playback_subframe_size = 0u;
    g_audio_state.usb_audio_playback_bit_resolution = 0u;
    g_audio_state.usb_audio_playback_endpoint_address = 0xffu;
    g_audio_state.usb_audio_playback_endpoint_attributes = 0u;
    g_audio_state.usb_audio_playback_endpoint_max_packet = 0u;
    g_audio_state.usb_audio_playback_sample_rate = 0u;

    if (kernel_usb_audio_probe_first_configured(&snapshot, &snapshot_index) != 0) {
        return;
    }
    if (snapshot.audio_control_interface_number == 0xffu ||
        snapshot.audio_streaming_interface_number == 0xffu ||
        snapshot.audio_streaming_alt_setting == 0xffu ||
        snapshot.audio_playback_endpoint_address == 0xffu ||
        snapshot.audio_playback_endpoint_max_packet == 0u) {
        return;
    }

    g_audio_state.usb_audio_attach_ready = 1u;
    g_audio_state.usb_audio_interface_count = snapshot.interface_count;
    g_audio_state.usb_audio_endpoint_count = snapshot.endpoint_count;
    g_audio_state.usb_audio_configuration_value = snapshot.configuration_value;
    g_audio_state.usb_audio_assigned_address = snapshot.assigned_address;
    g_audio_state.usb_audio_control_interface_number = snapshot.audio_control_interface_number;
    g_audio_state.usb_audio_streaming_interface_number = snapshot.audio_streaming_interface_number;
    g_audio_state.usb_audio_streaming_interface_count = snapshot.audio_streaming_interface_count;
    g_audio_state.usb_audio_streaming_alt_setting = snapshot.audio_streaming_alt_setting;
    g_audio_state.usb_audio_playback_channel_count = snapshot.audio_playback_channel_count;
    g_audio_state.usb_audio_playback_subframe_size = snapshot.audio_playback_subframe_size;
    g_audio_state.usb_audio_playback_bit_resolution = snapshot.audio_playback_bit_resolution;
    g_audio_state.usb_audio_playback_endpoint_address = snapshot.audio_playback_endpoint_address;
    g_audio_state.usb_audio_playback_endpoint_attributes = snapshot.audio_playback_endpoint_attributes;
    g_audio_state.usb_audio_playback_endpoint_max_packet = snapshot.audio_playback_endpoint_max_packet;
    g_audio_state.usb_audio_playback_sample_rate = snapshot.audio_playback_sample_rate;

    if (!was_attach_ready ||
        old_addr != g_audio_state.usb_audio_assigned_address ||
        old_cfg != g_audio_state.usb_audio_configuration_value ||
        old_ifaces != g_audio_state.usb_audio_interface_count ||
        old_eps != g_audio_state.usb_audio_endpoint_count ||
        old_ac_if != g_audio_state.usb_audio_control_interface_number ||
        old_as_if != g_audio_state.usb_audio_streaming_interface_number ||
        old_as_count != g_audio_state.usb_audio_streaming_interface_count ||
        old_as_alt != g_audio_state.usb_audio_streaming_alt_setting ||
        old_out_channels != g_audio_state.usb_audio_playback_channel_count ||
        old_out_subframe != g_audio_state.usb_audio_playback_subframe_size ||
        old_out_bits != g_audio_state.usb_audio_playback_bit_resolution ||
        old_out_ep != g_audio_state.usb_audio_playback_endpoint_address ||
        old_out_attr != g_audio_state.usb_audio_playback_endpoint_attributes ||
        old_out_maxpkt != g_audio_state.usb_audio_playback_endpoint_max_packet ||
        old_out_rate != g_audio_state.usb_audio_playback_sample_rate) {
        g_audio_state.usb_audio_attach_attempted = 0u;
    }

    if (!g_audio_state.usb_audio_attach_attempted) {
        g_audio_state.usb_audio_attach_attempted = 1u;
    }
    if (kernel_usb_audio_probe_attached_ready_count() != 0u) {
        g_audio_state.usb_audio_attached_ready = 1u;
        g_audio_state.usb_audio_transport_kind = kernel_usb_audio_playback_transport_kind();
    } else {
        struct kernel_usb_probe_snapshot attached_snapshot;
        uint32_t attached_index = 0u;

        if (kernel_usb_audio_probe_attach_first_configured(&attached_snapshot, &attached_index) == 0) {
            g_audio_state.usb_audio_attached_ready = 1u;
            g_audio_state.usb_audio_transport_kind = kernel_usb_audio_playback_transport_kind();
            if (!was_attached_ready) {
                kernel_debug_printf("audio: compat-uaudio attached-ready probe=%u addr=%u cfg=%u as_if=%u as_alt=%u ch=%u bits=%u rate=%u out_ep=%u maxpkt=%u\n",
                                    (unsigned int)attached_index,
                                    (unsigned int)attached_snapshot.assigned_address,
                                    (unsigned int)attached_snapshot.configuration_value,
                                    attached_snapshot.audio_streaming_interface_number == 0xffu ?
                                        0xffffffffu :
                                        (unsigned int)attached_snapshot.audio_streaming_interface_number,
                                    attached_snapshot.audio_streaming_alt_setting == 0xffu ?
                                        0xffffffffu :
                                        (unsigned int)attached_snapshot.audio_streaming_alt_setting,
                                    (unsigned int)attached_snapshot.audio_playback_channel_count,
                                    (unsigned int)attached_snapshot.audio_playback_bit_resolution,
                                    (unsigned int)attached_snapshot.audio_playback_sample_rate,
                                    attached_snapshot.audio_playback_endpoint_address == 0xffu ?
                                        0xffffffffu :
                                        (unsigned int)attached_snapshot.audio_playback_endpoint_address,
                                    (unsigned int)attached_snapshot.audio_playback_endpoint_max_packet);
            }
        }
    }

    if (!was_attach_ready ||
        old_addr != g_audio_state.usb_audio_assigned_address ||
        old_cfg != g_audio_state.usb_audio_configuration_value ||
        old_transport != g_audio_state.usb_audio_transport_kind ||
        old_ifaces != g_audio_state.usb_audio_interface_count ||
        old_eps != g_audio_state.usb_audio_endpoint_count ||
        old_ac_if != g_audio_state.usb_audio_control_interface_number ||
        old_as_if != g_audio_state.usb_audio_streaming_interface_number ||
        old_as_count != g_audio_state.usb_audio_streaming_interface_count ||
        old_as_alt != g_audio_state.usb_audio_streaming_alt_setting ||
        old_out_channels != g_audio_state.usb_audio_playback_channel_count ||
        old_out_subframe != g_audio_state.usb_audio_playback_subframe_size ||
        old_out_bits != g_audio_state.usb_audio_playback_bit_resolution ||
        old_out_ep != g_audio_state.usb_audio_playback_endpoint_address ||
        old_out_attr != g_audio_state.usb_audio_playback_endpoint_attributes ||
        old_out_maxpkt != g_audio_state.usb_audio_playback_endpoint_max_packet ||
        old_out_rate != g_audio_state.usb_audio_playback_sample_rate) {
        kernel_debug_printf("audio: compat-uaudio attach-ready idx=%x addr=%x cfg=%x ifaces=%x eps=%x ac_if=%x as_if=%x as_count=%x as_alt=%x ch=%x bits=%x rate=%x out_ep=%x attr=%x maxpkt=%x\n",
                            (unsigned int)snapshot_index,
                            (unsigned int)g_audio_state.usb_audio_assigned_address,
                            (unsigned int)g_audio_state.usb_audio_configuration_value,
                            (unsigned int)g_audio_state.usb_audio_interface_count,
                            (unsigned int)g_audio_state.usb_audio_endpoint_count,
                            (unsigned int)g_audio_state.usb_audio_control_interface_number,
                            (unsigned int)g_audio_state.usb_audio_streaming_interface_number,
                            (unsigned int)g_audio_state.usb_audio_streaming_interface_count,
                            (unsigned int)g_audio_state.usb_audio_streaming_alt_setting,
                            (unsigned int)g_audio_state.usb_audio_playback_channel_count,
                            (unsigned int)g_audio_state.usb_audio_playback_bit_resolution,
                            (unsigned int)g_audio_state.usb_audio_playback_sample_rate,
                            (unsigned int)g_audio_state.usb_audio_playback_endpoint_address,
                            (unsigned int)g_audio_state.usb_audio_playback_endpoint_attributes,
                            (unsigned int)g_audio_state.usb_audio_playback_endpoint_max_packet);
    }
}

static void mk_audio_maybe_promote_uaudio_backend(void) {
    static uint8_t promotion_in_progress = 0u;

    if (promotion_in_progress) {
        return;
    }
    if (g_audio_state.backend_kind != MK_AUDIO_BACKEND_SOFT &&
        g_audio_state.backend_kind != MK_AUDIO_BACKEND_PCSPKR) {
        return;
    }
    if (!g_audio_state.usb_audio_attached_ready ||
        kernel_usb_audio_playback_supported() != 0) {
        return;
    }

    promotion_in_progress = 1u;
    mk_audio_select_uaudio_backend();
    promotion_in_progress = 0u;
}

static const char *mk_audio_usb_transport_name(uint8_t kind) {
    switch (kind) {
    case KERNEL_USB_HOST_KIND_UHCI:
        return "uhci";
    case KERNEL_USB_HOST_KIND_OHCI:
        return "ohci";
    case KERNEL_USB_HOST_KIND_EHCI:
        return "ehci";
    case KERNEL_USB_HOST_KIND_XHCI:
        return "xhci";
    default:
        return "usb";
    }
}

static void mk_audio_build_usb_audio_reason(char *dst,
                                            size_t dst_size,
                                            const char *prefix,
                                            uint8_t transport_kind,
                                            const char *suffix) {
    if (dst == 0 || dst_size == 0u) {
        return;
    }
    dst[0] = '\0';
    if (prefix != 0 && prefix[0] != '\0') {
        mk_audio_copy_limited(dst, prefix, dst_size);
    }
    mk_audio_debug_append_text(dst, dst_size, mk_audio_usb_transport_name(transport_kind));
    if (suffix != 0 && suffix[0] != '\0') {
        mk_audio_debug_append_text(dst, dst_size, suffix);
    }
}

static void mk_audio_set_uaudio_identity(const char *suffix) {
    char device_name[MAX_AUDIO_DEV_LEN];
    char device_config[MAX_AUDIO_DEV_LEN];
    const char *transport_name = mk_audio_usb_transport_name(g_audio_state.usb_audio_transport_kind);

    memset(device_name, 0, sizeof(device_name));
    memset(device_config, 0, sizeof(device_config));
    mk_audio_copy_limited(device_name, "usb-", sizeof(device_name));
    mk_audio_debug_append_text(device_name, sizeof(device_name), transport_name);
    mk_audio_copy_limited(g_audio_state.info.device.name, device_name, MAX_AUDIO_DEV_LEN);
    mk_audio_copy_limited(g_audio_state.info.device.version, "compat-uaudio", MAX_AUDIO_DEV_LEN);
    mk_audio_copy_limited(device_config, "usb-audio-", sizeof(device_config));
    mk_audio_debug_append_text(device_config, sizeof(device_config), transport_name);
    if (suffix != 0 && suffix[0] != '\0') {
        mk_audio_debug_append_text(device_config, sizeof(device_config), suffix);
    }
    mk_audio_copy_limited(g_audio_state.info.device.config, device_config, MAX_AUDIO_DEV_LEN);
}

static void mk_audio_apply_uaudio_params(void) {
    g_audio_state.info.parameters.nblks = 8u;
    g_audio_state.info.parameters.round = 0u;
    mk_audio_normalize_uaudio_params(&g_audio_state.info.parameters);
}

static void mk_audio_refresh_topology_snapshot(void) {
    static uint8_t reconcile_in_progress = 0u;

    if (!g_audio_bootstrap_defer_probe) {
        mk_audio_refresh_usb_attach_snapshot();
    }
    if (!g_audio_bootstrap_defer_probe && !reconcile_in_progress) {
        reconcile_in_progress = 1u;
        if (g_audio_state.backend_kind == MK_AUDIO_BACKEND_COMPAT_UAUDIO &&
            !mk_audio_backend_current_is_usable()) {
            mk_audio_failover_from_unusable_uaudio();
        } else if (g_audio_state.backend_kind == MK_AUDIO_BACKEND_COMPAT_AUICH &&
                   !mk_audio_backend_current_is_usable()) {
            mk_audio_failover_from_unusable_compat();
        } else {
            mk_audio_maybe_promote_uaudio_backend();
        }
        reconcile_in_progress = 0u;
    }
    g_audio_state.info.parameters._spare[0] = (unsigned int)(
        g_audio_state.backend_kind == MK_AUDIO_BACKEND_COMPAT_AZALIA ?
            g_audio_state.azalia_irq_count :
            g_audio_state.compat_irq_count);
    g_audio_state.info.parameters._spare[1] = mk_audio_output_count();
    g_audio_state.info.parameters._spare[2] = mk_audio_input_count();
    g_audio_state.info.parameters._spare[3] = mk_audio_output_presence_mask();
    g_audio_state.info.parameters._spare[4] = mk_audio_input_presence_mask();
    g_audio_state.info.parameters._spare[5] = mk_audio_backend_feature_flags();
    if (g_audio_state.usb_audio_attach_ready) {
        g_audio_state.info.output_route =
            ((uint32_t)g_audio_state.usb_audio_streaming_interface_number << 24) |
            ((uint32_t)g_audio_state.usb_audio_streaming_alt_setting << 16) |
            ((uint32_t)g_audio_state.usb_audio_playback_endpoint_address << 8) |
            (uint32_t)g_audio_state.usb_audio_configuration_value;
    } else {
        g_audio_state.info.output_route =
            ((uint32_t)g_audio_state.azalia_output_pin_nid << 8) |
            (uint32_t)g_audio_state.azalia_output_dac_nid;
    }
    g_audio_state.info.controller_pci_id =
        ((uint32_t)g_audio_state.pci.vendor_id << 16) |
        (uint32_t)g_audio_state.pci.device_id;
    g_audio_state.info.controller_location =
        ((uint32_t)g_audio_state.pci.bus << 16) |
        ((uint32_t)g_audio_state.pci.slot << 8) |
        (uint32_t)g_audio_state.pci.function;
    g_audio_state.info.codec_vendor_id =
        g_audio_state.backend_kind == MK_AUDIO_BACKEND_COMPAT_AZALIA ?
            g_audio_state.azalia_vendor_id :
            0u;
}

static void mk_audio_compat_apply_mixer_state(void) {
    uint16_t output_value;
    uint16_t headphone_value;
    uint16_t surround_value;
    uint16_t center_lfe_value;
    uint16_t input_value;
    uint32_t output_mask;

    if (!g_audio_state.compat_codec_ready) {
        return;
    }

    output_mask = mk_audio_output_presence_mask();
    output_value = mk_audio_encode_output_gain(g_audio_state.output_level);
    input_value = mk_audio_encode_input_gain(g_audio_state.input_level);
    if (g_audio_state.output_muted) {
        output_value |= 0x8000u;
    }
    if (g_audio_state.input_muted) {
        input_value |= 0x8000u;
    }

    headphone_value = output_value;
    surround_value = output_value;
    center_lfe_value = output_value;

    if ((output_mask & 0x2u) != 0u || (output_mask & 0x4u) != 0u || (output_mask & 0x8u) != 0u) {
        /*
         * AC97 codecs on older notebooks are inconsistent about which analog
         * volume register actually feeds the built-in speakers. Mirroring the
         * selected gain across all exposed analog paths is safer than muting
         * the non-default routes and avoids "works elsewhere, silent here"
         * regressions when the codec advertises headphone/surround paths that
         * do not line up with the machine's actual speaker wiring.
         */
        if (!g_audio_state.output_muted) {
            output_value &= (uint16_t)~0x8000u;
            headphone_value &= (uint16_t)~0x8000u;
            surround_value &= (uint16_t)~0x8000u;
            center_lfe_value &= (uint16_t)~0x8000u;
        }
    }

    if ((g_audio_state.compat_caps & AC97_CAPS_HEADPHONES) != 0u) {
        (void)mk_audio_compat_write_codec(AC97_REG_HEADPHONE_VOLUME, headphone_value);
    }
    if ((output_mask & 0x4u) != 0u) {
        (void)mk_audio_compat_write_codec(AC97_REG_SURR_MASTER, surround_value);
    }
    if ((output_mask & 0x8u) != 0u) {
        (void)mk_audio_compat_write_codec(AC97_REG_CENTER_LFE_MASTER, center_lfe_value);
    }

    (void)mk_audio_compat_write_codec(AC97_REG_MASTER_VOLUME, output_value);
    (void)mk_audio_compat_write_codec(AC97_REG_PCMOUT_VOLUME, output_value);
    (void)mk_audio_compat_write_codec(AC97_REG_RECORD_SELECT,
                                      mk_audio_encode_record_source(g_audio_state.default_input));
    (void)mk_audio_compat_write_codec(AC97_REG_RECORD_GAIN, input_value);
    (void)mk_audio_compat_write_codec(AC97_REG_LINEIN_VOLUME,
                                      g_audio_state.input_muted ? 0x8000u : 0x0808u);
    (void)mk_audio_compat_write_codec(AC97_REG_MIC_VOLUME,
                                      g_audio_state.input_muted ? 0x8000u : 0x0000u);
}

static void mk_audio_compat_apply_params(void) {
    uint16_t ext_id = 0u;
    uint16_t ext_ctrl = 0u;
    uint16_t rate;

    if (!g_audio_state.compat_codec_ready) {
        return;
    }

    if (mk_audio_compat_read_codec(AC97_REG_EXT_AUDIO_ID, &ext_id) == 0) {
        g_audio_state.compat_ext_audio_id = ext_id;
        if ((ext_id & AC97_EXT_AUDIO_VRA) != 0u) {
            ext_ctrl |= AC97_EXT_AUDIO_VRA;
        }
        if ((ext_id & AC97_EXT_AUDIO_SDAC) != 0u) {
            ext_ctrl |= AC97_EXT_AUDIO_SDAC;
        }
        if ((ext_id & AC97_EXT_AUDIO_CDAC) != 0u) {
            ext_ctrl |= AC97_EXT_AUDIO_CDAC;
        }
        if ((ext_id & AC97_EXT_AUDIO_LDAC) != 0u) {
            ext_ctrl |= AC97_EXT_AUDIO_LDAC;
        }
        (void)mk_audio_compat_write_codec(AC97_REG_EXT_AUDIO_CTRL, ext_ctrl);
    }

    rate = (uint16_t)g_audio_state.info.parameters.rate;
    if (rate == 0u) {
        rate = 48000u;
    }
    (void)mk_audio_compat_write_codec(AC97_REG_PCM_FRONT_DAC_RATE, rate);
    if ((g_audio_state.compat_ext_audio_id & AC97_EXT_AUDIO_SDAC) != 0u) {
        (void)mk_audio_compat_write_codec(AC97_REG_PCM_SURR_DAC_RATE, rate);
    }
    if ((g_audio_state.compat_ext_audio_id & (AC97_EXT_AUDIO_CDAC | AC97_EXT_AUDIO_LDAC)) != 0u) {
        (void)mk_audio_compat_write_codec(AC97_REG_PCM_LFE_DAC_RATE, rate);
    }
    (void)mk_audio_compat_write_codec(AC97_REG_PCM_LR_ADC_RATE, rate);
}

static void mk_audio_compat_halt_output(void) {
    if (g_audio_state.compat_aud_base == 0u) {
        return;
    }

    mk_audio_compat_write8(g_audio_state.compat_aud_base,
                           g_audio_state.compat_aud_is_mmio,
                           AUICH_PCMO + AUICH_CTRL,
                           0u);
    (void)mk_audio_compat_read16(g_audio_state.compat_aud_base, g_audio_state.compat_aud_is_mmio, AUICH_PCMO + AUICH_STS);
    mk_audio_compat_write16(g_audio_state.compat_aud_base,
                            g_audio_state.compat_aud_is_mmio,
                            AUICH_PCMO + AUICH_STS,
                            (uint16_t)(AUICH_CELV | AUICH_LVBCI | AUICH_BCIS | AUICH_FIFOE));
    mk_audio_compat_write8(g_audio_state.compat_aud_base,
                           g_audio_state.compat_aud_is_mmio,
                           AUICH_PCMO + AUICH_CTRL,
                           AUICH_RR);
    for (uint32_t i = 0u; i < 32u; ++i) {
        mk_audio_compat_delay();
    }
    mk_audio_compat_write8(g_audio_state.compat_aud_base,
                           g_audio_state.compat_aud_is_mmio,
                           AUICH_PCMO + AUICH_CTRL,
                           0u);
    g_audio_state.compat_output_running = 0u;
    g_audio_state.compat_output_pending = 0u;
    g_audio_state.compat_output_producer = 0u;
    g_audio_state.compat_output_consumer = 0u;
    g_audio_state.compat_output_hw_civ = 0u;
    memset(g_audio_auich_pcmo_bytes, 0, sizeof(g_audio_auich_pcmo_bytes));
}

static void mk_audio_compat_reset_output_ring(void) {
    memset(g_audio_auich_pcmo_dmalist, 0, sizeof(g_audio_auich_pcmo_dmalist));
    memset(g_audio_auich_pcmo_buffers, 0, sizeof(g_audio_auich_pcmo_buffers));
    memset(g_audio_auich_pcmo_bytes, 0, sizeof(g_audio_auich_pcmo_bytes));
    g_audio_state.compat_output_running = 0u;
    g_audio_state.compat_output_pending = 0u;
    g_audio_state.compat_output_producer = 0u;
    g_audio_state.compat_output_consumer = 0u;
    g_audio_state.compat_output_hw_civ = 0u;
}

static void mk_audio_compat_halt_input(void) {
    if (g_audio_state.compat_aud_base == 0u) {
        return;
    }

    mk_audio_compat_write8(g_audio_state.compat_aud_base,
                           g_audio_state.compat_aud_is_mmio,
                           AUICH_PCMI + AUICH_CTRL,
                           0u);
    (void)mk_audio_compat_read16(g_audio_state.compat_aud_base, g_audio_state.compat_aud_is_mmio, AUICH_PCMI + AUICH_STS);
    mk_audio_compat_write16(g_audio_state.compat_aud_base,
                            g_audio_state.compat_aud_is_mmio,
                            AUICH_PCMI + AUICH_STS,
                            (uint16_t)(AUICH_CELV | AUICH_LVBCI | AUICH_BCIS | AUICH_FIFOE));
    mk_audio_compat_write8(g_audio_state.compat_aud_base,
                           g_audio_state.compat_aud_is_mmio,
                           AUICH_PCMI + AUICH_CTRL,
                           AUICH_RR);
    for (uint32_t i = 0u; i < 32u; ++i) {
        mk_audio_compat_delay();
    }
    mk_audio_compat_write8(g_audio_state.compat_aud_base,
                           g_audio_state.compat_aud_is_mmio,
                           AUICH_PCMI + AUICH_CTRL,
                           0u);
    g_audio_state.compat_input_running = 0u;
    g_audio_state.compat_input_pending = 0u;
    g_audio_state.compat_input_producer = 0u;
    g_audio_state.compat_input_consumer = 0u;
    g_audio_state.compat_input_hw_civ = 0u;
    memset(g_audio_auich_pcmi_bytes, 0, sizeof(g_audio_auich_pcmi_bytes));
}

static void mk_audio_compat_reset_input_ring(void) {
    memset(g_audio_auich_pcmi_dmalist, 0, sizeof(g_audio_auich_pcmi_dmalist));
    memset(g_audio_auich_pcmi_buffers, 0, sizeof(g_audio_auich_pcmi_buffers));
    memset(g_audio_auich_pcmi_bytes, 0, sizeof(g_audio_auich_pcmi_bytes));
    g_audio_state.compat_input_running = 0u;
    g_audio_state.compat_input_pending = 0u;
    g_audio_state.compat_input_producer = 0u;
    g_audio_state.compat_input_consumer = 0u;
    g_audio_state.compat_input_hw_civ = 0u;
}

static int mk_audio_compat_capture_block(uint32_t requested_bytes) {
    uint32_t dma_chunk_limit;
    uint32_t dma_words_total;
    uint32_t wait_count = 0u;
    uint16_t sts = 0u;
    uint16_t picb = 0xffffu;
    uint16_t captured_bytes;

    if (g_audio_state.compat_aud_base == 0u || !g_audio_state.compat_ready || !g_audio_state.compat_codec_ready) {
        return -1;
    }

    dma_chunk_limit = requested_bytes;
    if (dma_chunk_limit < g_audio_state.info.parameters.round) {
        dma_chunk_limit = g_audio_state.info.parameters.round;
    }
    if (dma_chunk_limit == 0u || dma_chunk_limit > AUICH_DMA_SLOT_SIZE) {
        dma_chunk_limit = AUICH_DMA_SLOT_SIZE;
    }
    if ((dma_chunk_limit & 1u) != 0u) {
        dma_chunk_limit -= 1u;
    }
    if (dma_chunk_limit < 2u) {
        dma_chunk_limit = 2u;
    }
    dma_words_total = dma_chunk_limit / 2u;

    mk_audio_compat_halt_input();
    mk_audio_compat_reset_input_ring();
    memset(&g_audio_auich_pcmi_buffers[0][0], 0, AUICH_DMA_SLOT_SIZE);
    g_audio_auich_pcmi_dmalist[0].base = (uint32_t)(uintptr_t)&g_audio_auich_pcmi_buffers[0][0];
    g_audio_auich_pcmi_dmalist[0].len = ((dma_chunk_limit / 2u) & 0xffffu) | AUICH_DMAF_IOC;
    g_audio_auich_pcmi_bytes[0] = (uint16_t)dma_chunk_limit;
    g_audio_state.compat_input_running = 1u;
    g_audio_state.compat_input_pending = 1u;
    g_audio_state.compat_input_producer = 1u;
    g_audio_state.compat_input_consumer = 0u;

    mk_audio_compat_write32(g_audio_state.compat_aud_base,
                            g_audio_state.compat_aud_is_mmio,
                            AUICH_PCMI + AUICH_BDBAR,
                            (uint32_t)(uintptr_t)&g_audio_auich_pcmi_dmalist[0]);
    mk_audio_compat_write16(g_audio_state.compat_aud_base,
                            g_audio_state.compat_aud_is_mmio,
                            AUICH_PCMI + AUICH_STS,
                            (uint16_t)(AUICH_CELV | AUICH_LVBCI | AUICH_BCIS | AUICH_FIFOE));
    mk_audio_compat_write8(g_audio_state.compat_aud_base, g_audio_state.compat_aud_is_mmio, AUICH_PCMI + AUICH_LVI, 0u);
    mk_audio_compat_write8(g_audio_state.compat_aud_base,
                           g_audio_state.compat_aud_is_mmio,
                           AUICH_PCMI + AUICH_CTRL,
                           (uint8_t)(AUICH_IOCE | AUICH_FEIE | AUICH_LVBIE | AUICH_RPBM));

    while (wait_count < 200000u) {
        sts = mk_audio_compat_read16(g_audio_state.compat_aud_base, g_audio_state.compat_aud_is_mmio, AUICH_PCMI + AUICH_STS);
        picb = (uint16_t)(mk_audio_compat_read16(g_audio_state.compat_aud_base, g_audio_state.compat_aud_is_mmio, AUICH_PCMI + AUICH_PICB) & 0xffffu);
        if ((sts & (AUICH_BCIS | AUICH_LVBCI)) != 0u || ((sts & (AUICH_DCH | AUICH_CELV)) != 0u && picb == 0u)) {
            break;
        }
        mk_audio_compat_delay();
        wait_count++;
    }

    if (wait_count == 200000u) {
        mk_audio_compat_debug_pcmi("capture-timeout");
    }

    if ((uint32_t)picb > dma_words_total) {
        picb = (uint16_t)dma_words_total;
    }
    captured_bytes = (uint16_t)((dma_words_total - (uint32_t)picb) * 2u);
    if (captured_bytes > (uint16_t)dma_chunk_limit) {
        captured_bytes = (uint16_t)dma_chunk_limit;
    }
    if (captured_bytes == 0u && wait_count == 200000u) {
        /* Some QEMU AC97 variants expose the capture engine but never advance
           PCMI even when playback/output is otherwise functional. Return a
           silent block so userland capture does not stall forever. */
        memset(&g_audio_auich_pcmi_buffers[0][0], 0, dma_chunk_limit);
        captured_bytes = (uint16_t)dma_chunk_limit;
        mk_audio_capture_note_xrun();
        kernel_debug_printf("audio: capture-silence-fallback bytes=%d\n",
                            (unsigned int)captured_bytes);
    }
    if (captured_bytes != 0u) {
        mk_audio_capture_ring_write(&g_audio_auich_pcmi_buffers[0][0], captured_bytes);
    }

    if ((sts & AUICH_FIFOE) != 0u) {
        mk_audio_capture_note_xrun();
    }
    mk_audio_compat_write16(g_audio_state.compat_aud_base,
                            g_audio_state.compat_aud_is_mmio,
                            AUICH_PCMI + AUICH_STS,
                            (uint16_t)(AUICH_CELV | AUICH_LVBCI | AUICH_BCIS | AUICH_FIFOE));
    mk_audio_compat_halt_input();
    return captured_bytes != 0u ? (int)captured_bytes : -1;
}

static void mk_audio_compat_update_output_progress(void) {
    uint8_t civ;
    uint8_t piv;
    uint16_t picb;
    uint16_t sts;

    if (!g_audio_state.compat_output_running || g_audio_state.compat_aud_base == 0u) {
        return;
    }

    civ = (uint8_t)(mk_audio_compat_read8(g_audio_state.compat_aud_base, g_audio_state.compat_aud_is_mmio, AUICH_PCMO + AUICH_CIV) & AUICH_LVI_MASK);
    piv = (uint8_t)(mk_audio_compat_read8(g_audio_state.compat_aud_base, g_audio_state.compat_aud_is_mmio, AUICH_PCMO + AUICH_PIV) & AUICH_LVI_MASK);
    picb = (uint16_t)(mk_audio_compat_read16(g_audio_state.compat_aud_base, g_audio_state.compat_aud_is_mmio, AUICH_PCMO + AUICH_PICB) & 0xffffu);
    sts = mk_audio_compat_read16(g_audio_state.compat_aud_base, g_audio_state.compat_aud_is_mmio, AUICH_PCMO + AUICH_STS);
    g_audio_state.compat_output_hw_civ = civ;

    while (g_audio_state.compat_output_pending > 0u &&
           g_audio_state.compat_output_consumer != civ) {
        uint8_t completed_slot =
            (uint8_t)((g_audio_state.compat_output_consumer + 1u) & AUICH_LVI_MASK);

        g_audio_state.compat_output_consumer =
            completed_slot;
        g_audio_state.compat_output_pending--;
        g_audio_state.playback_bytes_consumed += g_audio_auich_pcmo_bytes[completed_slot];
        g_audio_auich_pcmo_bytes[completed_slot] = 0u;
    }

    if (g_audio_state.compat_output_pending == 1u &&
        picb == 0u &&
        (piv == civ || (sts & (AUICH_DCH | AUICH_CELV | AUICH_BCIS | AUICH_LVBCI)) != 0u)) {
        g_audio_state.playback_bytes_consumed += g_audio_auich_pcmo_bytes[civ];
        g_audio_auich_pcmo_bytes[civ] = 0u;
        g_audio_state.compat_output_pending = 0u;
    }

    if ((sts & (AUICH_CELV | AUICH_LVBCI | AUICH_BCIS | AUICH_FIFOE)) != 0u) {
        if ((sts & AUICH_FIFOE) != 0u) {
            g_audio_state.playback_xruns++;
            g_audio_state.playback_underruns++;
        }
        mk_audio_compat_write16(g_audio_state.compat_aud_base,
                                g_audio_state.compat_aud_is_mmio,
                                AUICH_PCMO + AUICH_STS,
                                (uint16_t)(sts & (AUICH_CELV | AUICH_LVBCI | AUICH_BCIS | AUICH_FIFOE)));
    }

    if (g_audio_state.compat_output_pending == 0u &&
        ((sts & (AUICH_DCH | AUICH_CELV)) != 0u || (picb == 0u && piv == civ))) {
        if (g_audio_state.playback_bytes_written > g_audio_state.playback_bytes_consumed) {
            g_audio_state.playback_starvations++;
        }
        g_audio_state.compat_output_running = 0u;
    }
}

static void mk_audio_compat_update_input_progress(void) {
    uint8_t civ;
    uint8_t piv;
    uint16_t picb;
    uint16_t sts;

    if (!g_audio_state.compat_input_running || g_audio_state.compat_aud_base == 0u) {
        return;
    }

    civ = (uint8_t)(mk_audio_compat_read8(g_audio_state.compat_aud_base, g_audio_state.compat_aud_is_mmio, AUICH_PCMI + AUICH_CIV) & AUICH_LVI_MASK);
    piv = (uint8_t)(mk_audio_compat_read8(g_audio_state.compat_aud_base, g_audio_state.compat_aud_is_mmio, AUICH_PCMI + AUICH_PIV) & AUICH_LVI_MASK);
    picb = (uint16_t)(mk_audio_compat_read16(g_audio_state.compat_aud_base, g_audio_state.compat_aud_is_mmio, AUICH_PCMI + AUICH_PICB) & 0xffffu);
    sts = mk_audio_compat_read16(g_audio_state.compat_aud_base, g_audio_state.compat_aud_is_mmio, AUICH_PCMI + AUICH_STS);
    g_audio_state.compat_input_hw_civ = civ;

    while (g_audio_state.compat_input_pending > 0u &&
           g_audio_state.compat_input_consumer != civ) {
        uint8_t completed_slot =
            (uint8_t)((g_audio_state.compat_input_consumer + 1u) & AUICH_LVI_MASK);
        uint16_t captured_bytes = g_audio_auich_pcmi_bytes[completed_slot];

        g_audio_state.compat_input_consumer = completed_slot;
        g_audio_state.compat_input_pending--;
        if (captured_bytes != 0u) {
            mk_audio_capture_ring_write(&g_audio_auich_pcmi_buffers[completed_slot][0], captured_bytes);
        }

        g_audio_state.compat_input_producer = completed_slot;
        g_audio_state.compat_input_pending++;
        mk_audio_compat_write8(g_audio_state.compat_aud_base,
                               g_audio_state.compat_aud_is_mmio,
                               AUICH_PCMI + AUICH_LVI,
                               completed_slot);
    }

    if ((sts & (AUICH_CELV | AUICH_LVBCI | AUICH_BCIS | AUICH_FIFOE)) != 0u) {
        if ((sts & AUICH_FIFOE) != 0u) {
            mk_audio_capture_note_xrun();
        }
        mk_audio_compat_write16(g_audio_state.compat_aud_base,
                                g_audio_state.compat_aud_is_mmio,
                                AUICH_PCMI + AUICH_STS,
                                (uint16_t)(sts & (AUICH_CELV | AUICH_LVBCI | AUICH_BCIS | AUICH_FIFOE)));
    }

    if ((sts & (AUICH_DCH | AUICH_CELV)) != 0u && picb == 0u && piv == civ) {
        g_audio_state.compat_input_running = 0u;
    }
}

static __attribute__((unused)) void mk_audio_compat_irq_handler(void) {
    uint16_t status;
    uint16_t input_status;

    if (g_audio_state.compat_aud_base == 0u) {
        /* EOI is the dispatcher's job now (single per dispatch, line 196). */
        return;
    }

    status = mk_audio_compat_read16(g_audio_state.compat_aud_base, g_audio_state.compat_aud_is_mmio, AUICH_PCMO + AUICH_STS);
    if ((status & (AUICH_CELV | AUICH_LVBCI | AUICH_BCIS | AUICH_FIFOE)) != 0u) {
        g_audio_state.compat_irq_count++;
        mk_audio_compat_write16(g_audio_state.compat_aud_base,
                                g_audio_state.compat_aud_is_mmio,
                                AUICH_PCMO + AUICH_STS,
                                (uint16_t)(status & (AUICH_CELV | AUICH_LVBCI | AUICH_BCIS | AUICH_FIFOE)));
        if ((status & AUICH_FIFOE) != 0u) {
            g_audio_state.playback_xruns++;
            g_audio_state.playback_underruns++;
        }
    }

    input_status = mk_audio_compat_read16(g_audio_state.compat_aud_base, g_audio_state.compat_aud_is_mmio, AUICH_PCMI + AUICH_STS);
    if ((input_status & (AUICH_CELV | AUICH_LVBCI | AUICH_BCIS | AUICH_FIFOE)) != 0u) {
        g_audio_state.compat_irq_count++;
        mk_audio_compat_write16(g_audio_state.compat_aud_base,
                                g_audio_state.compat_aud_is_mmio,
                                AUICH_PCMI + AUICH_STS,
                                (uint16_t)(input_status & (AUICH_CELV | AUICH_LVBCI | AUICH_BCIS | AUICH_FIFOE)));
        if ((input_status & AUICH_FIFOE) != 0u) {
            mk_audio_capture_note_xrun();
        }
    }

    mk_audio_compat_update_output_progress();
    mk_audio_compat_update_input_progress();
    /* EOI is the dispatcher's job now (single per dispatch, line 196). */
}

static void __attribute__((unused)) mk_audio_azalia_irq_handler(void) {
    uint32_t int_status;
    uint32_t stream_mask;
    uint16_t wake_status;
    uint16_t state_status;
    uint32_t base;
    uint8_t stream_status;
    uint8_t rirb_status;

    if (g_audio_state.azalia_base == 0u) {
        /* EOI is the dispatcher's job now (single per dispatch, line 196). */
        return;
    }

    int_status = mk_audio_azalia_read32(g_audio_state.azalia_base, HDA_INTSTS);
    stream_mask = (1u << g_audio_state.azalia_output_stream_index);
    wake_status = mk_audio_azalia_read16(g_audio_state.azalia_base, HDA_GSTS);
    state_status = mk_audio_azalia_read16(g_audio_state.azalia_base, HDA_STATESTS);
    if (int_status != 0u || wake_status != 0u || state_status != 0u) {
        g_audio_state.azalia_irq_count++;
    }
    if (state_status != 0u) {
        g_audio_state.azalia_codec_mask |= state_status;
        mk_audio_azalia_write16(g_audio_state.azalia_base, HDA_STATESTS, state_status);
    }
    if (wake_status != 0u) {
        mk_audio_azalia_write16(g_audio_state.azalia_base, HDA_GSTS, wake_status);
    }
    rirb_status = mk_audio_azalia_read8(g_audio_state.azalia_base, HDA_RIRBSTS);
    if ((rirb_status & (HDA_RIRBSTS_RINTFL | HDA_RIRBSTS_RIRBOIS)) != 0u) {
        mk_audio_azalia_drain_rirb_irq();
        mk_audio_azalia_write8(g_audio_state.azalia_base,
                               HDA_RIRBSTS,
                               (uint8_t)(rirb_status |
                                         HDA_RIRBSTS_RINTFL |
                                         HDA_RIRBSTS_RIRBOIS));
    }
    if ((int_status & stream_mask) != 0u && g_audio_state.azalia_output_regbase != 0u) {
        base = g_audio_state.azalia_output_regbase;
        stream_status = mk_audio_azalia_read8(g_audio_state.azalia_base, (uint16_t)(base + HDA_SD_STS));
        if ((stream_status & (HDA_SD_STS_DESE | HDA_SD_STS_FIFOE | HDA_SD_STS_BCIS)) != 0u) {
            mk_audio_azalia_write8(g_audio_state.azalia_base,
                                   (uint16_t)(base + HDA_SD_STS),
                                   (uint8_t)(stream_status |
                                             HDA_SD_STS_DESE |
                                             HDA_SD_STS_FIFOE |
                                             HDA_SD_STS_BCIS));
            if ((stream_status & HDA_SD_STS_FIFOE) != 0u) {
                g_audio_state.playback_xruns++;
                g_audio_state.playback_underruns++;
            }
        }
        mk_audio_azalia_update_output_progress();
    }
    /* EOI is the dispatcher's job now (single per dispatch, line 196). */
}

static void mk_audio_azalia_set_irq_enabled(uint8_t enabled) {
    if (!g_audio_state.azalia_irq_registered || g_audio_state.pci.irq_line >= 16u) {
        return;
    }
    if (enabled != 0u) {
        kernel_irq_unmask(g_audio_state.pci.irq_line);
    }
}

static void mk_audio_azalia_drain_rirb_irq(void) {
    uint16_t rirb_rp;
    uint16_t rirb_wp;

    if (!g_audio_state.azalia_corb_ready || g_audio_state.azalia_rirb_entries == 0u) {
        return;
    }

    rirb_rp = g_audio_state.azalia_rirb_read_pos;
    rirb_wp = (uint16_t)(mk_audio_azalia_read16(g_audio_state.azalia_base, HDA_RIRBWP) & 0x00ffu);
    while (rirb_rp != rirb_wp) {
        struct mk_audio_hda_rirb_entry *entry;

        rirb_rp = (uint16_t)((rirb_rp + 1u) % g_audio_state.azalia_rirb_entries);
        entry = &g_audio_state.azalia_rirb[rirb_rp];
        if ((entry->response_ex & HDA_RIRB_RESP_UNSOL) != 0u) {
            mk_audio_azalia_queue_unsol_event(entry->response, entry->response_ex);
        }
    }
    g_audio_state.azalia_rirb_read_pos = rirb_rp;
    mk_audio_azalia_kick_unsol_events();
}

static int mk_audio_compat_start_output_if_needed(void) {
    if (g_audio_state.compat_aud_base == 0u) {
        return -1;
    }
    if (g_audio_state.compat_output_running || g_audio_state.compat_output_pending == 0u) {
        return 0;
    }

    mk_audio_compat_write32(g_audio_state.compat_aud_base,
                            g_audio_state.compat_aud_is_mmio,
                            AUICH_PCMO + AUICH_BDBAR,
                            (uint32_t)(uintptr_t)&g_audio_auich_pcmo_dmalist[0]);
    mk_audio_compat_write16(g_audio_state.compat_aud_base,
                            g_audio_state.compat_aud_is_mmio,
                            AUICH_PCMO + AUICH_STS,
                            (uint16_t)(AUICH_CELV | AUICH_LVBCI | AUICH_BCIS | AUICH_FIFOE));
    g_audio_state.compat_output_hw_civ =
        (uint8_t)(mk_audio_compat_read8(g_audio_state.compat_aud_base, g_audio_state.compat_aud_is_mmio, AUICH_PCMO + AUICH_CIV) & AUICH_LVI_MASK);
    g_audio_state.compat_output_consumer = g_audio_state.compat_output_hw_civ;
    mk_audio_compat_write8(g_audio_state.compat_aud_base,
                           g_audio_state.compat_aud_is_mmio,
                           AUICH_PCMO + AUICH_LVI,
                           (uint8_t)((g_audio_state.compat_output_producer - 1u) & AUICH_LVI_MASK));
    mk_audio_compat_write8(g_audio_state.compat_aud_base,
                           g_audio_state.compat_aud_is_mmio,
                           AUICH_PCMO + AUICH_CTRL,
                           (uint8_t)(AUICH_IOCE | AUICH_FEIE | AUICH_RPBM));
    g_audio_state.compat_output_running = 1u;
    return 0;
}

static __attribute__((unused)) const char *mk_audio_auich_family_name(uint16_t device_id) {
    switch (device_id) {
    case PCI_PRODUCT_INTEL_82801AA_ACA:
        return "ich-aa";
    case PCI_PRODUCT_INTEL_82801AB_ACA:
        return "ich-ab";
    case PCI_PRODUCT_INTEL_82801BA_ACA:
        return "ich-ba";
    case PCI_PRODUCT_INTEL_82801CA_ACA:
        return "ich-ca";
    case PCI_PRODUCT_INTEL_82801DB_ACA:
        return "ich-db";
    case PCI_PRODUCT_INTEL_82801EB_ACA:
        return "ich-eb";
    case PCI_PRODUCT_INTEL_6300ESB_ACA:
        return "esb6300";
    case PCI_PRODUCT_INTEL_82801FB_ACA:
        return "ich-fb";
    case PCI_PRODUCT_INTEL_6321ESB_ACA:
        return "esb6321";
    case PCI_PRODUCT_INTEL_82801GB_ACA:
        return "ich-gb";
    case PCI_PRODUCT_INTEL_82440MX_ACA:
        return "440mx";
    case PCI_PRODUCT_AMD_768_ACA:
        return "amd-768";
    case PCI_PRODUCT_AMD_8111_ACA:
        return "amd-8111";
    case PCI_PRODUCT_ATI_SB200_AUDIO:
        return "ati-sb200";
    case PCI_PRODUCT_ATI_SB300_AUDIO:
        return "ati-sb300";
    case PCI_PRODUCT_ATI_SB400_AUDIO:
        return "ati-sb400";
    case PCI_PRODUCT_ATI_SB600_AUDIO:
        return "ati-sb600";
    case PCI_PRODUCT_NVIDIA_MCP04_AC97:
        return "mcp04";
    case PCI_PRODUCT_NVIDIA_NFORCE4_AC97:
        return "nforce4";
    case PCI_PRODUCT_NVIDIA_NFORCE2_AC97:
        return "nforce2";
    case PCI_PRODUCT_NVIDIA_NFORCE2_400_AC97:
        return "nf2-400";
    case PCI_PRODUCT_NVIDIA_NFORCE3_AC97:
        return "nforce3";
    case PCI_PRODUCT_NVIDIA_NFORCE3_250_AC97:
        return "nf3-250";
    case PCI_PRODUCT_NVIDIA_NFORCE_AC97:
        return "nforce";
    case PCI_PRODUCT_NVIDIA_MCP51_AC97:
        return "mcp51";
    case PCI_PRODUCT_ALI_M5455_AC97:
        return "ali-m5455";
    case PCI_PRODUCT_SIS_7012_AC97:
        return "sis-7012";
    case PCI_PRODUCT_VIATECH_VT82C686A_AC97:
        return "via-686";
    case PCI_PRODUCT_VIATECH_VT8233_AC97:
        return "via-8233";
    default:
        return "ac97";
    }
}

static __attribute__((unused)) const char *mk_audio_ac97_vendor_name(uint16_t vendor_id) {
    switch (vendor_id) {
    case PCI_VENDOR_INTEL:
        return "intel-ac97";
    case PCI_VENDOR_AMD:
        return "amd-ac97";
    case PCI_VENDOR_ATI:
        return "ati-ac97";
    case PCI_VENDOR_NVIDIA:
        return "nvidia-ac97";
    case PCI_VENDOR_ALI:
        return "ali-ac97";
    case PCI_VENDOR_SIS:
        return "sis-ac97";
    case PCI_VENDOR_VIATECH:
        return "via-ac97";
    default:
        return "compat-ac97";
    }
}

static __attribute__((unused)) const char *mk_audio_hda_vendor_name(uint16_t vendor_id) {
    switch (vendor_id) {
    case PCI_VENDOR_INTEL:
        return "intel-hda";
    case PCI_VENDOR_AMD:
        return "amd-hda";
    case PCI_VENDOR_ATI:
        return "ati-hda";
    case PCI_VENDOR_NVIDIA:
        return "nvidia-hda";
    case PCI_VENDOR_SIS:
        return "sis-hda";
    case PCI_VENDOR_VIATECH:
        return "via-hda";
    default:
        return "compat-hda";
    }
}

static void mk_audio_format_pci_location(char *dst,
                                         size_t dst_size,
                                         const struct kernel_pci_device_info *pci) {
    static const char hex_digits[] = "0123456789abcdef";
    size_t pos;

    if (dst == 0 || dst_size < 12u || pci == 0) {
        return;
    }

    dst[0] = hex_digits[(pci->bus >> 4) & 0xFu];
    dst[1] = hex_digits[pci->bus & 0xFu];
    dst[2] = ':';
    dst[3] = hex_digits[(pci->slot >> 4) & 0xFu];
    dst[4] = hex_digits[pci->slot & 0xFu];
    dst[5] = '.';
    dst[6] = hex_digits[pci->function & 0xFu];
    dst[7] = ' ';
    dst[8] = 'i';
    dst[9] = 'r';
    dst[10] = 'q';
    pos = 11u;
    if (pci->irq_line == 0xFFu) {
        if (pos + 2u < dst_size) {
            dst[pos++] = '?';
        }
    } else {
        if (pci->irq_line >= 100u && pos + 1u < dst_size) {
            dst[pos++] = (char)('0' + ((pci->irq_line / 100u) % 10u));
        }
        if (pci->irq_line >= 10u && pos + 1u < dst_size) {
            dst[pos++] = (char)('0' + ((pci->irq_line / 10u) % 10u));
        }
        if (pos + 1u < dst_size) {
            dst[pos++] = (char)('0' + (pci->irq_line % 10u));
        }
    }
    dst[pos] = '\0';
}

static __attribute__((unused)) void mk_audio_enable_pci_device(const struct kernel_pci_device_info *pci) {
    uint32_t command_status;

    if (pci == 0) {
        return;
    }

    command_status = kernel_pci_config_read_u32(pci->bus, pci->slot, pci->function, 0x04u);
    command_status |= (uint32_t)(PCI_COMMAND_IO_SPACE |
                                 PCI_COMMAND_MEMORY_SPACE |
                                 PCI_COMMAND_BUS_MASTER |
                                 PCI_COMMAND_BACKTOBACK_ENABLE);
    kernel_pci_config_write_u32(pci->bus, pci->slot, pci->function, 0x04u, command_status);
}

static void mk_audio_pci_config_write_u8_masked(const struct kernel_pci_device_info *pci,
                                                uint8_t offset,
                                                uint8_t mask,
                                                uint8_t value) {
    uint8_t current;
    uint8_t shift;
    uint32_t reg_offset;
    uint32_t reg_value;
    uint32_t byte_mask;

    if (pci == 0) {
        return;
    }

    reg_offset = (uint32_t)(offset & 0xfcu);
    shift = (uint8_t)((offset & 0x03u) * 8u);
    reg_value = kernel_pci_config_read_u32(pci->bus, pci->slot, pci->function, (uint8_t)reg_offset);
    current = (uint8_t)((reg_value >> shift) & 0xffu);
    byte_mask = (uint32_t)0xffu << shift;
    reg_value &= ~byte_mask;
    reg_value |= (uint32_t)(current & mask) << shift;
    reg_value |= (uint32_t)(value & (uint8_t)~mask) << shift;
    kernel_pci_config_write_u32(pci->bus, pci->slot, pci->function, (uint8_t)reg_offset, reg_value);
}

static int mk_audio_azalia_has_usable_playback_path(void) {
    uint8_t pin_nid;
    uint8_t dac_nid;
    int selected_bit;

    if (!g_audio_state.azalia_ready) {
        return 0;
    }
    if (!g_audio_state.azalia_codec_probed || g_audio_state.azalia_afg_nid == 0u) {
        return 0;
    }
    if (!g_audio_state.azalia_widget_probed) {
        return 0;
    }
    if (g_audio_state.azalia_output_dac_nid == 0u) {
        return 0;
    }
    for (int bit = 0; bit < 4; ++bit) {
        pin_nid = g_audio_state.azalia_output_pin_nids[bit];
        dac_nid = g_audio_state.azalia_output_dac_nids[bit];

        if (dac_nid == 0u) {
            continue;
        }
        if (pin_nid == 0u || mk_audio_azalia_find_output_dac(pin_nid, 0u) == (int)dac_nid) {
            return 1;
        }
    }
    pin_nid = 0u;
    dac_nid = 0u;
    selected_bit = -1;
    if (mk_audio_azalia_choose_output_path(&pin_nid, &dac_nid, &selected_bit) == 0 &&
        dac_nid != 0u &&
        (pin_nid == 0u || mk_audio_azalia_find_output_dac(pin_nid, 0u) == (int)dac_nid)) {
        return 1;
    }
    return 0;
}

static __attribute__((unused)) void mk_audio_configure_azalia_pci(const struct kernel_pci_device_info *pci) {
    uint32_t config;
    uint8_t reg;

    if (pci == 0) {
        return;
    }

    /* Match the compat azalia bring-up: clear traffic-class selection and
     * disable Intel no-snoop on the HDA function before touching DMA. */
    mk_audio_pci_config_write_u8_masked(pci, ICH_PCI_HDTCSEL, (uint8_t)~ICH_PCI_HDTCSEL_MASK, 0u);

    switch (pci->vendor_id) {
    case PCI_VENDOR_ATI:
        config = kernel_pci_config_read_u32(pci->bus, pci->slot, pci->function,
                                            (uint8_t)(ATI_PCIE_SNOOP_REG & 0xfcu));
        reg = (uint8_t)((config >> ((ATI_PCIE_SNOOP_REG & 0x03u) * 8u)) & 0xffu);
        reg &= ATI_PCIE_SNOOP_MASK;
        reg |= ATI_PCIE_SNOOP_ENABLE;
        mk_audio_pci_config_write_u8_masked(pci, ATI_PCIE_SNOOP_REG, 0u, reg);
        break;
    case PCI_VENDOR_NVIDIA:
        config = kernel_pci_config_read_u32(pci->bus, pci->slot, pci->function,
                                            (uint8_t)(NVIDIA_HDA_OSTR_COH_REG & 0xfcu));
        reg = (uint8_t)((config >> ((NVIDIA_HDA_OSTR_COH_REG & 0x03u) * 8u)) & 0xffu);
        reg |= NVIDIA_HDA_STR_COH_ENABLE;
        mk_audio_pci_config_write_u8_masked(pci, NVIDIA_HDA_OSTR_COH_REG, 0u, reg);

        config = kernel_pci_config_read_u32(pci->bus, pci->slot, pci->function,
                                            (uint8_t)(NVIDIA_HDA_ISTR_COH_REG & 0xfcu));
        reg = (uint8_t)((config >> ((NVIDIA_HDA_ISTR_COH_REG & 0x03u) * 8u)) & 0xffu);
        reg |= NVIDIA_HDA_STR_COH_ENABLE;
        mk_audio_pci_config_write_u8_masked(pci, NVIDIA_HDA_ISTR_COH_REG, 0u, reg);

        config = kernel_pci_config_read_u32(pci->bus, pci->slot, pci->function,
                                            (uint8_t)(NVIDIA_PCIE_SNOOP_REG & 0xfcu));
        reg = (uint8_t)((config >> ((NVIDIA_PCIE_SNOOP_REG & 0x03u) * 8u)) & 0xffu);
        reg &= NVIDIA_PCIE_SNOOP_MASK;
        reg |= NVIDIA_PCIE_SNOOP_ENABLE;
        mk_audio_pci_config_write_u8_masked(pci, NVIDIA_PCIE_SNOOP_REG, 0u, reg);
        break;
    case PCI_VENDOR_INTEL:
        config = kernel_pci_config_read_u32(pci->bus, pci->slot, pci->function,
                                            (uint8_t)(ICH_PCI_MMC & 0xfcu));
        reg = (uint8_t)((config >> ((ICH_PCI_MMC & 0x03u) * 8u)) & 0xffu);
        reg &= (uint8_t)~ICH_PCI_MMC_ME;
        mk_audio_pci_config_write_u8_masked(pci, ICH_PCI_MMC, 0u, reg);
        mk_audio_pci_config_write_u8_masked(pci, INTEL_PCIE_NOSNOOP_REG, INTEL_PCIE_NOSNOOP_MASK, 0u);
        break;
    default:
        break;
    }
}

struct mk_audio_probe_ctx {
    struct kernel_pci_device_info *out;
    int found;
};

static int mk_audio_backend_current_is_usable(void) {
    if (g_audio_state.backend_kind == MK_AUDIO_BACKEND_COMPAT_AZALIA) {
        return mk_audio_azalia_has_usable_playback_path() &&
               (g_audio_state.info.flags & MK_AUDIO_CAPS_PLAYBACK) != 0u;
    }
    if (g_audio_state.backend_kind == MK_AUDIO_BACKEND_PCSPKR) {
        return kernel_timer_pc_speaker_available() &&
               (g_audio_state.info.flags & MK_AUDIO_CAPS_PLAYBACK) != 0u;
    }
    if (g_audio_state.backend_kind == MK_AUDIO_BACKEND_COMPAT_UAUDIO) {
        return kernel_usb_audio_playback_supported() == 0 &&
               (g_audio_state.info.flags & MK_AUDIO_CAPS_PLAYBACK) != 0u;
    }
    if (g_audio_state.backend_kind == MK_AUDIO_BACKEND_COMPAT_AUICH) {
        return g_audio_state.compat_ready &&
               g_audio_state.compat_codec_ready &&
               (g_audio_state.info.flags & MK_AUDIO_CAPS_PLAYBACK) != 0u;
    }
    return 0;
}

static int mk_audio_probe_compat_cb(const struct kernel_pci_device_info *info, void *ctx_ptr) {
    struct mk_audio_probe_ctx *ctx = (struct mk_audio_probe_ctx *)ctx_ptr;

    if (info == 0 || ctx == 0 || ctx->out == 0) {
        return 0;
    }
    if (info->class_code != PCI_CLASS_MULTIMEDIA ||
        info->subclass != PCI_SUBCLASS_MULTIMEDIA_AUDIO) {
        return 0;
    }
    if (!mk_audio_is_auich_device(info->vendor_id, info->device_id)) {
        return 0;
    }

    *ctx->out = *info;
    ctx->found = 1;
    return 1;
}

static int mk_audio_probe_compat_backend(struct kernel_pci_device_info *pci_out) {
    struct mk_audio_probe_ctx ctx;

    if (pci_out == 0) {
        return -1;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.out = pci_out;
    if (kernel_pci_enumerate(mk_audio_probe_compat_cb, &ctx) != 0) {
        return -1;
    }
    return ctx.found ? 0 : -1;
}

static int mk_audio_probe_azalia_cb(const struct kernel_pci_device_info *info, void *ctx_ptr) {
    struct mk_audio_probe_ctx *ctx = (struct mk_audio_probe_ctx *)ctx_ptr;

    if (info == 0 || ctx == 0 || ctx->out == 0) {
        return 0;
    }
    if (info->class_code != PCI_CLASS_MULTIMEDIA ||
        info->subclass != PCI_SUBCLASS_MULTIMEDIA_HDA) {
        return 0;
    }
    if (!kernel_pci_bar_is_mmio(info->bars[HDA_BAR_INDEX])) {
        return 0;
    }

    *ctx->out = *info;
    ctx->found = 1;
    return 1;
}

static int mk_audio_probe_azalia_backend(struct kernel_pci_device_info *pci_out) {
    struct mk_audio_probe_ctx ctx;

    if (pci_out == 0) {
        return -1;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.out = pci_out;
    if (kernel_pci_enumerate(mk_audio_probe_azalia_cb, &ctx) != 0) {
        return -1;
    }
    return ctx.found ? 0 : -1;
}

static void mk_audio_select_compat_backend(const struct kernel_pci_device_info *pci);
static void mk_audio_select_azalia_backend(const struct kernel_pci_device_info *pci);

struct mk_audio_try_ctx {
    int matched;
    int selected;
};

static int mk_audio_try_azalia_backend_cb(const struct kernel_pci_device_info *info, void *ctx_ptr) {
    struct mk_audio_try_ctx *ctx = (struct mk_audio_try_ctx *)ctx_ptr;

    if (info == 0 || ctx == 0) {
        return 0;
    }
    if (info->class_code != PCI_CLASS_MULTIMEDIA ||
        info->subclass != PCI_SUBCLASS_MULTIMEDIA_HDA) {
        return 0;
    }
    if (!kernel_pci_bar_is_mmio(info->bars[HDA_BAR_INDEX])) {
        return 0;
    }

    ctx->matched = 1;
    mk_audio_select_azalia_backend(info);
    if (mk_audio_backend_current_is_usable()) {
        ctx->selected = 1;
        return 1;
    }
    if (!g_audio_state.azalia_ready ||
        mk_audio_azalia_config_is_fatal(g_audio_state.info.device.config)) {
        return 0;
    }
    /*
     * Keep the real HDA backend selected even when the first probe does not
     * produce a usable playback path yet. Runtime reprobe/output-path
     * programming can still recover it, while falling back to pcspkr here
     * makes the system stay mute on machines where audio used to come up
     * after a later codec/path settle.
     */
    ctx->selected = 1;
    return 1;
}

static int mk_audio_try_compat_backend_cb(const struct kernel_pci_device_info *info, void *ctx_ptr) {
    struct mk_audio_try_ctx *ctx = (struct mk_audio_try_ctx *)ctx_ptr;

    if (info == 0 || ctx == 0) {
        return 0;
    }
    if (info->class_code != PCI_CLASS_MULTIMEDIA ||
        info->subclass != PCI_SUBCLASS_MULTIMEDIA_AUDIO) {
        return 0;
    }
    if (!mk_audio_is_auich_device(info->vendor_id, info->device_id)) {
        return 0;
    }

    ctx->matched = 1;
    mk_audio_select_compat_backend(info);
    if (mk_audio_backend_current_is_usable()) {
        ctx->selected = 1;
        return 1;
    }
    return 0;
}

static int mk_audio_try_azalia_backends(void) {
    struct mk_audio_try_ctx ctx;

    memset(&ctx, 0, sizeof(ctx));
    if (kernel_pci_enumerate(mk_audio_try_azalia_backend_cb, &ctx) != 0) {
        return -1;
    }
    return ctx.selected ? 0 : -1;
}

static int mk_audio_try_compat_backends(void) {
    struct mk_audio_try_ctx ctx;

    memset(&ctx, 0, sizeof(ctx));
    if (kernel_pci_enumerate(mk_audio_try_compat_backend_cb, &ctx) != 0) {
        return -1;
    }
    return ctx.selected ? 0 : -1;
}

static __attribute__((unused)) int mk_audio_probe_any_hardware_backend(void) {
    struct kernel_pci_device_info detected_pci;

    if (mk_audio_probe_azalia_backend(&detected_pci) == 0) {
        return 1;
    }
    if (mk_audio_probe_compat_backend(&detected_pci) == 0) {
        return 1;
    }
    return 0;
}

static void mk_audio_select_soft_backend(void);
static void mk_audio_select_pcspkr_backend(void);
static void mk_audio_set_softmix_reason(const char *reason);

static void mk_audio_failover_from_unusable_hda(void) {
    char previous_device_config[MAX_AUDIO_DEV_LEN];

    if (g_audio_state.backend_kind != MK_AUDIO_BACKEND_COMPAT_AZALIA) {
        return;
    }

    memset(previous_device_config, 0, sizeof(previous_device_config));
    mk_audio_copy_limited(previous_device_config,
                          g_audio_state.info.device.config,
                          sizeof(previous_device_config));

    if (g_audio_state.usb_audio_attached_ready &&
        kernel_usb_audio_playback_supported() == 0) {
        mk_audio_select_uaudio_backend();
        return;
    }

    if (kernel_timer_pc_speaker_available()) {
        mk_audio_select_pcspkr_backend();
        if (strcmp(previous_device_config, "hda-no-audio-fg") == 0) {
            mk_audio_copy_limited(g_audio_state.info.device.config,
                                  "pcspkr-fallback-hda-no-audio-fg",
                                  MAX_AUDIO_DEV_LEN);
        } else if (strcmp(previous_device_config, "hda-no-usable-output") == 0) {
            mk_audio_copy_limited(g_audio_state.info.device.config,
                                  "pcspkr-fallback-hda-no-usable-output",
                                  MAX_AUDIO_DEV_LEN);
        } else {
            mk_audio_copy_limited(g_audio_state.info.device.config,
                                  "pcspkr-fallback-no-usable-hw-backend",
                                  MAX_AUDIO_DEV_LEN);
        }
        kernel_debug_puts("audio: failover from unusable hda to pcspkr\n");
        return;
    }

    mk_audio_select_soft_backend();
    if (previous_device_config[0] != '\0') {
        mk_audio_set_softmix_reason(previous_device_config);
    } else {
        mk_audio_set_softmix_reason("no-usable-hw-backend");
    }
    kernel_debug_puts("audio: failover from unusable hda to softmix\n");
}

static void mk_audio_failover_from_unusable_uaudio(void) {
    char previous_device_config[MAX_AUDIO_DEV_LEN];

    if (g_audio_state.backend_kind != MK_AUDIO_BACKEND_COMPAT_UAUDIO) {
        return;
    }

    memset(previous_device_config, 0, sizeof(previous_device_config));
    mk_audio_copy_limited(previous_device_config,
                          g_audio_state.info.device.config,
                          sizeof(previous_device_config));

    if (kernel_timer_pc_speaker_available()) {
        mk_audio_select_pcspkr_backend();
        if (previous_device_config[0] != '\0') {
            mk_audio_copy_limited(g_audio_state.info.device.config,
                                  previous_device_config,
                                  MAX_AUDIO_DEV_LEN);
        } else {
            mk_audio_copy_limited(g_audio_state.info.device.config,
                                  "pcspkr-fallback-usb-audio-unusable",
                                  MAX_AUDIO_DEV_LEN);
        }
        kernel_debug_puts("audio: failover from unusable usb-audio to pcspkr\n");
        return;
    }

    mk_audio_select_soft_backend();
    if (previous_device_config[0] != '\0') {
        mk_audio_set_softmix_reason(previous_device_config);
    } else {
        mk_audio_set_softmix_reason("usb-audio-unusable");
    }
    kernel_debug_puts("audio: failover from unusable usb-audio to softmix\n");
}

static void mk_audio_failover_from_unusable_compat(void) {
    char previous_device_config[MAX_AUDIO_DEV_LEN];

    if (g_audio_state.backend_kind != MK_AUDIO_BACKEND_COMPAT_AUICH) {
        return;
    }

    memset(previous_device_config, 0, sizeof(previous_device_config));
    mk_audio_copy_limited(previous_device_config,
                          g_audio_state.info.device.config,
                          sizeof(previous_device_config));

    if (g_audio_state.usb_audio_attached_ready &&
        kernel_usb_audio_playback_supported() == 0) {
        mk_audio_select_uaudio_backend();
        return;
    }

    if (kernel_timer_pc_speaker_available()) {
        mk_audio_select_pcspkr_backend();
        if (previous_device_config[0] != '\0') {
            mk_audio_copy_limited(g_audio_state.info.device.config,
                                  previous_device_config,
                                  MAX_AUDIO_DEV_LEN);
        } else {
            mk_audio_copy_limited(g_audio_state.info.device.config,
                                  "pcspkr-fallback-no-usable-hw-backend",
                                  MAX_AUDIO_DEV_LEN);
        }
        kernel_debug_puts("audio: failover from unusable ac97 to pcspkr\n");
        return;
    }

    mk_audio_select_soft_backend();
    if (previous_device_config[0] != '\0') {
        mk_audio_set_softmix_reason(previous_device_config);
    } else {
        mk_audio_set_softmix_reason("no-usable-hw-backend");
    }
    kernel_debug_puts("audio: failover from unusable ac97 to softmix\n");
}

static void mk_audio_select_soft_backend(void) {
    g_audio_state.backend_kind = MK_AUDIO_BACKEND_SOFT;
    g_audio_state.info.flags = MK_AUDIO_CAPS_MIXER |
                               MK_AUDIO_CAPS_PLAYBACK |
                               MK_AUDIO_CAPS_BSD_AUDIOIO_ABI;
    g_audio_state.info.status.mode = AUMODE_PLAY;
    memset(&g_audio_state.pci, 0, sizeof(g_audio_state.pci));
    g_audio_state.compat_mix_base = 0u;
    g_audio_state.compat_aud_base = 0u;
    g_audio_state.azalia_base = 0u;
    g_audio_state.compat_caps = 0u;
    g_audio_state.compat_ext_audio_id = 0u;
    g_audio_state.azalia_gcap = 0u;
    g_audio_state.azalia_codec_mask = 0u;
    g_audio_state.azalia_corb_entries = 0u;
    g_audio_state.azalia_rirb_entries = 0u;
    g_audio_state.azalia_rirb_read_pos = 0u;
    g_audio_state.compat_ready = 0u;
    g_audio_state.compat_codec_ready = 0u;
    g_audio_state.compat_irq_registered = 0u;
    g_audio_state.compat_mix_is_mmio = 0u;
    g_audio_state.compat_aud_is_mmio = 0u;
    g_audio_state.compat_ignore_codecready = 0u;
    g_audio_state.azalia_ready = 0u;
    g_audio_state.azalia_irq_registered = 0u;
    g_audio_state.azalia_vmaj = 0u;
    g_audio_state.azalia_vmin = 0u;
    g_audio_state.azalia_codec_address = 0u;
    g_audio_state.azalia_afg_nid = 0u;
    g_audio_state.azalia_codec_probed = 0u;
    g_audio_state.azalia_corb_ready = 0u;
    g_audio_state.azalia_widget_probed = 0u;
    g_audio_state.azalia_path_programmed = 0u;
    g_audio_state.azalia_output_dac_nid = 0u;
    g_audio_state.azalia_spkr_dac_nid = 0u;
    g_audio_state.azalia_output_pin_nid = 0u;
    g_audio_state.azalia_input_dac_nid = 0u;
    g_audio_state.azalia_input_pin_nid = 0u;
    g_audio_state.azalia_output_stream_index = 0u;
    g_audio_state.azalia_output_stream_number = 0u;
    g_audio_state.azalia_output_running = 0u;
    memset(g_audio_state.azalia_output_pin_nids, 0, sizeof(g_audio_state.azalia_output_pin_nids));
    memset(g_audio_state.azalia_output_dac_nids, 0, sizeof(g_audio_state.azalia_output_dac_nids));
    memset(g_audio_state.azalia_input_pin_nids, 0, sizeof(g_audio_state.azalia_input_pin_nids));
    g_audio_state.azalia_speaker2_pin_nid = 0u;
    g_audio_state.azalia_speaker2_dac_nid = 0u;
    g_audio_state.azalia_fhp_pin_nid = 0u;
    g_audio_state.azalia_fhp_dac_nid = 0u;
    g_audio_state.azalia_speaker2_priority = -1;
    g_audio_state.azalia_speaker2_config_default = 0u;
    memset(g_audio_state.azalia_widget_selected, 0, sizeof(g_audio_state.azalia_widget_selected));
    memset(g_audio_state.azalia_widget_selected_valid, 0, sizeof(g_audio_state.azalia_widget_selected_valid));
    memset(g_audio_state.azalia_widget_disabled, 0, sizeof(g_audio_state.azalia_widget_disabled));
    memset(g_audio_state.azalia_widget_powered, 0, sizeof(g_audio_state.azalia_widget_powered));
    memset(g_audio_state.azalia_output_priorities, -1, sizeof(g_audio_state.azalia_output_priorities));
    memset(g_audio_state.azalia_output_present_bits, 0, sizeof(g_audio_state.azalia_output_present_bits));
    memset(g_audio_state.azalia_sense_pin_nids, 0, sizeof(g_audio_state.azalia_sense_pin_nids));
    memset(g_audio_state.azalia_sense_pin_output_bits, 0xff, sizeof(g_audio_state.azalia_sense_pin_output_bits));
    g_audio_state.azalia_sense_pin_count = 0u;
    g_audio_state.azalia_spkr_muter_mask = 0u;
    g_audio_state.azalia_output_jack_count = 0u;
    g_audio_state.azalia_analog_dac_count = 0u;
    memset(g_audio_state.azalia_output_config_defaults, 0, sizeof(g_audio_state.azalia_output_config_defaults));
    memset(g_audio_state.azalia_output_sort_keys, 0xff, sizeof(g_audio_state.azalia_output_sort_keys));
    g_audio_state.azalia_presence_refresh_tick = 0u;
    g_audio_state.azalia_unsol_output_mask = 0u;
    g_audio_state.azalia_unsol_rp = 0u;
    g_audio_state.azalia_unsol_wp = 0u;
    g_audio_state.azalia_unsol_kick = 0u;
    memset(g_audio_state.azalia_unsol_queue, 0, sizeof(g_audio_state.azalia_unsol_queue));
    g_audio_state.azalia_pin_policy_busy = 0u;
    g_audio_state.azalia_output_mask = 0u;
    g_audio_state.azalia_input_mask = 0u;
    g_audio_state.azalia_output_fmt = 0u;
    g_audio_state.azalia_output_regbase = 0u;
    g_audio_state.azalia_output_bytes = 0u;
    g_audio_state.azalia_output_blk = 0u;
    g_audio_state.azalia_output_swpos = 0u;
    g_audio_state.azalia_output_pos = 0u;
    g_audio_backend = &g_audio_backend_soft;
    mk_audio_copy_limited(g_audio_state.info.device.name, "compat", MAX_AUDIO_DEV_LEN);
    mk_audio_copy_limited(g_audio_state.info.device.version, "softmix", MAX_AUDIO_DEV_LEN);
    mk_audio_copy_limited(g_audio_state.info.device.config, "compat-pcm", MAX_AUDIO_DEV_LEN);
    mk_audio_refresh_topology_snapshot();
}

static void mk_audio_select_pcspkr_backend(void) {
    mk_audio_select_soft_backend();
    g_audio_state.backend_kind = MK_AUDIO_BACKEND_PCSPKR;
    g_audio_backend = &g_audio_backend_pcspkr;
    mk_audio_copy_limited(g_audio_state.info.device.name, "legacy", MAX_AUDIO_DEV_LEN);
    mk_audio_copy_limited(g_audio_state.info.device.version, "pcspkr", MAX_AUDIO_DEV_LEN);
    mk_audio_copy_limited(g_audio_state.info.device.config, "pit-buzzer", MAX_AUDIO_DEV_LEN);
    g_audio_state.info.parameters.rate = 8000u;
    g_audio_state.info.parameters.bits = 16u;
    g_audio_state.info.parameters.bps = 2u;
    g_audio_state.info.parameters.sig = 1u;
    g_audio_state.info.parameters.le = 1u;
    g_audio_state.info.parameters.pchan = 1u;
    g_audio_state.info.parameters.rchan = 1u;
    g_audio_state.info.parameters.nblks = 2u;
    g_audio_state.info.parameters.round = 80u;
    mk_audio_normalize_params(&g_audio_state.info.parameters);
    mk_audio_refresh_topology_snapshot();
}

static void mk_audio_select_uaudio_backend(void) {
    struct kernel_usb_host_controller_info controller_info;

    mk_audio_select_soft_backend();
    mk_audio_reset_uaudio_runtime();
    g_audio_state.backend_kind = MK_AUDIO_BACKEND_COMPAT_UAUDIO;
    g_audio_backend = &g_audio_backend_uaudio;
    g_audio_state.info.flags = MK_AUDIO_CAPS_PLAYBACK |
                               MK_AUDIO_CAPS_BSD_AUDIOIO_ABI;
    g_audio_state.info.status.mode = AUMODE_PLAY;
    mk_audio_set_uaudio_identity("-attached");
    if (kernel_usb_audio_playback_controller_info(&controller_info) == 0) {
        g_audio_state.pci.bus = controller_info.bus;
        g_audio_state.pci.slot = controller_info.slot;
        g_audio_state.pci.function = controller_info.function;
        g_audio_state.pci.irq_line = controller_info.irq_line;
        g_audio_state.pci.prog_if = controller_info.prog_if;
        g_audio_state.pci.vendor_id = controller_info.vendor_id;
        g_audio_state.pci.device_id = controller_info.device_id;
    }
    mk_audio_apply_uaudio_params();
    mk_audio_refresh_topology_snapshot();
}

static void mk_audio_set_softmix_reason(const char *reason) {
    if (reason == 0 || reason[0] == '\0') {
        return;
    }
    if (g_audio_state.backend_kind != MK_AUDIO_BACKEND_SOFT) {
        return;
    }
    mk_audio_copy_limited(g_audio_state.info.device.config, reason, MAX_AUDIO_DEV_LEN);
}

static void mk_audio_select_compat_backend(const struct kernel_pci_device_info *pci) {
    char location[MAX_AUDIO_DEV_LEN];

    if (pci == 0) {
        mk_audio_select_soft_backend();
        return;
    }

    g_audio_state.backend_kind = MK_AUDIO_BACKEND_COMPAT_AUICH;
    g_audio_state.pci = *pci;
    g_audio_state.compat_mix_base = 0u;
    g_audio_state.compat_aud_base = 0u;
    g_audio_state.azalia_base = 0u;
    g_audio_state.compat_caps = 0u;
    g_audio_state.compat_ext_audio_id = 0u;
    g_audio_state.azalia_gcap = 0u;
    g_audio_state.azalia_codec_mask = 0u;
    g_audio_state.azalia_corb_entries = 0u;
    g_audio_state.azalia_rirb_entries = 0u;
    g_audio_state.azalia_rirb_read_pos = 0u;
    g_audio_state.compat_ready = 0u;
    g_audio_state.compat_codec_ready = 0u;
    g_audio_state.compat_irq_registered = 0u;
    g_audio_state.compat_mix_is_mmio = 0u;
    g_audio_state.compat_aud_is_mmio = 0u;
    g_audio_state.compat_ignore_codecready =
        mk_audio_auich_quirk_ignore_codecready(pci->vendor_id, pci->device_id) ? 1u : 0u;
    g_audio_state.azalia_ready = 0u;
    g_audio_state.azalia_irq_registered = 0u;
    g_audio_state.azalia_vmaj = 0u;
    g_audio_state.azalia_vmin = 0u;
    g_audio_state.azalia_codec_address = 0u;
    g_audio_state.azalia_afg_nid = 0u;
    g_audio_state.azalia_codec_probed = 0u;
    g_audio_state.azalia_corb_ready = 0u;
    g_audio_state.azalia_widget_probed = 0u;
    g_audio_state.azalia_path_programmed = 0u;
    g_audio_state.azalia_output_dac_nid = 0u;
    g_audio_state.azalia_spkr_dac_nid = 0u;
    g_audio_state.azalia_output_pin_nid = 0u;
    g_audio_state.azalia_input_dac_nid = 0u;
    g_audio_state.azalia_input_pin_nid = 0u;
    g_audio_state.azalia_output_stream_index = 0u;
    g_audio_state.azalia_output_stream_number = 0u;
    g_audio_state.azalia_output_running = 0u;
    g_audio_state.azalia_vendor_id = 0u;
    g_audio_state.azalia_subsystem_id = 0u;
    g_audio_state.azalia_quirks = 0u;
    g_audio_state.azalia_speaker2_pin_nid = 0u;
    g_audio_state.azalia_speaker2_dac_nid = 0u;
    g_audio_state.azalia_speaker2_priority = -1;
    g_audio_state.azalia_speaker2_config_default = 0u;
    memset(g_audio_state.azalia_widget_selected, 0, sizeof(g_audio_state.azalia_widget_selected));
    memset(g_audio_state.azalia_widget_selected_valid, 0, sizeof(g_audio_state.azalia_widget_selected_valid));
    memset(g_audio_state.azalia_widget_disabled, 0, sizeof(g_audio_state.azalia_widget_disabled));
    memset(g_audio_state.azalia_widget_powered, 0, sizeof(g_audio_state.azalia_widget_powered));
    g_audio_state.azalia_output_mask = 0u;
    g_audio_state.azalia_input_mask = 0u;
    g_audio_state.azalia_output_fmt = 0u;
    g_audio_state.azalia_output_regbase = 0u;
    g_audio_state.azalia_output_bytes = 0u;
    g_audio_state.azalia_output_blk = 0u;
    g_audio_state.azalia_output_swpos = 0u;
    g_audio_state.azalia_output_pos = 0u;
    g_audio_backend = &g_audio_backend_compat;
    mk_audio_enable_pci_device(pci);
    if (mk_audio_auich_prefers_native_mmio(pci->vendor_id, pci->device_id) &&
        kernel_pci_bar_is_mmio(pci->bars[AUICH_MMBAR_INDEX]) &&
        kernel_pci_bar_is_mmio(pci->bars[AUICH_MBBAR_INDEX])) {
        g_audio_state.compat_mix_base = kernel_pci_bar_base(pci->bars[AUICH_MMBAR_INDEX]);
        g_audio_state.compat_aud_base = kernel_pci_bar_base(pci->bars[AUICH_MBBAR_INDEX]);
        g_audio_state.compat_mix_is_mmio = 1u;
        g_audio_state.compat_aud_is_mmio = 1u;
        g_audio_state.compat_ready = 1u;
    } else if ((pci->bars[AUICH_NAMBAR_INDEX] & 0x1u) != 0u &&
               (pci->bars[AUICH_NABMBAR_INDEX] & 0x1u) != 0u) {
        g_audio_state.compat_mix_base = (uintptr_t)(pci->bars[AUICH_NAMBAR_INDEX] & 0xFFFCu);
        g_audio_state.compat_aud_base = (uintptr_t)(pci->bars[AUICH_NABMBAR_INDEX] & 0xFFFCu);
        g_audio_state.compat_ready = 1u;
    } else if (mk_audio_auich_prefers_native_mmio(pci->vendor_id, pci->device_id)) {
        uint32_t cfg = kernel_pci_config_read_u32(pci->bus, pci->slot, pci->function, 0x40u);

        kernel_pci_config_write_u32(pci->bus,
                                    pci->slot,
                                    pci->function,
                                    0x40u,
                                    cfg | AUICH_CFG_IOSE);
        if ((pci->bars[AUICH_NAMBAR_INDEX] & 0x1u) != 0u &&
            (pci->bars[AUICH_NABMBAR_INDEX] & 0x1u) != 0u) {
            g_audio_state.compat_mix_base = (uintptr_t)(pci->bars[AUICH_NAMBAR_INDEX] & 0xFFFCu);
            g_audio_state.compat_aud_base = (uintptr_t)(pci->bars[AUICH_NABMBAR_INDEX] & 0xFFFCu);
            g_audio_state.compat_ready = 1u;
        }
    }
    mk_audio_copy_limited(g_audio_state.info.device.name,
                          mk_audio_ac97_vendor_name(pci->vendor_id),
                          MAX_AUDIO_DEV_LEN);
    mk_audio_copy_limited(g_audio_state.info.device.version,
                          mk_audio_auich_family_name(pci->device_id),
                          MAX_AUDIO_DEV_LEN);
    memset(location, 0, sizeof(location));
    mk_audio_format_pci_location(location, sizeof(location), pci);
    if (g_audio_state.compat_ready) {
        g_audio_state.info.flags |= MK_AUDIO_CAPS_CAPTURE;
        g_audio_state.info.status.mode = AUMODE_PLAY | AUMODE_RECORD;
        mk_audio_copy_limited(g_audio_state.info.device.config, location, MAX_AUDIO_DEV_LEN);
        mk_audio_compat_apply_device_quirks();
        (void)mk_audio_compat_reset_codec();
        if (g_audio_state.compat_codec_ready) {
            /* Preserve the software defaults/current mixer state instead of
             * inheriting the codec reset state, which may come up muted. */
            mk_audio_compat_sync_codec_caps();
            mk_audio_compat_apply_params();
            mk_audio_compat_apply_mixer_state();
        }
        if (pci->irq_line >= 16u) {
            (void)kernel_irq_register_handler(pci->irq_line, mk_audio_compat_irq_handler);
            kernel_irq_unmask(pci->irq_line);
            g_audio_state.compat_irq_registered = 1u;
        } else {
            /*
             * The current PIC layer only supports one handler per IRQ line.
             * On laptops like the T61 the audio INTx line may be shared with
             * SATA/USB, so do not claim or mask it here.
             */
        }
    } else {
        mk_audio_copy_limited(g_audio_state.info.device.config, "bar-unavailable", MAX_AUDIO_DEV_LEN);
    }
    mk_audio_refresh_topology_snapshot();
}

static __attribute__((unused)) int mk_audio_azalia_reset_controller(void) {
    uint32_t control;

    if (g_audio_state.azalia_base == 0u) {
        return -1;
    }

    control = mk_audio_azalia_read32(g_audio_state.azalia_base, HDA_GCTL);
    mk_audio_azalia_write32(g_audio_state.azalia_base, HDA_GCTL, control & ~HDA_GCTL_CRST);
    if (mk_audio_azalia_wait32(HDA_GCTL, HDA_GCTL_CRST, 0u, HDA_POLL_TIMEOUT) != 0) {
        g_audio_state.azalia_ready = 0u;
        return -1;
    }
    mk_audio_azalia_busy_delay(100u);

    control = mk_audio_azalia_read32(g_audio_state.azalia_base, HDA_GCTL);
    mk_audio_azalia_write32(g_audio_state.azalia_base, HDA_GCTL, control | HDA_GCTL_CRST);
    if (mk_audio_azalia_wait32(HDA_GCTL, HDA_GCTL_CRST, HDA_GCTL_CRST, HDA_POLL_TIMEOUT) != 0) {
        g_audio_state.azalia_ready = 0u;
        return -1;
    }
    mk_audio_azalia_busy_delay(100u);
    g_audio_state.azalia_ready = 1u;
    return 0;
}

static int mk_audio_azalia_try_output_stream_candidate(uint8_t regindex, uint8_t stream_number) {
    if (stream_number == 0u || regindex >= 30u) {
        return -1;
    }
    g_audio_state.azalia_output_stream_index = regindex;
    g_audio_state.azalia_output_stream_number = stream_number;
    g_audio_state.azalia_output_regbase = HDA_SD_BASE + ((uint32_t)regindex * HDA_SD_SIZE);
    return 0;
}

static int mk_audio_azalia_select_output_stream_from(uint8_t start_regindex) {
    uint8_t iss;
    uint8_t oss;
    uint8_t bss;
    uint8_t total;
    uint8_t first_output;

    g_audio_state.azalia_output_stream_index = 0u;
    g_audio_state.azalia_output_stream_number = 0u;
    g_audio_state.azalia_output_regbase = 0u;

    iss = (uint8_t)((g_audio_state.azalia_gcap >> HDA_GCAP_ISS_SHIFT) & HDA_GCAP_ISS_MASK);
    oss = (uint8_t)((g_audio_state.azalia_gcap >> HDA_GCAP_OSS_SHIFT) & HDA_GCAP_OSS_MASK);
    bss = (uint8_t)((g_audio_state.azalia_gcap >> HDA_GCAP_BSS_SHIFT) & HDA_GCAP_BSS_MASK);
    if (oss == 0u && bss == 0u) {
        return -1;
    }
    first_output = iss;
    total = (uint8_t)(iss + oss + bss);
    if (total <= first_output) {
        return -1;
    }

    if (start_regindex < first_output || start_regindex >= total) {
        start_regindex = first_output;
    }

    for (uint8_t pass = 0u; pass < 2u; ++pass) {
        uint8_t begin = pass == 0u ? start_regindex : first_output;
        uint8_t end = pass == 0u ? total : start_regindex;

        for (uint8_t regindex = begin; regindex < end; ++regindex) {
            uint8_t stream_number;

            if (regindex < iss) {
                continue;
            }
            stream_number = (uint8_t)((regindex - iss) + 1u);
            if (stream_number == 0u) {
                continue;
            }
            if (mk_audio_azalia_try_output_stream_candidate(regindex, stream_number) == 0) {
                return 0;
            }
        }
    }

    if (oss != 0u) {
        g_audio_state.azalia_output_stream_index = iss;
        g_audio_state.azalia_output_stream_number = 1u;
        g_audio_state.azalia_output_regbase =
            HDA_SD_BASE + ((uint32_t)g_audio_state.azalia_output_stream_index * HDA_SD_SIZE);
        return 0;
    }
    g_audio_state.azalia_output_stream_index = iss;
    g_audio_state.azalia_output_stream_number = 1u;
    g_audio_state.azalia_output_regbase =
        HDA_SD_BASE + ((uint32_t)g_audio_state.azalia_output_stream_index * HDA_SD_SIZE);
    return 0;
}

static __attribute__((unused)) int mk_audio_azalia_select_output_stream(void) {
    return mk_audio_azalia_select_output_stream_from(0u);
}

static int mk_audio_azalia_ring_size(uint8_t caps, uint8_t *selector_out, uint16_t *entries_out) {
    if (selector_out == 0 || entries_out == 0) {
        return -1;
    }

    if ((caps & HDA_CORBSIZE_CAP_256) != 0u) {
        *selector_out = HDA_CORBSIZE_SEL_256;
        *entries_out = 256u;
        return 0;
    }
    if ((caps & HDA_CORBSIZE_CAP_16) != 0u) {
        *selector_out = HDA_CORBSIZE_SEL_16;
        *entries_out = 16u;
        return 0;
    }
    if ((caps & HDA_CORBSIZE_CAP_2) != 0u) {
        *selector_out = HDA_CORBSIZE_SEL_2;
        *entries_out = 2u;
        return 0;
    }
    return -1;
}

static int mk_audio_azalia_init_command_rings(void) {
    uint8_t corb_sel;
    uint8_t rirb_sel;
    uint16_t corbrp;
    uint16_t rirbwp;
    uint16_t corb_entries;
    uint16_t rirb_entries;
    uint8_t corb_caps;
    uint8_t rirb_caps;

    if (g_audio_state.azalia_base == 0u || !g_audio_state.azalia_ready) {
        return -1;
    }

    corb_caps = mk_audio_azalia_read8(g_audio_state.azalia_base, HDA_CORBSIZE) & 0xf0u;
    rirb_caps = mk_audio_azalia_read8(g_audio_state.azalia_base, HDA_RIRBSIZE) & 0xf0u;
    if (mk_audio_azalia_ring_size(corb_caps, &corb_sel, &corb_entries) != 0 ||
        mk_audio_azalia_ring_size(rirb_caps, &rirb_sel, &rirb_entries) != 0) {
        return -1;
    }

    if (mk_audio_azalia_alloc_dma_buffers() != 0) {
        g_audio_state.azalia_corb_ready = 0u;
        return -1;
    }

    memset(g_audio_state.azalia_corb, 0, sizeof(uint32_t) * 256u);
    memset(g_audio_state.azalia_rirb,
           0,
           sizeof(struct mk_audio_hda_rirb_entry) * 256u);
    mk_audio_azalia_write8(g_audio_state.azalia_base, HDA_CORBCTL, 0u);
    mk_audio_azalia_write8(g_audio_state.azalia_base, HDA_RIRBCTL, 0u);
    if (mk_audio_azalia_wait8(HDA_CORBCTL, HDA_CORBCTL_CORBRUN, 0u, HDA_POLL_TIMEOUT) != 0 ||
        mk_audio_azalia_wait8(HDA_RIRBCTL, HDA_RIRBCTL_DMAEN, 0u, HDA_POLL_TIMEOUT) != 0) {
        g_audio_state.azalia_corb_ready = 0u;
        return -1;
    }
    mk_audio_azalia_write16(g_audio_state.azalia_base, HDA_STATESTS, HDA_STATESTS_SDIWAKE);
    mk_audio_azalia_write8(g_audio_state.azalia_base, HDA_CORBSTS, 0xffu);
    mk_audio_azalia_write8(g_audio_state.azalia_base, HDA_RIRBSTS, 0xffu);
    mk_audio_azalia_write8(g_audio_state.azalia_base, HDA_CORBSIZE, corb_sel);
    mk_audio_azalia_write8(g_audio_state.azalia_base, HDA_RIRBSIZE, rirb_sel);
    mk_audio_azalia_write32(g_audio_state.azalia_base, HDA_CORBLBASE,
                            (uint32_t)(uintptr_t)&g_audio_state.azalia_corb[0]);
    mk_audio_azalia_write32(g_audio_state.azalia_base, HDA_CORBUBASE, 0u);
    mk_audio_azalia_write32(g_audio_state.azalia_base, HDA_RIRBLBASE,
                            (uint32_t)(uintptr_t)&g_audio_state.azalia_rirb[0]);
    mk_audio_azalia_write32(g_audio_state.azalia_base, HDA_RIRBUBASE, 0u);
    corbrp = mk_audio_azalia_read16(g_audio_state.azalia_base, HDA_CORBRP);
    mk_audio_azalia_write16(g_audio_state.azalia_base, HDA_CORBRP, (uint16_t)(corbrp | HDA_CORBRP_CORBRPRST));
    mk_audio_azalia_write16(g_audio_state.azalia_base, HDA_CORBRP, (uint16_t)(corbrp & ~HDA_CORBRP_CORBRPRST));
    rirbwp = mk_audio_azalia_read16(g_audio_state.azalia_base, HDA_RIRBWP);
    mk_audio_azalia_write16(g_audio_state.azalia_base, HDA_RIRBWP, (uint16_t)(rirbwp | HDA_RIRBWP_RST));

    if (mk_audio_azalia_wait16(HDA_CORBRP, HDA_CORBRP_CORBRPRST, 0u, HDA_POLL_TIMEOUT) != 0) {
        g_audio_state.azalia_corb_ready = 0u;
        return -1;
    }

    mk_audio_azalia_write16(g_audio_state.azalia_base,
                            HDA_CORBWP,
                            (uint16_t)(mk_audio_azalia_read16(g_audio_state.azalia_base, HDA_CORBWP) &
                                       ~HDA_CORBWP_MASK));
    mk_audio_azalia_write16(g_audio_state.azalia_base, HDA_RINTCNT, 1u);
    mk_audio_azalia_write8(g_audio_state.azalia_base, HDA_CORBCTL, HDA_CORBCTL_CORBRUN);
    mk_audio_azalia_write8(g_audio_state.azalia_base,
                           HDA_RIRBCTL,
                           (uint8_t)(HDA_RIRBCTL_DMAEN | HDA_RIRBCTL_RINTCTL));

    if (mk_audio_azalia_wait8(HDA_CORBCTL, HDA_CORBCTL_CORBRUN, HDA_CORBCTL_CORBRUN, HDA_POLL_TIMEOUT) != 0) {
        g_audio_state.azalia_corb_ready = 0u;
        return -1;
    }
    if (mk_audio_azalia_wait8(HDA_RIRBCTL, HDA_RIRBCTL_DMAEN, HDA_RIRBCTL_DMAEN, HDA_POLL_TIMEOUT) != 0) {
        g_audio_state.azalia_corb_ready = 0u;
        return -1;
    }

    g_audio_state.azalia_corb_entries = corb_entries;
    g_audio_state.azalia_rirb_entries = rirb_entries;
    g_audio_state.azalia_rirb_read_pos =
        (uint16_t)(mk_audio_azalia_read16(g_audio_state.azalia_base, HDA_RIRBWP) & 0x00ffu);
    g_audio_state.azalia_unsol_rp = 0u;
    g_audio_state.azalia_unsol_wp = 0u;
    g_audio_state.azalia_unsol_kick = 0u;
    memset(g_audio_state.azalia_unsol_queue, 0, sizeof(g_audio_state.azalia_unsol_queue));
    g_audio_state.azalia_corb_ready = 1u;
    return 0;
}

static int mk_audio_azalia_command_raw(uint8_t codec,
                                       uint8_t nid,
                                       uint32_t verb_payload,
                                       uint32_t *response_out) {
    uint16_t corb_wp;
    uint16_t rirb_wp;
    uint16_t next_wp;
    uint16_t rirb_rp;
    uint16_t current_rirb_wp;
    uint32_t command;
    uint8_t ring_retry = 0u;

    command = ((uint32_t)(codec & 0x0fu) << 28) |
              ((uint32_t)nid << 20) |
              (verb_payload & 0x000fffffu);

retry_corb:
    if (!g_audio_state.azalia_corb_ready ||
        g_audio_state.azalia_corb_entries == 0u ||
        g_audio_state.azalia_rirb_entries == 0u) {
        goto immediate_fallback;
    }

    current_rirb_wp =
        (uint16_t)(mk_audio_azalia_read16(g_audio_state.azalia_base, HDA_RIRBWP) & 0x00ffu);
    rirb_rp = g_audio_state.azalia_rirb_read_pos;
    while (rirb_rp != current_rirb_wp) {
        struct mk_audio_hda_rirb_entry *entry;

        rirb_rp = (uint16_t)((rirb_rp + 1u) % g_audio_state.azalia_rirb_entries);
        entry = &g_audio_state.azalia_rirb[rirb_rp];
        if ((entry->response_ex & HDA_RIRB_RESP_UNSOL) != 0u) {
            mk_audio_azalia_queue_unsol_event(entry->response, entry->response_ex);
        }
    }
    g_audio_state.azalia_rirb_read_pos = rirb_rp;
    mk_audio_azalia_kick_unsol_events();
    mk_audio_azalia_write8(g_audio_state.azalia_base,
                           HDA_RIRBSTS,
                           (uint8_t)(HDA_RIRBSTS_RINTFL | HDA_RIRBSTS_RIRBOIS));
    corb_wp = (uint16_t)(mk_audio_azalia_read16(g_audio_state.azalia_base, HDA_CORBWP) & 0x00ffu);
    next_wp = (uint16_t)((corb_wp + 1u) % g_audio_state.azalia_corb_entries);
    rirb_rp = g_audio_state.azalia_rirb_read_pos;
    g_audio_state.azalia_corb[next_wp] = command;
    mk_audio_azalia_write16(g_audio_state.azalia_base, HDA_CORBWP, next_wp);

    for (uint32_t i = 0u; i < HDA_CORB_TIMEOUT; ++i) {
        rirb_wp = (uint16_t)(mk_audio_azalia_read16(g_audio_state.azalia_base, HDA_RIRBWP) & 0x00ffu);
        if (rirb_rp != rirb_wp) {
            while (rirb_rp != rirb_wp) {
                struct mk_audio_hda_rirb_entry *entry;

                rirb_rp = (uint16_t)((rirb_rp + 1u) % g_audio_state.azalia_rirb_entries);
                entry = &g_audio_state.azalia_rirb[rirb_rp];
                if ((entry->response_ex & HDA_RIRB_RESP_UNSOL) != 0u) {
                    mk_audio_azalia_queue_unsol_event(entry->response, entry->response_ex);
                    continue;
                }
                if ((entry->response_ex & HDA_RIRB_RESP_CODEC_MASK) != (uint32_t)(codec & 0x0fu)) {
                    continue;
                }
                g_audio_state.azalia_rirb_read_pos = rirb_rp;
                mk_audio_azalia_kick_unsol_events();
                if (response_out != 0) {
                    *response_out = entry->response;
                }
                mk_audio_azalia_write8(g_audio_state.azalia_base,
                                       HDA_RIRBSTS,
                                       (uint8_t)(HDA_RIRBSTS_RINTFL | HDA_RIRBSTS_RIRBOIS));
                return 0;
            }
            g_audio_state.azalia_rirb_read_pos = rirb_rp;
        }
        mk_audio_cooperative_delay(i);
    }

    /* Some older controllers/codecs can expose a live CORB/RIRB pair but
     * still get the ring into a stale/overflowed state during early probe.
     * Reinitialize the command rings once before abandoning the transport. */
    mk_audio_azalia_write8(g_audio_state.azalia_base,
                           HDA_RIRBSTS,
                           (uint8_t)(HDA_RIRBSTS_RINTFL | HDA_RIRBSTS_RIRBOIS));
    if (!ring_retry && mk_audio_azalia_init_command_rings() == 0) {
        ring_retry = 1u;
        goto retry_corb;
    }
    goto immediate_fallback;

immediate_fallback:
    if (g_audio_state.azalia_base == 0u) {
        return -1;
    }
    for (uint32_t i = 0u; i < HDA_CORB_TIMEOUT; ++i) {
        uint16_t irs = mk_audio_azalia_read16(g_audio_state.azalia_base, HDA_IRS);

        if ((irs & HDA_IRS_BUSY) == 0u) {
            /* Start from a clean immediate-command state instead of
             * carrying stale IRV/address bits into the next verb. */
            mk_audio_azalia_write16(g_audio_state.azalia_base, HDA_IRS, 0u);
            mk_audio_azalia_write32(g_audio_state.azalia_base, HDA_IC, command);
            mk_audio_azalia_write16(g_audio_state.azalia_base, HDA_IRS, HDA_IRS_BUSY);
            for (uint32_t j = 0u; j < HDA_CORB_TIMEOUT; ++j) {
                irs = mk_audio_azalia_read16(g_audio_state.azalia_base, HDA_IRS);
                if ((irs & HDA_IRS_BUSY) == 0u &&
                    (irs & HDA_IRS_VALID) != 0u) {
                    mk_audio_azalia_kick_unsol_events();
                    if (response_out != 0) {
                        *response_out = mk_audio_azalia_read32(g_audio_state.azalia_base, HDA_IR);
                    }
                    mk_audio_azalia_write16(g_audio_state.azalia_base, HDA_IRS, 0u);
                    return 0;
                }
                mk_audio_cooperative_delay(j);
            }
            return -1;
        }
        mk_audio_cooperative_delay(i);
    }

    return -1;
}

static int mk_audio_azalia_command(uint8_t codec,
                                   uint8_t nid,
                                   uint16_t verb,
                                   uint8_t payload,
                                   uint32_t *response_out) {
    return mk_audio_azalia_command_raw(codec,
                                       nid,
                                       (((uint32_t)verb & 0x0fffu) << 8) | (uint32_t)payload,
                                       response_out);
}

static int mk_audio_azalia_command_retry(uint8_t codec,
                                         uint8_t nid,
                                         uint16_t verb,
                                         uint8_t payload,
                                         uint32_t *response_out,
                                         uint32_t attempts) {
    if (attempts == 0u) {
        attempts = 1u;
    }
    for (uint32_t attempt = 0u; attempt < attempts; ++attempt) {
        if (mk_audio_azalia_command(codec, nid, verb, payload, response_out) == 0) {
            return 0;
        }
        mk_audio_compat_delay();
    }
    return -1;
}

static int mk_audio_azalia_get_parameter_retry(uint8_t codec,
                                               uint8_t nid,
                                               uint8_t parameter,
                                               uint32_t *response_out,
                                               uint32_t attempts) {
    if (attempts == 0u) {
        attempts = 1u;
    }
    for (uint32_t attempt = 0u; attempt < attempts; ++attempt) {
        if (mk_audio_azalia_command(codec,
                                    nid,
                                    HDA_VERB_GET_PARAMETER,
                                    parameter,
                                    response_out) == 0) {
            return 0;
        }
        mk_audio_compat_delay();
    }
    return -1;
}

static void mk_audio_azalia_sync_fg_caps(void) {
    uint32_t response = 0u;

    g_audio_state.azalia_fg_stream_formats = 0u;
    g_audio_state.azalia_fg_pcm = 0u;
    g_audio_state.azalia_fg_input_amp_cap = 0u;
    g_audio_state.azalia_fg_output_amp_cap = 0u;
    if (!g_audio_state.azalia_codec_probed || g_audio_state.azalia_afg_nid == 0u) {
        return;
    }
    mk_audio_azalia_power_widget(g_audio_state.azalia_afg_nid);
    mk_audio_compat_delay();

    if (mk_audio_azalia_get_parameter_retry(g_audio_state.azalia_codec_address,
                                            g_audio_state.azalia_afg_nid,
                                            0x0bu,
                                            &response,
                                            3u) == 0 &&
        (response & HDA_STREAM_FORMAT_PCM) != 0u) {
        g_audio_state.azalia_fg_stream_formats = response;
    }
    if (mk_audio_azalia_get_parameter_retry(g_audio_state.azalia_codec_address,
                                            g_audio_state.azalia_afg_nid,
                                            0x0au,
                                            &response,
                                            3u) == 0) {
        g_audio_state.azalia_fg_pcm = response;
    }
    if (mk_audio_azalia_get_parameter_retry(g_audio_state.azalia_codec_address,
                                            g_audio_state.azalia_afg_nid,
                                            HDA_PARAM_INPUT_AMP_CAP,
                                            &response,
                                            3u) == 0) {
        g_audio_state.azalia_fg_input_amp_cap = response & ~(0x7fu << 24);
    }
    if (mk_audio_azalia_get_parameter_retry(g_audio_state.azalia_codec_address,
                                            g_audio_state.azalia_afg_nid,
                                            HDA_PARAM_OUTPUT_AMP_CAP,
                                            &response,
                                            3u) == 0) {
        g_audio_state.azalia_fg_output_amp_cap = response & ~(0x7fu << 24);
    }
}

static uint16_t mk_audio_azalia_refresh_codec_mask(void) {
    uint16_t state_status;

    if (g_audio_state.azalia_base == 0u) {
        return g_audio_state.azalia_codec_mask;
    }

    for (uint32_t attempt = 0u; attempt < 32u; ++attempt) {
        state_status = mk_audio_azalia_read16(g_audio_state.azalia_base, HDA_STATESTS);
        if (state_status != 0u && state_status != 0xffffu) {
            g_audio_state.azalia_codec_mask |= state_status;
            mk_audio_azalia_write16(g_audio_state.azalia_base, HDA_STATESTS, state_status);
            break;
        }
        mk_audio_compat_delay();
    }

    return g_audio_state.azalia_codec_mask;
}

static void mk_audio_azalia_init_pin_widget(uint8_t nid,
                                            uint32_t pin_caps,
                                            uint32_t config_default) {
    uint32_t response = 0u;
    uint32_t device;
    uint32_t port;
    uint8_t pin_ctl = 0u;
    uint32_t vref_caps;
    uint32_t widget_caps = 0u;
    int output_bit = -1;

    if (nid == 0u || pin_caps == 0u || pin_caps == 0xffffffffu) {
        return;
    }

    device = mk_audio_hda_config_device(config_default);
    port = mk_audio_hda_config_port(config_default);
    if (port == HDA_CONFIG_PORT_NONE) {
        return;
    }

    if (device == HDA_CONFIG_DEVICE_LINEOUT ||
        device == HDA_CONFIG_DEVICE_SPEAKER ||
        device == HDA_CONFIG_DEVICE_HEADPHONE ||
        device == HDA_CONFIG_DEVICE_SPDIFOUT ||
        device == HDA_CONFIG_DEVICE_DIGITALOUT) {
        if ((pin_caps & HDA_PINCAP_OUTPUT) != 0u) {
            pin_ctl = HDA_PINCTL_OUT_EN;
        }
    } else {
        if ((pin_caps & HDA_PINCAP_INPUT) != 0u) {
            pin_ctl = HDA_PINCTL_IN_EN;
        } else if ((pin_caps & HDA_PINCAP_OUTPUT) != 0u) {
            pin_ctl = HDA_PINCTL_OUT_EN;
        }
    }

    if ((pin_ctl & HDA_PINCTL_IN_EN) != 0u &&
        device == HDA_CONFIG_DEVICE_MICIN) {
        vref_caps = (pin_caps >> HDA_PINCAP_VREF_SHIFT) & HDA_PINCAP_VREF_MASK;
        if ((vref_caps & (1u << HDA_PINCTL_VREF_80)) != 0u) {
            pin_ctl |= HDA_PINCTL_VREF_80;
        } else if ((vref_caps & (1u << HDA_PINCTL_VREF_50)) != 0u) {
            pin_ctl |= HDA_PINCTL_VREF_50;
        }
    }

    if ((pin_ctl & HDA_PINCTL_OUT_EN) != 0u &&
        device == HDA_CONFIG_DEVICE_HEADPHONE) {
        pin_ctl |= HDA_PINCTL_HP_EN;
    }

    if (pin_ctl == 0u) {
        return;
    }

    (void)mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                  nid,
                                  HDA_VERB_SET_PIN_WIDGET_CONTROL,
                                  pin_ctl,
                                  &response);
    mk_audio_azalia_enable_eapd(nid, pin_caps);

    if ((pin_ctl & HDA_PINCTL_OUT_EN) == 0u ||
        (pin_caps & HDA_PINCAP_PRESENCE) == 0u ||
        (mk_audio_hda_config_misc(config_default) & HDA_CONFIG_MISC_PRESENCEOV) != 0u ||
        (port != HDA_CONFIG_PORT_JACK && port != HDA_CONFIG_PORT_BOTH) ||
        mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                nid,
                                HDA_VERB_GET_PARAMETER,
                                HDA_PARAM_AUDIO_WIDGET_CAP,
                                &widget_caps) != 0 ||
        (widget_caps & HDA_WCAP_UNSOL) == 0u) {
        return;
    }

    output_bit = mk_audio_output_bit_from_ord(mk_audio_hda_output_mask(pin_caps, config_default), 0u);
    if (output_bit < 0 || output_bit >= 4) {
        return;
    }
    if (g_audio_state.azalia_sense_pin_count < 4u &&
        (mk_audio_hda_config_misc(config_default) & HDA_CONFIG_MISC_PRESENCEOV) == 0u) {
        uint8_t index = g_audio_state.azalia_sense_pin_count++;

        g_audio_state.azalia_sense_pin_nids[index] = nid;
        g_audio_state.azalia_sense_pin_output_bits[index] = (uint8_t)output_bit;
        if (!mk_audio_azalia_output_is_speaker(output_bit)) {
            g_audio_state.azalia_spkr_muter_mask |= (uint8_t)(1u << index);
        }
    }
    if (mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                nid,
                                HDA_VERB_SET_UNSOLICITED_RESPONSE,
                                (uint8_t)(HDA_UNSOL_ENABLE | mk_audio_azalia_output_unsol_tag(output_bit)),
                                &response) == 0) {
        g_audio_state.azalia_unsol_output_mask |= (uint8_t)(1u << output_bit);
    }
}

static int mk_audio_azalia_probe_widget_range(uint8_t first_nid,
                                              uint8_t count,
                                              int *best_output_priority) {
    int discovered_widgets = 0;

    for (uint8_t i = 0u; i < count; ++i) {
        uint8_t nid = (uint8_t)(first_nid + i);
        uint32_t caps;
        uint32_t type;

        if (nid == 0u) {
            continue;
        }
        if (mk_audio_azalia_get_parameter_retry(g_audio_state.azalia_codec_address,
                                                nid,
                                                HDA_PARAM_AUDIO_WIDGET_CAP,
                                                &caps,
                                                3u) != 0 ||
            caps == 0u ||
            caps == 0xffffffffu) {
            mk_audio_debug_azalia_widget(nid, 0xffu, 0u, 0u, 0u, -1);
            continue;
        }
        type = (caps >> HDA_WCAP_TYPE_SHIFT) & HDA_WCAP_TYPE_MASK;
        discovered_widgets++;
        mk_audio_azalia_power_widget(nid);
        if ((type == HDA_WID_AUD_MIXER || type == HDA_WID_AUD_SELECTOR) &&
            !mk_audio_azalia_widget_check_connection(nid, 0u)) {
            g_audio_state.azalia_widget_disabled[nid] = 1u;
            kernel_debug_printf("audio: hda widget pruned nid=%u type=%u no-conn-path\n",
                                (unsigned int)nid,
                                (unsigned int)type);
            mk_audio_debug_azalia_widget(nid, type, caps, 0u, 0u, -1);
            continue;
        }
        g_audio_state.azalia_widget_disabled[nid] = 0u;
        mk_audio_azalia_repair_connection_select(nid, type);
        if (type == HDA_WID_AUD_OUT || type == HDA_WID_AUD_IN) {
            uint32_t encodings = 0u;
            uint32_t pcm = 0u;

            if (mk_audio_azalia_query_widget_audio_caps(nid, caps, &encodings, &pcm) != 0) {
                kernel_debug_printf("audio: hda widget audio caps rejected nid=%u type=%u caps=%x\n",
                                    (unsigned int)nid,
                                    (unsigned int)type,
                                    (unsigned int)caps);
                mk_audio_debug_azalia_widget(nid, type, caps, 0u, 0u, -1);
                continue;
            }
            if (type == HDA_WID_AUD_OUT && g_audio_state.azalia_output_dac_nid == 0u) {
                if ((caps & HDA_WCAP_DIGITAL) == 0u &&
                    g_audio_state.azalia_analog_dac_count < 0xffu) {
                    g_audio_state.azalia_analog_dac_count++;
                }
                g_audio_state.azalia_output_dac_nid = nid;
                mk_audio_debug_azalia_widget(nid, type, caps, encodings, pcm, nid);
                continue;
            }
            if (type == HDA_WID_AUD_OUT &&
                (caps & HDA_WCAP_DIGITAL) == 0u &&
                g_audio_state.azalia_analog_dac_count < 0xffu) {
                g_audio_state.azalia_analog_dac_count++;
            }
            if (type == HDA_WID_AUD_IN && g_audio_state.azalia_input_dac_nid == 0u) {
                g_audio_state.azalia_input_dac_nid = nid;
                mk_audio_debug_azalia_widget(nid, type, caps, encodings, pcm, -1);
                continue;
            }
            mk_audio_debug_azalia_widget(nid, type, caps, encodings, pcm, -1);
            continue;
        }
        if (type == HDA_WID_PIN) {
            uint32_t pin_caps = 0u;
            uint32_t config_default = 0u;
            uint32_t output_mask = 0u;
            uint32_t input_mask = 0u;
            uint32_t device;
            uint32_t port;
            int handled_special_output = 0;
            int candidate_dac = -1;
            int priority;

            if (mk_audio_azalia_get_parameter_retry(g_audio_state.azalia_codec_address,
                                                    nid,
                                                    HDA_PARAM_PIN_CAP,
                                                    &pin_caps,
                                                    3u) != 0 ||
                pin_caps == 0u ||
                pin_caps == 0xffffffffu) {
                mk_audio_debug_azalia_widget(nid, type, caps, pin_caps, 0u, -1);
                continue;
            }

            (void)mk_audio_azalia_command_retry(g_audio_state.azalia_codec_address,
                                                nid,
                                                HDA_VERB_GET_CONFIG_DEFAULT,
                                                0u,
                                                &config_default,
                                                3u);
            mk_audio_azalia_apply_widget_quirks(nid, &config_default);
            device = mk_audio_hda_config_device(config_default);
            port = mk_audio_hda_config_port(config_default);
            if (port == HDA_CONFIG_PORT_JACK &&
                device == HDA_CONFIG_DEVICE_LINEOUT &&
                g_audio_state.azalia_output_jack_count < 0xffu) {
                g_audio_state.azalia_output_jack_count++;
            }
            mk_audio_azalia_init_pin_widget(nid, pin_caps, config_default);
            output_mask = mk_audio_hda_output_mask(pin_caps, config_default);
            input_mask = mk_audio_hda_input_mask(pin_caps, config_default);
            g_audio_state.azalia_output_mask |= output_mask;
            g_audio_state.azalia_input_mask |= input_mask;
            if (output_mask != 0u) {
                candidate_dac = mk_audio_azalia_find_output_dac(nid, 0u);
            }
            priority = mk_audio_hda_output_priority(pin_caps, config_default);
            if (candidate_dac >= 0 && output_mask != 0u) {
                handled_special_output = mk_audio_azalia_register_special_output_pin(nid,
                                                                                     (uint8_t)candidate_dac,
                                                                                     priority,
                                                                                     config_default);
            }
            if (candidate_dac >= 0 &&
                !handled_special_output &&
                priority > *best_output_priority) {
                g_audio_state.azalia_output_pin_nid = nid;
                g_audio_state.azalia_output_dac_nid = (uint8_t)candidate_dac;
                *best_output_priority = priority;
            }
            if (candidate_dac >= 0 && output_mask != 0u && !handled_special_output) {
                (void)mk_audio_azalia_register_output_path(output_mask,
                                                           nid,
                                                           (uint8_t)candidate_dac,
                                                           priority,
                                                           config_default);
            }
            if (g_audio_state.azalia_input_pin_nid == 0u && input_mask != 0u) {
                g_audio_state.azalia_input_pin_nid = nid;
            }
            if (input_mask != 0u) {
                mk_audio_azalia_register_input_path(input_mask, nid);
            }
            mk_audio_debug_azalia_widget(nid, type, caps, pin_caps, config_default, candidate_dac);
        } else {
            mk_audio_debug_azalia_widget(nid, type, caps, 0u, 0u, -1);
        }
    }

    return discovered_widgets;
}

static int mk_audio_azalia_probe_codec(void) {
    uint32_t vendor_response;
    uint32_t revision_response;
    uint32_t response;
    uint32_t subnodes;
    uint32_t afg_subnodes;
    uint8_t first_nid;
    uint8_t count;
    uint8_t afg_first_nid;
    uint8_t afg_count;
    uint16_t codec_mask;
    uint8_t had_codec;
    uint8_t prev_codec_address;
    uint8_t prev_afg_nid;
    uint32_t prev_vendor_id;
    uint32_t fallback_vendor_id = 0u;
    uint8_t fallback_codec = 0u;
    uint8_t have_function_nodes;

    had_codec = g_audio_state.azalia_codec_probed;
    prev_vendor_id = g_audio_state.azalia_vendor_id;
    prev_codec_address = g_audio_state.azalia_codec_address;
    prev_afg_nid = g_audio_state.azalia_afg_nid;
    if (!had_codec) {
        g_audio_state.azalia_codec_probed = 0u;
        g_audio_state.azalia_vendor_id = 0u;
        g_audio_state.azalia_codec_address = 0u;
        g_audio_state.azalia_afg_nid = 0u;
    }
    if (!g_audio_state.azalia_corb_ready) {
        return -1;
    }

    codec_mask = mk_audio_azalia_refresh_codec_mask();
    for (uint8_t pass = 0u; pass < 2u; ++pass) {
        for (uint8_t codec = 0u; codec < HDA_MAX_CODECS; ++codec) {
            uint8_t probed_by_mask = (uint8_t)((codec_mask & (1u << codec)) != 0u);

            if (pass == 0u && !probed_by_mask) {
                continue;
            }
            if (pass != 0u && probed_by_mask) {
                continue;
            }

            if (mk_audio_azalia_get_parameter_retry(codec,
                                                    0u,
                                                    HDA_PARAM_REVISION_ID,
                                                    &revision_response,
                                                    3u) != 0) {
                continue;
            }
            if (mk_audio_azalia_get_parameter_retry(codec,
                                                    0u,
                                                    HDA_PARAM_VENDOR_ID,
                                                    &vendor_response,
                                                    3u) != 0 ||
                vendor_response == 0u ||
                vendor_response == 0xffffffffu) {
                continue;
            }

            if (fallback_vendor_id == 0u) {
                fallback_vendor_id = vendor_response;
                fallback_codec = codec;
            }

            have_function_nodes = 0u;
            kernel_debug_printf("audio: hda codec root ok codec=%u vendor=%x rev=%x mask=%x\n",
                                (unsigned int)codec,
                                (unsigned int)vendor_response,
                                (unsigned int)revision_response,
                                (unsigned int)codec_mask);
            if (mk_audio_azalia_get_parameter_retry(codec,
                                                    0u,
                                                    HDA_PARAM_SUB_NODE_COUNT,
                                                    &subnodes,
                                                    3u) == 0) {
                first_nid = (uint8_t)((subnodes >> 16) & 0xffu);
                count = (uint8_t)(subnodes & 0xffu);
                have_function_nodes = (uint8_t)(count != 0u);
                for (uint8_t i = 0u; i < count; ++i) {
                    if (mk_audio_azalia_get_parameter_retry(codec,
                                                            (uint8_t)(first_nid + i),
                                                            HDA_PARAM_FUNCTION_GROUP_TYPE,
                                                            &response,
                                                            3u) == 0 &&
                        (response & 0xffu) == HDA_FGTYPE_AUDIO) {
                        uint32_t power_response = 0u;
                        uint8_t afg_nid = (uint8_t)(first_nid + i);

                        if (mk_audio_azalia_get_parameter_retry(codec,
                                                                afg_nid,
                                                                HDA_PARAM_SUB_NODE_COUNT,
                                                                &afg_subnodes,
                                                                3u) == 0) {
                            afg_first_nid = (uint8_t)((afg_subnodes >> 16) & 0xffu);
                            afg_count = (uint8_t)(afg_subnodes & 0xffu);
                            if (afg_count != 0u && afg_first_nid < 2u) {
                                kernel_debug_printf("audio: hda rejecting invalid afg widget range codec=%u afg=%u first=%u count=%u vendor=%x\n",
                                                    (unsigned int)codec,
                                                    (unsigned int)afg_nid,
                                                    (unsigned int)afg_first_nid,
                                                    (unsigned int)afg_count,
                                                    (unsigned int)vendor_response);
                                continue;
                            }
                            kernel_debug_printf("audio: hda audio fg ok codec=%u afg=%u widgets-first=%u widgets-count=%u vendor=%x\n",
                                                (unsigned int)codec,
                                                (unsigned int)afg_nid,
                                                (unsigned int)afg_first_nid,
                                                (unsigned int)afg_count,
                                                (unsigned int)vendor_response);
                        } else {
                            kernel_debug_printf("audio: hda audio fg subnode query failed codec=%u afg=%u vendor=%x\n",
                                                (unsigned int)codec,
                                                (unsigned int)afg_nid,
                                                (unsigned int)vendor_response);
                        }

                        g_audio_state.azalia_vendor_id = vendor_response;
                        g_audio_state.azalia_codec_address = codec;
                        g_audio_state.azalia_afg_nid = afg_nid;
                        g_audio_state.azalia_codec_probed = 1u;
                        (void)mk_audio_azalia_command(codec,
                                                      g_audio_state.azalia_afg_nid,
                                                      HDA_VERB_SET_POWER_STATE,
                                                      HDA_POWER_STATE_D0,
                                                      &power_response);
                        mk_audio_compat_delay();
                        mk_audio_azalia_sync_fg_caps();
                        mk_audio_azalia_detect_quirks();
                        mk_audio_azalia_apply_gpio_quirks();
                        return 0;
                    }
                }
            } else {
                kernel_debug_printf("audio: hda root subnode query failed codec=%u vendor=%x mask=%x\n",
                                    (unsigned int)codec,
                                    (unsigned int)vendor_response,
                                    (unsigned int)codec_mask);
            }

            /* Some older real HDA codecs/controllers respond to the codec root
             * but expose unreliable function-group metadata. Fall back to a
             * wider legacy scan of low NIDs before giving up. */
            for (uint8_t nid = 2u; nid < 0x40u; ++nid) {
                if (mk_audio_azalia_get_parameter_retry(codec,
                                                        nid,
                                                        HDA_PARAM_FUNCTION_GROUP_TYPE,
                                                        &response,
                                                        2u) == 0 &&
                    (response & 0xffu) == HDA_FGTYPE_AUDIO) {
                    uint32_t power_response = 0u;

                    if (mk_audio_azalia_get_parameter_retry(codec,
                                                            nid,
                                                            HDA_PARAM_SUB_NODE_COUNT,
                                                            &afg_subnodes,
                                                            2u) == 0) {
                        afg_first_nid = (uint8_t)((afg_subnodes >> 16) & 0xffu);
                        afg_count = (uint8_t)(afg_subnodes & 0xffu);
                        if (afg_count != 0u && afg_first_nid < 2u) {
                            kernel_debug_printf("audio: hda rejecting invalid legacy afg widget range codec=%u afg=%u first=%u count=%u vendor=%x\n",
                                                (unsigned int)codec,
                                                (unsigned int)nid,
                                                (unsigned int)afg_first_nid,
                                                (unsigned int)afg_count,
                                                (unsigned int)vendor_response);
                            continue;
                        }
                    }

                    g_audio_state.azalia_vendor_id = vendor_response;
                    g_audio_state.azalia_codec_address = codec;
                    g_audio_state.azalia_afg_nid = nid;
                    g_audio_state.azalia_codec_probed = 1u;
                    (void)mk_audio_azalia_command(codec,
                                                  nid,
                                                  HDA_VERB_SET_POWER_STATE,
                                                  HDA_POWER_STATE_D0,
                                                  &power_response);
                    mk_audio_compat_delay();
                    mk_audio_azalia_sync_fg_caps();
                    kernel_debug_printf("audio: hda recovered audio fg by nid scan codec=%u nid=%u vendor=%x\n",
                                        (unsigned int)codec,
                                        (unsigned int)nid,
                                        (unsigned int)vendor_response);
                    mk_audio_azalia_detect_quirks();
                    mk_audio_azalia_apply_gpio_quirks();
                    return 0;
                }
            }

            if (!have_function_nodes) {
                kernel_debug_printf("audio: hda falling back to legacy fg scan codec=%u vendor=%x\n",
                                    (unsigned int)codec,
                                    (unsigned int)vendor_response);
            }
        }
    }

    if (fallback_vendor_id != 0u) {
        if (!had_codec) {
            g_audio_state.azalia_vendor_id = fallback_vendor_id;
            g_audio_state.azalia_codec_address = fallback_codec;
        }
        kernel_debug_printf("audio: hda codec responded without audio fg codec=%u vendor=%x mask=%x\n",
                            (unsigned int)fallback_codec,
                            (unsigned int)fallback_vendor_id,
                            (unsigned int)codec_mask);
    } else {
        kernel_debug_printf("audio: hda codec probe found no responders mask=%x\n",
                            (unsigned int)codec_mask);
    }
    if (had_codec) {
        g_audio_state.azalia_codec_probed = 1u;
        g_audio_state.azalia_vendor_id = prev_vendor_id;
        g_audio_state.azalia_codec_address = prev_codec_address;
        g_audio_state.azalia_afg_nid = prev_afg_nid;
    }

    return -1;
}

static void mk_audio_azalia_apply_known_codec_topology(void) {
    if (g_audio_state.azalia_vendor_id == HDA_QEMU_CODEC_OUTPUT) {
        kernel_debug_puts("audio: hda fallback qemu output topology\n");
        g_audio_state.azalia_output_dac_nid = 2u;
        g_audio_state.azalia_output_pin_nid = 3u;
        g_audio_state.azalia_output_mask |= 0x1u;
        (void)mk_audio_azalia_register_output_path(0x1u, 3u, 2u, 100, 0u);
        return;
    }
    if (g_audio_state.azalia_vendor_id == HDA_QEMU_CODEC_DUPLEX) {
        kernel_debug_puts("audio: hda fallback qemu duplex topology\n");
        g_audio_state.azalia_output_dac_nid = 2u;
        g_audio_state.azalia_output_pin_nid = 3u;
        g_audio_state.azalia_input_dac_nid = 4u;
        g_audio_state.azalia_input_pin_nid = 5u;
        g_audio_state.azalia_output_mask |= 0x1u;
        g_audio_state.azalia_input_mask |= 0x2u;
        (void)mk_audio_azalia_register_output_path(0x1u, 3u, 2u, 100, 0u);
        mk_audio_azalia_register_input_path(0x2u, 5u);
    }
}

static int mk_audio_azalia_probe_widgets(void) {
    uint32_t subnodes;
    uint8_t first_nid;
    uint8_t count;
    uint8_t used_reported_range = 0u;
    int best_output_priority = -1;
    uint8_t had_widgets;
    uint8_t prev_output_dac_nid;
    uint8_t prev_spkr_dac_nid;
    uint8_t prev_output_pin_nid;
    uint8_t prev_input_dac_nid;
    uint8_t prev_input_pin_nid;
    uint8_t prev_output_pin_nids[4];
    uint8_t prev_output_dac_nids[4];
    uint8_t prev_input_pin_nids[2];
    uint8_t prev_speaker2_pin_nid;
    uint8_t prev_speaker2_dac_nid;
    uint8_t prev_fhp_pin_nid;
    uint8_t prev_fhp_dac_nid;
    int8_t prev_speaker2_priority;
    uint32_t prev_speaker2_config_default;
    uint8_t prev_widget_selected[256];
    uint8_t prev_widget_selected_valid[256];
    uint8_t prev_widget_disabled[256];
    int8_t prev_output_priorities[4];
    uint8_t prev_output_present_bits[4];
    uint8_t prev_sense_pin_nids[4];
    uint8_t prev_sense_pin_output_bits[4];
    uint8_t prev_sense_pin_count;
    uint8_t prev_spkr_muter_mask;
    uint8_t prev_output_jack_count;
    uint8_t prev_analog_dac_count;
    uint32_t prev_output_config_defaults[4];
    uint32_t prev_output_sort_keys[4];
    uint32_t prev_output_mask;
    uint32_t prev_input_mask;

    had_widgets = g_audio_state.azalia_widget_probed;
    prev_output_dac_nid = g_audio_state.azalia_output_dac_nid;
    prev_spkr_dac_nid = g_audio_state.azalia_spkr_dac_nid;
    prev_output_pin_nid = g_audio_state.azalia_output_pin_nid;
    prev_input_dac_nid = g_audio_state.azalia_input_dac_nid;
    prev_input_pin_nid = g_audio_state.azalia_input_pin_nid;
    prev_output_mask = g_audio_state.azalia_output_mask;
    prev_input_mask = g_audio_state.azalia_input_mask;
    memcpy(prev_output_pin_nids, g_audio_state.azalia_output_pin_nids, sizeof(prev_output_pin_nids));
    memcpy(prev_output_dac_nids, g_audio_state.azalia_output_dac_nids, sizeof(prev_output_dac_nids));
    memcpy(prev_input_pin_nids, g_audio_state.azalia_input_pin_nids, sizeof(prev_input_pin_nids));
    prev_speaker2_pin_nid = g_audio_state.azalia_speaker2_pin_nid;
    prev_speaker2_dac_nid = g_audio_state.azalia_speaker2_dac_nid;
    prev_fhp_pin_nid = g_audio_state.azalia_fhp_pin_nid;
    prev_fhp_dac_nid = g_audio_state.azalia_fhp_dac_nid;
    prev_speaker2_priority = g_audio_state.azalia_speaker2_priority;
    prev_speaker2_config_default = g_audio_state.azalia_speaker2_config_default;
    memcpy(prev_widget_selected,
           g_audio_state.azalia_widget_selected,
           sizeof(prev_widget_selected));
    memcpy(prev_widget_selected_valid,
           g_audio_state.azalia_widget_selected_valid,
           sizeof(prev_widget_selected_valid));
    memcpy(prev_widget_disabled,
           g_audio_state.azalia_widget_disabled,
           sizeof(prev_widget_disabled));
    memcpy(prev_output_priorities, g_audio_state.azalia_output_priorities, sizeof(prev_output_priorities));
    memcpy(prev_output_present_bits, g_audio_state.azalia_output_present_bits, sizeof(prev_output_present_bits));
    memcpy(prev_sense_pin_nids, g_audio_state.azalia_sense_pin_nids, sizeof(prev_sense_pin_nids));
    memcpy(prev_sense_pin_output_bits,
           g_audio_state.azalia_sense_pin_output_bits,
           sizeof(prev_sense_pin_output_bits));
    prev_sense_pin_count = g_audio_state.azalia_sense_pin_count;
    prev_spkr_muter_mask = g_audio_state.azalia_spkr_muter_mask;
    prev_output_jack_count = g_audio_state.azalia_output_jack_count;
    prev_analog_dac_count = g_audio_state.azalia_analog_dac_count;
    memcpy(prev_output_config_defaults,
           g_audio_state.azalia_output_config_defaults,
           sizeof(prev_output_config_defaults));
    memcpy(prev_output_sort_keys,
           g_audio_state.azalia_output_sort_keys,
           sizeof(prev_output_sort_keys));
    g_audio_state.azalia_widget_probed = 0u;
    g_audio_state.azalia_output_dac_nid = 0u;
    g_audio_state.azalia_spkr_dac_nid = 0u;
    g_audio_state.azalia_output_pin_nid = 0u;
    g_audio_state.azalia_input_dac_nid = 0u;
    g_audio_state.azalia_input_pin_nid = 0u;
    g_audio_state.azalia_output_mask = 0u;
    g_audio_state.azalia_input_mask = 0u;
    memset(g_audio_state.azalia_output_pin_nids, 0, sizeof(g_audio_state.azalia_output_pin_nids));
    memset(g_audio_state.azalia_output_dac_nids, 0, sizeof(g_audio_state.azalia_output_dac_nids));
    memset(g_audio_state.azalia_input_pin_nids, 0, sizeof(g_audio_state.azalia_input_pin_nids));
    g_audio_state.azalia_speaker2_pin_nid = 0u;
    g_audio_state.azalia_speaker2_dac_nid = 0u;
    g_audio_state.azalia_fhp_pin_nid = 0u;
    g_audio_state.azalia_fhp_dac_nid = 0u;
    g_audio_state.azalia_speaker2_priority = -1;
    g_audio_state.azalia_speaker2_config_default = 0u;
    memset(g_audio_state.azalia_widget_selected, 0, sizeof(g_audio_state.azalia_widget_selected));
    memset(g_audio_state.azalia_widget_selected_valid, 0, sizeof(g_audio_state.azalia_widget_selected_valid));
    memset(g_audio_state.azalia_widget_disabled, 0, sizeof(g_audio_state.azalia_widget_disabled));
    memset(g_audio_state.azalia_widget_powered, 0, sizeof(g_audio_state.azalia_widget_powered));
    memset(g_audio_state.azalia_output_priorities, 0, sizeof(g_audio_state.azalia_output_priorities));
    memset(g_audio_state.azalia_output_present_bits, 0, sizeof(g_audio_state.azalia_output_present_bits));
    memset(g_audio_state.azalia_sense_pin_nids, 0, sizeof(g_audio_state.azalia_sense_pin_nids));
    memset(g_audio_state.azalia_sense_pin_output_bits, 0xff, sizeof(g_audio_state.azalia_sense_pin_output_bits));
    g_audio_state.azalia_sense_pin_count = 0u;
    g_audio_state.azalia_spkr_muter_mask = 0u;
    g_audio_state.azalia_output_jack_count = 0u;
    g_audio_state.azalia_analog_dac_count = 0u;
    memset(g_audio_state.azalia_output_config_defaults, 0, sizeof(g_audio_state.azalia_output_config_defaults));
    memset(g_audio_state.azalia_output_sort_keys, 0xff, sizeof(g_audio_state.azalia_output_sort_keys));
    g_audio_state.azalia_presence_refresh_tick = 0u;
    g_audio_state.azalia_unsol_output_mask = 0u;
    g_audio_state.azalia_unsol_rp = 0u;
    g_audio_state.azalia_unsol_wp = 0u;
    g_audio_state.azalia_unsol_kick = 0u;
    memset(g_audio_state.azalia_unsol_queue, 0, sizeof(g_audio_state.azalia_unsol_queue));
    g_audio_state.azalia_pin_policy_busy = 0u;
    if (!g_audio_state.azalia_codec_probed || g_audio_state.azalia_afg_nid == 0u) {
        return -1;
    }
    mk_audio_azalia_power_widget(g_audio_state.azalia_afg_nid);
    mk_audio_compat_delay();
    if (g_audio_state.azalia_fg_stream_formats == 0u &&
        g_audio_state.azalia_fg_pcm == 0u &&
        g_audio_state.azalia_fg_input_amp_cap == 0u &&
        g_audio_state.azalia_fg_output_amp_cap == 0u) {
        mk_audio_azalia_sync_fg_caps();
    }
    if (mk_audio_azalia_get_parameter_retry(g_audio_state.azalia_codec_address,
                                            g_audio_state.azalia_afg_nid,
                                            HDA_PARAM_SUB_NODE_COUNT,
                                            &subnodes,
                                            3u) != 0) {
        kernel_debug_puts("audio: hda widget probe falling back to legacy nid scan\n");
        first_nid = 2u;
        count = 0x3eu;
    } else {
        first_nid = (uint8_t)((subnodes >> 16) & 0xffu);
        count = (uint8_t)(subnodes & 0xffu);
        if (count == 0u || first_nid < 2u) {
            kernel_debug_printf("audio: hda widget probe using legacy nid scan afg=%u first=%u count=%u\n",
                                (unsigned int)g_audio_state.azalia_afg_nid,
                                (unsigned int)first_nid,
                                (unsigned int)count);
            first_nid = 2u;
            count = 0x3eu;
        } else {
            used_reported_range = 1u;
        }
    }
    mk_audio_azalia_probe_widget_range(first_nid, count, &best_output_priority);
    if (used_reported_range &&
        g_audio_state.azalia_output_mask == 0u &&
        g_audio_state.azalia_output_dac_nid == 0u &&
        g_audio_state.azalia_output_pin_nid == 0u) {
        kernel_debug_printf("audio: hda widget probe retrying legacy nid scan afg=%u first=%u count=%u\n",
                            (unsigned int)g_audio_state.azalia_afg_nid,
                            (unsigned int)first_nid,
                            (unsigned int)count);
        mk_audio_azalia_probe_widget_range(2u, 0x3eu, &best_output_priority);
    }

    if (g_audio_state.azalia_output_mask == 0u &&
        (g_audio_state.azalia_output_dac_nid != 0u || g_audio_state.azalia_output_pin_nid != 0u)) {
        g_audio_state.azalia_output_mask = 0x1u;
    }
    if (g_audio_state.azalia_output_dac_nid == 0u && g_audio_state.azalia_output_pin_nid == 0u) {
        mk_audio_azalia_apply_known_codec_topology();
    }
    if (g_audio_state.azalia_output_mask != 0u) {
        mk_audio_azalia_rebalance_output_dacs();
        mk_audio_azalia_commit_output_routes();
        if (g_audio_state.azalia_spkr_muter_mask != 0u) {
            mk_audio_azalia_sync_speaker_mute_policy();
        }
    }
    if (g_audio_state.azalia_output_dac_nid != 0u || g_audio_state.azalia_output_pin_nid != 0u) {
        g_audio_state.azalia_widget_probed = 1u;
        kernel_debug_puts("audio: hda widget probe ok\n");
        return 0;
    }
    if (had_widgets) {
        g_audio_state.azalia_widget_probed = 1u;
        g_audio_state.azalia_output_dac_nid = prev_output_dac_nid;
        g_audio_state.azalia_spkr_dac_nid = prev_spkr_dac_nid;
        g_audio_state.azalia_output_pin_nid = prev_output_pin_nid;
        g_audio_state.azalia_input_dac_nid = prev_input_dac_nid;
        g_audio_state.azalia_input_pin_nid = prev_input_pin_nid;
        g_audio_state.azalia_output_mask = prev_output_mask;
        g_audio_state.azalia_input_mask = prev_input_mask;
        memcpy(g_audio_state.azalia_output_pin_nids, prev_output_pin_nids, sizeof(prev_output_pin_nids));
        memcpy(g_audio_state.azalia_output_dac_nids, prev_output_dac_nids, sizeof(prev_output_dac_nids));
        memcpy(g_audio_state.azalia_input_pin_nids, prev_input_pin_nids, sizeof(prev_input_pin_nids));
        g_audio_state.azalia_speaker2_pin_nid = prev_speaker2_pin_nid;
        g_audio_state.azalia_speaker2_dac_nid = prev_speaker2_dac_nid;
        g_audio_state.azalia_fhp_pin_nid = prev_fhp_pin_nid;
        g_audio_state.azalia_fhp_dac_nid = prev_fhp_dac_nid;
        g_audio_state.azalia_speaker2_priority = prev_speaker2_priority;
        g_audio_state.azalia_speaker2_config_default = prev_speaker2_config_default;
        memcpy(g_audio_state.azalia_widget_selected,
               prev_widget_selected,
               sizeof(prev_widget_selected));
        memcpy(g_audio_state.azalia_widget_selected_valid,
               prev_widget_selected_valid,
               sizeof(prev_widget_selected_valid));
        memcpy(g_audio_state.azalia_widget_disabled,
               prev_widget_disabled,
               sizeof(prev_widget_disabled));
        memcpy(g_audio_state.azalia_output_priorities, prev_output_priorities, sizeof(prev_output_priorities));
        memcpy(g_audio_state.azalia_output_present_bits, prev_output_present_bits, sizeof(prev_output_present_bits));
        memcpy(g_audio_state.azalia_sense_pin_nids, prev_sense_pin_nids, sizeof(prev_sense_pin_nids));
        memcpy(g_audio_state.azalia_sense_pin_output_bits,
               prev_sense_pin_output_bits,
               sizeof(prev_sense_pin_output_bits));
        g_audio_state.azalia_sense_pin_count = prev_sense_pin_count;
        g_audio_state.azalia_spkr_muter_mask = prev_spkr_muter_mask;
        g_audio_state.azalia_output_jack_count = prev_output_jack_count;
        g_audio_state.azalia_analog_dac_count = prev_analog_dac_count;
        memcpy(g_audio_state.azalia_output_config_defaults,
               prev_output_config_defaults,
               sizeof(prev_output_config_defaults));
        memcpy(g_audio_state.azalia_output_sort_keys,
               prev_output_sort_keys,
               sizeof(prev_output_sort_keys));
    }
    kernel_debug_puts("audio: hda widget probe found no usable output\n");
    return -1;
}

static int mk_audio_azalia_reprobe_output_topology(void) {
    mk_audio_azalia_refresh_codec_mask();
    if (mk_audio_azalia_probe_codec() != 0) {
        return -1;
    }
    if (mk_audio_azalia_probe_widgets() != 0) {
        return -1;
    }
    return 0;
}

static int mk_audio_azalia_format_from_params(const struct audio_swpar *params, uint16_t *fmt_out) {
    uint16_t fmt = 0u;
    unsigned channels;
    unsigned bits;
    unsigned rate;

    if (params == 0 || fmt_out == 0) {
        return -1;
    }

    channels = params->pchan;
    bits = params->bits;
    rate = params->rate;
    if (channels == 0u || channels > 16u) {
        return -1;
    }
    fmt |= (uint16_t)(channels - 1u);

    switch (bits) {
    case 8u: fmt |= HDA_SD_FMT_BITS_8_16; break;
    case 16u: fmt |= HDA_SD_FMT_BITS_16_16; break;
    case 20u: fmt |= HDA_SD_FMT_BITS_20_32; break;
    case 24u: fmt |= HDA_SD_FMT_BITS_24_32; break;
    case 32u: fmt |= HDA_SD_FMT_BITS_32_32; break;
    default: return -1;
    }

    switch (rate) {
    case 192000u: fmt |= HDA_SD_FMT_BASE_48 | HDA_SD_FMT_MULT_X4 | HDA_SD_FMT_DIV_BY1; break;
    case 176400u: fmt |= HDA_SD_FMT_BASE_44 | HDA_SD_FMT_MULT_X4 | HDA_SD_FMT_DIV_BY1; break;
    case 96000u: fmt |= HDA_SD_FMT_BASE_48 | HDA_SD_FMT_MULT_X2 | HDA_SD_FMT_DIV_BY1; break;
    case 88200u: fmt |= HDA_SD_FMT_BASE_44 | HDA_SD_FMT_MULT_X2 | HDA_SD_FMT_DIV_BY1; break;
    case 48000u: fmt |= HDA_SD_FMT_BASE_48 | HDA_SD_FMT_MULT_X1 | HDA_SD_FMT_DIV_BY1; break;
    case 44100u: fmt |= HDA_SD_FMT_BASE_44 | HDA_SD_FMT_MULT_X1 | HDA_SD_FMT_DIV_BY1; break;
    case 32000u: fmt |= HDA_SD_FMT_BASE_48 | HDA_SD_FMT_MULT_X2 | HDA_SD_FMT_DIV_BY3; break;
    case 22050u: fmt |= HDA_SD_FMT_BASE_44 | HDA_SD_FMT_MULT_X1 | HDA_SD_FMT_DIV_BY2; break;
    case 16000u: fmt |= HDA_SD_FMT_BASE_48 | HDA_SD_FMT_MULT_X1 | HDA_SD_FMT_DIV_BY3; break;
    case 11025u: fmt |= HDA_SD_FMT_BASE_44 | HDA_SD_FMT_MULT_X1 | HDA_SD_FMT_DIV_BY4; break;
    case 8000u: fmt |= HDA_SD_FMT_BASE_48 | HDA_SD_FMT_MULT_X1 | HDA_SD_FMT_DIV_BY6; break;
    default: return -1;
    }

    *fmt_out = fmt;
    return 0;
}

static int mk_audio_azalia_try_program_output_candidate(uint8_t pin_nid,
                                                        uint8_t dac_nid,
                                                        int selected_bit,
                                                        uint32_t output_mask) {
    uint8_t path[8];
    uint8_t path_indices[8];
    uint32_t path_len = 0u;
    int route_selected = 0;

    (void)output_mask;

    if (dac_nid == 0u) {
        return -1;
    }
    g_audio_state.azalia_output_pin_nid = pin_nid;
    g_audio_state.azalia_output_dac_nid = dac_nid;
    mk_audio_debug_azalia_route(pin_nid, dac_nid, selected_bit);
    mk_audio_azalia_power_widget(g_audio_state.azalia_afg_nid);
    mk_audio_azalia_apply_gpio_quirks();
    mk_audio_azalia_apply_processing_quirks();
    if (pin_nid != 0u) {
        if (mk_audio_azalia_resolve_output_path(pin_nid,
                                                dac_nid,
                                                0u,
                                                path,
                                                path_indices,
                                                8u,
                                                &path_len) == 0 &&
            mk_audio_azalia_apply_output_path(path, path_indices, path_len, 1u, 1u, 1u) == 0) {
            route_selected = 1;
        } else if (mk_audio_azalia_find_output_dac(pin_nid, 0u) == (int)dac_nid) {
            route_selected = 1;
            kernel_debug_printf("audio: hda route already-selected pin=%u dac=%u pci=%x:%x\n",
                                (unsigned int)pin_nid,
                                (unsigned int)dac_nid,
                                (unsigned int)g_audio_state.pci.vendor_id,
                                (unsigned int)g_audio_state.pci.device_id);
        } else {
            kernel_debug_printf("audio: hda route fallback pin=%u dac=%u pci=%x:%x\n",
                                (unsigned int)pin_nid,
                                (unsigned int)dac_nid,
                                (unsigned int)g_audio_state.pci.vendor_id,
                                (unsigned int)g_audio_state.pci.device_id);
        }
    }
    if (pin_nid == 0u) {
        mk_audio_azalia_power_widget(dac_nid);
        mk_audio_azalia_program_widget_amp(dac_nid, 0u, 0u);
    }
    mk_audio_azalia_apply_secondary_speaker_path();
    mk_audio_azalia_apply_output_pin_policy(selected_bit);
    g_audio_state.azalia_path_programmed = (uint8_t)((dac_nid != 0u &&
                                                      (pin_nid == 0u || route_selected)) ? 1u : 0u);
    if (!g_audio_state.azalia_path_programmed) {
        kernel_debug_printf("audio: hda candidate rejected pin=%u dac=%u pci=%x:%x\n",
                            (unsigned int)pin_nid,
                            (unsigned int)dac_nid,
                            (unsigned int)g_audio_state.pci.vendor_id,
                            (unsigned int)g_audio_state.pci.device_id);
        return -1;
    }
    return 0;
}

static int mk_audio_azalia_rebind_output_stream(void) {
    uint8_t path[8];
    uint8_t path_indices[8];
    uint32_t path_len = 0u;
    uint8_t pin_nid = g_audio_state.azalia_output_pin_nid;
    uint8_t dac_nid = g_audio_state.azalia_output_dac_nid;
    int selected_bit = -1;

    if (!g_audio_state.azalia_path_programmed || dac_nid == 0u) {
        return -1;
    }

    for (int bit = 0; bit < 4; ++bit) {
        if (g_audio_state.azalia_output_dac_nids[bit] != dac_nid) {
            continue;
        }
        if (pin_nid == 0u || g_audio_state.azalia_output_pin_nids[bit] == pin_nid) {
            selected_bit = bit;
            break;
        }
    }

    mk_audio_azalia_power_widget(g_audio_state.azalia_afg_nid);
    if (pin_nid != 0u) {
        if (mk_audio_azalia_resolve_output_path(pin_nid,
                                                dac_nid,
                                                0u,
                                                path,
                                                path_indices,
                                                8u,
                                                &path_len) != 0 ||
            mk_audio_azalia_apply_output_path(path, path_indices, path_len, 1u, 1u, 1u) != 0) {
            return -1;
        }
    } else {
        mk_audio_azalia_power_widget(dac_nid);
        mk_audio_azalia_program_widget_amp(dac_nid, 0u, 0u);
    }
    if (mk_audio_azalia_bind_dac_stream(dac_nid, 0u) != 0) {
        return -1;
    }
    mk_audio_azalia_apply_secondary_speaker_path();
    mk_audio_azalia_apply_output_pin_policy(selected_bit);
    return 0;
}

static int mk_audio_azalia_program_output_path(void) {
    uint32_t output_mask;
    uint32_t refresh_interval;
    uint8_t had_path_programmed;
    int selected_bit;
    int tried_bit = -1;
    uint8_t pin_nid;
    uint8_t dac_nid;
    uint8_t tried_pin = 0u;
    uint8_t tried_dac = 0u;

    had_path_programmed = g_audio_state.azalia_path_programmed;
    g_audio_state.azalia_path_programmed = 0u;
    if (!g_audio_state.azalia_codec_probed || g_audio_state.azalia_output_dac_nid == 0u) {
        return -1;
    }
    if (had_path_programmed &&
        g_audio_state.azalia_presence_refresh_tick != 0u &&
        mk_audio_azalia_current_output_path_valid() &&
        (uint32_t)(kernel_timer_get_ticks() - g_audio_state.azalia_presence_refresh_tick) <
            mk_audio_azalia_presence_refresh_interval() &&
        mk_audio_azalia_rebind_output_stream() == 0) {
        return 0;
    }
    refresh_interval = mk_audio_azalia_presence_refresh_interval();
    if (!had_path_programmed ||
        g_audio_state.azalia_presence_refresh_tick == 0u ||
        (uint32_t)(kernel_timer_get_ticks() - g_audio_state.azalia_presence_refresh_tick) >=
            refresh_interval) {
        mk_audio_azalia_refresh_output_presence();
    }
    output_mask = mk_audio_output_presence_mask();
    if (mk_audio_azalia_choose_output_path(&pin_nid, &dac_nid, &selected_bit) == 0 &&
        dac_nid != 0u) {
        tried_bit = selected_bit;
        tried_pin = pin_nid;
        tried_dac = dac_nid;
        if (had_path_programmed &&
            pin_nid == g_audio_state.azalia_output_pin_nid &&
            dac_nid == g_audio_state.azalia_output_dac_nid &&
            mk_audio_azalia_rebind_output_stream() == 0) {
            return 0;
        }
        if (mk_audio_azalia_try_program_output_candidate(pin_nid, dac_nid, selected_bit, output_mask) == 0) {
            return 0;
        }
    }

    for (int bit = 0; bit < 4; ++bit) {
        pin_nid = g_audio_state.azalia_output_pin_nids[bit];
        dac_nid = g_audio_state.azalia_output_dac_nids[bit];
        if (!mk_audio_azalia_output_candidate_available(bit)) {
            continue;
        }
        if (pin_nid == tried_pin && dac_nid == tried_dac) {
            continue;
        }
        if (had_path_programmed &&
            pin_nid == g_audio_state.azalia_output_pin_nid &&
            dac_nid == g_audio_state.azalia_output_dac_nid &&
            mk_audio_azalia_rebind_output_stream() == 0) {
            return 0;
        }
        if (mk_audio_azalia_try_program_output_candidate(pin_nid, dac_nid, bit, output_mask) == 0) {
            return 0;
        }
    }

    for (int bit = 0; bit < 4; ++bit) {
        dac_nid = g_audio_state.azalia_output_dac_nids[bit];
        if (!mk_audio_azalia_output_candidate_available(bit) || dac_nid == 0u) {
            continue;
        }
        if (bit == tried_bit && dac_nid == tried_dac) {
            continue;
        }
        if (had_path_programmed &&
            g_audio_state.azalia_output_pin_nid == 0u &&
            dac_nid == g_audio_state.azalia_output_dac_nid &&
            mk_audio_azalia_rebind_output_stream() == 0) {
            return 0;
        }
        if (mk_audio_azalia_try_program_output_candidate(0u, dac_nid, bit, output_mask) == 0) {
            return 0;
        }
    }

    if (g_audio_state.azalia_output_dac_nid != 0u &&
        mk_audio_azalia_try_program_output_candidate(0u,
                                                     g_audio_state.azalia_output_dac_nid,
                                                     selected_bit,
                                                     output_mask) == 0) {
        return 0;
    }
    return -1;
}

static void mk_audio_azalia_program_widget_amp_state(uint8_t nid,
                                                     uint8_t input_amp,
                                                     uint8_t index,
                                                     uint8_t muted) {
    uint32_t caps = 0u;
    uint32_t amp_caps = 0u;
    uint32_t gain = 0u;
    uint32_t payload;
    uint16_t verb = input_amp ? HDA_PARAM_INPUT_AMP_CAP : HDA_PARAM_OUTPUT_AMP_CAP;
    uint32_t response = 0u;

    if (nid == 0u) {
        return;
    }
    if (mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                nid,
                                HDA_VERB_GET_PARAMETER,
                                HDA_PARAM_AUDIO_WIDGET_CAP,
                                &caps) != 0) {
        return;
    }
    if (input_amp) {
        if ((caps & HDA_WCAP_INAMP) == 0u) {
            return;
        }
    } else if (!mk_audio_azalia_widget_has_effective_outamp(nid, caps)) {
        return;
    }
    if ((caps & HDA_WCAP_AMPOV) != 0u) {
        if (mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                    nid,
                                    HDA_VERB_GET_PARAMETER,
                                    verb,
                                    &amp_caps) != 0) {
            return;
        }
    } else {
        amp_caps = input_amp ? g_audio_state.azalia_fg_input_amp_cap
                             : g_audio_state.azalia_fg_output_amp_cap;
        if (amp_caps == 0u &&
            g_audio_state.azalia_afg_nid != 0u &&
            nid != g_audio_state.azalia_afg_nid) {
            mk_audio_azalia_sync_fg_caps();
            amp_caps = input_amp ? g_audio_state.azalia_fg_input_amp_cap
                                 : g_audio_state.azalia_fg_output_amp_cap;
        }
    }
    if (!input_amp && mk_audio_azalia_is_ad1981_oamp_widget(nid)) {
        amp_caps = 0x9e06211fu;
    }
    if (amp_caps == 0u || amp_caps == 0xffffffffu) {
        return;
    }

    gain = (amp_caps >> 8) & HDA_AMP_GAIN_MASK;
    payload = gain & HDA_AMP_GAIN_MASK;
    payload |= ((uint32_t)index << HDA_AMP_GAIN_INDEX_SHIFT);
    payload |= HDA_AMP_GAIN_LEFT | HDA_AMP_GAIN_RIGHT;
    payload |= input_amp ? HDA_AMP_GAIN_INPUT : HDA_AMP_GAIN_OUTPUT;
    if (muted != 0u) {
        payload |= HDA_AMP_GAIN_MUTE;
    } else {
        payload &= ~HDA_AMP_GAIN_MUTE;
    }
    (void)mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                  nid,
                                  HDA_VERB_SET_AMP_GAIN_MUTE,
                                  (uint8_t)payload,
                                  &response);
    (void)mk_audio_azalia_command_raw(g_audio_state.azalia_codec_address,
                                      nid,
                                      ((uint32_t)HDA_VERB_SET_AMP_GAIN_MUTE << 16) | payload,
                                      &response);
}

static void mk_audio_azalia_program_widget_amp(uint8_t nid, uint8_t input_amp, uint8_t index) {
    mk_audio_azalia_program_widget_amp_state(nid, input_amp, index, 0u);
}

static void mk_audio_azalia_program_widget_mixer_defaults(uint8_t nid,
                                                          uint32_t connection_count,
                                                          uint32_t selected_index) {
    if (nid == 0u || connection_count <= 1u) {
        return;
    }

    for (uint32_t i = 0u; i < connection_count; ++i) {
        mk_audio_azalia_program_widget_amp_state(nid,
                                                 1u,
                                                 (uint8_t)i,
                                                 (uint8_t)(i != selected_index));
    }
}

static void mk_audio_azalia_program_selector_defaults(uint8_t nid,
                                                      uint32_t connection_count,
                                                      uint32_t selected_index) {
    uint32_t caps = 0u;
    uint8_t connections[32];
    uint32_t child_caps = 0u;

    if (nid == 0u || connection_count == 0u) {
        return;
    }
    if (connection_count > 1u) {
        mk_audio_azalia_program_widget_amp(nid, 1u, (uint8_t)selected_index);
        return;
    }
    if (mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                nid,
                                HDA_VERB_GET_PARAMETER,
                                HDA_PARAM_AUDIO_WIDGET_CAP,
                                &caps) != 0) {
        mk_audio_azalia_program_widget_amp(nid, 1u, 0u);
        return;
    }
    if (!mk_audio_azalia_widget_has_effective_outamp(nid, caps)) {
        mk_audio_azalia_program_widget_amp(nid, 1u, 0u);
        return;
    }
    if (mk_audio_azalia_get_connections(nid, connections, 32u, &connection_count) != 0 ||
        connection_count != 1u ||
        !mk_audio_azalia_widget_enabled(connections[0])) {
        mk_audio_azalia_program_widget_amp(nid, 1u, 0u);
        return;
    }
    if (mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                connections[0],
                                HDA_VERB_GET_PARAMETER,
                                HDA_PARAM_AUDIO_WIDGET_CAP,
                                &child_caps) != 0) {
        mk_audio_azalia_program_widget_amp(nid, 1u, 0u);
        return;
    }

    if ((child_caps & HDA_WCAP_INAMP) == 0u ||
        !mk_audio_azalia_widget_has_effective_outamp(connections[0], child_caps)) {
        mk_audio_azalia_program_widget_amp(nid, 0u, 0u);
        return;
    }
    mk_audio_azalia_program_widget_amp(nid, 1u, 0u);
}

static void mk_audio_azalia_power_widget(uint8_t nid) {
    uint32_t caps = 0u;
    uint32_t response = 0u;

    if (nid == 0u) {
        return;
    }
    if (g_audio_state.azalia_widget_powered[nid] != 0u) {
        return;
    }
    if (mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                nid,
                                HDA_VERB_GET_PARAMETER,
                                HDA_PARAM_AUDIO_WIDGET_CAP,
                                &caps) != 0) {
        caps = 0u;
    }
    if (caps != 0u &&
        caps != 0xffffffffu &&
        (caps & HDA_WCAP_POWER) == 0u &&
        nid != g_audio_state.azalia_afg_nid) {
        return;
    }
    (void)mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                  nid,
                                  HDA_VERB_SET_POWER_STATE,
                                  HDA_POWER_STATE_D0,
                                  &response);
    g_audio_state.azalia_widget_powered[nid] = 1u;
    for (uint32_t settle = 0u; settle < 8u; ++settle) {
        mk_audio_compat_delay();
    }
}

static void mk_audio_azalia_enable_eapd(uint8_t nid, uint32_t pin_caps) {
    uint32_t response = 0u;

    if (nid == 0u) {
        return;
    }
    if (pin_caps == 0u || pin_caps == 0xffffffffu) {
        if (mk_audio_azalia_get_parameter_retry(g_audio_state.azalia_codec_address,
                                                nid,
                                                HDA_PARAM_PIN_CAP,
                                                &pin_caps,
                                                3u) != 0) {
            return;
        }
    }
    if ((pin_caps & HDA_PINCAP_EAPD) == 0u) {
        return;
    }
    if (mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                nid,
                                HDA_VERB_GET_EAPD_BTLENABLE,
                                0u,
                                &response) != 0) {
        response = 0u;
    }
    response = (response & 0xffu) | HDA_EAPD_ENABLE;
    (void)mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                  nid,
                                  HDA_VERB_SET_EAPD_BTLENABLE,
                                  (uint8_t)(response & 0xffu),
                                  &response);
}

static uint8_t mk_audio_azalia_output_pin_ctl(int output_bit) {
    uint32_t config_default = 0u;
    uint8_t pin_ctl = HDA_PINCTL_OUT_EN;

    if (output_bit >= 0 && output_bit < 4) {
        config_default = g_audio_state.azalia_output_config_defaults[output_bit];
    }
    if (mk_audio_hda_config_device(config_default) == HDA_CONFIG_DEVICE_HEADPHONE ||
        output_bit == 1) {
        pin_ctl |= HDA_PINCTL_HP_EN;
    }
    return pin_ctl;
}

static void mk_audio_azalia_set_output_pin_enabled(uint8_t pin_nid,
                                                   int output_bit,
                                                   uint8_t enabled) {
    uint32_t response = 0u;
    uint8_t pin_ctl = 0u;
    uint8_t is_sense_pin = 0u;

    if (pin_nid == 0u) {
        return;
    }
    for (uint32_t i = 0u; i < g_audio_state.azalia_sense_pin_count; ++i) {
        if (g_audio_state.azalia_sense_pin_nids[i] == pin_nid) {
            is_sense_pin = 1u;
            break;
        }
    }
    if (enabled != 0u) {
        pin_ctl = mk_audio_azalia_output_pin_ctl(output_bit);
    }
    mk_audio_azalia_power_widget(pin_nid);
    mk_audio_azalia_program_widget_amp(pin_nid, 0u, 0u);
    (void)mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                  pin_nid,
                                  HDA_VERB_SET_PIN_WIDGET_CONTROL,
                                  pin_ctl,
                                  &response);
    if (enabled != 0u) {
        mk_audio_azalia_enable_eapd(pin_nid, 0u);
    }
    if (is_sense_pin) {
        g_audio_state.azalia_presence_refresh_tick = 0u;
        if (g_audio_state.azalia_path_programmed != 0u &&
            g_audio_state.azalia_pin_policy_busy == 0u) {
            mk_audio_azalia_sync_speaker_mute_policy();
        }
    }
}

static void mk_audio_azalia_apply_output_pin_policy(int selected_bit) {
    int mute_speakers = mk_audio_azalia_should_mute_speakers(selected_bit);

    g_audio_state.azalia_pin_policy_busy = 1u;
    for (int bit = 0; bit < 4; ++bit) {
        uint8_t pin_nid = g_audio_state.azalia_output_pin_nids[bit];

        if (pin_nid == 0u) {
            continue;
        }
        if (bit == selected_bit) {
            mk_audio_azalia_set_output_pin_enabled(pin_nid, bit, 1u);
            continue;
        }
        if (!mk_audio_azalia_output_candidate_available(bit)) {
            mk_audio_azalia_set_output_pin_enabled(pin_nid, bit, 0u);
            continue;
        }
        if (mute_speakers && mk_audio_azalia_output_is_speaker(bit) &&
            mk_audio_azalia_speaker_mute_method() == MK_AUDIO_HDA_SPKR_MUTE_PIN_CTL) {
            mk_audio_azalia_set_output_pin_enabled(pin_nid, bit, 0u);
            continue;
        }
        mk_audio_azalia_set_output_pin_enabled(pin_nid, bit, 1u);
    }
    if (g_audio_state.azalia_speaker2_pin_nid != 0u) {
        if (mute_speakers && mk_audio_azalia_speaker_mute_method() == MK_AUDIO_HDA_SPKR_MUTE_PIN_CTL) {
            mk_audio_azalia_set_output_pin_enabled(g_audio_state.azalia_speaker2_pin_nid, 0, 0u);
        } else {
            mk_audio_azalia_set_output_pin_enabled(g_audio_state.azalia_speaker2_pin_nid, 0, 1u);
        }
    }
    g_audio_state.azalia_pin_policy_busy = 0u;
    mk_audio_azalia_apply_speaker_mute((uint8_t)(mute_speakers ? 1u : 0u));
}

static int mk_audio_azalia_query_widget_audio_caps(uint8_t nid,
                                                   uint32_t widget_caps,
                                                   uint32_t *encodings_out,
                                                   uint32_t *pcm_out) {
    uint32_t encodings = 0u;
    uint32_t pcm = 0u;

    if (encodings_out == 0 || pcm_out == 0 || nid == 0u) {
        return -1;
    }

    if ((widget_caps & HDA_WCAP_FORMATOV) != 0u) {
        if (mk_audio_azalia_get_parameter_retry(g_audio_state.azalia_codec_address,
                                                nid,
                                                0x0bu,
                                                &encodings,
                                                3u) != 0) {
            return -1;
        }
        if (encodings == 0u) {
            encodings = g_audio_state.azalia_fg_stream_formats;
            pcm = g_audio_state.azalia_fg_pcm;
        } else {
            if ((encodings & HDA_STREAM_FORMAT_PCM) == 0u) {
                return -1;
            }
            if (mk_audio_azalia_get_parameter_retry(g_audio_state.azalia_codec_address,
                                                    nid,
                                                    0x0au,
                                                    &pcm,
                                                    3u) != 0) {
                return -1;
            }
        }
    } else {
        encodings = g_audio_state.azalia_fg_stream_formats;
        pcm = g_audio_state.azalia_fg_pcm;
    }

    if (encodings == 0u || (encodings & HDA_STREAM_FORMAT_PCM) == 0u) {
        return -1;
    }
    if (pcm == 0u || pcm == 0xffffffffu) {
        return -1;
    }

    *encodings_out = encodings;
    *pcm_out = pcm;
    return 0;
}

static int mk_audio_azalia_get_selected_connection(uint8_t nid,
                                                   uint32_t connection_count,
                                                   uint32_t *selected_out) {
    uint32_t response = 0u;
    uint32_t selected;

    if (selected_out == 0) {
        return -1;
    }
    *selected_out = 0u;
    if (nid == 0u || connection_count == 0u) {
        return -1;
    }
    if (g_audio_state.azalia_widget_selected_valid[nid] != 0u) {
        selected = g_audio_state.azalia_widget_selected[nid];
        if (selected < connection_count) {
            *selected_out = selected;
            return 0;
        }
        g_audio_state.azalia_widget_selected_valid[nid] = 0u;
    }
    if (mk_audio_azalia_command_retry(g_audio_state.azalia_codec_address,
                                      nid,
                                      HDA_VERB_GET_CONNECTION_SELECT,
                                      0u,
                                      &response,
                                      3u) != 0) {
        return -1;
    }
    selected = response & 0xffu;
    if (selected >= connection_count) {
        return -1;
    }
    g_audio_state.azalia_widget_selected[nid] = (uint8_t)selected;
    g_audio_state.azalia_widget_selected_valid[nid] = 1u;
    *selected_out = selected;
    return 0;
}

static void mk_audio_azalia_cache_selected_connection(uint8_t nid,
                                                      uint32_t connection_count) {
    uint32_t selected = 0u;

    if (nid == 0u || connection_count == 0u) {
        return;
    }
    if (mk_audio_azalia_get_selected_connection(nid, connection_count, &selected) != 0) {
        return;
    }
    g_audio_state.azalia_widget_selected[nid] = (uint8_t)selected;
    g_audio_state.azalia_widget_selected_valid[nid] = 1u;
}

static int mk_audio_azalia_get_connections(uint8_t nid,
                                           uint8_t *connections,
                                           uint32_t max_connections,
                                           uint32_t *count_out) {
    uint32_t widget_caps;
    uint32_t list_len_info;
    uint32_t count = 0u;
    uint32_t last = 0u;
    uint32_t bits;
    uint32_t length;

    if (count_out == 0) {
        return -1;
    }
    *count_out = 0u;
    if (connections == 0 || max_connections == 0u) {
        return -1;
    }
    if (mk_audio_azalia_get_parameter_retry(g_audio_state.azalia_codec_address,
                                            nid,
                                            HDA_PARAM_AUDIO_WIDGET_CAP,
                                            &widget_caps,
                                            3u) != 0) {
        return -1;
    }
    if ((widget_caps & HDA_WCAP_CONNLIST) == 0u) {
        return 0;
    }
    if (mk_audio_azalia_get_parameter_retry(g_audio_state.azalia_codec_address,
                                            nid,
                                            HDA_PARAM_CONN_LIST_LEN,
                                            &list_len_info,
                                            3u) != 0) {
        return -1;
    }

    bits = (list_len_info & HDA_CONNLIST_LONG) != 0u ? 16u : 8u;
    length = list_len_info & HDA_CONNLIST_LEN_MASK;
    for (uint32_t entry_index = 0u; entry_index < length;) {
        uint32_t response = 0u;
        uint32_t per_word = 32u / bits;

        if (mk_audio_azalia_command_retry(g_audio_state.azalia_codec_address,
                                          nid,
                                          HDA_VERB_GET_CONNECTION_LIST_ENTRY,
                                          (uint8_t)entry_index,
                                          &response,
                                          3u) != 0) {
            return -1;
        }
        for (uint32_t k = 0u; k < per_word && entry_index < length; ++k, ++entry_index) {
            uint32_t conn = (response >> (k * bits)) & ((1u << bits) - 1u);
            uint32_t range_flag = 1u << (bits - 1u);

            if (count > 0u && (conn & range_flag) != 0u) {
                uint32_t end = conn & ~range_flag;

                while (last < end && count < max_connections) {
                    last += 1u;
                    connections[count++] = (uint8_t)last;
                }
            } else if (count < max_connections) {
                connections[count++] = (uint8_t)conn;
                last = conn;
            } else {
                last = conn;
            }
        }
    }
    *count_out = count;
    if (count > 1u) {
        mk_audio_azalia_cache_selected_connection(nid, count);
    }
    return 0;
}

static void mk_audio_azalia_repair_connection_select(uint8_t nid, uint32_t type) {
    uint8_t connections[32];
    uint8_t path[8];
    uint8_t path_indices[8];
    uint32_t connection_count = 0u;
    uint32_t candidate_dac = 0u;
    uint32_t path_len = 0u;
    uint32_t selected_index = 0u;
    uint32_t candidate_index = 0u;
    int have_selected = 0;

    if (nid == 0u || type == HDA_WID_AUD_MIXER) {
        return;
    }
    if (mk_audio_azalia_get_connections(nid, connections, 32u, &connection_count) != 0 ||
        connection_count <= 1u) {
        return;
    }

    have_selected = mk_audio_azalia_get_selected_connection(nid, connection_count, &selected_index) == 0;
    if (have_selected) {
        candidate_dac = (uint32_t)mk_audio_azalia_find_output_dac(connections[selected_index], 1u);
    }
    if (have_selected &&
        candidate_dac <= 0xffu &&
        mk_audio_azalia_resolve_output_path(connections[selected_index],
                                            (uint8_t)candidate_dac,
                                            1u,
                                            path,
                                            path_indices,
                                            8u,
                                            &path_len) == 0) {
        return;
    }

    for (uint32_t i = 0u; i < connection_count; ++i) {
        candidate_dac = (uint32_t)mk_audio_azalia_find_output_dac(connections[i], 1u);
        if (candidate_dac > 0xffu ||
            mk_audio_azalia_resolve_output_path(connections[i],
                                                (uint8_t)candidate_dac,
                                                1u,
                                                path,
                                                path_indices,
                                                8u,
                                                &path_len) != 0) {
            continue;
        }
        candidate_index = i;
        if (have_selected && i == selected_index) {
            return;
        }
        if (mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                    nid,
                                    HDA_VERB_SET_CONNECTION_SELECT,
                                    (uint8_t)i,
                                    0) == 0) {
            g_audio_state.azalia_widget_selected[nid] = (uint8_t)i;
            g_audio_state.azalia_widget_selected_valid[nid] = 1u;
        }
        return;
    }

    if (!have_selected && candidate_index == 0u) {
        g_audio_state.azalia_widget_selected_valid[nid] = 0u;
    }
}

static int mk_audio_azalia_find_connection_index_for_dac(uint8_t nid,
                                                         uint32_t type,
                                                         const uint8_t *connections,
                                                         uint32_t connection_count,
                                                         uint8_t target_dac,
                                                         uint32_t depth,
                                                         uint32_t *index_out) {
    uint32_t selected_index = 0u;
    int have_selected = 0;

    if (connections == 0 || index_out == 0 || connection_count == 0u) {
        return -1;
    }
    *index_out = 0u;

    if (type != HDA_WID_AUD_MIXER) {
        have_selected = mk_audio_azalia_get_selected_connection(nid, connection_count, &selected_index) == 0;
    }
    for (uint32_t pass = 0u; pass < 2u; ++pass) {
        for (uint32_t i = 0u; i < connection_count; ++i) {
            int found;

            if (have_selected) {
                if (pass == 0u && i != selected_index) {
                    continue;
                }
                if (pass != 0u && i == selected_index) {
                    continue;
                }
            } else if (pass != 0u) {
                continue;
            }

            found = mk_audio_azalia_find_output_dac(connections[i], depth + 1u);
            if (found != (int)target_dac) {
                continue;
            }
            *index_out = i;
            return 0;
        }
    }
    return -1;
}

static int mk_audio_azalia_resolve_output_path(uint8_t nid,
                                               uint8_t target_dac,
                                               uint32_t depth,
                                               uint8_t *path,
                                               uint8_t *path_indices,
                                               uint32_t max_path,
                                               uint32_t *path_len_out) {
    uint32_t caps = 0u;
    uint32_t type;
    uint8_t connections[32];
    uint32_t connection_count = 0u;
    uint32_t selected_index = 0u;

    if (path_len_out == 0 || path == 0 || path_indices == 0 ||
        !mk_audio_azalia_widget_enabled(nid) || target_dac == 0u || max_path == 0u ||
        depth >= 8u || depth >= max_path) {
        return -1;
    }

    path[depth] = nid;
    path_indices[depth] = 0xffu;
    if (nid == target_dac) {
        *path_len_out = depth + 1u;
        return 0;
    }

    if (mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                nid,
                                HDA_VERB_GET_PARAMETER,
                                HDA_PARAM_AUDIO_WIDGET_CAP,
                                &caps) != 0) {
        return -1;
    }
    type = (caps >> HDA_WCAP_TYPE_SHIFT) & HDA_WCAP_TYPE_MASK;
    if (mk_audio_azalia_get_connections(nid, connections, 32u, &connection_count) != 0 ||
        connection_count == 0u) {
        return -1;
    }
    if (mk_audio_azalia_find_connection_index_for_dac(nid,
                                                      type,
                                                      connections,
                                                      connection_count,
                                                      target_dac,
                                                      depth,
                                                      &selected_index) != 0) {
        return -1;
    }

    path_indices[depth] = (uint8_t)selected_index;
    return mk_audio_azalia_resolve_output_path(connections[selected_index],
                                               target_dac,
                                               depth + 1u,
                                               path,
                                               path_indices,
                                               max_path,
                                               path_len_out);
}

static int mk_audio_azalia_apply_output_path(const uint8_t *path,
                                             const uint8_t *path_indices,
                                             uint32_t path_len,
                                             uint8_t program_route,
                                             uint8_t power_widgets,
                                             uint8_t program_amps) {
    uint32_t response = 0u;

    if (path == 0 || path_indices == 0 || path_len == 0u) {
        return -1;
    }

    for (uint32_t i = 0u; i < path_len; ++i) {
        uint8_t nid = path[i];
        uint8_t selected_index = path_indices[i];
        uint32_t caps = 0u;
        uint32_t type;
        uint8_t connections[32];
        uint32_t connection_count = 0u;

        if (!mk_audio_azalia_widget_enabled(nid)) {
            return -1;
        }
        if (power_widgets != 0u) {
            mk_audio_azalia_power_widget(nid);
        }
        if (program_amps != 0u) {
            mk_audio_azalia_program_widget_amp(nid, 0u, 0u);
        }
        if (i + 1u >= path_len) {
            continue;
        }
        if (mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                    nid,
                                    HDA_VERB_GET_PARAMETER,
                                    HDA_PARAM_AUDIO_WIDGET_CAP,
                                    &caps) != 0) {
            return -1;
        }
        type = (caps >> HDA_WCAP_TYPE_SHIFT) & HDA_WCAP_TYPE_MASK;
        if (mk_audio_azalia_get_connections(nid, connections, 32u, &connection_count) != 0 ||
            connection_count == 0u ||
            selected_index >= connection_count ||
            !mk_audio_azalia_widget_enabled(connections[selected_index]) ||
            connections[selected_index] != path[i + 1u]) {
            return -1;
        }
        if (program_route != 0u &&
            type != HDA_WID_AUD_MIXER &&
            connection_count > 1u) {
            uint32_t current_selected = 0u;
            int have_selected = mk_audio_azalia_get_selected_connection(nid,
                                                                        connection_count,
                                                                        &current_selected) == 0;

            if (!have_selected || current_selected != selected_index) {
                if (mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                            nid,
                                            HDA_VERB_SET_CONNECTION_SELECT,
                                            selected_index,
                                            &response) != 0) {
                    return -1;
                }
            }
            g_audio_state.azalia_widget_selected[nid] = selected_index;
            g_audio_state.azalia_widget_selected_valid[nid] = 1u;
        }
        if (program_amps == 0u) {
            continue;
        }
        if (type == HDA_WID_AUD_MIXER) {
            mk_audio_azalia_program_widget_mixer_defaults(nid, connection_count, selected_index);
        } else if (type == HDA_WID_AUD_SELECTOR) {
            mk_audio_azalia_program_selector_defaults(nid, connection_count, selected_index);
        } else {
            mk_audio_azalia_program_widget_amp(nid, 1u, 0u);
        }
    }
    return 0;
}

static int mk_audio_azalia_widget_enabled(uint8_t nid) {
    if (nid == 0u) {
        return 0;
    }
    return g_audio_state.azalia_widget_disabled[nid] == 0u;
}

static int mk_audio_azalia_widget_check_connection(uint8_t nid, uint32_t depth) {
    uint32_t caps = 0u;
    uint32_t type;
    uint8_t connections[32];
    uint32_t connection_count = 0u;

    if (!mk_audio_azalia_widget_enabled(nid)) {
        return 0;
    }
    if (mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                nid,
                                HDA_VERB_GET_PARAMETER,
                                HDA_PARAM_AUDIO_WIDGET_CAP,
                                &caps) != 0 ||
        caps == 0u ||
        caps == 0xffffffffu) {
        return 0;
    }
    type = (caps >> HDA_WCAP_TYPE_SHIFT) & HDA_WCAP_TYPE_MASK;
    if (depth > 0u &&
        (type == HDA_WID_PIN || type == HDA_WID_AUD_OUT || type == HDA_WID_AUD_IN)) {
        return 1;
    }
    if (type == HDA_WID_BEEP_GENERATOR) {
        return 0;
    }
    if (++depth >= 10u) {
        return 0;
    }
    if (mk_audio_azalia_get_connections(nid, connections, 32u, &connection_count) != 0) {
        return 0;
    }
    for (uint32_t i = 0u; i < connection_count; ++i) {
        if (!mk_audio_azalia_widget_enabled(connections[i])) {
            continue;
        }
        if (mk_audio_azalia_widget_check_connection(connections[i], depth)) {
            return 1;
        }
    }
    return 0;
}

static int mk_audio_azalia_widget_has_output_path(uint8_t nid, uint32_t depth) {
    uint32_t caps = 0u;
    uint32_t type;
    uint8_t connections[32];
    uint32_t connection_count = 0u;

    if (!mk_audio_azalia_widget_enabled(nid) || depth >= 10u) {
        return 0;
    }
    if (mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                nid,
                                HDA_VERB_GET_PARAMETER,
                                HDA_PARAM_AUDIO_WIDGET_CAP,
                                &caps) != 0 ||
        caps == 0u ||
        caps == 0xffffffffu) {
        return 0;
    }

    type = (caps >> HDA_WCAP_TYPE_SHIFT) & HDA_WCAP_TYPE_MASK;
    if (type == HDA_WID_BEEP_GENERATOR) {
        return -1;
    }
    if (type == HDA_WID_AUD_OUT) {
        uint32_t encodings = 0u;
        uint32_t pcm = 0u;

        return mk_audio_azalia_query_widget_audio_caps(nid, caps, &encodings, &pcm) == 0;
    }
    if (depth > 0u && (type == HDA_WID_PIN || type == HDA_WID_AUD_IN)) {
        return 0;
    }
    if (mk_audio_azalia_get_connections(nid, connections, 32u, &connection_count) != 0) {
        return 0;
    }
    for (uint32_t i = 0u; i < connection_count; ++i) {
        if (!mk_audio_azalia_widget_enabled(connections[i])) {
            continue;
        }
        if (mk_audio_azalia_widget_has_output_path(connections[i], depth + 1u)) {
            return 1;
        }
    }
    return 0;
}

static int mk_audio_azalia_find_output_dac(uint8_t nid, uint32_t depth) {
    uint32_t caps = 0u;
    uint32_t type;
    uint8_t connections[32];
    uint32_t connection_count = 0u;
    uint32_t selected_index = 0u;
    int have_selected = 0;

    if (!mk_audio_azalia_widget_enabled(nid) || depth >= 8u) {
        return -1;
    }
    if (mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                nid,
                                HDA_VERB_GET_PARAMETER,
                                HDA_PARAM_AUDIO_WIDGET_CAP,
                                &caps) != 0) {
        return -1;
    }
    type = (caps >> HDA_WCAP_TYPE_SHIFT) & HDA_WCAP_TYPE_MASK;
    if (type == HDA_WID_AUD_OUT) {
        uint32_t encodings = 0u;
        uint32_t pcm = 0u;

        if (mk_audio_azalia_query_widget_audio_caps(nid, caps, &encodings, &pcm) != 0) {
            return -1;
        }
        return (int)nid;
    }
    if (depth > 0u && (type == HDA_WID_PIN || type == HDA_WID_AUD_IN)) {
        return -1;
    }
    if (mk_audio_azalia_get_connections(nid, connections, 32u, &connection_count) != 0) {
        return -1;
    }
    if ((type == HDA_WID_AUD_MIXER || type == HDA_WID_AUD_SELECTOR) &&
        !mk_audio_azalia_widget_has_output_path(nid, depth)) {
        return -1;
    }
    if (type == HDA_WID_AUD_MIXER) {
        for (uint32_t i = 0u; i < connection_count; ++i) {
            if (!mk_audio_azalia_widget_enabled(connections[i])) {
                continue;
            }
            int found = mk_audio_azalia_find_output_dac(connections[i], depth + 1u);

            if (found >= 0) {
                return found;
            }
        }
        return -1;
    }
    have_selected = mk_audio_azalia_get_selected_connection(nid, connection_count, &selected_index) == 0;
    if (have_selected && type != HDA_WID_AUD_MIXER) {
        return mk_audio_azalia_find_output_dac(connections[selected_index], depth + 1u);
    }
    for (uint32_t i = 0u; i < connection_count; ++i) {
        int found = mk_audio_azalia_find_output_dac(connections[i], depth + 1u);

        if (found >= 0) {
            return found;
        }
    }
    return -1;
}

static int mk_audio_azalia_find_alternate_output_dac(uint8_t pin_nid,
                                                     uint8_t current_dac,
                                                     const uint8_t *avoid_dacs,
                                                     uint32_t avoid_count,
                                                     uint8_t *dac_out) {
    uint8_t connections[32];
    uint32_t connection_count = 0u;
    uint8_t fallback_dac = 0u;

    if (dac_out == 0 || pin_nid == 0u || current_dac == 0u) {
        return -1;
    }
    *dac_out = 0u;
    if (mk_audio_azalia_get_connections(pin_nid, connections, 32u, &connection_count) != 0) {
        return -1;
    }
    for (uint32_t i = 0u; i < connection_count; ++i) {
        int found = mk_audio_azalia_find_output_dac(connections[i], 1u);
        uint8_t found_dac;
        uint8_t blocked = 0u;

        if (found < 0 || found > 0xff || (uint8_t)found == current_dac) {
            continue;
        }
        found_dac = (uint8_t)found;
        for (uint32_t j = 0u; j < avoid_count; ++j) {
            if (avoid_dacs != 0 && avoid_dacs[j] == found_dac) {
                blocked = 1u;
                break;
            }
        }
        if (!blocked) {
            *dac_out = found_dac;
            return 0;
        }
        if (fallback_dac == 0u) {
            fallback_dac = found_dac;
        }
    }
    if (fallback_dac != 0u) {
        *dac_out = fallback_dac;
        return 0;
    }
    return -1;
}

static int mk_audio_azalia_retarget_pin_to_dac(uint8_t pin_nid, uint8_t target_dac) {
    uint32_t caps = 0u;
    uint32_t type;
    uint8_t connections[32];
    uint32_t connection_count = 0u;
    uint32_t selected_index = 0u;
    uint32_t response = 0u;

    if (pin_nid == 0u || target_dac == 0u) {
        return -1;
    }
    if (mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                pin_nid,
                                HDA_VERB_GET_PARAMETER,
                                HDA_PARAM_AUDIO_WIDGET_CAP,
                                &caps) != 0) {
        return -1;
    }
    type = (caps >> HDA_WCAP_TYPE_SHIFT) & HDA_WCAP_TYPE_MASK;
    if (type != HDA_WID_PIN) {
        return -1;
    }
    if (mk_audio_azalia_get_connections(pin_nid, connections, 32u, &connection_count) != 0 ||
        connection_count == 0u) {
        return -1;
    }
    if (mk_audio_azalia_find_connection_index_for_dac(pin_nid,
                                                      type,
                                                      connections,
                                                      connection_count,
                                                      target_dac,
                                                      0u,
                                                      &selected_index) != 0) {
        return -1;
    }
    if (connection_count > 1u) {
        uint32_t current_selected = 0u;
        int have_selected = mk_audio_azalia_get_selected_connection(pin_nid,
                                                                    connection_count,
                                                                    &current_selected) == 0;

        if (!have_selected || current_selected != selected_index) {
            if (mk_audio_azalia_command(g_audio_state.azalia_codec_address,
                                        pin_nid,
                                        HDA_VERB_SET_CONNECTION_SELECT,
                                        (uint8_t)selected_index,
                                        &response) != 0) {
                return -1;
            }
        }
    }
    g_audio_state.azalia_widget_selected[pin_nid] = (uint8_t)selected_index;
    g_audio_state.azalia_widget_selected_valid[pin_nid] = 1u;
    return 0;
}

static int mk_audio_azalia_stream_reset(void) {
    uint32_t base;
    uint16_t ctl;
    uint8_t sts;

    base = g_audio_state.azalia_output_regbase;
    if (g_audio_state.azalia_base == 0u || base == 0u) {
        return -1;
    }

    ctl = mk_audio_azalia_read16(g_audio_state.azalia_base, (uint16_t)(base + HDA_SD_CTL));
    ctl &= (uint16_t)~HDA_SD_CTL_RUN;
    mk_audio_azalia_write16(g_audio_state.azalia_base, (uint16_t)(base + HDA_SD_CTL), ctl);
    for (uint32_t i = 0u; i < HDA_RESET_TIMEOUT; ++i) {
        ctl = mk_audio_azalia_read16(g_audio_state.azalia_base, (uint16_t)(base + HDA_SD_CTL));
        if ((ctl & HDA_SD_CTL_RUN) == 0u) {
            break;
        }
        mk_audio_cooperative_delay(i);
    }
    if ((ctl & HDA_SD_CTL_RUN) != 0u) {
        kernel_debug_puts("audio: hda stream reset run-clear timeout\n");
        return -1;
    }

    ctl = mk_audio_azalia_read16(g_audio_state.azalia_base, (uint16_t)(base + HDA_SD_CTL));
    mk_audio_azalia_write16(g_audio_state.azalia_base, (uint16_t)(base + HDA_SD_CTL), (uint16_t)(ctl | HDA_SD_CTL_SRST));
    for (uint32_t i = 0u; i < HDA_RESET_TIMEOUT; ++i) {
        ctl = mk_audio_azalia_read16(g_audio_state.azalia_base, (uint16_t)(base + HDA_SD_CTL));
        if ((ctl & HDA_SD_CTL_SRST) != 0u) {
            break;
        }
        mk_audio_cooperative_delay(i);
    }
    if ((ctl & HDA_SD_CTL_SRST) == 0u) {
        kernel_debug_puts("audio: hda stream reset set timeout\n");
        return -1;
    }

    mk_audio_azalia_write16(g_audio_state.azalia_base, (uint16_t)(base + HDA_SD_CTL), (uint16_t)(ctl & ~HDA_SD_CTL_SRST));
    for (uint32_t i = 0u; i < HDA_RESET_TIMEOUT; ++i) {
        ctl = mk_audio_azalia_read16(g_audio_state.azalia_base, (uint16_t)(base + HDA_SD_CTL));
        if ((ctl & HDA_SD_CTL_SRST) == 0u) {
            break;
        }
        mk_audio_cooperative_delay(i);
    }
    if ((ctl & HDA_SD_CTL_SRST) != 0u) {
        kernel_debug_puts("audio: hda stream reset clear timeout\n");
        return -1;
    }
    for (uint32_t settle = 0u; settle < 64u; ++settle) {
        mk_audio_compat_delay();
    }

    sts = mk_audio_azalia_read8(g_audio_state.azalia_base, (uint16_t)(base + HDA_SD_STS));
    sts |= (uint8_t)(HDA_SD_STS_DESE | HDA_SD_STS_FIFOE | HDA_SD_STS_BCIS);
    mk_audio_azalia_write8(g_audio_state.azalia_base, (uint16_t)(base + HDA_SD_STS), sts);
    g_audio_state.azalia_output_running = 0u;
    g_audio_state.azalia_output_pos = 0u;
    g_audio_state.azalia_output_start_tick = 0u;
    g_audio_state.azalia_output_deadline_tick = 0u;
    return 0;
}

static int mk_audio_azalia_stream_prepare_fast(void) {
    uint32_t base;
    uint16_t ctl;
    uint8_t sts;

    base = g_audio_state.azalia_output_regbase;
    if (g_audio_state.azalia_base == 0u || base == 0u) {
        return -1;
    }

    ctl = mk_audio_azalia_read16(g_audio_state.azalia_base, (uint16_t)(base + HDA_SD_CTL));
    ctl &= (uint16_t)~HDA_SD_CTL_RUN;
    mk_audio_azalia_write16(g_audio_state.azalia_base, (uint16_t)(base + HDA_SD_CTL), ctl);
    for (uint32_t i = 0u; i < HDA_RESET_TIMEOUT; ++i) {
        ctl = mk_audio_azalia_read16(g_audio_state.azalia_base, (uint16_t)(base + HDA_SD_CTL));
        if ((ctl & HDA_SD_CTL_RUN) == 0u) {
            break;
        }
        mk_audio_azalia_stream_delay(i);
    }
    if ((ctl & HDA_SD_CTL_RUN) != 0u) {
        kernel_debug_puts("audio: hda fast prepare run-clear timeout\n");
        return -1;
    }

    sts = mk_audio_azalia_read8(g_audio_state.azalia_base, (uint16_t)(base + HDA_SD_STS));
    sts |= (uint8_t)(HDA_SD_STS_DESE | HDA_SD_STS_FIFOE | HDA_SD_STS_BCIS);
    mk_audio_azalia_write8(g_audio_state.azalia_base, (uint16_t)(base + HDA_SD_STS), sts);
    g_audio_state.azalia_output_running = 0u;
    g_audio_state.azalia_output_pos = 0u;
    g_audio_state.azalia_output_start_tick = 0u;
    g_audio_state.azalia_output_deadline_tick = 0u;
    return 0;
}

static void mk_audio_azalia_finish_output_soft(void) {
    if (!g_audio_state.azalia_output_running) {
        return;
    }
    g_audio_state.azalia_output_pos = g_audio_state.azalia_output_bytes;
    if (g_audio_state.playback_bytes_consumed < g_audio_state.playback_bytes_written) {
        g_audio_state.playback_bytes_consumed = g_audio_state.playback_bytes_written;
    }
    g_audio_state.azalia_output_running = 0u;
    g_audio_state.azalia_output_blk = 0u;
    g_audio_state.azalia_output_swpos = 0u;
    g_audio_state.azalia_output_start_tick = 0u;
    g_audio_state.azalia_output_deadline_tick = 0u;
}

static void mk_audio_azalia_stream_halt(void) {
    uint32_t base;
    uint16_t ctl;
    uint32_t intctl;

    base = g_audio_state.azalia_output_regbase;
    if (g_audio_state.azalia_base == 0u || base == 0u) {
        return;
    }

    ctl = mk_audio_azalia_read16(g_audio_state.azalia_base, (uint16_t)(base + HDA_SD_CTL));
    ctl &= (uint16_t)~(HDA_SD_CTL_DEIE | HDA_SD_CTL_FEIE | HDA_SD_CTL_IOCE | HDA_SD_CTL_RUN);
    mk_audio_azalia_write16(g_audio_state.azalia_base, (uint16_t)(base + HDA_SD_CTL), ctl);
    intctl = mk_audio_azalia_read32(g_audio_state.azalia_base, HDA_INTCTL);
    intctl &= ~(1u << g_audio_state.azalia_output_stream_index);
    mk_audio_azalia_write32(g_audio_state.azalia_base, HDA_INTCTL, intctl);
    mk_audio_azalia_set_irq_enabled(0u);
    mk_audio_azalia_disconnect_output_stream();
    g_audio_state.azalia_output_running = 0u;
    g_audio_state.azalia_output_blk = 0u;
    g_audio_state.azalia_output_swpos = 0u;
    g_audio_state.azalia_output_start_tick = 0u;
    g_audio_state.azalia_output_deadline_tick = 0u;
}

static int mk_audio_azalia_stream_start_buffer(uint32_t bytes) {
    static int trace_budget = 0;
    uint32_t base;
    uint32_t bdl_addr;
    uint32_t intctl;
    uint32_t offset = 0u;
    uint32_t blk;
    char location[MAX_AUDIO_DEV_LEN];
    uint8_t ctl2;
    uint8_t initial_stream_index;
    uint8_t next_stream_index;
    uint16_t lvi = 0u;
    uint16_t ctl = 0u;
    uint8_t fast_prepared = 0u;
    uint8_t force_reprogram = 0u;
    uint8_t path_valid = 0u;
    uint8_t stream_bound = 0u;
    uint8_t reprobed = 0u;
    uint8_t rotated = 0u;
    int trace = 0;

    if (trace_budget > 0) {
        trace = 1;
        trace_budget--;
        kernel_debug_puts("audio: hda stream start enter\n");
    }

    if (g_audio_state.azalia_base == 0u || g_audio_state.azalia_output_regbase == 0u || bytes == 0u) {
        return -1;
    }
    if (mk_audio_azalia_has_fatal_probe_failure()) {
        if (!g_audio_state.azalia_codec_probed || g_audio_state.azalia_afg_nid == 0u) {
            mk_audio_copy_limited(g_audio_state.info.device.config, "hda-no-audio-fg", MAX_AUDIO_DEV_LEN);
        } else {
            mk_audio_copy_limited(g_audio_state.info.device.config, "hda-no-usable-output", MAX_AUDIO_DEV_LEN);
        }
        return -1;
    }
    initial_stream_index = g_audio_state.azalia_output_stream_index;
    next_stream_index = (uint8_t)(initial_stream_index + 1u);
retry_stream:
    fast_prepared = 0u;
    path_valid = 0u;
    stream_bound = 0u;
    if (!force_reprogram) {
        path_valid = (uint8_t)mk_audio_azalia_current_output_path_valid();
    }
    if (path_valid) {
        if (mk_audio_azalia_stream_prepare_fast() == 0) {
            fast_prepared = 1u;
        } else {
            force_reprogram = 1u;
            path_valid = 0u;
        }
    }
    if (!fast_prepared && mk_audio_azalia_stream_reset() != 0) {
        kernel_debug_puts("audio: hda stream reset failed, rotating stream\n");
        mk_audio_copy_limited(g_audio_state.info.device.config, "hda-stream-reset-failed", MAX_AUDIO_DEV_LEN);
        if (mk_audio_azalia_select_output_stream_from(next_stream_index) != 0) {
            kernel_debug_puts("audio: hda stream reset failed\n");
            mk_audio_copy_limited(g_audio_state.info.device.config, "hda-stream-reset-failed", MAX_AUDIO_DEV_LEN);
            return -1;
        }
        next_stream_index = (uint8_t)(g_audio_state.azalia_output_stream_index + 1u);
        if ((path_valid && mk_audio_azalia_rebind_output_stream() != 0) ||
            (!path_valid && mk_audio_azalia_program_output_path() != 0) ||
            mk_audio_azalia_stream_reset() != 0) {
            kernel_debug_puts("audio: hda stream reset failed\n");
            mk_audio_copy_limited(g_audio_state.info.device.config, "hda-stream-reset-failed", MAX_AUDIO_DEV_LEN);
            return -1;
        }
        force_reprogram = 1u;
        path_valid = (uint8_t)mk_audio_azalia_current_output_path_valid();
    }

    base = g_audio_state.azalia_output_regbase;
    if (force_reprogram && !path_valid) {
        path_valid = (uint8_t)mk_audio_azalia_current_output_path_valid();
    }
    if (force_reprogram && path_valid) {
        if (mk_audio_azalia_rebind_output_stream() == 0) {
            force_reprogram = 0u;
            stream_bound = 1u;
        } else {
            path_valid = 0u;
        }
    }
    if (!(fast_prepared && !force_reprogram && path_valid) &&
        mk_audio_azalia_program_output_path() != 0) {
        kernel_debug_puts("audio: hda stream codec-connect failed\n");
        mk_audio_copy_limited(g_audio_state.info.device.config, "hda-codec-connect-failed", MAX_AUDIO_DEV_LEN);
        if (!rotated &&
            path_valid &&
            mk_audio_azalia_select_output_stream_from(next_stream_index) == 0) {
            rotated = 1u;
            next_stream_index = (uint8_t)(g_audio_state.azalia_output_stream_index + 1u);
            force_reprogram = 1u;
            goto retry_stream;
        }
        if (!reprobed &&
            !path_valid &&
            !mk_audio_azalia_has_fatal_probe_failure()) {
            reprobed = 1u;
            if (mk_audio_azalia_reprobe_output_topology() == 0) {
                force_reprogram = 0u;
                goto retry_stream;
            }
        }
        if (!rotated &&
            mk_audio_azalia_select_output_stream_from(next_stream_index) == 0) {
            rotated = 1u;
            next_stream_index = (uint8_t)(g_audio_state.azalia_output_stream_index + 1u);
            force_reprogram = 1u;
            goto retry_stream;
        }
        return -1;
    }
    if (!stream_bound && mk_audio_azalia_rebind_output_stream() != 0) {
        kernel_debug_puts("audio: hda stream bind failed\n");
        mk_audio_copy_limited(g_audio_state.info.device.config, "hda-codec-connect-failed", MAX_AUDIO_DEV_LEN);
        if (!rotated &&
            mk_audio_azalia_select_output_stream_from(next_stream_index) == 0) {
            rotated = 1u;
            next_stream_index = (uint8_t)(g_audio_state.azalia_output_stream_index + 1u);
            force_reprogram = 1u;
            goto retry_stream;
        }
        if (!reprobed &&
            !path_valid &&
            !mk_audio_azalia_has_fatal_probe_failure()) {
            reprobed = 1u;
            if (mk_audio_azalia_reprobe_output_topology() == 0) {
                force_reprogram = 0u;
                goto retry_stream;
            }
        }
        return -1;
    }
    blk = g_audio_state.info.parameters.round;
    if (blk == 0u || blk > AUICH_DMA_SLOT_SIZE) {
        blk = AUICH_DMA_SLOT_SIZE;
    }
    if ((blk & 1u) != 0u) {
        blk -= 1u;
    }
    if (blk == 0u) {
        blk = AUICH_DMA_SLOT_SIZE;
    }

    if (mk_audio_azalia_alloc_dma_buffers() != 0) {
        return -1;
    }

    memset(g_audio_state.azalia_bdl,
           0,
           sizeof(struct mk_audio_hda_bdl_entry) * HDA_BDL_MAX);
    for (lvi = 0u; lvi < HDA_BDL_MAX && offset < bytes; ++lvi) {
        uint32_t chunk = bytes - offset;
        uint32_t page_offset = offset % PHYSMEM_PAGE_SIZE;
        uint8_t *page_ptr;

        if (chunk > blk) {
            chunk = blk;
        }
        if (chunk > (PHYSMEM_PAGE_SIZE - page_offset)) {
            chunk = PHYSMEM_PAGE_SIZE - page_offset;
        }
        page_ptr = mk_audio_azalia_output_ptr(offset);
        if (page_ptr == 0) {
            return -1;
        }
        g_audio_state.azalia_bdl[lvi].low =
            (uint32_t)(uintptr_t)page_ptr;
        g_audio_state.azalia_bdl[lvi].high = 0u;
        g_audio_state.azalia_bdl[lvi].length = chunk;
        g_audio_state.azalia_bdl[lvi].flags = 0x00000001u;
        offset += chunk;
    }
    if (lvi == 0u) {
        return -1;
    }
    lvi = (uint16_t)(lvi - 1u);
    if (trace) {
        kernel_debug_puts("audio: hda stream start bdl-ready\n");
    }

    mk_audio_azalia_write32(g_audio_state.azalia_base, (uint16_t)(base + HDA_SD_BDPL), 0u);
    mk_audio_azalia_write32(g_audio_state.azalia_base, (uint16_t)(base + HDA_SD_BDPU), 0u);
    bdl_addr = (uint32_t)(uintptr_t)&g_audio_state.azalia_bdl[0];
    mk_audio_azalia_write32(g_audio_state.azalia_base, (uint16_t)(base + HDA_SD_BDPL), bdl_addr);
    mk_audio_azalia_write32(g_audio_state.azalia_base, (uint16_t)(base + HDA_SD_BDPU), 0u);
    mk_audio_azalia_write16(g_audio_state.azalia_base, (uint16_t)(base + HDA_SD_LVI), (uint16_t)(lvi & HDA_SD_LVI_MASK));
    ctl2 = mk_audio_azalia_read8(g_audio_state.azalia_base, (uint16_t)(base + HDA_SD_CTL2));
    ctl2 = (uint8_t)((ctl2 & ~HDA_SD_CTL2_STRM) |
                     ((g_audio_state.azalia_output_stream_number << HDA_SD_CTL2_STRM_SHIFT) &
                      HDA_SD_CTL2_STRM));
    mk_audio_azalia_write8(g_audio_state.azalia_base, (uint16_t)(base + HDA_SD_CTL2), ctl2);
    mk_audio_azalia_write32(g_audio_state.azalia_base, (uint16_t)(base + HDA_SD_CBL), bytes);
    mk_audio_azalia_write16(g_audio_state.azalia_base, (uint16_t)(base + HDA_SD_FMT), g_audio_state.azalia_output_fmt);
    intctl = mk_audio_azalia_read32(g_audio_state.azalia_base, HDA_INTCTL);
    if (g_audio_state.azalia_irq_registered) {
        intctl |= HDA_INTCTL_GIE | HDA_INTCTL_CIE | (1u << g_audio_state.azalia_output_stream_index);
        mk_audio_azalia_write32(g_audio_state.azalia_base, HDA_INTCTL, intctl);
        mk_audio_azalia_set_irq_enabled(1u);
    } else {
        intctl &= ~(HDA_INTCTL_GIE | HDA_INTCTL_CIE | (1u << g_audio_state.azalia_output_stream_index));
        mk_audio_azalia_write32(g_audio_state.azalia_base, HDA_INTCTL, intctl);
    }
    ctl = mk_audio_azalia_read16(g_audio_state.azalia_base, (uint16_t)(base + HDA_SD_CTL));
    ctl |= (uint16_t)(HDA_SD_CTL_DEIE | HDA_SD_CTL_FEIE | HDA_SD_CTL_IOCE | HDA_SD_CTL_RUN);
    mk_audio_azalia_write16(g_audio_state.azalia_base, (uint16_t)(base + HDA_SD_CTL), ctl);
    for (uint32_t i = 0u; i < HDA_RESET_TIMEOUT; ++i) {
        ctl = mk_audio_azalia_read16(g_audio_state.azalia_base, (uint16_t)(base + HDA_SD_CTL));
        if ((ctl & HDA_SD_CTL_RUN) != 0u) {
            break;
        }
        mk_audio_compat_delay();
    }
    if ((ctl & HDA_SD_CTL_RUN) == 0u) {
        kernel_debug_puts("audio: hda run bit did not latch\n");
        mk_audio_copy_limited(g_audio_state.info.device.config, "hda-run-not-latched", MAX_AUDIO_DEV_LEN);
        mk_audio_azalia_stream_halt();
        if (!rotated &&
            mk_audio_azalia_select_output_stream_from(next_stream_index) == 0) {
            rotated = 1u;
            next_stream_index = (uint8_t)(g_audio_state.azalia_output_stream_index + 1u);
            force_reprogram = 1u;
            goto retry_stream;
        }
        if (!reprobed && !mk_audio_azalia_has_fatal_probe_failure()) {
            reprobed = 1u;
            if (!path_valid &&
                mk_audio_azalia_reprobe_output_topology() == 0) {
                force_reprogram = 0u;
                goto retry_stream;
            }
        }
        return -1;
    }
    if (trace) {
        kernel_debug_puts("audio: hda stream start run-latched\n");
    }
    g_audio_state.azalia_output_running = 1u;
    g_audio_state.azalia_output_bytes = bytes;
    g_audio_state.azalia_output_blk = blk;
    g_audio_state.azalia_output_swpos = 0u;
    g_audio_state.azalia_output_pos = 0u;
    g_audio_state.azalia_output_start_tick = kernel_timer_get_ticks();
    g_audio_state.azalia_output_deadline_tick =
        g_audio_state.azalia_output_start_tick +
        mk_audio_estimated_playback_ticks(&g_audio_state.info.parameters, bytes);
    memset(location, 0, sizeof(location));
    mk_audio_format_pci_location(location, sizeof(location), &g_audio_state.pci);
    if (location[0] != '\0') {
        mk_audio_copy_limited(g_audio_state.info.device.config, location, MAX_AUDIO_DEV_LEN);
    }
    return 0;
}

static void mk_audio_azalia_update_output_progress(void) {
    uint32_t base;
    uint32_t lpib;
    uint32_t fifos;
    uint32_t hwpos;
    uint32_t advance;
    uint32_t blk;
    uint32_t now_ticks;

    base = g_audio_state.azalia_output_regbase;
    if (!g_audio_state.azalia_output_running || g_audio_state.azalia_base == 0u || base == 0u) {
        return;
    }
    blk = g_audio_state.azalia_output_blk;
    if (blk == 0u) {
        blk = g_audio_state.info.parameters.round;
    }
    if (blk == 0u || blk > g_audio_state.azalia_output_bytes) {
        blk = g_audio_state.azalia_output_bytes;
    }

    lpib = mk_audio_azalia_read32(g_audio_state.azalia_base, (uint16_t)(base + HDA_SD_LPIB));
    if (lpib > g_audio_state.azalia_output_bytes) {
        lpib = g_audio_state.azalia_output_bytes;
    }
    fifos = mk_audio_azalia_read16(g_audio_state.azalia_base, (uint16_t)(base + HDA_SD_FIFOS));
    if ((fifos & 1u) != 0u) {
        fifos++;
    }
    hwpos = lpib + fifos + 1u;
    while (hwpos >= g_audio_state.azalia_output_bytes && g_audio_state.azalia_output_bytes != 0u) {
        hwpos -= g_audio_state.azalia_output_bytes;
    }
    if (hwpos >= g_audio_state.azalia_output_swpos) {
        advance = hwpos - g_audio_state.azalia_output_swpos;
    } else {
        advance = (g_audio_state.azalia_output_bytes - g_audio_state.azalia_output_swpos) + hwpos;
    }
    while (blk != 0u && advance >= blk) {
        if (g_audio_state.azalia_output_pos < g_audio_state.azalia_output_bytes) {
            uint32_t remaining = g_audio_state.azalia_output_bytes - g_audio_state.azalia_output_pos;
            uint32_t consumed = blk;

            if (consumed > remaining) {
                consumed = remaining;
            }
            g_audio_state.playback_bytes_consumed += consumed;
            g_audio_state.azalia_output_pos += consumed;
        }
        g_audio_state.azalia_output_swpos += blk;
        if (g_audio_state.azalia_output_swpos >= g_audio_state.azalia_output_bytes) {
            g_audio_state.azalia_output_swpos -= g_audio_state.azalia_output_bytes;
        }
        advance -= blk;
    }
    now_ticks = kernel_timer_get_ticks();
    if (g_audio_state.azalia_output_deadline_tick != 0u &&
        (int32_t)(now_ticks - g_audio_state.azalia_output_deadline_tick) >= 0) {
        g_audio_state.azalia_output_pos = g_audio_state.azalia_output_bytes;
    }
    if (g_audio_state.azalia_output_pos >= g_audio_state.azalia_output_bytes) {
        if (g_audio_state.playback_bytes_consumed < g_audio_state.playback_bytes_written) {
            g_audio_state.playback_bytes_consumed = g_audio_state.playback_bytes_written;
        }
        g_audio_state.azalia_output_running = 0u;
        g_audio_state.azalia_output_start_tick = 0u;
        g_audio_state.azalia_output_deadline_tick = 0u;
        kernel_debug_puts("audio: hda stream idle\n");
    }
}

static void mk_audio_select_azalia_backend(const struct kernel_pci_device_info *pci) {
    char location[MAX_AUDIO_DEV_LEN];

    if (pci == 0) {
        mk_audio_select_soft_backend();
        return;
    }

    g_audio_state.backend_kind = MK_AUDIO_BACKEND_COMPAT_AZALIA;
    g_audio_state.info.flags = MK_AUDIO_CAPS_MIXER | MK_AUDIO_CAPS_BSD_AUDIOIO_ABI;
    g_audio_state.info.status.mode = 0u;
    g_audio_state.pci = *pci;
    g_audio_state.compat_mix_base = 0u;
    g_audio_state.compat_aud_base = 0u;
    g_audio_state.azalia_base = 0u;
    g_audio_state.compat_caps = 0u;
    g_audio_state.compat_ext_audio_id = 0u;
    g_audio_state.azalia_gcap = 0u;
    g_audio_state.azalia_codec_mask = 0u;
    g_audio_state.azalia_corb_entries = 0u;
    g_audio_state.azalia_rirb_entries = 0u;
    g_audio_state.azalia_rirb_read_pos = 0u;
    g_audio_state.compat_ready = 0u;
    g_audio_state.compat_codec_ready = 0u;
    g_audio_state.compat_irq_registered = 0u;
    g_audio_state.compat_mix_is_mmio = 0u;
    g_audio_state.compat_aud_is_mmio = 0u;
    g_audio_state.compat_ignore_codecready = 0u;
    g_audio_state.azalia_ready = 0u;
    g_audio_state.azalia_irq_registered = 0u;
    g_audio_state.azalia_vmaj = 0u;
    g_audio_state.azalia_vmin = 0u;
    g_audio_state.azalia_codec_address = 0u;
    g_audio_state.azalia_afg_nid = 0u;
    g_audio_state.azalia_codec_probed = 0u;
    g_audio_state.azalia_corb_ready = 0u;
    g_audio_state.azalia_widget_probed = 0u;
    g_audio_state.azalia_path_programmed = 0u;
    g_audio_state.azalia_output_dac_nid = 0u;
    g_audio_state.azalia_spkr_dac_nid = 0u;
    g_audio_state.azalia_output_pin_nid = 0u;
    g_audio_state.azalia_input_dac_nid = 0u;
    g_audio_state.azalia_input_pin_nid = 0u;
    g_audio_state.azalia_output_stream_index = 0u;
    g_audio_state.azalia_output_stream_number = 0u;
    g_audio_state.azalia_output_running = 0u;
    g_audio_state.azalia_output_mask = 0u;
    g_audio_state.azalia_input_mask = 0u;
    g_audio_state.azalia_output_fmt = 0u;
    g_audio_state.azalia_output_regbase = 0u;
    g_audio_state.azalia_output_bytes = 0u;
    g_audio_state.azalia_output_blk = 0u;
    g_audio_state.azalia_output_swpos = 0u;
    g_audio_state.azalia_output_pos = 0u;
    g_audio_state.azalia_output_start_tick = 0u;
    g_audio_state.azalia_output_deadline_tick = 0u;
    memset(g_audio_state.azalia_output_pin_nids, 0, sizeof(g_audio_state.azalia_output_pin_nids));
    memset(g_audio_state.azalia_output_dac_nids, 0, sizeof(g_audio_state.azalia_output_dac_nids));
    memset(g_audio_state.azalia_input_pin_nids, 0, sizeof(g_audio_state.azalia_input_pin_nids));
    g_audio_state.azalia_speaker2_pin_nid = 0u;
    g_audio_state.azalia_speaker2_dac_nid = 0u;
    g_audio_state.azalia_fhp_pin_nid = 0u;
    g_audio_state.azalia_fhp_dac_nid = 0u;
    g_audio_state.azalia_speaker2_priority = -1;
    g_audio_state.azalia_speaker2_config_default = 0u;
    memset(g_audio_state.azalia_widget_selected, 0, sizeof(g_audio_state.azalia_widget_selected));
    memset(g_audio_state.azalia_widget_selected_valid, 0, sizeof(g_audio_state.azalia_widget_selected_valid));
    memset(g_audio_state.azalia_widget_disabled, 0, sizeof(g_audio_state.azalia_widget_disabled));
    memset(g_audio_state.azalia_widget_powered, 0, sizeof(g_audio_state.azalia_widget_powered));
    memset(g_audio_state.azalia_output_priorities, 0, sizeof(g_audio_state.azalia_output_priorities));
    memset(g_audio_state.azalia_output_present_bits, 0, sizeof(g_audio_state.azalia_output_present_bits));
    memset(g_audio_state.azalia_sense_pin_nids, 0, sizeof(g_audio_state.azalia_sense_pin_nids));
    memset(g_audio_state.azalia_sense_pin_output_bits, 0xff, sizeof(g_audio_state.azalia_sense_pin_output_bits));
    g_audio_state.azalia_sense_pin_count = 0u;
    g_audio_state.azalia_spkr_muter_mask = 0u;
    g_audio_state.azalia_output_jack_count = 0u;
    g_audio_state.azalia_analog_dac_count = 0u;
    memset(g_audio_state.azalia_output_config_defaults, 0, sizeof(g_audio_state.azalia_output_config_defaults));
    g_audio_state.azalia_presence_refresh_tick = 0u;
    g_audio_state.azalia_unsol_output_mask = 0u;
    g_audio_state.azalia_unsol_rp = 0u;
    g_audio_state.azalia_unsol_wp = 0u;
    g_audio_state.azalia_unsol_kick = 0u;
    memset(g_audio_state.azalia_unsol_queue, 0, sizeof(g_audio_state.azalia_unsol_queue));
    g_audio_state.azalia_pin_policy_busy = 0u;
    g_audio_backend = &g_audio_backend_azalia;

    mk_audio_enable_pci_device(pci);
    mk_audio_configure_azalia_pci(pci);
    g_audio_state.azalia_subsystem_id = kernel_pci_config_read_u32(pci->bus, pci->slot, pci->function, 0x2cu);
    if (kernel_pci_bar_is_mmio(pci->bars[HDA_BAR_INDEX])) {
        g_audio_state.azalia_base = kernel_pci_bar_base(pci->bars[HDA_BAR_INDEX]);
    }

    mk_audio_copy_limited(g_audio_state.info.device.name,
                          mk_audio_hda_vendor_name(pci->vendor_id),
                          MAX_AUDIO_DEV_LEN);
    mk_audio_copy_limited(g_audio_state.info.device.version, "compat-azalia", MAX_AUDIO_DEV_LEN);
    memset(location, 0, sizeof(location));
    mk_audio_format_pci_location(location, sizeof(location), pci);
    kernel_debug_printf("audio: azalia select begin pci=%x:%x irq=%u base=%x\n",
                        (unsigned int)pci->vendor_id,
                        (unsigned int)pci->device_id,
                        (unsigned int)pci->irq_line,
                        (unsigned int)g_audio_state.azalia_base);

    if (g_audio_state.azalia_base != 0u && mk_audio_azalia_reset_controller() == 0) {
        kernel_debug_puts("audio: azalia reset ok\n");
        g_audio_state.azalia_gcap = mk_audio_azalia_read16(g_audio_state.azalia_base, HDA_GCAP);
        g_audio_state.azalia_vmin = mk_audio_azalia_read8(g_audio_state.azalia_base, HDA_VMIN);
        g_audio_state.azalia_vmaj = mk_audio_azalia_read8(g_audio_state.azalia_base, HDA_VMAJ);
        g_audio_state.azalia_codec_mask = mk_audio_azalia_refresh_codec_mask();
        mk_audio_azalia_write16(g_audio_state.azalia_base, HDA_GSTS, 0xffffu);
        mk_audio_azalia_write32(g_audio_state.azalia_base, HDA_INTCTL, 0u);
        mk_audio_azalia_write32(g_audio_state.azalia_base, HDA_DPLBASE, 0u);
        mk_audio_azalia_write32(g_audio_state.azalia_base, HDA_DPUBASE, 0u);
        if (mk_audio_azalia_init_command_rings() != 0) {
            mk_audio_copy_limited(g_audio_state.info.device.config, "hda-ring-init-failed", MAX_AUDIO_DEV_LEN);
            kernel_debug_puts("audio: azalia ring init failed\n");
            mk_audio_refresh_topology_snapshot();
            return;
        }
        kernel_debug_puts("audio: azalia ring init ok\n");
        if (mk_audio_azalia_select_output_stream() != 0) {
            mk_audio_copy_limited(g_audio_state.info.device.config, "hda-no-output-stream", MAX_AUDIO_DEV_LEN);
            mk_audio_refresh_topology_snapshot();
            return;
        }
        if (mk_audio_azalia_probe_codec() != 0) {
            mk_audio_copy_limited(g_audio_state.info.device.config, "hda-no-audio-fg", MAX_AUDIO_DEV_LEN);
            kernel_debug_puts("audio: azalia codec probe failed\n");
        } else if (mk_audio_azalia_probe_widgets() != 0) {
            mk_audio_copy_limited(g_audio_state.info.device.config, "hda-no-usable-output", MAX_AUDIO_DEV_LEN);
            kernel_debug_puts("audio: azalia widget probe failed\n");
        } else {
            kernel_debug_puts("audio: azalia widget probe ok\n");
        }
        if (mk_audio_azalia_has_usable_playback_path() &&
            mk_audio_azalia_format_from_params(&g_audio_state.info.parameters,
                                               &g_audio_state.azalia_output_fmt) == 0) {
            g_audio_state.info.flags |= MK_AUDIO_CAPS_PLAYBACK;
            g_audio_state.info.status.mode |= AUMODE_PLAY;
            kernel_debug_puts("audio: azalia playback path usable\n");
        }
        if (g_audio_state.azalia_codec_probed) {
            mk_audio_azalia_write32(g_audio_state.azalia_base,
                                    HDA_GCTL,
                                    mk_audio_azalia_read32(g_audio_state.azalia_base, HDA_GCTL) | HDA_GCTL_UNSOL);
        }
        /*
         * The current PIC layer only supports one handler per IRQ line.
         * On laptops like the T61 the HDA INTx line is often shared with
         * SATA/USB, so claiming it here can break storage during boot.
         * Keep Azalia in polling mode until shared PCI INTx dispatch exists.
         *
         * Do not register or mask a PIC IRQ line for HDA on this platform.
         */
        g_audio_state.azalia_irq_registered = 0u;
        if (g_audio_state.info.device.config[0] == '\0' ||
            (strcmp(g_audio_state.info.device.config, "hda-no-audio-fg") != 0 &&
             strcmp(g_audio_state.info.device.config, "hda-no-usable-output") != 0)) {
            mk_audio_copy_limited(g_audio_state.info.device.config, location, MAX_AUDIO_DEV_LEN);
        }
        kernel_debug_printf("audio: azalia select done config=%s flags=%x\n",
                            g_audio_state.info.device.config,
                            (unsigned int)g_audio_state.info.flags);
    } else if (g_audio_state.azalia_base == 0u) {
        mk_audio_copy_limited(g_audio_state.info.device.config, "hda-bar-unavailable", MAX_AUDIO_DEV_LEN);
        kernel_debug_puts("audio: azalia base unavailable\n");
    } else {
        mk_audio_copy_limited(g_audio_state.info.device.config, "hda-reset-failed", MAX_AUDIO_DEV_LEN);
        kernel_debug_puts("audio: azalia reset failed\n");
    }

    mk_audio_refresh_topology_snapshot();
}

static void mk_audio_state_reset_playback(void) {
    g_audio_state.playback_head = 0u;
    g_audio_state.playback_tail = 0u;
    g_audio_state.playback_fill = 0u;
    g_audio_state.playback_bytes_consumed = 0u;
    memset(g_audio_state.playback_buffer, 0, sizeof(g_audio_state.playback_buffer));
}

static void mk_audio_state_reset_async_write(void) {
    g_audio_state.async_write_head = 0u;
    g_audio_state.async_write_tail = 0u;
    g_audio_state.async_write_fill = 0u;
    memset(g_audio_state.async_write_buffer, 0, sizeof(g_audio_state.async_write_buffer));
}

static void mk_audio_state_reset_capture(void) {
    g_audio_state.capture_head = 0u;
    g_audio_state.capture_tail = 0u;
    g_audio_state.capture_fill = 0u;
    g_audio_state.capture_bytes_read = 0u;
    memset(g_audio_state.capture_buffer, 0, sizeof(g_audio_state.capture_buffer));
}

static uint32_t mk_audio_async_ring_write(const uint8_t *data, uint32_t size) {
    uint32_t writable;
    uint32_t i;

    if (data == 0 || size == 0u) {
        return 0u;
    }

    writable = MK_AUDIO_ASYNC_WRITE_BUFFER_SIZE - g_audio_state.async_write_fill;
    if (size > writable) {
        size = writable;
    }
    for (i = 0u; i < size; ++i) {
        g_audio_state.async_write_buffer[g_audio_state.async_write_tail] = data[i];
        g_audio_state.async_write_tail =
            (g_audio_state.async_write_tail + 1u) % MK_AUDIO_ASYNC_WRITE_BUFFER_SIZE;
    }
    g_audio_state.async_write_fill += size;
    return size;
}

static uint32_t mk_audio_async_ring_peek(uint8_t *data, uint32_t size) {
    uint32_t readable;
    uint32_t i;
    uint32_t index;

    if (data == 0 || size == 0u) {
        return 0u;
    }

    readable = g_audio_state.async_write_fill;
    if (size > readable) {
        size = readable;
    }
    index = g_audio_state.async_write_head;
    for (i = 0u; i < size; ++i) {
        data[i] = g_audio_state.async_write_buffer[index];
        index = (index + 1u) % MK_AUDIO_ASYNC_WRITE_BUFFER_SIZE;
    }
    return size;
}

static void mk_audio_async_ring_consume(uint32_t size) {
    if (size > g_audio_state.async_write_fill) {
        size = g_audio_state.async_write_fill;
    }
    g_audio_state.async_write_head =
        (g_audio_state.async_write_head + size) % MK_AUDIO_ASYNC_WRITE_BUFFER_SIZE;
    g_audio_state.async_write_fill -= size;
}

static uint32_t mk_audio_async_pump_chunk_limit(void) {
    switch (g_audio_state.backend_kind) {
    case MK_AUDIO_BACKEND_COMPAT_AUICH:
        return AUICH_DMA_SLOT_SIZE;
    case MK_AUDIO_BACKEND_COMPAT_AZALIA:
        return 4096u;
    case MK_AUDIO_BACKEND_COMPAT_UAUDIO:
        return 16384u;
    default:
        return 1024u;
    }
}

static void mk_audio_capture_ring_write(const uint8_t *data, uint32_t size) {
    uint32_t i;
    uint8_t was_empty;

    if (data == 0 || size == 0u) {
        return;
    }
    was_empty = g_audio_state.capture_fill == 0u ? 1u : 0u;

    if (size > MK_AUDIO_SOFT_BUFFER_SIZE) {
        data += size - MK_AUDIO_SOFT_BUFFER_SIZE;
        size = MK_AUDIO_SOFT_BUFFER_SIZE;
    }

    if ((g_audio_state.capture_fill + size) > MK_AUDIO_SOFT_BUFFER_SIZE) {
        uint32_t overflow = (g_audio_state.capture_fill + size) - MK_AUDIO_SOFT_BUFFER_SIZE;

        g_audio_state.capture_head =
            (g_audio_state.capture_head + overflow) % MK_AUDIO_SOFT_BUFFER_SIZE;
        g_audio_state.capture_fill -= overflow;
        mk_audio_capture_note_xrun();
    }

    for (i = 0u; i < size; ++i) {
        g_audio_state.capture_buffer[g_audio_state.capture_tail] = data[i];
        g_audio_state.capture_tail = (g_audio_state.capture_tail + 1u) % MK_AUDIO_SOFT_BUFFER_SIZE;
    }
    g_audio_state.capture_fill += size;
    g_audio_state.capture_bytes_captured += size;
    if (was_empty && g_audio_state.capture_fill != 0u) {
        mk_audio_publish_event(MK_AUDIO_EVENT_CAPTURE_READY,
                               g_audio_state.capture_fill,
                               g_audio_state.capture_xruns);
    }
}

static int mk_audio_capture_ring_read(uint8_t *data, uint32_t size) {
    uint32_t i;
    uint32_t to_read;

    if (data == 0 || size == 0u) {
        return -1;
    }

    to_read = size;
    if (to_read > g_audio_state.capture_fill) {
        to_read = g_audio_state.capture_fill;
    }
    if (to_read == 0u) {
        return 0;
    }

    for (i = 0u; i < to_read; ++i) {
        data[i] = g_audio_state.capture_buffer[g_audio_state.capture_head];
        g_audio_state.capture_head = (g_audio_state.capture_head + 1u) % MK_AUDIO_SOFT_BUFFER_SIZE;
    }
    g_audio_state.capture_fill -= to_read;
    g_audio_state.capture_bytes_read += to_read;
    g_audio_state.capture_read_calls++;
    return (int)to_read;
}

static void mk_audio_refresh_status_snapshot(void) {
    uint32_t runtime_flags = (uint32_t)g_audio_state.backend_kind & MK_AUDIO_STATUS_BACKEND_MASK;
    uint32_t queued_bytes = 0u;
    uint32_t now_ticks = kernel_timer_get_ticks();
    uint32_t previous_queued_bytes = g_audio_state.audio_event_last_queued_bytes;
    uint8_t previous_active = g_audio_state.audio_event_last_active;
    uint32_t current_queued_bytes;
    uint32_t current_underruns;

    if (g_audio_state.backend_kind == MK_AUDIO_BACKEND_COMPAT_AUICH) {
        mk_audio_compat_update_output_progress();
        mk_audio_compat_update_input_progress();
        g_audio_state.info.status.active =
            (g_audio_state.compat_output_running ||
             g_audio_state.compat_output_pending > 0u ||
             g_audio_state.compat_input_running ||
             g_audio_state.async_write_fill > 0u) ? 1 : 0;
    } else if (g_audio_state.backend_kind == MK_AUDIO_BACKEND_COMPAT_AZALIA) {
        if (g_audio_state.status_snapshot_tick != now_ticks) {
            if (!g_audio_state.azalia_output_running ||
                g_audio_state.azalia_output_poll_tick != now_ticks) {
                mk_audio_azalia_update_output_progress();
                g_audio_state.azalia_output_poll_tick = now_ticks;
            }
        }
        g_audio_state.info.status.active =
            (g_audio_state.azalia_output_running || g_audio_state.async_write_fill > 0u) ? 1 : 0;
    } else if (g_audio_state.backend_kind == MK_AUDIO_BACKEND_COMPAT_UAUDIO) {
        queued_bytes = mk_audio_uaudio_pending_bytes();
        queued_bytes += g_audio_state.async_write_fill;
        g_audio_state.info.status.active = queued_bytes != 0u ? 1 : 0;
    } else {
        g_audio_state.info.status.active =
            (g_audio_state.playback_fill > 0u ||
             g_audio_state.capture_fill > 0u ||
             g_audio_state.async_write_fill > 0u) ? 1 : 0;
    }

    if (g_audio_state.compat_irq_registered || g_audio_state.azalia_irq_registered) {
        runtime_flags |= MK_AUDIO_STATUS_FLAG_IRQ_REGISTERED;
    }
    if (g_audio_state.compat_irq_count != 0u || g_audio_state.azalia_irq_count != 0u) {
        runtime_flags |= MK_AUDIO_STATUS_FLAG_IRQ_SEEN;
    }
    if ((g_audio_state.compat_ready || g_audio_state.azalia_ready) &&
        (g_audio_state.pci.irq_line >= 16u ||
         (!g_audio_state.compat_irq_registered && !g_audio_state.azalia_irq_registered))) {
        runtime_flags |= MK_AUDIO_STATUS_FLAG_NO_VALID_IRQ;
    }
    if (g_audio_state.playback_starvations != 0u) {
        runtime_flags |= MK_AUDIO_STATUS_FLAG_STARVATION;
    }
    if (g_audio_state.playback_underruns != 0u) {
        runtime_flags |= MK_AUDIO_STATUS_FLAG_UNDERRUN;
    }
    if (g_audio_state.capture_bytes_captured != 0u) {
        runtime_flags |= MK_AUDIO_STATUS_FLAG_CAPTURE_DATA;
    }
    if (g_audio_state.capture_xruns != 0u) {
        runtime_flags |= MK_AUDIO_STATUS_FLAG_CAPTURE_XRUN;
    }

    g_audio_state.info.status._spare[0] = (int)runtime_flags;
    if (g_audio_state.backend_kind == MK_AUDIO_BACKEND_COMPAT_AZALIA) {
        g_audio_state.info.status._spare[1] =
            (int)((g_audio_state.azalia_output_running ? 1u : 0u) + g_audio_state.async_write_fill);
    } else if (g_audio_state.backend_kind == MK_AUDIO_BACKEND_COMPAT_UAUDIO) {
        g_audio_state.info.status._spare[1] = (int)queued_bytes;
    } else {
        g_audio_state.info.status._spare[1] =
            (int)(g_audio_state.compat_output_pending + g_audio_state.async_write_fill);
    }
    g_audio_state.info.status._spare[2] = (int)g_audio_state.playback_bytes_written;
    g_audio_state.info.status._spare[3] = (int)g_audio_state.playback_bytes_consumed;
    g_audio_state.info.status._spare[4] = (int)g_audio_state.playback_xruns;
    g_audio_state.status_snapshot_tick = now_ticks;
    current_queued_bytes = (uint32_t)(g_audio_state.info.status._spare[1] >= 0 ?
                                      g_audio_state.info.status._spare[1] : 0);
    current_underruns = (uint32_t)mk_audio_current_underruns();
    if (previous_queued_bytes == 0u && current_queued_bytes != 0u) {
        mk_audio_publish_event(MK_AUDIO_EVENT_QUEUED, current_queued_bytes, current_underruns);
    }
    if (previous_active != 0u && g_audio_state.info.status.active == 0 && current_queued_bytes == 0u) {
        mk_audio_publish_event(MK_AUDIO_EVENT_IDLE, current_queued_bytes, current_underruns);
    }
    if (current_underruns > g_audio_state.audio_event_last_underruns) {
        mk_audio_publish_event(MK_AUDIO_EVENT_UNDERRUN, current_queued_bytes, current_underruns);
    }
    g_audio_state.audio_event_last_queued_bytes = current_queued_bytes;
    g_audio_state.audio_event_last_underruns = current_underruns;
    g_audio_state.audio_event_last_active = (uint8_t)g_audio_state.info.status.active;
    mk_audio_refresh_topology_snapshot();
}

static int mk_audio_backend_soft_start(void) {
    g_audio_state.info.status.pause = 0;
    g_audio_state.info.status.active = 1;
    g_audio_state.playback_xruns = 0u;
    g_audio_state.playback_starvations = 0u;
    g_audio_state.playback_underruns = 0u;
    g_audio_state.compat_irq_count = 0u;
    g_audio_state.azalia_irq_count = 0u;
    return 0;
}

static int mk_audio_backend_soft_stop(void) {
    g_audio_state.info.status.pause = 0;
    g_audio_state.info.status.active = 0;
    mk_audio_state_reset_playback();
    return 0;
}

static int mk_audio_backend_soft_write(const uint8_t *data, uint32_t size) {
    uint32_t i;

    if (data == 0 || size == 0u) {
        return -1;
    }

    for (i = 0u; i < size; ++i) {
        g_audio_state.playback_buffer[g_audio_state.playback_tail] = data[i];
        g_audio_state.playback_tail = (g_audio_state.playback_tail + 1u) % MK_AUDIO_SOFT_BUFFER_SIZE;
        if (g_audio_state.playback_fill < MK_AUDIO_SOFT_BUFFER_SIZE) {
            g_audio_state.playback_fill++;
        } else {
            g_audio_state.playback_head = (g_audio_state.playback_head + 1u) % MK_AUDIO_SOFT_BUFFER_SIZE;
        }
    }

    g_audio_state.playback_bytes_written += size;
    g_audio_state.playback_write_calls++;
    g_audio_state.info.status.active = 1;
    return (int)size;
}

static int mk_audio_backend_pcspkr_start(void) {
    if (!kernel_timer_pc_speaker_available()) {
        return -1;
    }
    kernel_timer_pc_speaker_disable();
    g_audio_state.info.status.pause = 0;
    g_audio_state.info.status.active = 1;
    g_audio_state.playback_xruns = 0u;
    g_audio_state.playback_starvations = 0u;
    g_audio_state.playback_underruns = 0u;
    return 0;
}

static int mk_audio_backend_pcspkr_stop(void) {
    kernel_timer_pc_speaker_disable();
    g_audio_state.info.status.pause = 0;
    g_audio_state.info.status.active = 0;
    mk_audio_state_reset_playback();
    return 0;
}

static int mk_audio_pcspkr_sample_at(const uint8_t *data, uint32_t size, uint32_t frame_index) {
    uint32_t bytes_per_sample;
    uint32_t channels;
    uint32_t frame_size;
    uint32_t offset;
    const uint8_t *src;

    bytes_per_sample = g_audio_state.info.parameters.bps;
    channels = g_audio_state.info.parameters.pchan;
    if (bytes_per_sample == 0u || channels == 0u) {
        return 0;
    }
    frame_size = bytes_per_sample * channels;
    offset = frame_index * frame_size;
    if (offset + bytes_per_sample > size) {
        return 0;
    }
    src = &data[offset];

    switch (bytes_per_sample) {
    case 1u:
        if (g_audio_state.info.parameters.sig != 0u) {
            return (int)((int8_t)src[0]);
        }
        return (int)src[0] - 128;
    case 2u: {
        uint16_t raw = g_audio_state.info.parameters.le != 0u ?
                           (uint16_t)((uint16_t)src[0] | ((uint16_t)src[1] << 8)) :
                           (uint16_t)((uint16_t)src[1] | ((uint16_t)src[0] << 8));
        if (g_audio_state.info.parameters.sig != 0u) {
            return (int)((int16_t)raw) / 256;
        }
        return ((int)raw - 32768) / 256;
    }
    case 3u: {
        uint32_t raw = g_audio_state.info.parameters.le != 0u ?
                           ((uint32_t)src[0] | ((uint32_t)src[1] << 8) | ((uint32_t)src[2] << 16)) :
                           ((uint32_t)src[2] | ((uint32_t)src[1] << 8) | ((uint32_t)src[0] << 16));
        if ((raw & 0x00800000u) != 0u) {
            raw |= 0xFF000000u;
        }
        return ((int32_t)raw) / 65536;
    }
    case 4u: {
        uint32_t raw = g_audio_state.info.parameters.le != 0u ?
                           ((uint32_t)src[0] |
                            ((uint32_t)src[1] << 8) |
                            ((uint32_t)src[2] << 16) |
                            ((uint32_t)src[3] << 24)) :
                           ((uint32_t)src[3] |
                            ((uint32_t)src[2] << 8) |
                            ((uint32_t)src[1] << 16) |
                            ((uint32_t)src[0] << 24));
        return (int)((int32_t)raw >> 24);
    }
    default:
        return 0;
    }
}

static int mk_audio_backend_pcspkr_write(const uint8_t *data, uint32_t size) {
    uint32_t bytes_per_sample;
    uint32_t channels;
    uint32_t frame_size;
    uint32_t total_frames;
    uint32_t frames_per_window;
    uint32_t frame = 0u;

    if (data == 0 || size == 0u || !kernel_timer_pc_speaker_available()) {
        return -1;
    }

    bytes_per_sample = g_audio_state.info.parameters.bps;
    channels = g_audio_state.info.parameters.pchan;
    if (bytes_per_sample == 0u || channels == 0u) {
        return -1;
    }
    frame_size = bytes_per_sample * channels;
    if (frame_size == 0u) {
        return -1;
    }
    total_frames = size / frame_size;
    if (total_frames == 0u) {
        return -1;
    }

    frames_per_window = g_audio_state.info.parameters.rate / 100u;
    if (frames_per_window == 0u) {
        frames_per_window = 1u;
    }

    g_audio_state.playback_bytes_written += size;
    g_audio_state.playback_write_calls++;
    g_audio_state.info.status.active = 1;

    while (frame < total_frames) {
        uint32_t window_frames = total_frames - frame;
        uint32_t zero_crossings = 0u;
        uint32_t magnitude_sum = 0u;
        uint32_t target_tick;
        int prev_sample = 0;
        int have_prev = 0;

        if (window_frames > frames_per_window) {
            window_frames = frames_per_window;
        }

        for (uint32_t i = 0u; i < window_frames; ++i) {
            int sample = mk_audio_pcspkr_sample_at(data, size, frame + i);

            if (sample < 0) {
                magnitude_sum += (uint32_t)(-sample);
            } else {
                magnitude_sum += (uint32_t)sample;
            }
            if (have_prev && ((prev_sample < 0 && sample >= 0) || (prev_sample >= 0 && sample < 0))) {
                zero_crossings++;
            }
            prev_sample = sample;
            have_prev = 1;
        }

        if (magnitude_sum / window_frames < 6u || zero_crossings < 2u) {
            kernel_timer_pc_speaker_disable();
        } else {
            uint32_t hz = (zero_crossings * g_audio_state.info.parameters.rate) / (window_frames * 2u);

            if (hz < 40u) {
                hz = 40u;
            }
            if (hz > 12000u) {
                hz = 12000u;
            }
            kernel_timer_pc_speaker_set_frequency(hz);
        }

        target_tick = kernel_timer_get_ticks() + 1u;
        while ((int32_t)(kernel_timer_get_ticks() - target_tick) < 0) {
            yield();
        }

        frame += window_frames;
        g_audio_state.playback_bytes_consumed =
            (uint32_t)(((uint64_t)frame * (uint64_t)frame_size) > size ? size : (frame * frame_size));
    }

    kernel_timer_pc_speaker_disable();
    g_audio_state.info.status.active = 0;
    return (int)size;
}

static int mk_audio_backend_uaudio_start(void) {
    if (!g_audio_state.usb_audio_attached_ready ||
        g_audio_state.usb_audio_playback_endpoint_address == 0xffu ||
        g_audio_state.usb_audio_playback_endpoint_max_packet == 0u ||
        kernel_usb_audio_playback_supported() != 0) {
        char device_config[MAX_AUDIO_DEV_LEN];

        memset(device_config, 0, sizeof(device_config));
        mk_audio_build_usb_audio_reason(device_config,
                                        sizeof(device_config),
                                        "usb-audio-",
                                        g_audio_state.usb_audio_transport_kind,
                                        "-unsupported");
        mk_audio_copy_limited(g_audio_state.info.device.config, device_config, MAX_AUDIO_DEV_LEN);
        mk_audio_failover_from_unusable_uaudio();
        return -1;
    }
    mk_audio_reset_uaudio_runtime();
    g_audio_state.usb_audio_stream_started = 1u;
    mk_audio_set_uaudio_identity("-attached");
    g_audio_state.info.status.pause = 0;
    g_audio_state.info.status.active = 0;
    return 0;
}

static int mk_audio_backend_uaudio_stop(void) {
    if (g_audio_state.usb_audio_stream_started) {
        (void)mk_audio_uaudio_flush_staging(1u);
    }
    mk_audio_uaudio_update_output_progress();
    mk_audio_reset_uaudio_runtime();
    mk_audio_set_uaudio_identity("-attached");
    g_audio_state.info.status.pause = 0;
    g_audio_state.info.status.active = 0;
    return 0;
}

static int mk_audio_backend_uaudio_write(const uint8_t *data, uint32_t size) {
    char device_config[MAX_AUDIO_DEV_LEN];
    uint32_t frame_bytes;
    uint32_t remaining;
    uint32_t copied = 0u;
    int flush_status;

    if (data == 0 || size == 0u || !g_audio_state.usb_audio_stream_started) {
        return -1;
    }
    frame_bytes = mk_audio_uaudio_frame_bytes();
    if (frame_bytes != 0u && (size % frame_bytes) != 0u) {
        memset(device_config, 0, sizeof(device_config));
        mk_audio_build_usb_audio_reason(device_config,
                                        sizeof(device_config),
                                        "usb-audio-",
                                        g_audio_state.usb_audio_transport_kind,
                                        "-unaligned-write");
        mk_audio_copy_limited(g_audio_state.info.device.config, device_config, MAX_AUDIO_DEV_LEN);
        g_audio_state.info.status.active = 0;
        mk_audio_failover_from_unusable_uaudio();
        return -1;
    }

    remaining = size;
    while (remaining != 0u) {
        uint32_t space = MK_AUDIO_SOFT_BUFFER_SIZE - (uint32_t)g_audio_state.usb_audio_staging_fill;
        uint32_t chunk = remaining;

        if (space == 0u) {
            flush_status = mk_audio_uaudio_flush_staging(1u);
            if (flush_status <= 0) {
                g_audio_state.info.status.active = mk_audio_uaudio_pending_bytes() != 0u ? 1 : 0;
                if (copied == 0u) {
                    mk_audio_failover_from_unusable_uaudio();
                }
                return copied != 0u ? (int)copied : -1;
            }
            space = MK_AUDIO_SOFT_BUFFER_SIZE - (uint32_t)g_audio_state.usb_audio_staging_fill;
        }
        if (chunk > space) {
            chunk = space;
        }

        memcpy(g_audio_state.usb_audio_staging_buffer + g_audio_state.usb_audio_staging_fill,
               data + copied,
               chunk);
        g_audio_state.usb_audio_staging_fill = (uint16_t)((uint32_t)g_audio_state.usb_audio_staging_fill + chunk);
        copied += chunk;
        remaining -= chunk;

        flush_status = mk_audio_uaudio_flush_staging(0u);
        if (flush_status < 0) {
            g_audio_state.info.status.active = mk_audio_uaudio_pending_bytes() != 0u ? 1 : 0;
            if (copied == 0u) {
                mk_audio_failover_from_unusable_uaudio();
            }
            return copied != 0u ? (int)copied : -1;
        }
    }

    if (copied == 0u) {
        memset(device_config, 0, sizeof(device_config));
        mk_audio_build_usb_audio_reason(device_config,
                                        sizeof(device_config),
                                        "usb-audio-",
                                        g_audio_state.usb_audio_transport_kind,
                                        "-write-failed");
        mk_audio_copy_limited(g_audio_state.info.device.config, device_config, MAX_AUDIO_DEV_LEN);
        g_audio_state.info.status.active = 0;
        mk_audio_failover_from_unusable_uaudio();
        return -1;
    }
    g_audio_state.playback_bytes_written += copied;
    g_audio_state.playback_write_calls++;
    if (g_audio_state.usb_audio_staging_fill == 0u) {
        mk_audio_set_uaudio_identity("-attached");
    }
    g_audio_state.info.status.active = mk_audio_uaudio_pending_bytes() != 0u ? 1 : 0;
    return (int)copied;
}

static int mk_audio_backend_compat_start(void) {
    if (!g_audio_state.compat_ready) {
        mk_audio_failover_from_unusable_compat();
        return -1;
    }
    if (!g_audio_state.compat_codec_ready && mk_audio_compat_reset_codec() != 0) {
        mk_audio_failover_from_unusable_compat();
        return -1;
    }
    mk_audio_compat_reset_output_ring();
    mk_audio_compat_reset_input_ring();
    mk_audio_compat_apply_params();
    mk_audio_compat_apply_mixer_state();
    return mk_audio_backend_soft_start();
}

static int mk_audio_backend_compat_stop(void) {
    uint32_t wait_count = 0u;

    while (g_audio_state.compat_output_pending > 0u && wait_count < 200000u) {
        mk_audio_compat_update_output_progress();
        mk_audio_compat_delay();
        wait_count++;
    }
    mk_audio_compat_halt_output();
    mk_audio_compat_halt_input();
    return mk_audio_backend_soft_stop();
}

static int mk_audio_backend_compat_write(const uint8_t *data, uint32_t size) {
    uint32_t offset = 0u;
    uint32_t dma_chunk_limit;

    if (!g_audio_state.compat_ready || !g_audio_state.compat_codec_ready || data == 0 || size == 0u) {
        if (data != 0 && size != 0u) {
            mk_audio_failover_from_unusable_compat();
        }
        return -1;
    }

    dma_chunk_limit = g_audio_state.info.parameters.round;
    if (dma_chunk_limit == 0u || dma_chunk_limit > AUICH_DMA_SLOT_SIZE) {
        dma_chunk_limit = AUICH_DMA_SLOT_SIZE;
    }
    if ((dma_chunk_limit & 1u) != 0u) {
        dma_chunk_limit -= 1u;
    }
    if (dma_chunk_limit == 0u) {
        dma_chunk_limit = AUICH_DMA_SLOT_SIZE;
    }

    mk_audio_compat_update_output_progress();

    while (offset < size) {
        uint8_t slot;
        uint32_t chunk_size = size - offset;

        if (chunk_size > dma_chunk_limit) {
            chunk_size = dma_chunk_limit;
        }
        if ((chunk_size & 1u) != 0u) {
            chunk_size -= 1u;
        }
        if (chunk_size == 0u) {
            break;
        }

        if (g_audio_state.compat_output_pending >= (AUICH_DMALIST_MAX - 1u)) {
            mk_audio_compat_update_output_progress();
            if (g_audio_state.compat_output_pending >= (AUICH_DMALIST_MAX - 1u)) {
                break;
            }
        }

        slot = g_audio_state.compat_output_producer;
        memcpy(&g_audio_auich_pcmo_buffers[slot][0], data + offset, chunk_size);
        if (chunk_size < AUICH_DMA_SLOT_SIZE) {
            memset(&g_audio_auich_pcmo_buffers[slot][chunk_size], 0, AUICH_DMA_SLOT_SIZE - chunk_size);
        }
        g_audio_auich_pcmo_dmalist[slot].base = (uint32_t)(uintptr_t)&g_audio_auich_pcmo_buffers[slot][0];
        g_audio_auich_pcmo_dmalist[slot].len = ((chunk_size / 2u) & 0xffffu) | AUICH_DMAF_IOC;
        g_audio_auich_pcmo_bytes[slot] = (uint16_t)chunk_size;

        g_audio_state.compat_output_producer =
            (uint8_t)((g_audio_state.compat_output_producer + 1u) & 0x1fu);
        g_audio_state.compat_output_pending++;
        offset += chunk_size;

        if (mk_audio_compat_start_output_if_needed() != 0) {
            break;
        }
        mk_audio_compat_write8(g_audio_state.compat_aud_base,
                               g_audio_state.compat_aud_is_mmio,
                               AUICH_PCMO + AUICH_LVI,
                               (uint8_t)((g_audio_state.compat_output_producer - 1u) & AUICH_LVI_MASK));
        mk_audio_compat_update_output_progress();
        if (offset < size) {
            yield();
        }
    }

    if (offset != 0u) {
        (void)mk_audio_backend_soft_write(data, offset);
    }
    return (int)offset;
}

static int mk_audio_backend_compat_read(uint8_t *data, uint32_t size) {
    if (!g_audio_state.compat_ready || !g_audio_state.compat_codec_ready || data == 0 || size == 0u) {
        return -1;
    }

    if (g_audio_state.capture_fill == 0u) {
        if (mk_audio_compat_capture_block(size) < 0) {
            return 0;
        }
    }

    return mk_audio_capture_ring_read(data, size);
}

static int mk_audio_backend_azalia_start(void) {
    if (!g_audio_state.azalia_ready) {
        return -1;
    }
    if (mk_audio_azalia_has_fatal_probe_failure()) {
        if (!g_audio_state.azalia_codec_probed || g_audio_state.azalia_afg_nid == 0u) {
            mk_audio_copy_limited(g_audio_state.info.device.config, "hda-no-audio-fg", MAX_AUDIO_DEV_LEN);
        } else {
            mk_audio_copy_limited(g_audio_state.info.device.config, "hda-no-usable-output", MAX_AUDIO_DEV_LEN);
        }
        g_audio_state.info.status.active = 0;
        mk_audio_failover_from_unusable_hda();
        return -1;
    }
    if (!mk_audio_azalia_has_usable_playback_path()) {
        if (!g_audio_state.azalia_codec_probed || g_audio_state.azalia_afg_nid == 0u) {
            mk_audio_copy_limited(g_audio_state.info.device.config, "hda-no-audio-fg", MAX_AUDIO_DEV_LEN);
        } else {
            mk_audio_copy_limited(g_audio_state.info.device.config, "hda-no-usable-output", MAX_AUDIO_DEV_LEN);
        }
        g_audio_state.info.status.active = 0;
        mk_audio_failover_from_unusable_hda();
        return -1;
    }
    g_audio_state.info.status.pause = 0;
    g_audio_state.info.status.active = 0;
    if (mk_audio_azalia_format_from_params(&g_audio_state.info.parameters,
                                           &g_audio_state.azalia_output_fmt) != 0) {
        return -1;
    }
    if (mk_audio_azalia_program_output_path() != 0) {
        if (mk_audio_azalia_has_fatal_probe_failure()) {
            if (!g_audio_state.azalia_codec_probed || g_audio_state.azalia_afg_nid == 0u) {
                mk_audio_copy_limited(g_audio_state.info.device.config, "hda-no-audio-fg", MAX_AUDIO_DEV_LEN);
            } else {
                mk_audio_copy_limited(g_audio_state.info.device.config, "hda-no-usable-output", MAX_AUDIO_DEV_LEN);
            }
            g_audio_state.info.status.active = 0;
            mk_audio_failover_from_unusable_hda();
            return -1;
        }
        mk_audio_azalia_refresh_codec_mask();
        if (mk_audio_azalia_probe_codec() != 0 ||
            mk_audio_azalia_probe_widgets() != 0 ||
            mk_audio_azalia_program_output_path() != 0) {
            if (!g_audio_state.azalia_codec_probed || g_audio_state.azalia_afg_nid == 0u) {
                mk_audio_copy_limited(g_audio_state.info.device.config, "hda-no-audio-fg", MAX_AUDIO_DEV_LEN);
            } else if (!mk_audio_azalia_has_usable_playback_path()) {
                mk_audio_copy_limited(g_audio_state.info.device.config, "hda-no-usable-output", MAX_AUDIO_DEV_LEN);
            } else if (!mk_audio_azalia_config_is_fatal(g_audio_state.info.device.config)) {
                mk_audio_copy_limited(g_audio_state.info.device.config,
                                      "hda-codec-connect-failed",
                                      MAX_AUDIO_DEV_LEN);
            }
            g_audio_state.info.status.active = 0;
            mk_audio_failover_from_unusable_hda();
            return -1;
        }
    }
    g_audio_state.azalia_output_running = 0u;
    g_audio_state.azalia_output_bytes = 0u;
    g_audio_state.azalia_output_blk = 0u;
    g_audio_state.azalia_output_swpos = 0u;
    g_audio_state.azalia_output_pos = 0u;
    g_audio_state.azalia_output_start_tick = 0u;
    g_audio_state.azalia_output_deadline_tick = 0u;
    return 0;
}

static int mk_audio_backend_azalia_stop(void) {
    mk_audio_azalia_stream_halt();
    g_audio_state.info.status.pause = 0;
    g_audio_state.info.status.active = 0;
    return 0;
}

static int mk_audio_backend_azalia_write(const uint8_t *data, uint32_t size) {
    static int trace_budget = 0;
    uint32_t bytes;
    int trace = 0;

    if (trace_budget > 0) {
        trace = 1;
        trace_budget--;
        kernel_debug_puts("audio: hda write enter\n");
    }

    if (!g_audio_state.azalia_ready || data == 0 || size == 0u) {
        return -1;
    }
    if (mk_audio_azalia_has_fatal_probe_failure()) {
        if (!g_audio_state.azalia_codec_probed || g_audio_state.azalia_afg_nid == 0u) {
            mk_audio_copy_limited(g_audio_state.info.device.config, "hda-no-audio-fg", MAX_AUDIO_DEV_LEN);
        } else {
            mk_audio_copy_limited(g_audio_state.info.device.config, "hda-no-usable-output", MAX_AUDIO_DEV_LEN);
        }
        g_audio_state.info.status.active = 0;
        mk_audio_failover_from_unusable_hda();
        return -1;
    }
    if (!mk_audio_azalia_has_usable_playback_path()) {
        if (!g_audio_state.azalia_codec_probed || g_audio_state.azalia_afg_nid == 0u) {
            mk_audio_copy_limited(g_audio_state.info.device.config, "hda-no-audio-fg", MAX_AUDIO_DEV_LEN);
        } else {
            mk_audio_copy_limited(g_audio_state.info.device.config, "hda-no-usable-output", MAX_AUDIO_DEV_LEN);
        }
        g_audio_state.info.status.active = 0;
        mk_audio_failover_from_unusable_hda();
        return -1;
    }
    if (g_audio_state.azalia_output_running) {
        mk_audio_azalia_update_output_progress();
        if (g_audio_state.azalia_output_running) {
            uint32_t now_ticks = kernel_timer_get_ticks();
            uint32_t remaining = g_audio_state.azalia_output_bytes - g_audio_state.azalia_output_pos;

            if ((g_audio_state.azalia_output_deadline_tick != 0u &&
                 (int32_t)(now_ticks + 1u - g_audio_state.azalia_output_deadline_tick) >= 0) ||
                remaining <= 1024u) {
                mk_audio_azalia_finish_output_soft();
            } else {
                mk_audio_azalia_stream_halt();
                g_audio_state.playback_bytes_consumed = g_audio_state.playback_bytes_written;
                g_audio_state.azalia_output_pos = g_audio_state.azalia_output_bytes;
            }
        }
    }
    bytes = size;
    if (mk_audio_azalia_alloc_dma_buffers() != 0) {
        return -1;
    }
    if (bytes > MK_AUDIO_SOFT_BUFFER_SIZE) {
        bytes = MK_AUDIO_SOFT_BUFFER_SIZE;
    }
    if ((bytes & 1u) != 0u) {
        bytes -= 1u;
    }
    if (bytes == 0u) {
        return -1;
    }

    if (trace) {
        kernel_debug_puts("audio: hda write copy\n");
    }
    mk_audio_azalia_copy_output_buffer(data, bytes);
    if (trace) {
        kernel_debug_puts("audio: hda write start-buffer\n");
    }
    if (mk_audio_azalia_stream_start_buffer(bytes) != 0) {
        if (trace) {
            kernel_debug_puts("audio: hda write start-buffer failed\n");
        }
        g_audio_state.info.status.active = 0;
        if (mk_audio_azalia_config_is_fatal(g_audio_state.info.device.config)) {
            mk_audio_failover_from_unusable_hda();
        }
        return -1;
    }
    g_audio_state.playback_bytes_written += bytes;
    g_audio_state.playback_write_calls++;
    g_audio_state.info.status.active = 1;
    if (trace) {
        kernel_debug_puts("audio: hda write return\n");
    }
    return (int)bytes;
}

static uint8_t mk_audio_clamp_gain(int value) {
    if (value < AUDIO_MIN_GAIN) {
        return AUDIO_MIN_GAIN;
    }
    if (value > AUDIO_MAX_GAIN) {
        return AUDIO_MAX_GAIN;
    }
    return (uint8_t)value;
}

static uint8_t mk_audio_level_from_ctrl(const mixer_ctrl_t *control) {
    uint32_t sum = 0u;
    int channels;

    if (control == 0 || control->un.value.num_channels <= 0) {
        return 0u;
    }

    channels = control->un.value.num_channels;
    if (channels > 8) {
        channels = 8;
    }
    for (int i = 0; i < channels; ++i) {
        sum += control->un.value.level[i];
    }
    return (uint8_t)(sum / (uint32_t)channels);
}

static void mk_audio_fill_level_ctrl(mixer_ctrl_t *control, uint8_t level) {
    if (control == 0) {
        return;
    }

    control->type = AUDIO_MIXER_VALUE;
    control->un.value.num_channels = 2;
    control->un.value.level[AUDIO_MIXER_LEVEL_LEFT] = level;
    control->un.value.level[AUDIO_MIXER_LEVEL_RIGHT] = level;
}

static int mk_audio_default_output_valid(int value) {
    return value >= 0 && (uint32_t)value < mk_audio_output_count();
}

static int mk_audio_default_input_valid(int value) {
    return value >= 0 && (uint32_t)value < mk_audio_input_count();
}

static int mk_audio_state_read_control(mixer_ctrl_t *control) {
    if (control == 0) {
        return -1;
    }

    switch (control->dev) {
    case MK_AUDIO_MIXER_OUTPUT_LEVEL:
        mk_audio_fill_level_ctrl(control, g_audio_state.output_level);
        return 0;
    case MK_AUDIO_MIXER_OUTPUT_MUTE:
        control->type = AUDIO_MIXER_ENUM;
        control->un.ord = g_audio_state.output_muted ? 1 : 0;
        return 0;
    case MK_AUDIO_MIXER_INPUT_LEVEL:
        mk_audio_fill_level_ctrl(control, g_audio_state.input_level);
        return 0;
    case MK_AUDIO_MIXER_INPUT_MUTE:
        control->type = AUDIO_MIXER_ENUM;
        control->un.ord = g_audio_state.input_muted ? 1 : 0;
        return 0;
    case MK_AUDIO_MIXER_OUTPUT_DEFAULT:
        control->type = AUDIO_MIXER_ENUM;
        control->un.ord = (int)g_audio_state.default_output;
        return 0;
    case MK_AUDIO_MIXER_INPUT_DEFAULT:
        control->type = AUDIO_MIXER_ENUM;
        control->un.ord = (int)g_audio_state.default_input;
        return 0;
    default:
        return -1;
    }
}

static int mk_audio_state_write_control(const mixer_ctrl_t *control) {
    if (control == 0) {
        return -1;
    }

    switch (control->dev) {
    case MK_AUDIO_MIXER_OUTPUT_LEVEL:
        if (control->type != AUDIO_MIXER_VALUE) {
            return -1;
        }
        g_audio_state.output_level = mk_audio_clamp_gain((int)mk_audio_level_from_ctrl(control));
        g_audio_state.output_muted = g_audio_state.output_level == 0u ? 1u : 0u;
        if (g_audio_state.backend_kind == MK_AUDIO_BACKEND_COMPAT_AUICH) {
            mk_audio_compat_apply_mixer_state();
        }
        return 0;
    case MK_AUDIO_MIXER_OUTPUT_MUTE:
        if (control->type != AUDIO_MIXER_ENUM) {
            return -1;
        }
        g_audio_state.output_muted = control->un.ord != 0 ? 1u : 0u;
        if (g_audio_state.backend_kind == MK_AUDIO_BACKEND_COMPAT_AUICH) {
            mk_audio_compat_apply_mixer_state();
        }
        return 0;
    case MK_AUDIO_MIXER_INPUT_LEVEL:
        if (control->type != AUDIO_MIXER_VALUE) {
            return -1;
        }
        g_audio_state.input_level = mk_audio_clamp_gain((int)mk_audio_level_from_ctrl(control));
        g_audio_state.input_muted = g_audio_state.input_level == 0u ? 1u : 0u;
        if (g_audio_state.backend_kind == MK_AUDIO_BACKEND_COMPAT_AUICH) {
            mk_audio_compat_apply_mixer_state();
        }
        return 0;
    case MK_AUDIO_MIXER_INPUT_MUTE:
        if (control->type != AUDIO_MIXER_ENUM) {
            return -1;
        }
        g_audio_state.input_muted = control->un.ord != 0 ? 1u : 0u;
        if (g_audio_state.backend_kind == MK_AUDIO_BACKEND_COMPAT_AUICH) {
            mk_audio_compat_apply_mixer_state();
        }
        return 0;
    case MK_AUDIO_MIXER_OUTPUT_DEFAULT:
        if (control->type != AUDIO_MIXER_ENUM || !mk_audio_default_output_valid(control->un.ord)) {
            return -1;
        }
        g_audio_state.default_output = (uint8_t)control->un.ord;
        if (g_audio_state.backend_kind == MK_AUDIO_BACKEND_COMPAT_AZALIA) {
            g_audio_state.azalia_path_programmed = 0u;
        }
        if (g_audio_state.backend_kind == MK_AUDIO_BACKEND_COMPAT_AUICH) {
            mk_audio_compat_apply_mixer_state();
        }
        return 0;
    case MK_AUDIO_MIXER_INPUT_DEFAULT:
        if (control->type != AUDIO_MIXER_ENUM || !mk_audio_default_input_valid(control->un.ord)) {
            return -1;
        }
        g_audio_state.default_input = (uint8_t)control->un.ord;
        if (g_audio_state.backend_kind == MK_AUDIO_BACKEND_COMPAT_AUICH) {
            mk_audio_compat_apply_mixer_state();
        }
        return 0;
    default:
        return -1;
    }
}

static int mk_audio_state_get_control_info(uint32_t index, struct mk_audio_control_info *info) {
    if (info == 0 || index >= (sizeof(g_audio_controls) / sizeof(g_audio_controls[0]))) {
        return -1;
    }

    *info = g_audio_controls[index];
    return 0;
}

static int mk_audio_local_handler(const struct mk_message *request,
                                  struct mk_message *reply,
                                  void *context) {
    (void)context;
    if (request == 0 || reply == 0) {
        return -1;
    }

    g_last_audio_request = *request;
    mk_message_init(reply, request->type);
    reply->source_pid = request->target_pid;
    reply->target_pid = request->source_pid;

    switch (request->type) {
    case MK_MSG_HELLO:
    case MK_MSG_AUDIO_GETINFO:
        if (request->payload_size != 0u) {
            return -1;
        }
        mk_audio_refresh_topology_snapshot();
        mk_audio_refresh_status_snapshot();
        if (mk_message_set_payload(reply, &g_audio_state.info, sizeof(g_audio_state.info)) != 0) {
            return -1;
        }
        g_last_audio_reply = *reply;
        return 0;
    case MK_MSG_AUDIO_GET_STATUS:
        if (request->payload_size != 0u) {
            return -1;
        }
        mk_audio_refresh_status_snapshot();
        if (mk_message_set_payload(reply,
                                   &g_audio_state.info.status,
                                   sizeof(g_audio_state.info.status)) != 0) {
            return -1;
        }
        g_last_audio_reply = *reply;
        return 0;
    case MK_MSG_AUDIO_GET_PARAMS:
        if (request->payload_size != 0u) {
            return -1;
        }
        if (mk_message_set_payload(reply,
                                   &g_audio_state.info.parameters,
                                   sizeof(g_audio_state.info.parameters)) != 0) {
            return -1;
        }
        g_last_audio_reply = *reply;
        return 0;
    case MK_MSG_AUDIO_SET_PARAMS:
        if (request->payload_size != sizeof(g_audio_state.info.parameters)) {
            return -1;
        }
        memcpy(&g_audio_state.info.parameters,
               request->payload,
               sizeof(g_audio_state.info.parameters));
        if (g_audio_state.backend_kind == MK_AUDIO_BACKEND_COMPAT_UAUDIO) {
            mk_audio_normalize_uaudio_params(&g_audio_state.info.parameters);
        } else {
            mk_audio_normalize_params(&g_audio_state.info.parameters);
        }
        if (g_audio_state.backend_kind == MK_AUDIO_BACKEND_COMPAT_AUICH) {
            mk_audio_compat_apply_params();
        }
        break;
    case MK_MSG_AUDIO_START:
        if (request->payload_size != 0u) {
            return -1;
        }
        if (g_audio_backend->start == 0 || g_audio_backend->start() != 0) {
            if (mk_audio_reply_result(reply, -1) != 0) {
                return -1;
            }
            g_last_audio_reply = *reply;
            return 0;
        }
        break;
    case MK_MSG_AUDIO_STOP:
        if (request->payload_size != 0u) {
            return -1;
        }
        if (g_audio_backend->stop == 0 || g_audio_backend->stop() != 0) {
            if (mk_audio_reply_result(reply, -1) != 0) {
                return -1;
            }
            g_last_audio_reply = *reply;
            return 0;
        }
        break;
    case MK_MSG_AUDIO_WRITE:
        if (request->payload_size != sizeof(struct mk_audio_write_request)) {
            return -1;
        }
        {
            const struct mk_audio_write_request *write_request;
            int rc;

            write_request = (const struct mk_audio_write_request *)request->payload;
            if (write_request->size > MK_AUDIO_INLINE_WRITE_MAX) {
                return -1;
            }
            if (g_audio_backend->write == 0) {
                rc = -1;
            } else {
                rc = g_audio_backend->write(write_request->data, write_request->size);
            }
            if (mk_audio_reply_result(reply, rc) != 0) {
                return -1;
            }
            g_last_audio_reply = *reply;
            return 0;
        }
    case MK_MSG_AUDIO_WRITE_ASYNC:
        if (request->payload_size != sizeof(struct mk_audio_write_request)) {
            return -1;
        }
        {
            const struct mk_audio_write_request *write_request;
            int rc;

            write_request = (const struct mk_audio_write_request *)request->payload;
            if (write_request->size > MK_AUDIO_INLINE_WRITE_MAX) {
                return -1;
            }
            rc = (int)mk_audio_async_ring_write(write_request->data, write_request->size);
            mk_audio_refresh_status_snapshot();
            if (mk_audio_reply_result(reply, rc) != 0) {
                return -1;
            }
            g_last_audio_reply = *reply;
            return 0;
        }
    case MK_MSG_AUDIO_READ:
        if (request->payload_size != sizeof(struct mk_audio_transfer_request)) {
            return -1;
        }
        {
            const struct mk_audio_transfer_request *read_request;
            struct mk_audio_read_reply payload;
            int rc;

            read_request = (const struct mk_audio_transfer_request *)request->payload;
            if (read_request->size > MK_AUDIO_INLINE_READ_MAX) {
                return -1;
            }
            memset(&payload, 0, sizeof(payload));
            if (g_audio_state.backend_kind != MK_AUDIO_BACKEND_COMPAT_AUICH) {
                rc = -1;
            } else {
                rc = mk_audio_backend_compat_read(payload.data, read_request->size);
            }
            if (rc < 0) {
                if (mk_audio_reply_result(reply, rc) != 0) {
                    return -1;
                }
            } else {
                payload.size = (uint32_t)rc;
                if (mk_message_set_payload(reply, &payload, sizeof(payload)) != 0) {
                    return -1;
                }
            }
            g_last_audio_reply = *reply;
            return 0;
        }
    case MK_MSG_AUDIO_MIXER_READ:
        if (request->payload_size != sizeof(mixer_ctrl_t)) {
            return -1;
        }
        mixer_ctrl_t control;

        memcpy(&control, request->payload, sizeof(control));
        if (mk_audio_state_read_control(&control) != 0) {
            if (mk_audio_reply_result(reply, -1) != 0) {
                return -1;
            }
            g_last_audio_reply = *reply;
            return 0;
        }
        if (mk_message_set_payload(reply, &control, sizeof(control)) != 0) {
            return -1;
        }
        g_last_audio_reply = *reply;
        return 0;
    case MK_MSG_AUDIO_MIXER_WRITE:
        if (request->payload_size != sizeof(mixer_ctrl_t)) {
            return -1;
        }
        if (mk_audio_state_write_control((const mixer_ctrl_t *)request->payload) != 0) {
            if (mk_audio_reply_result(reply, -1) != 0) {
                return -1;
            }
            g_last_audio_reply = *reply;
            return 0;
        }
        break;
    case MK_MSG_AUDIO_CONTROL_INFO:
        if (request->payload_size != sizeof(uint32_t)) {
            return -1;
        }
        {
            uint32_t index = 0u;
            struct mk_audio_control_info info;

            memcpy(&index, request->payload, sizeof(index));
            if (mk_audio_state_get_control_info(index, &info) != 0) {
                if (mk_audio_reply_result(reply, -1) != 0) {
                    return -1;
                }
                g_last_audio_reply = *reply;
                return 0;
            }
            if (mk_message_set_payload(reply, &info, sizeof(info)) != 0) {
                return -1;
            }
            g_last_audio_reply = *reply;
            return 0;
        }
    case MK_MSG_AUDIO_PLAY_ASSET:
        if (request->payload_size != sizeof(struct mk_audio_play_asset_request)) {
            return -1;
        }
        /* Defer to audiosvc worker; kernel handler returns failure to force fallback */
        if (mk_audio_reply_result(reply, -1) != 0) {
            return -1;
        }
        g_last_audio_reply = *reply;
        return 0;
    default:
        return -1;
    }

    if (mk_audio_reply_result(reply, 0) != 0) {
        return -1;
    }
    g_last_audio_reply = *reply;
    return 0;
}

static void mk_audio_service_init_state(void) {
    static uint8_t g_audio_tick_hook_registered = 0u;

    memset(&g_last_audio_request, 0, sizeof(g_last_audio_request));
    memset(&g_last_audio_reply, 0, sizeof(g_last_audio_reply));
    memset(&g_audio_state, 0, sizeof(g_audio_state));
    g_audio_bootstrap_defer_probe = 1u;

    g_audio_state.info.flags = MK_AUDIO_CAPS_MIXER |
                               MK_AUDIO_CAPS_PLAYBACK |
                               MK_AUDIO_CAPS_BSD_AUDIOIO_ABI;
    g_audio_state.info.status.mode = AUMODE_PLAY;
    AUDIO_INITPAR(&g_audio_state.info.parameters);
    g_audio_state.info.parameters.rate = 48000u;
    g_audio_state.info.parameters.bits = 16u;
    g_audio_state.info.parameters.bps = 2u;
    g_audio_state.info.parameters.sig = 1u;
    g_audio_state.info.parameters.le = 1u;
    g_audio_state.info.parameters.pchan = 2u;
    g_audio_state.info.parameters.rchan = 2u;
    g_audio_state.info.parameters.nblks = 4u;
    g_audio_state.info.parameters.round = 512u;
    g_audio_state.info.parameters.msb = 1u;
    mk_audio_normalize_params(&g_audio_state.info.parameters);
    g_audio_state.output_level = 192u;
    g_audio_state.input_level = 160u;
    g_audio_state.output_muted = 0u;
    g_audio_state.input_muted = 0u;
    g_audio_state.default_output = 0u;
    g_audio_state.default_input = 0u;
    mk_audio_state_reset_playback();
    mk_audio_state_reset_async_write();
    mk_audio_state_reset_capture();
    mk_audio_event_init_subscribers();
    if (!g_audio_tick_hook_registered) {
        (void)kernel_timer_register_tick_hook(mk_audio_service_tick);
        g_audio_tick_hook_registered = 1u;
    }
    kernel_text_puts("    audio: soft\n");
    mk_audio_select_soft_backend();
    kernel_text_puts("    audio: uaudio-driver\n");
    (void)kernel_uaudio_init();
    kernel_text_puts("    audio: usb-snap\n");
    mk_audio_refresh_usb_attach_snapshot();
}

static int mk_audio_service_launch_deferred(void) {
    kernel_debug_puts("audio: deferred launch enter\n");

    kernel_text_puts("    audio: azalia?\n");
    if (mk_audio_try_azalia_backends() == 0) {
        kernel_text_puts("    audio: azalia ok\n");
        kernel_debug_puts("audio: using azalia hardware backend\n");
        kernel_text_puts("    audio: topo\n");
    } else if (mk_audio_try_compat_backends() == 0) {
        kernel_text_puts("    audio: compat ok\n");
        kernel_debug_puts("audio: using compat ac97 backend\n");
        kernel_text_puts("    audio: topo\n");
    } else if (kernel_uaudio_present()) {
        kernel_text_puts("    audio: uaudio\n");
        mk_audio_select_uaudio_backend();
        kernel_debug_puts("audio: using usb audio class backend\n");
        kernel_text_puts("    audio: topo\n");
    } else if (kernel_timer_pc_speaker_available()) {
        kernel_text_puts("    audio: pcspkr\n");
        mk_audio_select_pcspkr_backend();
        kernel_debug_puts("audio: using pcspkr fallback backend\n");
        kernel_text_puts("    audio: topo\n");
    } else {
        kernel_text_puts("    audio: forced-soft\n");
        mk_audio_select_soft_backend();
        mk_audio_set_softmix_reason("forced-soft-backend");
        kernel_debug_puts("audio: using forced soft backend to avoid hardware issues\n");
        kernel_text_puts("    audio: topo\n");
    }

    mk_audio_refresh_topology_snapshot();
    mk_audio_debug_backend_state("audio: deferred launch backend=");

    kernel_text_puts("    audio: launch\n");
    if (mk_service_launch_task(MK_SERVICE_AUDIO,
                               "audio",
                               mk_audio_local_handler,
                               0,
                               userland_audio_service_entry,
                               32768u,
                               MK_LAUNCH_FLAG_BOOTSTRAP |
                               MK_LAUNCH_FLAG_BUILTIN) != 0) {
        kernel_text_puts("    audio: launch failed\n");
        return -1;
    }
    g_audio_bootstrap_defer_probe = 0u;
    kernel_text_puts("    audio: done\n");
    return 0;
}

void mk_audio_service_init(void) {
    mk_audio_service_init_state();
    (void)mk_service_register_local_handler(MK_SERVICE_AUDIO,
                                            "audio",
                                            mk_audio_local_handler,
                                            0);
    (void)mk_service_register_deferred_launcher(MK_SERVICE_AUDIO,
                                                mk_audio_service_launch_deferred);
}

int mk_audio_service_ready(void) {
    return mk_service_find_by_type(MK_SERVICE_AUDIO) != 0;
}

int mk_audio_service_get_info(struct mk_audio_info *info) {
    struct mk_message request;
    struct mk_message reply;

    if (info == 0) {
        return -1;
    }
    if (mk_audio_current_prefers_kernel_hot_path()) {
        mk_audio_refresh_topology_snapshot();
        mk_audio_refresh_status_snapshot();
        *info = g_audio_state.info;
        return 0;
    }

    if (mk_audio_prepare_request(&request, MK_MSG_AUDIO_GETINFO, 0, 0u) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_AUDIO, &request, &reply) != 0) {
        return -1;
    }
    if (reply.payload_size != sizeof(*info)) {
        return -1;
    }
    memcpy(info, reply.payload, sizeof(*info));
    return 0;
}

int mk_audio_service_get_status(struct audio_status *status) {
    struct mk_message request;
    struct mk_message reply;

    if (status == 0) {
        return -1;
    }
    if (mk_audio_current_prefers_kernel_hot_path()) {
        mk_audio_refresh_status_snapshot();
        *status = g_audio_state.info.status;
        return 0;
    }

    if (mk_audio_prepare_request(&request, MK_MSG_AUDIO_GET_STATUS, 0, 0u) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_AUDIO, &request, &reply) != 0) {
        return -1;
    }
    if (reply.payload_size != sizeof(*status)) {
        return -1;
    }
    memcpy(status, reply.payload, sizeof(*status));
    return 0;
}

int mk_audio_service_set_params(const struct audio_swpar *params) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_audio_result result;

    if (params == 0) {
        return -1;
    }
    if (mk_audio_current_prefers_kernel_hot_path()) {
        g_audio_state.info.parameters = *params;
        if (g_audio_state.backend_kind == MK_AUDIO_BACKEND_COMPAT_UAUDIO) {
            mk_audio_normalize_uaudio_params(&g_audio_state.info.parameters);
        } else {
            mk_audio_normalize_params(&g_audio_state.info.parameters);
        }
        if (g_audio_state.backend_kind == MK_AUDIO_BACKEND_COMPAT_AUICH) {
            mk_audio_compat_apply_params();
        }
        return 0;
    }

    if (mk_audio_prepare_request(&request, MK_MSG_AUDIO_SET_PARAMS, params, sizeof(*params)) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_AUDIO, &request, &reply) != 0) {
        return -1;
    }
    if (reply.payload_size != sizeof(result)) {
        return -1;
    }
    memcpy(&result, reply.payload, sizeof(result));
    return result.value;
}

int mk_audio_service_start(void) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_audio_result result;

    if (mk_audio_current_prefers_kernel_hot_path()) {
        if (g_audio_backend->start == 0 || g_audio_backend->start() != 0) {
            return -1;
        }
        return 0;
    }

    if (mk_audio_prepare_request(&request, MK_MSG_AUDIO_START, 0, 0u) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_AUDIO, &request, &reply) != 0) {
        return -1;
    }
    if (reply.payload_size != sizeof(result)) {
        return -1;
    }
    memcpy(&result, reply.payload, sizeof(result));
    return result.value;
}

int mk_audio_service_stop(void) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_audio_result result;

    if (mk_audio_current_prefers_kernel_hot_path()) {
        if (g_audio_backend->stop == 0 || g_audio_backend->stop() != 0) {
            return -1;
        }
        return 0;
    }

    if (mk_audio_prepare_request(&request, MK_MSG_AUDIO_STOP, 0, 0u) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_AUDIO, &request, &reply) != 0) {
        return -1;
    }
    if (reply.payload_size != sizeof(result)) {
        return -1;
    }
    memcpy(&result, reply.payload, sizeof(result));
    return result.value;
}

int mk_audio_service_write(const void *data, uint32_t size) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_audio_result result;
    struct mk_audio_write_request chunk;
    const uint8_t *src;
    uint32_t total_written;

    if (data == 0 || size == 0u) {
        return -1;
    }
    if (mk_audio_current_prefers_kernel_hot_path()) {
        if (g_audio_backend == 0 || g_audio_backend->write == 0) {
            return -1;
        }
        return g_audio_backend->write((const uint8_t *)data, size);
    }

    src = (const uint8_t *)data;
    total_written = 0u;
    while (total_written < size) {
        uint32_t chunk_size = size - total_written;

        if (chunk_size > MK_AUDIO_INLINE_WRITE_MAX) {
            chunk_size = MK_AUDIO_INLINE_WRITE_MAX;
        }
        memset(&chunk, 0, sizeof(chunk));
        chunk.size = chunk_size;
        memcpy(chunk.data, src + total_written, chunk_size);
        if (mk_audio_prepare_request(&request, MK_MSG_AUDIO_WRITE, &chunk, sizeof(chunk)) != 0) {
            return total_written != 0u ? (int)total_written : -1;
        }
        if (mk_service_request(MK_SERVICE_AUDIO, &request, &reply) != 0) {
            return total_written != 0u ? (int)total_written : -1;
        }
        if (reply.payload_size != sizeof(result)) {
            return total_written != 0u ? (int)total_written : -1;
        }
        memcpy(&result, reply.payload, sizeof(result));
        if (result.value <= 0) {
            return total_written != 0u ? (int)total_written : result.value;
        }
        total_written += (uint32_t)result.value;
        if (total_written < size) {
            yield();
        }
    }

    return (int)total_written;
}

int mk_audio_service_write_direct(const void *data, uint32_t size) {
    if (data == 0 || size == 0u || g_audio_backend == 0 || g_audio_backend->write == 0) {
        return -1;
    }
    return g_audio_backend->write((const uint8_t *)data, size);
}

int mk_audio_service_write_async(const void *data, uint32_t size) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_audio_result result;
    struct mk_audio_write_request chunk;
    const uint8_t *src;
    uint32_t total_written;

    if (data == 0 || size == 0u) {
        return -1;
    }
    if (mk_audio_current_prefers_kernel_hot_path()) {
        uint32_t written = mk_audio_async_ring_write((const uint8_t *)data, size);

        mk_audio_refresh_status_snapshot();
        return (int)written;
    }

    src = (const uint8_t *)data;
    total_written = 0u;
    while (total_written < size) {
        uint32_t chunk_size = size - total_written;

        if (chunk_size > MK_AUDIO_INLINE_WRITE_MAX) {
            chunk_size = MK_AUDIO_INLINE_WRITE_MAX;
        }
        memset(&chunk, 0, sizeof(chunk));
        chunk.size = chunk_size;
        memcpy(chunk.data, src + total_written, chunk_size);
        if (mk_audio_prepare_request(&request,
                                     MK_MSG_AUDIO_WRITE_ASYNC,
                                     &chunk,
                                     sizeof(chunk)) != 0) {
            return total_written != 0u ? (int)total_written : -1;
        }
        if (mk_service_request(MK_SERVICE_AUDIO, &request, &reply) != 0) {
            return total_written != 0u ? (int)total_written : -1;
        }
        if (reply.payload_size != sizeof(result)) {
            return total_written != 0u ? (int)total_written : -1;
        }
        memcpy(&result, reply.payload, sizeof(result));
        if (result.value <= 0) {
            return total_written != 0u ? (int)total_written : result.value;
        }
        total_written += (uint32_t)result.value;
    }

    return (int)total_written;
}

int mk_audio_service_play_asset(const char *path) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_audio_result result;
    struct mk_audio_play_asset_request payload;

    if (path == 0 || path[0] == '\0') {
        return -1;
    }

    memset(&payload, 0, sizeof(payload));
    mk_audio_copy_limited(payload.path, path, sizeof(payload.path));
    if (payload.path[0] == '\0') {
        return -1;
    }
    if (mk_audio_prepare_request(&request,
                                 MK_MSG_AUDIO_PLAY_ASSET,
                                 &payload,
                                 sizeof(payload)) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_AUDIO, &request, &reply) != 0) {
        return -1;
    }
    if (reply.payload_size != sizeof(result)) {
        return -1;
    }
    memcpy(&result, reply.payload, sizeof(result));
    return result.value;
}

static void mk_audio_service_tick(uint32_t tick) {
    (void)tick;
    mk_audio_service_pump_async();
}

void mk_audio_service_pump_async(void) {
    uint8_t chunk[MK_AUDIO_ASYNC_WRITE_CHUNK];
    uint32_t iterations = 0u;

    if (g_audio_backend == 0 || g_audio_backend->write == 0) {
        return;
    }

    while (g_audio_state.async_write_fill != 0u && iterations < 32u) {
        uint32_t chunk_size = g_audio_state.async_write_fill;
        int written;
        uint32_t chunk_limit = mk_audio_async_pump_chunk_limit();

        if (chunk_limit == 0u || chunk_limit > (uint32_t)sizeof(chunk)) {
            chunk_limit = (uint32_t)sizeof(chunk);
        }
        if (chunk_size > chunk_limit) {
            chunk_size = chunk_limit;
        }
        if (mk_audio_async_ring_peek(chunk, chunk_size) == 0u) {
            goto drain_playback;
        }
        written = g_audio_backend->write(chunk, chunk_size);
        if (written <= 0) {
            mk_audio_refresh_status_snapshot();
            goto drain_playback;
        }
        mk_audio_async_ring_consume((uint32_t)written);
        iterations += 1u;

        if ((uint32_t)written < chunk_size) {
            break;
        }
    }

drain_playback:
    
    if (g_audio_state.playback_fill > 0u) {
        if (g_audio_backend == &g_audio_backend_soft) {
            
            g_audio_state.playback_head = 0u;
            g_audio_state.playback_tail = 0u;
            g_audio_state.playback_bytes_consumed += g_audio_state.playback_fill;
            g_audio_state.playback_fill = 0u;
        }
    }

    mk_audio_refresh_status_snapshot();
}

int mk_audio_service_subscribe(process_t *subscriber) {
    struct mk_audio_event_subscription *subscription;

    if (subscriber == 0) {
        return -1;
    }

    subscription = mk_audio_find_subscription(subscriber);
    if (subscription != 0) {
        return 0;
    }

    subscription = mk_audio_alloc_subscription(subscriber);
    if (subscription == 0) {
        return -1;
    }

    mk_audio_refresh_status_snapshot();
    if (g_audio_state.audio_event_last_active != 0u ||
        g_audio_state.audio_event_last_queued_bytes != 0u) {
        mk_audio_enqueue_event(subscription,
                               MK_AUDIO_EVENT_QUEUED,
                               g_audio_state.audio_event_last_queued_bytes,
                               g_audio_state.audio_event_last_underruns);
    } else {
        mk_audio_enqueue_event(subscription,
                               MK_AUDIO_EVENT_IDLE,
                               0u,
                               g_audio_state.audio_event_last_underruns);
    }
    if (g_audio_state.capture_fill != 0u) {
        mk_audio_enqueue_event(subscription,
                               MK_AUDIO_EVENT_CAPTURE_READY,
                               g_audio_state.capture_fill,
                               g_audio_state.capture_xruns);
    }
    return 0;
}

int mk_audio_service_event_receive(process_t *subscriber,
                                   struct mk_audio_event *event,
                                   uint32_t timeout_ticks) {
    struct mk_audio_event_subscription *subscription;
    uint32_t dropped_events;
    int wait_rc;

    if (subscriber == 0 || event == 0) {
        return -1;
    }

    subscription = mk_audio_find_subscription(subscriber);
    if (subscription == 0) {
        return -1;
    }

    for (;;) {
        if (kernel_mailbox_try_receive(&subscription->mailbox, event) == 0) {
            return 0;
        }
        dropped_events = kernel_mailbox_dropped(&subscription->mailbox);
        if (dropped_events != 0u) {
            memset(event, 0, sizeof(*event));
            event->abi_version = 1u;
            event->event_type = MK_AUDIO_EVENT_OVERFLOW;
            event->backend_kind = g_audio_state.backend_kind;
            event->queued_bytes = g_audio_state.audio_event_last_queued_bytes;
            event->underruns = g_audio_state.audio_event_last_underruns;
            event->dropped_events = dropped_events;
            kernel_mailbox_clear_dropped(&subscription->mailbox);
            event->tick = kernel_timer_get_ticks();
            return 0;
        }
        if (timeout_ticks == 0u) {
            return -1;
        }
        wait_rc = kernel_mailbox_wait(&subscription->mailbox, timeout_ticks);
        if (wait_rc != TASK_WAIT_RESULT_SIGNALED) {
            return -1;
        }
    }
}

int mk_audio_service_read(void *data, uint32_t size) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_audio_transfer_request transfer;

    if (data == 0 || size == 0u || size > MK_AUDIO_INLINE_READ_MAX) {
        return -1;
    }
    if (mk_audio_current_process_is_service_worker()) {
        if (g_audio_state.backend_kind != MK_AUDIO_BACKEND_COMPAT_AUICH) {
            return -1;
        }
        return mk_audio_backend_compat_read((uint8_t *)data, size);
    }

    transfer.size = size;
    transfer.transfer_id = 0u;
    if (mk_audio_prepare_request(&request, MK_MSG_AUDIO_READ, &transfer, sizeof(transfer)) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_AUDIO, &request, &reply) != 0) {
        return -1;
    }
    if (reply.payload_size == sizeof(struct mk_audio_result)) {
        struct mk_audio_result result;

        memcpy(&result, reply.payload, sizeof(result));
        return result.value;
    }
    if (reply.payload_size != sizeof(struct mk_audio_read_reply)) {
        return -1;
    }
    {
        struct mk_audio_read_reply payload;

        memcpy(&payload, reply.payload, sizeof(payload));
        if (payload.size > size || payload.size > MK_AUDIO_INLINE_READ_MAX) {
            return -1;
        }
        memcpy(data, payload.data, payload.size);
        return (int)payload.size;
    }
}

int mk_audio_service_get_control_info(uint32_t index, struct mk_audio_control_info *info) {
    struct mk_message request;
    struct mk_message reply;

    if (info == 0) {
        return -1;
    }
    if (mk_audio_current_process_is_service_worker()) {
        return mk_audio_state_get_control_info(index, info);
    }

    if (mk_audio_prepare_request(&request, MK_MSG_AUDIO_CONTROL_INFO, &index, sizeof(index)) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_AUDIO, &request, &reply) != 0) {
        return -1;
    }
    if (reply.payload_size == sizeof(struct mk_audio_result)) {
        struct mk_audio_result result;

        memcpy(&result, reply.payload, sizeof(result));
        return result.value;
    }
    if (reply.payload_size != sizeof(*info)) {
        return -1;
    }
    memcpy(info, reply.payload, sizeof(*info));
    return 0;
}

int mk_audio_service_mixer_read(mixer_ctrl_t *control) {
    struct mk_message request;
    struct mk_message reply;

    if (control == 0) {
        return -1;
    }
    if (mk_audio_current_process_is_service_worker()) {
        return mk_audio_state_read_control(control);
    }

    if (mk_audio_prepare_request(&request, MK_MSG_AUDIO_MIXER_READ, control, sizeof(*control)) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_AUDIO, &request, &reply) != 0) {
        return -1;
    }
    if (reply.payload_size != sizeof(*control)) {
        return -1;
    }
    memcpy(control, reply.payload, sizeof(*control));
    return 0;
}

int mk_audio_service_mixer_write(const mixer_ctrl_t *control) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_audio_result result;

    if (control == 0) {
        return -1;
    }
    if (mk_audio_current_process_is_service_worker()) {
        return mk_audio_state_write_control(control);
    }

    if (mk_audio_prepare_request(&request, MK_MSG_AUDIO_MIXER_WRITE, control, sizeof(*control)) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_AUDIO, &request, &reply) != 0) {
        return -1;
    }
    if (reply.payload_size != sizeof(result)) {
        return -1;
    }
    memcpy(&result, reply.payload, sizeof(result));
    return result.value;
}

int mk_audio_service_last_request(struct mk_message *message) {
    if (message == 0) {
        return -1;
    }
    if (g_last_audio_request.type == MK_MSG_NONE) {
        return -1;
    }

    *message = g_last_audio_request;
    return 0;
}
