#include "service.h"

void ClientContext::setSelf(const User& user) {
  std::lock_guard lock(mtx_);
  self_=user;
}

User ClientContext::getSelf() {
  std::lock_guard lock(mtx_);
  return self_;
}

void ClientContext::setFriendList(const std::vector<User>& list) {
  std::lock_guard lock(mtx_);
  friends_.clear();
  for(auto &u:list) friends_[u.uid]=u;
}

std::vector<User> ClientContext::getFriendList() {
  std::lock_guard lock(mtx_);
  std::vector<User> res;
  for(auto &[id,u]:friends_) res.push_back(u);
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
  j["data"]["uid"] = ctx_.getSelf().uid;
  auto reply = network_.request(j);
  if(reply["status"] != 0) return reply["status"];

  // ctx_.clearFriends();

  for(auto& x : reply["data"]["friends"]) {
    User user;
    user.uid = x["uid"];
    user.username = x["username"];
    ctx_.addFriend(user);
  }

  return reply["status"];
}

int FriendService::listRequest() {
  nlohmann::json j;
  j["type"] = "friend";
  j["action"] = "list_request";
  j["data"]["uid"] = ctx_.getSelf().uid;
  auto reply = network_.request(j);
  if(reply["status"] != 0) return reply["status"];

  // ctx_.clearFriends();

  for(auto& x : reply["data"]["request"]) {
    User user;
    user.uid = x["uid"];
    user.username = x["username"];
    ctx_.addFriend(user);
  }

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
  if(sock_) sock_->~TcpSocket();
  if(recvThread_.joinable()) recvThread_.join();
}

bool ClientNetwork::send(json j) {
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
  j["time"] = time(nullptr);
  send(j);

  std::unique_lock lock(replyMutex_);
  replyCv_.wait(lock,[&]{
    return replies_.count(id);
  });
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
  } else if(type == "push") {
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
  auto reply=network_.request(j);

  if(reply["status"] != 0) {
    return false;
  }
  User user;
  user.uid = reply["data"]["uid"];
  user.username=username;
  ctx_.setSelf(user);
  return true;
}
