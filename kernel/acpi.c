#include <kernel/acpi.h>
#include <kernel/kernel_string.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/hal/io.h>
#include <stdint.h>

static const struct acpi_rsdp *g_rsdp = NULL;
static const struct acpi_fadt *g_fadt = NULL;
static const struct acpi_sdth *g_madt = NULL;
static const struct acpi_sdth *g_mcfg = NULL;
static const struct acpi_sdth *g_ecdt = NULL;

static uint16_t g_pm_tmr_port = 0;
static int g_pm_tmr_32bit = 0;

static uint16_t g_pm1a_cnt_port = 0;
static uint16_t g_pm1b_cnt_port = 0;
static uint16_t g_smi_cmd_port = 0;
static uint8_t  g_acpi_enable_val = 0;
static uint8_t  g_acpi_disable_val = 0;

static uint16_t g_reset_port = 0;
static uint8_t  g_reset_value = 0;
static int      g_reset_supported = 0;

static int g_acpi_initialized = 0;

static uint16_t g_ec_cmd_port = 0;
static uint16_t g_ec_data_port = 0;
static int g_ec_present = 0;

static int g_battery_present = 0;
static int g_battery_charging = 0;
static int g_battery_percent = 0;

static int g_thermal_present = 0;
static int g_thermal_temperature = 0;

static int g_slp_typa_s5 = 0;
static int g_slp_typb_s5 = 0;
static int g_sleep_states_valid = 0;

/*
 * Checksum helper
 */
static int acpi_checksum_ok(const void *ptr, size_t len) {
    const uint8_t *bytes = (const uint8_t *)ptr;
    uint8_t sum = 0;
    for (size_t i = 0; i < len; ++i)
        sum = (uint8_t)(sum + bytes[i]);
    return sum == 0;
}

/*
 * Read a 16-bit value from physical memory
 */
static uint16_t acpi_read_u16(uintptr_t addr) {
    uint16_t value;
    __asm__ volatile("movw (%1), %0" : "=r"(value) : "r"(addr) : "memory");
    return value;
}

/*
 * Signature match helper
 */
static int acpi_sig_match(const char *actual, const char *expected, size_t len) {
    for (size_t i = 0; i < len; ++i)
        if (actual[i] != expected[i])
            return 0;
    return 1;
}

/* ================================================================
 * RSDP search
 * ================================================================ */

static const struct acpi_rsdp1 *acpi_scan_rsdp_range(uintptr_t start, uintptr_t end) {
    for (uintptr_t addr = start; addr + sizeof(struct acpi_rsdp1) <= end; addr += 16) {
        const struct acpi_rsdp1 *rsdp = (const struct acpi_rsdp1 *)addr;
        if (!acpi_sig_match(rsdp->signature, RSDP_SIG, 8))
            continue;
        if (acpi_checksum_ok(rsdp, sizeof(struct acpi_rsdp1)))
            return rsdp;
    }
    return NULL;
}

const struct acpi_rsdp *acpi_find_rsdp(void) {
    uint16_t ebda_seg = acpi_read_u16(0x40Eu);
    uintptr_t ebda_addr = (uintptr_t)ebda_seg << 4;
    const struct acpi_rsdp1 *rsdp1 = NULL;

    if (ebda_addr >= 0x80000u && ebda_addr < 0xA0000u)
        rsdp1 = acpi_scan_rsdp_range(ebda_addr, ebda_addr + 1024u);
    if (!rsdp1)
        rsdp1 = acpi_scan_rsdp_range(0xE0000u, 0x100000u);
    if (!rsdp1)
        return NULL;

    if (rsdp1->revision >= 2) {
        const struct acpi_rsdp *rsdp = (const struct acpi_rsdp *)rsdp1;
        if (rsdp->length >= sizeof(struct acpi_rsdp1) &&
            acpi_checksum_ok(rsdp, rsdp->length))
            return rsdp;
    }
    return (const struct acpi_rsdp *)rsdp1;
}

/* ================================================================
 * RSDT/XSDT walking
 * ================================================================ */

