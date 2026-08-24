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

// 用户管理相关
bool UsrManager::verify(int uid,const std::string& password) {
  Account auth;
  {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = uid_map_.find(uid);
    if(it == uid_map_.end()) return false;
    auth = it->second;
  }
  return auth.password_hash == passwordHash(password,auth.salt);
}

bool UsrManager::regis(const std::string& username,const std::string& password,int uid) {
  Account auth;
  auth.uid = uid;
  auth.username = username;
  auth.last_logout_time = 0;
  auth.salt = getSalt();
  auth.password_hash = passwordHash(password,auth.salt);
  {
    std::lock_guard<std::mutex> lock(mtx_);
    if(name_map_.count(username)) return false;
    if(!insertUser(auth)) return false;
    uid_map_[uid] = auth;
    name_map_[username] = uid;
  }
  return true;
}

bool UsrManager::login(const std::string& username,const std::string& password,int& out_uid) {
  Account auth;
  {
    std::lock_guard<std::mutex> lock(mtx_);
    auto iter = name_map_.find(username);
    if(iter == name_map_.end()) return false;
    auto uiter = uid_map_.find(iter->second);
    if(uiter == uid_map_.end()) return false;
    auth = uiter->second;
  }
  if(auth.password_hash != passwordHash(password, auth.salt)) return false;
  out_uid = auth.uid;
  return true;
}

bool UsrManager::delUsr(int uid,const std::string& password) {
  if(!verify(uid,password)) return false;
  std::lock_guard<std::mutex> lock(mtx_);
  auto it = uid_map_.find(uid);
  if(it == uid_map_.end()) return false;
  if(!deleteUser(uid)) return false;
  name_map_.erase(it->second.username);
  uid_map_.erase(it);
  return true;
}

int UsrManager::getMaxUid() {
  int max_uid = 10000;

  for(auto& [uid, auth] : uid_map_) {
    if(uid > max_uid) max_uid = uid;
  }

  return max_uid;
}

bool UsrManager::isExist(const std::string& username) {
  std::lock_guard<std::mutex> lock(mtx_);
  if(name_map_.count(username)) {
    return true;
  }
  return false;
}

std::optional<User> UsrManager::getUser(int uid) {
  std::lock_guard<std::mutex> lock(mtx_);

  auto iter = uid_map_.find(uid);
  if(iter == uid_map_.end()) {
    return std::nullopt;
  }
  User usr;
  usr.uid = iter->second.uid;
  usr.username = iter->second.username;
  usr.online = false;
  return usr;
}

uint64_t UsrManager::getLastLogout(int uid) {
  std::lock_guard<std::mutex> lock(mtx_);
  auto it = uid_map_.find(uid);
  if(it != uid_map_.end()) {
    return it->second.last_logout_time;
  }
  return 0;
}

bool UsrManager::load() {
  auto all = selectAllUsers();
  if(db_ && all.empty() && !db_->isConnected()) return false;
  std::lock_guard<std::mutex> lock(mtx_);
  uid_map_.clear();
  name_map_.clear();
  for(auto& a : all) {
    uid_map_[a.uid] = a;
    name_map_[a.username] = a.uid;
  }
  return true;
}

bool UsrManager::insertUser(const Account& a) {
  if(!db_) return false;
  std::string sql = 
    "INSERT INTO users (uid,username,password_hash,salt,last_logout_time) VALUES (" +
    std::to_string(a.uid) + ",'" + db_->escape(a.username) + "','" +
    a.password_hash + "','" + a.salt + "',0)";
  if(!db_->execute(sql)) {
    LOG(ERROR) << "insertUser failed";
    return false;
  }
  return true;
}

bool UsrManager::deleteUser(int uid) {
  if(!db_) return false;
  return db_->execute("DELETE FROM users WHERE uid=" + std::to_string(uid));
}

