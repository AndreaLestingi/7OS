#include "../drivers/ata.h"
#include "filesystem.h"

#define FS_MAGIC 0x7A4F5346u
#define FS_VERSION 1u
#define FS_BLOCK_SIZE 512u

#define FS_SUPER_LBA 128u
#define FS_BITMAP_LBA 129u
#ifndef FS_BITMAP_SECTORS
#define FS_BITMAP_SECTORS 8u
#endif
#ifndef FS_MAX_INODES
#define FS_MAX_INODES 64u
#endif
#define FS_INODE_TABLE_LBA (FS_BITMAP_LBA + FS_BITMAP_SECTORS)
#define FS_INODE_TABLE_SECTORS ((FS_MAX_INODES + 7u) / 8u)
#define FS_DATA_LBA (FS_INODE_TABLE_LBA + FS_INODE_TABLE_SECTORS)

enum FsInodeType {
	INODE_FILE = 1,
	INODE_DIR = 2
};

struct Superblock {
	u32 magic;
	u32 version;
	u32 block_size;
	u32 inode_count;
	u32 block_count;
	u32 inode_table_lba;
	u32 bitmap_lba;
	u32 data_lba;
} __attribute__((packed));

struct Inode {
	u8 used;
	u8 type;
	u16 links;
	u32 size;
	u32 direct[8];
	u32 reserved[6];
} __attribute__((packed));

static Superblock g_sb;
static u8* const g_bitmap = reinterpret_cast<u8*>(0x110000);
static Inode g_inodes[FS_MAX_INODES];
static bool g_mounted = false;
static FsError g_last_error = FS_ERR_NONE;
static Inode g_inode_scratch[FS_MAX_INODES];
static u8 g_block_scratch[FS_BLOCK_SIZE];
static u8 g_block_scratch2[FS_BLOCK_SIZE];

static void mem_set(u8* dst, u8 value, u32 count) {
	for (u32 i = 0; i < count; i++) {
		dst[i] = value;
	}
}

static void mem_copy(u8* dst, const u8* src, u32 count) {
	for (u32 i = 0; i < count; i++) {
		dst[i] = src[i];
	}
}

static u32 str_len(const char* s) {
	u32 i = 0;
	while (s && s[i]) {
		i++;
	}
	return i;
}

static bool str_equals(const char* a, const char* b) {
	u32 i = 0;
	while (a[i] && b[i]) {
		if (a[i] != b[i]) return false;
		i++;
	}
	return a[i] == b[i];
}

static u32 fnv1a_32(const char* s) {
	u32 hash = 2166136261u;
	while (*s) {
		hash ^= (u8)*s++;
		hash *= 16777619u;
	}
	return hash;
}

static bool disk_read_lba(u32 lba, u8* buffer, u32 sectors) {
	return ata_read_sectors(lba, buffer, sectors);
}

static bool disk_write_lba(u32 lba, const u8* buffer, u32 sectors) {
	return ata_write_sectors(lba, buffer, sectors);
}

static bool ensure_root_dir_block();

static void mark_block_used(u32 index) {
	u32 byte_index = index / 8u;
	u32 bit_index = index % 8u;
	g_bitmap[byte_index] |= (u8)(1u << bit_index);
}

static void mark_block_free(u32 index) {
	u32 byte_index = index / 8u;
	u32 bit_index = index % 8u;
	g_bitmap[byte_index] &= (u8)~(1u << bit_index);
}

static bool flush_bitmap() {
	return disk_write_lba(g_sb.bitmap_lba, g_bitmap, FS_BITMAP_SECTORS);
}

static bool flush_inodes() {
	return disk_write_lba(g_sb.inode_table_lba, (const u8*)g_inodes, FS_INODE_TABLE_SECTORS);
}

static int alloc_inode_raw() {
	u32 limit = g_sb.inode_count;
	if (limit == 0 || limit > FS_MAX_INODES) {
		limit = FS_MAX_INODES;
	}
	for (u32 i = 1; i < limit; i++) {
		if (g_inodes[i].used == 0) {
			return (int)i;
		}
	}
	return -1;
}

static int alloc_block_raw() {
	u32 limit = g_sb.block_count;
	u32 max_blocks = FS_BLOCK_SIZE * 8u * FS_BITMAP_SECTORS;
	if (limit == 0 || limit > max_blocks) {
		limit = max_blocks;
	}
	for (u32 i = 1; i < limit; i++) {
		u32 byte_index = i / 8u;
		u32 bit_index = i % 8u;
		u8 mask = (u8)(1u << bit_index);
		if ((g_bitmap[byte_index] & mask) == 0) {
			return (int)i;
		}
	}
	return -1;
}

static bool read_root_dir_block(u8* out_block) {
	Inode root = g_inodes[0];
	if (root.used == 0 || root.type != INODE_DIR || root.direct[0] == 0) {
		return false;
	}
	return disk_read_lba(g_sb.data_lba + root.direct[0], out_block, 1);
}

static bool load_dir_block(int dir_inode, u8* out_block) {
	if (dir_inode < 0 || dir_inode >= (int)FS_MAX_INODES) return false;
	Inode* dir = &g_inodes[dir_inode];
	if (dir->used == 0 || dir->type != INODE_DIR || dir->direct[0] == 0) return false;
	return disk_read_lba(g_sb.data_lba + dir->direct[0], out_block, 1);
}

static bool write_dir_block(int dir_inode, const u8* block) {
	if (dir_inode < 0 || dir_inode >= (int)FS_MAX_INODES) return false;
	Inode* dir = &g_inodes[dir_inode];
	if (dir->used == 0 || dir->type != INODE_DIR || dir->direct[0] == 0) return false;
	return disk_write_lba(g_sb.data_lba + dir->direct[0], block, 1);
}

static int find_dir_entry(const char* name, DirEntry* entries, u32 entry_count, const Inode* inode_table, u8 type) {
	for (u32 i = 0; i < entry_count; i++) {
		if (!entries[i].used) continue;
		if (!str_equals(entries[i].name, name)) continue;
		int inode = (int)entries[i].inode;
		if (inode < 0 || inode >= (int)FS_MAX_INODES) continue;
		if (inode_table[inode].used == 0) continue;
		if (type == 0 || inode_table[inode].type == type) return (int)i;
	}
	return -1;
}

