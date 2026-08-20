#pragma once
#include "socket.h"
#include "shared.h"
#include "reactor.h"
#include "database.h"
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
  void init(int start) { counter_.store(start); }
  int get() { return counter_.fetch_add(1); }

private:
  std::atomic<int> counter_{10000};
};

class UsrManager {
public:

  bool regis(const std::string& username,const std::string& password,int uid);
  bool login(const std::string& username,const std::string& password,int& out_uid);
  bool delUsr(int uid,const std::string& password);
  void setDb(Database* db) { db_ = db; }
  int getMaxUid();
  bool isExist(const std::string& username);
  std::optional<User> getUser(int uid);

  bool load();
  uint64_t getLastLogout(int uid);
  void updateLastLogout(int uid,uint64_t t);

private:
  bool insertUser(const Account& a);
  bool deleteUser(int uid);
  std::vector<Account> selectAllUsers();

  bool verify(int uid,const std::string& password); // 验证密码

private:
  std::unordered_map<int,Account> uid_map_;
  std::unordered_map<std::string,int> name_map_;
  std::mutex mtx_;
  Database* db_ = nullptr;
};

class FriendManager {
public:
  void setDb(Database* db) { db_ = db; }

  std::vector<int> list_friend(int uid);
  std::vector<int> list_request(int uid);
  int del(int uid1,int uid2);
  int request(int from_uid,int to_uid);
  int agree(int uid1,int uid2);
  int reject(int uid1,int uid2);
  bool isFriend(int uid1,int uid2);
  bool removeUsr(int uid); // 注销
  // 屏蔽/拉黑相关
  int block(int uid,int target);
  int unblock(int uid,int target);
  bool isBlocked(int uid,int target);
  std::vector<int> getBlockList(int uid);
  bool load();

private:
  bool insertFriend(int a,int b);
  bool deleteFriendPair(int a,int b);
  bool insertFriendRequest(int from,int to);
  bool deleteFriendRequest(int a,int b);
  bool insertBlock(int uid,int target);
  bool deleteBlock(int uid,int target);
  bool deleteUserAll(int uid);
  void loadFriends();
  void loadRequests();
  void loadBlocks();

  bool isFriend_(int uid1,int uid2);

private:
  std::unordered_map<int,std::unordered_set<int>> friends_;
  std::unordered_map<int,std::unordered_set<int>> requests_;
  std::unordered_map<int,std::unordered_set<int>> block_;
  Database* db_ = nullptr;
  std::mutex mtx_;
};

class MessageManager {
public:
  void setDb(Database* db) { db_ = db; }
  int add(const Message& msg);

  std::vector<Message> getMessagesByTime(int uid,uint64_t time); //获取某个时间后的所有消息，可用于离线消息的同步
  std::vector<Message> getAllMsg(int uid); //获取所有消息
  std::string getMsgId();
  bool removeGroupMessages(int gid);
  bool removeUsr(int uid);

private:
  std::vector<Message> queryMessages(int uid,uint64_t time);
  Message rowToMessage(MYSQL_ROW row);

private:
  std::atomic<uint64_t> count{0};
  std::mutex mtx_;
  Database* db_ = nullptr;
};

class GroupManager {
public:
  void setDb(Database* db) { db_ = db; }

  int createGroup(const User& owner,std::string& name,int& out_gid);
  int disbandGroup(int group_id,int uid);
  // 成员管理
  int joinRequest(int group_id,int apply_uid);
  int handleRequest(int group_id,int handler_uid,const User& target_usr,bool approval);
  int leaveGroup(int group_id,int uid);
  int kickMember(int group_id,int handler_uid,int target_id);
  int removeUser(int uid,std::vector<int>* disbanded); // 注销相关，退出所有群，群主则解散其群
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
  bool load();

private:
  bool insertGroup(const GroupInfo& g);
  bool deleteGroup(int gid);
  bool insertGroupMember(int gid,const GroupMember& m);
  bool deleteGroupMember(int gid,int uid);
  bool updateMemberPermission(int gid,int uid,int perm);
  bool updateMemberRemind(int gid,int uid,int remind);
  bool insertJoinRequest(int gid,int uid);
  bool deleteJoinRequest(int gid,int uid);
  void loadGroups();
  void loadMembers();
  void loadJoinRequests();

  int makeGroupId() { return next_group_id_++; }

private:
  std::unordered_map<int,GroupInfo> groups_; // group_id -> info
  std::unordered_map<int,std::unordered_map<int,GroupMember>> members_; // group_id -> uid -> info
  std::unordered_map<int,std::unordered_set<int>> join_requests_; // group_id -> set<apply_uid>
  std::atomic<int> next_group_id_{100000};
  std::mutex mtx_;
  Database* db_ = nullptr;
};

class FileManager {
public:
  FileManager(const std::string& meta_path = "./data/filemeta.json",const std::string& storage_dir = "./data/files/");

  void setDb(Database* db) { db_ = db; }

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
  bool insertFileMeta(const FileMeta& m);
  bool updateFileMetaDb(const FileMeta& m);
  bool updateFileReceived(const std::string& file_id,uint64_t received);
  bool updateFileStatus(const std::string& file_id,int status);
  bool deleteFileMetaDb(const std::string& file_id);
  bool loadMeta();

  void cleanupOrphans(); // 启动时清理

private:
  std::string storage_dir_; // 文件存储目录（以 / 结尾）
  std::unordered_map<std::string,FileMeta> metas_; // file_id -> FileMeta
  mutable std::mutex mtx_;
  std::atomic<uint64_t> id_counter_{0};
  Database* db_ = nullptr;
};

class SessionManager {
public:
  void bindUser(int user_id,std::shared_ptr<Connection> conn);
  void unbindUser(int user_id);
  std::shared_ptr<Connection> getConn(int uid);

  bool isOnline(int uid);

  bool sendChatTo(int uid,const Message& msg);
  bool sendPushTo(int uid,nlohmann::json& push);

private:
  std::unordered_map<int,std::shared_ptr<Connection>> user_to_conn_;
  std::mutex mtx_;
};
