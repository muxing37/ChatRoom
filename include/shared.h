#pragma once
#include <iostream>
#include <string>

struct User {
    int uid;
    std::string username;
    std::string password;
};

struct Message {
    std::string message_id;
    std::string type; // text, file, system

    int from_uid;
    int to_uid;

    std::string content;
    uint64_t timestamp;

    int status;  // 0:待发送 1:已送达 2:已读
};

enum class ClientState {
    LOGIN,
    MAIN_MENU,
    FRIEND_MENU,
    PRIVATE_CHAT,
    GROUP_MENU
};