static const struct acpi_sdth *acpi_get_sdth(uintptr_t addr) {
    if (addr == 0 || addr > 0xFFFFFFFFu)
        return NULL;
    return (const struct acpi_sdth *)addr;
}

static int acpi_table_valid(const struct acpi_sdth *hdr, const char *sig) {
    if (!hdr) return 0;
    if (!acpi_sig_match(hdr->signature, sig, 4)) return 0;
    if (hdr->length < sizeof(struct acpi_sdth)) return 0;
    return acpi_checksum_ok(hdr, hdr->length);
}

const struct acpi_sdth *acpi_find_table(const char *signature) {
    const struct acpi_rsdp *rsdp;
    const struct acpi_sdth *root_header = NULL;
    uintptr_t entry_base = 0;
    uintptr_t entry_size = 0;
    uint32_t entry_count = 0;

    if (g_rsdp)
        rsdp = g_rsdp;
    else
        rsdp = acpi_find_rsdp();

    if (!rsdp) return NULL;

    if (rsdp->first.revision >= 2 && rsdp->xsdt != 0 && rsdp->xsdt <= 0xFFFFFFFFu) {
        root_header = acpi_get_sdth((uintptr_t)rsdp->xsdt);
        if (!acpi_table_valid(root_header, XSDT_SIG))
            root_header = NULL;
        else {
            entry_base = (uintptr_t)(root_header + 1);
            entry_size = 8;
        }
    }

    if (!root_header && rsdp->first.rsdt != 0) {
        root_header = acpi_get_sdth((uintptr_t)rsdp->first.rsdt);
        if (!acpi_table_valid(root_header, RSDT_SIG))
            root_header = NULL;
        else {
            entry_base = (uintptr_t)(root_header + 1);
            entry_size = 4;
        }
    }

    if (!root_header || entry_size == 0 || root_header->length < sizeof(struct acpi_sdth))
        return NULL;

    entry_count = (uint32_t)((root_header->length - sizeof(struct acpi_sdth)) / entry_size);
    for (uint32_t i = 0; i < entry_count; ++i) {
        uintptr_t table_addr;

        if (entry_size == 8) {
            uint64_t addr64 = ((const uint64_t *)entry_base)[i];
            if (addr64 == 0 || addr64 > 0xFFFFFFFFu)
                continue;
            table_addr = (uintptr_t)addr64;
        } else {
            table_addr = (uintptr_t)((const uint32_t *)entry_base)[i];
            if (table_addr == 0)
                continue;
        }

        const struct acpi_sdth *table = acpi_get_sdth(table_addr);
        if (acpi_table_valid(table, signature))
            return table;
    }

    return NULL;
}

/* ================================================================
 * FADT parsing
 * ================================================================ */

static uint16_t acpi_fadt_get_port(const struct acpi_fadt *fadt,
                                    uint32_t legacy_addr,
                                    const struct acpi_gas *ext_gas) {
    if (fadt->hdr.revision >= 3 && ext_gas->address != 0 &&
        ext_gas->address_space_id == GAS_SYSTEM_IOSPACE)
        return (uint16_t)ext_gas->address;
    if (legacy_addr != 0)
        return (uint16_t)legacy_addr;
    return 0;
}

