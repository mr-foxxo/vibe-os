#ifndef KERNEL_ACPI_H
#define KERNEL_ACPI_H

#include <stdint.h>

/* ACPI table signatures */
#define RSDP_SIG    "RSD PTR "
#define RSDT_SIG    "RSDT"
#define XSDT_SIG    "XSDT"
#define FADT_SIG    "FACP"
#define MADT_SIG    "APIC"
#define MCFG_SIG    "MCFG"
#define DSDT_SIG    "DSDT"
#define SSDT_SIG    "SSDT"
#define ECDT_SIG    "ECDT"
#define HPET_SIG    "HPET"

/* RSDP v1 - 20 bytes */
struct acpi_rsdp1 {
    char     signature[8];
    uint8_t  checksum;
    char     oemid[6];
    uint8_t  revision;
    uint32_t rsdt;
} __attribute__((packed));

/* RSDP v2+ - extended */
struct acpi_rsdp {
    struct acpi_rsdp1 first;
    uint32_t length;
    uint64_t xsdt;
    uint8_t  ext_checksum;
    uint8_t  reserved[3];
} __attribute__((packed));

/* Standard ACPI table header - 36 bytes */
struct acpi_sdth {
    char     signature[4];
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oemid[6];
    char     oemtableid[8];
    uint32_t oemrevision;
    char     aslcompilerid[4];
    uint32_t aslcompilerrevision;
} __attribute__((packed));

/* Generic Address Structure */
struct acpi_gas {
    uint8_t  address_space_id;
    uint8_t  register_bit_width;
    uint8_t  register_bit_offset;
    uint8_t  access_size;
    uint64_t address;
} __attribute__((packed));

#define GAS_SYSTEM_MEMORY  0
#define GAS_SYSTEM_IOSPACE 1
#define GAS_PCI_CFG_SPACE  2
#define GAS_EMBEDDED       3
#define GAS_SMBUS          4

/* FADT - Fixed ACPI Description Table */
struct acpi_fadt {
    struct acpi_sdth hdr;
    uint32_t firmware_ctl;
    uint32_t dsdt;
    uint8_t  int_model;
    uint8_t  pm_profile;
    uint16_t sci_int;
    uint32_t smi_cmd;
    uint8_t  acpi_enable;
    uint8_t  acpi_disable;
    uint8_t  s4bios_req;
    uint8_t  pstate_cnt;
    uint32_t pm1a_evt_blk;
    uint32_t pm1b_evt_blk;
    uint32_t pm1a_cnt_blk;
    uint32_t pm1b_cnt_blk;
    uint32_t pm2_cnt_blk;
    uint32_t pm_tmr_blk;
    uint32_t gpe0_blk;
    uint32_t gpe1_blk;
    uint8_t  pm1_evt_len;
    uint8_t  pm1_cnt_len;
    uint8_t  pm2_cnt_len;
    uint8_t  pm_tmr_len;
    uint8_t  gpe0_blk_len;
    uint8_t  gpe1_blk_len;
    uint8_t  gpe1_base;
    uint8_t  cst_cnt;
    uint16_t p_lvl2_lat;
    uint16_t p_lvl3_lat;
    uint16_t flush_size;
    uint16_t flush_stride;
    uint8_t  duty_offset;
    uint8_t  duty_width;
    uint8_t  day_alrm;
    uint8_t  mon_alrm;
    uint8_t  century;
    uint16_t iapc_boot_arch;
    uint8_t  reserved1;
    uint32_t flags;
    struct acpi_gas reset_reg;
    uint8_t  reset_value;
    uint8_t  reserved2a;
    uint8_t  reserved2b;
    uint8_t  fadt_minor;
    uint64_t x_firmware_ctl;
    uint64_t x_dsdt;
    struct acpi_gas x_pm1a_evt_blk;
    struct acpi_gas x_pm1b_evt_blk;
    struct acpi_gas x_pm1a_cnt_blk;
    struct acpi_gas x_pm1b_cnt_blk;
    struct acpi_gas x_pm2_cnt_blk;
    struct acpi_gas x_pm_tmr_blk;
    struct acpi_gas x_gpe0_blk;
    struct acpi_gas x_gpe1_blk;
    struct acpi_gas sleep_control_reg;
    struct acpi_gas sleep_status_reg;
} __attribute__((packed));

#define FADT_PM_UNSPEC      0
#define FADT_PM_DESKTOP     1
#define FADT_PM_MOBILE      2
#define FADT_PM_WORKSTATION 3
#define FADT_PM_ENT_SERVER  4
#define FADT_PM_SOHO_SERVER 5
#define FADT_PM_APPLIANCE   6
#define FADT_PM_PERF_SERVER 7

#define FADT_TMR_VAL_EXT     0x00000100
#define FADT_RESET_REG_SUP   0x00000400
#define FADT_HW_REDUCED_ACPI 0x00100000

/* MADT - Multiple APIC Description Table */
struct acpi_madt {
    struct acpi_sdth hdr;
    uint32_t local_apic_address;
    uint32_t flags;
} __attribute__((packed));

#define ACPI_APIC_PCAT_COMPAT 0x00000001

#define ACPI_MADT_TYPE_LAPIC           0
#define ACPI_MADT_TYPE_IOAPIC          1
#define ACPI_MADT_TYPE_INT_OVERRIDE    2
#define ACPI_MADT_TYPE_NMI             3
#define ACPI_MADT_TYPE_LAPIC_NMI       4
#define ACPI_MADT_TYPE_LAPIC_OVERRIDE  5
#define ACPI_MADT_TYPE_IO_SAPIC        6
#define ACPI_MADT_TYPE_LOCAL_SAPIC     7
#define ACPI_MADT_TYPE_PLATFORM_INT    8
#define ACPI_MADT_TYPE_X2APIC          9
#define ACPI_MADT_TYPE_X2APIC_NMI     10
#define ACPI_MADT_TYPE_GICC           11

