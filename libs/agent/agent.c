// apps/btt-agent/agent.c

#define _GNU_SOURCE 
#include <unistd.h> 
#include "agent.h"
#include "proto.h"
#include "net_client.h"
#include "serial_mgr.h"
#include "HiF_media_ss522.h"   // 多媒体库
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>  // NEW
#include <time.h>    // NEW: 计时用
#define MAX_CH 4
#define DEFAULT_SECS 30
#define JPEG_MAX (1024*1024)   // 你样例里给出 1080P 1MB 建议；D1 会更小  :contentReference[oaicite:4]{index=4}

static int g_inited = 0;

// --- 录像上下文（每通道）
typedef struct {
    pthread_t th;
    int       running;
    int       ch;
    int       secs;
    volatile int stop;
    void*     mp4;    // hif_mp4_handle
    int       seen_idr;
    pthread_mutex_t io_mtx;
    char      path[256];
} rec_ctx_t;

static rec_ctx_t g_rec[MAX_CH];

// --- 媒体初始化一次：D1_NTSC + H264；编码统一启动
static int media_init_once(void){
    if (g_inited) return 0;
    if (HiF_media_init()!=0) return -1;                                         // :contentReference[oaicite:5]{index=5}
    for (int ch=0; ch<MAX_CH; ch++){
        hif_media_param_t p; memset(&p,0,sizeof(p));
        (void)HiF_media_get_param(ch, &p);                                      // :contentReference[oaicite:6]{index=6}
        p.pic_size   = HIF_PIC_D1_NTSC;                                         // 你确定以 D1_NTSC 为主  :contentReference[oaicite:7]{index=7}
        p.payload    = VE_TYPE_H264;                                            // 主编码 H264       :contentReference[oaicite:8]{index=8}
        p.gop_mode   = VENC_GOP_MODE_NORMAL_P;
        p.rc_mode    = VENC_RC_CBR;
        p.frame_rate = 25;
        p.bit_rate   = 2*1024;  // D1 下 2Mbps 起步，可再调
        p.max_bit_rate = p.bit_rate;
        p.long_term_min_bit_rate = p.bit_rate/2;
        p.cb_get_stream = NULL;     // 非录像状态不注册回调
        (void)HiF_media_set_param(ch, &p);                                      // :contentReference[oaicite:9]{index=9}
    }
    if (HiF_media_start_encode()!=0){ HiF_media_exit(); return -2; }            // :contentReference[oaicite:10]{index=10}
    g_inited = 1;
    return 0;
}

// --- 录像回调（所有通道共用）
static int on_stream(FRAME_INFO fi, char* buf, int len){
    if (!buf || len<=0) return 0;
    int ch = fi.chn;                                   // 回调里带有通道号    :contentReference[oaicite:11]{index=11}
    if (ch<0 || ch>=MAX_CH) return 0;
    rec_ctx_t* c = &g_rec[ch];
    if (!c->running) return 0;

    if (fi.frame_type==FRAME_I) c->seen_idr = 1;       // 见到 I 帧再写入     :contentReference[oaicite:12]{index=12}
    if (c->stop) return 0;

    pthread_mutex_lock(&c->io_mtx);
    if (c->mp4 && c->seen_idr){
        // 写 MP4
        (void)HiF_media_mp4_write_frame(c->mp4, (unsigned char*)buf, len, (int)fi.timestamp);  // :contentReference[oaicite:13]{index=13}
    }
    pthread_mutex_unlock(&c->io_mtx);
    return 0;
}

// 注册/注销回调：当任意通道开始录像，就把回调挂上；全部停止后摘掉（简单实现）
static int update_global_callback(void){
    int any = 0;
    for (int i=0;i<MAX_CH;i++) if (g_rec[i].running) { any=1; break; }

    // 简化：重新 set_param 设置回调指针
    for (int ch=0; ch<MAX_CH; ch++){
        hif_media_param_t p; memset(&p,0,sizeof(p));
        (void)HiF_media_get_param(ch,&p);
        p.cb_get_stream = any ? on_stream : NULL;
        (void)HiF_media_set_param(ch,&p);
    }
    return 0;
}

