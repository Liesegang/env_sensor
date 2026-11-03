/* Actual transport, codec and POSIX-backed actual SD/RAM reader. */
#include "../src/card_history.c"
char test_directory[256];
int test_write_failure=-1;
void (*test_sync_hook)(void);
size_t test_read_bytes;
void (*test_read_hook)(void);
off_t test_read_position;
size_t test_readdir_calls;
int64_t test_uptime_ms;
static struct card_ble_clock test_clock={.boot_id=0x1234};
static int lease_releases;
static int64_t held_until;
static bool link_active=true;
void card_ble_clock_snapshot(struct card_ble_clock*c){*c=test_clock;}
int env_telemetry_ble_hold_history(struct bt_conn*c,uint32_t ms){assert(c&&ms<=300000);if(!link_active)return -ENOTCONN;held_until=mock_now+ms;return 0;}
int env_telemetry_ble_set_history_lease(struct bt_conn*c,uint32_t ms){return env_telemetry_ble_hold_history(c,ms);}
void env_telemetry_ble_release_history(struct bt_conn*c){assert(c);++lease_releases;held_until=mock_now+5000;}
void card_display_saved_begin(bool utc,uint64_t anchor){(void)utc;(void)anchor;}
void card_display_saved_record(const struct card_sample*s){(void)s;}
void card_display_saved_end(void){}
static void put(uint8_t*p,uint64_t v,size_t n){for(size_t i=0;i<n;++i)p[i]=(uint8_t)(v>>(8*i));}
static uint64_t get(const uint8_t*p,size_t n){uint64_t v=0;for(size_t i=0;i<n;++i)v|=(uint64_t)p[i]<<(8*i);return v;}
static ssize_t control(struct bt_conn*p,uint8_t op,uint32_t req,uint64_t boot,uint32_t seq){
    uint8_t packet[20]={'H','C',1,op};put(packet+4,req,4);put(packet+8,boot,8);put(packet+16,seq,4);
    size_t before=test_read_bytes;ssize_t result=write_control(p,NULL,packet,20,0,0);assert(test_read_bytes==before);return result;
}
static void work(void){if(!setjmp(worker_done))history_worker(NULL,NULL,NULL);}
static size_t data(struct bt_conn*p,uint8_t*out){
    size_t length=page_length;for(size_t off=0;off<length;){ssize_t n=read_data(p,NULL,out+off,20,(uint16_t)off);assert(n>0);off+=(size_t)n;}return length;
}
static void queue_rows(uint32_t first,uint32_t last){
    for(uint32_t i=first;i<=last;++i){struct card_sample sample={.sequence=i,.uptime_s=i-1,.valid=CARD_VALID_CO2,.co2_ppm=(int16_t)(600+i),.temperature_mc=-9999,.humidity_mpercent=99999};assert(!sd_logger_queue(&sample));}
}
static struct bt_conn *cancel_peer;
static void cancel_during_read(void){
    assert(worker_busy&&!pull_pending);
    assert(control(cancel_peer,ENV_HISTORY_OP_CANCEL,8,0,0)==20);
    assert(control(cancel_peer,ENV_HISTORY_OP_PULL,9,0,0)<0);
    queue_rows(14,14); /* Sensor producer continues while storage I/O runs. */
}
static void slow_prepare_read(void){mock_now=299000;}
int main(int argc,char**argv){
    assert(argc==3);snprintf(test_directory,sizeof(test_directory),"%s",argv[2]);
    assert(!sd_logger_set_boot_id(test_clock.boot_id));assert(!sd_logger_init());assert(!card_history_init());queue_rows(1,13);
    struct bt_conn peer={.security=4,.subscribed=true};uint8_t bytes[440],again[440],hs[72];
    assert(read_status(&peer,NULL,hs,20,0)==20&&owner==&peer&&held_until==120000);
    if(!strcmp(argv[1],"replay")){
        assert(control(&peer,ENV_HISTORY_OP_PULL,1,0,0)==20);assert(status.state==ENV_HISTORY_STATE_PREPARING&&!test_read_bytes);
        assert(held_until==300000);
        work();assert(status.state==ENV_HISTORY_STATE_READY&&page_length==440&&notify_count==1&&parameter_requests==1&&held_until==120000);
        assert(read_data(&peer,NULL,bytes,20,0)==20);
        assert(control(&peer,ENV_HISTORY_OP_ACK,1,0x1234,6)<0);
        assert(data(&peer,bytes)==440);
        assert(control(&peer,ENV_HISTORY_OP_ACK,99,0x1234,6)<0);
        assert(control(&peer,ENV_HISTORY_OP_ACK,1,0x1234,5)<0);
        assert(control(&peer,ENV_HISTORY_OP_PULL,99,0,0)<0&&data(&peer,again)==440&&!memcmp(bytes,again,440));
        history_disconnected(&peer,19);assert(!peer.refs&&!owner&&status.state==ENV_HISTORY_STATE_IDLE);
        assert(control(&peer,ENV_HISTORY_OP_PULL,1,0,0)==20);work();assert(data(&peer,again)==440&&!memcmp(bytes,again,440));
        assert(control(&peer,ENV_HISTORY_OP_ACK,1,0x1234,6)==20);assert(status.state==ENV_HISTORY_STATE_IDLE);
        assert(control(&peer,ENV_HISTORY_OP_ACK,1,0x1234,6)==20);work();
        assert(control(&peer,ENV_HISTORY_OP_PULL,2,0x1234,6)==20);work();assert(data(&peer,bytes)==440);
        assert(get(bytes+16,4)==7&&get(bytes+32,4)==7);
        /* Clock sync may change HS, but the pending HP is frozen until ACK. */
        test_clock.revision=1;test_clock.unix_anchor_s=1800000000;test_clock.uptime_anchor_s=0;
        assert(read_status(&peer,NULL,hs,72,0)==72&&get(hs+56,4)==1);
        assert(data(&peer,again)==440&&!memcmp(bytes,again,440));
        assert(control(&peer,ENV_HISTORY_OP_ACK,2,0x1234,12)==20);work();
        assert(parameter_requests==2); /* One request per connection, not per page. */
        assert(control(&peer,ENV_HISTORY_OP_PULL,3,0x1234,12)==20);work();assert(data(&peer,bytes)==100);
        assert(get(bytes+16,4)==13&&get(bytes+28,4)==1&&get(bytes+32+8,8)==1800000012);
        assert(get(bytes+32+16,4)==0&&get(bytes+32+20,4)==0); /* Invalid RAM garbage canonicalized. */
        assert(control(&peer,ENV_HISTORY_OP_ACK,3,0x1234,13)==20&&status.state==ENV_HISTORY_STATE_COMPLETE&&!owner&&peer.refs==0);
        assert(held_until>=mock_now+5000&&control(&peer,ENV_HISTORY_OP_ACK,3,0x1234,13)==20);
        assert(read_status(&peer,NULL,hs,20,0)==20&&held_until==mock_now+5000&&!owner&&!peer.refs);
        for(uint16_t offset=20;offset<72;offset+=20){assert(read_status(&peer,NULL,hs+offset,20,offset)>0);assert(held_until==mock_now+5000&&!owner&&!peer.refs);}
        history_disconnected(&peer,19);assert(status.state==ENV_HISTORY_STATE_IDLE&&!owner);
        assert(read_status(&peer,NULL,hs,20,0)==20&&held_until==mock_now+120000&&owner==&peer);
        /* Saving the same RAM rows to CSV changes neither their identity nor
         * canonical wire values. The reader deduplicates the overlapping RAM.
         */
        struct card_sample saved[13];for(uint32_t i=1;i<=13;++i)saved[i-1]=(struct card_sample){.sequence=i,.uptime_s=i-1,.valid=CARD_VALID_CO2,.co2_ppm=(int16_t)(600+i),.temperature_mc=-9999,.humidity_mpercent=99999};
        size_t committed;assert(!sd_logger_append(saved,13,&committed)&&committed==13);
        assert(control(&peer,ENV_HISTORY_OP_PULL,3,0x1234,12)==20);work();assert(data(&peer,again)==100&&!memcmp(bytes,again,100));
        assert(control(&peer,ENV_HISTORY_OP_ACK,3,0x1234,13)==20&&status.state==ENV_HISTORY_STATE_COMPLETE);
        assert(control(&peer,ENV_HISTORY_OP_CANCEL,4,0,0)==20);work();
    }else if(!strcmp(argv[1],"cancel_timeout")){
        assert(control(&peer,ENV_HISTORY_OP_PULL,1,0,0)==20);
        assert(control(&peer,ENV_HISTORY_OP_CANCEL,2,0,0)==20);work();assert(!worker_busy&&!pull_pending&&!owner&&!test_read_bytes);
        assert(control(&peer,ENV_HISTORY_OP_PULL,3,0,0)==20);history_disconnected(&peer,19);work();assert(!worker_busy&&!test_read_bytes);
        assert(control(&peer,ENV_HISTORY_OP_PULL,4,0,0)==20);work();assert(status.state==ENV_HISTORY_STATE_READY);
        assert(read_data(&peer,NULL,bytes,20,420)==20&&!page_read_complete);
        assert(control(&peer,ENV_HISTORY_OP_ACK,4,0x1234,6)<0);
        mock_now=120001;history_expired(&history_timeout.work);assert(status.state==ENV_HISTORY_STATE_ERROR&&!owner&&!peer.refs);
        assert(control(&peer,ENV_HISTORY_OP_PULL,5,0,0)==20);work();assert(data(&peer,bytes)==440&&get(bytes+16,4)==1);
        assert(control(&peer,ENV_HISTORY_OP_CANCEL,6,0,0)==20);work();assert(!owner&&!peer.refs);
        peer.security=3;assert(control(&peer,ENV_HISTORY_OP_PULL,7,0,0)==BT_GATT_ERR(BT_ATT_ERR_AUTHENTICATION));
    }else if(!strcmp(argv[1],"cancel_inflight")){
        struct card_sample saved={.sequence=1,.uptime_s=0,.valid=CARD_VALID_CO2,.co2_ppm=601};
        size_t committed;assert(!sd_logger_append(&saved,1,&committed)&&committed==1);
        int before_notify=notify_count;
        assert(control(&peer,ENV_HISTORY_OP_PULL,7,0,0)==20);
        cancel_peer=&peer;test_read_hook=cancel_during_read;work();
        assert(!worker_busy&&!owner&&peer.refs==0&&status.state==ENV_HISTORY_STATE_IDLE);
        assert(notify_count==before_notify); /* Late SD result cannot publish. */
        assert(control(&peer,ENV_HISTORY_OP_PULL,10,0x1234,13)==20);work();
        assert(data(&peer,bytes)==100&&get(bytes+16,4)==14);
        assert(control(&peer,ENV_HISTORY_OP_ACK,10,0x1234,14)==20&&status.state==ENV_HISTORY_STATE_COMPLETE);
    }else if(!strcmp(argv[1],"preparing_lease")){
        struct card_sample saved={.sequence=1,.uptime_s=0,.valid=CARD_VALID_CO2,.co2_ppm=601};
        size_t committed;assert(!sd_logger_append(&saved,1,&committed)&&committed==1);
        assert(control(&peer,ENV_HISTORY_OP_PULL,1,0,0)==20&&lease_deadline==300000&&held_until==300000);
        mock_now=120001;history_expired(&history_timeout.work);
        assert(status.state==ENV_HISTORY_STATE_PREPARING&&owner==&peer);
        mock_now=150000;assert(read_status(&peer,NULL,hs,72,0)==72);
        assert(lease_deadline==300000&&held_until==300000); /* Polling cannot extend preparation. */
        test_read_hook=slow_prepare_read;work();
        assert(status.state==ENV_HISTORY_STATE_READY&&held_until==419000&&lease_deadline==419000);
        mock_now=419001;history_expired(&history_timeout.work);
        assert(status.state==ENV_HISTORY_STATE_ERROR&&!owner&&!peer.refs);
        mock_now=500000;assert(control(&peer,ENV_HISTORY_OP_PULL,2,0,0)==20&&lease_deadline==800000);
        mock_now=800001;history_expired(&history_timeout.work);work();
        assert(status.state==ENV_HISTORY_STATE_ERROR&&!owner&&!worker_busy&&!pull_pending);
    }else{assert(0);}
    printf("Card history transport PASS: %s\n",argv[1]);
}
