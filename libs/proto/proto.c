// libs/proto/proto.c
#include "proto.h"
#include <string.h>
#include <stdio.h>
static inline void le16_enc(uint8_t* p, uint16_t v){
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}
static inline uint16_t le16_dec(const uint8_t* p){
    return (uint16_t)(p[0] | (uint16_t)p[1]<<8);
}

uint16_t proto_crc16(const uint8_t* data, size_t len){
    // CRC-16/CCITT-FALSE
    uint16_t crc = 0xFFFF;
    for (size_t i=0; i<len; i++){
        crc ^= (uint16_t)data[i] << 8;
        for (int b=0; b<8; b++){
            crc = (crc & 0x8000) ? (uint16_t)((crc<<1) ^ 0x1021) : (uint16_t)(crc<<1);
        }
    }
    return crc;
}
static int hdr_ok(const proto_hdr_t* h){
    return h->magic0==PROTO_MAGIC0 && h->magic1==PROTO_MAGIC1 && h->ver==PROTO_VER;
}

int proto_pack(uint8_t* out, size_t out_sz, uint8_t type,
               const uint8_t* payload, uint32_t payload_len){
    if (!out) return -1;
    size_t need = sizeof(proto_hdr_t) + (size_t)payload_len + 2; // +尾CRC
    if (out_sz < need) return -2;

    proto_hdr_t hdr;
    hdr.magic0 = PROTO_MAGIC0;
    hdr.magic1 = PROTO_MAGIC1;
    hdr.ver    = PROTO_VER;
    hdr.type   = type;
    hdr.len    = payload_len;

    // 写header
    memcpy(out, &hdr, sizeof(hdr));
    // 写payload
    if (payload_len && payload)
        memcpy(out + sizeof(hdr), payload, payload_len);

    // 计算整帧CRC（不含尾CRC）
    uint16_t crc = proto_crc16(out, sizeof(hdr) + payload_len);
    le16_enc(out + sizeof(hdr) + payload_len, crc);
    return (int)need;
}

int proto_try_parse(const uint8_t* buf, size_t len,
                    proto_hdr_t* hdr_out, const uint8_t** payload, uint32_t* payload_len){
    if (!buf || len < sizeof(proto_hdr_t)+2) return -1;

    const proto_hdr_t* h = (const proto_hdr_t*)buf;
    if (!hdr_ok(h)) return -2;

    uint32_t L = h->len;
    size_t total = sizeof(proto_hdr_t) + (size_t)L + 2; // +尾CRC
    if (len < total) return -3;

    const uint8_t* tail_crc_p = buf + sizeof(proto_hdr_t) + L;
    uint16_t crc_rx = le16_dec(tail_crc_p);
    uint16_t crc_calc = proto_crc16(buf, sizeof(proto_hdr_t)+L);
    if (crc_rx != crc_calc) return -4;

    if (hdr_out)     *hdr_out = *h;
    if (payload)     *payload = buf + sizeof(proto_hdr_t);
    if (payload_len) *payload_len = L;

    uint8_t *ch = **payload;
     
    printf("[SNAP_CH] ch=%u, len=%d\n", ch[0], L);
    return (int)total;
}