#include "manager.h"

const std::string SAVEPATH = "./data";
const std::string USRDATA = SAVEPATH + "/usrdata.json";
const std::string FRIENDDATA = SAVEPATH + "/frienddata.json";

std::string privateId(int uid1, int uid2) {
  if(uid1 > uid2) std::swap(uid1, uid2);
  return "private_" + std::to_string(uid1) + "_" + std::to_string(uid2);
}
std::string groupId(int groupId) {
  return "group_" + std::to_string(groupId);
}

bool UsrManager::regis(const std::string& username,const std::string& password,int uid) {
  std::lock_guard<std::mutex> lock(mtx_);

  if(name_map.count(username)) {
    return false;
  }

  User user{
    uid,
    username,
    password
  };

  uid_map[uid] = user;
  name_map[username] = uid;

  return true;
}

bool UsrManager::login(const std::string& username,const std::string& password,int& out_uid) {
  std::lock_guard<std::mutex> lock(mtx_);

  auto iter = name_map.find(username);
  if(iter == name_map.end()) {
    return false;
  }

  int uid = iter->second;
  auto uiter = uid_map.find(uid);
  if(uiter == uid_map.end()) {
    return false;
  }

  if(uiter->second.password != password) {
    return false;
  }

  out_uid = uid;
  return true;
}

bool UsrManager::load(const std::string& path) {
  std::ifstream ifs(path);

  if(!ifs.is_open()) return false;

  nlohmann::json j;
  ifs >> j;

  uid_map.clear();
  name_map.clear();

  for(auto& item : j) {
    User user;

    user.uid = item["uid"];
    user.username = item["username"];
    user.password = item["password"];

    uid_map[user.uid] = user;
    name_map[user.username] = user.uid;
  }

  return true;
}

bool UsrManager::save(const std::string& path) {
  nlohmann::json j = nlohmann::json::array();

  for(auto& [uid, user] : uid_map) {
    j.push_back({
      {"uid",user.uid},
      {"username",user.username},
      {"password",user.password}
    });
  }

  std::ofstream ofs(path);

  if(!ofs.is_open()) return false;

  ofs << j.dump(4);

  return true;
}

int UsrManager::getMaxUid() {
  int max_uid = 10000;

  for(auto& [uid, user] : uid_map) {
    if(uid > max_uid) max_uid = uid;
  }

  return max_uid;
}

bool UsrManager::isExist(const std::string& username) {
  std::lock_guard<std::mutex> lock(mtx_);
  if(name_map.count(username)) {
    return true;
  }
  return false;
}

User* UsrManager::getUser(int uid) {
  std::lock_guard<std::mutex> lock(mtx_);

  auto iter = uid_map.find(uid);
  if(iter == uid_map.end()) {
    return nullptr;
  }

  return &iter->second;
}

std::vector<int> FriendManager::list_friend(int uid) {
  std::lock_guard<std::mutex> lock(mtx_);

  std::vector<int> res;

  auto it = friends.find(uid);
  if(it == friends.end()) return res;

  for(int f : it->second) res.push_back(f);

  return res;
}

std::vector<int> FriendManager::list_request(int uid) {
  std::lock_guard<std::mutex> lock(mtx_);

  std::vector<int> res;

  auto it = requests.find(uid);
  if(it == requests.end()) return res;

  for(int f : it->second) {
    std::cout << f << std::endl;
    res.push_back(f);
  }
  return res;
}

int FriendManager::del(int uid1,int uid2) {
  std::lock_guard<std::mutex> lock(mtx_);

  auto it1 = friends.find(uid1);
  if(it1 != friends.end()) it1->second.erase(uid2);

  auto it2 = friends.find(uid2);
  if(it2 != friends.end()) it2->second.erase(uid1);

  save(FRIENDDATA);
  return 0;
}

int FriendManager::request(int uid1,int uid2) {
  if(uid1 == uid2) return 1; //不可添加自己为好友
  std::lock_guard<std::mutex> lock(mtx_);
  if(FriendManager::isFriend(uid1,uid2)) return 2; //已经是好友
  if(requests[uid1].count(uid2)) {
    return 3; //已发送过申请
  }
  requests[uid1].insert(uid2);
  save(FRIENDDATA);

  return 0;
}

int FriendManager::agree(int uid1,int uid2) {
  std::lock_guard<std::mutex> lock(mtx_);

  friends[uid1].insert(uid2);
  friends[uid2].insert(uid1);
  if(requests[uid1].count(uid2)) requests[uid1].erase(uid2);
  if(requests[uid2].count(uid1)) requests[uid2].erase(uid1);
  
  save(FRIENDDATA);
  return true;
}