static void* rec_thread(void* arg){
    rec_ctx_t* c = (rec_ctx_t*)arg;
    c->seen_idr = 0;
    c->stop = 0;

    // 打开 MP4 文件（单路：第二参数务必为1）   :contentReference[oaicite:15]{index=15}
    snprintf(c->path,sizeof(c->path), "/tmp/ch%d_%dsec.mp4", c->ch, c->secs);
    c->mp4 = HiF_media_mp4_open_file(c->path, 1);                              // :contentReference[oaicite:16]{index=16}
    if (!c->mp4){ c->running=0; return NULL; }

    unsigned long t0 = (unsigned long)time(NULL);
    while (!c->stop){
        sleep(1);
        if ((int)(time(NULL)-t0) >= c->secs) break;
    }

    // 正确收尾：先停止写 → 关文件句柄（回调路径已用 stop 标志屏蔽写）  :contentReference[oaicite:17]{index=17}
    c->stop = 1;
    usleep(100*1000);
    pthread_mutex_lock(&c->io_mtx);
    if (c->mp4){ HiF_media_mp4_close_file(c->mp4); c->mp4=NULL; }               // :contentReference[oaicite:18]{index=18}
    pthread_mutex_unlock(&c->io_mtx);
    c->running = 0;
    update_global_callback();
    // 通知服务器录像完成
    uint8_t buf[512]; pld_rec_path_t p = { .ch=(uint8_t)c->ch };
    size_t pathlen = strnlen(c->path, sizeof(c->path))+1;
    memcpy(buf, &p, sizeof(p)); memcpy(buf+sizeof(p), c->path, pathlen);
    uint8_t out[600]; int n = proto_pack(out,sizeof(out), MSG_REC_STOPPED, buf, (uint32_t)(sizeof(p)+pathlen));
    if (n>0) net_client_send(out,n);
    return NULL;
}

static int start_rec(int ch, int secs){
    if (ch<0 || ch>=MAX_CH) return -1;
    if (media_init_once()!=0) return -2;                                         // 启一次编码   :contentReference[oaicite:19]{index=19}
    rec_ctx_t* c = &g_rec[ch];
    if (c->running) return 0;
    c->ch = ch; c->secs = (secs>0 && secs<=600)? secs : DEFAULT_SECS;
    c->running = 1; c->stop = 0; c->mp4=NULL; c->seen_idr=0;
    pthread_mutex_init(&c->io_mtx, NULL);//动态初始化：
    update_global_callback();
    if (pthread_create(&c->th, NULL, rec_thread, c)!=0){ c->running=0; return -3; }
    // 立即回报“已开始”
    uint8_t buf[512]; pld_rec_path_t p={.ch=(uint8_t)ch};
    const char* path = "(writing...)"; size_t L=strlen(path)+1;
    memcpy(buf,&p,sizeof(p)); memcpy(buf+sizeof(p),path,L);
    uint8_t out[600]; int n=proto_pack(out,sizeof(out),MSG_REC_STARTED,buf,(uint32_t)(sizeof(p)+L));
    if (n>0) net_client_send(out,n);
    return 0;
}

static int stop_rec(int ch){
    if (ch<0 || ch>=MAX_CH) return -1;
    rec_ctx_t* c=&g_rec[ch];
    if (!c->running) return 0;
    c->stop = 1;
    pthread_join(c->th,NULL);
    return 0;
}

static int stop_rec_all(void){
    for (int ch=0; ch<MAX_CH; ch++) stop_rec(ch);
    return 0;
}

