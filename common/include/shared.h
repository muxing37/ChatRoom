#pragma once
#include <iostream>
#include <string>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <chrono>

constexpr int64_t MAX_FILE_SIZE = 30LL * 1024 * 1024 * 1024; // 单文件大小上限（QQ 4GB,微信 1GB）

struct User {
    int uid = 0;
    std::string username;
    bool online = false;
};

struct Account {
    int uid;
    std::string username;
    std::string password_hash;
    std::string salt;
    uint64_t last_logout_time;
};

struct GroupInfo {
    int group_id;
    std::string name;
    int owner_uid;
    uint64_t create_time;
};

inline void to_json(nlohmann::json& j,const GroupInfo& g) {
    j = {
        {"group_id",g.group_id},
        {"group_name",g.name},
        {"owner_uid",g.owner_uid},
        {"create_time",g.create_time}
    };
}
inline void from_json(const nlohmann::json& j,GroupInfo& g) {
    j.at("group_id").get_to(g.group_id);
    j.at("group_name").get_to(g.name);
    j.at("owner_uid").get_to(g.owner_uid);
    j.at("create_time").get_to(g.create_time);
}

struct GroupMember {
    int uid;
    std::string usr_name;
    int permission; // 0:群主 1:管理员 2:普通成员
    uint64_t join_time;
    int remind; // 消息免打扰选项 0:正常通知 1:接收消息但不通知 2:不接收消息
};

inline void to_json(nlohmann::json& j,const GroupMember& m) {
    j = {
        {"uid",m.uid},
        {"usr_name",m.usr_name},
        {"permission",m.permission},
        {"join_time",m.join_time},
        {"remind",m.remind}
    };
}
inline void from_json(const nlohmann::json& j,GroupMember& m) {
    j.at("uid").get_to(m.uid);
    j.at("usr_name").get_to(m.usr_name);
    j.at("permission").get_to(m.permission);
    j.at("join_time").get_to(m.join_time);
    j.at("remind").get_to(m.remind);
}

struct JoinRequest { // 好友/加群申请时间、留言等
    int uid;
    uint64_t apply_time;
    std::string text;
};

struct Message {
    std::string message_id;
    std::string type; // text, file
    std::string chat_type; // private, group

    int from_uid;
    int target_id; // uid或group_id

    std::string content;
    uint64_t time;

    int status; // 服务端无用，客户端用于标记已读消息
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

struct FileMeta {
    std::string file_id;
    std::string file_name; // 原始文件名
    int64_t file_size; // 文件大小（字节）
    int uploader_uid; // 上传者 uid
    uint64_t upload_time; // 上传完成时间
    std::string storage_path; // 相对于 data/files/ 的存储路径，如 "f1/23/f123456"
    std::string file_hash; // 文件哈希
    std::string chat_type; // private 或 group
    int target_id; // uid 或 gid

    int status; // 0:上传中(.uploading) 1:已完成
    uint64_t received; // 已接收字节（断点续传起点）
};

inline void to_json(nlohmann::json& j,const FileMeta& f) {
    j = nlohmann::json{
        {"file_id",f.file_id},
        {"file_name",f.file_name},
        {"file_size",f.file_size},
        {"uploader_uid",f.uploader_uid},
        {"upload_time",f.upload_time},
        {"storage_path",f.storage_path},
        {"file_hash",f.file_hash},
        {"chat_type",f.chat_type},
        {"target_id",f.target_id},
        {"status",f.status},
        {"received",f.received}
    };
}

inline void from_json(const nlohmann::json& j,FileMeta& f) {
    j.at("file_id").get_to(f.file_id);
    j.at("file_name").get_to(f.file_name);
    j.at("file_size").get_to(f.file_size);
    j.at("uploader_uid").get_to(f.uploader_uid);
    j.at("upload_time").get_to(f.upload_time);
    j.at("storage_path").get_to(f.storage_path);
    if(j.contains("file_hash")) j.at("file_hash").get_to(f.file_hash);
    else f.file_hash.clear();
    if(j.contains("chat_type")) j.at("chat_type").get_to(f.chat_type);
    else f.chat_type.clear();
    if(j.contains("target_id")) j.at("target_id").get_to(f.target_id);
    else f.target_id = 0;
    if(j.contains("status")) j.at("status").get_to(f.status);
    else f.status = 1;
    if(j.contains("received")) j.at("received").get_to(f.received);
    else f.received = 0;
}

inline uint64_t now_ms() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ).count();
    return static_cast<uint64_t>(ms);
}