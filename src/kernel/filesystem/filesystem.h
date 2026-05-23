#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include "../util/types.h"

struct DirEntry {
    u8 used;
    u8 inode;
    char name[30];
} __attribute__((packed));

enum FsError {
    FS_ERR_NONE = 0,
    FS_ERR_IO = 1,
    FS_ERR_BAD_MAGIC = 2
};

extern "C" bool fs_init();
extern "C" bool fs_format(u32 total_blocks, u32 inode_count);
extern "C" bool fs_list_root(DirEntry* out_entries, u32 max_entries, u32* out_count);
extern "C" bool fs_list_dir(const char* dirname, DirEntry* out_entries, u32 max_entries, u32* out_count);
extern "C" bool fs_mkdir(const char* name);
extern "C" bool fs_mkdir_path(const char* path);
extern "C" bool fs_is_dir(const char* path);
extern "C" bool fs_copy_path(const char* src_path, const char* dst_path);
extern "C" bool fs_create(const char* name);
extern "C" bool fs_write(const char* name, const char* data);
extern "C" bool fs_write_path(const char* path, const char* data);
extern "C" bool fs_rename(const char* src_path, const char* dst_path);
extern "C" bool fs_remove(const char* name);
extern "C" bool fs_remove_path(const char* path);
extern "C" bool fs_remove_path_recursive(const char* path);
extern "C" bool fs_read(const char* name, char* out, u32 maxlen, u32* out_len);
extern "C" bool fs_read_path(const char* path, char* out, u32 maxlen, u32* out_len);
extern "C" u32 fs_hash_password(const char* s);
extern "C" bool fs_write_passwd(u32 hash);
extern "C" bool fs_read_passwd(u32* out_hash);
extern "C" FsError fs_last_error();
extern "C" bool fs_is_formatted();
extern "C" int getMB();
extern "C" u64 fs_get_disk_bytes();
extern "C" bool fs_get_usage(u64* total_bytes, u64* used_bytes, u64* free_bytes);

#endif