static int do_snap_one(int ch){
    if (media_init_once()!=0) return -1;                                          // :contentReference[oaicite:20]{index=20}
    // 保险：设置通道参数到 D1/H264
    hif_media_param_t p; memset(&p,0,sizeof(p));
    (void)HiF_media_get_param(ch,&p);
    p.pic_size = HIF_PIC_D1_NTSC; p.payload = VE_TYPE_H264;
    if (HiF_media_set_param(ch, &p) != 0) {
        printf("错误：设置参数失败！\n");
        HiF_media_exit();
        return -1;
    }                                             // :contentReference[oaicite:21]{index=21}

    // 抓拍
    unsigned char* jpg = (unsigned char*)malloc(JPEG_MAX);
    if (!jpg) return -2;
    int len = HiF_media_snap(ch,jpg, JPEG_MAX);                                      // :contentReference[oaicite:22]{index=22}
    if (len>0){
        // 回发 JPEG：type=MSG_SNAP_JPEG，payload=ch(1B)+jpg
        uint8_t* tmp = (uint8_t*)malloc((size_t)len + 1);
        if (!tmp){ free(jpg); return -3; }
        pld_snap_jpeg_t h = { .ch = (uint8_t)ch };
        memcpy(tmp, &h, sizeof(h));
        memcpy(tmp+sizeof(h), jpg, (size_t)len);
        uint8_t* out = (uint8_t*)malloc((size_t)len + 64);
        int n = proto_pack(out, (size_t)len + 64, MSG_SNAP_JPEG, tmp, (uint32_t)(sizeof(h)+len));
        if (n>0) net_client_send(out, n);
        free(out); free(tmp);
    }
    free(jpg);
    return (len>0)? 0 : -4;
}

static int do_status(pld_status_t* st){
    uint8_t mask=0;
    for(int ch=0; ch<MAX_CH; ch++){
        hif_video_loss_t v={ .ch=(unsigned char)ch };
        int r = HiF_media_get_cam_status(&v);                                     // :contentReference[oaicite:23]{index=23}
        if (r==0 && !v.is_lost) mask |= (1u<<ch);
    }
    st->ok_cam_mask = mask;

    // TTL/485 回显探测（尝试打开 → 写 "PING" → 读 50ms）
    const char ping[] = "PING";
    int fd_ttl  = serialmgr_open_ttl(115200);
    int fd_485  = serialmgr_open_rs485(115200);
    st->ttl_ok = 0; st->rs485_ok = 0;

    if (fd_ttl>0){
        (void)serialmgr_write(fd_ttl, ping, sizeof(ping));
        char rb[32]; int n = serialmgr_read(fd_ttl, rb, sizeof(rb), 50);
        st->ttl_ok = (n>0);
        serialmgr_close(fd_ttl);
    }
    if (fd_485>0){
        (void)serialmgr_write(fd_485, ping, sizeof(ping));
        char rb[32]; int n = serialmgr_read(fd_485, rb, sizeof(rb), 50);
        st->rs485_ok = (n>0);
        serialmgr_close(fd_485);
    }
    st->rsv = 0;
    return 0;
}

int agent_init(void){
    HiF_media_init();                 // 串口库要求的初始化   :contentReference[oaicite:24]{index=24}
    return 0;
}
void agent_fini(void){
    stop_rec_all();
    if (g_inited){ HiF_media_exit(); g_inited=0; }                                // :contentReference[oaicite:25]{index=25}
}


// ---------- NEW: 简易单调时钟（毫秒/纳秒） ----------
static inline uint64_t nsec_now(){
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec*1000000000ull + (uint64_t)ts.tv_nsec;
}
// ---------- NEW: 串口压测发送（fd 已打开），返回实际发送字节 ----------
static size_t serial_send_burst(int fd, size_t total_bytes){
    // 4KB 块；可按需调大
    static unsigned char pattern[4096];
    static int inited = 0;
    if (!inited){
        for (int i=0;i<(int)sizeof(pattern);i++) pattern[i] = (unsigned char)(i & 0xFF);
        inited = 1;
    }
    size_t sent = 0;
    while (sent < total_bytes){
        size_t chunk = total_bytes - sent;
        if (chunk > sizeof(pattern)) chunk = sizeof(pattern);
        int n = serialmgr_write(fd, pattern, chunk);
        if (n < 0) break;
        sent += (size_t)n;
        // 若写短了，继续循环
    }
    return sent;
}

