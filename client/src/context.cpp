#include "context.h"
#include <filesystem>
#include <cstdio>

// 用户本人
void ClientContext::setSelf(const User& user) {
  std::lock_guard lock(mtx_);
  self_=user;
}

User ClientContext::getSelf() {
  std::lock_guard lock(mtx_);
  return self_;
}

void ClientContext::reset() {
  std::lock_guard lock(mtx_);
  self_ = User{};
  friends_.clear();
  friendRequests_.clear();
  blocked_.clear();
  msgs_.clear();
  known_msg_.clear();
  last_sync_time_ = 0;
  groupMap_.clear();
  groupSort_.clear();
  groupMembers_.clear();
  usrPermission_.clear();
  usrRemind_.clear();
  groupJoinReqs_.clear();
  last_logout_time_ = 0;
}

void ClientContext::setFriendOnline(int uid,bool online) {
  std::lock_guard lock(mtx_);
  auto it = friends_.find(uid);
  if(it != friends_.end()) it->second.online = online;
}

void ClientContext::removeFriendRequest(int uid) {
  std::lock_guard lock(mtx_);
  friendRequests_.erase(uid);
}
// 好友相关
void ClientContext::setFriendList(const std::vector<User>& list) {
  std::lock_guard lock(mtx_);
  friends_.clear();
  for(auto &u : list) friends_[u.uid] = u;
}

std::vector<User> ClientContext::getFriendList() {
  std::lock_guard lock(mtx_);
  std::vector<User> res;
  for(auto &[id,u] : friends_) {
    res.push_back(u);
  }
  return res;
}

bool ClientContext::hasFriend(int uid) {
  std::lock_guard lock(mtx_);
  return friends_.count(uid);
}

std::optional<User> ClientContext::getFriend(int uid) {
  std::lock_guard lock(mtx_);
  auto it=friends_.find(uid);
  if(it==friends_.end()) return std::nullopt;
  return it->second;
}

void ClientContext::addFriendRequest(const User& user) {
  std::lock_guard lock(mtx_);
  friendRequests_[user.uid]=user;
}

void ClientContext::addFriend(const User& user) {
  std::lock_guard lock(mtx_);
  friends_[user.uid]=user;
}

void ClientContext::delFriend(int uid) {
  std::lock_guard lock(mtx_);
  friends_.erase(uid);
}

void ClientContext::setFriendRequests(const std::vector<User>& list) {
  std::lock_guard lock(mtx_);
  friendRequests_.clear();
  for(auto &u:list) friendRequests_[u.uid]=u;
}

std::vector<User> ClientContext::getFriendRequests() {
  std::lock_guard lock(mtx_);
  std::vector<User> res;
  for(auto &[id,u]:friendRequests_) res.push_back(u);
  return res;
}

void ClientContext::addBlock(int uid) {
  std::lock_guard lock(mtx_);
  blocked_.insert(uid);
}

void ClientContext::unBlock(int uid) {
  std::lock_guard lock(mtx_);
  blocked_.erase(uid);
}

bool ClientContext::isBlocked(int uid) {
  std::lock_guard lock(mtx_);
  if(blocked_.count(uid)) {
    return true;
  }
  return false;
}

void ClientContext::setBlockList(const std::vector<int>& list) {
  std::lock_guard lock(mtx_);
  blocked_.clear();
  for(int uid : list) {
    blocked_.insert(uid);
  }
}

std::vector<int> ClientContext::getBlockList() {
  std::lock_guard lock(mtx_);
  std::vector<int> result;
  for(auto uid : blocked_) {
    result.push_back(uid);
  }
  return result;
}

// 聊天相关
void ClientContext::addMessage(Message& msg) {
  std::lock_guard<std::mutex> lock(mtx_);
  if(msg.from_uid == self_.uid || msg.time <= last_logout_time_) {
    msg.status = 1;
  } else {
    msg.status = 0;
  }
  if(msg.chat_type == "private") {
    if(msg.from_uid == self_.uid) {
      msgs_[msg.target_id].push_back(msg);
    } else {
      msgs_[msg.from_uid].push_back(msg);
    }
  } else if(msg.chat_type == "group") {
    msgs_[msg.target_id].push_back(msg);
  }
  local_db_.save(msg);
}

