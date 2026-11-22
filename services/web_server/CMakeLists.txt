add_library(web_server_lib STATIC
    src/web_server.cc
)

target_include_directories(web_server_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(web_server_lib PRIVATE
    ${CMAKE_SOURCE_DIR}/3d-partys/cpp-httplib
)

target_link_libraries(web_server_lib PUBLIC
    common_lib
    config_store_lib
    kafka_manager_lib
    nlohmann_json::nlohmann_json
)
