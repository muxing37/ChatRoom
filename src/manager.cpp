#include "manager.h"

const std::string SAVEPATH = "./data";
const std::string USRDATA = SAVEPATH + "/usrdata.json";
const std::string FRIENDDATA = SAVEPATH + "/frienddata.json";
const std::string PCHATDATA = SAVEPATH + "/pchatdata.json";

std::string privateId(int uid1, int uid2) {
  if(uid1 > uid2) std::swap(uid1, uid2);
  return "private_" + std::to_string(uid1) + "_" + std::to_string(uid2);
}
std::string groupId(int groupId) {
  return "group_" + std::to_string(groupId);
}

bool UsrManager::verify(const std::string& username,const std::string& password) {

}

bool UsrManager::regis(const std::string& username,const std::string& password,int uid) {
  std::lock_guard<std::mutex> lock(mtx_);

  if(name_map.count(username)) {
    return false;
  }

  Account auth {
    uid,
    username,
    password,
    password
  };

  uid_map[uid] = auth;
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

bool UsrManager::delUsr(int uid) {

}

bool UsrManager::load(const std::string& path) {
  std::ifstream ifs(path);

  if(!ifs.is_open()) return false;

  nlohmann::json j;
  ifs >> j;

  uid_map.clear();
  name_map.clear();

  for(auto& item : j) {
    Account auth;

    auth.uid = item["uid"];
    auth.username = item["username"];
    auth.password = item["password"];
    // auth.password_hash = item["password_hash"];

    uid_map[auth.uid] = auth;
    name_map[auth.username] = auth.uid;
  }

  return true;
}

bool UsrManager::save(const std::string& path) {
  nlohmann::json j = nlohmann::json::array();

  for(auto& [uid, auth] : uid_map) {
    j.push_back({
      {"uid",auth.uid},
      {"username",auth.username},
      {"password",auth.password},
      {"password_hash",auth.password}
    });
  }

  std::ofstream ofs(path);

  if(!ofs.is_open()) return false;

  ofs << j.dump(4);

  return true;
}

int UsrManager::getMaxUid() {
  int max_uid = 10000;

  for(auto& [uid, auth] : uid_map) {
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
  User *usr = new User;
  usr->uid = iter->second.uid;
  usr->username = iter->second.username;
  usr->online = false;
  return usr;
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

bool FriendManager::removeUsr(int uid) {

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
  save(PCHATDATA);
  return 0;
}

std::vector<Message> MessageManager::getMessagesByTime(int uid,uint64_t time) {
  std::lock_guard<std::mutex> lock(mtx_);
  std::vector<Message> result;
  for(auto& [key, msgs] : history_) {
    for(auto& msg : msgs) {
      if((msg.from_uid == uid || msg.target_id == uid) && msg.time > time) {
        result.push_back(msg);
      }
    }
  }
  std::sort(result.begin(),result.end(),[](const Message& a,const Message& b) { return a.time < b.time; });
  return result;
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

std::vector<Message> MessageManager::getAllMsg(int uid) {
  std::lock_guard<std::mutex> lock(mtx_);
  std::vector<Message> result;
  for(const auto& [key,msgs] : history_) {
    for(const auto& msg : msgs) {
      if(msg.from_uid == uid || msg.target_id == uid) {
        result.push_back(msg);
      }
    }
  }
  std::sort(result.begin(),result.end(),
    [](const Message& a,const Message& b){
      return a.time < b.time;
    });

  return result;
}

std::string MessageManager::getMsgId() {
  uint64_t ms = now_ms();

  uint64_t i = count++;
  return std::to_string(ms) + "_" + std::to_string(i);
}

bool MessageManager::removeUsr(int uid) {

}

bool MessageManager::save(const std::string& path) {
  // std::lock_guard<std::mutex> lock(mtx_);
  nlohmann::json j;
  // 保存聊天记录
  for(const auto& [key, msgs] : history_) {
    j["history"][key] = msgs;
  }
  // // 保存离线消息
  // for(const auto& [uid, msgs] : offline_msg_) {
  //   j["offline"][std::to_string(uid)] = msgs;
  // }
  // 保存计数器
  j["count"] = count.load();
  std::ofstream ofs(path);
  if(!ofs.is_open()) {
    return false;
  }
  ofs << std::setw(4) << j;
  return true;
}

bool MessageManager::load(const std::string& path) {
  std::lock_guard<std::mutex> lock(mtx_);
  std::ifstream ifs(path);
  if (!ifs.is_open()) {
    return false;
  }
  nlohmann::json j;
  ifs >> j;
  history_.clear();
  // offline_msg_.clear();
  // 恢复聊天记录
  if(j.contains("history")) {
    for (auto& [key, value] : j["history"].items()) {
      history_[key] = value.get<std::vector<Message>>();
    }
  }
  // 恢复离线消息
  // if(j.contains("offline")) {
  //   for (auto& [uid, value] : j["offline"].items()) {
  //     offline_msg_[std::stoi(uid)] = value.get<std::vector<Message>>();
  //   }
  // }
  // 恢复计数器
  if(j.contains("count")) {
    count.store(j["count"].get<uint64_t>());
  } else {
    count.store(0);
  }
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

bool SessionManager::isOnline(int uid) {
  std::lock_guard lock(mtx_);
  return user_to_sock_.count(uid);
}

bool SessionManager::sendChatTo(int to_uid,const Message& msg) {
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

bool SessionManager::sendPushTo(int to_uid,nlohmann::json& push) {
  auto sock = getSock(to_uid);
  if(!sock) return -1;
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
