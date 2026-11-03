#ifndef MOCK_ZEPHYR_H
#define MOCK_ZEPHYR_H
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#define CONFIG_BT_MAX_CONN 1
#define CONFIG_BT_MAX_PAIRED 1
#define CONFIG_BT_ID_MAX 1
#define CONFIG_BT_DEVICE_NAME "Mock Sensor"
#define CONFIG_BT_UNINIT_MPSL_ON_DISABLE 1
#if defined(SECURE_CHECK)
#define CONFIG_ENV_TELEMETRY_SECURE 1
#define CONFIG_BT_SETTINGS 1
#define CONFIG_BT_SMP 1
#else
#define CONFIG_BT_SETTINGS 0
#define CONFIG_BT_SMP 0
#endif
#define CONFIG_BT_PRIVACY 0
#define IS_ENABLED(v) (v)
#define BUILD_ASSERT(v,msg) _Static_assert(v,msg)
#define ARG_UNUSED(v) (void)(v)
#define ARRAY_SIZE(v) (sizeof(v)/sizeof((v)[0]))
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define K_FOREVER -1
#define K_NO_WAIT 0
#define K_MSEC(v) (v)
#define K_SECONDS(v) ((v)*1000)
#define printk printf
static int64_t mock_now;
static int start_count,stop_count,disconnect_count,notify_count;
static bool mock_advertising;
static bool mock_stack_enabled;
static int enable_count, disable_count, restore_count, mock_enable_error;
static void (*mock_disable_hook)(void);
static void (*mock_notify_hook)(void);
static void (*mock_notify_observer)(void);
static uint16_t mock_mtu=247;
static void (*mock_disconnect_hook)(void);
static void (*mock_wait_hook)(void);
static bool mock_assert_drained;
struct k_work_sync {int unused;};
struct k_mutex {int unused;};struct k_sem {int count;};struct k_work {void(*handler)(struct k_work*);};
struct k_work_delayable {struct k_work work;void (*handler)(struct k_work*);int64_t due;bool pending;};
#define K_MUTEX_DEFINE(v) struct k_mutex v
#define K_SEM_DEFINE(v,a,b) struct k_sem v={a}
#define K_WORK_DEFINE(v,fn) struct k_work v={.handler=fn}
#define K_WORK_DELAYABLE_DEFINE(v,fn) struct k_work_delayable v={.handler=fn}
#define K_THREAD_DEFINE(name,size,fn,a,b,c,prio,x,y) void (*name##_function)(void*,void*,void*)=fn
static int k_work_submit(struct k_work *w){w->handler(w);return 1;}
static bool k_work_flush(struct k_work *w,struct k_work_sync *s){(void)w;(void)s;return false;}
static void k_msleep(int milliseconds){mock_now+=milliseconds;}
static int64_t k_uptime_get(void) {return mock_now;}
static int k_mutex_lock(struct k_mutex *v,int x) {(void)v;(void)x;return 0;}
static int k_mutex_unlock(struct k_mutex *v) {(void)v;return 0;}
static int k_sem_take(struct k_sem *s,int x) {if(s->count){--s->count;return 0;}if(x>0&&mock_wait_hook){void(*fn)(void)=mock_wait_hook;mock_wait_hook=NULL;fn();if(s->count){--s->count;return 0;}}if(x>0)mock_now+=x;return -16;}
static void k_sem_give(struct k_sem *s) {s->count=1;}
static int k_work_reschedule(struct k_work_delayable *w,int64_t ms) {w->due=mock_now+ms;w->pending=true;return 0;}
static int k_work_cancel_delayable(struct k_work_delayable *w) {w->pending=false;return 0;}
struct bt_uuid {int type;};struct bt_uuid_128 {struct bt_uuid uuid;uint8_t val[16];};
#define BT_UUID_INIT_128(...) {{0},{__VA_ARGS__}}
#define BT_UUID_128_ENCODE(...) 0
struct bt_conn {int refs;bool subscribed;int security;};
static struct bt_conn *mock_peer;
typedef struct {uint8_t type;uint8_t bytes[6];} bt_addr_le_t;
struct bt_gatt_attr {int unused;};
#define BT_GATT_ERR(v) (-(v))
#define BT_ATT_ERR_UNLIKELY 14
#define BT_ATT_ERR_AUTHENTICATION 5
#define BT_ATT_ERR_INVALID_OFFSET 7
#define BT_ATT_ERR_INVALID_ATTRIBUTE_LEN 13
#define BT_ATT_ERR_VALUE_NOT_ALLOWED 19
#define BT_ATT_ERR_NOT_SUPPORTED 6
#define BT_GATT_WRITE_FLAG_PREPARE 1
#define BT_GATT_CCC_NOTIFY 1
#define BT_GATT_SERVICE_DEFINE(name,...) struct {struct bt_gatt_attr attrs[4];} name
#define BT_GATT_PRIMARY_SERVICE(...) 0
#define BT_GATT_CHARACTERISTIC(...) 0
#define BT_GATT_CCC(...) 0
#define BT_CONN_CB_DEFINE(v) struct bt_conn_cb v
#if defined(SECURE_CHECK)
typedef int bt_security_t;
enum bt_security_err {BT_SECURITY_ERR_SUCCESS, BT_SECURITY_ERR_PAIR_NOT_ALLOWED};
#endif
struct bt_conn_cb {void (*connected)(struct bt_conn*,uint8_t);void (*disconnected)(struct bt_conn*,uint8_t);void (*recycled)(void);
void (*le_param_updated)(struct bt_conn*,uint16_t,uint16_t,uint16_t);
#if defined(SECURE_CHECK)
void (*security_changed)(struct bt_conn*, bt_security_t, enum bt_security_err);
#endif
};
extern struct bt_conn_cb connection_callbacks;
#define BT_CONN_TYPE_ALL 255
struct bt_data {int unused;};
#define BT_DATA_BYTES(...) {0}
#define BT_DATA(...) {0}
#define BT_ID_DEFAULT 0
#define BT_LE_ADV_OPT_CONN 3
#define BT_HCI_ERR_REMOTE_USER_TERM_CONN 19
struct bt_le_adv_param {int id;int options;uint16_t interval_min,interval_max;};
static int bt_enable(void *v) {(void)v;++enable_count;if(mock_enable_error)return mock_enable_error;mock_stack_enabled=true;return 0;}
static int bt_disable(void) {if(mock_assert_drained){assert(!mock_peer||!mock_peer->refs);mock_assert_drained=false;}++disable_count;mock_stack_enabled=false;mock_advertising=false;if(mock_disable_hook)mock_disable_hook();if(mock_peer && mock_peer->refs)connection_callbacks.disconnected(mock_peer,22);connection_callbacks.recycled();return 0;}
static void bt_id_get(bt_addr_le_t *a,size_t *count){assert(*count==1);*a=(bt_addr_le_t){.type=1,.bytes={1,2,3,4,5,0xc0}};}
static int bt_id_create(bt_addr_le_t *a,void *v){(void)v;assert(a->bytes[5]==0xc0);++restore_count;return 0;}
static void bt_conn_foreach(int type,void (*fn)(struct bt_conn*,void*),void *ctx){(void)type;if(mock_peer&&mock_peer->refs)fn(mock_peer,ctx);}
static int bt_le_adv_start(const struct bt_le_adv_param *p,const struct bt_data *a,size_t ac,const struct bt_data *s,size_t sc) {(void)p;(void)a;(void)ac;(void)s;(void)sc;mock_advertising=true;++start_count;return 0;}
static int bt_le_adv_stop(void) {mock_advertising=false;++stop_count;return 0;}
static struct bt_conn *bt_conn_ref(struct bt_conn *c) {mock_peer=c;++c->refs;return c;}
static void bt_conn_unref(struct bt_conn *c) {--c->refs;}
static int bt_conn_disconnect(struct bt_conn *c,uint8_t r) {(void)c;(void)r;++disconnect_count;if(mock_disconnect_hook)mock_disconnect_hook();return 0;}
static bool bt_gatt_is_subscribed(struct bt_conn *c,const struct bt_gatt_attr *a,uint16_t v) {(void)a;(void)v;return c->subscribed;}
static uint16_t bt_gatt_get_mtu(struct bt_conn *c) {(void)c;return mock_mtu;}
static int bt_gatt_notify(struct bt_conn *c,const struct bt_gatt_attr *a,const void *d,uint16_t l) {(void)c;(void)a;(void)d;(void)l;if(mock_notify_observer)mock_notify_observer();if(mock_notify_hook){mock_notify_hook();return -107;}++notify_count;return 0;}
static ssize_t bt_gatt_attr_read(struct bt_conn *c,const struct bt_gatt_attr *a,void *o,uint16_t l,uint16_t offset,const void *d,uint16_t n) {(void)c;(void)a;if(offset>n)return -7;uint16_t count=MIN(l,n-offset);memcpy(o,(const uint8_t*)d+offset,count);return count;}