// ---------- NEW: TTL 压测（自动打开→发送→关闭→打印/回报） ----------
static void do_ttl_burst(void){
    const int    baud  = 115200;
    const size_t total =  512*1024; // 512KB 默认
    int fd = serialmgr_open_ttl(baud);
    if (fd <= 0){
        uint8_t out[128];
        int n = proto_pack(out,sizeof(out),MSG_ERR,(const uint8_t*)"ttl_open_fail",14);
        if (n>0) net_client_send(out,n);
        fprintf(stderr, "[TTL] open failed (baud=%d)\n", baud);
        return;
    }

    uint64_t t0 = nsec_now();
    size_t sent = serial_send_burst(fd, total);
    // 可选：如你在 POSIX TTL 下希望等硬件发完，可在 serial_mgr 暴露 tcdrain；这里简单 sleep 小段时间
    // usleep(50*1000);
    uint64_t t1 = nsec_now();
    serialmgr_close(fd);

    double sec = (t1 - t0)/1e9;
    double kbps = (sent*8.0)/1000.0/sec;
    double kBps = sent/1000.0/sec;
    printf("[TTL] burst sent=%zu bytes in %.3f s => %.1f kB/s (%.1f kb/s) @%d baud\n",
           sent, sec, kBps, kbps, baud);

    char msg[128];
    int m = snprintf(msg, sizeof(msg), "ttl_done bytes=%zu time=%.3fs rate=%.1fkB/s",
                     sent, sec, kBps);
    uint8_t out[192];
    int n = proto_pack(out,sizeof(out), MSG_OK, (const uint8_t*)msg, (uint32_t)(m+1));
    if (n>0) net_client_send(out,n);
}

// ---------- NEW: RS485 压测 ----------
static void do_rs485_burst(void){
    const int    baud  = 115200;
    const size_t total =  512*1024; // 512KB 默认
    int fd = serialmgr_open_rs485(baud);
    if (fd <= 0){
        uint8_t out[128];
        int n = proto_pack(out,sizeof(out),MSG_ERR,(const uint8_t*)"rs485_open_fail",16);
        if (n>0) net_client_send(out,n);
        fprintf(stderr, "[RS485] open failed (baud=%d)\n", baud);
        return;
    }

    uint64_t t0 = nsec_now();
    size_t sent = serial_send_burst(fd, total);
    uint64_t t1 = nsec_now();
    serialmgr_close(fd);

    double sec = (t1 - t0)/1e9;
    double kbps = (sent*8.0)/1000.0/sec;
    double kBps = sent/1000.0/sec;
    printf("[RS485] burst sent=%zu bytes in %.3f s => %.1f kB/s (%.1f kb/s) @%d baud\n",
           sent, sec, kBps, kbps, baud);

    char msg[128];
    int m = snprintf(msg, sizeof(msg), "rs485_done bytes=%zu time=%.3fs rate=%.1fkB/s",
                     sent, sec, kBps);
    uint8_t out[192];
    int n = proto_pack(out,sizeof(out), MSG_OK, (const uint8_t*)msg, (uint32_t)(m+1));
    if (n>0) net_client_send(out,n);
}
// ---------- NEW: 串口异步发送（无队列，最小实现） ----------
typedef enum { SER_TTL = 0, SER_RS485 = 1 } ser_kind_t;

typedef struct {
    ser_kind_t kind;
    uint8_t   *data;     // 非 burst 时复制一份
    size_t     len;
    int        burst;    // 1=跑 do_*_burst，0=普通写
} ser_job_t;

// 每个口一个互斥，保证同一时间只有一个发送在跑；不用队列
static pthread_mutex_t g_ttl_mtx  = PTHREAD_MUTEX_INITIALIZER;//静态初始化：
static pthread_mutex_t g_rs485_mtx= PTHREAD_MUTEX_INITIALIZER;//静态初始化：

static void* ser_worker(void* arg){
    ser_job_t* job = (ser_job_t*)arg;
    pthread_detach(pthread_self());
    pthread_mutex_t* m = (job->kind==SER_TTL)? &g_ttl_mtx : &g_rs485_mtx;

    // —— 进入工作区（提交方已 trylock 成功，这里只负责最终解锁） ——
    if (job->burst){
        // 压测路径沿用你现有实现（打开->发送->回包）
        if (job->kind==SER_TTL) do_ttl_burst();
        else                    do_rs485_burst();
    } else {
        // 普通一次性写 -> 完成后回包 wrote=*
        int fd = (job->kind==SER_TTL)? serialmgr_open_ttl(115200)
                                     : serialmgr_open_rs485(115200);
        int wrote = -1;
        if (fd>0){
            wrote = serialmgr_write(fd, job->data, (int)job->len);
            serialmgr_close(fd);
        }
        char msg[32]; snprintf(msg,sizeof(msg),"wrote=%d",wrote);
        uint8_t out[64];
        int n = proto_pack(out,sizeof(out),
                           (wrote>=0)?MSG_OK:MSG_ERR,
                           (const uint8_t*)msg, (uint32_t)(strlen(msg)+1));
        if (n>0) net_client_send(out,n);
    }

    if (job->data) free(job->data);
    pthread_mutex_unlock(m);
    free(job);
    return NULL;
}

