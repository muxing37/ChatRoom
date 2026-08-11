#pragma once
#include "socket.h"
#include "shared.h"
#include <fstream>
#include <termios.h>
#include <pthread.h>
#include <cstring>
#include <cstdlib>
#include <thread>
#include <atomic>
#include <mutex>
#include <optional>
#include <unordered_set>
#include <memory>

class UidGenerator {
public:
  void init(int start) {
    counter_.store(start);
  }

  int get() {
    return counter_.fetch_add(1);
  }

private:
  std::atomic<int> counter_{10000};
};

class UsrManager {
public:
  bool verify(int uid,const std::string& password); // 验证密码
  bool regis(const std::string& username,const std::string& password,int uid);
  bool login(const std::string& username,const std::string& password,int& out_uid);
  bool delUsr(int uid,const std::string& password);

  bool load(const std::string& path);
  bool save(const std::string& path);

  int getMaxUid();
  bool isExist(const std::string& username);
  std::optional<User> getUser(int uid);
    
private:
  std::unordered_map<int,Account> uid_map;
  std::unordered_map<std::string,int> name_map;
  std::mutex mtx_;
};

class FriendManager {
public:
  std::vector<int> list_friend(int uid);
  std::vector<int> list_request(int uid);
  int del(int uid1,int uid2);
  int request(int from_uid,int to_uid);
  int agree(int uid1,int uid2);
  int reject(int uid1,int uid2);

  bool isFriend(int uid1,int uid2);

  bool removeUsr(int uid); // 注销

  bool load(const std::string& path);
  bool save(const std::string& path);

  // 屏蔽/拉黑相关
  int block(int uid,int target);
  int unblock(int uid,int target);
  bool isBlocked(int uid,int target);
  std::vector<int> getBlockList(int uid);

private:
  bool isFriend_(int uid1,int uid2);

private:
  std::unordered_map<int,std::unordered_set<int>> friends_;
  std::unordered_map<int,std::unordered_set<int>> requests_;
  std::unordered_map<int,std::unordered_set<int>> block_;
  std::mutex mtx_;
};

class MessageManager {
public:
  int add(const Message& msg);

  std::vector<Message> getMessagesByTime(int uid,uint64_t time); //获取某个时间后的所有消息，可用于离线消息的同步
  std::vector<Message> getAllMsg(int uid); //获取所有消息
  std::vector<Message> getHistory(int uid1,int uid2);
  // std::vector<Message> getGroupMessages(int gid,uint64_t after_time);
  std::string getMsgId();

  bool removeUsr(int uid);

  bool save(const std::string& path);
  bool load(const std::string& path);

private:
  std::unordered_map<std::string,std::vector<Message>> history_;
  std::atomic<uint64_t> count{0};
  std::mutex mtx_;
};

class GroupManager {
public:
  int createGroup(const User& owner,std::string& name,int& out_gid);
  int disbandGroup(int group_id,int uid);
  int renameGroup(int group_id,int handler_id,std::string& new_name);
  int transferOwner(); // 转移群主
  // 成员管理
  int joinRequest(int group_id,int apply_uid);
  int handleRequest(int group_id,int handler_uid,const User& target_usr,bool approval);
  int leaveGroup(int group_id,int uid);
  int kickMember(int group_id,int handler_uid,int target_id);
  int removeUser(int uid); // 注销相关，退出所有群，群主则解散其群
  // 管理员设置
  int setAdmin(int group_id,int handler_uid,int target_id,bool admin);
  // 消息免打扰设置
  int setRemind(int group_id,int uid,int remind);
  // 查询相关
  GroupInfo getGroupInfo(int group_id);
  std::vector<GroupMember> getMembers(int group_id);
  std::vector<int> listJoinRequests(int group_id);
  std::vector<int> getUserGroup(int uid); // 获取某用户所在的所有群id
  bool isMember(int group_id,int uid);
  int getPermission(int group_id,int uid); // 查询某用户在群中的权限
  uint64_t getJoinTime(int group_id,int uid);
  int ifRemind(int group_id,int uid);

  bool load(const std::string& path);
  bool save(const std::string& path);

private:
  int makeGroupId() {
    return next_group_id_++;
  }

private:
  std::unordered_map<int,GroupInfo> groups_; // group_id -> info
  std::unordered_map<int,std::unordered_map<int,GroupMember>> members_; // group_id -> uid -> info
  std::unordered_map<int,std::unordered_set<int>> join_requests_; // group_id -> set<apply_uid>
  std::atomic<int> next_group_id_{100000};
  std::mutex mtx_;
};

class FileManager {
public:
  FileManager(const std::string& meta_path = "./data/filemeta.json",const std::string& storage_dir = "./data/files/");

  bool init();
  bool addFileMeta(const FileMeta& meta); // 新增文件元数据
  bool updateFileMeta(const FileMeta& meta); // 更新已有条目
  std::optional<FileMeta> getFileMeta(const std::string& file_id);
  std::optional<FileMeta> findIncomplete(const std::string& hash,int uid,const std::string& chat_type,int target_id);// 按 哈希,上传者,目标 查找未完成任务（重启后续传）
  std::vector<FileMeta> getAllMeta();
  std::string generateFileId();
  std::string makeStoragePath(const std::string& file_id); // 格式类似 "f1/23/f123456"
  // 路径
  std::string getFullPath(const std::string& storage_path); // 根目录 + 相对路径
  std::string getPartPath(const std::string& file_id); // tmp/<file_id>.uploading
  // 上传生命周期
  bool createPart(const std::string& file_id); // 建 .uploading (正在传输的文件)
  bool writePart(const std::string& file_id,uint64_t offset,const char* data,size_t len); // 写 .uploading 并更新进度
  uint64_t getPartSize(const std::string& file_id); // 当前已接收字节（续传相关）
  bool finishUpload(const std::string& file_id); // 将 .uploading 正式存入, status=1
  bool removePart(const std::string& file_id); // 失败/放弃时清理

private:
  bool loadMeta();
  bool saveMeta();
  void cleanupOrphans(); // 启动时清理

private:
  std::string meta_path_;
  std::string storage_dir_; // 文件存储目录（以 / 结尾）
  std::unordered_map<std::string,FileMeta> metas_; // file_id -> FileMeta
  mutable std::mutex mtx_;
  std::atomic<uint64_t> id_counter_{0};
};

class SessionManager {
public:
  void bindUser(int user_id,std::shared_ptr<TcpSocket> sock);
  void unbindUser(int user_id);
  std::shared_ptr<TcpSocket> getSock(int user_id);
  
  bool isOnline(int uid);
  bool sendChatTo(int uid,const Message& msg);
  bool sendPushTo(int uid,nlohmann::json& push);

private:
  std::unordered_map<int,std::shared_ptr<TcpSocket>> user_to_sock_;
  std::mutex mtx_;
};
