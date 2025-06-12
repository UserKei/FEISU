cmake_minimum_required(VERSION 3.10)
project(backend)

# Windows特定设置：vcpkg工具链
if(WIN32)
    set(CMAKE_TOOLCHAIN_FILE "C:/Users/James/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")
endif()

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows特定设置
if(WIN32)
    # Windows 10/11定义
    add_compile_definitions(_WIN32_WINNT=0x0A00)
    
    # UTF-8编码支持
    add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
    add_compile_definitions(_SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING)
    
    # 设置控制台子系统
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SUBSYSTEM:CONSOLE")
endif()

# macOS特定设置
if(APPLE)
    # 确保使用C++17标准
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")
endif()

# 查找Crow库
set(Crow_FOUND FALSE)
set(CROW_FOUND FALSE)

# 方法1: 尝试CONFIG模式（Windows上vcpkg的配置）
find_package(Crow CONFIG QUIET)
if(TARGET Crow::Crow OR TARGET crow::crow)
    set(Crow_FOUND TRUE)
endif()

# 方法2: 尝试pkg-config（macOS/Linux）
if(NOT Crow_FOUND)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(CROW QUIET IMPORTED_TARGET crow)
        if(CROW_FOUND)
            set(Crow_FOUND TRUE)
        endif()
    endif()
endif()

# 如果找不到Crow库，处理回退方案
if(NOT Crow_FOUND)
    if(WIN32)
        # Windows上的手动配置
        message(WARNING "Crow targets not found, using manual configuration")
        include_directories("C:/Users/James/vcpkg/installed/x64-windows/include")
    else()
        # macOS/Linux上的安装说明
        message(FATAL_ERROR "Crow not found. Please install Crow framework:\n"
            "  macOS: brew install crowcpp/crow/crow\n"
            "  Ubuntu: sudo apt-get install libcrow-dev\n"
            "  Or build from source: https://github.com/CrowCpp/Crow")
    endif()
endif()

# 创建可执行文件
add_executable(backend main.cpp)

# 链接Crow库
if(TARGET Crow::Crow)
    target_link_libraries(backend PRIVATE Crow::Crow)
elseif(TARGET crow::crow)
    target_link_libraries(backend PRIVATE crow::crow)
elseif(CROW_FOUND AND TARGET PkgConfig::CROW)
    target_link_libraries(backend PRIVATE PkgConfig::CROW)
elseif(WIN32)
    # Windows手动链接
    target_link_libraries(backend PRIVATE ws2_32 crypt32)
endif()

# macOS额外链接pthread
if(APPLE)
    find_library(PTHREAD_LIB pthread)
    if(PTHREAD_LIB)
        target_link_libraries(backend ${PTHREAD_LIB})
    endif()
endif()

# 设置Debug模式
set(CMAKE_BUILD_TYPE Debug)