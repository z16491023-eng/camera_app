// apps/btt-agent/agent.h
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 初始化业务：媒体与串口
int agent_init(void);
void agent_fini(void);

// 处理一包完整的协议数据（来自 net_client 的 on_packet）
void agent_handle_packet(const uint8_t* buf, size_t len);

#ifdef __cplusplus
}
#endif
