#include "manager.h"

// bool UsrManager::regis()
UidGenerator get_uid;

bool UsrManager::regis(const std::string& username,const std::string& password,int& out_uid) {
  std::lock_guard<std::mutex> lock(mtx_);

  if (name_map_.count(username)) {
    return false;
  }

  int uid = get_uid.get();
  User user{
    uid,
    username,
    password
  };

  uid_map_[uid] = user;
  name_map_[username] = uid;

  out_uid = uid;
  return true;
}

bool UsrManager::login(const std::string& username,const std::string& password,int& out_uid) {
  std::lock_guard<std::mutex> lock(mtx_);

  auto iter = name_map_.find(username);
  if(iter == name_map_.end()) {
    return false;
  }

  int uid = iter->second;
  auto uiter = uid_map_.find(uid);
  if(uiter == uid_map_.end()) {
    return false;
  }

  if(uiter->second.password != password) {
    return false;
  }

  out_uid = uid;
  return true;
}

void SessionManager::unbindUser(int user_id) {
  std::lock_guard<std::mutex> lock(mtx_);
  user_to_sock_.erase(user_id);
}

void SessionManager::bindUser(int user_id,std::shared_ptr<TcpSocket> sock) {
  std::lock_guard<std::mutex> lock(mtx_);
  user_to_sock_[user_id] = std::move(sock);
}

std::shared_ptr<TcpSocket> SessionManager::getSock(int user_id) {
  std::lock_guard<std::mutex> lock(mtx_);
  auto it = user_to_sock_.find(user_id);
  if (it == user_to_sock_.end()) return nullptr;
  return it->second;
}
