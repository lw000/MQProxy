add_library(kafka_manager_lib STATIC
    src/kafka_consumer.cc
    src/kafka_manager.cc
)

target_include_directories(kafka_manager_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(kafka_manager_lib PUBLIC
    common_lib
    config_store_lib
    CppKafka::cppkafka
)
