add_library(config_store_lib STATIC
    src/config_store.cc
)

target_include_directories(config_store_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(config_store_lib PUBLIC
    common_lib
    SQLiteCpp
    nlohmann_json::nlohmann_json
)