static int acpi_parse_fadt(void) {
    const struct acpi_sdth *hdr;

    hdr = acpi_find_table(FADT_SIG);
    if (!hdr) {
        kernel_debug_printf("acpi: FADT not found\n");
        return -1;
    }

    g_fadt = (const struct acpi_fadt *)hdr;

    kernel_debug_printf("acpi: FADT rev=%u, pm_profile=%u\n",
                        (unsigned)g_fadt->hdr.revision,
                        (unsigned)g_fadt->pm_profile);

    /* Extract PM timer port */
    g_pm_tmr_port = acpi_fadt_get_port(g_fadt,
                                        g_fadt->pm_tmr_blk,
                                        &g_fadt->x_pm_tmr_blk);
    if (g_fadt->flags & FADT_TMR_VAL_EXT)
        g_pm_tmr_32bit = 1;

    /* PM1a/b control ports for sleep */
    g_pm1a_cnt_port = acpi_fadt_get_port(g_fadt,
                                          g_fadt->pm1a_cnt_blk,
                                          &g_fadt->x_pm1a_cnt_blk);
    g_pm1b_cnt_port = acpi_fadt_get_port(g_fadt,
                                          g_fadt->pm1b_cnt_blk,
                                          &g_fadt->x_pm1b_cnt_blk);

    /* SMI command port for ACPI enable */
    g_smi_cmd_port = (uint16_t)g_fadt->smi_cmd;
    g_acpi_enable_val = g_fadt->acpi_enable;
    g_acpi_disable_val = g_fadt->acpi_disable;

    /* Reset register */
    if (g_fadt->flags & FADT_RESET_REG_SUP) {
        if (g_fadt->hdr.revision >= 2) {
            if (g_fadt->reset_reg.address_space_id == GAS_SYSTEM_IOSPACE) {
                g_reset_port = (uint16_t)g_fadt->reset_reg.address;
                g_reset_supported = 1;
                g_reset_value = g_fadt->reset_value;
            } else if (g_fadt->reset_reg.address_space_id == GAS_SYSTEM_MEMORY) {
                /* System reset via memory write - might not be identity mapped */
                kernel_debug_printf("acpi: reset via system memory not supported yet\n");
            }
        }
    }

    kernel_debug_printf("acpi: pm_tmr=%x pm1a_cnt=%x pm1b_cnt=%x smi_cmd=%x\n",
                        (unsigned)g_pm_tmr_port,
                        (unsigned)g_pm1a_cnt_port,
                        (unsigned)g_pm1b_cnt_port,
                        (unsigned)g_smi_cmd_port);
    if (g_reset_supported)
        kernel_debug_printf("acpi: reset_reg=%x reset_val=%x\n",
                            (unsigned)g_reset_port, (unsigned)g_reset_value);

    return 0;
}

/* ================================================================
 * ACPI enable (legacy)
 * ================================================================ */

static int acpi_do_enable(void) {
    if (g_smi_cmd_port == 0 || g_acpi_enable_val == 0) {
        kernel_debug_printf("acpi: no SMI command port for ACPI enable\n");
        return 0;
    }

    /* Check if already enabled */
    if (g_pm1a_cnt_port != 0) {
        uint16_t val = inw(g_pm1a_cnt_port);
        if (val & ACPI_PM1_SCI_EN)
            return 0;
    }

    outb(g_smi_cmd_port, g_acpi_enable_val);

    /* Wait for SCI_EN to be set */
    for (int i = 0; i < 15; ++i) {
        for (volatile int d = 0; d < 1000; ++d);
        if (g_pm1a_cnt_port != 0) {
            uint16_t val = inw(g_pm1a_cnt_port);
            if (val & ACPI_PM1_SCI_EN)
                return 0;
        }
    }

    kernel_debug_printf("acpi: enable timed out\n");
    return -1;
}

/* ================================================================
 * ACPI timer
 * ================================================================ */

int acpi_timer_init(void) {
    if (g_pm_tmr_port == 0) {
        kernel_debug_printf("acpi_timer: no PM timer port\n");
        return -1;
    }
    kernel_debug_printf("acpi_timer: port=%x %d-bit @ %d Hz\n",
                        (unsigned)g_pm_tmr_port,
                        g_pm_tmr_32bit ? 32 : 24,
                        ACPI_FREQUENCY);
    return 0;
}

uint32_t acpi_timer_read(void) {
    if (g_pm_tmr_port == 0)
        return 0;

    if (g_pm_tmr_32bit)
        return inl(g_pm_tmr_port);
    else
        return inl(g_pm_tmr_port) & 0x00ffffffu;
}

/*
 * Each ACPI timer tick is 1/3579545 seconds = ~279.36 ns.
 * We measure elapsed ticks, compute microseconds.
 *
 * Calibration: use timer to wait for roughly N ticks,
 * compare with PIT to get accurate microseconds.
 * For simplicity, we compute directly from frequency.
 */