static int submit_serial_job(ser_kind_t kind, const uint8_t* data, size_t len, int burst){
    pthread_mutex_t* m = (kind==SER_TTL)? &g_ttl_mtx : &g_rs485_mtx;//静态初始化：

    // 不排队，直接试图占用口；忙则立即回错误
    if (pthread_mutex_trylock(m)!=0){
        const char* emsg = (kind==SER_TTL)? "ttl_busy" : "rs485_busy";
        uint8_t out[64];
        int n = proto_pack(out,sizeof(out), MSG_ERR,
                           (const uint8_t*)emsg, (uint32_t)(strlen(emsg)+1));
        if (n>0) net_client_send(out,n);
        return -1;
    }

    ser_job_t* job = (ser_job_t*)calloc(1,sizeof(*job));
    if (!job){ pthread_mutex_unlock(m); return -2; }
    job->kind  = kind;
    job->burst = burst;

    if (!burst && len>0){
        job->data = (uint8_t*)malloc(len);
        if (!job->data){ free(job); pthread_mutex_unlock(m); return -3; }
        memcpy(job->data, data, len);
        job->len = len;
    }

    pthread_t th;
    if (pthread_create(&th, NULL, ser_worker, job)!=0){
        if (job->data) free(job->data);
        free(job);
        pthread_mutex_unlock(m);
        uint8_t out[64];
        int n = proto_pack(out,sizeof(out), MSG_ERR,
                           (const uint8_t*)"thread_fail", 12);
        if (n>0) net_client_send(out,n);
        return -4;
    }
    // 成功：主线程立即返回；完成回包由工作线程发送
    return 0;
}