void UsrManager::updateLastLogout(int uid,uint64_t t) {
  if(!db_) return;
  db_->execute("UPDATE users SET last_logout_time=" + std::to_string(t) + " WHERE uid=" + std::to_string(uid));
  std::lock_guard<std::mutex> lock(mtx_);
  auto it = uid_map_.find(uid);
  if(it != uid_map_.end()) it->second.last_logout_time = t;
}

std::vector<Account> UsrManager::selectAllUsers() {
  std::vector<Account> out;
  if(!db_) return out;
  MYSQL_RES* res = db_->query("SELECT uid,username,password_hash,salt,last_logout_time FROM users");
  if(!res) return out;
  MYSQL_ROW row;
  while((row = mysql_fetch_row(res))) {
    Account a;
    a.uid = std::stoi(row[0]);
    a.username = row[1] ? row[1] : "";
    a.password_hash = row[2] ? row[2] : "";
    a.salt = row[3] ? row[3] : "";
    a.last_logout_time = row[4] ? std::strtoull(row[4],nullptr,10) : 0;
    out.push_back(a);
  }
  mysql_free_result(res);
  return out;
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
    res.push_back(f);
  }
  return res;
}

int FriendManager::del(int uid1,int uid2) {
  std::lock_guard<std::mutex> lock(mtx_);
  if(!deleteFriendPair(uid1,uid2)) return -1;
  deleteFriendMessages(uid1,uid2); // 删除双方私聊消息
  auto it1 = friends_.find(uid1);
  if(it1 != friends_.end()) it1->second.erase(uid2);
  auto it2 = friends_.find(uid2);
  if(it2 != friends_.end()) it2->second.erase(uid1);

  return 0;
}

int FriendManager::request(int from_uid,int to_uid) {
  if(from_uid == to_uid) return 1; //不可添加自己为好友
  std::lock_guard<std::mutex> lock(mtx_);
  if(FriendManager::isFriend_(from_uid,to_uid)) return 2; //已经是好友
  if(requests_[to_uid].count(from_uid)) {
    return 3; //已发送过申请
  }
  if(!insertFriendRequest(from_uid,to_uid)) return -1;
  requests_[to_uid].insert(from_uid);
  return 0;
}

int FriendManager::agree(int uid1,int uid2) {
  std::lock_guard<std::mutex> lock(mtx_);
  if(db_) {
    db_->begin();
    if(!(insertFriend(uid1,uid2) && insertFriend(uid2,uid1) && deleteFriendRequest(uid1,uid2))) {
      db_->rollback();
      return -1;
    }
    db_->commit();
  }
  friends_[uid1].insert(uid2);
  friends_[uid2].insert(uid1);
  if(requests_[uid1].count(uid2)) requests_[uid1].erase(uid2);
  if(requests_[uid2].count(uid1)) requests_[uid2].erase(uid1);
  
  return true;
}

int FriendManager::reject(int uid1,int uid2) {
  std::lock_guard<std::mutex> lock(mtx_);
  if(db_) {
    db_->begin();
    if(!deleteFriendRequest(uid1,uid2)) {
      db_->rollback();
      return -1;
    }
    db_->commit();
  }
  if(requests_[uid1].count(uid2)) requests_[uid1].erase(uid2);
  if(requests_[uid2].count(uid1)) requests_[uid2].erase(uid1);

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
  if(!deleteUserAll(uid)) return false;
  friends_.erase(uid);
  requests_.erase(uid);
  block_.erase(uid);

  for(auto& [u,set] : friends_) set.erase(uid);
  for(auto& [u,set] : requests_) set.erase(uid);
  for(auto& [u,set] : block_) set.erase(uid);


  return true;
}

// 好友屏蔽/拉黑相关
int FriendManager::block(int uid,int target) {
  std::lock_guard lock(mtx_);
  if(!isFriend_(uid,target)) return -1; // 只能屏蔽好友
  if(!insertBlock(uid,target)) return -1;
  block_[uid].insert(target);
  return 0;
}

