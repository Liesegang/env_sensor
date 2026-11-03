#ifndef CARD_IMAGE_PROTOCOL_H_
#define CARD_IMAGE_PROTOCOL_H_
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define CARD_IMAGE_BYTES 120000U
#define CARD_IMAGE_MAX_CHUNK 232U
#define CARD_IMAGE_COMMITTED 0xc17e0001U
enum card_image_state { IMAGE_IDLE, IMAGE_RECEIVING, IMAGE_STORED, IMAGE_ERROR,
    IMAGE_DISPLAYING, IMAGE_DISPLAYED, IMAGE_PREPARING, IMAGE_COMMITTING };
struct card_image_begin { uint32_t id, crc; };
struct card_image_chunk { uint32_t id, offset; const uint8_t *bytes; size_t length; };
static inline uint32_t image_get32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static inline void image_put32(uint8_t *p, uint32_t v) {
    for (unsigned int i=0;i<4;++i) p[i]=(uint8_t)(v>>(8*i));
}
static inline bool image_palette_valid(const uint8_t *p, size_t length) {
    for (size_t i=0;i<length;++i) {
        uint8_t a=p[i]>>4,b=p[i]&15;
        if (!((a<=3||a==5||a==6)&&(b<=3||b==5||b==6))) return false;
    }
    return true;
}
/* Standard reflected IEEE CRC32; initial=0, incremental=zlib.crc32 semantics. */
static inline uint32_t image_crc32(uint32_t crc,const uint8_t *p,size_t length) {
    crc=~crc;
    for(size_t i=0;i<length;++i) {
        crc^=p[i];
        for(unsigned int bit=0;bit<8;++bit) crc=(crc>>1)^(0xedb88320U & (0U-(crc&1U)));
    }
    return ~crc;
}
static inline int image_decode_begin(const uint8_t *p,size_t len,struct card_image_begin *out) {
    if (!p||!out||len!=20||p[0]!='I'||p[1]!='C'||p[2]!=1||p[3]!=1||!image_get32(p+4)||
        image_get32(p+8)!=CARD_IMAGE_BYTES||p[16]!=0x90||p[17]!=1||p[18]!=0x58||p[19]!=2) return -EINVAL;
    *out=(struct card_image_begin){image_get32(p+4),image_get32(p+12)}; return 0;
}
static inline int image_decode_command(const uint8_t *p,size_t len,uint8_t *op,uint32_t *id) {
    if(!p||len!=8||p[0]!='I'||p[1]!='C'||p[2]!=1||(p[3]!=2&&p[3]!=3)||!image_get32(p+4)) return -EINVAL;
    *op=p[3]; *id=image_get32(p+4); return 0;
}
static inline int image_decode_chunk(const uint8_t *p,size_t len,struct card_image_chunk *out) {
    if(!p||!out||len<=12||len>12+CARD_IMAGE_MAX_CHUNK||p[0]!='I'||p[1]!='D'||p[2]!=1||p[3]||
       !image_get32(p+4)||image_get32(p+8)>CARD_IMAGE_BYTES||len-12>CARD_IMAGE_BYTES-image_get32(p+8)||
       !image_palette_valid(p+12,len-12)) return -EINVAL;
    *out=(struct card_image_chunk){image_get32(p+4),image_get32(p+8),p+12,len-12}; return 0;
}
/* 0=new sequential chunk, 1=identical last retry, negative=out of order. */
static inline int image_chunk_order(const struct card_image_chunk *chunk,uint32_t id,uint32_t received,
    uint32_t last_offset,size_t last_length,const uint8_t *last_bytes) {
    if(chunk->id!=id) return -EINVAL;
    if(chunk->offset==received) return 0;
    if(last_length&&chunk->offset==last_offset&&chunk->length==last_length&&
       !memcmp(chunk->bytes,last_bytes,last_length)) return 1;
    return -EINVAL;
}
static inline void image_file_header(uint8_t h[32],uint32_t crc,uint32_t generation,bool committed) {
    memset(h,0,32); h[0]='C';h[1]='I';h[2]=1;h[3]=32;
    h[4]=0x90;h[5]=1;h[6]=0x58;h[7]=2;
    image_put32(h+8,CARD_IMAGE_BYTES);image_put32(h+12,crc);image_put32(h+16,generation);
    image_put32(h+20,committed?CARD_IMAGE_COMMITTED:0);
}
static inline int image_check_header(const uint8_t h[32]) {
    if(h[0]!='C'||h[1]!='I'||h[2]!=1||h[3]!=32||h[4]!=0x90||h[5]!=1||h[6]!=0x58||h[7]!=2||
       image_get32(h+8)!=CARD_IMAGE_BYTES||!image_get32(h+16)||image_get32(h+20)!=CARD_IMAGE_COMMITTED) return -EBADMSG;
    for(size_t i=24;i<32;++i) if(h[i]) return -EBADMSG;
    return 0;
}
#endif
