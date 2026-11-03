/* Real CSV/file round trips and deterministic producer-during-flush checks. */
#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
char test_directory[256];
int test_write_failure = -1;
void (*test_sync_hook)(void);
size_t test_read_bytes;
void (*test_read_hook)(void);
off_t test_read_position;
size_t test_readdir_calls;
#include "../src/sd_logger.c"

static struct card_sample restored_rows[86402];
static size_t restored_count;
static bool restored_utc;
static uint64_t restored_anchor;
static int restore_ended;
void card_display_saved_begin(bool utc, uint64_t anchor) { restored_count=0;restored_utc=utc;restored_anchor=anchor; }
void card_display_saved_record(const struct card_sample*s) { assert(restored_count<ARRAY_SIZE(restored_rows));restored_rows[restored_count++]=*s; }
void card_display_saved_end(void) { ++restore_ended; }

static struct card_sample make_sample(uint32_t sequence,uint32_t uptime,uint64_t utc)
{
    return (struct card_sample){.sequence=sequence,.uptime_s=uptime,.unix_s=utc,
        .temperature_mc=-12345,.humidity_mpercent=45678,.hcho_millippb=123456,
        .bme_temperature_mc=20123,.bme_humidity_mpercent=40123,.pressure_pa=101234,
        .gas_resistance_ohm=123456789,.millilux=4294967000U,.co2_ppm=1321,
        .raw_voc=54321,.raw_nox=12345,.voc_index=175,.nox_index=13,
        .stcc_status=65535,.stcc_self_test=32768,.valid=0x1fff,
        .rht_source=CARD_RHT_SHT45,.gas_valid=1,.heater_stable=1};
}
static void remove_column(char *row,size_t index)
{
    char *start=row;
    for(size_t i=0;i<index;++i){start=strchr(start,',');assert(start);++start;}
    char *end=strchr(start,',');
    if(end)memmove(start,end+1,strlen(end+1)+1);
    else{assert(start>row);strcpy(start-1,"\n");}
}
static void write_log(int index,uint32_t first,size_t count,uint32_t step,uint64_t utc_base,bool legacy)
{
    char path[512];snprintf(path,sizeof(path),"%s/LOG%05d.CSV",test_directory,index);
    FILE*f=fopen(path,"w");assert(f);
    char header[512];strcpy(header,csv_header);if(legacy){remove_column(header,COL_BOOT);remove_column(header,2);}fputs(header,f);
    for(size_t i=0;i<count;++i){struct card_sample s=make_sample(i+1,first+i*step,utc_base?utc_base+i*step:0);assert(format_sample(&s)>0);if(legacy){remove_column(line,COL_BOOT);remove_column(line,2);}fputs(line,f);}
    fclose(f);
}
static void producer_during_sync(void)
{
    struct card_sample sample=make_sample(601,600,1800000600);
    assert(!sd_logger_queue(&sample));
}
static void write_boot_log(int index,uint64_t identity,size_t count)
{
    uint64_t original=current_boot_id;current_boot_id=identity;
    write_log(index,0,count,1,0,false);current_boot_id=original;
}
static bool history_producer_ran;
static void history_producer_hook(void)
{
    if(test_read_position==(off_t)(sizeof(csv_header)-1)){
        struct card_sample s=make_sample(7,6,0);assert(!sd_logger_queue(&s));history_producer_ran=true;
    }else{test_read_hook=history_producer_hook;}
}
static void parse_file(const char *path,size_t expected)
{
    struct fs_file_t file;fs_file_t_init(&file);assert(!fs_open(&file,path,FS_O_READ));
    struct csv_reader reader={.file=&file};struct csv_schema schema;
    assert(reader_line(&reader,line,sizeof(line))==1);assert(!parse_header(line,&schema));
    size_t count=0;while(reader_line(&reader,line,sizeof(line))==1){struct card_sample s;assert(!parse_row(line,&schema,&s));assert(s.sequence==++count);assert(s.unix_s==1800000000+s.uptime_s);assert(s.millilux==4294967000U);assert(s.temperature_mc==-12345);assert(s.hcho_millippb==123456);assert(s.valid==0x1fff);}
    assert(count==expected);assert(!fs_close(&file));
}
int main(int argc,char **argv)
{
    assert(argc==3);snprintf(test_directory,sizeof(test_directory),"%s",argv[2]);
    assert(sizeof(struct card_sample)==72);
    assert(!sd_logger_set_boot_id(0x1234));
    if(!strcmp(argv[1],"roundtrip")){
        assert(!sd_logger_init());
        for(uint32_t i=0;i<600;++i){struct card_sample s=make_sample(i+1,i,1800000000+i);assert(!sd_logger_queue(&s));}
        /* Header fsync happens before the batch, still with producer unlocked. */
        test_sync_hook=producer_during_sync;flush_queue();assert(queue_count==1);assert(!rollback_pending);
        parse_file(filename,600);flush_queue();assert(!queue_count);parse_file(filename,601);
        assert(!sd_logger_flush_async());assert(sd_logger_flush_async()==-EALREADY);
    }else if(!strcmp(argv[1],"rollback")){
        assert(!sd_logger_init());
        for(uint32_t i=0;i<10;++i){struct card_sample s=make_sample(i+1,i,1800000000+i);assert(!sd_logger_queue(&s));}
        test_write_failure=5;flush_queue();assert(queue_count==10);assert(!rollback_pending);
        test_write_failure=-1;flush_queue();assert(!queue_count);parse_file(filename,10);
    }else if(!strcmp(argv[1],"capacity")){
        assert(!sd_logger_init());struct card_sample s=make_sample(1,0,0);
        for(size_t i=0;i<SD_QUEUE_CAPACITY;++i)assert(!sd_logger_queue(&s));
        assert(sd_logger_queue(&s)==-ENOSPC);assert(queue_count==SD_QUEUE_CAPACITY);
    }else if(!strcmp(argv[1],"legacy")){
        write_log(0,0,1440,60,0,true);write_log(1,100,20,60,0,true);
        restore_history(false);assert(restore_ended==1&&!restored_utc&&restored_count==20);assert(restored_anchor==1240);
        assert(!restored_rows[0].unix_s&&restored_rows[19].co2_ppm==1321);
        assert(restored_rows[19].temperature_mc==-12345&&restored_rows[19].valid==0x1fff);
    }else if(!strcmp(argv[1],"utc24h")){
        write_log(0,0,43200,1,1800000000,false);write_log(1,0,50400,1,1800043200,false);
        /* An interrupted tail must not become a reading. */
        char path[512];snprintf(path,sizeof(path),"%s/LOG00001.CSV",test_directory);FILE*f=fopen(path,"a");assert(f);fputs("999999,99999,1800999999,0x",f);fclose(f);
        restore_history(false);assert(restore_ended==1&&restored_utc&&restored_count==86401);assert(restored_anchor==1800093599);
        for(size_t i=0;i<restored_count;++i)assert(restored_rows[i].unix_s>=restored_anchor-86400&&restored_rows[i].unix_s<=restored_anchor);
    }else if(!strcmp(argv[1],"clock_late")){
        write_log(0,0,1200,1,1800000000,false);write_log(1,0,20,60,0,true);
        restore_history(false);assert(!restored_utc&&restored_count==20);
        restore_history(true);assert(restored_utc&&restored_count==1200&&restored_anchor==1800001199);
    }else if(!strcmp(argv[1],"clock_unknown_fallback")){
        write_log(0,100,20,60,0,true);
        restore_history(false);assert(restore_ended==1&&!restored_utc&&restored_count==20);
        uint64_t saved_anchor=restored_anchor;
        struct card_sample last_saved=restored_rows[19];
        /* First phone sync cannot invent UTC for an older boot. Keep it
         * available as a saved session when no UTC archive exists yet. */
        restore_history(true);
        assert(restore_ended==1&&!restored_utc&&restored_count==20);
        assert(restored_anchor==saved_anchor);
        assert(!memcmp(&restored_rows[19],&last_saved,sizeof(last_saved)));
    }else if(!strcmp(argv[1],"missing")){
        struct card_sample s=make_sample(1,0,0);s.valid&=~CARD_VALID_HCHO;assert(format_sample(&s)>0);
        char header[512];strcpy(header,csv_header);struct csv_schema schema;assert(!parse_header(header,&schema));
        struct card_sample parsed;assert(!parse_row(line,&schema,&parsed));assert(!(parsed.valid&CARD_VALID_HCHO));assert(parsed.valid&CARD_VALID_CO2);
    }else if(!strcmp(argv[1],"history_boundary")){
        assert(!sd_logger_init());
        for(uint32_t i=0;i<640;++i){struct card_sample s=make_sample(i+1,i,0);assert(!sd_logger_queue(&s));}
        struct card_sample rows[6];struct sd_logger_history_page page;
        assert(!sd_logger_history_read((struct sd_logger_cursor){0},rows,6,&page));
        assert(page.count==6&&page.boot_id==0x1234&&page.more&&!page.gap);
        assert(page.earliest.sequence==1&&page.latest.sequence==640);
        struct sd_logger_cursor after={0x1234,597};
        assert(!sd_logger_history_read(after,rows,6,&page));assert(rows[0].sequence==598&&rows[5].sequence==603);
        flush_queue();assert(queue_count==40);
        assert(!sd_logger_history_read(after,rows,6,&page));
        assert(page.count==6&&rows[0].sequence==598&&rows[5].sequence==603&&page.boot_id==0x1234&&!page.gap);
        assert(!sd_logger_history_read((struct sd_logger_cursor){0x1234,637},rows,6,&page));
        assert(page.count==3&&rows[0].sequence==638&&rows[2].sequence==640&&!page.more);
        assert(!sd_logger_history_read((struct sd_logger_cursor){0x1234,640},rows,6,&page));assert(!page.count&&!page.gap);
    }else if(!strcmp(argv[1],"history_reboot_gap")){
        write_log(0,0,8,1,0,true);assert(!sd_logger_init());
        for(uint32_t i=0;i<3;++i){struct card_sample s=make_sample(i+1,i,0);assert(!sd_logger_queue(&s));}
        struct card_sample rows[6];struct sd_logger_history_page page;
        uint64_t legacy=UINT64_C(0x8000000100000000);
        assert(!sd_logger_history_read((struct sd_logger_cursor){0},rows,6,&page));assert(page.boot_id==legacy&&page.count==6);
        assert(!sd_logger_history_read((struct sd_logger_cursor){legacy,6},rows,6,&page));assert(page.count==2&&page.more&&rows[0].sequence==7);
        assert(!sd_logger_history_read((struct sd_logger_cursor){legacy,8},rows,6,&page));assert(page.boot_id==0x1234&&page.count==3&&!page.more);
        /* The phone may have ACKed an unflushed row in a previous boot. Both
         * a missing boot and a missing sequence restart with an explicit GAP.
         */
        assert(!sd_logger_history_read((struct sd_logger_cursor){0xdead,12},rows,6,&page));assert(page.gap&&page.boot_id==legacy&&rows[0].sequence==1);
        assert(!sd_logger_history_read((struct sd_logger_cursor){legacy,12},rows,6,&page));assert(page.gap&&page.boot_id==legacy&&rows[0].sequence==1);
    }else if(!strcmp(argv[1],"history_clock")){
        uint64_t original=current_boot_id;current_boot_id=0x5555;
        char path[512];snprintf(path,sizeof(path),"%s/LOG00000.CSV",test_directory);FILE*f=fopen(path,"w");assert(f);fputs(csv_header,f);
        for(uint32_t i=0;i<6;++i){struct card_sample s=make_sample(i+1,i,i>=3?1800000000+i:i?1800003600+i:0);assert(format_sample(&s)>0);fputs(line,f);}fclose(f);current_boot_id=original;
        write_log(1,0,2,1,0,true);
        restore_history(true);assert(restored_count==6&&restored_utc);
        for(uint32_t i=0;i<6;++i)assert(restored_rows[i].unix_s==1800000000+i);
        struct card_sample rows[6];struct sd_logger_history_page page;
        assert(!sd_logger_history_read((struct sd_logger_cursor){0},rows,6,&page));assert(page.boot_id==0x5555);
        for(uint32_t i=0;i<6;++i)assert(rows[i].unix_s==1800000000+i);
        assert(!sd_logger_history_read((struct sd_logger_cursor){0x5555,6},rows,6,&page));assert(page.count==2&&page.boot_id==legacy_boot_id(1));
        assert(!rows[0].unix_s&&!rows[1].unix_s);
    }else if(!strcmp(argv[1],"history_seek")){
        write_boot_log(0,0x5555,20000);
        struct card_sample rows[6];struct sd_logger_history_page page;
        assert(!sd_logger_history_read((struct sd_logger_cursor){0x5555,10000},rows,6,&page));assert(rows[0].sequence==10001&&rows[5].sequence==10006);
        test_read_bytes=0;
        assert(!sd_logger_history_read((struct sd_logger_cursor){0x5555,10006},rows,6,&page));
        assert(rows[0].sequence==10007&&rows[5].sequence==10012&&test_read_bytes<16384);
    }else if(!strcmp(argv[1],"history_exact")){
        write_boot_log(0,0x5555,600);
        struct card_sample rows[6];struct sd_logger_history_page page;
        assert(!sd_logger_history_read((struct sd_logger_cursor){0x5555,594},rows,6,&page));
        assert(page.count==6&&rows[5].sequence==600&&!page.more);
        assert(!sd_logger_history_read((struct sd_logger_cursor){0x5555,600},rows,6,&page));assert(!page.count&&!page.more);
        char path[512];snprintf(path,sizeof(path),"%s/LOG00000.CSV",test_directory);assert(!unlink(path));
        write_boot_log(1,0x5555,6);
        sd_logger_history_reset(); /* Reconnect after external media changes. */
        assert(!sd_logger_history_read((struct sd_logger_cursor){0},rows,6,&page));assert(page.count==6&&!page.more);
    }else if(!strcmp(argv[1],"history_producer")){
        assert(!sd_logger_init());
        struct card_sample initial[6];for(uint32_t i=0;i<6;++i)initial[i]=make_sample(i+1,i,0);
        size_t committed;assert(!sd_logger_append(initial,6,&committed)&&committed==6);
        test_read_hook=history_producer_hook;
        struct card_sample rows[6];struct sd_logger_history_page page;
        assert(!sd_logger_history_read((struct sd_logger_cursor){0},rows,6,&page));
        assert(history_producer_ran&&page.count==6&&rows[5].sequence==6&&page.latest.sequence==7&&page.more);
        assert(!sd_logger_history_read((struct sd_logger_cursor){0x1234,6},rows,6,&page));assert(page.count==1&&rows[0].sequence==7&&!page.more);
    }else if(!strcmp(argv[1],"history_archive_cache")){
        for(int i=0;i<64;++i)write_boot_log(i,0x5000+(uint64_t)i,72);
        struct card_sample rows[6];struct sd_logger_history_page page;
        assert(!sd_logger_history_read((struct sd_logger_cursor){0},rows,6,&page));
        assert(page.boot_id==0x5000&&page.count==6&&rows[5].sequence==6&&page.latest.boot_id==0x503f);
        assert(test_read_bytes>200000&&test_readdir_calls>64);
        test_read_bytes=test_readdir_calls=0;
        for(uint32_t seq=6;seq<72;seq+=6){
            assert(!sd_logger_history_read((struct sd_logger_cursor){0x5000,seq},rows,6,&page));
            assert(page.boot_id==0x5000&&rows[0].sequence==seq+1&&rows[5].sequence==seq+6);
        }
        assert(test_readdir_calls==0&&test_read_bytes<=11*2048);
        printf("Archive cache: 64 files, 11 sequential pages read %zu bytes, directory scans=%zu\n",test_read_bytes,test_readdir_calls);
        test_read_bytes=test_readdir_calls=0;
        assert(!sd_logger_history_read((struct sd_logger_cursor){0x5000,72},rows,6,&page));
        assert(page.boot_id==0x5001&&rows[0].sequence==1&&test_readdir_calls==0&&test_read_bytes<8192);
        test_read_bytes=test_readdir_calls=0;
        assert(!sd_logger_history_read((struct sd_logger_cursor){0x5001,6},rows,6,&page));
        assert(rows[0].sequence==7&&test_readdir_calls==0&&test_read_bytes<=2048);
        /* Reconnect rebuilds bounds once, then sequential pages remain cheap. */
        sd_logger_history_reset();test_read_bytes=test_readdir_calls=0;
        assert(!sd_logger_history_read((struct sd_logger_cursor){0x5001,12},rows,6,&page));
        assert(rows[0].sequence==13&&test_read_bytes>200000&&test_readdir_calls>64);
        test_read_bytes=test_readdir_calls=0;
        assert(!sd_logger_history_read((struct sd_logger_cursor){0x5001,18},rows,6,&page));
        assert(rows[0].sequence==19&&test_readdir_calls==0&&test_read_bytes<=2048);
        /* A 600-row append invalidates cached latest/next-file metadata. */
        struct card_sample batch[600];for(uint32_t i=0;i<600;++i)batch[i]=make_sample(i+1,i,0);
        size_t committed;assert(!sd_logger_append(batch,600,&committed)&&committed==600);
        test_read_bytes=test_readdir_calls=0;
        assert(!sd_logger_history_read((struct sd_logger_cursor){0x5001,24},rows,6,&page));
        assert(rows[0].sequence==25&&page.latest.boot_id==0x1234&&page.latest.sequence==600&&test_readdir_calls>64);
        test_read_bytes=test_readdir_calls=0;
        assert(!sd_logger_history_read((struct sd_logger_cursor){0x5001,30},rows,6,&page));
        assert(rows[0].sequence==31&&test_readdir_calls==0&&test_read_bytes<=2048);
        for(uint32_t i=0;i<20;++i)batch[i]=make_sample(601+i,600+i,0);
        test_write_failure=2;assert(sd_logger_append(batch,20,&committed)<0&&!committed);test_write_failure=-1;
        assert(!sd_logger_history_read((struct sd_logger_cursor){0x1234,600},rows,6,&page));
        assert(!page.count&&page.latest.sequence==600&&!page.more);
    }else{assert(0);}
    printf("Storage checks PASS: %s\n",argv[1]);
}