static bool write_sys_file(const char* filename, const u8* data, u32 len) {
	if (!ensure_root_dir_block()) return false;
	if (!read_root_dir_block(g_block_scratch)) return false;

	DirEntry* entries = (DirEntry*)g_block_scratch;
	u32 entry_count = FS_BLOCK_SIZE / sizeof(DirEntry);
	int sys_entry = find_dir_entry("sys", entries, entry_count, g_inodes, INODE_DIR);
	int sys_inode = -1;

	if (sys_entry < 0) {
		sys_inode = alloc_inode_raw();
		if (sys_inode < 0) return false;
		int sys_block = alloc_block_raw();
		if (sys_block < 0) return false;
		mark_block_used((u32)sys_block);
		g_inodes[sys_inode].used = 1;
		g_inodes[sys_inode].type = INODE_DIR;
		g_inodes[sys_inode].links = 1;
		g_inodes[sys_inode].size = 0;
		g_inodes[sys_inode].direct[0] = (u32)sys_block;

		int free_slot = -1;
		for (u32 i = 0; i < entry_count; i++) {
			if (!entries[i].used) { free_slot = (int)i; break; }
		}
		if (free_slot < 0) return false;
		mem_set((u8*)&entries[free_slot], 0, sizeof(DirEntry));
		entries[free_slot].used = 1;
		entries[free_slot].inode = (u8)sys_inode;
		mem_copy((u8*)entries[free_slot].name, (const u8*)"sys", 3);

		mem_set(g_block_scratch2, 0, FS_BLOCK_SIZE);
		if (!disk_write_lba(g_sb.data_lba + g_inodes[sys_inode].direct[0], g_block_scratch2, 1)) return false;
	} else {
		sys_inode = (int)entries[sys_entry].inode;
		if (sys_inode < 0 || sys_inode >= (int)FS_MAX_INODES) return false;
		if (g_inodes[sys_inode].type != INODE_DIR || g_inodes[sys_inode].direct[0] == 0) return false;
	}

	if (!disk_write_lba(g_sb.data_lba + g_inodes[0].direct[0], g_block_scratch, 1)) return false;

	if (!disk_read_lba(g_sb.data_lba + g_inodes[sys_inode].direct[0], g_block_scratch2, 1)) return false;

	DirEntry* sys_entries = (DirEntry*)g_block_scratch2;
	int file_entry = find_dir_entry(filename, sys_entries, entry_count, g_inodes, INODE_FILE);
	int file_inode = -1;

	if (file_entry < 0) {
		file_inode = alloc_inode_raw();
		if (file_inode < 0) return false;
		int file_block = alloc_block_raw();
		if (file_block < 0) return false;
		mark_block_used((u32)file_block);
		g_inodes[file_inode].used = 1;
		g_inodes[file_inode].type = INODE_FILE;
		g_inodes[file_inode].links = 1;
		g_inodes[file_inode].size = len;
		g_inodes[file_inode].direct[0] = (u32)file_block;

		int free_slot = -1;
		for (u32 i = 0; i < entry_count; i++) {
			if (!sys_entries[i].used) { free_slot = (int)i; break; }
		}
		if (free_slot < 0) return false;
		mem_set((u8*)&sys_entries[free_slot], 0, sizeof(DirEntry));
		sys_entries[free_slot].used = 1;
		sys_entries[free_slot].inode = (u8)file_inode;
		mem_copy((u8*)sys_entries[free_slot].name, (const u8*)filename, str_len(filename));
	} else {
		file_inode = (int)sys_entries[file_entry].inode;
		if (file_inode < 0 || file_inode >= (int)FS_MAX_INODES) return false;
		g_inodes[file_inode].size = len;
	}

	if (!disk_write_lba(g_sb.data_lba + g_inodes[sys_inode].direct[0], g_block_scratch2, 1)) return false;

	u32 write_len = len < FS_BLOCK_SIZE ? len : FS_BLOCK_SIZE;
	mem_set(g_block_scratch, 0, FS_BLOCK_SIZE);
	mem_copy(g_block_scratch, data, write_len);
	if (!disk_write_lba(g_sb.data_lba + g_inodes[file_inode].direct[0], g_block_scratch, 1)) return false;

	return flush_bitmap() && flush_inodes();
}

static bool read_sys_file(const char* filename, u8* out, u32 maxlen, u32* out_len) {
	if (!g_mounted) return false;
	Inode* root = &g_inodes[0];
	if (root->used == 0 || root->direct[0] == 0) return false;

	if (!disk_read_lba(g_sb.data_lba + root->direct[0], g_block_scratch, 1)) return false;

	DirEntry* entries = (DirEntry*)g_block_scratch;
	u32 entry_count = FS_BLOCK_SIZE / sizeof(DirEntry);

	int sys_idx = find_dir_entry("sys", entries, entry_count, g_inodes, INODE_DIR);
	if (sys_idx < 0) return false;
	int sys_inode = (int)entries[sys_idx].inode;
	if (sys_inode < 0 || sys_inode >= (int)FS_MAX_INODES) return false;
	if (g_inodes[sys_inode].type != INODE_DIR || g_inodes[sys_inode].direct[0] == 0) return false;

	if (!disk_read_lba(g_sb.data_lba + g_inodes[sys_inode].direct[0], g_block_scratch2, 1)) return false;

	DirEntry* sys_entries = (DirEntry*)g_block_scratch2;
	int file_idx = find_dir_entry(filename, sys_entries, entry_count, g_inodes, INODE_FILE);
	if (file_idx < 0) return false;
	int file_inode = (int)sys_entries[file_idx].inode;
	if (file_inode < 0 || file_inode >= (int)FS_MAX_INODES) return false;
	if (g_inodes[file_inode].direct[0] == 0) return false;

	if (!disk_read_lba(g_sb.data_lba + g_inodes[file_inode].direct[0], g_block_scratch, 1)) return false;

	u32 sz = g_inodes[file_inode].size;
	if (sz > maxlen) sz = maxlen;
	mem_copy(out, g_block_scratch, sz);
	if (out_len) *out_len = sz;
	return true;
}

static bool write_status_flag(u8 value) {
	return write_sys_file("status", &value, 1);
}

static bool ensure_root_dir_block() {
	Inode* root = &g_inodes[0];
	if (root->direct[0] != 0) {
		return true;
	}

	int block = -1;
	u32 limit = g_sb.block_count;
	u32 max_blocks = FS_BLOCK_SIZE * 8u * FS_BITMAP_SECTORS;
	if (limit == 0 || limit > max_blocks) {
		limit = max_blocks;
	}
	for (u32 i = 1; i < limit; i++) {
		u32 byte_index = i / 8u;
		u32 bit_index = i % 8u;
		u8 mask = (u8)(1u << bit_index);
		if ((g_bitmap[byte_index] & mask) == 0) {
			block = (int)i;
			break;
		}
	}

	if (block < 0) {
		return false;
	}

	mark_block_used((u32)block);
	root->direct[0] = (u32)block;

	u8 empty[FS_BLOCK_SIZE];
	mem_set(empty, 0, FS_BLOCK_SIZE);
	if (!disk_write_lba(g_sb.data_lba + root->direct[0], empty, 1)) {
		return false;
	}

	return flush_bitmap() && flush_inodes();
}

static bool remove_entry_from_dir(Inode* dir, const char* filename) {
	if (!dir || dir->direct[0] == 0) return false;
	if (!disk_read_lba(g_sb.data_lba + dir->direct[0], g_block_scratch, 1)) return false;

	DirEntry* entries = (DirEntry*)g_block_scratch;
	u32 entry_count = FS_BLOCK_SIZE / sizeof(DirEntry);
	int target_idx = -1;
	for (u32 i = 0; i < entry_count; i++) {
		if (entries[i].used && str_equals(entries[i].name, filename)) {
			int inode = (int)entries[i].inode;
			if (inode <= 0 || inode >= (int)FS_MAX_INODES) return false;
			if (g_inodes[inode].used && g_inodes[inode].type == INODE_FILE) {
				target_idx = (int)i;
				break;
			}
		}
	}
	if (target_idx < 0) return false;

	int inode = (int)entries[target_idx].inode;
	if (inode <= 0 || inode >= (int)FS_MAX_INODES) return false;
	if (g_inodes[inode].type != INODE_FILE) return false;

	u32 block = g_inodes[inode].direct[0];
	if (block != 0 && block < g_sb.block_count) {
		mark_block_free(block);
		mem_set(g_block_scratch2, 0, FS_BLOCK_SIZE);
		if (!disk_write_lba(g_sb.data_lba + block, g_block_scratch2, 1)) return false;
	}

	mem_set((u8*)&entries[target_idx], 0, sizeof(DirEntry));
	mem_set((u8*)&g_inodes[inode], 0, sizeof(Inode));

	if (!disk_write_lba(g_sb.data_lba + dir->direct[0], g_block_scratch, 1)) return false;
	return flush_bitmap() && flush_inodes();
}

static bool remove_dir_contents(int dir_inode) {
	if (dir_inode <= 0 || dir_inode >= (int)FS_MAX_INODES) return false;
	Inode* dir = &g_inodes[dir_inode];
	if (dir->used == 0 || dir->type != INODE_DIR || dir->direct[0] == 0) return false;

	u8 dir_block[FS_BLOCK_SIZE];
	if (!disk_read_lba(g_sb.data_lba + dir->direct[0], dir_block, 1)) return false;

	u8 zero_block[FS_BLOCK_SIZE];
	mem_set(zero_block, 0, FS_BLOCK_SIZE);

	DirEntry* entries = (DirEntry*)dir_block;
	u32 entry_count = FS_BLOCK_SIZE / sizeof(DirEntry);
	for (u32 i = 0; i < entry_count; i++) {
		if (!entries[i].used) continue;
		int inode = (int)entries[i].inode;
		if (inode <= 0 || inode >= (int)FS_MAX_INODES) return false;

		if (g_inodes[inode].type == INODE_DIR) {
			if (!remove_dir_contents(inode)) return false;
		} else if (g_inodes[inode].type != INODE_FILE) {
			return false;
		}

		u32 block = g_inodes[inode].direct[0];
		if (block != 0 && block < g_sb.block_count) {
			mark_block_free(block);
			if (!disk_write_lba(g_sb.data_lba + block, zero_block, 1)) return false;
		}

		mem_set((u8*)&g_inodes[inode], 0, sizeof(Inode));
		mem_set((u8*)&entries[i], 0, sizeof(DirEntry));
	}

	if (!disk_write_lba(g_sb.data_lba + dir->direct[0], dir_block, 1)) return false;
	return true;
}

