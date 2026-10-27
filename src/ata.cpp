#include "ata.h"
#include "io.h"

#define ATA_SR_BSY  0x80
#define ATA_SR_DRQ  0x08
#define ATA_SR_ERR  0x01

struct ata_dev {
    uint16_t base;
    uint8_t  drive; /* 0xE0 / 0xF0 */
    bool     used;
};

static ata_dev g_devs[4];
static int g_ndev = 0;
static int g_cur = -1;

static uint16_t g_base = 0;
static uint8_t  g_drive = 0;

static inline uint16_t R_DATA(void)     { return g_base; }
static inline uint16_t R_SECCOUNT(void) { return (uint16_t)(g_base + 2); }
static inline uint16_t R_LBA_LO(void)   { return (uint16_t)(g_base + 3); }
static inline uint16_t R_LBA_MID(void)  { return (uint16_t)(g_base + 4); }
static inline uint16_t R_LBA_HI(void)   { return (uint16_t)(g_base + 5); }
static inline uint16_t R_DRIVE(void)    { return (uint16_t)(g_base + 6); }
static inline uint16_t R_STATUS(void)   { return (uint16_t)(g_base + 7); }
static inline uint16_t R_CMD(void)      { return (uint16_t)(g_base + 7); }
static inline uint16_t R_ALT(void) {
    return (g_base == 0x1F0) ? (uint16_t)0x3F6 : (uint16_t)0x376;
}

static void ata_delay(void) {
    inb(R_ALT()); inb(R_ALT()); inb(R_ALT()); inb(R_ALT());
}

static int ata_wait_bsy(void) {
    for (int i = 0; i < 5000000; i++) {
        if (!(inb(R_STATUS()) & ATA_SR_BSY)) return 0;
    }
    return -1;
}

static int ata_wait_drq(void) {
    for (int i = 0; i < 5000000; i++) {
        uint8_t s = inb(R_STATUS());
        if (s & ATA_SR_ERR) return -1;
        if (s & ATA_SR_DRQ) return 0;
    }
    return -1;
}

static void ata_soft_reset(uint16_t base) {
    uint16_t alt = (base == 0x1F0) ? (uint16_t)0x3F6 : (uint16_t)0x376;
    outb(alt, 0x04);
    for (volatile int i = 0; i < 200000; i++) {}
    outb(alt, 0x00);
    for (volatile int i = 0; i < 200000; i++) {}
}

static bool ata_identify(uint16_t base, uint8_t drive) {
    g_base = base;
    g_drive = drive;

    outb(R_DRIVE(), drive);
    ata_delay();

    uint8_t st = inb(R_STATUS());
    if (st == 0xFF || st == 0x00) return false;

    outb(R_SECCOUNT(), 0);
    outb(R_LBA_LO(), 0);
    outb(R_LBA_MID(), 0);
    outb(R_LBA_HI(), 0);
    outb(R_CMD(), 0xEC);
    ata_delay();

    st = inb(R_STATUS());
    if (st == 0 || st == 0xFF) return false;
    if (ata_wait_bsy() < 0) return false;

    uint8_t mid = inb(R_LBA_MID());
    uint8_t hi  = inb(R_LBA_HI());
    if (mid != 0 || hi != 0) return false; /* ATAPI optical */

    st = inb(R_STATUS());
    if (st & ATA_SR_ERR) return false;
    if (!(st & ATA_SR_DRQ)) {
        if (ata_wait_drq() < 0) return false;
    }
    for (int i = 0; i < 256; i++) (void)inw(R_DATA());
    return true;
}

int ata_scan(void) {
    static const uint16_t bases[2] = { 0x1F0, 0x170 };
    static const uint8_t drives[2] = { 0xE0, 0xF0 };
    g_ndev = 0;
    g_cur = -1;
    g_base = 0;
    for (int i = 0; i < 4; i++) g_devs[i].used = false;

    for (int bi = 0; bi < 2; bi++) {
        ata_soft_reset(bases[bi]);
        for (int di = 0; di < 2; di++) {
            if (g_ndev >= 4) break;
            if (ata_identify(bases[bi], drives[di])) {
                g_devs[g_ndev].base = bases[bi];
                g_devs[g_ndev].drive = drives[di];
                g_devs[g_ndev].used = true;
                g_ndev++;
            }
        }
    }
    if (g_ndev > 0) {
        g_cur = 0;
        g_base = g_devs[0].base;
        g_drive = g_devs[0].drive;
    }
    return g_ndev;
}

bool ata_present(void) {
    if (g_ndev <= 0) ata_scan();
    return g_ndev > 0;
}

int ata_count(void) {
    if (g_ndev <= 0) ata_scan();
    return g_ndev;
}

bool ata_select_index(int index) {
    if (g_ndev <= 0) ata_scan();
    if (index < 0 || index >= g_ndev) return false;
    g_cur = index;
    g_base = g_devs[index].base;
    g_drive = g_devs[index].drive;
    outb(R_DRIVE(), g_drive);
    ata_delay();
    return true;
}

uint32_t ata_sector_count(void) { return 0; }

bool ata_write_sectors(uint32_t lba, const void* buf, uint32_t count) {
    if (g_base == 0 && !ata_present()) return false;
    const uint16_t* src = (const uint16_t*)buf;
    for (uint32_t s = 0; s < count; s++) {
        if (ata_wait_bsy() < 0) return false;
        outb(R_DRIVE(), (uint8_t)(g_drive | ((lba >> 24) & 0x0F)));
        ata_delay();
        outb(R_SECCOUNT(), 1);
        outb(R_LBA_LO(), (uint8_t)(lba & 0xFF));
        outb(R_LBA_MID(), (uint8_t)((lba >> 8) & 0xFF));
        outb(R_LBA_HI(), (uint8_t)((lba >> 16) & 0xFF));
        outb(R_CMD(), 0x30);
        if (ata_wait_drq() < 0) return false;
        for (int i = 0; i < 256; i++) outw(R_DATA(), src[i]);
        src += 256;
        lba++;
        if (ata_wait_bsy() < 0) return false;
        uint8_t st = inb(R_STATUS());
        if (st & ATA_SR_ERR) return false;
    }
    /* flush cache */
    outb(R_CMD(), 0xE7);
    ata_wait_bsy();
    return true;
}

bool ata_read_sectors(uint32_t lba, void* buf, uint32_t count) {
    if (g_base == 0 && !ata_present()) return false;
    uint16_t* dst = (uint16_t*)buf;
    for (uint32_t s = 0; s < count; s++) {
        if (ata_wait_bsy() < 0) return false;
        outb(R_DRIVE(), (uint8_t)(g_drive | ((lba >> 24) & 0x0F)));
        ata_delay();
        outb(R_SECCOUNT(), 1);
        outb(R_LBA_LO(), (uint8_t)(lba & 0xFF));
        outb(R_LBA_MID(), (uint8_t)((lba >> 8) & 0xFF));
        outb(R_LBA_HI(), (uint8_t)((lba >> 16) & 0xFF));
        outb(R_CMD(), 0x20);
        if (ata_wait_drq() < 0) return false;
        for (int i = 0; i < 256; i++) dst[i] = inw(R_DATA());
        dst += 256;
        lba++;
    }
    return true;
}
