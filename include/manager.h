#pragma once
#include "socket.h"
#include "user.h"
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
  User* getUser(int uid);
    
private:
  std::unordered_map<int,User> uid_map_;
  std::unordered_map<std::string,int> name_map_;
  std::mutex mtx_;
};

class FriendManager {
public:
  std::vector<int> list(int uid);
  int add(int uid1,int uid2);
  int del(int uid1,int uid2);
  int apply(int uid);

  bool load(const std::string& path);
  bool save(const std::string& path);

private:
  std::unordered_map<int,std::unordered_set<int>> friends;
  std::mutex mtx_;
};

class MenuManager {
public:
  void show(ClientState state);

private:
  const std::vector<std::string> menu_login_ = {
    "登录",
    "注册",
    "退出"
  };

  const std::vector<std::string> menu_main_ = {
    "好友管理",
    "私聊",
    "群聊",
    "个人信息",
    "退出登录"
  };

  const std::vector<std::string> menu_friend_ = {
    "查看好友列表",
    "添加好友",
    "删除好友",
    "好友申请",
    "返回主菜单"
  };

  const std::vector<std::string> menu_private_chat_ = {
    "选择好友聊天",
    "查看聊天记录",
    "返回主菜单"
  };

  const std::vector<std::string> menu_group_ = {
    "查看群聊",
    "创建群聊",
    "加入群聊",
    "退出群聊",
    "返回主菜单"
  };

};

class SessionManager {
public:
  void bindUser(int user_id,std::shared_ptr<TcpSocket> sock);
  void unbindUser(int user_id);
  std::shared_ptr<TcpSocket> getSock(int user_id);

  template<typename Func>
  void forEach(Func&& func);

private:
  std::unordered_map<int,std::shared_ptr<TcpSocket>> user_to_sock_;
  std::mutex mtx_;
};

template<typename Func>
void SessionManager::forEach(Func&& func) {
  std::lock_guard<std::mutex> lock(mtx_);
  for(auto& [uid,sock] : user_to_sock_) {
    func(uid,sock);
  }
}