static bool remove_entry_from_dir_recursive(Inode* dir, const char* filename) {
	if (!dir || dir->direct[0] == 0) return false;

	u8 dir_block[FS_BLOCK_SIZE];
	if (!disk_read_lba(g_sb.data_lba + dir->direct[0], dir_block, 1)) return false;

	DirEntry* entries = (DirEntry*)dir_block;
	u32 entry_count = FS_BLOCK_SIZE / sizeof(DirEntry);
	int target_idx = -1;
	for (u32 i = 0; i < entry_count; i++) {
		if (entries[i].used && str_equals(entries[i].name, filename)) {
			int inode = (int)entries[i].inode;
			if (inode <= 0 || inode >= (int)FS_MAX_INODES) return false;
			if (g_inodes[inode].used && g_inodes[inode].type == INODE_DIR) {
				target_idx = (int)i;
				break;
			}
		}
	}
	if (target_idx < 0) {
		for (u32 i = 0; i < entry_count; i++) {
			if (entries[i].used && str_equals(entries[i].name, filename)) {
				int inode = (int)entries[i].inode;
				if (inode <= 0 || inode >= (int)FS_MAX_INODES) return false;
				if (g_inodes[inode].used && g_inodes[inode].type == INODE_FILE) {
					target_idx = (int)i;
					break;
				}
			}
		}
	}
	if (target_idx < 0) return false;

	int inode = (int)entries[target_idx].inode;
	if (inode <= 0 || inode >= (int)FS_MAX_INODES) return false;
	if (g_inodes[inode].type == INODE_DIR) {
		if (!remove_dir_contents(inode)) return false;
	} else if (g_inodes[inode].type != INODE_FILE) {
		return false;
	}

	u32 block = g_inodes[inode].direct[0];
	if (block != 0 && block < g_sb.block_count) {
		mark_block_free(block);
		u8 zero_block[FS_BLOCK_SIZE];
		mem_set(zero_block, 0, FS_BLOCK_SIZE);
		if (!disk_write_lba(g_sb.data_lba + block, zero_block, 1)) return false;
	}

	mem_set((u8*)&entries[target_idx], 0, sizeof(DirEntry));
	mem_set((u8*)&g_inodes[inode], 0, sizeof(Inode));

	if (!disk_write_lba(g_sb.data_lba + dir->direct[0], dir_block, 1)) return false;
	return flush_bitmap() && flush_inodes();
}

extern "C" bool fs_init() {
	if (!disk_read_lba(FS_SUPER_LBA, (u8*)&g_sb, 1)) {
		g_last_error = FS_ERR_IO;
		g_mounted = false;
		return false;
	}

	if (g_sb.magic != FS_MAGIC || g_sb.block_size != FS_BLOCK_SIZE) {
		g_last_error = FS_ERR_BAD_MAGIC;
		g_mounted = false;
		return false;
	}

	if (!disk_read_lba(g_sb.bitmap_lba, g_bitmap, FS_BITMAP_SECTORS)) {
		g_last_error = FS_ERR_IO;
		g_mounted = false;
		return false;
	}

	u8* inode_bytes = (u8*)g_inodes;
	if (!disk_read_lba(g_sb.inode_table_lba, inode_bytes, FS_INODE_TABLE_SECTORS)) {
		g_last_error = FS_ERR_IO;
		g_mounted = false;
		return false;
	}

	g_last_error = FS_ERR_NONE;
	g_mounted = true;
	return true;
}

extern "C" bool fs_format(u32 total_blocks, u32 inode_count) {
	if (inode_count > FS_MAX_INODES) {
		inode_count = FS_MAX_INODES;
	}

	u32 max_blocks = FS_BLOCK_SIZE * 8u * FS_BITMAP_SECTORS;
	if (total_blocks == 0) {
		u64 disk_sectors = 0;
		if (ata_get_sector_count(&disk_sectors) && disk_sectors > FS_DATA_LBA) {
			u64 usable = disk_sectors - FS_DATA_LBA;
			total_blocks = (usable > (u64)max_blocks) ? max_blocks : (u32)usable;
		} else {
			total_blocks = max_blocks;
		}
	} else if (total_blocks > max_blocks) {
		total_blocks = max_blocks;
	}

	g_sb.magic = FS_MAGIC;
	g_sb.version = FS_VERSION;
	g_sb.block_size = FS_BLOCK_SIZE;
	g_sb.inode_count = inode_count;
	g_sb.block_count = total_blocks;
	g_sb.inode_table_lba = FS_INODE_TABLE_LBA;
	g_sb.bitmap_lba = FS_BITMAP_LBA;
	g_sb.data_lba = FS_DATA_LBA;

	mem_set(g_bitmap, 0, FS_BLOCK_SIZE * FS_BITMAP_SECTORS);
	mem_set((u8*)g_inodes, 0, sizeof(g_inodes));

	g_inodes[0].used = 1;
	g_inodes[0].type = INODE_DIR;
	g_inodes[0].links = 1;
	g_inodes[0].size = 0;
	g_inodes[0].direct[0] = 0;

	if (!ensure_root_dir_block()) {
		return false;
	}

	if (!disk_write_lba(FS_SUPER_LBA, (const u8*)&g_sb, 1)) {
		return false;
	}
	if (!disk_write_lba(g_sb.bitmap_lba, g_bitmap, FS_BITMAP_SECTORS)) {
		return false;
	}
	if (!disk_write_lba(g_sb.inode_table_lba, (const u8*)g_inodes, FS_INODE_TABLE_SECTORS)) {
		return false;
	}

	if (!write_status_flag(0xFF)) {
		return false;
	}

	u32 default_hash = fnv1a_32("password123");
	u8 hash_buf[4];
	hash_buf[0] = (u8)(default_hash & 0xFF);
	hash_buf[1] = (u8)((default_hash >> 8) & 0xFF);
	hash_buf[2] = (u8)((default_hash >> 16) & 0xFF);
	hash_buf[3] = (u8)((default_hash >> 24) & 0xFF);
	if (!write_sys_file("passwd", hash_buf, 4)) {
		return false;
	}

	g_last_error = FS_ERR_NONE;
	g_mounted = true;
	return true;
}

extern "C" bool fs_list_root(DirEntry* out_entries, u32 max_entries, u32* out_count) {
	if (!g_mounted) {
		return false;
	}

	if (out_count) {
		*out_count = 0;
	}

	if (max_entries == 0) {
		return true;
	}

	Inode root = g_inodes[0];
	if (root.used == 0 || root.type != INODE_DIR || root.direct[0] == 0) {
		return true;
	}

	u8 block[FS_BLOCK_SIZE];
	if (!disk_read_lba(g_sb.data_lba + root.direct[0], block, 1)) {
		return false;
	}

	DirEntry* entries = (DirEntry*)block;
	u32 count = 0;
	u32 entry_count = FS_BLOCK_SIZE / sizeof(DirEntry);
	for (u32 i = 0; i < entry_count && count < max_entries; i++) {
		if (entries[i].used) {
			out_entries[count++] = entries[i];
		}
	}

	if (out_count) {
		*out_count = count;
	}
	return true;
}

extern "C" bool fs_list_dir(const char* dirname, DirEntry* out_entries, u32 max_entries, u32* out_count) {
	if (!g_mounted) return false;
	if (out_count) *out_count = 0;
	if (max_entries == 0) return true;

	Inode* root = &g_inodes[0];
	if (root->used == 0 || root->direct[0] == 0) return false;

	if (!disk_read_lba(g_sb.data_lba + root->direct[0], g_block_scratch, 1)) return false;

	DirEntry* root_entries = (DirEntry*)g_block_scratch;
	u32 entry_count = FS_BLOCK_SIZE / sizeof(DirEntry);
	int dir_entry = find_dir_entry(dirname, root_entries, entry_count, g_inodes, INODE_DIR);
	if (dir_entry < 0) return false;
	int dir_inode = (int)root_entries[dir_entry].inode;
	if (dir_inode < 0 || dir_inode >= (int)FS_MAX_INODES) return false;
	if (g_inodes[dir_inode].direct[0] == 0) return false;

	if (!disk_read_lba(g_sb.data_lba + g_inodes[dir_inode].direct[0], g_block_scratch2, 1)) return false;

	DirEntry* entries = (DirEntry*)g_block_scratch2;
	u32 count = 0;
	for (u32 i = 0; i < entry_count && count < max_entries; i++) {
		if (entries[i].used) {
			out_entries[count++] = entries[i];
		}
	}
	if (out_count) *out_count = count;
	return true;
}

