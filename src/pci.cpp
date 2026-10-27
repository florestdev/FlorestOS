#include "pci.h"
#include "io.h"

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

uint32_t pci_read_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (1u << 31) |
                       ((uint32_t)bus << 16) |
                       ((uint32_t)slot << 11) |
                       ((uint32_t)func << 8) |
                       (offset & 0xFC);
    outl(PCI_CONFIG_ADDR, address);
    return inl(PCI_CONFIG_DATA);
}

void pci_write_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    uint32_t address = (1u << 31) |
                       ((uint32_t)bus << 16) |
                       ((uint32_t)slot << 11) |
                       ((uint32_t)func << 8) |
                       (offset & 0xFC);
    outl(PCI_CONFIG_ADDR, address);
    outl(PCI_CONFIG_DATA, value);
}

bool pci_find_device(uint16_t vendor, uint16_t device, uint8_t* bus, uint8_t* slot, uint8_t* func) {
    for (int b = 0; b < 256; b++) {
        for (int s = 0; s < 32; s++) {
            for (int f = 0; f < 8; f++) {
                uint32_t id = pci_read_config(b, s, f, 0);
                uint16_t v = id & 0xFFFF;
                uint16_t d = (id >> 16) & 0xFFFF;
                if (v == 0xFFFF) continue;
                if (v == vendor && d == device) {
                    *bus = b;
                    *slot = s;
                    *func = f;
                    return true;
                }
            }
        }
    }
    return false;
}

uint32_t pci_get_bar(uint8_t bus, uint8_t slot, uint8_t func, int bar_index) {
    uint32_t bar = pci_read_config(bus, slot, func, 0x10 + bar_index * 4);
    return bar;
}
