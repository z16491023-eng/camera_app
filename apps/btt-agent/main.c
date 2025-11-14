// apps/btt-agent/main.c
#define _GNU_SOURCE 
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include "net_client.h"
#include <stdlib.h>
#include "agent.h"
static volatile int g_exit = 0;
static void on_sigint(int s){ (void)s; g_exit = 1; }

static void on_state(int connected){
    printf("[net] %s\n", connected? "connected":"disconnected");
}

static void on_packet(const unsigned char* data, size_t len){
    agent_handle_packet(data, len);
    // printf("[net] rx %zu bytes\n", len);
}

int main(int argc, char** argv){
    signal(SIGINT, on_sigint);//ctr+c 注册优雅退出
    if (agent_init()!=0){ fprintf(stderr,"agent init failed\n"); return 1; }//初始化串口
    const char* ip = (argc>1)? argv[1] : "192.168.1.77";//默认ip
    unsigned short port = (argc>2)? (unsigned short)atoi(argv[2]) : 9000;//默认端口
    if (net_client_start(ip, port, on_packet, on_state)!=0){//进入网络net功能，实现回调on_state打印当前连接状态，on_packet解析指令，并且进行分发任务
        fprintf(stderr, "net start failed\n");
        return 1;
    }
    printf("btt-agent started. SERVER_IP=%s SERVER_PORT=%u\n", ip, port);
    while (!g_exit) sleep(1);
    net_client_stop();//网络线程回收
    agent_fini();//串口线程退出回收
    return 0;
}
