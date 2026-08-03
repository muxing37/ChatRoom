#include "manager.h"

const std::string SAVEPATH = "./data";
const std::string USRDATA = SAVEPATH + "/usrdata.json";
const std::string FRIENDDATA = SAVEPATH + "/frienddata.json";
const std::string PCHATDATA = SAVEPATH + "/pchatdata.json";
const std::string GROUPDATA = SAVEPATH + "/groupdata.json";

std::string privateId(int uid1, int uid2) {
  if(uid1 > uid2) std::swap(uid1, uid2);
  return "private_" + std::to_string(uid1) + "_" + std::to_string(uid2);
}
std::string groupId(int groupId) {
  return "group_" + std::to_string(groupId);
}
// 用户管理相关
bool UsrManager::verify(const std::string& username,const std::string& password) {

  return true;
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

// 好友管理相关
std::vector<int> FriendManager::list_friend(int uid) {
  std::lock_guard<std::mutex> lock(mtx_);

  std::vector<int> res;

  auto it = friends_.find(uid);
  if(it == friends_.end()) return res;

  for(int f : it->second) res.push_back(f);

  return res;
}

std::vector<int> FriendManager::list_request(int uid) {
  std::lock_guard<std::mutex> lock(mtx_);

  std::vector<int> res;

  auto it = requests_.find(uid);
  if(it == requests_.end()) return res;

  for(int f : it->second) {
    std::cout << f << std::endl;
    res.push_back(f);
  }
  return res;
}

int FriendManager::del(int uid1,int uid2) {
  std::lock_guard<std::mutex> lock(mtx_);

  auto it1 = friends_.find(uid1);
  if(it1 != friends_.end()) it1->second.erase(uid2);

  auto it2 = friends_.find(uid2);
  if(it2 != friends_.end()) it2->second.erase(uid1);

  save(FRIENDDATA);
  return 0;
}

int FriendManager::request(int from_uid,int to_uid) {
  if(from_uid == to_uid) return 1; //不可添加自己为好友
  std::lock_guard<std::mutex> lock(mtx_);
  if(FriendManager::isFriend(from_uid,to_uid)) return 2; //已经是好友
  if(requests_[to_uid].count(from_uid)) {
    return 3; //已发送过申请
  }
  requests_[to_uid].insert(from_uid);
  save(FRIENDDATA);
  return 0;
}

int FriendManager::agree(int uid1,int uid2) {
  std::lock_guard<std::mutex> lock(mtx_);

  friends_[uid1].insert(uid2);
  friends_[uid2].insert(uid1);
  if(requests_[uid1].count(uid2)) requests_[uid1].erase(uid2);
  if(requests_[uid2].count(uid1)) requests_[uid2].erase(uid1);
  
  save(FRIENDDATA);
  return true;
}

int FriendManager::reject(int uid1,int uid2) {
  std::lock_guard<std::mutex> lock(mtx_);

  if(requests_[uid1].count(uid2)) requests_[uid1].erase(uid2);
  if(requests_[uid2].count(uid1)) requests_[uid2].erase(uid1);
  save(FRIENDDATA);
  return true;
}

bool FriendManager::isFriend(int uid1,int uid2) {
  // std::lock_guard<std::mutex> lock(mtx_);
  auto it = friends_.find(uid1);
  if(it == friends_.end()) {
    return false;
  }
  return it->second.count(uid2) > 0;
}

bool FriendManager::removeUsr(int uid) {

  return true;
}

// 好友屏蔽/拉黑相关
int FriendManager::block(int uid,int target) {
  std::lock_guard lock(mtx_);
  if(!isFriend(uid,target)) return -1;   // 只能屏蔽好友
  block_[uid].insert(target);
  save(FRIENDDATA);
  return 0;
}

int FriendManager::unblock(int uid,int target) {
  std::lock_guard lock(mtx_);
  block_[uid].erase(target);
  save(FRIENDDATA);
  return 0;
}

bool FriendManager::isBlocked(int uid,int target) {
  std::lock_guard lock(mtx_);
  auto it = block_.find(uid);
  if(it != block_.end() && it->second.count(target) > 0) {
    return true;
  }
  return false;
}

std::vector<int> FriendManager::getBlockList(int uid) {
  std::lock_guard lock(mtx_);
  auto it = block_.find(uid);
  if(it == block_.end()) return {};
  std::vector<int> result;
  for(auto id : it->second) {
    result.push_back(id);
  }
  return result;
}

bool FriendManager::load(const std::string& path) {
  std::ifstream ifs(path);

  if(!ifs.is_open()) return false;

  nlohmann::json j;
  ifs >> j;

  friends_.clear();
  requests_.clear();

  if(j.contains("friends")) {
    for(auto& [uid, arr] : j["friends"].items()) {
      int id = std::stoi(uid);
      for(auto& x : arr) {
        friends_[id].insert(x.get<int>());
      }
    }
  }

  if(j.contains("requests")) {
    for(auto& [uid, arr] : j["requests"].items()) {
      int id = std::stoi(uid);
      for(auto& x : arr) {
        requests_[id].insert(x.get<int>());
      }
    }
  }

  if(j.contains("block")) {
    for(auto& [uid, arr] : j["block"].items()) {
      int id = std::stoi(uid);
      for(auto& x : arr) {
        block_[id].insert(x.get<int>());
      }
    }
  }
  return true;
}

bool FriendManager::save(const std::string& path) {
  nlohmann::json j;
  // 保存好友
  for(const auto& [uid, list] : friends_) {
    for(int id : list) {
      j["friends"][std::to_string(uid)].push_back(id);
    }
  }
  // 保存好友申请
  for(const auto& [uid, list] : requests_) {
    for(int id : list) {
      j["requests"][std::to_string(uid)].push_back(id);
    }
  }
  // 保存屏蔽列表
  for(const auto& [uid, set] : block_) {
    for(int blocked_uid : set) {
      j["block"][std::to_string(uid)].push_back(blocked_uid);
    }
  }
  std::ofstream ofs(path);
  if(!ofs.is_open()) return false;

  ofs << std::setw(4) << j;
  return true;
}

// 消息管理相关
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

  return true;
}

