/*
 * VirtIO-net legacy (QEMU -device virtio-net-pci,disable-legacy=off)
 * Minimal TX/RX via legacy IO space.
 */
#include "virtio_net.h"
#include "pci.h"
#include "io.h"
#include "string.h"
#include "vga.h"

/* Legacy virtio PCI IO offsets */
#define VIRTIO_DEV_FEAT   0x00
#define VIRTIO_GUEST_FEAT 0x04
#define VIRTIO_QADDR      0x08
#define VIRTIO_QSIZE      0x0C
#define VIRTIO_QSEL       0x0E
#define VIRTIO_QNOTIFY    0x10
#define VIRTIO_STATUS     0x12
#define VIRTIO_ISR        0x13
#define VIRTIO_NET_MAC    0x14  /* after config if MSIX off — device-specific */

#define VIRTIO_STATUS_ACK 1
#define VIRTIO_STATUS_DRV 2
#define VIRTIO_STATUS_OK  4
#define VIRTIO_STATUS_FAIL 0x80

#define Q_RX 0
#define Q_TX 1

#define QUEUE_SIZE 8

struct vdesc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

struct vavail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[QUEUE_SIZE];
} __attribute__((packed));

struct vused_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

struct vused {
    uint16_t flags;
    uint16_t idx;
    vused_elem ring[QUEUE_SIZE];
} __attribute__((packed));

struct vq {
    vdesc descs[QUEUE_SIZE] __attribute__((aligned(16)));
    vavail avail __attribute__((aligned(2)));
    uint8_t pad[4096 - sizeof(vdesc)*QUEUE_SIZE - sizeof(vavail)];
    vused used __attribute__((aligned(4096)));
} __attribute__((aligned(4096)));

static uint16_t io_base = 0;
static bool initialized = false;
static uint8_t mac[6] = {0x52,0x54,0x00,0x12,0x34,0x56};
static vq rx_q __attribute__((aligned(4096)));
static vq tx_q __attribute__((aligned(4096)));
static uint8_t rx_bufs[QUEUE_SIZE][1600] __attribute__((aligned(16)));
static uint8_t tx_bufs[QUEUE_SIZE][1600] __attribute__((aligned(16)));
/* virtio-net rx header 10 bytes */
struct virtio_net_hdr {
    uint8_t flags, gso_type;
    uint16_t hdr_len, gso_size, csum_start, csum_offset;
} __attribute__((packed));
static virtio_net_hdr rx_hdrs[QUEUE_SIZE];
static virtio_net_hdr tx_hdr;
static uint16_t rx_last = 0, tx_free = 0;

static inline void vw8(uint16_t o, uint8_t v) { outb((uint16_t)(io_base+o), v); }
static inline void vw16(uint16_t o, uint16_t v){ outw((uint16_t)(io_base+o), v); }
static inline void vw32(uint16_t o, uint32_t v){ outl((uint16_t)(io_base+o), v); }
static inline uint8_t  vr8(uint16_t o) { return inb((uint16_t)(io_base+o)); }
static inline uint16_t vr16(uint16_t o){ return inw((uint16_t)(io_base+o)); }
static inline uint32_t vr32(uint16_t o){ return inl((uint16_t)(io_base+o)); }

static void setup_queue(int qidx, vq* q) {
    memset(q, 0, sizeof(*q));
    vw16(VIRTIO_QSEL, (uint16_t)qidx);
    uint16_t qs = vr16(VIRTIO_QSIZE);
    if (qs == 0) return;
    if (qs > QUEUE_SIZE) qs = QUEUE_SIZE;
    uint32_t pfn = ((uint32_t)(uintptr_t)q) >> 12;
    vw32(VIRTIO_QADDR, pfn);
}

