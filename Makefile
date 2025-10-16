# 1. 编译器定义
CC := arm-mix410-linux-gcc

# 2. 编译选项
# -I./  表示在当前目录寻找头文件
CFLAGS := -I./ -g -Wall -static

# 3. 链接选项
# -L./  表示在当前目录寻找库文件
# -lhsdk_media_3520  表示链接libhsdk_media_3520.a
# -lpthread          链接线程库
# 3. 链接选项 (终极顺序修正版)
# 黄金法则: 上层封装库在前，底层依赖库在后。
LIBS := -L./lib/ \
        -lhsdk_media_3520 \
        -lss_mpi \
        -lss_nnie \
        -lss_ive \
        -lss_hdmi \
        -lss_upvqe \
        -lss_dnvqe \
        -lss_voice_engine \
        -lsecurec \
        -lpthread -lm -ldl -lcrypto -lssl

# 4. 定义所有目标程序
TARGETS :=  snap_test 

# 5. 默认目标 "all"
all: $(TARGETS)

# 6. 通用编译规则 (关键)
# $< 代表第一个依赖项 (即.c文件)
# $@ 代表目标 (即可执行文件)
# LIBS 变量现在被放在了命令的末尾
$(TARGETS): %: %.c
	$(CC) $(CFLAGS) $< -o $@ $(LIBS)

# 7. 清理规则
clean:
	rm -f $(TARGETS) *.o