cmake_minimum_required(VERSION 3.20)
project(camera_app LANGUAGES C)

set(CMAKE_C_STANDARD 11)
# 可执行文件输出到 build/bin（由 -B 决定 build 根）
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 头文件
include_directories(${CMAKE_SOURCE_DIR}/include)

# proto
add_library(proto STATIC
  ${CMAKE_SOURCE_DIR}/libs/proto/proto.c
)

# net
add_library(netclient STATIC
  ${CMAKE_SOURCE_DIR}/libs/net/net_client.c
)

# 串口库
set(EXT_DIR ${CMAKE_SOURCE_DIR}/external)
add_library(serialmgr STATIC
  ${CMAKE_SOURCE_DIR}/libs/serial/serial_mgr.c
)
# serialmgr 依赖外部 hgs_misc
target_link_libraries(serialmgr PRIVATE ${EXT_DIR}/libhgs_misc.a)

# 媒体薄封装
add_library(agent STATIC
  ${CMAKE_SOURCE_DIR}/libs/agent/agent.c
)
set(LIB_DIR ${CMAKE_SOURCE_DIR}/external)
target_link_libraries(agent PRIVATE
  ${LIB_DIR}/libhsdk_media_3520.a
  ${LIB_DIR}/libss_mpi.a
  ${LIB_DIR}/libss_voice_engine.a
  ${LIB_DIR}/libsecurec.a
  ${LIB_DIR}/libss_upvqe.a
  ${LIB_DIR}/libss_dnvqe.a
  ${LIB_DIR}/libss_nnie.a
  ${LIB_DIR}/libss_ive.a
  ${LIB_DIR}/libss_hdmi.a
  pthread m dl
)


# btt-agent/主函数
add_executable(btt-agent
  ${CMAKE_SOURCE_DIR}/apps/btt-agent/main.c
)



target_link_libraries(btt-agent PRIVATE netclient agent proto serialmgr  pthread m dl)









# /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
# /////////////////////////////////////////////////工具区///////////////////////////////////////////////////////////

# tools、camerad 先留空目录，后续里程碑再加
message(STATUS "RUNTIME_OUT= ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

set(EXT_DIR ${CMAKE_SOURCE_DIR}/external)
add_executable(bt_at_test ${CMAKE_SOURCE_DIR}/apps/tools/bt_at_test.c)
target_include_directories(bt_at_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(bt_at_test PRIVATE
  ${EXT_DIR}/libhgs_misc.a
  pthread m dl)  # 系统库最后

set(EXT_DIR ${CMAKE_SOURCE_DIR}/external)
add_executable(bt_echo ${CMAKE_SOURCE_DIR}/apps/tools/bt_echo.c)
target_include_directories(bt_echo  PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(bt_echo  PRIVATE
  ${EXT_DIR}/libhgs_misc.a
  pthread m dl)  # 系统库最后

set(EXT_DIR ${CMAKE_SOURCE_DIR}/external)
add_executable(uat_init ${CMAKE_SOURCE_DIR}/apps/tools/uat_init.c)
target_include_directories(uat_init  PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(uat_init  PRIVATE
  ${EXT_DIR}/libhgs_misc.a
  pthread m dl)  # 系统库最后


add_executable(rec_30s apps/tools/rec_30s.c)
target_include_directories(rec_30s PRIVATE ${CMAKE_SOURCE_DIR}/include)
set(LIB_DIR ${CMAKE_SOURCE_DIR}/external)
target_link_libraries(rec_30s PRIVATE
  ${LIB_DIR}/libhsdk_media_3520.a
  ${LIB_DIR}/libss_mpi.a
  ${LIB_DIR}/libss_voice_engine.a
  ${LIB_DIR}/libsecurec.a
  ${LIB_DIR}/libss_upvqe.a
  ${LIB_DIR}/libss_dnvqe.a
  ${LIB_DIR}/libss_nnie.a
  ${LIB_DIR}/libss_ive.a
  ${LIB_DIR}/libss_hdmi.a
  pthread m dl
)

add_executable(snap_test apps/tools/snap_test.c)
target_include_directories(snap_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
set(LIB_DIR ${CMAKE_SOURCE_DIR}/external)
target_link_libraries(snap_test PRIVATE
  ${LIB_DIR}/libhsdk_media_3520.a
  ${LIB_DIR}/libss_mpi.a
  ${LIB_DIR}/libss_voice_engine.a
  ${LIB_DIR}/libsecurec.a
  ${LIB_DIR}/libss_upvqe.a
  ${LIB_DIR}/libss_dnvqe.a
  ${LIB_DIR}/libss_nnie.a
  ${LIB_DIR}/libss_ive.a
  ${LIB_DIR}/libss_hdmi.a
  pthread m dl
)
