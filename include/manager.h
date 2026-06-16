#pragma once
#include "socket.h"
#include "user.h"
#include <termios.h>
#include <pthread.h>
#include <cstring>
#include <cstdlib>
#include <thread>
#include <atomic>
#include <mutex>
#include <memory>

class UidGenerator {
public:
  int get() {
    return counter_.fetch_add(1);
  }

private:
  std::atomic<int> counter_{10000};
};

class UsrManager {
public:
    bool regis(const std::string& username,const std::string& password,int& out_uid);
    bool login(const std::string& username,const std::string& password,int& out_uid);

private:
    std::unordered_map<int,User> uid_map_;
    std::unordered_map<std::string,int> name_map_;
    std::mutex mtx_;
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