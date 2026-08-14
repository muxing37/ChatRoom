#include "manager.h"
#include "hash.h"
#include <algorithm>
#include <cstdio>
#include <cerrno>
#include <sstream>
#include <iomanip>
#include <filesystem>

const std::string SAVEPATH = "./data";
const std::string USRDATA = SAVEPATH + "/usrdata.json";
const std::string FRIENDDATA = SAVEPATH + "/frienddata.json";
const std::string PCHATDATA = SAVEPATH + "/pchatdata.json";
const std::string GROUPDATA = SAVEPATH + "/groupdata.json";

// 未完成上传的过期时间，超过该时长仍未完成的 .uploading 视为放弃，服务端重启时删除
constexpr uint64_t UPLOAD_STALE_MS = 24ULL * 3600 * 1000; // 24 小时

std::string privateId(int uid1, int uid2) {
  if(uid1 > uid2) std::swap(uid1, uid2);
  return "private_" + std::to_string(uid1) + "_" + std::to_string(uid2);
}
std::string groupId(int groupId) {
  return "group_" + std::to_string(groupId);
}
// 用户管理相关
bool UsrManager::verify(int uid,const std::string& password) {
  auto uiter = uid_map.find(uid);
  if(uiter == uid_map.end()) {
    return false;
  }
  if(uiter->second.password_hash != passwordHash(password,uiter->second.salt)) {
    return false;
  }
  return true;
}

bool UsrManager::regis(const std::string& username,const std::string& password,int uid) {
  std::lock_guard<std::mutex> lock(mtx_);

  if(name_map.count(username)) {
    return false;
  }

  Account auth;
  auth.uid = uid;
  auth.username = username;
  auth.salt = getSalt();
  auth.password_hash = passwordHash(password,auth.salt);

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
  if(!verify(uid,password)) {
    return false;
  }
  out_uid = uid;
  return true;
}

