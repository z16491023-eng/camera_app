// include/proto.h
#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PROTO_MAGIC0 0x48 /* 'H' */
#define PROTO_MAGIC1 0x47 /* 'G' */
#define PROTO_VER    2

typedef enum {
    // 基础
    MSG_PING = 1, MSG_PONG = 2, MSG_ECHO = 3,

    // 拍照/录像
    MSG_SNAP_ALL      = 10,   // 指令1
    MSG_REC_START_ALL = 11,   // 指令2
    MSG_SNAP_CH       = 12,   // 指令3
    MSG_REC_START_CH  = 13,   // 指令4
    MSG_REC_STOP_ALL  = 14,
    MSG_REC_STOP_CH   = 15,

    // 串口
    MSG_TTL_WRITE     = 20,
    MSG_RS485_WRITE   = 21,

    // 状态
    MSG_GET_STATUS    = 30,
    MSG_RESP_STATUS   = 31,

    // 响应（成功/失败/二进制承载）
    MSG_OK            = 40,   // payload: 可选文本
    MSG_ERR           = 41,   // payload: 错误文本
    MSG_SNAP_JPEG     = 42,   // payload: ch(1B) + jpg...
    MSG_REC_STARTED   = 43,   // payload: ch(1B) + path
    MSG_REC_STOPPED   = 44,     // payload: ch(1B) + path
    MSG_SNAP_SAVED    = 45  // 新增：抓拍已保存（payload: pld_rec_path_t + '\0'路径）
} proto_msg_t;

#pragma pack(push, 1)
typedef struct {
    uint8_t  magic0, magic1;   // 'H','G'
    uint8_t  ver;              // 2
    uint8_t  type;             // proto_msg_t
    uint32_t len;              // payload长度(LE)
} proto_hdr_t;

// 常用 payload 结构
typedef struct { uint8_t ch; } pld_ch_t;
typedef struct { uint8_t ch; uint8_t reserved; uint16_t secs; } pld_rec_ch_t;
typedef struct { uint16_t secs; } pld_rec_all_t;
typedef struct { uint8_t ok_cam_mask; uint8_t ttl_ok; uint8_t rs485_ok; uint8_t rsv; } pld_status_t;
typedef struct { uint8_t ch; /* 后面紧跟 N 字节 JPEG */ } pld_snap_jpeg_t;
typedef struct { uint8_t ch; /* 后面紧跟 以'\0'结尾的路径字符串 */ } pld_rec_path_t;
typedef struct { uint16_t len; /* 后面紧跟 len 字节串口数据 */ } pld_serial_write_t;
#pragma pack(pop)

// CRC16/CCITT-FALSE
uint16_t proto_crc16(const uint8_t* data, size_t len);

// 打包：hdr+payload+crc16_tail
// 返回总长度（hdr+payload+2），<0表示失败
int proto_pack(uint8_t* out, size_t out_sz, uint8_t type,
               const uint8_t* payload, uint32_t payload_len);

// 解析：成功返回总帧长（hdr+payload+2），并输出hdr/payload指针与长度
// 失败返回 <0
int proto_try_parse(const uint8_t* buf, size_t len,
                    proto_hdr_t* hdr_out, const uint8_t** payload, uint32_t* payload_len);

#ifdef __cplusplus
}
#endif
