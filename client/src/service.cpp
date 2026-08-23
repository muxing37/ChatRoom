#include "service.h"
#include "context.h"
#include "hash.h"
#include <iostream>
#include <cstdlib>
#include <algorithm>

namespace { constexpr size_t FILE_CHUNK = 1024 * 1024; }

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

bool ClientNetwork::requestWithoutWait(json& j,std::function<void(const json& reply)> cb) {
  auto id = generateRequestId();
  j["msg_type"] = "request";
  j["request_id"] = id;
  j["time"] = now_ms();
  if(!send(j)) return false;
  {
    std::lock_guard<std::mutex> lock(asyncMtx_);
    abs_[id] = std::move(cb);
  }
  return true;
}

nlohmann::json ClientNetwork::request(json& j) {
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

void ClientNetwork::setDisconnectHandler(std::function<void()> cb) {
  disconnect_cb_ = cb;
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
  {
    std::lock_guard<std::mutex> lock(replyMutex_);
    replies_.clear();
  }
  replyCv_.notify_all();
  if(disconnect_cb_) disconnect_cb_();
}

void ClientNetwork::dispatch(const json& j) {
  auto type = j.value("msg_type","");

  if(type == "reply") {
    pushReply(j);
  }
  if(type == "push") {
    if(j.value("type","") == "heartbeat") {
      nlohmann::json pong;
      pong["type"] = "heartbeat";
      pong["action"] = "pong";
      send(pong);
      return;
    }
    if(pushHandler_) pushHandler_(j);
  }
}

void ClientNetwork::pushReply(const json& j) {
  auto id = j["request_id"].get<std::string>();
  std::function<void(const json& reply)> cb;
  {
    std::lock_guard<std::mutex> lock(asyncMtx_);
    auto it = abs_.find(id);
    if(it != abs_.end()) {
      cb = std::move(it->second);
      abs_.erase(it);
    }
  }
  if(cb) {
    try {
      cb(j);
    } catch(...) {}
    return;
  }
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
  ctx_.reset(); // 清空上个会话的本地状态
  ctx_.setLastLogoutTime(reply["data"].value("last_logout_time",0ULL));
  User user;
  user.uid = reply["data"]["uid"];
  user.username=username;
  ctx_.setSelf(user);
  ctx_.openLocalDb(username,user.uid);
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
  ctx_.setLastLogoutTime(0);
  ctx_.reset(); // 清空上个会话的本地状态
  User user;
  user.uid = reply["data"]["uid"];
  user.username = username;
  ctx_.setSelf(user);
  ctx_.openLocalDb(username,user.uid);
  return true;
}

bool AuthService::logout() {
  nlohmann::json j;
  j["type"] = "user";
  j["action"] = "logout";
  auto reply = network_.request(j);
  return reply["status"] == 0;
}

bool AuthService::delauth(const std::string& password) {
  nlohmann::json j;
  j["type"] = "user";
  j["action"] = "delete";
  j["data"] = {
    {"password",password},
    {"uid",ctx_.getSelf().uid}
  };
  auto reply = network_.request(j);
  if(reply["status"] != 0) return false;
  ctx_.removeLocalDb(ctx_.getSelf().username,ctx_.getSelf().uid);
  return true;
}

FriendService::FriendService(ClientNetwork& network,ClientContext& context) : network_(network),ctx_(context) {}

int FriendService::request(int uid) {
  if(uid == ctx_.getSelf().uid) return -1;
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
    user.online = x.value("online",false);
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

void FriendService::handlePush(const nlohmann::json& push) {
  if(push["type"] != "friend") return;
  std::string action = push.value("action","");
  if(action == "request") {
    User u;
    u.uid = push["data"]["uid"];
    u.username = push["data"]["username"];
    ctx_.addFriendRequest(u);
  } else if(action == "agree") {
    User u;
    u.uid = push["data"]["uid"];
    u.username = push["data"]["username"];
    ctx_.addFriend(u);
    ctx_.removeFriendRequest(u.uid);
  } else if(action == "reject") {
    ctx_.removeFriendRequest(push["data"].value("uid",0));
  } else if(action == "del") {
    ctx_.delFriend(push["data"].value("uid",0));
  } else if(action == "online") {
    ctx_.setFriendOnline(push["data"].value("uid",0),true);
  } else if(action == "offline") {
    ctx_.setFriendOnline(push["data"].value("uid",0),false);
  }
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
  if(!network_.requestWithoutWait(j,[this](const nlohmann::json& reply) {
      if(reply["status"] != 0) return;
      try {
        Message msg = reply["data"].get<Message>();
        if(ctx_.isMessageRepeat(msg.message_id)) return;
        ctx_.addMessage(msg);
        ctx_.markMessageReceived(msg.message_id);
      } catch(...) {}
    })) {
    return -1;
  }
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
  if(!network_.requestWithoutWait(j,[this](const nlohmann::json& reply) {
      if(reply["status"] != 0) return;
      try {
        Message msg = reply["data"].get<Message>();
        if(ctx_.isMessageRepeat(msg.message_id)) return;
        ctx_.addMessage(msg);
        ctx_.markMessageReceived(msg.message_id);
      } catch(...) {}
    })) {
    return -1;
  }
  return 0;
}

int ChatService::syncHistory() {
  int self_uid = ctx_.getSelf().uid;
  uint64_t last_time = ctx_.getLastSyncTime();
  nlohmann::json j;
  j["type"] = "chat";
  j["action"] = "sync_new";
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
    {"target_uid",target_uid},
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
      ctx_.setSelfPermission(gid,m.permission);
      ctx_.setSelfRemind(gid,m.remind);
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

int GroupService::syncJoinRequests() {
  nlohmann::json j;
  j["type"] = "group";
  j["action"] = "list_all_join_requests";
  j["data"] = {
    {"from_uid",ctx_.getSelf().uid}
  };
  auto reply = network_.request(j);
  if(reply["status"] != 0) return reply["status"];
  ctx_.clearGroupJoinRequests();
  for(auto& item : reply["data"]["groups"]) {
    int gid = item["gid"];
    std::vector<User> reqs;
    for(auto& r : item["requests"]) {
      User u;
      u.uid = r["uid"];
      u.username = r["username"];
      reqs.push_back(u);
    }
    ctx_.setGroupJoinRequests(gid,reqs);
  }
  return 0;
}

void GroupService::handlePush(const nlohmann::json& push) {
  if(push["type"] == "group") {
    if(push["action"] == "kicked") {
      ctx_.removeGroup(push["data"]["group_id"]);
    }
    if(push["action"] == "join_request") {
      User u;
      u.uid = push["data"].value("apply_uid",0);
      u.username = push["data"].value("apply_name","");
      ctx_.addGroupJoinRequest(push["data"].value("group_id",0),u);
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
      int m_uid = push["data"].value("member_uid",0);
      if(update_type == "set_admin") {
        auto om = ctx_.getGroupMember(gid,m_uid);
        if(om) {
          auto m = *om;
          m.permission = push["data"].value("new_permission",2);
          ctx_.addGroupMember(gid,m);
        }
      }
      if(update_type == "kick" || update_type == "leave") {
        ctx_.removeGroupMember(gid,m_uid);
      }
      if(update_type == "join") {
        GroupMember m;
        m.uid = m_uid;
        m.usr_name = push["data"].value("member_name","");
        m.permission = push["data"].value("permission",2);
        m.join_time = push["data"].value("join_time",(uint64_t)now_ms());
        m.remind = push["data"].value("remind",0);
        ctx_.addGroupMember(gid,m);
      }
      if(update_type == "disbanded") {
        ctx_.removeGroup(gid);
      }
    }
  }
}

FileService::FileService(ClientNetwork& network,ClientContext& ctx) : network_(network),ctx_(ctx){}

std::optional<FileService::DataLink> FileService::makeLink(const std::string& action,const nlohmann::json& data) {
  nlohmann::json j;
  j["type"] = "file";
  j["action"] = action;
  j["data"] = data;
  j["data"]["from_uid"] = ctx_.getSelf().uid;
  auto reply = network_.request(j);
  if(reply["status"] != 0) return std::nullopt;
  DataLink dl;
  dl.ip = reply["data"].value("ip",std::string());
  dl.port = (unsigned short)reply["data"].value("port",0);
  dl.file_id = reply["data"].value("file_id",std::string());
  dl.offset = reply["data"].value("offset",(uint64_t)0);
  dl.file_size = reply["data"].value("file_size",(uint64_t)0);
  if(dl.ip.empty() || dl.port == 0) return std::nullopt;
  return dl;
}

int FileService::uploadFile(
  const std::string& path,
  const std::string& chat_type,
  int target_id,
  ProgressBack progress,
  const std::string& f_name
) {
  std::string full_path = path;
  if(!full_path.empty() && full_path[0] == '~' && (full_path.size() == 1 || full_path[1] == '/')) {
    const char* home = getenv("HOME");
    if(home) full_path = std::string(home) + full_path.substr(1);
  }
  struct stat st;
  if(stat(full_path.c_str(),&st) != 0) {
    std::cerr << "[FileService] 上传失败: 路径不存在或不可访问: " << full_path << std::endl;
    return -1;
  }
  if(!S_ISREG(st.st_mode)) {
    std::cerr << "[FileService] 上传失败: 不是普通文件: " << full_path << std::endl;
    return -2;
  }
  uint64_t file_size = (uint64_t)st.st_size;
  if(file_size == 0) {
    std::cerr << "[FileService] 上传失败: 空文件: " << full_path << std::endl;
    return -3;
  }
  std::string file_name = f_name;
  if(file_name.empty()) {
    auto slash = full_path.find_last_of('/');
    if(slash == std::string::npos) {
      file_name = full_path;
    } else {
      file_name = full_path.substr(slash + 1);
    }
  }
  std::string file_hash = sha256File(full_path);

  std::string file_id = "";
  for(int i = 0;i < 3;i++) {
    nlohmann::json data = {
      {"chat_type",chat_type},
      {"target_id",target_id},
      {"file_name",file_name},
      {"file_size",file_size},
      {"file_id",file_id},
      {"file_hash",file_hash}
    };
    auto dl = makeLink("upload_req",data);
    if(!dl) {
      std::cerr << "[FileService] 上传失败: 服务端未接受 upload_req" << std::endl;
      return -4;
    }
    file_id = dl->file_id;
    auto sock = TcpSocket::connect(dl->ip, dl->port);
    if(!sock) {
      std::cerr << "[FileService] 上传失败: 连接失败 " << dl->ip << ":" << dl->port << std::endl;
      return -5;
    }

    int rfd = open(full_path.c_str(), O_RDONLY);
    if(rfd < 0) {
      std::cerr << "[FileService] 上传失败: 无法打开文件: " << full_path << std::endl;
      return -1;
    }
    if(lseek(rfd,(off_t)dl->offset,SEEK_SET) == (off_t)-1) {
      close(rfd);
      std::cerr << "[FileService] 上传失败: lseek 定位失败" << std::endl;
      return -1;
    }
    uint64_t offset = dl->offset;
    char buf[FILE_CHUNK];
    bool ok = true;
    while(offset < file_size) {
      ssize_t n = read(rfd, buf, sizeof(buf));
      if(n <= 0) { ok = false; break; }
      if(sock->sendRaw(buf,(size_t)n) != 0) {
        ok = false;
        break;
      }
      offset += (uint64_t)n;
      if(progress) progress(offset,file_size);
    }
    close(rfd);
    if(ok && offset == file_size) {
      std::cout << "[FileService] 上传完成: " << file_name << " (" << file_size << " 字节)" << std::endl;
      return 0;
    }
    std::cerr << "[FileService] 上传中断于 " << offset << "/" << file_size << "，尝试续传..." << std::endl;
  }
  return -6;
}

int FileService::downloadFile(const std::string& file_id,const std::string& save_path,ProgressBack progress,uint64_t resume_offset) {
  std::string fid = file_id;
  for(int i = 0; i < 3;i++) {
    nlohmann::json data = {
      {"file_id",fid},
      {"offset",resume_offset}
    };
    auto dl = makeLink("download_req",data);
    if(!dl) return -1;
    fid = dl->file_id;

    auto sock = TcpSocket::connect(dl->ip,dl->port);
    std::cout << dl->ip << ":" << dl->port;
    if(!sock) return -2;

    int wfd = open(save_path.c_str(), O_WRONLY | O_CREAT, 0644);
    if(wfd < 0) return -3;
    uint64_t offset = dl->offset;
    uint64_t total = dl->file_size;
    if(lseek(wfd,(off_t)offset,SEEK_SET) == (off_t)-1) {
      close(wfd);
      return -4;
    }
    char buf[FILE_CHUNK];
    bool ok = true;
    while(offset < total) {
      int n = sock->recvRaw(buf,sizeof(buf));
      if(n <= 0) {
        ok = false;
        break;
      }
      if(write(wfd,buf,(size_t)n) != (ssize_t)n) {
        ok = false;
        break;
      }
      offset += (uint64_t)n;
      if(progress) progress(offset,total);
    }
    close(wfd);
    if(ok && offset == total) return 0;
    resume_offset = offset;
  }
  return -5;
}