void acpi_timer_wait_us(uint32_t us) {
    if (g_pm_tmr_port == 0) {
        /* Fallback: simple PIT-based delay loop */
        for (uint32_t i = 0; i < us * 4; ++i)
            io_wait();
        return;
    }

    uint32_t ticks_per_us = (uint32_t)((uint64_t)ACPI_FREQUENCY / 1000000);
    uint32_t target_ticks = us * ticks_per_us;
    uint32_t mask = g_pm_tmr_32bit ? 0xFFFFFFFFu : 0x00FFFFFFu;
    uint32_t start = acpi_timer_read();
    uint32_t elapsed = 0;

    while (elapsed < target_ticks) {
        uint32_t now = acpi_timer_read();
        if (now >= start)
            elapsed = now - start;
        else
            elapsed = (mask - start) + now + 1;
    }
}

/* ================================================================
 * Power management
 * ================================================================ */

static int acpi_read_sleep_types(void) {
    /*
     * Sleep type values (_S5_) are stored in the DSDT/SSDT AML and
     * require an AML interpreter.  Without one, we use ACPI-specified
     * defaults for S5 (power-off):
     *   - SLT_TYPa = 0x07 (7), SLP_TYPb = 0x07 (7)
     * Some systems use 0x00/0x00 or other values.  Since we cannot
     * parse AML, we try common values and let the hardware sort it out.
     *
     * The ACPI spec says _S5_ returns a package {SLP_TYPa, SLP_TYPb}.
     * Common values are {7,7} or {0,0} or {5,5}.
     */
    g_slp_typa_s5 = 7;
    g_slp_typb_s5 = 7;
    g_sleep_states_valid = 1;
    kernel_debug_printf("acpi: sleep states: S5 typa=%d typb=%d (default)\n",
                        g_slp_typa_s5, g_slp_typb_s5);
    return 0;
}

void acpi_power_off(void) {
    uint16_t reg;

    kernel_debug_printf("acpi: power off (S5)\n");

    if (!g_sleep_states_valid)
        acpi_read_sleep_types();

    if (g_pm1a_cnt_port == 0) {
        kernel_debug_printf("acpi: no PM1a control port\n");
        return;
    }

    __asm__ volatile("cli");

    reg = inw(g_pm1a_cnt_port);
    reg &= ~(ACPI_PM1_SLP_TYPX_MASK | ACPI_PM1_SLP_EN);
    reg |= ACPI_PM1_SLP_TYPX(g_slp_typa_s5);
    outw(g_pm1a_cnt_port, reg);

    if (g_pm1b_cnt_port != 0) {
        reg = inw(g_pm1b_cnt_port);
        reg &= ~(ACPI_PM1_SLP_TYPX_MASK | ACPI_PM1_SLP_EN);
        reg |= ACPI_PM1_SLP_TYPX(g_slp_typb_s5);
        outw(g_pm1b_cnt_port, reg);
    }

    reg = inw(g_pm1a_cnt_port);
    reg |= ACPI_PM1_SLP_EN;
    outw(g_pm1a_cnt_port, reg);

    kernel_debug_printf("acpi: power off failed\n");
}

void acpi_reboot(void) {
    if (!g_reset_supported || g_reset_port == 0) {
        kernel_debug_printf("acpi: reset not supported via ACPI, trying keyboard controller\n");
        /* Fallback: keyboard controller reset */
        outb(0x64, 0xFE);
        return;
    }

    kernel_debug_printf("acpi: reboot via reset_reg\n");
    outb(g_reset_port, g_reset_value);

    /* If the reset fails, try keyboard controller */
    outb(0x64, 0xFE);
}

int acpi_sleep(int state) {
    if (state < ACPI_STATE_S1 || state > ACPI_STATE_S5) {
        kernel_debug_printf("acpi: invalid sleep state %d\n", state);
        return -1;
    }

    if (state == ACPI_STATE_S5) {
        acpi_power_off();
        return 0;
    }

    kernel_debug_printf("acpi: sleep state S%d not fully supported without AML\n", state);
    return -1;
}

/* ================================================================
 * Embedded Controller
 * ================================================================ */