bool virtio_net_init(void) {
    uint8_t bus, slot, func;
    if (!pci_find_device(VIRTIO_VENDOR, VIRTIO_NET_DEVICE, &bus, &slot, &func))
        return false;

    uint32_t bar0 = pci_get_bar(bus, slot, func, 0);
    if (!(bar0 & 1)) return false;
    io_base = (uint16_t)(bar0 & ~3u);

    uint32_t cmd = pci_read_config(bus, slot, func, 0x04);
    cmd |= 0x05;
    pci_write_config(bus, slot, func, 0x04, cmd);

    /* reset */
    vw8(VIRTIO_STATUS, 0);
    vw8(VIRTIO_STATUS, VIRTIO_STATUS_ACK);
    vw8(VIRTIO_STATUS, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRV);

    uint32_t features = vr32(VIRTIO_DEV_FEAT);
    /* MAC feature bit 5 */
    uint32_t guest = 0;
    if (features & (1u << 5)) guest |= (1u << 5);
    vw32(VIRTIO_GUEST_FEAT, guest);

    setup_queue(Q_RX, &rx_q);
    setup_queue(Q_TX, &tx_q);

    /* fill RX descriptors */
    for (int i = 0; i < QUEUE_SIZE; i++) {
        rx_q.descs[i].addr = (uint64_t)(uintptr_t)&rx_hdrs[i];
        rx_q.descs[i].len = sizeof(virtio_net_hdr);
        rx_q.descs[i].flags = 2 | 1; /* WRITE | NEXT */
        rx_q.descs[i].next = (uint16_t)(i + QUEUE_SIZE);
        /* second half as data buffers — use same desc ring carefully:
           legacy: chain header + buffer */
        /* simpler: one desc per buf including space for hdr at start of rx_bufs */
    }
    /* redo RX simpler: each buf starts with hdr */
    for (int i = 0; i < QUEUE_SIZE; i++) {
        rx_q.descs[i].addr = (uint64_t)(uintptr_t)rx_bufs[i];
        rx_q.descs[i].len = 1600;
        rx_q.descs[i].flags = 2; /* WRITE */
        rx_q.descs[i].next = 0;
        rx_q.avail.ring[i] = (uint16_t)i;
    }
    rx_q.avail.idx = QUEUE_SIZE;
    vw16(VIRTIO_QNOTIFY, Q_RX);

    for (int i = 0; i < QUEUE_SIZE; i++) {
        tx_q.descs[i].flags = 0;
        tx_q.descs[i].next = 0;
    }
    tx_free = 0;
    rx_last = 0;

    vw8(VIRTIO_STATUS, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRV | VIRTIO_STATUS_OK);

    /* try read MAC from config space after ISR */
    for (int i = 0; i < 6; i++) {
        uint8_t b = vr8((uint16_t)(VIRTIO_NET_MAC + i));
        if (b != 0x00 && b != 0xFF) mac[i] = b;
    }

    initialized = true;
    vga_print("  [nic] VirtIO-net\n");
    return true;
}

bool virtio_net_send(const void* data, size_t len) {
    if (!initialized || len == 0 || len > 1514) return false;
    int i = tx_free % QUEUE_SIZE;
    memset(&tx_hdr, 0, sizeof(tx_hdr));
    /* two-desc chain: hdr + data in one buffer with hdr prefix */
    uint8_t* p = tx_bufs[i];
    memset(p, 0, sizeof(virtio_net_hdr));
    memcpy(p + sizeof(virtio_net_hdr), data, len);
    tx_q.descs[i].addr = (uint64_t)(uintptr_t)p;
    tx_q.descs[i].len = (uint32_t)(sizeof(virtio_net_hdr) + len);
    tx_q.descs[i].flags = 0;
    uint16_t av = tx_q.avail.idx;
    tx_q.avail.ring[av % QUEUE_SIZE] = (uint16_t)i;
    tx_q.avail.idx = (uint16_t)(av + 1);
    vw16(VIRTIO_QNOTIFY, Q_TX);
    tx_free++;
    return true;
}

size_t virtio_net_recv(void* buf, size_t max_len) {
    if (!initialized) return 0;
    if (rx_last == rx_q.used.idx) return 0;
    vused_elem* e = &rx_q.used.ring[rx_last % QUEUE_SIZE];
    uint32_t id = e->id;
    uint32_t len = e->len;
    if (id >= QUEUE_SIZE || len <= sizeof(virtio_net_hdr)) {
        rx_last++;
        return 0;
    }
    size_t copy = len - sizeof(virtio_net_hdr);
    if (copy > max_len) copy = max_len;
    memcpy(buf, rx_bufs[id] + sizeof(virtio_net_hdr), copy);

    /* re-post buffer */
    rx_q.descs[id].addr = (uint64_t)(uintptr_t)rx_bufs[id];
    rx_q.descs[id].len = 1600;
    rx_q.descs[id].flags = 2;
    uint16_t av = rx_q.avail.idx;
    rx_q.avail.ring[av % QUEUE_SIZE] = (uint16_t)id;
    rx_q.avail.idx = (uint16_t)(av + 1);
    vw16(VIRTIO_QNOTIFY, Q_RX);
    rx_last++;
    return copy;
}

void virtio_net_get_mac(uint8_t m[6]) {
    for (int i = 0; i < 6; i++) m[i] = mac[i];
}

bool virtio_net_link_up(void) {
    return initialized;
}