bool UsrManager::delUsr(int uid,const std::string& password) {
  std::lock_guard<std::mutex> lock(mtx_);
  if(!verify(uid,password)) {
    return false;
  }
  auto it = uid_map.find(uid);
  if(it == uid_map.end()) return false;
  name_map.erase(it->second.username);
  uid_map.erase(it);
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
    // auth.password = item["password"];
    auth.password_hash = item["password_hash"];
    auth.salt = item["salt"];

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
      {"salt",auth.salt},
      {"password_hash",auth.password_hash}
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

std::optional<User> UsrManager::getUser(int uid) {
  std::lock_guard<std::mutex> lock(mtx_);

  auto iter = uid_map.find(uid);
  if(iter == uid_map.end()) {
    return std::nullopt;
  }
  User usr;
  usr.uid = iter->second.uid;
  usr.username = iter->second.username;
  usr.online = false;
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
  if(FriendManager::isFriend_(from_uid,to_uid)) return 2; //已经是好友
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
  std::lock_guard<std::mutex> lock(mtx_);
  auto it = friends_.find(uid1);
  if(it == friends_.end()) {
    return false;
  }
  return it->second.count(uid2) > 0;
}

bool FriendManager::isFriend_(int uid1,int uid2) {
  auto it = friends_.find(uid1);
  if(it == friends_.end()) {
    return false;
  }
  return it->second.count(uid2) > 0;
}

bool FriendManager::removeUsr(int uid) {
  std::lock_guard<std::mutex> lock(mtx_);

  friends_.erase(uid);
  requests_.erase(uid);
  block_.erase(uid);

  for(auto& [u,set] : friends_) set.erase(uid);
  for(auto& [u,set] : requests_) set.erase(uid);
  for(auto& [u,set] : block_) set.erase(uid);

  save(FRIENDDATA);
  return true;
}

// 好友屏蔽/拉黑相关
int FriendManager::block(int uid,int target) {
  std::lock_guard lock(mtx_);
  if(!isFriend_(uid,target)) return -1; // 只能屏蔽好友
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
  std::string cvs_id;
  if(msg.chat_type == "group") {
    cvs_id = groupId(msg.target_id);
  } else {
    cvs_id = privateId(msg.from_uid,msg.target_id);
  }
  history_[cvs_id].push_back(msg);
  save(PCHATDATA);
  return 0;
}

std::vector<Message> MessageManager::getMessagesByTime(int uid,uint64_t time) {
  std::lock_guard<std::mutex> lock(mtx_);
  std::vector<Message> result;
  for(auto& [key, msgs] : history_) {
    bool group_key = (key.rfind("group_",0) == 0);
    for(auto& msg : msgs) {
      if(group_key) {
        if(msg.time > time) result.push_back(msg);
      } else {
        if((msg.from_uid == uid || msg.target_id == uid) && msg.time > time) {
          result.push_back(msg);
        }
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
    bool group_key = (key.rfind("group_",0) == 0);
    for(const auto& msg : msgs) {
      if(group_key || msg.from_uid == uid || msg.target_id == uid) {
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
  std::lock_guard<std::mutex> lock(mtx_);
  for(auto it = history_.begin(); it != history_.end(); ) {
    auto& msgs = it->second;
    bool group_key = (it->first.rfind("group_",0) == 0);
    msgs.erase(std::remove_if(msgs.begin(), msgs.end(),
      [uid,group_key](const Message& m) {
        // if(group_key) return m.from_uid == uid;
        return m.from_uid == uid || m.target_id == uid;
      }), msgs.end());
    if(msgs.empty()) it = history_.erase(it);
    else it++;
  }
  save(PCHATDATA);
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

int GroupManager::removeUser(int uid) {
  std::lock_guard<std::mutex> lock(mtx_);
  std::vector<int> owned;
  for(auto& [gid,info] : groups_) {
    if(info.owner_uid == uid) owned.push_back(gid);
  }
  for(int gid : owned) {
    groups_.erase(gid);
    members_.erase(gid);
    join_requests_.erase(gid);
  }
  for(auto& [gid,memMap] : members_) {
    memMap.erase(uid);
  }
  for(auto& [gid,reqs] : join_requests_) {
    reqs.erase(uid);
  }
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
  std::ifstream ifs(path);
  if(!ifs.is_open()) return false;
  nlohmann::json j;
  try {
    ifs >> j;
  } catch(...) {
    return false;
  }
  groups_.clear();
  members_.clear();
  join_requests_.clear();

  if(j.contains("groups")) {
    for(auto& item : j["groups"]) {
      GroupInfo info;
      from_json(item, info);
      groups_[info.group_id] = info;
    }
  }
  if(j.contains("members")) {
    for(auto& [gid_s, arr] : j["members"].items()) {
      int gid = std::stoi(gid_s);
      for(auto& item : arr) {
        GroupMember m;
        from_json(item, m);
        members_[gid][m.uid] = m;
      }
    }
  }
  if(j.contains("join_requests")) {
    for(auto& [gid_s, arr] : j["join_requests"].items()) {
      int gid = std::stoi(gid_s);
      for(auto& x : arr) {
        join_requests_[gid].insert(x.get<int>());
      }
    }
  }
  if(j.contains("next_group_id")) {
    next_group_id_.store(j["next_group_id"].get<int>());
  } else {
    int max_gid = 100000;
    for(auto& [gid, info] : groups_) if(gid > max_gid) max_gid = gid;
    next_group_id_.store(max_gid + 1);
  }
  return true;
}

bool GroupManager::save(const std::string& path) {
  nlohmann::json j;
  j["groups"] = nlohmann::json::array();
  for(auto& [gid, info] : groups_) {
    nlohmann::json item;
    to_json(item, info);
    j["groups"].push_back(item);
  }
  for(auto& [gid, memMap] : members_) {
    nlohmann::json arr = nlohmann::json::array();
    for(auto& [uid, m] : memMap) {
      nlohmann::json item;
      to_json(item, m);
      arr.push_back(item);
    }
    j["members"][std::to_string(gid)] = arr;
  }
  for(auto& [gid, reqs] : join_requests_) {
    nlohmann::json arr = nlohmann::json::array();
    for(int uid : reqs) arr.push_back(uid);
    j["join_requests"][std::to_string(gid)] = arr;
  }
  j["next_group_id"] = next_group_id_.load();
  std::ofstream ofs(path);
  if(!ofs.is_open()) return false;
  ofs << std::setw(4) << j;
  return true;
}

void SessionManager::bindUser(int user_id,std::shared_ptr<Connection> conn) {
  std::lock_guard<std::mutex> lock(mtx_);
  user_to_conn_[user_id] = std::move(conn);
}

void SessionManager::unbindUser(int user_id) {
  std::lock_guard<std::mutex> lock(mtx_);
  user_to_conn_.erase(user_id);
}

bool SessionManager::isOnline(int uid) {
  std::lock_guard lock(mtx_);
  return user_to_conn_.count(uid);
}

bool SessionManager::sendChatTo(int to_uid,const Message& msg) {
  std::shared_ptr<Connection> conn;
  {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = user_to_conn_.find(to_uid);
    if(it == user_to_conn_.end()) return false;
    conn = it->second;
  }
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
  std::string p = push.dump();

  auto loop = conn->loop();
  loop->runInLoop([conn,p] { conn->send(p); });
  return true;
}

bool SessionManager::sendPushTo(int to_uid,nlohmann::json& push) {
  std::shared_ptr<Connection> conn;
  {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = user_to_conn_.find(to_uid);
    if(it == user_to_conn_.end()) return false;
    conn = it->second;
  }
  push["msg_type"] = "push";
  std::string p = push.dump();
  auto loop = conn->loop();
  loop->runInLoop([conn, p] { conn->send(p); });
  return true;
}

std::shared_ptr<Connection> SessionManager::getConn(int uid) {
  std::lock_guard<std::mutex> lock(mtx_);
  auto it = user_to_conn_.find(uid);
  if(it == user_to_conn_.end()) return nullptr;
  return it->second;
}

FileManager::FileManager(const std::string& meta_path,const std::string& storage_dir)
  : meta_path_(meta_path), storage_dir_(storage_dir) {
  if(storage_dir_.empty() || storage_dir_.back() != '/') {
    storage_dir_.push_back('/');
  }
}

bool FileManager::init() {
  std::lock_guard<std::mutex> lock(mtx_);
  std::error_code ec;
  std::filesystem::create_directories(storage_dir_,ec);
  std::filesystem::create_directories(storage_dir_ + "tmp",ec);
  loadMeta();
  cleanupOrphans();
  return true;
}

std::string FileManager::generateFileId() {
  uint64_t ms = now_ms();
  uint64_t n = id_counter_.fetch_add(1);
  std::ostringstream oss;
  oss << std::hex << ms << std::setw(4) << std::setfill('0') << n;
  return oss.str();
}

std::string FileManager::makeStoragePath(const std::string& file_id) {
  std::string p1 = file_id.substr(0,2);
  std::string p2 = file_id.substr(2,2);
  return p1 + "/" + p2 + "/" + file_id;
}

std::string FileManager::getFullPath(const std::string& storage_path) {
  return storage_dir_ + storage_path;
}

std::string FileManager::getPartPath(const std::string& file_id) {
  return storage_dir_ + "tmp/" + file_id + ".uploading";
}

bool FileManager::addFileMeta(const FileMeta& meta) {
  std::lock_guard<std::mutex> lock(mtx_);
  if(metas_.count(meta.file_id)) return false;
  metas_[meta.file_id] = meta;
  return saveMeta();
}

bool FileManager::updateFileMeta(const FileMeta& meta) {
  std::lock_guard<std::mutex> lock(mtx_);
  auto it = metas_.find(meta.file_id);
  if(it == metas_.end()) return false;
  it->second = meta;
  saveMeta();
  return true;
}

std::optional<FileMeta> FileManager::getFileMeta(const std::string& file_id) {
  std::lock_guard<std::mutex> lock(mtx_);
  auto it = metas_.find(file_id);
  if(it == metas_.end()) return std::nullopt;
  return it->second;
}

std::optional<FileMeta> FileManager::findIncomplete(const std::string& hash,int uid,const std::string& chat_type,int target_id) {
  std::lock_guard<std::mutex> lock(mtx_);
  if(hash.empty()) return std::nullopt;
  for(auto& [id,m] : metas_) {
    if(m.status == 0 && m.uploader_uid == uid && m.file_hash == hash && m.chat_type == chat_type && m.target_id == target_id) {
      return m;
    }
  }
  return std::nullopt;
}

std::vector<FileMeta> FileManager::getAllMeta() {
  std::lock_guard<std::mutex> lock(mtx_);
  std::vector<FileMeta> res;
  res.reserve(metas_.size());
  for(auto& [id,m] : metas_) res.push_back(m);
  return res;
}

bool FileManager::createPart(const std::string& file_id) {
  std::lock_guard<std::mutex> lock(mtx_);
  std::error_code ec;
  std::filesystem::create_directories(storage_dir_ + "tmp",ec);
  std::string path = getPartPath(file_id);
  int fd = open(path.c_str(),O_WRONLY | O_CREAT,0644);
  if(fd < 0) return false;
  close(fd);
  return true;
}

bool FileManager::writePart(const std::string& file_id,uint64_t offset,const char* data,size_t len) {
  std::lock_guard<std::mutex> lock(mtx_);
  auto it = metas_.find(file_id);
  if(it == metas_.end()) return false;
  std::string path = getPartPath(file_id);
  int fd = open(path.c_str(),O_WRONLY);
  if(fd < 0) return false;
  if(lseek(fd,(off_t)offset,SEEK_SET) == (off_t)-1) {
    close(fd);
    return false;
  }
  size_t total = 0;
  while(total < len) {
    ssize_t n = write(fd,data+total,len-total);
    if(n < 0) {
      if(errno == EINTR) continue;
      close(fd);
      return false;
    }
    total += (size_t)n;
  }
  close(fd);
  it->second.received = offset + len;
  return true;
}

uint64_t FileManager::getPartSize(const std::string& file_id) {
  std::lock_guard<std::mutex> lock(mtx_);
  auto it = metas_.find(file_id);
  if(it == metas_.end()) return 0;
  return it->second.received;
}

bool FileManager::finishUpload(const std::string& file_id) {
  std::lock_guard<std::mutex> lock(mtx_);
  auto it = metas_.find(file_id);
  if(it == metas_.end()) return false;
  FileMeta& meta = it->second;
  if(meta.status != 0) return false; // 已完成或未知状态
  if(meta.received != (uint64_t)meta.file_size) return false; // 未传完

  std::string part = getPartPath(file_id);
  std::string final = meta.storage_path;
  auto pos = final.find_last_of('/');
  std::string parent;
  if(pos == std::string::npos) {
    parent = storage_dir_;
  } else {
    parent = storage_dir_ + final.substr(0,pos);
  }
  std::error_code ec;
  std::filesystem::create_directories(parent,ec);

  std::string final_path = getFullPath(final);
  if(std::rename(part.c_str(),final_path.c_str()) != 0) return false;

  meta.status = 1;
  return saveMeta();
}

bool FileManager::removePart(const std::string& file_id) {
  std::lock_guard<std::mutex> lock(mtx_);
  std::string part = getPartPath(file_id);
  unlink(part.c_str());
  metas_.erase(file_id);
  return saveMeta();
}

bool FileManager::saveMeta() {
  nlohmann::json j;
  j["files"] = nlohmann::json::array();
  for(auto& [id,m] : metas_) {
    nlohmann::json item;
    to_json(item,m);
    j["files"].push_back(item);
  }
  std::ofstream ofs(meta_path_);
  if(!ofs.is_open()) return false;
  ofs << std::setw(4) << j;
  return true;
}

bool FileManager::loadMeta() {
  std::ifstream ifs(meta_path_);
  if(!ifs.is_open()) return false;
  nlohmann::json j;
  try {
    ifs >> j;
  } catch(...) {
    return false;
  }
  metas_.clear();
  if(j.contains("files")) {
    for(auto& item : j["files"]) {
      FileMeta m;
      from_json(item,m);
      metas_[m.file_id] = m;
    }
  }
  id_counter_.store(metas_.size());
  return true;
}

void FileManager::cleanupOrphans() {
  std::error_code ec;
  for(auto& entry : std::filesystem::directory_iterator(storage_dir_ + "tmp",ec)) {
    if(ec) break;
    if(entry.path().extension() != ".uploading") continue;
    std::string id = entry.path().stem().string();
    if(metas_.find(id) == metas_.end()) {
      std::filesystem::remove(entry.path(),ec);
    }
  }
  bool changed = false;
  uint64_t now = now_ms();
  for(auto it = metas_.begin(); it != metas_.end(); ) {
    if(it->second.status == 0 && now - it->second.upload_time > UPLOAD_STALE_MS) {
      std::string part = getPartPath(it->first);
      unlink(part.c_str());
      it = metas_.erase(it);
      changed = true;
    } else {
      it++;
    }
  }
  if(changed) saveMeta();
}
