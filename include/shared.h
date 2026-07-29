#pragma once
#include <iostream>
#include <string>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <chrono>

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

struct GroupInfo {
    int group_id;
    std::string name;
    int owner_uid;
    uint64_t creat_time;
};

struct GroupMember {
    int uid;
    int permission; // 0:群主 1:管理员 2:普通成员
    uint64_t join_time;
    int remind; // 消息免打扰选项 0:正常通知 1:接收消息但不通知 2:不接收消息
};

struct JoinRequest { // 好友/加群申请时间、留言等
    int uid;
    uint64_t apply_time;
    std::string text;
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

uint64_t now_ms() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ).count();
    return static_cast<uint64_t>(ms);
}