bool MessageManager::save(const std::string& path) {
  // std::lock_guard<std::mutex> lock(mtx_);
  nlohmann::json j;
  // 保存聊天记录
  for(const auto& [key, msgs] : history_) {
    j["history"][key] = msgs;
  }
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
  // 恢复计数器
  if(j.contains("count")) {
    count.store(j["count"].get<uint64_t>());
  } else {
    count.store(0);
  }
  return true;
}

// 群管理相关
int GroupManager::createGroup(const User& owner,std::string& name,int& out_gid) {
  std::lock_guard<std::mutex> lock(mtx_);
  int gid = makeGroupId();
  GroupInfo info {
    gid,
    name,
    owner.uid,
    now_ms()
  };
  groups_[gid] = info;
  GroupMember owner_info {
    owner.uid,
    owner.username,
    0,
    info.create_time,
    0
  };
  members_[gid][owner.uid] = owner_info;

  out_gid = gid;
  save(GROUPDATA);
  return 0;
}

int GroupManager::disbandGroup(int group_id,int uid) {
  std::lock_guard<std::mutex> lock(mtx_);
  auto it = groups_.find(group_id);
  if(it == groups_.end()) return -1; // 群不存在
  if(uid != it->second.owner_uid) return -2; // 非群主
  groups_.erase(it);
  members_.erase(group_id);
  join_requests_.erase(group_id);
  save(GROUPDATA);
  return 0;
}

int GroupManager::renameGroup(int group_id,int handler_id,std::string& new_name) {
  std::lock_guard<std::mutex> lock(mtx_);

  return 0;
}

int GroupManager::transferOwner() { // 转移群主
  std::lock_guard<std::mutex> lock(mtx_);

  return 0;
}
// 成员管理
int GroupManager::joinRequest(int group_id,int apply_uid) {
  std::lock_guard<std::mutex> lock(mtx_);
  if(groups_.find(group_id) == groups_.end()) return -1; // 群不存在
  if(members_[group_id].count(apply_uid)) return -2; // 已是成员
  if(join_requests_[group_id].count(apply_uid)) return -3; // 已申请
  join_requests_[group_id].insert(apply_uid);
  save(GROUPDATA);
  return 0;
}

int GroupManager::handleRequest(int group_id,int handler_uid,const User& target_usr,bool approval) {
  std::lock_guard<std::mutex> lock(mtx_);
  auto& req = join_requests_[group_id];
  if(!req.count(target_usr.uid)) return -1; // 不存在该申请
  if(approval) {
    auto& mem = members_[group_id];
    if(!mem.count(handler_uid) || mem[handler_uid].permission > 1) return -2; // 没有权限
    GroupMember info {
      target_usr.uid,
      target_usr.username,
      2,
      now_ms(),
      0
    };
    mem[target_usr.uid] = info;
    req.erase(target_usr.uid);
  } else {
    req.erase(target_usr.uid);
  }
  save(GROUPDATA);
  return 0;
}

int GroupManager::leaveGroup(int group_id,int uid) {
  std::lock_guard<std::mutex> lock(mtx_);
  auto& mem = members_[group_id];
  if(!mem.count(uid)) return -1;
  if(mem[uid].permission == 0) return -2;  // 群主不能直接退出，应先解散或转让群主权限
  mem.erase(uid);
  save(GROUPDATA);
  return 0;
}

