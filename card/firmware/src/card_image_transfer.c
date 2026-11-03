#include "card_image_transfer.h"
#include "card_image_protocol.h"
#include "card_display.h"
#include "sd_logger.h"
#include <env_telemetry_ble.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <stdio.h>

#define IMAGE_PREFIX "/SD:/CARDIMG"
#define IMAGE_PERMISSIONS (BT_GATT_PERM_READ_LESC | BT_GATT_PERM_WRITE_LESC | \
    BT_GATT_PERM_READ_AUTHEN | BT_GATT_PERM_WRITE_AUTHEN)
#define IMAGE_UUID(n) BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x722acd00U+(n),0xe77f,0x468d,0xa916,0x67e599e7cffdULL))
static struct bt_uuid_128 image_service_uuid=IMAGE_UUID(0x90);
static struct bt_uuid_128 control_uuid=IMAGE_UUID(0x91);
static struct bt_uuid_128 data_uuid=IMAGE_UUID(0x92);
static struct bt_uuid_128 status_uuid=IMAGE_UUID(0x93);
K_MUTEX_DEFINE(image_lock);
K_SEM_DEFINE(image_requested,0,1);
static struct bt_conn *owner;
static bool initialized, busy, commit_decided;
static uint8_t state, operation;
static uint32_t transfer_id, expected_crc, received, rolling_crc, token;
static uint32_t file_generation, last_offset;
static size_t last_length, operation_length;
static uint8_t last_bytes[CARD_IMAGE_MAX_CHUNK];
static uint8_t operation_bytes[12+CARD_IMAGE_MAX_CHUNK];
static uint32_t operation_token;
static int32_t image_error;
static char stage_path[32];
static int64_t transfer_deadline;
static uint8_t status_read_frame[24];
static void transfer_expired(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(transfer_timeout,transfer_expired);

static bool transfer_active(void) { return state==IMAGE_RECEIVING||state==IMAGE_PREPARING||state==IMAGE_COMMITTING||state==IMAGE_STORED||state==IMAGE_DISPLAYING; }
static void renew_locked(struct bt_conn *conn) {
    if (transfer_active()) {
        transfer_deadline=k_uptime_get()+120000;
        (void)env_telemetry_ble_hold_connection(conn,120000);
        (void)k_work_reschedule(&transfer_timeout,K_SECONDS(120));
    }
}
static void release_owner_locked(void) {
    if(owner) {
        env_telemetry_ble_release_connection(owner);
        bt_conn_unref(owner);owner=NULL;
    }
    (void)k_work_cancel_delayable(&transfer_timeout);
}
static void cancel_locked(int error) {
    ++token; state=error?IMAGE_ERROR:IMAGE_IDLE; image_error=error;
    release_owner_locked();
}
static void transfer_expired(struct k_work *work) {
    ARG_UNUSED(work);k_mutex_lock(&image_lock,K_FOREVER);
    if(transfer_active()) {
        int64_t remaining=transfer_deadline-k_uptime_get();
        if(remaining>0) (void)k_work_reschedule(&transfer_timeout,K_MSEC(remaining));
        else if(commit_decided&&state==IMAGE_COMMITTING) { release_owner_locked(); }
        else {cancel_locked(-ETIMEDOUT);printk("IMAGE: transfer timed out\n");}
    }
    k_mutex_unlock(&image_lock);
}

static int close_file(struct fs_file_t *file,int error) {
    int closed=fs_close(file);return error?error:closed;
}
static int read_exact(struct fs_file_t *file,void *buffer,size_t length) {
    ssize_t count=fs_read(file,buffer,length);return count<0?(int)count:count==(ssize_t)length?0:-EBADMSG;
}
static int write_exact(struct fs_file_t *file,const void *buffer,size_t length) {
    ssize_t count=fs_write(file,buffer,length);return count<0?(int)count:count==(ssize_t)length?0:-EIO;
}
struct file_meta { uint32_t generation,crc; };
/* Caller owns a mounted storage transaction. No 120-KiB secondary buffer. */
static int read_slot(const char *path,uint8_t *frame,struct file_meta *meta) {
    struct fs_file_t file;fs_file_t_init(&file);
    int error=fs_open(&file,path,FS_O_READ);if(error)return error;
    uint8_t header[32],scratch[256];
    error=read_exact(&file,header,sizeof(header));
    if(!error)error=image_check_header(header);
    uint32_t crc=0;
    for(size_t offset=0;!error&&offset<CARD_IMAGE_BYTES;) {
        size_t amount=MIN(sizeof(scratch),CARD_IMAGE_BYTES-offset);
        uint8_t *destination=frame?frame+offset:scratch;
        error=read_exact(&file,destination,amount);
        if(!error&&!image_palette_valid(destination,amount))error=-EBADMSG;
        if(!error)crc=image_crc32(crc,destination,amount);
        offset+=amount;
    }
    if(!error&&crc!=image_get32(header+12))error=-EBADMSG;
    if(!error){ssize_t extra=fs_read(&file,scratch,1);if(extra!=0)error=extra<0?(int)extra:-EBADMSG;}
    if(!error)*meta=(struct file_meta){image_get32(header+16),crc};
    return close_file(&file,error);
}
static int choose_slots(const char *prefix,char paths[2][32],struct file_meta metas[2]) {
    int good[2];
    for(unsigned int i=0;i<2;++i) {
        int count=snprintf(paths[i],32,"%s%u.BIN",prefix,i);
        if(count<0||count>=32)return -ENAMETOOLONG;
        good[i]=read_slot(paths[i],NULL,&metas[i]);
    }
    if(!good[0]&&!good[1])return (int32_t)(metas[1].generation-metas[0].generation)>0?1:0;
    if(!good[0]) { return 0; }
    if(!good[1]) { return 1; }
    return -ENOENT;
}

int card_image_load_file(const char *prefix,uint8_t *frame,size_t length) {
    if(!prefix||!frame||length!=CARD_IMAGE_BYTES)return -EINVAL;
    int error=sd_logger_storage_begin();if(error)return error;
    char paths[2][32];struct file_meta metas[2]={{0}};
    int selected=choose_slots(prefix,paths,metas);
    error=selected<0?selected:read_slot(paths[selected],frame,&metas[selected]);
    int ended=sd_logger_storage_end();if(!error)error=ended;
    if(!error) {
        k_mutex_lock(&image_lock,K_FOREVER);
        if(transfer_id&&(state==IMAGE_STORED||state==IMAGE_DISPLAYING)&&
           (metas[selected].generation!=file_generation||metas[selected].crc!=expected_crc))error=-EBADMSG;
        k_mutex_unlock(&image_lock);
    }
    return error;
}

static int prepare_file(uint32_t crc,uint32_t *generation,char path[32]) {
    int error=sd_logger_storage_begin();if(error)return error;
    char paths[2][32];struct file_meta metas[2]={{0}};
    int current=choose_slots(IMAGE_PREFIX,paths,metas);
    if(current==-ENAMETOOLONG)error=current;
    unsigned int selected=current<0?0U:(unsigned int)(1-current);
    *generation=current<0?1U:metas[current].generation+1U;if(!*generation)*generation=1;
    if(!error) {
        memcpy(path,paths[selected],32);
        struct fs_file_t file;fs_file_t_init(&file);
        error=fs_open(&file,path,FS_O_CREATE|FS_O_WRITE);
        if(!error) {
            uint8_t header[32];image_file_header(header,crc,*generation,false);
            error=fs_truncate(&file,0);
            if(!error)error=write_exact(&file,header,32);
            if(!error)error=fs_sync(&file);
            error=close_file(&file,error);
        }
    }
    int ended=sd_logger_storage_end();return error?error:ended;
}
static int write_payload(const char *path,uint32_t offset,const uint8_t *bytes,size_t length) {
    int error=sd_logger_storage_begin();if(error)return error;
    struct fs_file_t file;fs_file_t_init(&file);
    error=fs_open(&file,path,FS_O_WRITE);
    if(!error) {
        error=fs_seek(&file,32+(off_t)offset,FS_SEEK_SET);
        if(!error)error=write_exact(&file,bytes,length);
        if(!error)error=fs_sync(&file);
        error=close_file(&file,error);
    }
    int ended=sd_logger_storage_end();return error?error:ended;
}
static int mark_file(const char *path,bool commit) {
    int error=sd_logger_storage_begin();if(error)return error;
    struct fs_file_t file;fs_file_t_init(&file);
    error=fs_open(&file,path,FS_O_WRITE);
    if(!error) {
        uint8_t tag[4];image_put32(tag,commit?CARD_IMAGE_COMMITTED:0);
        error=fs_seek(&file,20,FS_SEEK_SET);
        if(!error)error=write_exact(&file,tag,4);
        if(!error)error=fs_sync(&file);
        error=close_file(&file,error);
    }
    int ended=sd_logger_storage_end();return error?error:ended;
}
static void update_parameters(struct bt_conn *conn,bool fast) {
    struct bt_le_conn_param params={
        .interval_min=fast?12:CONFIG_BT_PERIPHERAL_PREF_MIN_INT,
        .interval_max=fast?24:CONFIG_BT_PERIPHERAL_PREF_MAX_INT,
        .latency=fast?0:CONFIG_BT_PERIPHERAL_PREF_LATENCY,
        .timeout=CONFIG_BT_PERIPHERAL_PREF_TIMEOUT};
    (void)bt_conn_le_param_update(conn,&params);
}
static void update_connection(bool fast) {
    k_mutex_lock(&image_lock,K_FOREVER);
    struct bt_conn *conn=owner?bt_conn_ref(owner):NULL;
    k_mutex_unlock(&image_lock);
    if(conn){update_parameters(conn,fast);bt_conn_unref(conn);}
}
static void image_worker(void *a,void *b,void *c) {
    ARG_UNUSED(a);ARG_UNUSED(b);ARG_UNUSED(c);
    for(;;) {
        k_sem_take(&image_requested,K_FOREVER);
        k_mutex_lock(&image_lock,K_FOREVER);
        uint32_t request_token=operation_token;
        uint8_t op=operation;bool current=busy&&request_token==token;
        char path[32];memcpy(path,stage_path,32);
        uint32_t crc=expected_crc,generation=file_generation;
        k_mutex_unlock(&image_lock);
        int error=0;
        if(current&&op==1){update_connection(true);error=prepare_file(crc,&generation,path);}
        else if(current&&op==4) {
            struct card_image_chunk chunk;
            error=image_decode_chunk(operation_bytes,operation_length,&chunk);
            if(!error)error=write_payload(path,chunk.offset,chunk.bytes,chunk.length);
        } else if(current&&op==2) {
            /* Re-read persisted bytes before setting the commit marker. */
            error=sd_logger_storage_begin();
            if(!error) {
                struct fs_file_t file;fs_file_t_init(&file);
                error=fs_open(&file,path,FS_O_READ);
                if(!error) {
                    uint8_t scratch[256];uint32_t disk_crc=0;
                    error=fs_seek(&file,32,FS_SEEK_SET);
                    for(size_t offset=0;!error&&offset<CARD_IMAGE_BYTES;offset+=sizeof(scratch)) {
                        size_t count=MIN(sizeof(scratch),CARD_IMAGE_BYTES-offset);
                        error=read_exact(&file,scratch,count);
                        if(!error&&!image_palette_valid(scratch,count))error=-EBADMSG;
                        if(!error)disk_crc=image_crc32(disk_crc,scratch,count);
                    }
                    if(!error&&disk_crc!=crc)error=-EBADMSG;
                    error=close_file(&file,error);
                }
                int ended=sd_logger_storage_end();if(!error)error=ended;
            }
            k_mutex_lock(&image_lock,K_FOREVER);
            current=request_token==token;
            /* Atomically finalize the cancellation decision after persisted
             * CRC validation, before the commit marker can reach media. */
            if(!error&&current)commit_decided=true;
            k_mutex_unlock(&image_lock);
            if(!error&&current)error=mark_file(path,true);
        }
        k_mutex_lock(&image_lock,K_FOREVER);
        current=request_token==token;
        busy=false;
        if(current&&error){state=IMAGE_ERROR;image_error=error;}
        else if(current&&op==1){file_generation=generation;memcpy(stage_path,path,32);state=IMAGE_RECEIVING;}
        else if(current&&op==4) {
            struct card_image_chunk chunk;(void)image_decode_chunk(operation_bytes,operation_length,&chunk);
            rolling_crc=image_crc32(rolling_crc,chunk.bytes,chunk.length);
            received+=chunk.length;last_offset=chunk.offset;last_length=chunk.length;
            memcpy(last_bytes,chunk.bytes,chunk.length);
        } else if(current&&op==2)state=IMAGE_STORED;
        bool display=current&&!error&&op==2;
        struct bt_conn *release=current&&error&&owner?bt_conn_ref(owner):NULL;
        if(current&&error)release_owner_locked();
        k_mutex_unlock(&image_lock);
        /* A cancelled token never reaches the commit marker. After the
         * irreversible decision, cancellation is rejected rather than
         * attempting an unreliable compensating write on removable media. */
        if(display) {
            update_connection(false);
            error=card_display_set_image_file(IMAGE_PREFIX);
            if(error)card_image_display_result(request_token,error);
        } else if(release) {
            update_parameters(release,false);bt_conn_unref(release);
        }
    }
}
K_THREAD_DEFINE(card_image_worker,2048,image_worker,NULL,NULL,NULL,8,0,0);

static ssize_t read_status(struct bt_conn *conn,const struct bt_gatt_attr *attr,void *buffer,uint16_t length,uint16_t offset) {
    if(bt_conn_get_security(conn)!=BT_SECURITY_L4)return BT_GATT_ERR(BT_ATT_ERR_AUTHENTICATION);
    uint8_t status[24];
    k_mutex_lock(&image_lock,K_FOREVER);
    if(!offset) {
        status_read_frame[0]='I';status_read_frame[1]='S';status_read_frame[2]=1;status_read_frame[3]=state;
        image_put32(status_read_frame+4,transfer_id);image_put32(status_read_frame+8,received);image_put32(status_read_frame+12,CARD_IMAGE_BYTES);
        image_put32(status_read_frame+16,expected_crc);image_put32(status_read_frame+20,(uint32_t)image_error);
    }
    memcpy(status,status_read_frame,sizeof(status));
    if(owner==conn)renew_locked(conn);
    k_mutex_unlock(&image_lock);
    return bt_gatt_attr_read(conn,attr,buffer,length,offset,status,sizeof(status));
}
static ssize_t write_control(struct bt_conn *conn,const struct bt_gatt_attr *attr,const void *buffer,uint16_t length,uint16_t offset,uint8_t flags) {
    ARG_UNUSED(attr);
    if(bt_conn_get_security(conn)!=BT_SECURITY_L4)return BT_GATT_ERR(BT_ATT_ERR_AUTHENTICATION);
    if(offset||(flags&BT_GATT_WRITE_FLAG_PREPARE))return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    const uint8_t *packet=buffer;struct card_image_begin begin={0};uint8_t op=0;uint32_t id=0;
    int error=length==20?image_decode_begin(packet,length,&begin):image_decode_command(packet,length,&op,&id);
    if(error)return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
    if(length==20){op=1;id=begin.id;}
    k_mutex_lock(&image_lock,K_FOREVER);
    if(!initialized)error=-EACCES;
    else if(op==3) {
        if(id!=transfer_id||owner!=conn)error=-EINVAL;
        else if(state==IMAGE_STORED||state==IMAGE_DISPLAYING||state==IMAGE_DISPLAYED||
                (state==IMAGE_COMMITTING&&commit_decided))error=-EALREADY;
        else cancel_locked(0);
    } else if(busy)error=-EBUSY;
    else if(op==1) {
        if(state==IMAGE_STORED||state==IMAGE_DISPLAYING)error=-EBUSY;
        else {
            release_owner_locked();owner=bt_conn_ref(conn);++token;transfer_id=id;
            expected_crc=begin.crc;received=rolling_crc=0;last_length=0;image_error=0;commit_decided=false;state=IMAGE_PREPARING;
        }
    } else if(owner!=conn||id!=transfer_id||state!=IMAGE_RECEIVING||received!=CARD_IMAGE_BYTES||rolling_crc!=expected_crc)error=-EINVAL;
    else state=IMAGE_COMMITTING;
    if(!error&&op!=3) {
        operation=op;operation_token=token;operation_length=length;memcpy(operation_bytes,packet,length);
        busy=true;renew_locked(conn);k_sem_give(&image_requested);
    }
    k_mutex_unlock(&image_lock);
    return error?BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED):length;
}
static ssize_t write_data(struct bt_conn *conn,const struct bt_gatt_attr *attr,const void *buffer,uint16_t length,uint16_t offset,uint8_t flags) {
    ARG_UNUSED(attr);
    if(bt_conn_get_security(conn)!=BT_SECURITY_L4)return BT_GATT_ERR(BT_ATT_ERR_AUTHENTICATION);
    if(offset||(flags&BT_GATT_WRITE_FLAG_PREPARE))return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    struct card_image_chunk chunk;int error=image_decode_chunk(buffer,length,&chunk);if(error)return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
    k_mutex_lock(&image_lock,K_FOREVER);
    if(!initialized||owner!=conn||state!=IMAGE_RECEIVING)error=-EACCES;
    else if(busy)error=-EBUSY;
    else error=image_chunk_order(&chunk,transfer_id,received,last_offset,last_length,last_bytes);
    if(error>=0) {
        renew_locked(conn);
        if(!error){operation=4;operation_token=token;operation_length=length;memcpy(operation_bytes,buffer,length);busy=true;k_sem_give(&image_requested);}
    }
    k_mutex_unlock(&image_lock);
    return error<0?BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED):length;
}
/* Zephyr sorts static services by their linker-section name. Append auxiliary
 * services after environment_service to preserve its bonded clients' handles. */
