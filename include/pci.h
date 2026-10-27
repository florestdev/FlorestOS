#pragma once
#include "types.h"

uint32_t pci_read_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void     pci_write_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value);

/* Find first device matching vendor/device, fill bus/slot/func. Returns false if not found. */
bool pci_find_device(uint16_t vendor, uint16_t device, uint8_t* bus, uint8_t* slot, uint8_t* func);

uint32_t pci_get_bar(uint8_t bus, uint8_t slot, uint8_t func, int bar_index);
