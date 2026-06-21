#pragma once
#include <iostream>
#include <string>

struct User {
    int uid;
    std::string username;
    std::string password;
};

enum class ClientState {
    LOGIN,
    MAIN_MENU,
    FRIEND_MENU,
    PRIVATE_CHAT,
    GROUP_MENU
};