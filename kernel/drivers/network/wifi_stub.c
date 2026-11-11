#include <kernel/driver_manager.h>
#include <kernel/drivers/network/wifi_stub.h>
#include <kernel/kernel.h>
#include <kernel/kernel_string.h>

static int g_registered = 0;
static int g_present    = 0;

static const struct wifi_stub_ap g_fake_aps[] = {
    { "VibeOS-Net",    { 0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x01u }, -45,  6u, 1u },
    { "OpenAir",       { 0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x02u }, -72, 11u, 0u },
    { "HomeHub",       { 0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x03u }, -60,  1u, 1u },
    { "Campus-Net",    { 0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x04u }, -50,  6u, 1u },
    { "Guest-WiFi",    { 0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x05u }, -80, 11u, 0u },
    { "Lab-5GHz",      { 0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x06u }, -55, 36u, 1u },
};

#define WIFI_STUB_FAKE_AP_COUNT (sizeof(g_fake_aps) / sizeof(g_fake_aps[0]))

static uint32_t g_scan_count = 0u;
static uint32_t g_scan_ticks = 0u;
static int g_scan_in_progress = 0;

static void wifi_stub_probe(void) {
    g_present = 1;
    kernel_debug_puts("network: wi-fi stub device detected\n");
}

void kernel_wifi_stub_init(void) {
    if (!g_registered) {
        register_driver("wifi-stub", "network", kernel_wifi_stub_init);
        g_registered = 1;
        return;
    }
    wifi_stub_probe();
}

int kernel_wifi_stub_present(void) {
    return g_present;
}

void kernel_wifi_stub_scan(void) {
    if (!g_present) {
        g_scan_count = 0u;
        return;
    }
    g_scan_in_progress = 1;
    g_scan_ticks = 3u;
    kernel_debug_puts("network: wi-fi stub scan started\n");
}

int kernel_wifi_stub_scan_tick(void) {
    if (!g_scan_in_progress) return 0;
    if (g_scan_ticks > 0u) {
        --g_scan_ticks;
        return 0;
    }
    g_scan_count = (uint32_t)WIFI_STUB_FAKE_AP_COUNT;
    g_scan_in_progress = 0;
    kernel_debug_puts("network: wi-fi stub scan complete\n");
    return 1;
}

uint32_t kernel_wifi_stub_scan_count(void) {
    return g_scan_count;
}

int kernel_wifi_stub_scan_result(uint32_t index, struct wifi_stub_ap *out) {
    struct wifi_stub_ap ap;
    int variation;

    if (out == 0 || index >= g_scan_count) return -1;

    ap = g_fake_aps[index];
    variation = (int)((uint32_t)(index * 7u) % 5u) - 2;
    ap.rssi = (int8_t)((int)ap.rssi + variation);
    if (ap.rssi > -20) ap.rssi = -20;
    if (ap.rssi < -100) ap.rssi = -100;

    memcpy(out, &ap, sizeof(*out));
    return 0;
}

int kernel_wifi_stub_scan_busy(void) {
    return g_scan_in_progress;
}
