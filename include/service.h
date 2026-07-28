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
#include <fstream>
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

  void addBlock(int uid);
  void unBlock(int uid);
  bool isBlocked(int uid);
  void setBlockList(const std::vector<int>& list);
  std::vector<int> getBlockList();
  // 聊天相关
  void addMessage(const Message& msg);
  std::vector<Message> getMessage(int id);
  void setMessage(int uid,const std::vector<Message>& msgs);
  void loadMoreMessages(int uid,const std::vector<Message>& msgs);
  void updateLastSyncTime(uint64_t t);
  uint64_t getLastSyncTime();
  bool isMessageRepeat(const std::string& msg_id);
  void markMessageReceived(const std::string& msg_id);
  // 群组相关

  // 本地信息存储相关
  std::unordered_map<int,std::vector<Message>> getAllMessages() const;
  void loadMessages(const std::unordered_map<int,std::vector<Message>>& allMsgs);

private:
  // 用户本人
  User self_;
  // 好友相关
  std::unordered_map<int,User> friends_;
  std::unordered_map<int,User> friendRequests_;
  std::unordered_set<int> blocked_;
  // 聊天相关
  std::unordered_map<int,std::vector<Message>> msgs_;
  uint64_t last_sync_time_ = 0;
  std::unordered_set<std::string> known_msg_;
  // 群组相关


  mutable std::mutex mtx_;
};

class ChatStorage {
public:
  ChatStorage(const std::string& filepath);
  bool save(const ClientContext& ctx);
  bool load(ClientContext& ctx);

private:
  std::string filepath_;
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
  bool logout(); //登出
  bool delauth(); //注销账户

private:
  ClientNetwork& network_;
  ClientContext& ctx_;
};

class FriendService {
public:
  FriendService(ClientNetwork& network,ClientContext& ctx);
  int request(int uid); //好友申请
  int del(int uid); //删除好友
  int listFriend(); //获取好友列表
  int listRequest(); //获取好友申请列表
  int agree(int uid); //同意好友申请
  int reject(int uid); //拒绝好友申请

  int block(int uid); //屏蔽
  int unblock(int uid); //解除屏蔽
  int getBlockList(); //获取屏蔽的好友列表

private:
  ClientNetwork& network_;
  ClientContext& ctx_;
};

class PrivateChatService {
public:
  PrivateChatService(ClientNetwork& network,ClientContext& ctx);
  int sendPrivateMessage(int to_uid,const std::string& text); //发送私聊消息
  int syncHistory(); //从服务端同步聊天记录
  std::vector<Message> getMessages(int uid); //从本地ctx获取聊天记录
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