#if defined(SECURE_CHECK)
#define BT_GATT_PERM_READ_LESC 16
#define BT_GATT_PERM_WRITE_LESC 32
#define BT_GATT_PERM_READ_AUTHEN 64
#define BT_GATT_PERM_WRITE_AUTHEN 128
#define BT_SECURITY_L4 4
#define BT_PASSKEY_RAND UINT32_MAX
struct bt_conn_pairing_feat {int unused;};
struct bt_bond_info {int unused;};
struct bt_conn_auth_cb {
 enum bt_security_err (*pairing_accept)(struct bt_conn*,const struct bt_conn_pairing_feat*);
 uint32_t (*app_passkey)(struct bt_conn*);
 void (*passkey_display)(struct bt_conn*,unsigned int);
 void (*cancel)(struct bt_conn*);
};
struct bt_conn_auth_info_cb {
 void (*pairing_complete)(struct bt_conn*,bool);
 void (*pairing_failed)(struct bt_conn*,enum bt_security_err);
};
static bool stored_bond, loaded_bond;
static int settings_load_count, unpair_count, auth_register_count, info_register_count, settings_error;
static int settings_init_error, auth_register_error, info_register_error;
static bool stored_identity_present, stored_identity_mismatch, suppress_identity_save;
static size_t stored_identity_length = sizeof(bt_addr_le_t);
static int identity_read_error, identity_save_error, identity_save_count;
static uint32_t mock_random = 123456;
static unsigned int random_count;
static int bt_conn_auth_cb_register(const struct bt_conn_auth_cb *cb){assert(cb->app_passkey && cb->passkey_display && cb->cancel && cb->pairing_accept);++auth_register_count;return auth_register_error;}
static int bt_conn_auth_info_cb_register(struct bt_conn_auth_info_cb *cb){assert(cb->pairing_complete && cb->pairing_failed);++info_register_count;return info_register_error;}
static int bt_conn_auth_cancel(struct bt_conn *conn){(void)conn;return 0;}
static int bt_conn_get_security(struct bt_conn *conn){return conn->security;}
static int bt_conn_set_security(struct bt_conn *conn,int level){(void)conn;assert(level==4);return 0;}
static int settings_subsys_init(void){return settings_init_error;}
static int settings_load_subtree(const char *key){assert(mock_stack_enabled && !strcmp(key,"bt"));++settings_load_count;if(settings_error)return settings_error;loaded_bond=stored_bond;return 0;}
static void bt_foreach_bond(int id,void(*fn)(const struct bt_bond_info*,void*),void*ctx){assert(id==0);if(loaded_bond){struct bt_bond_info b={0};fn(&b,ctx);}}
typedef int(*settings_read_cb)(void*,void*,size_t);
static int read_mock_id(void *context,void *data,size_t size){(void)context;if(identity_read_error)return identity_read_error;bt_addr_le_t addr;size_t n=1;bt_id_get(&addr,&n);assert(size==sizeof(addr));if(stored_identity_mismatch)addr.bytes[0]^=1;memcpy(data,&addr,size);return size;}
static int settings_save_one(const char *key,const void *data,size_t size){bt_addr_le_t addr;size_t count=1;bt_id_get(&addr,&count);assert(mock_stack_enabled && !strcmp(key,"bt/id") && size==sizeof(addr) && !memcmp(data,&addr,size));++identity_save_count;if(identity_save_error)return identity_save_error;if(!suppress_identity_save)stored_identity_present=true;return 0;}
static int settings_load_subtree_direct(const char *key,int(*fn)(const char*,size_t,settings_read_cb,void*,void*),void*ctx){
 if(settings_error)return settings_error;
 if(!strcmp(key,"bt/id")){if(stored_identity_present)(void)fn(NULL,stored_identity_length,read_mock_id,NULL,ctx);return 0;}
 assert(!strcmp(key,"bt/keys"));
 return stored_bond ? fn("peer",64,NULL,NULL,ctx) : 0;
}
static int bt_unpair(int id,const bt_addr_le_t *addr){assert(id==0 && !addr && mock_stack_enabled && (!mock_peer || !mock_peer->refs));stored_bond=false;loaded_bond=false;++unpair_count;return 0;}
static int bt_rand(void *out,size_t size){assert(size==4 && mock_stack_enabled);memcpy(out,&mock_random,4);mock_random+=91237;++random_count;return 0;}
#endif

#endif