void ClientContext::markRead(int uid) {
  std::lock_guard<std::mutex> lock(mtx_);
  auto it = msgs_.find(uid);
  if(it == msgs_.end()) return;
  for(auto& m : it->second) {
    if(m.status != 1) {
      m.status = 1;
      local_db_.save(m);
    }
  }
}

std::vector<Message> ClientContext::getMessage(int id) {
  std::lock_guard<std::mutex> lock(mtx_);
  auto it = msgs_.find(id);

  if(it == msgs_.end()) return {};
  return it->second;
}

void ClientContext::setMessage(int id,const std::vector<Message>& msgs) {
  std::lock_guard<std::mutex> lock(mtx_);
  msgs_[id] = msgs;
}

void ClientContext::updateLastSyncTime(uint64_t t) {
  std::lock_guard lock(mtx_);
  if(t > last_sync_time_) last_sync_time_ = t;
}

uint64_t ClientContext::getLastSyncTime() {
  std::lock_guard lock(mtx_);
  return last_sync_time_;
}

bool ClientContext::isMessageRepeat(const std::string& msg_id) {
  std::lock_guard lock(mtx_);
  return known_msg_.count(msg_id);
}

void ClientContext::markMessageReceived(const std::string& msg_id) {
  std::lock_guard lock(mtx_);
  known_msg_.insert(msg_id);
}

// 群组相关
  // 群组列表
void ClientContext::setGroupList(const std::vector<GroupInfo>& list) {
  std::lock_guard lock(mtx_);
  groupMap_.clear();
  groupSort_.clear();
  for(auto& g : list) {
    groupMap_[g.group_id] = g;
    groupSort_.push_back(g.group_id);
  }
}

std::vector<GroupInfo> ClientContext::getGroupList() {
  std::lock_guard lock(mtx_);
  std::vector<GroupInfo> res;
  for(int gid : groupSort_) {
    auto it = groupMap_.find(gid);
    if(it != groupMap_.end()) res.push_back(it->second);
  }
  return res;
}

void ClientContext::addGroup(const GroupInfo& g) {
  std::lock_guard lock(mtx_);
  if(groupMap_.count(g.group_id)) return;
  groupMap_[g.group_id] = g;
  groupSort_.push_back(g.group_id);
}

void ClientContext::removeGroup(int gid) {
  std::lock_guard lock(mtx_);
  if(groupMap_.count(gid)) return;
  groupMap_.erase(gid);
  groupSort_.erase(std::remove(groupSort_.begin(),groupSort_.end(),gid),groupSort_.end());
  groupMembers_.erase(gid);
  usrPermission_.erase(gid);
  usrRemind_.erase(gid);
}

  // 群成员
void ClientContext::setGroupMembers(int gid,const std::vector<GroupMember>& members) {
  std::lock_guard lock(mtx_);
  groupMembers_[gid] = members;
}

std::vector<GroupMember> ClientContext::getGroupAllMembers(int gid) {
  std::lock_guard lock(mtx_);
  auto it = groupMembers_.find(gid);
  if(it != groupMembers_.end()) {
    return it->second;
  }
  return {};
}

void ClientContext::addGroupMember(int gid,const GroupMember& m) {
  std::lock_guard lock(mtx_);
  auto& mem = groupMembers_[gid];
  for(auto& me : mem) {
    if(me.uid == m.uid) {
      me = m;
      return;
    }
  }
  mem.push_back(m);
}

void ClientContext::removeGroupMember(int gid,int uid) {
  std::lock_guard lock(mtx_);
  auto it = groupMembers_.find(gid);
  if (it == groupMembers_.end()) return;
  auto& mem = it->second;
  mem.erase(std::remove_if(mem.begin(),mem.end(),
    [uid](const GroupMember& m) { return m.uid == uid; }),mem.end());
}

std::optional<GroupMember> ClientContext::getGroupMember(int gid,int uid) {
  std::lock_guard lock(mtx_);
  auto it = groupMembers_.find(gid);
  if(it == groupMembers_.end()) return std::nullopt;
  for(auto& m : it->second) {
    if(m.uid == uid) return m;
  }
  return std::nullopt;
}

  // 本人在群内的信息
void ClientContext::setSelfPermission(int gid,int perm) {
  std::lock_guard lock(mtx_);
  usrPermission_[gid] = perm;
}

