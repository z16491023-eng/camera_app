// libs/net/net_client.c
#define _POSIX_C_SOURCE 200809L
#include "net_client.h"
#include "proto.h"
#include <pthread.h>      // 线程
#include <arpa/inet.h>       // inet_pton / sockaddr_in   tcp接口
#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>//socket接口
#include <sys/select.h>//监听
#include <fcntl.h>
#include <time.h>

static pthread_t g_thr;
static int g_run = 0;
static int g_sock = -1;
static pthread_mutex_t g_tx_mtx = PTHREAD_MUTEX_INITIALIZER;//互斥锁 
static net_on_packet_cb g_on_pkt = NULL;//函数指针
static net_on_state_cb  g_on_state = NULL;//函数指针
static char g_host[128] = "127.0.0.1";
static uint16_t g_port = 9000;

static int set_nodelay_keepalive(int fd) {
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));//检测网络
    //  TCP_KEEPIDLE/TCP_KEEPINTVL/TCP_KEEPCNT
    int snd = 1<<20;  // 1MB调大发送缓冲（默认很小）
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &snd, sizeof(snd));
    return 0;
}

static int dial() {
    char host[128]; strncpy(host, g_host, sizeof(host));
    const char* env_ip = getenv("SERVER_IP");//假如配置环境变量，使用环境变量
    const char* env_pt = getenv("SERVER_PORT");//假如配置环境变量，使用环境变量
    if (env_ip) strncpy(host, env_ip, sizeof(host));
    uint16_t port = g_port;
    if (env_pt) port = (uint16_t)atoi(env_pt);//char to int

    struct addrinfo hints = {0}, *res = NULL;//网络编程骨架：1.addr info结构体 1.1 结构体设置位socket流形式stream，1.2设置family位ip4，AF_inet
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family   = AF_INET;
    char service[16]; snprintf(service, sizeof(service), "%u", port);//把port换成无符号10进制
    if (getaddrinfo(host, service, &hints, &res) != 0) return -1;//获取到自己需要的addrinfo 结构体，可以直接被socket操作。  

    int fd = -1;
    for (struct addrinfo* p=res; p; p=p->ai_next){//获得所有的本地ip，更先进的addr操作方式
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd<0) continue;
        if (connect(fd, p->ai_addr, p->ai_addrlen)==0) /*连接服务器*/{ set_nodelay_keepalive(fd); break; }//定制socket参数
        close(fd); fd=-1;
    }
    
    freeaddrinfo(res);//释放addr info
    return fd;
}

static int send_all(int fd, const void* buf, size_t len) {
    const uint8_t* p=(const uint8_t*)buf; size_t left=len;
    while (left>0) {
        ssize_t n = send(fd, p, left, 0);
        if (n<0) { if (errno==EINTR) continue; return -1; }
        if (n==0) return -1;
        left -= (size_t)n; p += n;
    }
    return 0;
}

int net_client_send(const void* data, size_t len){
    if (!g_run || g_sock<0 || !data || len==0) return -1;
    pthread_mutex_lock(&g_tx_mtx);
    int rc = send_all(g_sock, data, len);
    pthread_mutex_unlock(&g_tx_mtx);
    return rc;
}

static int send_ping() {
    uint8_t buf[32];
    int n = proto_pack(buf, sizeof(buf), MSG_PING, NULL, 0);
    if (n>0) return net_client_send(buf, (size_t)n);
    return -1;
}

static void* rx_loop(void* _){
    (void)_;
    uint8_t rx[4096];
    uint8_t proto_buf[4096];

    while (g_run){
        // 尝试建立连接
        g_sock = dial();
        if (g_sock<0){ sleep(2); continue; }
        if (g_on_state) g_on_state(1);

        // 周期心跳
        time_t t_last_ping = time(NULL);

        for (;;) {
            // 将set集合中, 所有文件文件描述符对应的标志位设置为0, 集合中没有添加任何文件描述符
            // 将文件描述符fd添加到set集合中 == 将fd对应的标志位设置为1
            fd_set rds; FD_ZERO(&rds); FD_SET(g_sock, &rds);
            struct timeval tv={.tv_sec=1, .tv_usec=0};
            int r = select(g_sock+1, &rds, NULL, NULL, &tv);
            if (r==0) { // timeout
                time_t now = time(NULL);
                // if (now - t_last_ping >= 15){ send_ping(); t_last_ping = now; }
                if (!g_run) break;
                continue;
            }
            if (r<0){ if(errno==EINTR) continue; break; }

            ssize_t n = recv(g_sock, rx, sizeof(rx), 0);
            if (n<=0) break; // 断开

            // 优先尝试解析 proto；失败则按原样回显
            proto_hdr_t h; const uint8_t* pl=NULL; uint32_t L=0;
            int parsed = proto_try_parse(rx, (size_t)n, &h, &pl, &L);
            if (parsed>0) {
                if (h.type==MSG_PING){
                    int m = proto_pack(proto_buf, sizeof(proto_buf), MSG_PONG, NULL, 0);
                    if (m>0) net_client_send(proto_buf, (size_t)m);
                } else if (h.type==MSG_ECHO){
                    int m = proto_pack(proto_buf, sizeof(proto_buf), MSG_ECHO, pl, L);
                    if (m>0) net_client_send(proto_buf, (size_t)m);
                } else {
                    // 里程碑#1：其他类型先不处理；直接 ACK ECHO 方便联调
                    int m = proto_pack(proto_buf, sizeof(proto_buf), MSG_ECHO, pl, L);
                    if (m>0) net_client_send(proto_buf, (size_t)m);
                }
                if (g_on_pkt) g_on_pkt(rx, (size_t)parsed);
            } else {
                // 非协议：原样回显
                net_client_send(rx, (size_t)n);
                if (g_on_pkt) g_on_pkt(rx, (size_t)n);
            }
        }

        if (g_on_state) g_on_state(0);
        close(g_sock); g_sock=-1;
        if (!g_run) break;
        sleep(2); // 重连节流
    }
    return NULL;
}

int net_client_start(const char* host, uint16_t port,
                     net_on_packet_cb on_pkt, net_on_state_cb on_state){
    if (g_run) return 0;
    if (host) strncpy(g_host, host, sizeof(g_host));
    g_port = port;
    g_on_pkt = on_pkt;
    g_on_state = on_state;
    g_run = 1;
    if (pthread_create(&g_thr, NULL, rx_loop, NULL)!=0){ g_run=0; return -1; }
    return 0;
}

void net_client_stop(void){
    if (!g_run) return;
    g_run = 0;
    if (g_sock>=0) shutdown(g_sock, SHUT_RDWR);
    pthread_join(g_thr, NULL);
}
