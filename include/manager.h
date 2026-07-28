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
  bool verify(const std::string& username,const std::string& password); //验证密码
  bool regis(const std::string& username,const std::string& password,int uid);
  bool login(const std::string& username,const std::string& password,int& out_uid);
  bool delUsr(int uid);

  bool load(const std::string& path);
  bool save(const std::string& path);

  int getMaxUid();
  bool isExist(const std::string& username);
  User* getUser(int uid);
    
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

private:

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
