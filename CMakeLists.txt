cmake_minimum_required(VERSION 3.16)
project(ChatProject)

set(CMAKE_CXX_STANDARD 17)

include_directories(${PROJECT_SOURCE_DIR}/common/include)
include_directories(${PROJECT_SOURCE_DIR}/client/include)
include_directories(${PROJECT_SOURCE_DIR}/server/include)
include_directories(${PROJECT_SOURCE_DIR}/client/include/third)
include_directories(${PROJECT_SOURCE_DIR}/server/include/third)

file(READ ${PROJECT_SOURCE_DIR}/client/web/chat.html CHAT_HTML_RAW)
file(MAKE_DIRECTORY ${PROJECT_BINARY_DIR}/generated)
file(WRITE ${PROJECT_BINARY_DIR}/generated/chat_html.h
    "// auto-generated, do not edit\n"
    "static const char* CHAT_HTML_CONTENT = R\"CHATHTML(\n${CHAT_HTML_RAW}\n)CHATHTML\";\n")
include_directories(${PROJECT_BINARY_DIR}/generated)

# server
add_executable(server
    server/src/main_server.cpp
    server/src/server.cpp
    server/src/imgmeta.cpp
    server/src/manager.cpp
    server/src/reactor.cpp
    server/src/database.cpp
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
    client/src/sqlite.cpp
    common/src/socket.cpp
    common/src/hash.cpp
)

find_package(PkgConfig REQUIRED)
find_package(OpenSSL REQUIRED)

find_path(GLOG_INCLUDE_DIR glog/logging.h)
find_library(GLOG_LIBRARY NAMES glog)
include_directories(${GLOG_INCLUDE_DIR})

include_directories(${READLINE_INCLUDE_DIRS})
find_library(MYSQL_LIBRARY NAMES mysqlclient)
find_library(SQLITE3_LIBRARY NAMES sqlite3)
target_link_libraries(client PRIVATE ${SQLITE3_LIBRARY})
target_link_libraries(server PRIVATE ${MYSQL_LIBRARY})
target_link_libraries(server PRIVATE ${READLINE_LIBRARIES} OpenSSL::SSL OpenSSL::Crypto)
target_link_libraries(client PRIVATE ${READLINE_LIBRARIES} OpenSSL::SSL OpenSSL::Crypto)
target_link_libraries(server PRIVATE ${GLOG_LIBRARY})
