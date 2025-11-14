// include/net_client.h
#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" { 
#endif

typedef void (*net_on_packet_cb)(const uint8_t* data, size_t len); // 原始包回调（里程碑#1先做 echo）
typedef void (*net_on_state_cb)(int connected);                    // 连接状态 0/1

// 启动客户端；host 可用环境变量 SERVER_IP/PORT 覆盖
int  net_client_start(const char* host, uint16_t port,
                      net_on_packet_cb on_pkt, net_on_state_cb on_state);
// 异步发送（线程安全）
int  net_client_send(const void* data, size_t len);
// 停止并回收线程
void net_client_stop(void);

#ifdef __cplusplus
}
#endif