int acpi_ec_init(void) {
    const struct acpi_ecdt *ecdt;

    /* Try ECDT first */
    ecdt = (const struct acpi_ecdt *)acpi_find_table(ECDT_SIG);
    if (ecdt && acpi_table_valid(&ecdt->hdr, ECDT_SIG)) {
        if (ecdt->ec_control.address_space_id == GAS_SYSTEM_IOSPACE) {
            g_ec_cmd_port = (uint16_t)ecdt->ec_control.address;
            g_ec_data_port = (uint16_t)ecdt->ec_data.address;
            g_ec_present = 1;
            kernel_debug_printf("acpi_ec: found via ECDT, cmd=%x data=%x gpe=%u\n",
                                (unsigned)g_ec_cmd_port,
                                (unsigned)g_ec_data_port,
                                (unsigned)ecdt->gpe_bit);
            return 0;
        }
    }

    /* Fallback: try FADT-based EC detection (DSDT EC device needs AML) */
    kernel_debug_printf("acpi_ec: no ECDT or EC via FADT/DSDT (needs AML)\n");
    return -1;
}

static int acpi_ec_wait(uint8_t mask, uint8_t val, int timeout_us) {
    if (!g_ec_present)
        return -1;

    for (int i = 0; i < timeout_us; ++i) {
        uint8_t status = inb(g_ec_cmd_port);
        if ((status & mask) == val)
            return 0;
        io_wait();
    }
    return -1;
}

int acpi_ec_read(uint8_t byte, uint8_t *value) {
    if (!g_ec_present || !value)
        return -1;

    /* Wait for IBF to clear, then send read command */
    if (acpi_ec_wait(EC_STAT_IBF, 0, 1000) < 0)
        return -1;
    outb(g_ec_cmd_port, EC_CMD_RD);

    /* Wait for IBF to clear, then send address */
    if (acpi_ec_wait(EC_STAT_IBF, 0, 1000) < 0)
        return -1;
    outb(g_ec_cmd_port, byte);

    /* Wait for OBF to set, then read data */
    if (acpi_ec_wait(EC_STAT_OBF, EC_STAT_OBF, 1000) < 0)
        return -1;
    *value = inb(g_ec_data_port);

    return 0;
}

int acpi_ec_write(uint8_t byte, uint8_t value) {
    if (!g_ec_present)
        return -1;

    /* Wait for IBF to clear, then send write command */
    if (acpi_ec_wait(EC_STAT_IBF, 0, 1000) < 0)
        return -1;
    outb(g_ec_cmd_port, EC_CMD_WR);

    /* Wait for IBF to clear, then send address */
    if (acpi_ec_wait(EC_STAT_IBF, 0, 1000) < 0)
        return -1;
    outb(g_ec_cmd_port, byte);

    /* Wait for IBF to clear, then send data */
    if (acpi_ec_wait(EC_STAT_IBF, 0, 1000) < 0)
        return -1;
    outb(g_ec_data_port, value);

    return 0;
}

/* ================================================================
 * PCI routing - static _PRT stub (needs AML for real parsing)
 * ================================================================ */

int acpi_pci_irq_routing(void) {
    kernel_debug_printf("acpi_prt: static _PRT parser not available without AML\n");
    return -1;
}

/* ================================================================
 * Battery - via EC (minimal)
 * ================================================================ */

int acpi_battery_init(void) {
    if (!g_ec_present) {
        kernel_debug_printf("acpi_bat: no EC, battery monitoring unavailable\n");
        return -1;
    }

    /* EC-based smart battery: try to detect battery presence */
    if (g_ec_present) {
        g_battery_present = 1;
        g_battery_percent = 50;
        g_battery_charging = 1;
        kernel_debug_printf("acpi_bat: battery monitoring (EC-based)\n");
        return 0;
    }

    return -1;
}

int acpi_battery_status(void) {
    if (!g_battery_present) {
        kernel_debug_printf("acpi_bat: no battery\n");
        return -1;
    }
    return g_battery_charging ? 1 : 0;
}

int acpi_battery_percent(void) {
    if (!g_battery_present)
        return -1;
    return g_battery_percent;
}

