cmake_minimum_required(VERSION 3.16)
project(CppChatServer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(
    chat_server
    src/main.cpp 
    src/app.cpp
    src/net/socket.cpp)

target_include_directories(chat_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(
    socket_test
    tests/socket_test.cpp
    src/net/socket.cpp  )

target_include_directories(socket_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(
    socket_options_test
    tests/socket_options_test.cpp
    src/net/socket.cpp 
)
target_include_directories(socket_options_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(
    socket_listen_test
    tests/socket_listen_test.cpp
    src/net/socket.cpp 
)
target_include_directories(socket_listen_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(
    epoll_test
    tests/epoll_test.cpp
    src/net/epoll.cpp 
)
target_include_directories(epoll_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(
    event_dispatcher_test
    tests/event_dispatcher_test.cpp
    src/net/event_dispatcher.cpp
    src/net/epoll.cpp 
)
target_include_directories(event_dispatcher_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

find_package(Threads REQUIRED)
add_executable(
    event_loop_test
    tests/event_loop_test.cpp
    src/net/event_loop.cpp
    src/net/event_dispatcher.cpp 
    src/net/epoll.cpp 
)
target_include_directories(event_loop_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(event_loop_test PRIVATE Threads::Threads)

add_executable(
    tcp_accept_test
    tests/tcp_accept_test.cpp
    src/net/event_loop.cpp
    src/net/event_dispatcher.cpp 
    src/net/epoll.cpp
    src/net/socket.cpp
)
target_include_directories(tcp_accept_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(tcp_accept_test PRIVATE Threads::Threads)

add_executable(
    tcp_connection_test
    tests/tcp_connection_test.cpp
    src/net/length_field_codec.cpp
    src/net/event_loop.cpp
    src/net/event_dispatcher.cpp 
    src/net/epoll.cpp
    src/net/socket.cpp 
    src/net/tcp_connection.cpp
)
target_include_directories(tcp_connection_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(tcp_connection_test PRIVATE Threads::Threads)

add_executable(
    length_field_codec_test
    tests/length_field_codec_test.cpp
    src/net/length_field_codec.cpp
)

target_include_directories(length_field_codec_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
      
add_executable(
    tcp_connection_length_field_codec_test
    tests/tcp_connection_codec_test.cpp
    src/net/event_loop.cpp
    src/net/event_dispatcher.cpp 
    src/net/epoll.cpp
    src/net/socket.cpp 
    src/net/tcp_connection.cpp
    src/net/length_field_codec.cpp
)
target_include_directories(tcp_connection_length_field_codec_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(tcp_connection_length_field_codec_test PRIVATE Threads::Threads)

add_executable(
    tcp_connection_send_test
    tests/tcp_connection_send_test.cpp
    src/net/socket.cpp
    src/net/epoll.cpp
    src/net/event_dispatcher.cpp
    src/net/event_loop.cpp
    src/net/tcp_connection.cpp
    src/net/length_field_codec.cpp
)

target_include_directories(tcp_connection_send_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(tcp_connection_send_test PRIVATE Threads::Threads)

add_executable(
    tcp_connection_shutdown_test
    tests/tcp_connection_shutdown_test.cpp
    src/net/socket.cpp
    src/net/epoll.cpp
    src/net/event_dispatcher.cpp
    src/net/event_loop.cpp
    src/net/tcp_connection.cpp
    src/net/length_field_codec.cpp
)

target_include_directories(tcp_connection_shutdown_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(tcp_connection_shutdown_test PRIVATE Threads::Threads)

add_executable(
    tcp_server_test
    tests/tcp_server_test.cpp
    src/net/socket.cpp
    src/net/epoll.cpp
    src/net/event_dispatcher.cpp
    src/net/event_loop.cpp
    src/net/tcp_connection.cpp
    src/net/length_field_codec.cpp
    src/net/tcp_server.cpp
)

target_include_directories(tcp_server_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(tcp_server_test PRIVATE Threads::Threads)

add_executable(tcp_connection_backpressure_test
    tests/tcp_connection_backpressure_test.cpp
    src/net/socket.cpp
    src/net/epoll.cpp
    src/net/event_dispatcher.cpp
    src/net/event_loop.cpp
    src/net/tcp_connection.cpp
    src/net/length_field_codec.cpp
)

target_include_directories(tcp_connection_backpressure_test
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(tcp_connection_backpressure_test
    PRIVATE Threads::Threads
)