#ifndef CARD_HISTORY_MOCK_H_
#define CARD_HISTORY_MOCK_H_
#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#define ARG_UNUSED(v) (void)(v)
#define ARRAY_SIZE(v) (sizeof(v)/sizeof((v)[0]))
#define MIN(a,b) ((a)<(b)?(a):(b))
#define K_FOREVER -1
#define K_NO_WAIT 0
#define K_SECONDS(v) ((v)*1000)
#define K_MSEC(v) (v)
#define printk(...) ((void)0)
static int64_t mock_now;
static jmp_buf worker_done;
struct k_mutex {int held;};
struct k_sem {int count;};
struct k_thread {int unused;};
struct k_work {int unused;};
struct k_work_delayable {void(*handler)(struct k_work*);bool pending;struct k_work work;};
#define K_MUTEX_DEFINE(v) struct k_mutex v
#define K_SEM_DEFINE(v,a,b) struct k_sem v={a}
#define K_WORK_DELAYABLE_DEFINE(v,f) struct k_work_delayable v={.handler=f}
#define K_THREAD_STACK_DEFINE(v,n) char v[n]
#define K_THREAD_STACK_SIZEOF(v) sizeof(v)
static int k_mutex_lock(struct k_mutex*v,int wait){(void)wait;assert(!v->held);v->held=1;return 0;}
static void k_mutex_unlock(struct k_mutex*v){assert(v->held);v->held=0;}
static void k_sem_give(struct k_sem*v){v->count=1;}
static int k_sem_take(struct k_sem*v,int wait){(void)wait;if(!v->count)longjmp(worker_done,1);--v->count;return 0;}
static int64_t k_uptime_get(void){return mock_now;}
static int k_work_reschedule(struct k_work_delayable*v,int64_t delay){(void)delay;v->pending=true;return 1;}
static int k_work_cancel_delayable(struct k_work_delayable*v){v->pending=false;return 0;}
static void k_thread_create(struct k_thread*t,char*s,size_t n,void(*entry)(void*,void*,void*),void*a,void*b,void*c,int p,int o,int d){(void)t;(void)s;(void)n;(void)entry;(void)a;(void)b;(void)c;(void)p;(void)o;(void)d;}
struct bt_conn {int refs,security;bool subscribed;};
struct bt_conn_cb {void(*disconnected)(struct bt_conn*,uint8_t);};
struct bt_uuid {int unused;};
struct bt_uuid_128 {struct bt_uuid uuid;uint8_t val[16];};
#define BT_UUID_128_ENCODE(...) 0
#define BT_UUID_INIT_128(...) {{0},{0}}
#define BT_CONN_CB_DEFINE(v) struct bt_conn_cb v
struct bt_gatt_attr {int unused;};
#define BT_GATT_SERVICE_DEFINE(v,...) struct {struct bt_gatt_attr attrs[8];} v
#define BT_GATT_ERR(v) (-(v))
#define BT_ATT_ERR_AUTHENTICATION 5
#define BT_ATT_ERR_INVALID_OFFSET 7
#define BT_ATT_ERR_UNLIKELY 14
#define BT_ATT_ERR_INSUFFICIENT_RESOURCES 17
#define BT_ATT_ERR_VALUE_NOT_ALLOWED 19
#define BT_SECURITY_L4 4
#define BT_GATT_CCC_NOTIFY 1
#define CONFIG_BT_PERIPHERAL_PREF_MIN_INT 12
#define CONFIG_BT_PERIPHERAL_PREF_MAX_INT 24
#define CONFIG_BT_PERIPHERAL_PREF_LATENCY 0
#define CONFIG_BT_PERIPHERAL_PREF_TIMEOUT 600
struct bt_le_conn_param {uint16_t interval_min,interval_max,latency,timeout;};
static int parameter_requests;
static int bt_conn_le_param_update(struct bt_conn*c,const struct bt_le_conn_param*p){assert(c&&p->interval_min==12&&p->interval_max==24&&!p->latency&&p->timeout==600);++parameter_requests;return 0;}
static struct bt_conn*bt_conn_ref(struct bt_conn*c){++c->refs;return c;}
static void bt_conn_unref(struct bt_conn*c){assert(c->refs>0);--c->refs;}
static int bt_conn_get_security(struct bt_conn*c){return c->security;}
static ssize_t bt_gatt_attr_read(struct bt_conn*c,const struct bt_gatt_attr*a,void*out,uint16_t n,uint16_t off,const void*data,uint16_t length){(void)c;(void)a;if(off>length)return -7;size_t count=MIN(n,length-off);memcpy(out,(const uint8_t*)data+off,count);return (ssize_t)count;}
static bool bt_gatt_is_subscribed(struct bt_conn*c,const struct bt_gatt_attr*a,uint16_t v){(void)a;(void)v;return c->subscribed;}
static int notify_count;
static uint8_t last_hint[8];
static int bt_gatt_notify(struct bt_conn*c,const struct bt_gatt_attr*a,const void*p,uint16_t n){(void)c;(void)a;assert(n==8);memcpy(last_hint,p,n);++notify_count;return 0;}
#endif