int GroupManager::kickMember(int group_id,int handler_uid,int target_id) {
  std::lock_guard<std::mutex> lock(mtx_);
  auto& mem = members_[group_id];
  if(!mem.count(handler_uid) || !mem.count(target_id)) return -1;
  int hand_per = mem[handler_uid].permission;
  int target_per = mem[target_id].permission;
  if(hand_per == 2) return -2; // 普通成员无权限
  if(hand_per == 1 && (target_per == 0 || target_per == 1)) return -3; // 管理员无权踢群主或其他管理员
  mem.erase(target_id);
  save(GROUPDATA);
  return 0;
}
// 管理员设置
int GroupManager::setAdmin(int group_id,int handler_uid,int target_id,bool admin) {
  std::lock_guard<std::mutex> lock(mtx_);
  auto& mem = members_[group_id];
  if(!mem.count(handler_uid) || mem[handler_uid].permission != 0) return -1; // 仅群主有权限
  if(!mem.count(target_id)) return -2;
  if(admin) {
    mem[target_id].permission = 1;
  } else {
    mem[target_id].permission = 2;
  }
  save(GROUPDATA);
  return 0;
}
// 消息免打扰设置
int GroupManager::setRemind(int group_id,int uid,int remind) {
  std::lock_guard<std::mutex> lock(mtx_);
  auto& mem = members_[group_id];
  if(!mem.count(uid)) return -1;
  mem[uid].remind = remind;
  save(GROUPDATA);
  return 0;
}
// 查询相关
GroupInfo GroupManager::getGroupInfo(int group_id) {
  std::lock_guard<std::mutex> lock(mtx_);
  auto it = groups_.find(group_id);
  if(it != groups_.end()) {
    return it->second;
  }
  return {0};
}

std::vector<int> GroupManager::listJoinRequests(int group_id) {
  std::lock_guard<std::mutex> lock(mtx_);
  std::vector<int> res;
  auto it = join_requests_.find(group_id);
  if(it == join_requests_.end()) return res;
  for(int uid : it->second) res.push_back(uid);
  return res;
}

std::vector<GroupMember> GroupManager::getMembers(int group_id) {
  std::lock_guard<std::mutex> lock(mtx_);
  std::vector<GroupMember> res;
  auto it = members_.find(group_id);
  if(it != members_.end()) {
    for(auto& [uid, info] : it->second) res.push_back(info);
  }
  return res;
}

std::vector<int> GroupManager::getUserGroup(int uid) { // 获取某用户所在的所有群id
  std::lock_guard<std::mutex> lock(mtx_);
  std::vector<int> res;
  for(auto& [gid, memMap] : members_) {
    if(memMap.count(uid)) res.push_back(gid);
  }
  return res;
}

bool GroupManager::isMember(int group_id,int uid) {
  std::lock_guard<std::mutex> lock(mtx_);
  auto it = members_.find(group_id);
  if(it != members_.end() && it->second.count(uid) > 0) {
    return true;
  }
  return false;
}

int GroupManager::getPermission(int group_id,int uid) { // 查询某用户在群中的权限
  std::lock_guard<std::mutex> lock(mtx_);
  auto it = members_.find(group_id);
  if(it != members_.end()) {
    auto iter = it->second.find(uid);
    if(iter != it->second.end()) return iter->second.permission;
  }
  return -1; // 非成员
}

uint64_t GroupManager::getJoinTime(int group_id,int uid) {
  std::lock_guard<std::mutex> lock(mtx_);
  auto it = members_.find(group_id);
  if(it != members_.end()) {
    auto iter = it->second.find(uid);
    if(iter != it->second.end()) return iter->second.join_time;
  }
  return 0; // 非成员
}

int GroupManager::ifRemind(int group_id,int uid) {
  std::lock_guard<std::mutex> lock(mtx_);
  auto it = members_.find(group_id);
  if(it != members_.end()) {
    auto iter = it->second.find(uid);
    if(iter != it->second.end()) return iter->second.remind;
  }
  return -1;
}

bool GroupManager::load(const std::string& path) {
  std::lock_guard<std::mutex> lock(mtx_);

  return true;
}

bool GroupManager::save(const std::string& path) {
  std::lock_guard<std::mutex> lock(mtx_);

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
  if(msg.chat_type == "private") {
    push["action"] = "private_chat";
  }
  if(msg.chat_type == "group") {
    push["action"] = "group_chat";
  }
  push["data"] = msg;
  if(sock->sendMsg(push.dump()) != NetResult::OK) {
    return -1;
  }
  return 0;
}

bool SessionManager::sendPushTo(int to_uid,nlohmann::json& push) {
  auto sock = getSock(to_uid);
  if(!sock) return -1;
  push["msg_type"] = "push";
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
