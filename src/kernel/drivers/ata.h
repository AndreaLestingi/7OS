#ifndef ATA_H
#define ATA_H

#include "../util/types.h"

bool ata_init();
bool ata_read_sectors(u32 lba, u8* buffer, u32 sector_count);
bool ata_write_sectors(u32 lba, const u8* buffer, u32 sector_count);
bool ata_get_sector_count(u64* out_sectors);

#endif