// 网络压测：原样裸数据（不打协议头），尽量跑满带宽
static void do_net_burst(void){
    // 可调：持续时长与块大小
    const int    secs      =  40;        // 默认 40s
    const size_t chunk_sz  = 64*1024;  // 默认 64KB
    // 预生成块内容（可重复复用）
    static unsigned char* block = NULL;
    static size_t block_sz = 0;
    if (!block || block_sz < chunk_sz){
        block = (unsigned char*)malloc(chunk_sz);
        block_sz = chunk_sz;
        for (size_t i=0;i<block_sz;i++) block[i] = (unsigned char)(i & 0xFF);
    }

    uint64_t t0 = nsec_now();
    uint64_t deadline = t0 + (uint64_t)secs * 1000000000ull;
    size_t total = 0;

    // 此函数在 net_client 的接收线程上下文里调用，会占满该线程约 secs 秒；

    while (nsec_now() < deadline){
       //阻塞的 send_all
        if (net_client_send(block, chunk_sz) != 0){
            // 对端断开，提前结束
            break;
        }
        total += chunk_sz;
    }

    uint64_t t1 = nsec_now();
    double msec = (t1 - t0) / 1e6;//t是纳秒 10的6次方
    double sec  = msec / 1000.0;//msec是毫秒
    double mbps = (total * 8.0) / (1024.0*1024.0) / sec; // MiB/s * 8 ≈ Mib/s，1千个1千就是M，对应(1024.0*1024.0) 
    double MBps = (total) / (1024.0*1024.0) / sec;

    // 本端打印统计
    printf("[NET] burst: bytes=%zu time=%.3fs rate=%.2f MB/s (%.2f Mb/s) chunk=%zuB dur=%ds\n",
           total, sec, MBps, mbps, chunk_sz, secs);

   
   
}
void agent_handle_packet(const uint8_t* buf, size_t len){
    proto_hdr_t h; const uint8_t* pl=NULL; uint32_t L=0;
    int ok = proto_try_parse(buf, len, &h, &pl, &L);
    if (ok<=0) return;

    uint8_t out[256];

    switch (h.type){
    case MSG_PING:{
        // int n = proto_pack(out,sizeof(out),MSG_PONG,NULL,0);
        // if (n>0) net_client_send(out,n);
        do_net_burst();
    }break;

    case MSG_SNAP_ALL:{
        for (int ch=0; ch<MAX_CH; ch++) (void)do_snap_one(ch);
        int n=proto_pack(out,sizeof(out),MSG_OK,(const uint8_t*)"snap_all",9);
        if (n>0) net_client_send(out,n);
    }break;

    case MSG_SNAP_CH:{
        if (L<sizeof(pld_ch_t)) { int n=proto_pack(out,sizeof(out),MSG_ERR,(const uint8_t*)"bad pld",7); if(n>0)net_client_send(out,n); break; }
        int ch=((pld_ch_t*)pl)->ch;
        int r = do_snap_one(ch);
        const char* msg = r==0?"snap_ok":"snap_err";
        int n=proto_pack(out,sizeof(out), r==0?MSG_OK:MSG_ERR, (const uint8_t*)msg, (uint32_t)(strlen(msg)+1));
        if (n>0) net_client_send(out,n);
    }break;

    case MSG_REC_START_ALL:{
        int secs = DEFAULT_SECS;
        if (L>=sizeof(pld_rec_all_t)) secs = ((pld_rec_all_t*)pl)->secs;
        for (int ch=0; ch<MAX_CH; ch++) (void)start_rec(ch, secs);
    }break;

    case MSG_REC_START_CH:{
        if (L<sizeof(pld_rec_ch_t)) { int n=proto_pack(out,sizeof(out),MSG_ERR,(const uint8_t*)"bad pld",7); if(n>0)net_client_send(out,n); break; }
        pld_rec_ch_t r = *(pld_rec_ch_t*)pl;
        (void)start_rec(r.ch, r.secs? r.secs:DEFAULT_SECS);
    }break;

    case MSG_REC_STOP_ALL:{
        (void)stop_rec_all();
        int n=proto_pack(out,sizeof(out),MSG_OK,(const uint8_t*)"stop_all",9);
        if (n>0) net_client_send(out,n);
    }break;

    case MSG_REC_STOP_CH:{
        if (L<sizeof(pld_ch_t)) { int n=proto_pack(out,sizeof(out),MSG_ERR,(const uint8_t*)"bad pld",7); if(n>0)net_client_send(out,n); break; }
        int ch = ((pld_ch_t*)pl)->ch;
        (void)stop_rec(ch);
        int n=proto_pack(out,sizeof(out),MSG_OK,(const uint8_t*)"stop_ch",8);
        if (n>0) net_client_send(out,n);
    }break;

    case MSG_TTL_WRITE:
    case MSG_RS485_WRITE:{
        if (L < sizeof(pld_serial_write_t)) {
            int n=proto_pack(out,sizeof(out),MSG_ERR,(const uint8_t*)"bad pld",7);
            if(n>0)net_client_send(out,n);
            break;
        }
        const pld_serial_write_t* w = (const pld_serial_write_t*)pl;
        if (L < sizeof(*w) + w->len) {
            int n=proto_pack(out,sizeof(out),MSG_ERR,(const uint8_t*)"bad len",7);
            if(n>0)net_client_send(out,n);
            break;
        }
        const uint8_t* data = pl + sizeof(*w);

        const int is_ttl   = (h.type==MSG_TTL_WRITE);
        const int is_burst = (w->len==1 && data[0]==0x0A); // 你的压测触发

        // 提交异步任务；忙则立即回 *_busy；成功则无立即回包（完成时回）
        (void)submit_serial_job(is_ttl?SER_TTL:SER_RS485, data, w->len, is_burst);
    }break;

    case MSG_GET_STATUS:{
        pld_status_t st; memset(&st,0,sizeof(st));
        (void)do_status(&st);
        int n=proto_pack(out,sizeof(out),MSG_RESP_STATUS,(const uint8_t*)&st,(uint32_t)sizeof(st));
        if (n>0) net_client_send(out,n);
    }break;

    default:{
        int n=proto_pack(out,sizeof(out),MSG_ERR,(const uint8_t*)"unknown",8);
        if (n>0) net_client_send(out,n);
    }break;
    }
}
