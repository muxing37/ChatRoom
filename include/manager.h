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
  // int add(int uid1,int uid2);
  int del(int uid1,int uid2);
  int request(int uid1,int uid2);
  int agree(int uid1,int uid2);
  int reject(int uid1,int uid2);

  bool isFriend(int uid1,int uid2);

  bool load(const std::string& path);
  bool save(const std::string& path);

private:
  // struct Request{
  //   int from;
  //   int to;
  //   int status;
  //   std::string msg;
  // };

  std::unordered_map<int,std::unordered_set<int>> friends;
  std::unordered_map<int,std::unordered_set<int>> requests;
  // std::unordered_map<int,std::vector<Request>> requests;
  std::mutex mtx_;
};

class MessageManager {
public:
  int add(int from_uid,int to_uid,const Message& msg);
  std::vector<Message> getOffline(int uid);
  std::vector<Message> getHistory(int uid1,int uid2);

  void mark_sent(const std::string& message_id);
  void mark_read(const std::string& message_id);

  bool save(const std::string& path);
  bool load(const std::string& path);
  
private:
  std::vector<Message> messages;
  std::mutex mtx;

};

class SessionManager {
public:
  void bindUser(int user_id,std::shared_ptr<TcpSocket> sock);
  void unbindUser(int user_id);
  std::shared_ptr<TcpSocket> getSock(int user_id);

  bool isOnline(int uid);
  bool sendTo(int uid,const Message& msg);

  // template<typename Func>
  // void forEach(Func&& func);

private:
  std::unordered_map<int,std::shared_ptr<TcpSocket>> user_to_sock_;
  std::mutex mtx_;
};

// template<typename Func>
// void SessionManager::forEach(Func&& func) {
//   std::lock_guard<std::mutex> lock(mtx_);
//   for(auto& [uid,sock] : user_to_sock_) {
//     func(uid,sock);
//   }
// }