extern "C" FsError fs_last_error() {
	return g_last_error;
}

extern "C" bool fs_is_formatted() {
	Superblock sb;
	if (!disk_read_lba(FS_SUPER_LBA, (u8*)&sb, 1)) {
		return false;
	}

	if (sb.magic != FS_MAGIC || sb.block_size != FS_BLOCK_SIZE) {
		return false;
	}

	if (!disk_read_lba(sb.inode_table_lba, (u8*)g_inode_scratch, FS_INODE_TABLE_SECTORS)) {
		return false;
	}

	Inode root = g_inode_scratch[0];
	if (root.used == 0 || root.type != INODE_DIR || root.direct[0] == 0) {
		return false;
	}
	if (root.direct[0] >= sb.block_count) {
		return false;
	}

	if (!disk_read_lba(sb.data_lba + root.direct[0], g_block_scratch, 1)) {
		return false;
	}

	DirEntry* entries = (DirEntry*)g_block_scratch;
	u32 entry_count = FS_BLOCK_SIZE / sizeof(DirEntry);
	int sys_entry = find_dir_entry("sys", entries, entry_count, g_inode_scratch, INODE_DIR);
	if (sys_entry < 0) {
		return false;
	}

	int sys_inode = (int)entries[sys_entry].inode;
	if (sys_inode < 0 || sys_inode >= (int)FS_MAX_INODES) {
		return false;
	}
	Inode sys_dir = g_inode_scratch[sys_inode];
	if (sys_dir.used == 0 || sys_dir.type != INODE_DIR || sys_dir.direct[0] == 0) {
		return false;
	}
	if (sys_dir.direct[0] >= sb.block_count) {
		return false;
	}

	if (!disk_read_lba(sb.data_lba + sys_dir.direct[0], g_block_scratch2, 1)) {
		return false;
	}

	DirEntry* sys_entries = (DirEntry*)g_block_scratch2;
	int status_entry = find_dir_entry("status", sys_entries, entry_count, g_inode_scratch, INODE_FILE);
	if (status_entry < 0) {
		return false;
	}

	int status_inode = (int)sys_entries[status_entry].inode;
	if (status_inode < 0 || status_inode >= (int)FS_MAX_INODES) {
		return false;
	}
	Inode status = g_inode_scratch[status_inode];
	if (status.used == 0 || status.direct[0] == 0) {
		return false;
	}
	if (status.direct[0] >= sb.block_count) {
		return false;
	}

	if (!disk_read_lba(sb.data_lba + status.direct[0], g_block_scratch, 1)) {
		return false;
	}

	return g_block_scratch[0] == 0xFF;
}

extern "C" int find_free_inode() {
	if (!g_mounted) {
		return -1;
	}

	u32 limit = g_sb.inode_count;
	if (limit == 0 || limit > FS_MAX_INODES) {
		limit = FS_MAX_INODES;
	}

	for (u32 i = 1; i < limit; i++) {
		if (g_inodes[i].used == 0) {
			return (int)i;
		}
	}

	return -1;
}

extern "C" int find_free_block() {
	if (!g_mounted) {
		return -1;
	}

	u32 limit = g_sb.block_count;
	u32 max_blocks = FS_BLOCK_SIZE * 8u * FS_BITMAP_SECTORS;
	if (limit == 0 || limit > max_blocks) {
		limit = max_blocks;
	}

	for (u32 i = 1; i < limit; i++) {
		u32 byte_index = i / 8u;
		u32 bit_index = i % 8u;
		u8 mask = (u8)(1u << bit_index);
		if ((g_bitmap[byte_index] & mask) == 0) {
			return (int)i;
		}
	}

	return -1;
}

extern "C" bool fs_mkdir(const char* name) {
	if (!g_mounted) {
		return false;
	}
	if (!name || name[0] == '\0') {
		return false;
	}
	if (str_len(name) >= sizeof(DirEntry::name)) {
		return false;
	}

	if (!ensure_root_dir_block()) {
		return false;
	}

	int inode = find_free_inode();
	if (inode < 0) {
		return false;
	}

	int block = find_free_block();
	if (block < 0) {
		return false;
	}

	Inode* root = &g_inodes[0];
	u8 dir_block[FS_BLOCK_SIZE];
	if (!disk_read_lba(g_sb.data_lba + root->direct[0], dir_block, 1)) {
		return false;
	}

	DirEntry* entries = (DirEntry*)dir_block;
	u32 entry_count = FS_BLOCK_SIZE / sizeof(DirEntry);
	int free_slot = -1;
	for (u32 i = 0; i < entry_count; i++) {
		if (entries[i].used) {
			if (str_equals(entries[i].name, name)) {
				int inode = (int)entries[i].inode;
				if (inode < 0 || inode >= (int)FS_MAX_INODES) return false;
				if (g_inodes[inode].used && g_inodes[inode].type == INODE_DIR) {
					return false;
				}
			}
		} else if (free_slot < 0) {
			free_slot = (int)i;
		}
	}

	if (free_slot < 0) {
		return false;
	}

	mark_block_used((u32)block);
	g_inodes[inode].used = 1;
	g_inodes[inode].type = INODE_DIR;
	g_inodes[inode].links = 1;
	g_inodes[inode].size = 0;
	g_inodes[inode].direct[0] = (u32)block;

	u8 empty_dir[FS_BLOCK_SIZE];
	mem_set(empty_dir, 0, FS_BLOCK_SIZE);
	if (!disk_write_lba(g_sb.data_lba + (u32)block, empty_dir, 1)) {
		return false;
	}

	mem_set((u8*)&entries[free_slot], 0, sizeof(DirEntry));
	entries[free_slot].used = 1;
	entries[free_slot].inode = (u8)inode;
	mem_copy((u8*)entries[free_slot].name, (const u8*)name, str_len(name));

	if (!disk_write_lba(g_sb.data_lba + root->direct[0], dir_block, 1)) {
		return false;
	}

	if (!flush_bitmap()) {
		return false;
	}

	if (!flush_inodes()) {
		return false;
	}

	return true;
}

extern "C" bool fs_is_dir(const char* path) {
	if (!g_mounted || !path || path[0] == '\0') return false;

	Inode* root = &g_inodes[0];
	if (root->used == 0 || root->direct[0] == 0) return false;
	if (!disk_read_lba(g_sb.data_lba + root->direct[0], g_block_scratch, 1)) return false;
	DirEntry* root_entries = (DirEntry*)g_block_scratch;
	u32 ec = FS_BLOCK_SIZE / sizeof(DirEntry);

	u32 sep = 0;
	while (path[sep] && path[sep] != '/') sep++;

	if (path[sep] == '\0') {
		return find_dir_entry(path, root_entries, ec, g_inodes, INODE_DIR) >= 0;
	}

	char parent[30];
	u32 plen = sep < 29 ? sep : 29;
	mem_copy((u8*)parent, (const u8*)path, plen);
	parent[plen] = '\0';
	const char* child = path + sep + 1;
	if (child[0] == '\0') return false;

	int parent_idx = find_dir_entry(parent, root_entries, ec, g_inodes, INODE_DIR);
	if (parent_idx < 0) return false;
	int parent_inode = (int)root_entries[parent_idx].inode;
	if (parent_inode < 0 || parent_inode >= (int)FS_MAX_INODES) return false;
	if (g_inodes[parent_inode].direct[0] == 0) return false;

	if (!disk_read_lba(g_sb.data_lba + g_inodes[parent_inode].direct[0], g_block_scratch2, 1)) return false;
	DirEntry* parent_entries = (DirEntry*)g_block_scratch2;
	return find_dir_entry(child, parent_entries, ec, g_inodes, INODE_DIR) >= 0;
}

