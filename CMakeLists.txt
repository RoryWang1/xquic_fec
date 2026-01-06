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

# 创建我们要开发的 App
add_executable(demo_app src/main.c)

# 包含 xquic 头文件
target_include_directories(demo_app PRIVATE 
    third_party/xquic/include 
    ${SSL_INC_PATH}
)

# 链接 xquic 库
# xquic-static 是 xquic CMakeLists 里定义的 target 名字
target_link_libraries(demo_app PRIVATE xquic-static)

# 因为 xquic-static 可能没有传递 SSL 的链接依赖（因为它也是静态库），我们需要显式链接 SSL
target_link_libraries(demo_app PRIVATE ${SSL_LIB_PATH} pthread)

if(APPLE)
    target_link_libraries(demo_app PRIVATE "-framework CoreFoundation")
endif()

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