int FriendManager::reject(int uid1,int uid2) {
  std::lock_guard<std::mutex> lock(mtx_);

  if(requests[uid1].count(uid2)) requests[uid1].erase(uid2);
  if(requests[uid2].count(uid1)) requests[uid2].erase(uid1);
  save(FRIENDDATA);
  return true;
}

bool FriendManager::isFriend(int uid1,int uid2) {
  // std::lock_guard<std::mutex> lock(mtx_);
  auto it = friends.find(uid1);
  if(it == friends.end()) {
    return false;
  }
  return it->second.count(uid2) > 0;
}

bool FriendManager::load(const std::string& path) {
  std::ifstream ifs(path);

  if(!ifs.is_open()) return false;

  nlohmann::json j;
  ifs >> j;

  friends.clear();
  requests.clear();

  if(j.contains("friends")) {
    for(auto& [uid, arr] : j["friends"].items()) {
      int id = std::stoi(uid);
      for(auto& x : arr) {
        friends[id].insert(x.get<int>());
      }
    }
  }

  if(j.contains("requests")) {
    for(auto& [uid, arr] : j["requests"].items()) {
      int id = std::stoi(uid);
      for(auto& x : arr) {
        requests[id].insert(x.get<int>());
      }
    }
  }

  return true;
}

bool FriendManager::save(const std::string& path) {
  nlohmann::json j;
  // 保存好友
  for(const auto& [uid, list] : friends) {
    for(int id : list) {
      j["friends"][std::to_string(uid)].push_back(id);
    }
  }
  // 保存好友申请
  for(const auto& [uid, list] : requests) {
    for(int id : list) {
      j["requests"][std::to_string(uid)].push_back(id);
    }
  }

  std::ofstream ofs(path);
  if(!ofs.is_open()) return false;

  ofs << std::setw(4) << j;
  return true;
}

int MessageManager::add(const Message& msg) {
  std::lock_guard lock(mtx_);
  std::string cvs_id = privateId(msg.from_uid,msg.target_id);
  history_[cvs_id].push_back(msg);
  return 0;
}

int MessageManager::addOfflineMsg(const Message& msg) {
  std::lock_guard lock(mtx_);
  offline_msg_[msg.target_id].push_back(msg);
  return true;
}

int MessageManager::delOfflineMsg(const std::string& msg_id) {

}

std::vector<Message> MessageManager::getOfflineMsg(int uid) {
  std::lock_guard lock(mtx_);
  auto it = offline_msg_.find(uid);
  if(it == offline_msg_.end()) {
    return {};
  }
  return it->second;
}

std::vector<Message> MessageManager::getHistory(int uid1,int uid2) {
  std::lock_guard lock(mtx_);
  std::string cvs_id = privateId(uid1,uid2);
  auto it = history_.find(cvs_id);
  if(it == history_.end()) {
    return {};
  }
  return it->second;
}

bool MessageManager::save(const std::string& path) {
  std::lock_guard lock(mtx_);

}

bool MessageManager::load(const std::string& path) {
  std::lock_guard lock(mtx_);

}

void SessionManager::unbindUser(int user_id) {
  std::lock_guard<std::mutex> lock(mtx_);
  user_to_sock_.erase(user_id);
}

void SessionManager::bindUser(int user_id,std::shared_ptr<TcpSocket> sock) {
  std::lock_guard<std::mutex> lock(mtx_);
  user_to_sock_[user_id] = std::move(sock);
}

bool SessionManager::isOnline(int uid) {
  std::lock_guard lock(mtx_);
  return user_to_sock_.count(uid);
}

bool SessionManager::sendTo(int to_uid,const Message& msg) {
  auto sock = getSock(to_uid);
  if(!sock) return -1;
  nlohmann::json push;
  push["msg_type"] = "push";
  push["type"] = "chat";
  push["action"] = "private_chat";
  push["data"] = msg;
  if(sock->sendMsg(push.dump()) != NetResult::OK) {
    return -1;
  }
  return 0;
}

std::shared_ptr<TcpSocket> SessionManager::getSock(int user_id) {
  std::lock_guard<std::mutex> lock(mtx_);
  auto it = user_to_sock_.find(user_id);
  if(it == user_to_sock_.end()) return nullptr;
  return it->second;
}
