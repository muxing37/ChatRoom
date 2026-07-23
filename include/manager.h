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
  bool regis(const std::string& username,const std::string& password,int uid);
  bool login(const std::string& username,const std::string& password,int& out_uid);
  
  bool load(const std::string& path);
  bool save(const std::string& path);

  int getMaxUid();
  bool isExist(const std::string& username);
  User* getUser(int uid);
    
private:
  std::unordered_map<int,User> uid_map;
  std::unordered_map<std::string,int> name_map;
  std::mutex mtx_;
};

class FriendManager {
public:
  std::vector<int> list_friend(int uid);
  std::vector<int> list_request(int uid);
  int del(int uid1,int uid2);
  int request(int uid1,int uid2);
  int agree(int uid1,int uid2);
  int reject(int uid1,int uid2);

  bool isFriend(int uid1,int uid2);

  bool load(const std::string& path);
  bool save(const std::string& path);

private:
  std::unordered_map<int,std::unordered_set<int>> friends;
  std::unordered_map<int,std::unordered_set<int>> requests;
  std::mutex mtx_;
};

class MessageManager {
public:
  int add(const Message& msg);
  int addOfflineMsg(const Message& msg);
  int delOfflineMsg(const std::string& msg_id);
  std::vector<Message> getOfflineMsg(int uid);
  std::vector<Message> getAllMsg(int uid);
  std::vector<Message> getHistory(int uid1,int uid2);
  std::string getMsgId();

  bool save(const std::string& path);
  bool load(const std::string& path);

private:
  std::unordered_map<std::string,std::vector<Message>> history_;
  std::unordered_map<int,std::vector<Message>> offline_msg_;
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
  bool sendTo(int uid,const Message& msg);

private:
  std::unordered_map<int,std::shared_ptr<TcpSocket>> user_to_sock_;
  std::mutex mtx_;
};