extern "C" bool fs_copy_path(const char* src_path, const char* dst_path) {
	if (!g_mounted || !src_path || !dst_path) return false;
	if (src_path[0] == '\0' || dst_path[0] == '\0') return false;

	int src_inode = -1;
	{
		u32 sep = 0;
		while (src_path[sep] && src_path[sep] != '/') sep++;

		Inode* root = &g_inodes[0];
		if (root->used == 0 || root->direct[0] == 0) return false;
		if (!disk_read_lba(g_sb.data_lba + root->direct[0], g_block_scratch, 1)) return false;
		DirEntry* root_entries = (DirEntry*)g_block_scratch;
		u32 ec = FS_BLOCK_SIZE / sizeof(DirEntry);

		if (src_path[sep] == '\0') {
			int idx = find_dir_entry(src_path, root_entries, ec, g_inodes, INODE_FILE);
			if (idx < 0) return false;
			src_inode = (int)root_entries[idx].inode;
		} else {
			char src_dir[30];
			u32 dlen = sep < 29 ? sep : 29;
			mem_copy((u8*)src_dir, (const u8*)src_path, dlen);
			src_dir[dlen] = '\0';
			const char* src_name = src_path + sep + 1;
			if (src_name[0] == '\0') return false;

			int dir_idx = find_dir_entry(src_dir, root_entries, ec, g_inodes, INODE_DIR);
			if (dir_idx < 0) return false;
			int dir_inode = (int)root_entries[dir_idx].inode;
			if (dir_inode < 0 || dir_inode >= (int)FS_MAX_INODES) return false;
			if (g_inodes[dir_inode].direct[0] == 0) return false;

			if (!disk_read_lba(g_sb.data_lba + g_inodes[dir_inode].direct[0], g_block_scratch2, 1)) return false;
			DirEntry* dir_entries = (DirEntry*)g_block_scratch2;
			int file_idx = find_dir_entry(src_name, dir_entries, ec, g_inodes, INODE_FILE);
			if (file_idx < 0) return false;
			src_inode = (int)dir_entries[file_idx].inode;
		}
	}

	if (src_inode < 0 || src_inode >= (int)FS_MAX_INODES) return false;
	if (g_inodes[src_inode].direct[0] == 0) return false;

	u8 file_data[FS_BLOCK_SIZE];
	if (!disk_read_lba(g_sb.data_lba + g_inodes[src_inode].direct[0], file_data, 1)) return false;
	u32 file_size = g_inodes[src_inode].size;

	if (!ensure_root_dir_block()) return false;

	u32 dst_sep = 0;
	while (dst_path[dst_sep] && dst_path[dst_sep] != '/') dst_sep++;

	int dst_dir_inode = 0;
	const char* dst_name = dst_path;

	if (dst_path[dst_sep] == '/') {
		char dst_dir[30];
		u32 dlen = dst_sep < 29 ? dst_sep : 29;
		mem_copy((u8*)dst_dir, (const u8*)dst_path, dlen);
		dst_dir[dlen] = '\0';
		dst_name = dst_path + dst_sep + 1;
		if (dst_name[0] == '\0') return false;

		Inode* root = &g_inodes[0];
		if (!disk_read_lba(g_sb.data_lba + root->direct[0], g_block_scratch, 1)) return false;
		DirEntry* root_entries = (DirEntry*)g_block_scratch;
		u32 ec = FS_BLOCK_SIZE / sizeof(DirEntry);
		int dir_idx = find_dir_entry(dst_dir, root_entries, ec, g_inodes, INODE_DIR);
		if (dir_idx < 0) return false;
		dst_dir_inode = (int)root_entries[dir_idx].inode;
		if (dst_dir_inode < 0 || dst_dir_inode >= (int)FS_MAX_INODES) return false;
	} else {
		Inode* root = &g_inodes[0];
		if (!disk_read_lba(g_sb.data_lba + root->direct[0], g_block_scratch, 1)) return false;
		DirEntry* root_entries = (DirEntry*)g_block_scratch;
		u32 ec = FS_BLOCK_SIZE / sizeof(DirEntry);
		int dir_idx = find_dir_entry(dst_path, root_entries, ec, g_inodes, INODE_DIR);
		if (dir_idx >= 0) {
			dst_dir_inode = (int)root_entries[dir_idx].inode;
			u32 s = 0;
			while (src_path[s] && src_path[s] != '/') s++;
			dst_name = (src_path[s] == '/') ? src_path + s + 1 : src_path;
		}
	}

	if (str_len(dst_name) == 0 || str_len(dst_name) >= sizeof(DirEntry::name)) return false;

	Inode* dst_dir = &g_inodes[dst_dir_inode];
	if (dst_dir->used == 0 || dst_dir->direct[0] == 0) return false;
	if (!disk_read_lba(g_sb.data_lba + dst_dir->direct[0], g_block_scratch2, 1)) return false;
	DirEntry* dst_entries = (DirEntry*)g_block_scratch2;
	u32 entry_count = FS_BLOCK_SIZE / sizeof(DirEntry);

	int dst_file_inode = -1;
	int free_slot = -1;
	for (u32 i = 0; i < entry_count; i++) {
		if (dst_entries[i].used) {
			if (str_equals(dst_entries[i].name, dst_name)) {
				int idx = (int)dst_entries[i].inode;
				if (idx >= 0 && idx < (int)FS_MAX_INODES &&
				    g_inodes[idx].used && g_inodes[idx].type == INODE_FILE) {
					dst_file_inode = idx;
					break;
				}
			}
		} else if (free_slot < 0) {
			free_slot = (int)i;
		}
	}

	if (dst_file_inode < 0) {
		if (free_slot < 0) return false;
		int new_inode = alloc_inode_raw();
		if (new_inode < 0) return false;
		int new_block = alloc_block_raw();
		if (new_block < 0) return false;
		mark_block_used((u32)new_block);
		g_inodes[new_inode].used = 1;
		g_inodes[new_inode].type = INODE_FILE;
		g_inodes[new_inode].links = 1;
		g_inodes[new_inode].size = file_size;
		g_inodes[new_inode].direct[0] = (u32)new_block;
		mem_set((u8*)&dst_entries[free_slot], 0, sizeof(DirEntry));
		dst_entries[free_slot].used = 1;
		dst_entries[free_slot].inode = (u8)new_inode;
		mem_copy((u8*)dst_entries[free_slot].name, (const u8*)dst_name, str_len(dst_name));
		dst_file_inode = new_inode;
	} else {
		g_inodes[dst_file_inode].size = file_size;
	}

	if (!disk_write_lba(g_sb.data_lba + dst_dir->direct[0], g_block_scratch2, 1)) return false;
	if (!disk_write_lba(g_sb.data_lba + g_inodes[dst_file_inode].direct[0], file_data, 1)) return false;

	return flush_bitmap() && flush_inodes();
}

