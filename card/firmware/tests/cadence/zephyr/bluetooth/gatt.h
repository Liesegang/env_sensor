#ifndef CADENCE_BT_GATT_H_
#define CADENCE_BT_GATT_H_
#include <sys/types.h>
#include <string.h>
#include <stdint.h>
#include <zephyr/bluetooth/conn.h>
struct bt_uuid { int unused; };
struct bt_uuid_128 { struct bt_uuid uuid; uint8_t val[16]; };
struct bt_gatt_attr { int unused; };
#define BT_UUID_128_ENCODE(...) 0
#define BT_UUID_INIT_128(...) {{0},{0}}
#define BT_GATT_SERVICE_DEFINE(v,...) int v
#define BT_GATT_ERR(v) (-(v))
#define BT_ATT_ERR_AUTHENTICATION 5
#define BT_ATT_ERR_INVALID_OFFSET 7
#define BT_ATT_ERR_UNLIKELY 14
#define BT_ATT_ERR_VALUE_NOT_ALLOWED 19
static inline ssize_t bt_gatt_attr_read(struct bt_conn *conn,const struct bt_gatt_attr *attr,void *out,uint16_t length,uint16_t offset,const void *data,uint16_t size)
{
    (void)conn;(void)attr;if(offset>size)return -7;
    size_t count=length<size-offset?length:size-offset;
    memcpy(out,(const uint8_t*)data+offset,count);return (ssize_t)count;
}
#endif
