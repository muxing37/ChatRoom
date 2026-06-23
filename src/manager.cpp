#include "manager.h"

// bool UsrManager::regis()

bool UsrManager::regis(const std::string& username,const std::string& password,int uid) {
  std::lock_guard<std::mutex> lock(mtx_);

  if (name_map_.count(username)) {
    return false;
  }

  // int uid = get_uid.get();
  User user{
    uid,
    username,
    password
  };

  uid_map_[uid] = user;
  name_map_[username] = uid;

  // out_uid = uid;
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

bool UsrManager::load(const std::string& path) {
  std::ifstream ifs(path);

  if(!ifs.is_open()) return false;

  nlohmann::json j;
  ifs >> j;

  uid_map_.clear();
  name_map_.clear();

  for(auto& item : j) {
    User user;

    user.uid = item["uid"];
    user.username = item["username"];
    user.password = item["password"];

    uid_map_[user.uid] = user;
    name_map_[user.username] = user.uid;
  }

  return true;
}

bool UsrManager::save(const std::string& path) {
  nlohmann::json j = nlohmann::json::array();

  for(auto& [uid, user] : uid_map_) {
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

  for(auto& [uid, user] : uid_map_) {
    if(uid > max_uid) max_uid = uid;
  }

  return max_uid;
}

User* UsrManager::getUser(int uid) {
  std::lock_guard<std::mutex> lock(mtx_);

  auto iter = uid_map_.find(uid);
  if(iter == uid_map_.end()) {
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

  for(int f : it->second) res.push_back(f);

  return res;
}

// int FriendManager::add(int uid1,int uid2) {
//   if(uid1 == uid2) return 1;
  
// }

int FriendManager::del(int uid1,int uid2) {
  std::lock_guard<std::mutex> lock(mtx_);

  auto it1 = friends.find(uid1);
  if(it1 != friends.end()) it1->second.erase(uid2);

  auto it2 = friends.find(uid2);
  if(it2 != friends.end()) it2->second.erase(uid1);

  return 0;
}

int FriendManager::request(int uid1,int uid2) {
  if(uid1 == uid2) return 1; //不可添加自己为好友
  std::lock_guard<std::mutex> lock(mtx_);

  if(isFriend(uid1,uid2)) return 2; //已经是好友

  if(requests[uid1].count(uid2)) {
    return 3; //已发送过申请
  }

  requests[uid1].insert(uid2);
  return 0;
}

int FriendManager::agree(int uid1,int uid2) {
  std::lock_guard<std::mutex> lock(mtx_);

  friends[uid1].insert(uid2);
  friends[uid2].insert(uid1);
  if(requests[uid1].count(uid2)) requests[uid1].erase(uid2);
  if(requests[uid2].count(uid1)) requests[uid2].erase(uid1);
  return true;
}

int FriendManager::reject(int uid1,int uid2) {
  std::lock_guard<std::mutex> lock(mtx_);

  if(requests[uid1].count(uid2)) requests[uid1].erase(uid2);
  if(requests[uid2].count(uid1)) requests[uid2].erase(uid1);
  return true;
}

bool FriendManager::isFriend(int uid1,int uid2) {
  std::lock_guard<std::mutex> lock(mtx_);

  auto it = friends.find(uid1);

  if(it == friends.end()) {
    return false;
  }

  return it->second.count(uid2) > 0;
}

bool FriendManager::load(const std::string& path) {

}

bool FriendManager::save(const std::string& path) {

}

void MenuManager::show(ClientState state) {
  const std::vector<std::string>* menu = nullptr;
  std::string title;

  switch(state) {
    case ClientState::LOGIN:
      menu = &menu_login_;
      title = "登录菜单";
      break;

    case ClientState::MAIN_MENU:
      menu = &menu_main_;
      title = "主菜单";
      break;

    case ClientState::FRIEND_MENU:
      menu = &menu_friend_;
      title = "好友管理";
      break;

    case ClientState::PRIVATE_CHAT:
      menu = &menu_private_chat_;
      title = "私聊";
      break;

    case ClientState::GROUP_MENU:
      menu = &menu_group_;
      title = "群聊";
      break;

    default:
      return;
  }

  std::cout << "\n";
  std::cout << "====================\n";
  std::cout << "      " << title << '\n';
  std::cout << "====================\n\n";

  for (size_t i = 0; i < menu->size(); ++i) {
    std::cout << i + 1 << ". " << (*menu)[i] << '\n';
  }

  std::cout << "\n请选择: ";
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