extern "C" bool fs_mkdir_path(const char* path) {
	if (!g_mounted || !path || path[0] == '\0') return false;

	u32 sep = 0;
	while (path[sep] && path[sep] != '/') sep++;

	if (path[sep] == '\0') {
		return fs_mkdir(path);
	}

	char parent_name[30];
	u32 plen = sep < 29 ? sep : 29;
	mem_copy((u8*)parent_name, (const u8*)path, plen);
	parent_name[plen] = '\0';
	const char* newdir = path + sep + 1;
	if (newdir[0] == '\0') return false;
	for (u32 i = 0; newdir[i]; i++) {
		if (newdir[i] == '/') return false;
	}
	if (str_len(newdir) >= sizeof(DirEntry::name)) return false;

	if (!ensure_root_dir_block()) return false;

	Inode* root = &g_inodes[0];
	if (!disk_read_lba(g_sb.data_lba + root->direct[0], g_block_scratch, 1)) return false;

	DirEntry* root_entries = (DirEntry*)g_block_scratch;
	u32 entry_count = FS_BLOCK_SIZE / sizeof(DirEntry);
	int dir_entry = find_dir_entry(parent_name, root_entries, entry_count, g_inodes, INODE_DIR);
	if (dir_entry < 0) return false;

	int parent_inode = (int)root_entries[dir_entry].inode;
	if (parent_inode < 0 || parent_inode >= (int)FS_MAX_INODES) return false;
	if (g_inodes[parent_inode].direct[0] == 0) return false;

	if (!disk_read_lba(g_sb.data_lba + g_inodes[parent_inode].direct[0], g_block_scratch2, 1)) return false;

	DirEntry* parent_entries = (DirEntry*)g_block_scratch2;
	int free_slot = -1;
	for (u32 i = 0; i < entry_count; i++) {
		if (parent_entries[i].used) {
			if (str_equals(parent_entries[i].name, newdir)) {
				int idx = (int)parent_entries[i].inode;
				if (idx >= 0 && idx < (int)FS_MAX_INODES &&
				    g_inodes[idx].used && g_inodes[idx].type == INODE_DIR) {
					return false;
				}
			}
		} else if (free_slot < 0) {
			free_slot = (int)i;
		}
	}
	if (free_slot < 0) return false;

	int new_inode = alloc_inode_raw();
	if (new_inode < 0) return false;
	int new_block = alloc_block_raw();
	if (new_block < 0) return false;

	mark_block_used((u32)new_block);
	g_inodes[new_inode].used = 1;
	g_inodes[new_inode].type = INODE_DIR;
	g_inodes[new_inode].links = 1;
	g_inodes[new_inode].size = 0;
	g_inodes[new_inode].direct[0] = (u32)new_block;

	u8 empty_dir[FS_BLOCK_SIZE];
	mem_set(empty_dir, 0, FS_BLOCK_SIZE);
	if (!disk_write_lba(g_sb.data_lba + (u32)new_block, empty_dir, 1)) return false;

	mem_set((u8*)&parent_entries[free_slot], 0, sizeof(DirEntry));
	parent_entries[free_slot].used = 1;
	parent_entries[free_slot].inode = (u8)new_inode;
	mem_copy((u8*)parent_entries[free_slot].name, (const u8*)newdir, str_len(newdir));

	if (!disk_write_lba(g_sb.data_lba + g_inodes[parent_inode].direct[0], g_block_scratch2, 1)) return false;

	return flush_bitmap() && flush_inodes();
}

extern "C" bool fs_write(const char* name, const char* data) {
	if (!g_mounted) return false;
	if (!name || name[0] == '\0') return false;
	if (str_len(name) >= sizeof(DirEntry::name)) return false;
	if (!ensure_root_dir_block()) return false;

	Inode* root = &g_inodes[0];
	u8 dir_block[FS_BLOCK_SIZE];
	if (!disk_read_lba(g_sb.data_lba + root->direct[0], dir_block, 1)) return false;

	DirEntry* entries = (DirEntry*)dir_block;
	u32 entry_count = FS_BLOCK_SIZE / sizeof(DirEntry);
	int target_inode = -1;
	int free_slot = -1;

	for (u32 i = 0; i < entry_count; i++) {
		if (entries[i].used) {
			if (str_equals(entries[i].name, name)) {
				int inode = (int)entries[i].inode;
				if (inode < 0 || inode >= (int)FS_MAX_INODES) return false;
				if (g_inodes[inode].used && g_inodes[inode].type == INODE_FILE) {
					target_inode = inode;
					break;
				}
			}
		} else if (free_slot < 0) {
			free_slot = (int)i;
		}
	}

	if (target_inode < 0) {
		if (free_slot < 0) return false;
		int new_inode = find_free_inode();
		if (new_inode < 0) return false;
		int new_block = find_free_block();
		if (new_block < 0) return false;
		mark_block_used((u32)new_block);
		g_inodes[new_inode].used = 1;
		g_inodes[new_inode].type = INODE_FILE;
		g_inodes[new_inode].links = 1;
		g_inodes[new_inode].direct[0] = (u32)new_block;
		mem_set((u8*)&entries[free_slot], 0, sizeof(DirEntry));
		entries[free_slot].used = 1;
		entries[free_slot].inode = (u8)new_inode;
		mem_copy((u8*)entries[free_slot].name, (const u8*)name, str_len(name));
		target_inode = new_inode;
	}

	u32 len = str_len(data);
	if (len > FS_BLOCK_SIZE) len = FS_BLOCK_SIZE;
	g_inodes[target_inode].size = len;

	if (!disk_write_lba(g_sb.data_lba + root->direct[0], dir_block, 1)) return false;

	u8 file_block[FS_BLOCK_SIZE];
	mem_set(file_block, 0, FS_BLOCK_SIZE);
	mem_copy(file_block, (const u8*)data, len);
	if (!disk_write_lba(g_sb.data_lba + g_inodes[target_inode].direct[0], file_block, 1)) return false;

	return flush_bitmap() && flush_inodes();
}

extern "C" bool fs_write_path(const char* path, const char* data) {
	if (!g_mounted || !path || path[0] == '\0') return false;

	u32 sep = 0;
	while (path[sep] && path[sep] != '/') sep++;

	if (path[sep] == '\0') {
		return fs_write(path, data);
	}

	char dirname[30];
	u32 dlen = sep < 29 ? sep : 29;
	mem_copy((u8*)dirname, (const u8*)path, dlen);
	dirname[dlen] = '\0';
	const char* filename = path + sep + 1;
	if (filename[0] == '\0') return false;

	Inode* root = &g_inodes[0];
	if (root->used == 0 || root->direct[0] == 0) return false;
	if (!disk_read_lba(g_sb.data_lba + root->direct[0], g_block_scratch, 1)) return false;

	DirEntry* root_entries = (DirEntry*)g_block_scratch;
	u32 entry_count = FS_BLOCK_SIZE / sizeof(DirEntry);
	int dir_entry = find_dir_entry(dirname, root_entries, entry_count, g_inodes, INODE_DIR);
	if (dir_entry < 0) return false;
	int dir_inode = (int)root_entries[dir_entry].inode;
	if (dir_inode < 0 || dir_inode >= (int)FS_MAX_INODES) return false;
	if (g_inodes[dir_inode].direct[0] == 0) return false;

	if (!disk_read_lba(g_sb.data_lba + g_inodes[dir_inode].direct[0], g_block_scratch2, 1)) return false;
	DirEntry* dir_entries = (DirEntry*)g_block_scratch2;
	int file_inode = -1;
	int free_slot = -1;
	for (u32 i = 0; i < entry_count; i++) {
		if (dir_entries[i].used) {
			if (str_equals(dir_entries[i].name, filename)) {
				int inode = (int)dir_entries[i].inode;
				if (inode < 0 || inode >= (int)FS_MAX_INODES) return false;
				if (g_inodes[inode].used && g_inodes[inode].type == INODE_FILE) {
					file_inode = inode;
					break;
				}
			}
		} else if (free_slot < 0) {
			free_slot = (int)i;
		}
	}

	if (file_inode < 0) {
		if (free_slot < 0) return false;
		int new_inode = find_free_inode();
		if (new_inode < 0) return false;
		int new_block = find_free_block();
		if (new_block < 0) return false;
		mark_block_used((u32)new_block);
		g_inodes[new_inode].used = 1;
		g_inodes[new_inode].type = INODE_FILE;
		g_inodes[new_inode].links = 1;
		g_inodes[new_inode].direct[0] = (u32)new_block;
		mem_set((u8*)&dir_entries[free_slot], 0, sizeof(DirEntry));
		dir_entries[free_slot].used = 1;
		dir_entries[free_slot].inode = (u8)new_inode;
		mem_copy((u8*)dir_entries[free_slot].name, (const u8*)filename, str_len(filename));
		file_inode = new_inode;
	}

	u32 len = str_len(data);
	if (len > FS_BLOCK_SIZE) len = FS_BLOCK_SIZE;
	g_inodes[file_inode].size = len;

	if (!disk_write_lba(g_sb.data_lba + g_inodes[dir_inode].direct[0], g_block_scratch2, 1)) return false;

	u8 file_block[FS_BLOCK_SIZE];
	mem_set(file_block, 0, FS_BLOCK_SIZE);
	mem_copy(file_block, (const u8*)data, len);
	if (!disk_write_lba(g_sb.data_lba + g_inodes[file_inode].direct[0], file_block, 1)) return false;

	return flush_bitmap() && flush_inodes();
}

