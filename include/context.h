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

class ClientContext {
public:
  // 用户本人
  void setSelf(const User& user);
  User getSelf();
  // 会话重置（重新登录时清空所有本地状态）
  void reset();
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
  void setFriendOnline(int uid,bool online); // 好友上线/下线推送
  void removeFriendRequest(int uid); // 申请被拒推送

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
    // 群组列表
  void setGroupList(const std::vector<GroupInfo>& list);
  std::vector<GroupInfo> getGroupList();
  void addGroup(const GroupInfo& g);
  void removeGroup(int gid);
    // 群成员
  void setGroupMembers(int gid,const std::vector<GroupMember>& members);
  std::vector<GroupMember> getGroupAllMembers(int gid);
  void addGroupMember(int gid,const GroupMember& m);
  void removeGroupMember(int gid,int uid);
  std::optional<GroupMember> getGroupMember(int gid,int uid);
    // 本人在群内的信息
  void setSelfPermission(int gid,int perm);
  int getSelfPermission(int gid);
  void setSelfRemind(int gid,int remind);
  int getSelfRemind(int gid);
    // 群入群申请缓存（登录同步 + push 增量）
  void setGroupJoinRequests(int gid,const std::vector<User>& reqs);
  void addGroupJoinRequest(int gid,const User& u);
  void removeGroupJoinRequest(int gid,int uid);
  void clearGroupJoinRequests();
  std::unordered_map<int,std::vector<User>> getGroupJoinRequests();
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
  std::unordered_map<int,GroupInfo> groupMap_; // gid -> 信息
  std::vector<int> groupSort_; // gid排序
  std::unordered_map<int,std::vector<GroupMember>> groupMembers_; // gid -> 成员列表
  std::unordered_map<int,int> usrPermission_; // gid -> 用户的权限
  std::unordered_map<int,int> usrRemind_; // gid -> 用户的提醒设置
  std::unordered_map<int,std::vector<User>> groupJoinReqs_; // gid -> 入群申请列表

  mutable std::mutex mtx_;
};