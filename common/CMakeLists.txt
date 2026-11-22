add_library(common_lib STATIC
    src/bootstrap_config.cc
    src/logger.cc
    src/service_base.cc
    src/service_manager.cc
)

target_include_directories(common_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(common_lib PRIVATE
    ${CMAKE_SOURCE_DIR}/3d-partys/tomlplusplus
)

target_link_libraries(common_lib PUBLIC
    spdlog::spdlog
)