struct acpi_madt_lapic {
    uint8_t  type;
    uint8_t  length;
    uint8_t  acpi_processor_id;
    uint8_t  apic_id;
    uint32_t flags;
} __attribute__((packed));

#define ACPI_PROC_ENABLE 0x00000001

struct acpi_madt_ioapic {
    uint8_t  type;
    uint8_t  length;
    uint8_t  ioapic_id;
    uint8_t  reserved;
    uint32_t address;
    uint32_t global_int_base;
} __attribute__((packed));

struct acpi_madt_int_override {
    uint8_t  type;
    uint8_t  length;
    uint8_t  bus;
    uint8_t  source;
    uint32_t global_int;
    uint16_t flags;
} __attribute__((packed));

struct acpi_madt_x2apic {
    uint8_t  type;
    uint8_t  length;
    uint8_t  reserved[2];
    uint32_t apic_id;
    uint32_t flags;
    uint32_t acpi_processor_uid;
} __attribute__((packed));

struct acpi_madt_lapic_override {
    uint8_t  type;
    uint8_t  length;
    uint16_t reserved;
    uint64_t lapic_address;
} __attribute__((packed));

/* MCFG - PCI Express Memory Mapped Config Space */
struct acpi_mcfg {
    struct acpi_sdth hdr;
    uint8_t  reserved[8];
} __attribute__((packed));

struct acpi_mcfg_entry {
    uint64_t base_address;
    uint16_t segment;
    uint8_t  min_bus_number;
    uint8_t  max_bus_number;
    uint32_t reserved1;
} __attribute__((packed));

/* ECDT - Embedded Controller Description Table */
struct acpi_ecdt {
    struct acpi_sdth hdr;
    struct acpi_gas ec_control;
    struct acpi_gas ec_data;
    uint32_t uid;
    uint8_t  gpe_bit;
    uint8_t  ec_id[1];
} __attribute__((packed));

/* HPET */
struct acpi_hpet {
    struct acpi_sdth hdr;
    uint32_t event_timer_block_id;
    struct acpi_gas base_address;
    uint8_t  hpet_number;
    uint16_t main_counter_min_clock_tick;
    uint8_t  page_protection;
} __attribute__((packed));

/* ACPI frequency */
#define ACPI_FREQUENCY 3579545

/* PM1 control register bits */
#define ACPI_PM1_SCI_EN          0x0001
#define ACPI_PM1_SLP_TYPX_MASK   0x1c00
#define ACPI_PM1_SLP_TYPX(x)     (((x) & 0x7) << 10)
#define ACPI_PM1_SLP_EN          0x2000

/* EC status bits */
#define EC_STAT_OBF  0x01
#define EC_STAT_IBF  0x02
#define EC_STAT_CMD  0x08
#define EC_STAT_BURST 0x10
#define EC_STAT_SCI_EVT 0x20
#define EC_STAT_SMI_EVT 0x40

/* EC commands */
#define EC_CMD_RD  0x80
#define EC_CMD_WR  0x81
#define EC_CMD_BE  0x82
#define EC_CMD_BD  0x83
#define EC_CMD_QR  0x84

/* Sleep states */
#define ACPI_STATE_S0 0
#define ACPI_STATE_S1 1
#define ACPI_STATE_S2 2
#define ACPI_STATE_S3 3
#define ACPI_STATE_S4 4
#define ACPI_STATE_S5 5

/*
 * Public API
 */

/* Initialize ACPI subsystem - call after memory init, before scheduler */
int  acpi_init(void);

/* Find RSDP in BIOS memory */
const struct acpi_rsdp *acpi_find_rsdp(void);

/* Find an ACPI table by signature */
const struct acpi_sdth *acpi_find_table(const char *signature);

/* Get FADT */
const struct acpi_fadt *acpi_get_fadt(void);

/* ACPI timer */
int  acpi_timer_init(void);
uint32_t acpi_timer_read(void);
void acpi_timer_wait_us(uint32_t us);

/* Power management */
void acpi_power_off(void);
void acpi_reboot(void);
int  acpi_sleep(int state);

/* Embedded controller */
int  acpi_ec_init(void);
int  acpi_ec_read(uint8_t byte, uint8_t *value);
int  acpi_ec_write(uint8_t byte, uint8_t value);

/* PCI IRQ routing - parse _PRT (static data only, no AML) */
int  acpi_pci_irq_routing(void);

/* Battery - report via EC/SMBus */
int  acpi_battery_init(void);
int  acpi_battery_status(void);
int  acpi_battery_percent(void);

/* Thermal zones */
int  acpi_thermal_init(void);
int  acpi_thermal_temperature(int *temp_celsius);

/* MADT parsing - LAPIC enumeration */
int  acpi_madt_parse(void (*lapic_callback)(uint8_t apic_id, uint8_t acpi_proc_id, uint32_t flags),
                     void (*ioapic_callback)(uint8_t ioapic_id, uint32_t address, uint32_t gsi_base),
                     void (*override_callback)(uint8_t bus, uint8_t source, uint32_t gsi, uint16_t flags));

/* MCFG parsing */
int  acpi_mcfg_parse(void (*entry_callback)(uint64_t base, uint16_t seg, uint8_t bus_start, uint8_t bus_end));

#endif /* KERNEL_ACPI_H */