int ClientContext::getSelfPermission(int gid) {
  std::lock_guard lock(mtx_);
  auto it = usrPermission_.find(gid);
  if(it != usrPermission_.end()) {
    return it->second;
  }
  return -1;
}

void ClientContext::setSelfRemind(int gid,int remind) {
  std::lock_guard lock(mtx_);
  usrRemind_[gid] = remind;
}

int ClientContext::getSelfRemind(int gid) {
  std::lock_guard lock(mtx_);
  auto it = usrRemind_.find(gid);
  if(it != usrRemind_.end()) {
    return it->second;
  }
  return 0;
}

// 群入群申请缓存
void ClientContext::setGroupJoinRequests(int gid,const std::vector<User>& reqs) {
  std::lock_guard lock(mtx_);
  groupJoinReqs_[gid] = reqs;
}

void ClientContext::addGroupJoinRequest(int gid,const User& u) {
  std::lock_guard lock(mtx_);
  auto& v = groupJoinReqs_[gid];
  for(auto& x : v) if(x.uid == u.uid) return; // 去重
  v.push_back(u);
}

void ClientContext::removeGroupJoinRequest(int gid,int uid) {
  std::lock_guard lock(mtx_);
  auto it = groupJoinReqs_.find(gid);
  if(it == groupJoinReqs_.end()) return;
  auto& v = it->second;
  v.erase(std::remove_if(v.begin(),v.end(),[uid](const User& u){ return u.uid==uid; }), v.end());
  if(v.empty()) groupJoinReqs_.erase(it);
}

void ClientContext::clearGroupJoinRequests() {
  std::lock_guard lock(mtx_);
  groupJoinReqs_.clear();
}

std::unordered_map<int,std::vector<User>> ClientContext::getGroupJoinRequests() {
  std::lock_guard lock(mtx_);
  return groupJoinReqs_;
}

// 本地信息存储相关
std::unordered_map<int,std::vector<Message>> ClientContext::getAllMessages() const {
  std::lock_guard<std::mutex> lock(mtx_);
  return msgs_;
}

void ClientContext::loadMessages(const std::unordered_map<int,std::vector<Message>>& allMsgs) {
  std::lock_guard<std::mutex> lock(mtx_);
  uint64_t maxTime = 0;
  for(const auto& [id,msgs] : allMsgs) {
    for(const auto& msg : msgs) {
      if(known_msg_.count(msg.message_id)) continue;
      known_msg_.insert(msg.message_id);
      msgs_[id].push_back(msg);
      if(msg.time > maxTime) maxTime = msg.time;
    }
  }

  if (maxTime > last_sync_time_) last_sync_time_ = maxTime;
}

bool ClientContext::openLocalDb(const std::string& username,int uid) {
  std::lock_guard<std::mutex> lock(mtx_);
  std::error_code ec;
  std::filesystem::create_directories("data",ec);
  local_db_.close();
  std::string path = "data/chatroom_" + username + "_" + std::to_string(uid) + ".db";
  if(!local_db_.init(path)) return false;
  auto all = local_db_.loadAll();
  msgs_.clear();
  known_msg_.clear();
  last_sync_time_ = 0;
  for(auto& msg : all) {
    known_msg_.insert(msg.message_id);
    if(msg.chat_type == "private") {
      if(msg.from_uid == self_.uid) {
        msgs_[msg.target_id].push_back(msg);
      } else {
        msgs_[msg.from_uid].push_back(msg);
      }
    } else if(msg.chat_type == "group") {
      msgs_[msg.target_id].push_back(msg);
    }
    if(msg.time > last_sync_time_) last_sync_time_ = msg.time;
  }
  return true;
}

void ClientContext::removeLocalDb(const std::string& username,int uid) {
  std::lock_guard<std::mutex> lock(mtx_);
  local_db_.close();
  std::string path = "data/chatroom_" + username + "_" + std::to_string(uid) + ".db";
  std::remove(path.c_str());
}

void ClientContext::closeLocalDb() {
  std::lock_guard<std::mutex> lock(mtx_);
  local_db_.close();
}

void ClientContext::setLastLogoutTime(uint64_t t) {
  std::lock_guard<std::mutex> lock(mtx_);
  last_logout_time_ = t;
}

uint64_t ClientContext::getLastLogoutTime() {
  std::lock_guard<std::mutex> lock(mtx_);
  return last_logout_time_;
}
