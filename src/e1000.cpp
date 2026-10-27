#include "e1000.h"
#include "pci.h"
#include "io.h"
#include "string.h"
#include "vga.h"

/* e1000 register offsets */
#define REG_CTRL     0x0000
#define REG_STATUS   0x0008
#define REG_EECD     0x0010
#define REG_EERD     0x0014
#define REG_ICR      0x00C0
#define REG_IMS      0x00D0
#define REG_IMC      0x00D8
#define REG_RCTL     0x0100
#define REG_TCTL     0x0400
#define REG_TIPG     0x0410
#define REG_RDBAL    0x2800
#define REG_RDBAH    0x2804
#define REG_RDLEN    0x2808
#define REG_RDH      0x2810
#define REG_RDT      0x2818
#define REG_TDBAL    0x3800
#define REG_TDBAH    0x3804
#define REG_TDLEN    0x3808
#define REG_TDH      0x3810
#define REG_TDT      0x3818
#define REG_MTA      0x5200
#define REG_RAL      0x5400
#define REG_RAH      0x5404

#define RCTL_EN      (1 << 1)
#define RCTL_SBP     (1 << 2)
#define RCTL_UPE     (1 << 3)
#define RCTL_MPE     (1 << 4)
#define RCTL_LPE     (1 << 5)
#define RCTL_LBM_NONE (0 << 6)
#define RCTL_BAM     (1 << 15)
#define RCTL_BSIZE_2048 (0 << 16)
#define RCTL_SECRC   (1 << 26)

#define TCTL_EN      (1 << 1)
#define TCTL_PSP     (1 << 3)
#define TCTL_CT_SHIFT 4
#define TCTL_COLD_SHIFT 12

#define CMD_EOP      (1 << 0)
#define CMD_IFCS     (1 << 1)
#define CMD_RS       (1 << 3)

#define TDESC_STA_DD (1 << 0)
#define RDESC_STA_DD (1 << 0)
#define RDESC_STA_EOP (1 << 1)

struct e1000_rx_desc {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t special;
} __attribute__((packed));

struct e1000_tx_desc {
    uint64_t addr;
    uint16_t length;
    uint8_t  cso;
    uint8_t  cmd;
    uint8_t  status;
    uint8_t  css;
    uint16_t special;
} __attribute__((packed));

#define NUM_RX_DESC 16
#define NUM_TX_DESC 16
#define RX_BUF_SIZE 2048

/* DMA-safe buffers in known physical memory (identity mapped in our simple setup) */
static e1000_rx_desc rx_descs[NUM_RX_DESC] __attribute__((aligned(16)));
static e1000_tx_desc tx_descs[NUM_TX_DESC] __attribute__((aligned(16)));
static uint8_t rx_bufs[NUM_RX_DESC][RX_BUF_SIZE] __attribute__((aligned(16)));
static uint8_t tx_buf[RX_BUF_SIZE] __attribute__((aligned(16)));

static volatile uint32_t* mmio = nullptr;
static uint8_t mac_addr[6];
static uint32_t rx_cur = 0;
static uint32_t tx_cur = 0;
static bool initialized = false;

static inline void wr32(uint32_t reg, uint32_t val) {
    mmio[reg / 4] = val;
}
static inline uint32_t rd32(uint32_t reg) {
    return mmio[reg / 4];
}

static void eeprom_read(uint8_t addr, uint16_t* data) {
    wr32(REG_EERD, (1) | ((uint32_t)addr << 8));
    for (int i = 0; i < 100000; i++) {
        uint32_t v = rd32(REG_EERD);
        if (v & (1 << 4)) {
            *data = (v >> 16) & 0xFFFF;
            return;
        }
    }
    *data = 0;
}

