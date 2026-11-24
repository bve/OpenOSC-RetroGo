/* OpenOSC-RetroGo: FCEUmm save states use an in-memory stream. */
#ifndef FCEUMM_PORT_FILESYSTEM_H
#define FCEUMM_PORT_FILESYSTEM_H
#include <stddef.h>
typedef struct fs_file fs_file_t;
enum { FS_READ = 0, FS_WRITE = 1, FS_COMPRESS = 2 };
enum { LFS_SEEK_SET = 0, LFS_SEEK_CUR = 1, LFS_SEEK_END = 2 };
fs_file_t *fs_open(const char *path, int mode, int flags);
int fs_read(fs_file_t *file, void *buffer, size_t size);
int fs_write(fs_file_t *file, const void *buffer, size_t size);
int fs_seek(fs_file_t *file, int offset, int whence);
void fs_close(fs_file_t *file);
#endif
