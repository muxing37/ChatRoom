cmake_minimum_required(VERSION 3.16)
project(ChatProject)

set(CMAKE_CXX_STANDARD 17)

include_directories(${PROJECT_SOURCE_DIR}/common/include)
include_directories(${PROJECT_SOURCE_DIR}/client/include)
include_directories(${PROJECT_SOURCE_DIR}/server/include)
include_directories(${PROJECT_SOURCE_DIR}/client/include/third)
include_directories(${PROJECT_SOURCE_DIR}/server/include/third)

# server
add_executable(server
    server/src/main_server.cpp
    server/src/server.cpp
    server/src/imgmeta.cpp
    server/src/manager.cpp
    common/src/socket.cpp
    common/src/hash.cpp
)

# client
add_executable(client
    client/src/main_client.cpp
    client/src/client.cpp
    client/src/service.cpp
    client/src/context.cpp
    client/src/webui.cpp
    client/src/cliui.cpp
    common/src/socket.cpp
    common/src/hash.cpp
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(READLINE REQUIRED readline)
find_package(OpenSSL REQUIRED)

include_directories(${READLINE_INCLUDE_DIRS})

target_link_libraries(server PRIVATE ${READLINE_LIBRARIES} OpenSSL::SSL OpenSSL::Crypto)
target_link_libraries(client PRIVATE ${READLINE_LIBRARIES} OpenSSL::SSL OpenSSL::Crypto)