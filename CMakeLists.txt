cmake_minimum_required(VERSION 3.10)
project(MyXQuicProject)

set(CMAKE_C_STANDARD 99)
set(PLATFORM "mac")

# 设置 xquic 编译所需的选项 (复制自之前的成功编译命令)
set(XQC_ENABLE_TESTING OFF CACHE BOOL "" FORCE) # 作为一个库使用时，通常不需要编译它的测试用例
set(XQC_SUPPORT_SENDMMSG_BUILD OFF CACHE BOOL "" FORCE) # macOS 不支持
set(XQC_ENABLE_EVENT_LOG ON CACHE BOOL "" FORCE)
set(XQC_ENABLE_BBR2 ON CACHE BOOL "" FORCE)
set(XQC_ENABLE_RENO ON CACHE BOOL "" FORCE)
set(XQC_ENABLE_FEC ON CACHE BOOL "" FORCE)
set(XQC_ENABLE_RSC ON CACHE BOOL "" FORCE)
set(GCOV OFF CACHE BOOL "" FORCE)

# 配置 SSL 路径 (指向 third_party/xquic 下已经存在的 boringssl)
set(SSL_TYPE "boringssl" CACHE STRING "" FORCE)
set(BORINGSSL_ROOT "${CMAKE_SOURCE_DIR}/third_party/xquic/third_party/boringssl")
set(SSL_PATH "${BORINGSSL_ROOT}" CACHE STRING "" FORCE)
set(SSL_INC_PATH "${BORINGSSL_ROOT}/include" CACHE STRING "" FORCE)
# 注意：这里假设 boringssl 已经编译好了，库文件在 build 目录下
set(SSL_LIB_PATH "${BORINGSSL_ROOT}/build/libssl.a;${BORINGSSL_ROOT}/build/libcrypto.a" CACHE STRING "" FORCE)

# 添加 xquic 子目录
add_subdirectory(third_party/xquic)

# 查找 Libevent (Mac/Homebrew)
set(LIBEVENT_ROOT "/opt/homebrew")
include_directories(${LIBEVENT_ROOT}/include)
link_directories(${LIBEVENT_ROOT}/lib)
set(LIBEVENT_LIB event)



# --- Camera Streaming Demo ---

# Server
add_executable(camera_server src/camera_server.c)
target_include_directories(camera_server PRIVATE 
    third_party/xquic/include 
    ${SSL_INC_PATH}
)
target_link_libraries(camera_server PRIVATE xquic-static ${SSL_LIB_PATH} pthread ${LIBEVENT_LIB} c++)
if(APPLE)
    target_link_libraries(camera_server PRIVATE "-framework CoreFoundation")
endif()

# Client
add_executable(camera_client src/camera_client.c)
target_include_directories(camera_client PRIVATE 
    third_party/xquic/include 
    ${SSL_INC_PATH}
)
target_link_libraries(camera_client PRIVATE xquic-static ${SSL_LIB_PATH} pthread ${LIBEVENT_LIB} c++)
if(APPLE)
    target_link_libraries(camera_client PRIVATE "-framework CoreFoundation")
endif()

# Dual Player (SDL2)
# Links against SDL2 from Homebrew
add_executable(dual_player src/dual_player.c)
target_include_directories(dual_player PRIVATE /opt/homebrew/include)
target_link_libraries(dual_player PRIVATE -L/opt/homebrew/lib -lSDL2 pthread)


