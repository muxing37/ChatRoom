#pragma once
#include "shared.h"
#include "context.h"
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
  void setDisconnectHandler(std::function<void()> cb);

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
  std::function<void()> disconnect_cb_;
  std::atomic<uint64_t> requestCounter_{1};
};

class AuthService {
public:
  AuthService(ClientNetwork& network,ClientContext& ctx);
  bool login(const std::string& username,const std::string& password);
  bool regis(const std::string& username,const std::string& password);
  bool logout(); //登出
  bool delauth(const std::string& password); //注销账户

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

  void handlePush(const nlohmann::json& push);

private:
  ClientNetwork& network_;
  ClientContext& ctx_;
};

class ChatService {
public:
  ChatService(ClientNetwork& network,ClientContext& ctx);
  int sendPrivateMessage(int to_uid,const std::string& text); //发送私聊消息
  int sendGroupMessage(int gid,const std::string& text); //发送群聊消息
  int syncHistory(); //从服务端同步聊天记录
  std::vector<Message> getMessages(int uid); //从本地ctx获取聊天记录
  void handlePush(const nlohmann::json& push);

private:
  ClientNetwork& network_;
  ClientContext& ctx_;
};

class GroupService {
public:
  GroupService(ClientNetwork& network,ClientContext& ctx);
  int createGroup(const std::string& name,int& out_gid);
  int disbandGroup(int gid);
  // 成员管理
  int requestJoin(int gid);
  int handleJoinRequest(int gid,int target_uid,bool approval);
  int leaveGroup(int gid);
  int kickMember(int gid,int target_uid);
  int setAdmin(int gid,int target_uid,bool admin);
  int setRemind(int gid,int remind);
  // 查询与同步
  int listMyGroups(); // 获取我加入的群列表，更新 ctx
  int listMembers(int gid); // 获取某群成员，更新 ctx
  int listJoinRequests(int gid,std::vector<User>& out_requests); // 获取某群申请列表
  int syncJoinRequests(); // 登录时同步管理的所有群的入群申请

  void handlePush(const nlohmann::json& push);

private:
  ClientNetwork& network_;
  ClientContext& ctx_;
};

class FileService {
public:
  using ProgressBack = std::function<void(uint64_t,uint64_t)>;
  FileService(ClientNetwork& network,ClientContext& ctx);
  int uploadFile(const std::string& path,const std::string& chat_type,int target_id,ProgressBack progress,const std::string& file_name = "");
  int downloadFile(const std::string& file_id,const std::string& save_path,ProgressBack progress,uint64_t resume_offset = 0);

private:
  struct DataLink {
    std::string ip;
    unsigned short port;
    std::string file_id;
    uint64_t offset;
    uint64_t file_size;
  };
  std::optional<DataLink> makeLink(const std::string& action,const nlohmann::json& data);
private:
  ClientNetwork& network_;
  ClientContext& ctx_;
};
