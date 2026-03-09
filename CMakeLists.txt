cmake_minimum_required(VERSION 3.15)
project(MQProxy VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置vcpkg工具链
set(CMAKE_TOOLCHAIN_FILE "D:/work/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")

# 设置vcpkg installed目录
set(VCPKG_INSTALLED_DIR "D:/work/vcpkg/installed/x64-windows")

# 设置CMAKE_PREFIX_PATH帮助CMake找到包
set(CMAKE_PREFIX_PATH "${VCPKG_INSTALLED_DIR};${CMAKE_PREFIX_PATH}")
set(RdKafka_DIR "${VCPKG_INSTALLED_DIR}/share/rdkafka")

# 设置输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# 查找依赖包
find_package(RdKafka CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# tomlplusplus使用本地3d-partys目录

# 收集源文件
file(GLOB_RECURSE SOURCES 
    "src/*.cpp"
    "src/*.cc"
    "src/*.cxx"
)

# 收集头文件
file(GLOB_RECURSE HEADERS 
    "include/*.h"
    "include/*.hpp"
)

# 创建可执行文件
add_executable(${PROJECT_NAME}
    ${SOURCES}
    ${HEADERS}
)

# 包含头文件目录
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/3d-partys
    ${CMAKE_CURRENT_SOURCE_DIR}/3d-partys/tomlplusplus
)

# 链接库
target_link_libraries(${PROJECT_NAME} PRIVATE
    RdKafka::rdkafka
    spdlog::spdlog
    nlohmann_json::nlohmann_json
)

# Windows特定设置
if(WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        _WIN32_WINNT=0x0601
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
    
    # 复制配置文件到输出目录
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/config.toml
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/config.toml
    )
    
    # 根据构建类型设置DLL源目录
    # Debug模式: vcpkg/debug/bin
    # Release模式: vcpkg/bin
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(DLL_SRC_DIR "D:/work/vcpkg/installed/x64-windows/debug/bin")
        if(NOT EXISTS "${DLL_SRC_DIR}")
            set(DLL_SRC_DIR "D:/work/vcpkg/installed/x64-windows/bin")
        endif()
        
        # Debug模式下的DLL列表
        set(DLL_LIST
            "rdkafka.dll"
            "rdkafka++.dll"
            "libssl-3-x64.dll"
            "libcrypto-3-x64.dll"
            "zlibd1.dll"
            "zstd.dll"
            "lz4d.dll"
            "tomlplusplus-3.dll"
            "spdlogd.dll"
            "libcurl-d.dll"
        )
    else()
        set(DLL_SRC_DIR "D:/work/vcpkg/installed/x64-windows/bin")
        
        # Release模式下的DLL列表
        set(DLL_LIST
            "rdkafka.dll"
            "rdkafka++.dll"
            "libssl-3-x64.dll"
            "libcrypto-3-x64.dll"
            "zlib1.dll"
            "zstd.dll"
            "lz4.dll"
            "tomlplusplus-3.dll"
            "spdlog.dll"
            "libcurl.dll"
        )
    endif()
    
    # 复制程序依赖的第三方DLL (仅当源文件存在时)
    foreach(DLL_NAME ${DLL_LIST})
        if(EXISTS "${DLL_SRC_DIR}/${DLL_NAME}")
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${DLL_SRC_DIR}/${DLL_NAME}
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/${DLL_NAME}
            )
        endif()
    endforeach()
endif()

# 安装规则
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

# 调试配置
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
    # 多线程编译
    target_compile_options(${PROJECT_NAME} PRIVATE /MP)
endif()