BT_GATT_SERVICE_DEFINE(zz_card_image_service,
    BT_GATT_PRIMARY_SERVICE(&image_service_uuid),
    BT_GATT_CHARACTERISTIC(&control_uuid.uuid,BT_GATT_CHRC_READ|BT_GATT_CHRC_WRITE,IMAGE_PERMISSIONS,read_status,write_control,NULL),
    BT_GATT_CHARACTERISTIC(&data_uuid.uuid,BT_GATT_CHRC_WRITE,IMAGE_PERMISSIONS,NULL,write_data,NULL),
    BT_GATT_CHARACTERISTIC(&status_uuid.uuid,BT_GATT_CHRC_READ,IMAGE_PERMISSIONS,read_status,NULL,NULL));

static void image_disconnected(struct bt_conn *conn,uint8_t reason) {
    ARG_UNUSED(reason);k_mutex_lock(&image_lock,K_FOREVER);
    if(owner==conn){if(state==IMAGE_RECEIVING||state==IMAGE_PREPARING||
        (state==IMAGE_COMMITTING&&!commit_decided))cancel_locked(-ECONNRESET);else release_owner_locked();}
    k_mutex_unlock(&image_lock);
}
BT_CONN_CB_DEFINE(card_image_callbacks)={.disconnected=image_disconnected};
uint32_t card_image_display_started(void) {
    k_mutex_lock(&image_lock,K_FOREVER);
    uint32_t request=0;
    if(state==IMAGE_STORED){request=token;state=IMAGE_DISPLAYING;if(owner)renew_locked(owner);}
    k_mutex_unlock(&image_lock);return request;
}
void card_image_display_result(uint32_t request_token,int error) {
    k_mutex_lock(&image_lock,K_FOREVER);
    if(request_token&&request_token==token&&(state==IMAGE_STORED||state==IMAGE_DISPLAYING)) {
        state=error?IMAGE_ERROR:IMAGE_DISPLAYED;image_error=error;release_owner_locked();
    }
    k_mutex_unlock(&image_lock);
    printk("IMAGE: display result=%d\n",error);
}
int card_image_transfer_init(void) {
    k_mutex_lock(&image_lock,K_FOREVER);initialized=true;k_mutex_unlock(&image_lock);
    printk("IMAGE GATT: control=%u data=%u status=%u (appended after environment)\n",
        bt_gatt_attr_get_handle(&attr_zz_card_image_service[2]),
        bt_gatt_attr_get_handle(&attr_zz_card_image_service[4]),
        bt_gatt_attr_get_handle(&attr_zz_card_image_service[6]));
    /* Register only: preserve automatic saved-history selection at boot. */
    return card_display_register_image_file(IMAGE_PREFIX);
}
