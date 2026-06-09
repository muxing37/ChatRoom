cmake_minimum_required(VERSION 3.10)
project(ChatRoom)

set(CMAKE_CXX_STANDARD 17)

add_executable(chat
    src/main.cpp
    src/server.cpp
    src/client.cpp
    src/ctrl.cpp
    src/socket.cpp
)

target_include_directories(chat PRIVATE include)

target_link_libraries(chat pthread readline history)