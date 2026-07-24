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
  for(auto &u : list) friends_[u.uid]=u;
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
// 群组相关


//----------------------------------------------------

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
  j["time"] = time(nullptr);
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

PrivateChatService::PrivateChatService(ClientNetwork& network,ClientContext& ctx) : network_(network),ctx_(ctx){}

int PrivateChatService::sendPrivateMessage(int to_uid,const std::string& text) {
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
    {"time",time(nullptr)},
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
  // ctx_.addMessage();
  return 0;
}

int PrivateChatService::syncHistory(int uid) {
  nlohmann::json j;
  j["type"] = "chat";
  j["action"] = "private_history";
  j["data"] = {
    {"from_uid",uid}
  };
  auto reply = network_.request(j);
  if(reply["status"] != 0) return -1;
  std::vector<Message> msgs;
  for(auto t : reply["data"]["messages"]) {
    msgs.push_back(t.get<Message>());
  }
  ctx_.setMessage(uid,msgs);
  return 0;
}

std::vector<Message> PrivateChatService::getMessages(int uid) {
  return ctx_.getMessage(uid);
}

void PrivateChatService::gotPush(const nlohmann::json& push) {
  auto msg = push["data"].get<Message>();
  ctx_.addMessage(msg);
}

// std::string PrivateChatService::getMsgId() {

// }
