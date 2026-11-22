cmake_minimum_required(VERSION 3.15)

# vcpkg 的 Boost wrapper 使用旧 FindBoost 模块，CMake 4.x 需显式关闭该策略告警
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 OLD)
endif()

# vcpkg 工具链必须在 project() 之前设置，否则不生效
if(NOT DEFINED VCPKG_ROOT OR "${VCPKG_ROOT}" STREQUAL "")
    set(VCPKG_ROOT "C:/vcpkg" CACHE PATH "vcpkg 安装根目录")
endif()
set(CMAKE_TOOLCHAIN_FILE "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "")

project(MQProxy VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(VCPKG_INSTALLED_DIR "${VCPKG_ROOT}/installed/x64-windows")
set(CMAKE_PREFIX_PATH "${VCPKG_INSTALLED_DIR};${CMAKE_PREFIX_PATH}")
set(RdKafka_DIR "${VCPKG_INSTALLED_DIR}/share/rdkafka")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

find_package(cppkafka CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(SQLiteCpp CONFIG REQUIRED)

if(WIN32)
    # cpp-httplib 0.66 要求 Windows 10 及以上
    add_compile_definitions(_WIN32_WINNT=0x0A00 NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

add_subdirectory(common)
add_subdirectory(services/config_store)
add_subdirectory(services/kafka_manager)
add_subdirectory(services/web_server)

add_executable(${PROJECT_NAME}
    apps/mqproxy/main.cc
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    web_server_lib
    kafka_manager_lib
    config_store_lib
    common_lib
)

if(WIN32)
    # 运行期依赖 DLL：由配置决定从 vcpkg debug/bin 或 bin 复制，避免硬编码盘符
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "$<IF:$<CONFIG:Debug>,${VCPKG_INSTALLED_DIR}/debug/bin,${VCPKG_INSTALLED_DIR}/bin>"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
        VERBATIM
    )

    # 静态管理页随可执行文件部署，路径由 SERVER.webroot 注入
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/web")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/web
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/web
        )
    endif()

    # config.toml 由用户从 config.example.toml 创建，存在时自动复制
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/config.toml")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_CURRENT_SOURCE_DIR}/config.toml
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/config.toml
        )
    else()
        message(STATUS "config.toml not found; copy config.example.toml to config.toml to enable auto-copy")
    endif()
endif()

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /MP)
endif()