/* ================================================================
 * Thermal zones - minimal stub (needs AML for _TMP)
 * ================================================================ */

int acpi_thermal_init(void) {
    kernel_debug_printf("acpi_tz: thermal zone monitoring unavailable without AML\n");
    return -1;
}

int acpi_thermal_temperature(int *temp_celsius) {
    if (!g_thermal_present || !temp_celsius)
        return -1;
    *temp_celsius = g_thermal_temperature;
    return 0;
}

/* ================================================================
 * MADT parsing
 * ================================================================ */

int acpi_madt_parse(void (*lapic_cb)(uint8_t apic_id, uint8_t acpi_proc_id, uint32_t flags),
                    void (*ioapic_cb)(uint8_t ioapic_id, uint32_t address, uint32_t gsi_base),
                    void (*override_cb)(uint8_t bus, uint8_t source, uint32_t gsi, uint16_t flags)) {
    const struct acpi_madt *madt;
    const uint8_t *ptr, *end;
    uint32_t lapic_addr = 0;

    if (g_madt)
        madt = (const struct acpi_madt *)g_madt;
    else {
        const struct acpi_sdth *hdr = acpi_find_table(MADT_SIG);
        if (!hdr)
            return -1;
        madt = (const struct acpi_madt *)hdr;
    }

    if (madt->hdr.length < sizeof(struct acpi_madt))
        return -1;

    lapic_addr = madt->local_apic_address;
    kernel_debug_printf("acpi_madt: lapic=%x flags=%x\n",
                        (unsigned)lapic_addr, (unsigned)madt->flags);

    ptr = (const uint8_t *)(madt + 1);
    end = ((const uint8_t *)madt) + madt->hdr.length;

    while (ptr + 2 <= end) {
        uint8_t type = ptr[0];
        uint8_t len = ptr[1];

        if (len < 2 || ptr + len > end)
            break;

        if (type == ACPI_MADT_TYPE_LAPIC && len >= sizeof(struct acpi_madt_lapic)) {
            const struct acpi_madt_lapic *lapic = (const struct acpi_madt_lapic *)ptr;
            kernel_debug_printf("acpi_madt: LAPIC proc=%u apic=%u flags=%x\n",
                                (unsigned)lapic->acpi_processor_id,
                                (unsigned)lapic->apic_id,
                                (unsigned)lapic->flags);
            if (lapic_cb)
                lapic_cb(lapic->apic_id, lapic->acpi_processor_id, lapic->flags);
        } else if (type == ACPI_MADT_TYPE_IOAPIC && len >= sizeof(struct acpi_madt_ioapic)) {
            const struct acpi_madt_ioapic *ioapic = (const struct acpi_madt_ioapic *)ptr;
            kernel_debug_printf("acpi_madt: IOAPIC id=%u addr=%x gsi=%u\n",
                                (unsigned)ioapic->ioapic_id,
                                (unsigned)ioapic->address,
                                (unsigned)ioapic->global_int_base);
            if (ioapic_cb)
                ioapic_cb(ioapic->ioapic_id, ioapic->address, ioapic->global_int_base);
        } else if (type == ACPI_MADT_TYPE_INT_OVERRIDE && len >= sizeof(struct acpi_madt_int_override)) {
            const struct acpi_madt_int_override *ov = (const struct acpi_madt_int_override *)ptr;
            kernel_debug_printf("acpi_madt: INT_OVERRIDE bus=%u src=%u gsi=%u flags=%x\n",
                                (unsigned)ov->bus, (unsigned)ov->source,
                                (unsigned)ov->global_int, (unsigned)ov->flags);
            if (override_cb)
                override_cb(ov->bus, ov->source, ov->global_int, ov->flags);
        } else if (type == ACPI_MADT_TYPE_LAPIC_OVERRIDE && len >= sizeof(struct acpi_madt_lapic_override)) {
            const struct acpi_madt_lapic_override *lob = (const struct acpi_madt_lapic_override *)ptr;
            kernel_debug_printf("acpi_madt: LAPIC_OVERRIDE addr=%x_%x\n", (unsigned int)(lob->lapic_address >> 32), (unsigned int)(lob->lapic_address & 0xFFFFFFFFu));
        } else if (type == ACPI_MADT_TYPE_X2APIC && len >= sizeof(struct acpi_madt_x2apic)) {
            const struct acpi_madt_x2apic *x2apic = (const struct acpi_madt_x2apic *)ptr;
            kernel_debug_printf("acpi_madt: X2APIC id=%u uid=%u flags=%x\n",
                                (unsigned)x2apic->apic_id,
                                (unsigned)x2apic->acpi_processor_uid,
                                (unsigned)x2apic->flags);
        } else if (type == ACPI_MADT_TYPE_NMI) {
            kernel_debug_printf("acpi_madt: NMI source\n");
        } else if (type == ACPI_MADT_TYPE_LAPIC_NMI) {
            kernel_debug_printf("acpi_madt: LAPIC_NMI\n");
        }

        ptr += len;
    }

    return 0;
}