bool e1000_init(void) {
    uint8_t bus, slot, func;
    /* common Intel NICs emulated by QEMU/VBox */
    static const uint16_t ids[] = { 0x100E, 0x100F, 0x10D3, 0x109A, 0x1209, 0 };
    bool found = false;
    for (int i = 0; ids[i]; i++) {
        if (pci_find_device(E1000_VENDOR, ids[i], &bus, &slot, &func)) { found = true; break; }
    }
    if (!found) {
        return false;
    }

    uint32_t bar0 = pci_get_bar(bus, slot, func, 0);
    if (bar0 & 1) {
        vga_print_color("[E1000] Unexpected I/O BAR\n", 0x0C, 0x00);
        return false;
    }
    uint32_t mmio_addr = bar0 & 0xFFFFFFF0;
    mmio = (volatile uint32_t*)(uintptr_t)mmio_addr;

    /* Enable bus mastering */
    uint32_t cmd = pci_read_config(bus, slot, func, 0x04);
    cmd |= (1 << 2) | (1 << 0); /* BM + IO */
    pci_write_config(bus, slot, func, 0x04, cmd);

    /* Reset */
    wr32(REG_CTRL, rd32(REG_CTRL) | (1 << 26));
    for (volatile int i = 0; i < 100000; i++);

    /* Read MAC from EEPROM */
    uint16_t word;
    eeprom_read(0, &word);
    mac_addr[0] = word & 0xFF;
    mac_addr[1] = (word >> 8) & 0xFF;
    eeprom_read(1, &word);
    mac_addr[2] = word & 0xFF;
    mac_addr[3] = (word >> 8) & 0xFF;
    eeprom_read(2, &word);
    mac_addr[4] = word & 0xFF;
    mac_addr[5] = (word >> 8) & 0xFF;

    /* If EEPROM failed, use QEMU default-ish */
    if (mac_addr[0] == 0 && mac_addr[1] == 0) {
        mac_addr[0] = 0x52;
        mac_addr[1] = 0x54;
        mac_addr[2] = 0x00;
        mac_addr[3] = 0x12;
        mac_addr[4] = 0x34;
        mac_addr[5] = 0x56;
    }

    /* Clear MTA (multicast table) */
    for (int i = 0; i < 128; i++)
        wr32(REG_MTA + i * 4, 0);

    /* Setup RX descriptors */
    memset(rx_descs, 0, sizeof(rx_descs));
    for (int i = 0; i < NUM_RX_DESC; i++) {
        rx_descs[i].addr = (uint64_t)(uintptr_t)&rx_bufs[i][0];
        rx_descs[i].status = 0;
    }

    wr32(REG_RDBAL, (uint32_t)(uintptr_t)rx_descs);
    wr32(REG_RDBAH, 0);
    wr32(REG_RDLEN, NUM_RX_DESC * sizeof(e1000_rx_desc));
    wr32(REG_RDH, 0);
    wr32(REG_RDT, NUM_RX_DESC - 1);
    rx_cur = 0;

    /* RCTL */
    wr32(REG_RCTL, RCTL_EN | RCTL_UPE | RCTL_MPE | RCTL_LBM_NONE |
                   RCTL_BAM | RCTL_BSIZE_2048 | RCTL_SECRC);

    /* Setup TX */
    memset(tx_descs, 0, sizeof(tx_descs));
    for (int i = 0; i < NUM_TX_DESC; i++) {
        tx_descs[i].addr = 0;
        tx_descs[i].status = TDESC_STA_DD;
    }
    wr32(REG_TDBAL, (uint32_t)(uintptr_t)tx_descs);
    wr32(REG_TDBAH, 0);
    wr32(REG_TDLEN, NUM_TX_DESC * sizeof(e1000_tx_desc));
    wr32(REG_TDH, 0);
    wr32(REG_TDT, 0);
    tx_cur = 0;

    wr32(REG_TCTL, TCTL_EN | TCTL_PSP | (0x10 << TCTL_CT_SHIFT) | (0x40 << TCTL_COLD_SHIFT));
    wr32(REG_TIPG, 0x0060200A);

    /* Link up */
    wr32(REG_CTRL, rd32(REG_CTRL) | (1 << 6)); /* set link up */

    initialized = true;

    vga_print_color("[E1000] Initialized  MAC=", 0x0A, 0x00);
    for (int i = 0; i < 6; i++) {
        char h[3];
        uint8_t b = mac_addr[i];
        h[0] = "0123456789ABCDEF"[(b >> 4) & 0xF];
        h[1] = "0123456789ABCDEF"[b & 0xF];
        h[2] = 0;
        vga_print(h);
        if (i < 5) vga_print(":");
    }
    vga_print("\n");
    return true;
}

void e1000_get_mac(uint8_t mac[6]) {
    memcpy(mac, mac_addr, 6);
}

bool e1000_link_up(void) {
    if (!initialized) return false;
    return (rd32(REG_STATUS) & 2) != 0; /* LU bit */
}

bool e1000_send(const void* data, size_t len) {
    if (!initialized || len == 0 || len > RX_BUF_SIZE) return false;

    /* Wait for descriptor */
    for (int spin = 0; spin < 100000; spin++) {
        if (tx_descs[tx_cur].status & TDESC_STA_DD) break;
    }

    memcpy(tx_buf, data, len);
    tx_descs[tx_cur].addr = (uint64_t)(uintptr_t)tx_buf;
    tx_descs[tx_cur].length = (uint16_t)len;
    tx_descs[tx_cur].cmd = CMD_EOP | CMD_IFCS | CMD_RS;
    tx_descs[tx_cur].status = 0;

    uint32_t next = (tx_cur + 1) % NUM_TX_DESC;
    wr32(REG_TDT, next);
    tx_cur = next;
    return true;
}

size_t e1000_recv(void* buf, size_t max_len) {
    if (!initialized) return 0;

    e1000_rx_desc* desc = &rx_descs[rx_cur];
    if (!(desc->status & RDESC_STA_DD)) return 0;

    size_t len = desc->length;
    if (len > max_len) len = max_len;
    memcpy(buf, rx_bufs[rx_cur], len);

    desc->status = 0;
    wr32(REG_RDT, rx_cur);
    rx_cur = (rx_cur + 1) % NUM_RX_DESC;
    return len;
}
