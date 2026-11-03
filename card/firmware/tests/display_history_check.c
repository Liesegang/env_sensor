/* Application history rings and mode/EPD sequencing, without a physical panel. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
int64_t test_uptime_ms;
#include "../src/card_display.c"
static int init_count,refresh_count,sleep_count,image_started,image_completed,image_error;
static int load_error;
static uint32_t image_token=23,completed_tokens[8];
static int pairing_completed;
static uint32_t pairing_completed_id;
static void (*during_refresh)(void);
static char caption_used[86],label_used[5];
int sd_logger_restore_utc_async(void){return 0;}
void sd_logger_bus_lock(void){}
void sd_logger_bus_unlock(void){}
void env_telemetry_ble_pairing_display_ready(uint32_t id,int e){assert(!e);++pairing_completed;pairing_completed_id=id;}
int epd3in6e_init(struct epd3in6e*d){d->initialized=true;++init_count;return 0;}
int epd3in6e_refresh_frame(struct epd3in6e*d,const uint8_t*f,size_t n){
    assert(d->initialized&&f&&n==120000);++refresh_count;
    if(during_refresh){void(*callback)(void)=during_refresh;during_refresh=NULL;callback();}
    return 0;
}
int epd3in6e_sleep(struct epd3in6e*d){d->initialized=false;++sleep_count;return 0;}
int epd_dashboard_render_labeled(uint8_t*f,size_t n,const struct epd_dashboard_series*s,size_t c,const char*caption,const char*label){assert(f&&n==120000&&s&&c==8);strcpy(caption_used,caption);strcpy(label_used,label);return 0;}
int epd_dashboard_pairing_render(uint8_t*f,size_t n,uint32_t pin){(void)f;(void)n;(void)pin;return 0;}
int card_image_load_file(const char*p,uint8_t*f,size_t n){assert(!strcmp(p,"/SD:/CARDIMG")&&f==frame&&n==120000);return load_error;}
uint32_t card_image_display_started(void){++image_started;return image_token;}
void card_image_display_result(uint32_t token,int e){assert(image_completed<8);completed_tokens[image_completed++]=token;image_error=e;}
static struct card_sample sample(uint32_t uptime,uint64_t utc,int16_t co2){return(struct card_sample){.sequence=uptime+1,.uptime_s=uptime,.unix_s=utc,.co2_ppm=co2,.valid=CARD_VALID_CO2};}
static bool present(size_t col){return(snapshot[SERIES_CO2].valid_bits[col/8]&BIT(col%8))!=0;}
static void finish_refresh(void){
    assert(refresh_pending&&!k_sem_take(&refresh_requested,K_NO_WAIT));
    process_refresh_requests();assert(!refresh_pending&&!refresh_requested.count);
}
static void periodic_during_refresh(void){
    uint32_t revision=requested_revision;
    assert(!card_display_refresh_periodic());
    assert(refresh_pending&&requested_revision==revision&&!refresh_requested.count);
}
static void new_images_during_refresh(void){
    periodic_during_refresh();
    image_token=24;assert(!card_display_set_image_file("/SD:/CARDIMG"));
    image_token=25;assert(!card_display_set_image_file("/SD:/CARDIMG"));
    periodic_during_refresh();
}
static bool restore_utc;
static void saved_during_refresh(void){
    card_display_saved_begin(restore_utc,restore_utc?1800000600:600);
    struct card_sample saved=sample(600,restore_utc?1800000600:0,1800);
    card_display_saved_record(&saved);card_display_saved_end();
}
int main(int argc,char**argv)
{
    assert(argc==2);assert(!card_display_init(true));
    assert(sizeof(history)+sizeof(snapshot)+sizeof(saved_history)+sizeof(sample_stamp)+sizeof(saved_stamp)<12000);
    if(!strcmp(argv[1],"bins")){
        for(uint32_t i=0;i<600;++i){struct card_sample s=sample(i,0,600+i);card_display_record(&s);}
        assert(take_snapshot(599,false)==1&&present(143));assert(snapshot[SERIES_CO2].current_value==1199);
        struct card_sample s=sample(600,0,1500);card_display_record(&s);assert(take_snapshot(600,false)==2&&present(142)&&present(143));
        s=sample(1800,0,1600);card_display_record(&s);assert(take_snapshot(1800,false)==3&&!present(142)&&present(143)&&present(141));
        assert(!snapshot[SERIES_HCHO].has_current_value);
    }else if(!strcmp(argv[1],"unknown")){
        card_display_saved_begin(false,86410);struct card_sample saved=sample(86410,0,1800);card_display_saved_record(&saved);card_display_saved_end();
        struct card_sample live=sample(10,0,500);card_display_record(&live);
        assert(take_snapshot(10,false)==1&&snapshot[SERIES_CO2].current_value==500);
        assert(take_snapshot(86410,true)==1&&snapshot[SERIES_CO2].current_value==1800);
        card_display_button(CARD_SWITCH_UP,true,1,NULL);finish_refresh();assert(!strcmp(label_used,"LAST")&&strstr(caption_used,"CLOCK UNKNOWN"));
        card_display_button(CARD_SWITCH_DOWN,true,1,NULL);finish_refresh();assert(!strcmp(label_used,"NOW")&&strstr(caption_used,"CURRENT"));
    }else if(!strcmp(argv[1],"utcmerge")){
        uint64_t base=1800000000;
        card_display_saved_begin(true,base+600);struct card_sample saved=sample(1,base,1000);card_display_saved_record(&saved);saved=sample(601,base+600,1100);card_display_saved_record(&saved);card_display_saved_end();
        struct card_sample live=sample(10,0,500);card_display_record(&live);assert(take_snapshot(10,false)==1);
        live=sample(11,base+1200,1200);card_display_record(&live);assert(live_utc);assert(take_snapshot(base+1200,false)==3&&present(141)&&present(142)&&present(143));assert(snapshot[SERIES_CO2].current_value==1200);
        live=sample(88811,base+90000,900);card_display_record(&live);assert(take_snapshot(base+90000,false)==1);assert(snapshot[SERIES_CO2].current_value==900);
    }else if(!strcmp(argv[1],"image")){
        assert(!card_display_set_image_file("/SD:/CARDIMG"));finish_refresh();assert(image_started==1&&image_completed==1&&!image_error&&refresh_count==1&&sleep_count==1);
        load_error=-EILSEQ;assert(!card_display_set_image_file("/SD:/CARDIMG"));finish_refresh();assert(image_completed==2&&image_error==-EILSEQ&&refresh_count==1&&init_count==1);
    }else if(!strcmp(argv[1],"image_periodic")){
        struct card_sample first=sample(0,0,600);card_display_record(&first);
        assert(!card_display_set_image_file("/SD:/CARDIMG"));finish_refresh();
        uint32_t revision=requested_revision;
        for(uint32_t i=1;i<=600;++i){
            test_uptime_ms=(int64_t)i*1000;
            struct card_sample measured=sample(i,i>=60?1800000000+i:0,600+i);
            card_display_record(&measured);
            if(i%60==0){assert(!card_display_refresh_periodic());}
            assert(!refresh_pending&&!refresh_requested.count&&requested_revision==revision);
        }
        assert(image_started==1&&image_completed==1&&refresh_count==1&&sleep_count==1);
        assert(!card_display_refresh());finish_refresh();assert(refresh_count==2&&image_completed==2);
        card_display_button(CARD_SWITCH_PUSH,true,1,NULL);finish_refresh();
        assert(requested_mode==CARD_DISPLAY_GRAPH&&refresh_count==3&&strstr(caption_used,"CURRENT UTC"));
        card_display_button(CARD_SWITCH_PUSH,true,2,NULL);finish_refresh();
        assert(requested_mode==CARD_DISPLAY_IMAGE&&refresh_count==4&&image_completed==3);
    }else if(!strcmp(argv[1],"graph_periodic")){
        struct card_sample measured=sample(0,0,600);card_display_record(&measured);
        assert(!card_display_refresh_periodic());finish_refresh();
        assert(refresh_count==1&&snapshot[SERIES_CO2].current_value==600);
        test_uptime_ms=600000;measured=sample(600,0,1500);card_display_record(&measured);
        assert(!card_display_refresh_periodic());assert(card_display_refresh_periodic()==-EALREADY);
        finish_refresh();assert(refresh_count==2&&sleep_count==2&&image_started==0);
        assert(snapshot[SERIES_CO2].current_value==1500&&present(142)&&present(143));
    }else if(!strcmp(argv[1],"image_inflight")){
        assert(!card_display_set_image_file("/SD:/CARDIMG"));
        during_refresh=periodic_during_refresh;finish_refresh();
        assert(refresh_count==1&&image_completed==1&&completed_tokens[0]==23);
        assert(!card_display_set_image_file("/SD:/CARDIMG"));
        during_refresh=new_images_during_refresh;finish_refresh();
        assert(refresh_count==3&&image_started==3&&image_completed==3&&sleep_count==3);
        assert(completed_tokens[1]==23&&completed_tokens[2]==25&&requested_mode==CARD_DISPLAY_IMAGE);
        uint32_t revision=requested_revision;
        assert(!card_display_refresh_periodic());
        assert(!refresh_pending&&!refresh_requested.count&&requested_revision==revision&&image_completed==3);
    }else if(!strcmp(argv[1],"pairing_periodic")){
        assert(!card_display_set_image_file("/SD:/CARDIMG"));finish_refresh();
        card_display_pairing_event(ENV_TELEMETRY_PAIRING_OPENED,0,0,NULL);
        assert(card_display_refresh_periodic()==-EBUSY&&!refresh_pending);
        card_display_pairing_event(ENV_TELEMETRY_PAIRING_PASSKEY,123456,17,NULL);
        uint32_t revision=requested_revision;
        assert(card_display_refresh_periodic()==-EBUSY&&requested_revision==revision);
        finish_refresh();assert(pairing_completed==1&&pairing_completed_id==17&&image_completed==1);
        card_display_pairing_event(ENV_TELEMETRY_PAIRING_CLOSED,0,0,NULL);
        finish_refresh();assert(refresh_count==3&&image_completed==2&&requested_mode==CARD_DISPLAY_IMAGE);
        assert(!card_display_refresh_periodic()&&!refresh_pending&&!refresh_requested.count);
    }else if(!strcmp(argv[1],"saved_inflight")||!strcmp(argv[1],"saved_utc_inflight")){
        restore_utc=!strcmp(argv[1],"saved_utc_inflight");
        struct card_sample measured=sample(10,0,600);card_display_record(&measured);
        assert(!card_display_refresh_periodic());during_refresh=saved_during_refresh;
        finish_refresh();assert(refresh_count==2&&sleep_count==2&&requested_saved);
        assert(!strcmp(label_used,"LAST")&&strstr(caption_used,restore_utc?"SAVED UTC":"CLOCK UNKNOWN"));
        assert(snapshot[SERIES_CO2].current_value==1800);
    }else if(!strcmp(argv[1],"clock_rebase")){
        uint64_t base=1800000000;
        card_display_saved_begin(true,base-600);
        struct card_sample saved=sample(10,base-600,1800);card_display_saved_record(&saved);card_display_saved_end();
        struct card_sample live=sample(0,base,600);card_display_record(&live);
        live=sample(600,base+600,800);card_display_record(&live);
        assert(take_snapshot(base+600,false)==3);
        live=sample(601,base-3000+1,900);card_display_record(&live);
        assert(latest_sequence==602&&latest_stamp==base-3000+1);
        assert(take_snapshot(latest_stamp,false)==2&&snapshot[SERIES_CO2].current_value==900);
        assert(saved_stamp[((base-600)/600)%144]==base-600);
        live=sample(602,base+602,1000);card_display_record(&live);
        assert(take_snapshot(latest_stamp,false)==3&&snapshot[SERIES_CO2].current_value==1000);
        assert(saved_stamp[((base-600)/600)%144]==base-600);
    }else{assert(0);}
    printf("Display history checks PASS: %s\n",argv[1]);
}
