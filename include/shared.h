#pragma once
#include <iostream>
#include <string>
#include <cstdint>
#include <nlohmann/json.hpp>

struct User {
    int uid;
    std::string username;
    bool online;
    // std::string password;
};

struct Account {
    int uid;
    std::string username;
    std::string password;
    std::string password_hash;
    std::string salt;
};

struct Message {
    std::string message_id;
    std::string type; // text, file, system
    std::string chat_type; // private, group

    int from_uid;
    int target_id; // uid或group_id

    std::string content;
    uint64_t time;

    int status;  // 0:待发送(接收方离线) 1:已送达
};
inline void to_json(nlohmann::json& j,const Message& msg) {
    j = {
        {"message_id",msg.message_id},
        {"type",msg.type},
        {"chat_type",msg.chat_type},
        {"from_uid",msg.from_uid},
        {"target_id",msg.target_id},
        {"content",msg.content},
        {"time",msg.time},
        {"status",msg.status}
    };
}
inline void from_json(const nlohmann::json& j,Message& msg) {
    j.at("message_id").get_to(msg.message_id);
    j.at("type").get_to(msg.type);
    j.at("chat_type").get_to(msg.chat_type);
    j.at("from_uid").get_to(msg.from_uid);
    j.at("target_id").get_to(msg.target_id);
    j.at("content").get_to(msg.content);
    j.at("time").get_to(msg.time);
    j.at("status").get_to(msg.status);
}
