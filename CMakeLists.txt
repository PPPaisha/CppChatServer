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