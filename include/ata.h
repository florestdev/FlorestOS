#pragma once
#include "types.h"

bool ata_present(void);
/* Select drive by index among detected ATA disks (0..n-1). Returns false if none. */
bool ata_select_index(int index);
int  ata_count(void); /* number of ATA disks found (call after ata_present or scan) */
int  ata_scan(void);  /* rescan, returns count */

uint32_t ata_sector_count(void);
bool ata_write_sectors(uint32_t lba, const void* buf, uint32_t count);
bool ata_read_sectors(uint32_t lba, void* buf, uint32_t count);
