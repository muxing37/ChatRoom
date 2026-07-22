#include "service.h"

enum class ClientState {
  LOGIN,
  MAIN_MENU,
  FRIEND_MENU,
  GROUP_MENU
};

class CliUI {
public:
  CliUI(AuthService& authService,FriendService& friendService,ClientContext& ctx);
  void run();

private:
  void loginMenu();
  void mainMenu();
  void friendMenu();

  void showFriendList();
  void addFriend();
  void deleteFriend();
  void showFriendRequest();

  void show(ClientState state);
  int inputChoice(int min,int max);
  std::string inputString(const std::string& prompt);

private:
  AuthService& authService_;
  FriendService& friendService_;
  ClientContext& ctx_;
  bool running_=true;

private:
  const std::vector<std::string> menu_login_={
    "登录",
    "注册",
    "退出"
  };
  const std::vector<std::string> menu_main_={
    "好友管理",
    "私聊",
    "群聊",
    "个人信息",
    "退出登录"
  };
  const std::vector<std::string> menu_friend_={
    "查看好友列表",
    "添加好友",
    "删除好友",
    "查看好友申请",
    "返回主菜单"
  };
  const std::vector<std::string> menu_group_={
    "查看群聊",
    "创建群聊",
    "加入群聊",
    "退出群聊",
    "返回主菜单"
  };
};