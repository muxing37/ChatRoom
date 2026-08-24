#pragma once
#include "service.h"
#include "context.h"
#include <atomic>
#include <vector>
#include <string>
#include <cstdint>
#include <mutex>

enum class ClientState {
  LOGIN,
  MAIN_MENU,
  FRIEND_MENU,
  GROUP_MENU,
  GROUP_DETAIL_MENU,
  FILE_MENU
};

class CliUI {
public:
  CliUI(
    AuthService& authService,
    FriendService& friendService,
    ChatService& chatService,
    GroupService& groupService,
    FileService& fileService,
    ClientContext& ctx
  );
  ~CliUI();
  bool run();
  void handleChatPush(const nlohmann::json& push);
  void handleUiPush(const nlohmann::json& push);

private:
  // 登录/主循环
  bool loginMenu();
  bool mainMenu();
  void init();
  void showOfflineMessages();
  // 好友
  void friendMenu();
  void showFriendList();
  void addFriend();
  void deleteFriend();
  void showFriendRequest();
  void blockMenu();
  // 聊天
  void privateChat();
  void chatLoop(int peerId,bool isGroup);
  void printNewMessages(int peerId);
  void printMessage(const Message& msg);
  void uploadFileToChat(int peerId,bool isGroup);
  // 群组
  void groupMenu();
  void showGroupList();
  void createGroup();
  void joinGroup();
  void groupRoom();
  void groupManage();
  void handleGroupJoinRequest(int gid);
  int myGroupPermission(int gid);
  void setAdmin(int gid);
  void kickMember(int gid);
  void leaveGroup();
  void disbandGroup();
  void printMembers(const std::vector<GroupMember>& members);
  // 文件
  void fileMenu();
  void uploadFile();
  void downloadFile();
  // 账号管理
  void deleteAccount();
  // 通用展示/输入
  void show(ClientState state);
  int inputChoice(int min,int max,const std::string& prompt="请选择：");
  // std::string inputString(const std::string& prompt);
  std::string inputPassword(const std::string& prompt); // 密码隐藏输入
  int inputUid(const std::string& prompt);
  std::string formatTime(uint64_t t);

  std::string readLine(const std::string& prompt,bool hidden = false);
  bool isChatOf(const Message& msg);
  std::string chatSourceName(const Message& msg);
  std::string formatMessage(const Message& msg);
  std::vector<std::string> formatNewMessages(int peerId);

private:
  AuthService& authService_;
  FriendService& friendService_;
  ChatService& chatService_;
  GroupService& groupService_;
  FileService& fileService_;
  ClientContext& ctx_;
  bool running_=true;
  size_t chatPrinted_=0;

  std::mutex outMtx_;
  std::atomic<bool> inChat_{false};
  std::atomic<int> chatPeer_{0};
  std::atomic<bool> chatIsGroup_{false};

private:
  const std::vector<std::string> menu_login_={
    "登录","注册","退出程序"
  };
  const std::vector<std::string> menu_main_={
    "好友管理","私聊","群聊","文件传输","注销账户","退出登录"
  };
  const std::vector<std::string> menu_friend_={
    "查看好友列表","添加好友","删除好友","处理好友申请","屏蔽管理","返回主菜单"
  };
  const std::vector<std::string> menu_group_={
    "查看群聊列表","创建群聊","加入群聊","进入群聊房间","群管理","解散群聊","退出群聊","返回主菜单"
  };
  const std::vector<std::string> menu_group_detail_={
    "查看群成员","处理入群申请","设置管理员","踢出成员","返回"
  };
  const std::vector<std::string> menu_file_={
    "上传文件","下载文件","返回主菜单"
  };
};