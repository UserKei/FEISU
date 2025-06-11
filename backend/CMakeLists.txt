cmake_minimum_required(VERSION 3.10)
project(backend)

# 设置 vcpkg 工具链
set(CMAKE_TOOLCHAIN_FILE "C:/Users/James/vcpkg/scripts/buildsystems/vcpkg.cmake")

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 添加Windows 11的版本定义
add_compile_definitions(_WIN32_WINNT=0x0A00)  # Windows 10/11

# 设置UTF-8编码支持
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
add_compile_definitions(_SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING)

# 方法1: 使用正确目标名（首选）
find_package(crow CONFIG REQUIRED)

add_executable(backend main.cpp)

# 尝试两种可能的目标名
if(TARGET Crow::Crow)
    target_link_libraries(backend PRIVATE Crow::Crow)
elseif(TARGET crow::crow)
    target_link_libraries(backend PRIVATE crow::crow)
else()
    # 方法2: 回退到手动配置
    message(WARNING "Crow targets not found, using manual configuration")
    include_directories("C:/Users/James/vcpkg/installed/x64-windows/include")
    target_link_libraries(backend PRIVATE ws2_32 crypt32)
endif()

# Windows 特定设置
if(WIN32)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SUBSYSTEM:CONSOLE")
endif()