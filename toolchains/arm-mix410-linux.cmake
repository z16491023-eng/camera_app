# 目标系统
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# --- 1) 指定交叉编译器（绝对路径，避免被本机 gcc/g++ 抢走） ---
set(CMAKE_C_COMPILER "")