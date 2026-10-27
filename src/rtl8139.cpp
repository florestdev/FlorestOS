/*
 * Realtek RTL8139 — classic PCI NIC (QEMU rtl8139, many boards)
 */
#include "rtl8139.h"
#include "pci.h"
#include "io.h"
#include "string.h"
#include "vga.h"

#define REG_IDR0    0x00
#define REG_TSD0    0x10
#define REG_TSAD0   0x20
#define REG_RBSTART 0x30
#define REG_CR      0x37
#define REG_CAPR    0x38
#define REG_IMR     0x3C
#define REG_ISR     0x3E
#define REG_TCR     0x40
#define REG_RCR     0x44
#define REG_CONFIG1 0x52
#define REG_MSR     0x58

#define CR_RST      0x10
#define CR_RE       0x08
#define CR_TE       0x04
#define CR_BUFE     0x01

#define RCR_AAP     (1<<0)
#define RCR_APM     (1<<1)
#define RCR_AM      (1<<2)
#define RCR_AB      (1<<3)
#define RCR_WRAP    (1<<7)
#define RCR_MXDMA   (7<<8)

#define TCR_MXDMA   (7<<8)
#define TCR_IFG     (3<<24)

#define RX_BUF_TOT  (8192 + 16 + 1500)
#define NUM_TX      4

static uint16_t io_base = 0;
static bool initialized = false;
static uint8_t mac[6];
static uint8_t rx_ring[RX_BUF_TOT] __attribute__((aligned(4)));
static uint8_t tx_buf[NUM_TX][1600] __attribute__((aligned(4)));
static int tx_cur = 0;
static uint16_t rx_offset = 0;

static inline void ow8(uint16_t r, uint8_t v)  { outb((uint16_t)(io_base + r), v); }
static inline void ow16(uint16_t r, uint16_t v){ outw((uint16_t)(io_base + r), v); }
static inline void ow32(uint16_t r, uint32_t v){ outl((uint16_t)(io_base + r), v); }
static inline uint8_t  ir8(uint16_t r)  { return inb((uint16_t)(io_base + r)); }
static inline uint16_t ir16(uint16_t r) { return inw((uint16_t)(io_base + r)); }
static inline uint32_t ir32(uint16_t r) { return inl((uint16_t)(io_base + r)); }

bool rtl8139_init(void) {
    uint8_t bus, slot, func;
    if (!pci_find_device(RTL8139_VENDOR, RTL8139_DEVICE, &bus, &slot, &func))
        return false;

    uint32_t bar0 = pci_get_bar(bus, slot, func, 0);
    if (!(bar0 & 1)) return false; /* need I/O BAR */
    io_base = (uint16_t)(bar0 & ~3u);

    /* enable bus master + IO */
    uint32_t cmd = pci_read_config(bus, slot, func, 0x04);
    cmd |= 0x05;
    pci_write_config(bus, slot, func, 0x04, cmd);

    ow8(REG_CONFIG1, 0x00); /* power on */

    /* soft reset */
    ow8(REG_CR, CR_RST);
    for (int i = 0; i < 100000; i++) {
        if (!(ir8(REG_CR) & CR_RST)) break;
    }

    for (int i = 0; i < 6; i++) mac[i] = ir8(REG_IDR0 + i);

    /* RX ring physical = identity */
    uint32_t rx_phys = (uint32_t)(uintptr_t)rx_ring;
    ow32(REG_RBSTART, rx_phys);

    ow32(REG_IMR, 0); /* poll mode */
    ow16(REG_ISR, 0xFFFF);

    ow32(REG_RCR, RCR_AAP | RCR_APM | RCR_AM | RCR_AB | RCR_WRAP | RCR_MXDMA);
    ow32(REG_TCR, TCR_MXDMA | TCR_IFG);

    ow8(REG_CR, CR_RE | CR_TE);
    rx_offset = 0;
    tx_cur = 0;
    initialized = true;

    vga_print("  [nic] RTL8139 IO=0x");
    /* hex print short */
    {
        char h[] = "0123456789ABCDEF";
        vga_putchar(h[(io_base >> 12) & 0xF]);
        vga_putchar(h[(io_base >> 8) & 0xF]);
        vga_putchar(h[(io_base >> 4) & 0xF]);
        vga_putchar(h[io_base & 0xF]);
    }
    vga_print("\n");
    return true;
}

bool rtl8139_send(const void* data, size_t len) {
    if (!initialized || len == 0 || len > 1514) return false;
    int i = tx_cur % NUM_TX;
    /* wait previous */
    for (int t = 0; t < 100000; t++) {
        uint32_t tsd = ir32(REG_TSD0 + i * 4);
        if (tsd & (1u << 13) /* OWN clear means done, or first use */) break;
        if (!(tsd & 0x1FFF) && (tsd & (1u << 15))) break; /* TOK */
        if (!(tsd & 0xFFF)) break;
    }
    memcpy(tx_buf[i], data, len);
    ow32(REG_TSAD0 + i * 4, (uint32_t)(uintptr_t)tx_buf[i]);
    ow32(REG_TSD0 + i * 4, (uint32_t)len);
    tx_cur = (i + 1) % NUM_TX;
    return true;
}

size_t rtl8139_recv(void* buf, size_t max_len) {
    if (!initialized) return 0;
    if (ir8(REG_CR) & CR_BUFE) return 0; /* empty */

    uint16_t* p = (uint16_t*)(rx_ring + (rx_offset % 8192));
    uint16_t status = p[0];
    uint16_t pkt_len = p[1];

    if (!(status & 1) /* ROK */) {
        /* bad packet — reset RX */
        ow8(REG_CR, CR_TE);
        ow8(REG_CR, CR_RE | CR_TE);
        rx_offset = 0;
        ow32(REG_RBSTART, (uint32_t)(uintptr_t)rx_ring);
        return 0;
    }
    if (pkt_len < 4 || pkt_len > 2048) {
        rx_offset = 0;
        return 0;
    }
    size_t copy = pkt_len - 4; /* strip CRC */
    if (copy > max_len) copy = max_len;
    uint8_t* src = (uint8_t*)p + 4;
    /* may wrap — ring is WRAP mode with extra space */
    memcpy(buf, src, copy);

    rx_offset = (uint16_t)((rx_offset + pkt_len + 4 + 3) & ~3);
    ow16(REG_CAPR, (uint16_t)(rx_offset - 16));
    return copy;
}

void rtl8139_get_mac(uint8_t m[6]) {
    for (int i = 0; i < 6; i++) m[i] = mac[i];
}

bool rtl8139_link_up(void) {
    if (!initialized) return false;
    return (ir8(REG_MSR) & 0x04) != 0; /* LINKB */
}
