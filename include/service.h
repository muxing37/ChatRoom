#pragma once
#include "shared.h"
#include "socket.h"
#include <unordered_map>
#include <vector>
#include <mutex>
#include <optional>
#include <unordered_set>
#include <memory>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <ctime>

// 可以让 ClientContext 在数据更新时提供一个简单的通知机制
class ClientContext {
public:
  // 用户本人
  void setSelf(const User& user);
  User getSelf();
  // 好友相关
  void setFriendList(const std::vector<User>& list);
  std::vector<User> getFriendList();
  bool hasFriend(int uid);
  std::optional<User> getFriend(int uid);
  void addFriend(const User& user);
  void addFriendRequest(const User& user);
  void delFriend(int uid);
  void setFriendRequests(const std::vector<User>& list);
  std::vector<User> getFriendRequests();
  // 聊天相关
  void addMessage(const Message& msg);
  std::vector<Message> getMessage(int id);
  void setMessage(int uid,const std::vector<Message>& msgs);
  void loadMoreMessages(int uid,const std::vector<Message>& msgs);
  // 群组相关


private:
  // 用户本人
  User self_;
  // 好友相关
  std::unordered_map<int,User> friends_;
  std::unordered_map<int,User> friendRequests_;
  // 聊天相关
  std::unordered_map<int,std::vector<Message>> msgs_;
  
  // 群组相关


  mutable std::mutex mtx_;
};

class ClientNetwork {
public:
  using json = nlohmann::json;
  using PushHandler = std::function<void(const json&)>;

  ClientNetwork(std::shared_ptr<TcpSocket>& sock);
  ~ClientNetwork();
  void start();
  void stop();

  bool send(const json& j);
  json request(json j);

  void setPushHandler(PushHandler handler);

private:
  void recvLoop();
  void dispatch(const json& j);
  void pushReply(const json& j);
  std::string generateRequestId();

private:
  std::shared_ptr<TcpSocket> sock_;
  std::atomic<bool> running_{false};
  std::thread recvThread_;
  std::mutex replyMutex_;
  std::condition_variable replyCv_;
  std::unordered_map<std::string,json> replies_;
  PushHandler pushHandler_;
  std::atomic<uint64_t> requestCounter_{1};
};

class AuthService {
public:
  AuthService(ClientNetwork& network,ClientContext& ctx);
  bool login(const std::string& username,const std::string& password);
  bool regis(const std::string& username,const std::string& password);

private:
  ClientNetwork& network_;
  ClientContext& ctx_;
};

class FriendService {
public:
  FriendService(ClientNetwork& network,ClientContext& ctx);
  int request(int uid);
  int del(int uid);
  int listFriend();
  int listRequest();
  int agree(int uid);
  int reject(int uid);

private:
  ClientNetwork& network_;
  ClientContext& ctx_;
};

class PrivateChatService {
public:
  PrivateChatService(ClientNetwork& network,ClientContext& ctx);
  int sendPrivateMessage(int to_uid,const std::string& text);
  int syncHistory(int uid);
  std::vector<Message> getMessages(int uid);
  void gotPush(const nlohmann::json& push);

private:
  ClientNetwork& network_;
  ClientContext& ctx_;
};

class GroupService {
public:
  GroupService(ClientNetwork& network,ClientContext& ctx);

private:
  ClientNetwork& network_;
  ClientContext& ctx_;
};