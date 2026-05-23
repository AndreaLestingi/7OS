#include "ata.h"
#include "../arch/i386/io.h"

#define ATA_PRIMARY_IO 0x1F0
#define ATA_PRIMARY_CTRL 0x3F6

#define ATA_REG_DATA       0x00
#define ATA_REG_ERROR      0x01
#define ATA_REG_SECCOUNT0  0x02
#define ATA_REG_LBA0       0x03
#define ATA_REG_LBA1       0x04
#define ATA_REG_LBA2       0x05
#define ATA_REG_HDDEVSEL   0x06
#define ATA_REG_COMMAND    0x07
#define ATA_REG_STATUS     0x07

#define ATA_CMD_READ_PIO   0x20
#define ATA_CMD_WRITE_PIO  0x30
#define ATA_CMD_CACHE_FLUSH 0xE7
#define ATA_CMD_IDENTIFY   0xEC

static inline void ata_io_wait() {
    inb(ATA_PRIMARY_CTRL);
    inb(ATA_PRIMARY_CTRL);
    inb(ATA_PRIMARY_CTRL);
    inb(ATA_PRIMARY_CTRL);
}

static inline bool ata_wait_not_busy() {
    u8 status;
    for (u32 i = 0; i < 0x4000; i++) {
        status = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
        if (status == 0xFF) return false;
        if (!(status & 0x80)) return (status & 0x01) == 0;
    }
    return false;
}

static inline bool ata_wait_drq() {
    u8 status;
    for (u32 i = 0; i < 0x4000; i++) {
        status = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
        if (status == 0xFF) return false;
        if (status & 0x01) return false;
        if (!(status & 0x80) && (status & 0x08)) return true;
    }
    return false;
}

bool ata_init() {
    outb(ATA_PRIMARY_CTRL, 0x04);
    for (u32 i = 0; i < 4; i++) inb(ATA_PRIMARY_CTRL);
    outb(ATA_PRIMARY_CTRL, 0x00);

    outb(ATA_PRIMARY_IO + ATA_REG_HDDEVSEL, 0xA0);
    for (u32 i = 0; i < 4; i++) inb(ATA_PRIMARY_CTRL);

    u8 status;
    for (u32 i = 0; i < 0x4000; i++) {
        status = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
        if (status == 0xFF) return false;
        if (!(status & 0x80) && (status & 0x40)) return true;
    }
    return false;
}

static inline void ata_select_drive(u32 lba) {
    outb(ATA_PRIMARY_IO + ATA_REG_HDDEVSEL, 0xE0 | ((lba >> 24) & 0x0F));
    ata_io_wait();
}

bool ata_read_sectors(u32 lba, u8* buffer, u32 sector_count) {
    if (sector_count == 0) return true;

    for (u32 s = 0; s < sector_count; s++) {
        ata_select_drive(lba);
        outb(ATA_PRIMARY_IO + ATA_REG_SECCOUNT0, 1);
        outb(ATA_PRIMARY_IO + ATA_REG_LBA0, (u8)(lba & 0xFF));
        outb(ATA_PRIMARY_IO + ATA_REG_LBA1, (u8)((lba >> 8) & 0xFF));
        outb(ATA_PRIMARY_IO + ATA_REG_LBA2, (u8)((lba >> 16) & 0xFF));
        outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_READ_PIO);
        ata_io_wait();

        if (!ata_wait_drq()) return false;

        for (u32 i = 0; i < 256; i++) {
            u16 data = inw(ATA_PRIMARY_IO + ATA_REG_DATA);
            buffer[0] = (u8)(data & 0xFF);
            buffer[1] = (u8)(data >> 8);
            buffer += 2;
        }

        lba++;
    }

    return true;
}

bool ata_write_sectors(u32 lba, const u8* buffer, u32 sector_count) {
    if (sector_count == 0) return true;

    for (u32 s = 0; s < sector_count; s++) {
        ata_select_drive(lba);
        outb(ATA_PRIMARY_IO + ATA_REG_SECCOUNT0, 1);
        outb(ATA_PRIMARY_IO + ATA_REG_LBA0, (u8)(lba & 0xFF));
        outb(ATA_PRIMARY_IO + ATA_REG_LBA1, (u8)((lba >> 8) & 0xFF));
        outb(ATA_PRIMARY_IO + ATA_REG_LBA2, (u8)((lba >> 16) & 0xFF));
        outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);
        ata_io_wait();

        if (!ata_wait_drq()) return false;

        for (u32 i = 0; i < 256; i++) {
            u16 data = (u16)buffer[0] | ((u16)buffer[1] << 8);
            outw(ATA_PRIMARY_IO + ATA_REG_DATA, data);
            buffer += 2;
        }

        outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_CACHE_FLUSH);
        ata_io_wait();
        if (!ata_wait_not_busy()) return false;

        lba++;
    }

    return true;
}

bool ata_get_sector_count(u64* out_sectors) {
    if (!out_sectors) return false;

    outb(ATA_PRIMARY_IO + ATA_REG_HDDEVSEL, 0xA0);
    ata_io_wait();
    outb(ATA_PRIMARY_IO + ATA_REG_SECCOUNT0, 0);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA0, 0);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA1, 0);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA2, 0);
    outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    ata_io_wait();

    u8 status = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
    if (status == 0 || status == 0xFF) return false;

    if (!ata_wait_drq()) return false;

    u16 data[256];
    for (u32 i = 0; i < 256; i++) {
        data[i] = inw(ATA_PRIMARY_IO + ATA_REG_DATA);
    }

    u32 sectors28 = (u32)data[60] | ((u32)data[61] << 16);
    u64 sectors48 = (u64)data[100] | ((u64)data[101] << 16) | ((u64)data[102] << 32) | ((u64)data[103] << 48);
    u64 sectors = sectors48 != 0 ? sectors48 : (u64)sectors28;
    if (sectors == 0) return false;

    *out_sectors = sectors;
    return true;
}