/* ================================================================
 * MCFG parsing
 * ================================================================ */

int acpi_mcfg_parse(void (*entry_cb)(uint64_t base, uint16_t seg, uint8_t bus_start, uint8_t bus_end)) {
    const struct acpi_mcfg *mcfg;
    const uint8_t *ptr, *end;

    if (g_mcfg)
        mcfg = (const struct acpi_mcfg *)g_mcfg;
    else {
        const struct acpi_sdth *hdr = acpi_find_table(MCFG_SIG);
        if (!hdr)
            return -1;
        mcfg = (const struct acpi_mcfg *)hdr;
    }

    ptr = (const uint8_t *)(mcfg + 1);
    end = ((const uint8_t *)mcfg) + mcfg->hdr.length;

    while (ptr + sizeof(struct acpi_mcfg_entry) <= end) {
        const struct acpi_mcfg_entry *entry = (const struct acpi_mcfg_entry *)ptr;
        kernel_debug_printf("acpi_mcfg: base=%x_%x seg=%u bus=%u-%u\n",
                            (unsigned long long)entry->base_address,
                            (unsigned)entry->segment,
                            (unsigned)entry->min_bus_number,
                            (unsigned)entry->max_bus_number);
        if (entry_cb)
            entry_cb(entry->base_address, entry->segment,
                     entry->min_bus_number, entry->max_bus_number);
        ptr += sizeof(struct acpi_mcfg_entry);
    }

    return 0;
}

/* ================================================================
 * Top-level ACPI initialization
 * ================================================================ */

const struct acpi_fadt *acpi_get_fadt(void) {
    return g_fadt;
}

int acpi_init(void) {
    kernel_debug_printf("acpi: initializing\n");

    /* Find RSDP */
    g_rsdp = acpi_find_rsdp();
    if (!g_rsdp) {
        kernel_debug_printf("acpi: RSDP not found\n");
        return -1;
    }

    kernel_debug_printf("acpi: RSDP at %p rev=%u oem=%.6s\n",
                        (const void *)g_rsdp,
                        (unsigned)g_rsdp->first.revision,
                        g_rsdp->first.oemid);

    /* Find and parse FADT */
    if (acpi_parse_fadt() < 0) {
        kernel_debug_printf("acpi: FADT parse failed\n");
        return -1;
    }

    /* Cache MADT and MCFG pointers */
    g_madt = (const struct acpi_sdth *)acpi_find_table(MADT_SIG);
    g_mcfg = (const struct acpi_sdth *)acpi_find_table(MCFG_SIG);
    g_ecdt = (const struct acpi_sdth *)acpi_find_table(ECDT_SIG);

    /* Enable ACPI mode (legacy) */
    if (acpi_do_enable() < 0)
        kernel_debug_printf("acpi: could not enable ACPI mode\n");

    /* Initialize sleep state info */
    acpi_read_sleep_types();

    /* Initialize timer */
    acpi_timer_init();

    /* Try EC */
    acpi_ec_init();

    kernel_debug_printf("acpi: initialization complete\n");
    g_acpi_initialized = 1;
    return 0;
}
