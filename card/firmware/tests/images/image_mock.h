#ifndef CARD_IMAGE_MOCK_H_
#define CARD_IMAGE_MOCK_H_
#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <sys/types.h>
#include <stdio.h>
#include <errno.h>
#define CONFIG_BT_PERIPHERAL_PREF_MIN_INT 12
#define CONFIG_BT_PERIPHERAL_PREF_MAX_INT 24
#define CONFIG_BT_PERIPHERAL_PREF_LATENCY 0
#define CONFIG_BT_PERIPHERAL_PREF_TIMEOUT 600
#define ARG_UNUSED(v) (void)(v)
#define MIN(a,b) ((a)<(b)?(a):(b))
#define K_FOREVER -1
#define K_SECONDS(v) ((v)*1000)
#define K_MSEC(v) (v)
#define printk(...) ((void)0)
static int64_t mock_now;
static jmp_buf worker_done;
static int mounted,fs_writes,param_calls;
static void (*storage_hook)(void),(*write_hook)(off_t,size_t);
struct k_mutex {int held;};
struct k_sem {int count;};
struct k_work {int unused;};
struct k_work_delayable {void(*handler)(struct k_work*);bool pending;};
#define K_MUTEX_DEFINE(v) struct k_mutex v
#define K_SEM_DEFINE(v,a,b) struct k_sem v={a}
#define K_WORK_DELAYABLE_DEFINE(v,f) struct k_work_delayable v={f,false}
#define K_THREAD_DEFINE(v,s,f,a,b,c,p,x,y) void (*v##_entry)(void*,void*,void*)=f
static int k_mutex_lock(struct k_mutex *v,int wait){(void)wait;assert(!v->held);v->held=1;return 0;}
static void k_mutex_unlock(struct k_mutex *v){assert(v->held);v->held=0;}
static void k_sem_give(struct k_sem *v){v->count=1;}
static int k_sem_take(struct k_sem *v,int wait){(void)wait;if(!v->count)longjmp(worker_done,1);--v->count;return 0;}
static int64_t k_uptime_get(void){return mock_now;}
static int k_work_reschedule(struct k_work_delayable*v,int64_t delay){(void)delay;v->pending=true;return 1;}
static int k_work_cancel_delayable(struct k_work_delayable*v){v->pending=false;return 0;}
struct bt_conn {int refs,security;};
struct bt_conn_cb {void(*disconnected)(struct bt_conn*,uint8_t);};
struct bt_uuid {int unused;};
struct bt_uuid_128 {struct bt_uuid uuid;uint8_t val[16];};
#define BT_UUID_128_ENCODE(...) 0
#define BT_UUID_INIT_128(...) {{0},{0}}
#define BT_CONN_CB_DEFINE(v) struct bt_conn_cb v
struct bt_le_conn_param {uint16_t interval_min,interval_max,latency,timeout;};
struct bt_gatt_attr {int unused;};
#define BT_GATT_SERVICE_DEFINE(v,...) int v
#define BT_GATT_ERR(v) (-(v))
#define BT_ATT_ERR_AUTHENTICATION 5
#define BT_ATT_ERR_INVALID_OFFSET 7
#define BT_ATT_ERR_VALUE_NOT_ALLOWED 19
#define BT_GATT_WRITE_FLAG_PREPARE 1
#define BT_SECURITY_L4 4
static struct bt_conn *bt_conn_ref(struct bt_conn*c){++c->refs;return c;}
static void bt_conn_unref(struct bt_conn*c){assert(c->refs>0);--c->refs;}
static int bt_conn_get_security(struct bt_conn*c){return c->security;}
static int bt_conn_le_param_update(struct bt_conn*c,const struct bt_le_conn_param*p){assert(c&&p->interval_min==12&&p->interval_max==24&&!p->latency&&p->timeout==600);++param_calls;return 0;}
static ssize_t bt_gatt_attr_read(struct bt_conn*c,const struct bt_gatt_attr*a,void*out,uint16_t n,uint16_t off,const void*data,uint16_t length){(void)c;(void)a;if(off>length)return -7;size_t count=MIN(n,length-off);memcpy(out,(const uint8_t*)data+off,count);return count;}
#define FS_O_READ 1
#define FS_O_WRITE 2
#define FS_O_CREATE 4
#define FS_SEEK_SET 0
struct fake_file {uint8_t data[120032];size_t length;bool exists;};
static struct fake_file files[2];
struct fs_file_t {struct fake_file*file;off_t offset;};
static void fs_file_t_init(struct fs_file_t*f){memset(f,0,sizeof(*f));}
static int fs_open(struct fs_file_t*f,const char*path,int flags){assert(mounted);int i=strstr(path,"CARDIMG1")?1:0;f->file=&files[i];if(!f->file->exists&&!(flags&FS_O_CREATE))return -ENOENT;f->file->exists=true;return 0;}
static int fs_close(struct fs_file_t*f){(void)f;return 0;}
static int fs_truncate(struct fs_file_t*f,size_t n){assert(n<=sizeof(f->file->data));f->file->length=n;return 0;}
static int fs_seek(struct fs_file_t*f,off_t off,int mode){assert(mode==0&&off>=0&&(size_t)off<=sizeof(f->file->data));f->offset=off;return 0;}
static ssize_t fs_write(struct fs_file_t*f,const void*data,size_t n){assert(mounted&&(size_t)f->offset+n<=sizeof(f->file->data));if(write_hook)write_hook(f->offset,n);memcpy(f->file->data+f->offset,data,n);f->offset+=n;if((size_t)f->offset>f->file->length)f->file->length=f->offset;++fs_writes;return n;}
static ssize_t fs_read(struct fs_file_t*f,void*data,size_t n){assert(mounted);size_t avail=(size_t)f->offset>=f->file->length?0:f->file->length-f->offset;size_t count=MIN(avail,n);memcpy(data,f->file->data+f->offset,count);f->offset+=count;return count;}
static int fs_sync(struct fs_file_t*f){(void)f;return 0;}
#endif
