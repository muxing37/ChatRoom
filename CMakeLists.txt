cmake_minimum_required(VERSION 3.16)
project(ChatProject)

set(CMAKE_CXX_STANDARD 17)

include_directories(${PROJECT_SOURCE_DIR}/include)

# server
add_executable(server
    src/main_server.cpp
    src/server.cpp
    src/client.cpp
    src/socket.cpp
    src/ctrl.cpp
)

# client
add_executable(client
    src/main_client.cpp
    src/server.cpp
    src/client.cpp
    src/socket.cpp
    src/ctrl.cpp
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(READLINE REQUIRED readline)

include_directories(${READLINE_INCLUDE_DIRS})

target_link_libraries(server PRIVATE ${READLINE_LIBRARIES})
target_link_libraries(client PRIVATE ${READLINE_LIBRARIES})