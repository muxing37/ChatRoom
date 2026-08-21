#pragma once
#include "service.h"
#include "context.h"
#include <atomic>
#include <vector>
#include <string>
#include <cstdint>

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
  bool run();
  void notifyPush();

private:
  // 登录/主循环
  bool loginMenu();
  bool mainMenu();
  void init();
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
  int inputChoice(int min,int max);
  std::string inputString(const std::string& prompt);
  int inputUid(const std::string& prompt);
  std::string formatTime(uint64_t t);

private:
  AuthService& authService_;
  FriendService& friendService_;
  ChatService& chatService_;
  GroupService& groupService_;
  FileService& fileService_;
  ClientContext& ctx_;
  bool running_=true;
  std::atomic<uint64_t> pushSeq_{0};
  uint64_t shownSeq_=0;
  size_t chatPrinted_=0;

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