extern "C" bool fs_rename(const char* src_path, const char* dst_path) {
	if (!g_mounted || !src_path || !dst_path) return false;
	if (src_path[0] == '\0' || dst_path[0] == '\0') return false;

	u32 entry_count = FS_BLOCK_SIZE / sizeof(DirEntry);

	char src_dirname[30];
	u32 src_sep = 0;
	while (src_path[src_sep] && src_path[src_sep] != '/') src_sep++;
	const char* src_name = src_path;
	int src_dir_inode = 0;
	if (src_path[src_sep] == '/') {
		if (src_sep == 0) return false;
		u32 copy_len = src_sep < 29 ? src_sep : 29;
		mem_copy((u8*)src_dirname, (const u8*)src_path, copy_len);
		src_dirname[copy_len] = '\0';
		src_name = src_path + src_sep + 1;
		if (src_name[0] == '\0') return false;

		u8 root_block[FS_BLOCK_SIZE];
		if (!load_dir_block(0, root_block)) return false;
		DirEntry* root_entries = (DirEntry*)root_block;
		int dir_entry = find_dir_entry(src_dirname, root_entries, entry_count, g_inodes, INODE_DIR);
		if (dir_entry < 0) return false;
		src_dir_inode = (int)root_entries[dir_entry].inode;
	}

	if (str_len(src_name) >= sizeof(DirEntry::name)) return false;

	u8 src_block[FS_BLOCK_SIZE];
	if (!load_dir_block(src_dir_inode, src_block)) return false;
	DirEntry* src_entries = (DirEntry*)src_block;
	int src_entry = find_dir_entry(src_name, src_entries, entry_count, g_inodes, INODE_FILE);
	u8 src_type = INODE_FILE;
	if (src_entry < 0) {
		src_entry = find_dir_entry(src_name, src_entries, entry_count, g_inodes, INODE_DIR);
		src_type = INODE_DIR;
	}
	if (src_entry < 0) return false;

	int src_inode = (int)src_entries[src_entry].inode;
	if (src_inode < 0 || src_inode >= (int)FS_MAX_INODES) return false;

	char dst_dirname[30];
	u32 dst_sep = 0;
	while (dst_path[dst_sep] && dst_path[dst_sep] != '/') dst_sep++;
	const char* dst_name = dst_path;
	int dst_dir_inode = 0;
	bool dst_is_dir = false;

	if (dst_path[dst_sep] == '/') {
		if (dst_sep == 0) return false;
		u32 copy_len = dst_sep < 29 ? dst_sep : 29;
		mem_copy((u8*)dst_dirname, (const u8*)dst_path, copy_len);
		dst_dirname[copy_len] = '\0';
		dst_name = dst_path + dst_sep + 1;

		u8 root_block[FS_BLOCK_SIZE];
		if (!load_dir_block(0, root_block)) return false;
		DirEntry* root_entries = (DirEntry*)root_block;
		int dir_entry = find_dir_entry(dst_dirname, root_entries, entry_count, g_inodes, INODE_DIR);
		if (dir_entry < 0) return false;
		dst_dir_inode = (int)root_entries[dir_entry].inode;

		if (dst_name[0] == '\0') {
			dst_name = src_name;
			dst_is_dir = true;
		}
	} else {
		u8 root_block[FS_BLOCK_SIZE];
		if (!load_dir_block(0, root_block)) return false;
		DirEntry* root_entries = (DirEntry*)root_block;
		int dir_entry = find_dir_entry(dst_path, root_entries, entry_count, g_inodes, INODE_DIR);
		if (dir_entry >= 0) {
			dst_dir_inode = (int)root_entries[dir_entry].inode;
			dst_name = src_name;
			dst_is_dir = true;
		} else {
			dst_dir_inode = 0;
			dst_name = dst_path;
		}
	}

	if (dst_name[0] == '\0') return false;
	if (str_len(dst_name) >= sizeof(DirEntry::name)) return false;

	if (dst_is_dir && src_type == INODE_DIR && dst_dir_inode == src_inode) return true;

	u8 dst_block[FS_BLOCK_SIZE];
	if (!load_dir_block(dst_dir_inode, dst_block)) return false;
	DirEntry* dst_entries = (DirEntry*)dst_block;

	if (dst_dir_inode == src_dir_inode && str_equals(dst_name, src_name)) return true;
	if (find_dir_entry(dst_name, dst_entries, entry_count, g_inodes, src_type) >= 0) return false;

	if (dst_dir_inode == src_dir_inode) {
		mem_set((u8*)src_entries[src_entry].name, 0, sizeof(DirEntry::name));
		mem_copy((u8*)src_entries[src_entry].name, (const u8*)dst_name, str_len(dst_name));
		return write_dir_block(src_dir_inode, (const u8*)src_block);
	}

	int free_slot = -1;
	for (u32 i = 0; i < entry_count; i++) {
		if (!dst_entries[i].used) { free_slot = (int)i; break; }
	}
	if (free_slot < 0) return false;

	mem_set((u8*)&dst_entries[free_slot], 0, sizeof(DirEntry));
	dst_entries[free_slot].used = 1;
	dst_entries[free_slot].inode = (u8)src_inode;
	mem_copy((u8*)dst_entries[free_slot].name, (const u8*)dst_name, str_len(dst_name));

	mem_set((u8*)&src_entries[src_entry], 0, sizeof(DirEntry));

	if (!write_dir_block(src_dir_inode, (const u8*)src_block)) return false;
	if (!write_dir_block(dst_dir_inode, (const u8*)dst_block)) return false;
	return true;
}

extern "C" bool fs_remove(const char* name) {
	if (!g_mounted) return false;
	if (!name || name[0] == '\0') return false;
	if (str_len(name) >= sizeof(DirEntry::name)) return false;

	Inode* root = &g_inodes[0];
	if (root->used == 0 || root->direct[0] == 0) return false;
	return remove_entry_from_dir(root, name);
}

extern "C" bool fs_remove_path(const char* path) {
	if (!g_mounted || !path || path[0] == '\0') return false;

	u32 sep = 0;
	while (path[sep] && path[sep] != '/') sep++;

	if (path[sep] == '\0') {
		return fs_remove(path);
	}

	char dirname[30];
	u32 dlen = sep < 29 ? sep : 29;
	mem_copy((u8*)dirname, (const u8*)path, dlen);
	dirname[dlen] = '\0';
	const char* filename = path + sep + 1;
	if (filename[0] == '\0') return false;

	Inode* root = &g_inodes[0];
	if (root->used == 0 || root->direct[0] == 0) return false;
	if (!disk_read_lba(g_sb.data_lba + root->direct[0], g_block_scratch, 1)) return false;

	DirEntry* root_entries = (DirEntry*)g_block_scratch;
	u32 entry_count = FS_BLOCK_SIZE / sizeof(DirEntry);
	int dir_entry = find_dir_entry(dirname, root_entries, entry_count, g_inodes, INODE_DIR);
	if (dir_entry < 0) return false;
	int dir_inode = (int)root_entries[dir_entry].inode;
	if (dir_inode < 0 || dir_inode >= (int)FS_MAX_INODES) return false;

	return remove_entry_from_dir(&g_inodes[dir_inode], filename);
}

extern "C" bool fs_remove_path_recursive(const char* path) {
	if (!g_mounted || !path || path[0] == '\0') return false;

	u32 sep = 0;
	while (path[sep] && path[sep] != '/') sep++;

	if (path[sep] == '\0') {
		Inode* root = &g_inodes[0];
		if (root->used == 0 || root->direct[0] == 0) return false;
		return remove_entry_from_dir_recursive(root, path);
	}

	char dirname[30];
	u32 dlen = sep < 29 ? sep : 29;
	mem_copy((u8*)dirname, (const u8*)path, dlen);
	dirname[dlen] = '\0';
	const char* filename = path + sep + 1;
	if (filename[0] == '\0') return false;

	Inode* root = &g_inodes[0];
	if (root->used == 0 || root->direct[0] == 0) return false;
	if (!disk_read_lba(g_sb.data_lba + root->direct[0], g_block_scratch, 1)) return false;

	DirEntry* root_entries = (DirEntry*)g_block_scratch;
	u32 entry_count = FS_BLOCK_SIZE / sizeof(DirEntry);
	int dir_entry = find_dir_entry(dirname, root_entries, entry_count, g_inodes, INODE_DIR);
	if (dir_entry < 0) return false;
	int dir_inode = (int)root_entries[dir_entry].inode;
	if (dir_inode < 0 || dir_inode >= (int)FS_MAX_INODES) return false;

	return remove_entry_from_dir_recursive(&g_inodes[dir_inode], filename);
}

