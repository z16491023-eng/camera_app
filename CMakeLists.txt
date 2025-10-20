cmake_minimum_required(VERSION 3.20)
project(camera_app   VERSION 1.0 LANGUAGES C)

message(STATUS ">>> TOP CMake: ${CMAKE_CURRENT_LIST_FILE}")

set(CMAKE_C_STANDARD 11)
# 可执行文件输出到 build/bin（由 -B 决定 build 根）
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
# 显式列文件；GLOB，配 CONFIGURE_DEPENDS 自动增量
file(GLOB SRC_LIST CONFIGURE_DEPENDS
     ${CMAKE_SOURCE_DIR}/src/*.c)
    
add_executable(snap_test ${SRC_LIST})
# 头文件
target_include_directories(snap_test PRIVATE
  ${CMAKE_SOURCE_DIR}/include
)

# ---- 链接第三方静态库（按你给的顺序，显式 .a 防止误连到 .so）----
set(LIB_DIR ${CMAKE_SOURCE_DIR}/lib)

# 如需“尽量全静态”，打开这个选项；不想全静态，改成 OFF 即可
option(STATIC_ALL "Try fully static link (-static)" OFF)
if(STATIC_ALL)
  # 优先选 .a，并尝试把 libc 也静态化
  set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
  target_link_options(snap_test PRIVATE -static)
endif()

target_link_libraries(snap_test PRIVATE
  ${LIB_DIR}/libhsdk_media_3520.a
  ${LIB_DIR}/libss_mpi.a           # ← 先放 mpi
  ${LIB_DIR}/libss_voice_engine.a  # ← 再放 voice_engine（mpi 依赖它）
  ${LIB_DIR}/libsecurec.a
  ${LIB_DIR}/libss_upvqe.a
  ${LIB_DIR}/libss_dnvqe.a
  ${LIB_DIR}/libss_nnie.a
  ${LIB_DIR}/libss_ive.a
  ${LIB_DIR}/libss_hdmi.a
  ${LIB_DIR}/libhgs_misc.a
  
)
# 系统库最后（动态）
target_link_libraries(snap_test PRIVATE pthread m dl)
# ---- 诊断输出（可删）----
message(STATUS "SOURCE_DIR = ${CMAKE_SOURCE_DIR}")
message(STATUS "BINARY_DIR = ${CMAKE_BINARY_DIR}")
message(STATUS "RUNTIME_OUT= ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")