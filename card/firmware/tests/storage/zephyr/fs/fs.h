#ifndef STORAGE_TEST_FS_H
#define STORAGE_TEST_FS_H
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define FS_FATFS 0
#define FS_MOUNT_FLAG_NO_FORMAT 1
#define FS_O_READ 1
#define FS_O_WRITE 2
#define FS_O_CREATE 4
#define FS_SEEK_SET SEEK_SET
#define FS_SEEK_END SEEK_END
#define FS_DIR_ENTRY_FILE 0
struct fs_mount_t { int type;void *fs_data;const char *mnt_point;int flags; };
struct fs_file_t { int fd; };
struct fs_dir_t { DIR *dir; };
struct fs_dirent { char name[256];size_t size;int type; };
extern char test_directory[256];
extern int test_write_failure;
extern void (*test_sync_hook)(void);
extern size_t test_read_bytes;
extern void (*test_read_hook)(void);
extern off_t test_read_position;
extern size_t test_readdir_calls;
static inline const char *test_path(const char *path, char *out) { snprintf(out,512,"%s%s",test_directory,path+4);return out; }
static inline void fs_file_t_init(struct fs_file_t*f){f->fd=-1;}
static inline void fs_dir_t_init(struct fs_dir_t*d){d->dir=NULL;}
static inline int fs_mount(struct fs_mount_t*m){(void)m;return 0;}
static inline int fs_unmount(struct fs_mount_t*m){(void)m;return 0;}
static inline int fs_open(struct fs_file_t*f,const char*p,int flags){char path[512];int access=flags&FS_O_WRITE?O_WRONLY:O_RDONLY;if(flags&FS_O_CREATE)access|=O_CREAT;f->fd=open(test_path(p,path),access,0600);return f->fd<0?-errno:0;}
static inline int fs_close(struct fs_file_t*f){int e=close(f->fd);f->fd=-1;return e?-errno:0;}
static inline ssize_t fs_read(struct fs_file_t*f,void*b,size_t n){test_read_position=lseek(f->fd,0,SEEK_CUR);if(test_read_hook){void(*hook)(void)=test_read_hook;test_read_hook=NULL;hook();}ssize_t r=read(f->fd,b,n);if(r>0)test_read_bytes+=(size_t)r;return r<0?-errno:r;}
static inline ssize_t fs_write(struct fs_file_t*f,const void*b,size_t n){if(test_write_failure==0)return -EIO;if(test_write_failure>0)--test_write_failure;ssize_t r=write(f->fd,b,n);return r<0?-errno:r;}
static inline int fs_seek(struct fs_file_t*f,off_t n,int where){return lseek(f->fd,n,where)<0?-errno:0;}
static inline off_t fs_tell(struct fs_file_t*f){off_t r=lseek(f->fd,0,SEEK_CUR);return r<0?-errno:r;}
static inline int fs_truncate(struct fs_file_t*f,off_t n){return ftruncate(f->fd,n)?-errno:0;}
static inline int fs_sync(struct fs_file_t*f){if(test_sync_hook){void(*h)(void)=test_sync_hook;test_sync_hook=NULL;h();}return fsync(f->fd)?-errno:0;}
static inline int fs_stat(const char*p,struct fs_dirent*e){char path[512];struct stat s;if(stat(test_path(p,path),&s))return -errno;e->size=s.st_size;e->type=S_ISDIR(s.st_mode);return 0;}
static inline int fs_opendir(struct fs_dir_t*d,const char*p){char path[512];d->dir=opendir(test_path(p,path));return d->dir?0:-errno;}
static inline int fs_readdir(struct fs_dir_t*d,struct fs_dirent*e){++test_readdir_calls;struct dirent*r;do{r=readdir(d->dir);}while(r&&r->d_name[0]=='.');if(!r){e->name[0]=0;return 0;}snprintf(e->name,sizeof(e->name),"%s",r->d_name);e->type=r->d_type==DT_DIR;return 0;}
static inline int fs_closedir(struct fs_dir_t*d){return closedir(d->dir)?-errno:0;}
#endif