extern "C" bool fs_read(const char* name, char* out, u32 maxlen, u32* out_len) {
	if (!g_mounted) return false;
	if (!name || name[0] == '\0') return false;

	Inode* root = &g_inodes[0];
	if (root->used == 0 || root->direct[0] == 0) return false;

	if (!disk_read_lba(g_sb.data_lba + root->direct[0], g_block_scratch, 1)) return false;

	DirEntry* entries = (DirEntry*)g_block_scratch;
	u32 entry_count = FS_BLOCK_SIZE / sizeof(DirEntry);
	int target_entry = find_dir_entry(name, entries, entry_count, g_inodes, INODE_FILE);
	if (target_entry < 0) return false;
	int target_inode = (int)entries[target_entry].inode;
	if (g_inodes[target_inode].direct[0] == 0) return false;

	if (!disk_read_lba(g_sb.data_lba + g_inodes[target_inode].direct[0], g_block_scratch2, 1)) return false;

	u32 sz = g_inodes[target_inode].size;
	if (sz > maxlen) sz = maxlen;
	mem_copy((u8*)out, g_block_scratch2, sz);
	if (out_len) *out_len = sz;
	return true;
}

extern "C" bool fs_read_path(const char* path, char* out, u32 maxlen, u32* out_len) {
	if (!g_mounted || !path || path[0] == '\0') return false;

	u32 sep = 0;
	while (path[sep] && path[sep] != '/') sep++;

	if (path[sep] == '\0') {
		return fs_read(path, out, maxlen, out_len);
	}

	char dirname[30];
	u32 dlen = sep < 29 ? sep : 29;
	mem_copy((u8*)dirname, (const u8*)path, dlen);
	dirname[dlen] = '\0';
	const char* filename = path + sep + 1;
	if (filename[0] == '\0') return false;

	Inode* root = &g_inodes[0];
	if (root->used == 0 || root->direct[0] == 0) return false;
	if (!disk_read_lba(g_sb.data_lba + root->direct[0], g_block_scratch, 1)) return false;

	DirEntry* root_entries = (DirEntry*)g_block_scratch;
	u32 entry_count = FS_BLOCK_SIZE / sizeof(DirEntry);
	int dir_entry = find_dir_entry(dirname, root_entries, entry_count, g_inodes, INODE_DIR);
	if (dir_entry < 0) return false;
	int dir_inode = (int)root_entries[dir_entry].inode;
	if (dir_inode < 0 || dir_inode >= (int)FS_MAX_INODES) return false;
	if (g_inodes[dir_inode].direct[0] == 0) return false;

	if (!disk_read_lba(g_sb.data_lba + g_inodes[dir_inode].direct[0], g_block_scratch2, 1)) return false;
	DirEntry* dir_entries = (DirEntry*)g_block_scratch2;
	int file_entry = find_dir_entry(filename, dir_entries, entry_count, g_inodes, INODE_FILE);
	if (file_entry < 0) return false;
	int file_inode = (int)dir_entries[file_entry].inode;
	if (g_inodes[file_inode].direct[0] == 0) return false;

	if (!disk_read_lba(g_sb.data_lba + g_inodes[file_inode].direct[0], g_block_scratch, 1)) return false;
	u32 sz = g_inodes[file_inode].size;
	if (sz > maxlen) sz = maxlen;
	mem_copy((u8*)out, g_block_scratch, sz);
	if (out_len) *out_len = sz;
	return true;
}

extern "C" u32 fs_hash_password(const char* s) {
	return fnv1a_32(s);
}

extern "C" bool fs_write_passwd(u32 hash) {
	u8 buf[4];
	buf[0] = (u8)(hash & 0xFF);
	buf[1] = (u8)((hash >> 8) & 0xFF);
	buf[2] = (u8)((hash >> 16) & 0xFF);
	buf[3] = (u8)((hash >> 24) & 0xFF);
	return write_sys_file("passwd", buf, 4);
}

extern "C" bool fs_read_passwd(u32* out_hash) {
	u8 buf[4];
	u32 len = 0;
	if (!read_sys_file("passwd", buf, 4, &len)) return false;
	if (len < 4) return false;
	*out_hash = (u32)buf[0] | ((u32)buf[1] << 8) | ((u32)buf[2] << 16) | ((u32)buf[3] << 24);
	return true;
}

extern "C" bool fs_create(const char* name) {
	if (!g_mounted) {
		return false;
	}
	if (!name || name[0] == '\0') {
		return false;
	}
	if (str_len(name) >= sizeof(DirEntry::name)) {
		return false;
	}

	if (!ensure_root_dir_block()) {
		return false;
	}

	int inode = find_free_inode();
	if (inode < 0) {
		return false;
	}

	int block = find_free_block();
	if (block < 0) {
		return false;
	}

	Inode* root = &g_inodes[0];
	u8 dir_block[FS_BLOCK_SIZE];
	if (!disk_read_lba(g_sb.data_lba + root->direct[0], dir_block, 1)) {
		return false;
	}

	DirEntry* entries = (DirEntry*)dir_block;
	u32 entry_count = FS_BLOCK_SIZE / sizeof(DirEntry);
	int free_slot = -1;
	for (u32 i = 0; i < entry_count; i++) {
		if (entries[i].used) {
			if (str_equals(entries[i].name, name)) {
				int inode = (int)entries[i].inode;
				if (inode < 0 || inode >= (int)FS_MAX_INODES) return false;
				if (g_inodes[inode].used && g_inodes[inode].type == INODE_FILE) {
					return false;
				}
			}
		} else if (free_slot < 0) {
			free_slot = (int)i;
		}
	}

	if (free_slot < 0) {
		return false;
	}

	mark_block_used((u32)block);
	g_inodes[inode].used = 1;
	g_inodes[inode].type = INODE_FILE;
	g_inodes[inode].links = 1;
	g_inodes[inode].size = 0;
	g_inodes[inode].direct[0] = (u32)block;

	mem_set((u8*)&entries[free_slot], 0, sizeof(DirEntry));
	entries[free_slot].used = 1;
	entries[free_slot].inode = (u8)inode;
	mem_copy((u8*)entries[free_slot].name, (const u8*)name, str_len(name));

	if (!disk_write_lba(g_sb.data_lba + root->direct[0], dir_block, 1)) {
		return false;
	}

	if (!flush_bitmap()) {
		return false;
	}

	if (!flush_inodes()) {
		return false;
	}

	return true;
}

extern "C" u64 fs_get_disk_bytes() {
	u64 sectors = 0;
	if (ata_get_sector_count(&sectors)) {
		return sectors * 512ull;
	}

	u32 total_blocks = g_sb.block_count;
	return (u64)total_blocks * (u64)g_sb.block_size;
}

extern "C" int getMB() {
	u64 total_bytes = fs_get_disk_bytes();
	return (int)(total_bytes >> 20);
}

extern "C" bool fs_get_usage(u64* total_bytes, u64* used_bytes, u64* free_bytes) {
	if (!g_mounted) return false;

	u32 total_blocks = g_sb.block_count;
	u32 max_blocks = FS_BLOCK_SIZE * 8u * FS_BITMAP_SECTORS;
	if (total_blocks == 0 || total_blocks > max_blocks) {
		total_blocks = max_blocks;
	}

	u32 used_blocks = 0;
	for (u32 i = 1; i < total_blocks; i++) {
		u32 byte_index = i / 8u;
		u32 bit_index = i % 8u;
		if (g_bitmap[byte_index] & (u8)(1u << bit_index)) {
			used_blocks++;
		}
	}

	u64 total_bytes_val = (u64)total_blocks * (u64)FS_BLOCK_SIZE;
	u64 used_bytes_val = (u64)used_blocks * (u64)FS_BLOCK_SIZE;
	u64 free_bytes_val = total_bytes_val > used_bytes_val ? (total_bytes_val - used_bytes_val) : 0;

	if (total_bytes) *total_bytes = total_bytes_val;
	if (used_bytes) *used_bytes = used_bytes_val;
	if (free_bytes) *free_bytes = free_bytes_val;
	return true;
}