int FriendManager::unblock(int uid,int target) {
  std::lock_guard lock(mtx_);
  if(!deleteBlock(uid,target)) return -1;
  block_[uid].erase(target);
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

bool FriendManager::load() {
  std::lock_guard<std::mutex> lock(mtx_);
  friends_.clear();
  requests_.clear();
  block_.clear();
  loadFriends();
  loadRequests();
  loadBlocks();
  return true;
}

bool FriendManager::insertFriend(int a,int b) {
  if(!db_) return false;
  return db_->execute("INSERT INTO friends (uid1,uid2) VALUES (" +
    std::to_string(a) + "," + std::to_string(b) + ")");
}

bool FriendManager::deleteFriendPair(int a,int b) {
  if(!db_) return false;
  return db_->execute("DELETE FROM friends WHERE (uid1=" + std::to_string(a) +
    " AND uid2=" + std::to_string(b) + ") OR (uid1=" + std::to_string(b) +
    " AND uid2=" + std::to_string(a) + ")");
}

bool FriendManager::deleteFriendMessages(int a,int b) {
  if(!db_) return false;
  return db_->execute("DELETE FROM messages WHERE chat_type='private' AND ((from_uid=" + std::to_string(a) +
    " AND target_id=" + std::to_string(b) + ") OR (from_uid=" + std::to_string(b) +
    " AND target_id=" + std::to_string(a) + "))");
}

bool FriendManager::insertFriendRequest(int from,int to) {
  if(!db_) return false;
  return db_->execute("INSERT INTO friend_requests (from_uid,to_uid,apply_time) VALUES (" +
    std::to_string(from) + "," + std::to_string(to) + "," + std::to_string(now_ms()) + ")");
}

bool FriendManager::deleteFriendRequest(int a,int b) {
  if(!db_) return false;
  return db_->execute("DELETE FROM friend_requests WHERE (from_uid=" + std::to_string(a) +
    " AND to_uid=" + std::to_string(b) + ") OR (from_uid=" + std::to_string(b) +
    " AND to_uid=" + std::to_string(a) + ")");
}

bool FriendManager::insertBlock(int uid,int target) {
  if(!db_) return false;
  return db_->execute("INSERT INTO blocks (uid,block_uid) VALUES (" +
    std::to_string(uid) + "," + std::to_string(target) + ")");
}

bool FriendManager::deleteBlock(int uid,int target) {
  if(!db_) return false;
  return db_->execute("DELETE FROM blocks WHERE uid=" + std::to_string(uid) +
    " AND block_uid=" + std::to_string(target));
}

bool FriendManager::deleteUserAll(int uid) {
  if(!db_) return false;
  db_->execute("DELETE FROM friends WHERE uid1=" + std::to_string(uid) + " OR uid2=" + std::to_string(uid));
  db_->execute("DELETE FROM friend_requests WHERE from_uid=" + std::to_string(uid) + " OR to_uid=" + std::to_string(uid));
  db_->execute("DELETE FROM blocks WHERE uid=" + std::to_string(uid) + " OR block_uid=" + std::to_string(uid));
  return true;
}

void FriendManager::loadFriends() {
  if(!db_) return;
  MYSQL_RES* res = db_->query("SELECT uid1,uid2 FROM friends");
  if(!res) return;
  MYSQL_ROW row;
  while((row = mysql_fetch_row(res))) {
    int a = std::atoi(row[0]), b = std::atoi(row[1]);
    friends_[a].insert(b);
  }
  mysql_free_result(res);
}

void FriendManager::loadRequests() {
  if(!db_) return;
  MYSQL_RES* res = db_->query("SELECT from_uid,to_uid FROM friend_requests");
  if(!res) return;
  MYSQL_ROW row;
  while((row = mysql_fetch_row(res))) {
    int a = std::atoi(row[0]), b = std::atoi(row[1]);
    requests_[a].insert(b);
  }
  mysql_free_result(res);
}

void FriendManager::loadBlocks() {
  if(!db_) return;
  MYSQL_RES* res = db_->query("SELECT uid,block_uid FROM blocks");
  if(!res) return;
  MYSQL_ROW row;
  while((row = mysql_fetch_row(res))) {
    int a = std::atoi(row[0]), b = std::atoi(row[1]);
    block_[a].insert(b);
  }
  mysql_free_result(res);
}

std::vector<Message> MessageManager::getMessagesByTime(int uid,uint64_t time) {
  return queryMessages(uid,time);
}

std::vector<Message> MessageManager::getAllMsg(int uid) {
  return queryMessages(uid,0);
}

std::string MessageManager::getMsgId() {
  uint64_t ms = now_ms();
  uint64_t i = count++;
  return std::to_string(ms) + "_" + std::to_string(i);
}

bool MessageManager::removeGroupMessages(int gid) {
  // std::lock_guard<std::mutex> lock(mtx_);
  if(!db_) return false;
  return db_->execute("DELETE FROM messages WHERE chat_type='group' AND target_id=" + std::to_string(gid));
}

bool MessageManager::removeUsr(int uid) {
  if(!db_) return false;
  db_->execute("DELETE FROM messages WHERE chat_type='private' AND (from_uid=" + std::to_string(uid) +" OR target_id=" + std::to_string(uid) + ")");
  return true;
}

std::vector<Message> MessageManager::queryMessages(int uid,uint64_t time) {
  std::vector<Message> result;
  if(!db_) return result;
  std::string sql =
    "SELECT message_id,type,chat_type,from_uid,target_id,content,msg_time,status FROM messages WHERE ("
      "(chat_type='private' AND (from_uid=" + std::to_string(uid) +
      " OR target_id=" + std::to_string(uid) + "))"
      " OR "
      "(chat_type='group'"
        " AND target_id IN (SELECT group_id FROM group_members WHERE uid=" + std::to_string(uid) + ")"
        " AND msg_time >= (SELECT join_time FROM group_members WHERE uid=" + std::to_string(uid) + " AND group_id=target_id)"
      ")"
    ") AND msg_time >=" + std::to_string(time) +
    " ORDER BY msg_time";
  MYSQL_RES* res = db_->query(sql);
  if(!res) return result;
  MYSQL_ROW row;
  while((row = mysql_fetch_row(res))) {
    result.push_back(rowToMessage(row));
  }
  mysql_free_result(res);
  return result;
}

Message MessageManager::rowToMessage(MYSQL_ROW row) {
  Message m;
  m.message_id = row[0] ? row[0] : "";
  m.type = row[1] ? row[1] : "";
  m.chat_type = row[2] ? row[2] : "";
  m.from_uid = std::stoi(row[3]);
  m.target_id = std::stoi(row[4]);
  m.content = row[5] ? row[5] : "";
  m.time = std::strtoull(row[6],nullptr,10);
  m.status = std::stoi(row[7]);
  return m;
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
  GroupMember owner_info {
    owner.uid,
    owner.username,
    0,
    info.create_time,
    0
  };
  if(db_) {
    db_->begin();
    if(!(insertGroup(info) && insertGroupMember(gid,owner_info))) {
      db_->rollback();
      return -1;
    }
    db_->commit();
  }
  groups_[gid] = info;
  members_[gid][owner.uid] = owner_info;
  out_gid = gid;
  return 0;
}

int GroupManager::disbandGroup(int group_id,int uid) {
  std::lock_guard<std::mutex> lock(mtx_);
  auto it = groups_.find(group_id);
  if(it == groups_.end()) return -1; // 群不存在
  if(uid != it->second.owner_uid) return -2; // 非群主
  if(!deleteGroup(group_id)) return -3;
  groups_.erase(it);
  members_.erase(group_id);
  join_requests_.erase(group_id);
  return 0;
}

// 成员管理
int GroupManager::joinRequest(int group_id,int apply_uid) {
  std::lock_guard<std::mutex> lock(mtx_);
  if(groups_.find(group_id) == groups_.end()) return -1; // 群不存在
  if(members_[group_id].count(apply_uid)) return -2; // 已是成员
  if(join_requests_[group_id].count(apply_uid)) return -3; // 已申请
  if(!insertJoinRequest(group_id,apply_uid)) return -4;
  join_requests_[group_id].insert(apply_uid);
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
    if(db_) {
      db_->begin();
      if(!(insertGroupMember(group_id,info) && deleteJoinRequest(group_id,target_usr.uid))) {
        db_->rollback();
        return -1;
      }
      db_->commit();
    }
    mem[target_usr.uid] = info;
    req.erase(target_usr.uid);
  } else {
    if(!deleteJoinRequest(group_id,target_usr.uid)) return -1;
    req.erase(target_usr.uid);
  }
  return 0;
}

