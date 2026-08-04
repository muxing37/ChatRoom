#include "service.h"

// 用户本人
void ClientContext::setSelf(const User& user) {
  std::lock_guard lock(mtx_);
  self_=user;
}

User ClientContext::getSelf() {
  std::lock_guard lock(mtx_);
  return self_;
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
    std::cout << u.username << std::endl;
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
void ClientContext::addMessage(const Message& msg) {
  std::lock_guard<std::mutex> lock(mtx_);
  if(msg.chat_type == "private") {
    if(msg.from_uid == self_.uid) {
      msgs_[msg.target_id].push_back(msg);
    } else {
      msgs_[msg.from_uid].push_back(msg);
    }
  } else if(msg.chat_type == "group") {
    msgs_[msg.target_id].push_back(msg);
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

void ClientContext::loadMoreMessages(int uid,const std::vector<Message>& msgs) {

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

//----------------------------------------------------

ChatStorage::ChatStorage(const std::string& filepath) : filepath_(filepath) {}

bool ChatStorage::save(const ClientContext& ctx) {
  auto allMsgs = ctx.getAllMessages();
  nlohmann::json j;
  nlohmann::json& messagesArray = j["messages"];
  for(const auto& [peerId, msgs] : allMsgs) {
    for(const auto& msg : msgs) {
      nlohmann::json msgJson;
      to_json(msgJson,msg);
      messagesArray.push_back({
        {"peer_id",peerId},
        {"msg",msgJson}
      });
    }
  }

  std::ofstream ofs(filepath_);
  if(!ofs.is_open()) return false;
  ofs << j.dump(4);
  return true;
}

bool ChatStorage::load(ClientContext& ctx) {
  std::ifstream ifs(filepath_);
  if(!ifs.is_open()) return false;
  nlohmann::json j;
  try {
    ifs >> j;
  } catch(...) {
    return false;
  }
  std::unordered_map<int, std::vector<Message>> allMsgs;
  for(const auto& item : j.at("messages")) {
    int peerId = item.at("peer_id");
    Message msg;
    from_json(item.at("msg"), msg);
    allMsgs[peerId].push_back(msg);
  }
  ctx.loadMessages(allMsgs);
  return true;
}

ClientNetwork::ClientNetwork(std::shared_ptr<TcpSocket>& sock) : sock_(std::move(sock)) {}

ClientNetwork::~ClientNetwork() {
  stop();
}

void ClientNetwork::start() {
  running_ = true;

  recvThread_ = std::thread(
    &ClientNetwork::recvLoop,
    this
  );
}

void ClientNetwork::stop() {
  running_ = false;
  if(sock_) sock_->closefd();
  if(recvThread_.joinable()) recvThread_.join();
}

bool ClientNetwork::send(const json& j) {
  if(sock_->sendMsg(j.dump()) == NetResult::OK) {
    return true;
  } else {
    return false;
  }
}

nlohmann::json ClientNetwork::request(json j) {
  auto id = generateRequestId();
  j["msg_type"] = "request";
  j["request_id"] = id;
  j["time"] = now_ms();
  // time(nullptr);
  send(j);

  std::unique_lock lock(replyMutex_);
  if(!replyCv_.wait_for(lock,std::chrono::seconds(30),
  [&]{return replies_.count(id);})) {
    return {
      {"status",-1},
      {"error","timeout"}
    };
  }
  auto reply = replies_[id];
  replies_.erase(id);
  return reply;
}

void ClientNetwork::setPushHandler(PushHandler handler) {
  pushHandler_=std::move(handler);
}

void ClientNetwork::recvLoop() {
  while(running_) {
    std::string s;
    if(sock_->recvMsg(s)!=NetResult::OK) break;
    if(s.empty()) continue;
    json j;

    try {
      j = json::parse(s);
    } catch(...) {
      continue;
    }
    dispatch(j);
  }
}

void ClientNetwork::dispatch(const json& j) {
  auto type = j.value("msg_type","");

  if(type == "reply") {
    pushReply(j);
  }
  if(type == "push") {
    if(pushHandler_) pushHandler_(j);
  }
}

void ClientNetwork::pushReply(const json& j) {
  auto id = j["request_id"].get<std::string>();
  {
    std::lock_guard lock(replyMutex_);
    replies_[id]=j;
  }
  replyCv_.notify_all();
}

std::string ClientNetwork::generateRequestId() {
  return std::to_string(requestCounter_++);
}

AuthService::AuthService(ClientNetwork& network,ClientContext& ctx) : network_(network),ctx_(ctx){}

bool AuthService::login(const std::string& username,const std::string& password) {
  nlohmann::json j;
  j["msg_type"]="request";
  j["type"]="user";
  j["action"]="login";

  j["data"]={
    {"username",username},
    {"password",password}
  };
  auto reply = network_.request(j);

  if(reply["status"] != 0) {
    return false;
  }
  User user;
  user.uid = reply["data"]["uid"];
  user.username=username;
  ctx_.setSelf(user);
  return true;
}

bool AuthService::regis(const std::string& username,const std::string& password) {
  nlohmann::json j;
  j["msg_type"]="request";
  j["type"]="user";
  j["action"]="register";
  j["data"]={
    {"username",username},
    {"password",password}
  };
  auto reply = network_.request(j);

  if(reply["status"] != 0) {
    return false;
  }
  User user;
  user.uid = reply["data"]["uid"];
  user.username = username;
  ctx_.setSelf(user);
  return true;
}

FriendService::FriendService(ClientNetwork& network,ClientContext& context) : network_(network),ctx_(context) {}

int FriendService::request(int uid) {
  nlohmann::json j;
  j["type"] = "friend";
  j["action"] = "request";
  j["data"]["from_uid"] = ctx_.getSelf().uid;
  j["data"]["to_uid"] = uid;
  auto reply = network_.request(j);
  return reply["status"];
}

int FriendService::del(int uid) {
  nlohmann::json j;
  j["type"] = "friend";
  j["action"] = "del";
  j["data"]["from_uid"] = ctx_.getSelf().uid;
  j["data"]["to_uid"] = uid;
  auto reply = network_.request(j);

  return reply["status"];
}

int FriendService::listFriend() {
  nlohmann::json j;
  j["type"] = "friend";
  j["action"] = "list_friend";
  j["data"]["from_uid"] = ctx_.getSelf().uid;
  auto reply = network_.request(j);
  if(reply["status"] != 0) return reply["status"];

  std::vector<User> list;

  for(auto& x : reply["data"]["friends"]) {
    User user;
    user.uid = x["uid"];
    user.username = x["username"];
    list.push_back(user);
  }
  ctx_.setFriendList(list);
  return reply["status"];
}

int FriendService::listRequest() {
  nlohmann::json j;
  j["type"] = "friend";
  j["action"] = "list_request";
  j["data"]["from_uid"] = ctx_.getSelf().uid;
  auto reply = network_.request(j);
  if(reply["status"] != 0) return reply["status"];

  std::vector<User> list;
  for(auto& x : reply["data"]["request"]) {
    User user;
    user.uid = x["uid"];
    user.username = x["username"];
    list.push_back(user);
  }
  ctx_.setFriendRequests(list);
  return reply["status"];
}

int FriendService::agree(int uid) {
  nlohmann::json j;
  j["type"] = "friend";
  j["action"] = "agree";
  j["data"]["from_uid"] = ctx_.getSelf().uid;
  j["data"]["to_uid"] = uid;
  auto reply = network_.request(j);
  return reply["status"];
}

int FriendService::reject(int uid) {
  nlohmann::json j;
  j["type"] = "friend";
  j["action"] = "reject";
  j["data"]["from_uid"] = ctx_.getSelf().uid;
  j["data"]["to_uid"] = uid;
  auto reply = network_.request(j);
  return reply["status"];
}

int FriendService::block(int uid) {
  nlohmann::json j;
  j["type"] = "friend";
  j["action"] = "block";
  j["data"] = {
    {"from_uid",ctx_.getSelf().uid},
    {"to_uid", uid}
  };
  auto reply = network_.request(j);
  return reply["status"];
}

int FriendService::unblock(int uid) {
  nlohmann::json j;
  j["type"] = "friend";
  j["action"] = "unblock";
  j["data"] = {
    {"from_uid",ctx_.getSelf().uid},
    {"to_uid",uid}
  };
  auto reply = network_.request(j);
  return reply["status"];
}

int FriendService::getBlockList() {
  nlohmann::json j;
  j["type"] = "friend";
  j["action"] = "list_block";
  j["data"] = {
    {"from_uid",ctx_.getSelf().uid}
  };
  auto reply = network_.request(j);
  if(reply["status"] != 0) return -1;
  std::vector<int> list = reply["data"]["block_list"].get<std::vector<int>>();
  ctx_.setBlockList(list);
  return 0;
}

ChatService::ChatService(ClientNetwork& network,ClientContext& ctx) : network_(network),ctx_(ctx){}

int ChatService::sendPrivateMessage(int to_uid,const std::string& text) {
  nlohmann::json j;
  j["type"] = "chat";
  j["action"] = "private_chat";
  j["data"] = {
    {"message_id","0"},
    {"type","text"},
    {"chat_type","private"},
    {"from_uid",ctx_.getSelf().uid},
    {"target_id",to_uid},
    {"content",text},
    {"time",0},
    {"status",0}
  };
  auto reply = network_.request(j);
  if(reply["status"] != 0) {
    return -1;
  }
  auto data = reply["data"];
  Message msg;
  msg.message_id = data["message_id"];
  msg.type = data["type"];
  msg.chat_type = data["chat_type"];
  msg.from_uid = ctx_.getSelf().uid;
  msg.target_id = to_uid;
  msg.content = text;
  msg.time = data["time"];
  ctx_.addMessage(msg);
  return 0;
}

int ChatService::sendGroupMessage(int gid,const std::string& text) {
  nlohmann::json j;
  j["type"] = "chat";
  j["action"] = "group_chat";
  j["data"] = {
    {"message_id","0"},
    {"type","text"},
    {"chat_type","group"},
    {"from_uid",ctx_.getSelf().uid},
    {"target_id",gid},
    {"content",text},
    {"time",0},
    {"status",0},
  };
  nlohmann::json reply = network_.request(j);
  if(reply["status"] != 0) return reply["status"];
  Message msg = reply["data"].get<Message>();
  ctx_.addMessage(msg);
  return 0;
}

int ChatService::syncHistory() {
  int self_uid = ctx_.getSelf().uid;
  uint64_t last_time = ctx_.getLastSyncTime();
  nlohmann::json j;
  j["type"] = "chat";
  j["action"] = "private_history_sync";
  j["data"] = {
    {"from_uid",self_uid},
    {"last_time",last_time}
  };
  auto reply = network_.request(j);
  if(reply["status"] != 0) return -1;
  std::vector<Message> msgs;
  for(auto& item : reply["data"]["messages"]) {
    msgs.push_back(item.get<Message>());
  }

  for(auto& msg : msgs) {
    if(!ctx_.isMessageRepeat(msg.message_id)) {
      ctx_.markMessageReceived(msg.message_id);
      ctx_.addMessage(msg);
      ctx_.updateLastSyncTime(msg.time);
    }
  }
  return 0;
}

std::vector<Message> ChatService::getMessages(int uid) {
  return ctx_.getMessage(uid);
}

void ChatService::handlePush(const nlohmann::json& push) {
  if(push["type"] == "chat") {
    auto msg = push["data"].get<Message>();
    if(!ctx_.isMessageRepeat(msg.message_id)) {
      ctx_.markMessageReceived(msg.message_id);
      ctx_.addMessage(msg);
      ctx_.updateLastSyncTime(msg.time);
    }
  }
}

GroupService::GroupService(ClientNetwork& network,ClientContext& ctx) : network_(network),ctx_(ctx){}

int GroupService::createGroup(const std::string& name,int& out_gid) {
  nlohmann::json j;
  j["type"] = "group";
  j["action"] = "create";
  j["data"] = {
    {"from_uid",ctx_.getSelf().uid},
    {"group_name",name}
  };
  nlohmann::json reply = network_.request(j);
  if(reply["status"] != 0) return reply["status"];
  GroupInfo g;
  g.group_id = reply["data"]["group_id"];
  g.name = reply["data"]["name"];
  g.owner_uid = reply["data"]["owner_uid"];
  g.create_time = reply["data"]["create_time"];
  ctx_.addGroup(g);
  ctx_.setSelfPermission(g.group_id,0);
  ctx_.setSelfRemind(g.group_id,0);
  out_gid = g.group_id;
  return 0;
}

int GroupService::disbandGroup(int gid) {
  nlohmann::json j;
  j["type"] = "group";
  j["action"] = "disband";
  j["data"] = {
    {"from_uid",ctx_.getSelf().uid},
    {"group_id",gid}
  };
  nlohmann::json reply = network_.request(j);
  if(reply["status"] == 0) ctx_.removeGroup(gid);
  return reply["status"];
}

int GroupService::requestJoin(int gid) {
  nlohmann::json j;
  j["type"] = "group";
  j["action"] = "join_request";
  j["data"] = {
    {"from_uid",ctx_.getSelf().uid},
    {"group_id",gid}
  };
  nlohmann::json reply = network_.request(j);
  return reply["status"];
}

int GroupService::handleJoinRequest(int gid,int target_uid,bool approval) {
  nlohmann::json j;
  j["type"] = "group";
  j["action"] = "handle_request";
  j["data"] = {
    {"from_uid",ctx_.getSelf().uid},
    {"group_id",gid},
    {"target_id",target_uid},
    {"approval",approval}
  };
  nlohmann::json reply = network_.request(j);
  return reply["status"];
}

int GroupService::leaveGroup(int gid) {
  nlohmann::json j;
  j["type"] = "group";
  j["action"] = "leave";
  j["data"] = {
    {"from_uid",ctx_.getSelf().uid},
    {"group_id",gid}
  };
  nlohmann::json reply = network_.request(j);
  if(reply["status"] == 0) ctx_.removeGroup(gid);
  return reply["status"];
}

int GroupService::kickMember(int gid,int target_uid) {
  nlohmann::json j;
  j["type"] = "group";
  j["action"] = "kick";
  j["data"] = { 
    {"from_uid",ctx_.getSelf().uid},
    {"group_id",gid},
    {"target_uid",target_uid}
  };
  nlohmann::json reply = network_.request(j);
  if(reply["status"] == 0) ctx_.removeGroupMember(gid,target_uid);
  return reply["status"];
}

int GroupService::setAdmin(int gid,int target_uid,bool admin) {
  nlohmann::json j;
  j["type"] = "group";
  j["action"] = "set_admin";
  j["data"] = {
    {"from_uid",ctx_.getSelf().uid},
    {"group_id",gid},
    {"target_uid",target_uid},
    {"is_admin",admin}
  };
  nlohmann::json reply = network_.request(j);
  if(reply["status"] == 0) {
    auto member = ctx_.getGroupMember(reply["data"]["group_id"],reply["data"]["target_uid"]);
    if(member.has_value()) {
      auto m = member.value();
      if(reply["data"]["is_admin"]) {
        m.permission = 1;
      } else {
        m.permission = 2;
      }
      ctx_.addGroupMember(gid,m);
    }
  }
  return reply["status"];
}

int GroupService::setRemind(int gid,int remind) {
  nlohmann::json j;
  j["type"] = "group";
  j["action"] = "set_remind";
  j["data"] = {
    {"from_uid",ctx_.getSelf().uid},
    {"group_id",gid},
    {"remind",remind}
  };
  nlohmann::json reply = network_.request(j);
  if(reply["status"] == 0) {
    ctx_.setSelfRemind(gid,remind);
    auto member = ctx_.getGroupMember(gid,ctx_.getSelf().uid);
    if(member.has_value()) {
      auto m = member.value();
      m.remind = remind;
      ctx_.addGroupMember(gid,m);
    }
  }
  return reply["status"];
}

int GroupService::listMyGroups() {
  nlohmann::json j;
  j["type"] = "group";
  j["action"] = "list_my_groups";
  j["data"] = {
    {"from_uid",ctx_.getSelf().uid}
  };
  nlohmann::json reply = network_.request(j);
  if(reply["status"] != 0) return reply["status"];
  std::vector<GroupInfo> groups;
  for(auto& g : reply["data"]["groups"]) {
    groups.push_back(g.get<GroupInfo>());
  }
  ctx_.setGroupList(groups);
  return 0;
}

int GroupService::listMembers(int gid) {
  nlohmann::json j;
  j["type"] = "group";
  j["action"] = "list_members";
  j["data"] = {
    {"from_uid",ctx_.getSelf().uid},
    {"group_id",gid}
  };
  nlohmann::json reply = network_.request(j);
  if(reply["status"] != 0) return reply["status"];
  std::vector<GroupMember> members;
  for(auto& item : reply["data"]["members"]) {
    members.push_back(item.get<GroupMember>());
  }
  ctx_.setGroupMembers(gid,members);
  for(auto& m : members) {
    if(m.uid == ctx_.getSelf().uid) {
      ctx_.setSelfPermission(gid, m.permission);
      ctx_.setSelfRemind(gid, m.remind);
      break;
    }
  }
  return 0;
}

int GroupService::listJoinRequests(int gid,std::vector<User>& out_requests) {
  nlohmann::json j;
  j["type"] = "group";
  j["action"] = "list_join_requests";
  j["data"] = {
    {"from_uid",ctx_.getSelf().uid},
    {"group_id",gid}
  };
  nlohmann::json reply = network_.request(j);
  if(reply["status"] != 0) return reply["status"];
  out_requests.clear();
  for(auto& r : reply["data"]["requests"]) {
    User u;
    u.uid = r["uid"];
    u.username = r["username"];
    out_requests.push_back(u);
  }
  return 0;
}

void GroupService::handlePush(const nlohmann::json& push) {
  if(push["type"] == "group") {
    if(push["action"] == "kicked") {
      ctx_.removeGroup(push["data"]["group_id"]);
    }
    if(push["action"] == "join_result") {
      if(push["data"]["approval"] == true) {
        int gid = push["data"]["group_id"];
        GroupInfo g = push["data"].get<GroupInfo>();
        ctx_.addGroup(g);
        std::vector<GroupMember> members;
        for(auto& item : push["data"]["members"]) {
          members.push_back(item.get<GroupMember>());
        }
        ctx_.setGroupMembers(gid,members);
      }
    }
    if(push["action"] == "permission_changed") {
      int gid = push["data"]["group_id"];
      ctx_.setSelfPermission(gid,push["data"]["new_permission"]);
    }
    if(push["action"] == "member_update") {
      std::string update_type = push["data"].value("update_type","");
      int gid = push["data"]["group_id"];
      int m_uid = push["data"]["member_uid"];
      if(update_type == "set_admin") {
        auto m = ctx_.getGroupMember(gid,m_uid).value();
        m.permission = push["data"]["new_permission"];
        ctx_.addGroupMember(gid,m);
      }
      if(update_type == "kick" || update_type == "leave") {
        ctx_.removeGroupMember(gid,m_uid);
      }
      if(update_type == "join") {
        GroupMember m = push["data"].get<GroupMember>();
        ctx_.addGroupMember(gid,m);
      }
      if(update_type == "disbanded") {
        ctx_.removeGroup(gid);
      }
    }
  }
}

FileService::FileService(ClientNetwork& network,ClientContext& ctx) : network_(network),ctx_(ctx){}