int GroupManager::leaveGroup(int group_id,int uid) {
  std::lock_guard<std::mutex> lock(mtx_);
  auto& mem = members_[group_id];
  if(!mem.count(uid)) return -1;
  if(mem[uid].permission == 0) return -2;  // 群主不能直接退出，应先解散
  if(db_) {
    db_->begin();
    if(!deleteGroupMember(group_id,uid)) {
      db_->rollback();
      return -1;
    }
    db_->commit();
  }
  mem.erase(uid);
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
  if(handler_uid == target_id) return -4; // 不能踢自己
  if(db_) {
    db_->begin();
    if(!deleteGroupMember(group_id,target_id)) {
      db_->rollback();
      return -1;
    }
    db_->commit();
  }
  mem.erase(target_id);
  return 0;
}

int GroupManager::removeUser(int uid,std::vector<int>* disbanded) {
  std::lock_guard<std::mutex> lock(mtx_);
  std::vector<int> owned;
  for(auto& [gid,info] : groups_) {
    if(info.owner_uid == uid) owned.push_back(gid);
  }
  for(int gid : owned) {
    deleteGroup(gid);
    groups_.erase(gid);
    members_.erase(gid);
    join_requests_.erase(gid);
    if(disbanded) disbanded->push_back(gid);
  }
  if(db_) {
    db_->execute("DELETE FROM group_members WHERE uid=" + std::to_string(uid));
    db_->execute("DELETE FROM group_join_requests WHERE uid=" + std::to_string(uid));
  }
  for(auto& [gid,memMap] : members_) {
    memMap.erase(uid);
  }
  for(auto& [gid,reqs] : join_requests_) {
    reqs.erase(uid);
  }
  
  return 0;
}
// 管理员设置
int GroupManager::setAdmin(int group_id,int handler_uid,int target_id,bool admin) {
  std::lock_guard<std::mutex> lock(mtx_);
  auto& mem = members_[group_id];
  if(handler_uid == target_id) return -3; // 不能把自己设置为管理员
  if(!mem.count(handler_uid) || mem[handler_uid].permission != 0) return -1; // 仅群主有权限
  if(!mem.count(target_id)) return -2;
  if(admin) {
    if(db_) {
      db_->begin();
      if(!updateMemberPermission(group_id,target_id,1)) {
        db_->rollback();
        return -1;
      }
      db_->commit();
    }
    mem[target_id].permission = 1;
  } else {
    if(db_) {
      db_->begin();
      if(!updateMemberPermission(group_id,target_id,2)) {
        db_->rollback();
        return -1;
      }
      db_->commit();
    }
    mem[target_id].permission = 2;
  }
  return 0;
}
// 消息免打扰设置
int GroupManager::setRemind(int group_id,int uid,int remind) {
  std::lock_guard<std::mutex> lock(mtx_);
  auto& mem = members_[group_id];
  if(!mem.count(uid)) return -1;
  if(db_) {
    if(!updateMemberRemind(group_id,uid,remind)) return -1;
  }
  mem[uid].remind = remind;
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

bool GroupManager::load() {
  std::lock_guard<std::mutex> lock(mtx_);
  groups_.clear();
  members_.clear();
  join_requests_.clear();
  loadGroups();
  loadMembers();
  loadJoinRequests();
  int max_gid = 100000;
  for(auto& [gid,info] : groups_) {
    if(gid > max_gid) max_gid = gid;
  }
  next_group_id_.store(max_gid + 1);
  return true;
}

bool GroupManager::insertGroup(const GroupInfo& g) {
  if(!db_) return false;
  return db_->execute("INSERT INTO chat_groups (group_id,name,owner_uid,create_time) VALUES (" + 
    std::to_string(g.group_id) + ",'" + db_->escape(g.name) + "'," +
    std::to_string(g.owner_uid) + "," + std::to_string(g.create_time) + ")");
}

bool GroupManager::deleteGroup(int gid) {
  if(!db_) return false;
  return db_->execute("DELETE FROM chat_groups WHERE group_id=" + std::to_string(gid));
}

bool GroupManager::insertGroupMember(int gid,const GroupMember& m) {
  if(!db_) return false;
  return db_->execute("INSERT INTO group_members (group_id,uid,permission,join_time,remind) VALUES (" +
    std::to_string(gid) + "," + std::to_string(m.uid) + "," + std::to_string(m.permission) + "," +
    std::to_string(m.join_time) + "," + std::to_string(m.remind) + ")");
}

bool GroupManager::deleteGroupMember(int gid,int uid) {
  if(!db_) return false;
  return db_->execute("DELETE FROM group_members WHERE group_id=" + std::to_string(gid) + " AND uid=" + std::to_string(uid));
}

bool GroupManager::updateMemberPermission(int gid,int uid,int perm) {
  if(!db_) return false;
  return db_->execute("UPDATE group_members SET permission=" + std::to_string(perm) +
    " WHERE group_id=" + std::to_string(gid) + " AND uid=" + std::to_string(uid));
}

bool GroupManager::updateMemberRemind(int gid,int uid,int remind) {
  if(!db_) return false;
  return db_->execute("UPDATE group_members SET remind=" + std::to_string(remind) +
    " WHERE group_id=" + std::to_string(gid) + " AND uid=" + std::to_string(uid));
}

bool GroupManager::insertJoinRequest(int gid,int uid) {
  if(!db_) return false;
  return db_->execute("INSERT INTO group_join_requests (group_id,uid,apply_time) VALUES (" +
    std::to_string(gid) + "," + std::to_string(uid) + "," + std::to_string(now_ms()) + ")");
}

bool GroupManager::deleteJoinRequest(int gid,int uid) {
  if(!db_) return false;
  return db_->execute("DELETE FROM group_join_requests WHERE group_id=" + std::to_string(gid) + " AND uid=" + std::to_string(uid));
}

void GroupManager::loadGroups() {
  if(!db_) return;
  MYSQL_RES* res = db_->query("SELECT group_id,name,owner_uid,create_time FROM chat_groups");
  if(!res) return;
  MYSQL_ROW row;
  while(row = mysql_fetch_row(res)) {
    GroupInfo g;
    g.group_id = std::stoi(row[0]);
    g.name = row[1] ? row[1] : "";
    g.owner_uid = std::stoi(row[2]);
    g.create_time = std::strtoull(row[3],nullptr,10);
    groups_[g.group_id] = g;
  }
  mysql_free_result(res);
}

void GroupManager::loadMembers() {
  if(!db_) return;
  MYSQL_RES* res = db_->query(
    "SELECT m.group_id,m.uid,m.permission,m.join_time,m.remind,u.username "
    "FROM group_members m LEFT JOIN users u ON u.uid=m.uid");
  if(!res) return;
  MYSQL_ROW row;
  while(row = mysql_fetch_row(res)) {
    GroupMember m;
    int gid = std::stoi(row[0]);
    m.uid = std::stoi(row[1]);
    m.permission = std::stoi(row[2]);
    m.join_time = std::strtoull(row[3],nullptr,10);
    m.remind = std::stoi(row[4]);
    m.usr_name = row[5] ? row[5] : "";
    members_[gid][m.uid] = m;
  }
  mysql_free_result(res);
}

void GroupManager::loadJoinRequests() {
  if(!db_) return;
  MYSQL_RES* res = db_->query("SELECT group_id,uid FROM group_join_requests");
  if(!res) return;
  MYSQL_ROW row;
  while(row = mysql_fetch_row(res)) {
    int gid = std::stoi(row[0]);
    int uid = std::stoi(row[1]);
    join_requests_[gid].insert(uid);
  }
  mysql_free_result(res);
}

void SessionManager::bindUser(int user_id,std::shared_ptr<Connection> conn) {
  std::lock_guard<std::mutex> lock(mtx_);
  user_to_conn_[user_id] = std::move(conn);
}

void SessionManager::unbindUser(int user_id,const std::shared_ptr<Connection>& conn) {
  std::lock_guard<std::mutex> lock(mtx_);
  auto it = user_to_conn_.find(user_id);
  if(it != user_to_conn_.end() && it->second == conn) {
    user_to_conn_.erase(it);
  }
}

bool SessionManager::isBoundTo(int uid,const std::shared_ptr<Connection>& conn) {
  std::lock_guard lock(mtx_);
  auto it = user_to_conn_.find(uid);
  return it != user_to_conn_.end() && it->second == conn;
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
  : storage_dir_(storage_dir) {
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
  if(!insertFileMeta(meta)) return false;
  metas_[meta.file_id] = meta;
  return true;
}

bool FileManager::updateFileMeta(const FileMeta& meta) {
  std::lock_guard<std::mutex> lock(mtx_);
  auto it = metas_.find(meta.file_id);
  if(it == metas_.end()) return false;
  if(!updateFileMetaDb(meta)) return false;
  it->second = meta;
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
  updateFileStatus(file_id,1);
  return true;
}

bool FileManager::removePart(const std::string& file_id) {
  std::lock_guard<std::mutex> lock(mtx_);
  std::string part = getPartPath(file_id);
  unlink(part.c_str());
  deleteFileMetaDb(file_id);
  metas_.erase(file_id);
  return true;
}

bool FileManager::loadMeta() {
  if(!db_) return false;
  MYSQL_RES* res = db_->query("SELECT file_id,file_name,file_size,uploader_uid,upload_time,storage_path,file_hash,chat_type,target_id,status,received FROM file_meta");
  if(!res) return false;
  MYSQL_ROW row;
  while((row = mysql_fetch_row(res))) {
    FileMeta m;
    m.file_id = row[0] ? row[0] : "";
    m.file_name = row[1] ? row[1] : "";
    m.file_size = std::strtoll(row[2],nullptr,10);
    m.uploader_uid = std::atoi(row[3]);
    m.upload_time = std::strtoull(row[4],nullptr,10);
    m.storage_path = row[5] ? row[5] : "";
    m.file_hash = row[6] ? row[6] : "";
    m.chat_type = row[7] ? row[7] : "";
    m.target_id = row[8] ? std::atoi(row[8]) : 0;
    m.status = std::atoi(row[9]);
    m.received = std::strtoull(row[10],nullptr,10);
    metas_[m.file_id] = m;
  }
  mysql_free_result(res);
  return true;
}

bool FileManager::insertFileMeta(const FileMeta& m) {
  if(!db_) return false;
  return db_->execute(
    "INSERT INTO file_meta (file_id,file_name,file_size,uploader_uid,upload_time,storage_path,file_hash,chat_type,target_id,status,received) VALUES ('"
    + db_->escape(m.file_id) + "','"
    + db_->escape(m.file_name) + "',"
    + std::to_string(m.file_size) + ","
    + std::to_string(m.uploader_uid) + ","
    + std::to_string(m.upload_time) + ",'"
    + db_->escape(m.storage_path) + "','"
    + db_->escape(m.file_hash) + "','"
    + db_->escape(m.chat_type) + "',"
    + std::to_string(m.target_id) + ","
    + std::to_string(m.status) + ","
    + std::to_string(m.received) + ")"
  );
}

bool FileManager::updateFileMetaDb(const FileMeta& m) {
  if(!db_) return false;
  return db_->execute(
    "UPDATE file_meta SET file_name='" + db_->escape(m.file_name) + "',"
    "file_size=" + std::to_string(m.file_size) + ","
    "uploader_uid=" + std::to_string(m.uploader_uid) + ","
    "upload_time=" + std::to_string(m.upload_time) + ","
    "storage_path='" + db_->escape(m.storage_path) + "',"
    "file_hash='" + db_->escape(m.file_hash) + "',"
    "chat_type='" + db_->escape(m.chat_type) + "',"
    "target_id=" + std::to_string(m.target_id) + ","
    "status=" + std::to_string(m.status) + ","
    "received=" + std::to_string(m.received)
    + " WHERE file_id='" + db_->escape(m.file_id) + "'"
  );
}

bool FileManager::updateFileReceived(const std::string& file_id,uint64_t received) {
  if(!db_) return false;
  return db_->execute("UPDATE file_meta SET received=" + std::to_string(received) +
    " WHERE file_id='" + db_->escape(file_id) + "'"
  );
}

bool FileManager::updateFileStatus(const std::string& file_id,int status) {
  if(!db_) return false;
  return db_->execute("UPDATE file_meta SET status=" + std::to_string(status) +
    " WHERE file_id='" + db_->escape(file_id) + "'"
  );
}

bool FileManager::deleteFileMetaDb(const std::string& file_id) {
  if(!db_) return false;
  return db_->execute("DELETE FROM file_meta WHERE file_id='" + db_->escape(file_id) + "'");
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
  uint64_t now = now_ms();
  for(auto it = metas_.begin(); it != metas_.end(); ) {
    if(it->second.status == 0 && now - it->second.upload_time > UPLOAD_STALE_MS) {
      std::string part = getPartPath(it->first);
      unlink(part.c_str());
      deleteFileMetaDb(it->first);
      it = metas_.erase(it);
    } else {
      it++;
    }
  }
}

// 消息入库优化
void MessageWrite::start() {
  thread_ = std::thread([this] { loop(); });
}

void MessageWrite::stop() {
  {
    std::lock_guard<std::mutex> lock(mtx_);
    stop_ = true;
  }
  cv_.notify_all();
  if(thread_.joinable()) thread_.join();
}

void MessageWrite::push(const Message& msg) {
  {
    std::lock_guard<std::mutex> lock(mtx_);
    queue_.push_back(msg);
  }
  cv_.notify_one();
}

void MessageWrite::loop() {
  while(true) {
    std::deque<Message> msgs;
    {
      std::unique_lock<std::mutex> lock(mtx_);
      cv_.wait_for(lock,std::chrono::milliseconds(100),[&] { return stop_ || !queue_.empty(); });
      msgs.swap(queue_);
      if(stop_ && msgs.empty()) break;
    }
    down(msgs);
  }
}

void MessageWrite::down(std::deque<Message>& msgs) {
  if(msgs.empty()) return;
  std::string sql = 
    "INSERT INTO messages (message_id,type,chat_type,from_uid,target_id,content,msg_time,status) VALUES ";
  bool first = true;
  for(auto& m : msgs) {
    if(!first) sql += ",";
    first = false;
    sql += "('"
      + db_->escape(m.message_id) + "','"
      + db_->escape(m.type) + "','"
      + db_->escape(m.chat_type) + "',"
      + std::to_string(m.from_uid) + ","
      + std::to_string(m.target_id) + ",'"
      + db_->escape(m.content) + "',"
      + std::to_string(m.time) + ","
      + std::to_string(m.status) + ")";
  }
  if(!db_->execute(sql)) {
    LOG(ERROR) << "消息批量写消息失败，丢失 " << msgs.size() << " 条";
  }
}
