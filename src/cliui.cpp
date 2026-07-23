#include "cliui.h"

CliUI::CliUI(
  AuthService& authService,
  FriendService& friendService,
  PrivateChatService& pcService,
  ClientContext& ctx
) : 
  authService_(authService),
  friendService_(friendService),
  pcService_(pcService),
  ctx_(ctx) {}

void CliUI::run() {
  show(ClientState::LOGIN);
  loginMenu();
  while(running_) {
    mainMenu();
  }
}

void CliUI::loginMenu() {
  while(true) {
    int choice = inputChoice(1,3);
    if(choice==3) {
      running_=false;
      return;
    }
    std::string username = inputString("用户名:");
    std::string password = inputString("密码:");

    bool ok=false;
    if(choice==1) {
      ok = authService_.login(username,password);
    } else {
      ok = authService_.regis(username,password);
    }
    if(ok) {
      return;
    }

  }
}

void CliUI::mainMenu() {
  show(ClientState::MAIN_MENU);

  switch(inputChoice(1,5)) {
    case 1:
      friendMenu();
      break;

    case 2:
      privateChat();
      break;

    case 3:
      break;

    case 4:
      break;

    case 5:
      running_=false;
      break;
  }
}

void CliUI::friendMenu() {
  while(true) {
    show(ClientState::FRIEND_MENU);

    switch(inputChoice(1,5)) {
      case 1:
        showFriendList();
        break;

      case 2:
        addFriend();
        break;

      case 3:
        deleteFriend();
        break;

      case 4:
        showFriendRequest();
        break;

      case 5:
        return;
    }
  }
}

void CliUI::showFriendList() {
  if(friendService_.listFriend() != 0) {
    std::cout<<"获取好友列表失败\n";
    return;
  }
  auto friends=ctx_.getFriendList();
  std::cout << "\n好友列表:\n";
  int i=0;
  for(auto &u:friends) {
    std::cout << ++i << "." << u.uid << " " << u.username << '\n';
  }
}

void CliUI::privateChat() {
  if(friendService_.listFriend() != 0) {
    std::cout<<"获取好友列表失败\n";
    return;
  }
  auto friends=ctx_.getFriendList();
  std::cout << "\n好友列表:\n";
  int i=0;
  for(auto &u:friends) {
    std::cout << ++i << "." << u.uid << " " << u.username << '\n';
  }
  std::cout << "请输入好友序号：" << std::endl;
  int choice = inputChoice(1,friends.size());
  auto frie = friends[choice];
  // int uid = frie.uid;
  // pcService_.syncHistory(frie.uid);
  // auto msgs = ctx_.getMessage(frie.uid);
  // for(auto msg : msgs) {
  //   std::cout << frie.username << ":" << msg.time << msg.content << std::endl;
  // }
  std::string input;
  while(input != "/exit") {
    std::cin >> input;
    if(input == "/exit") break;
    pcService_.sendPrivateMessage(frie.uid,input);
  }
}

void CliUI::addFriend() {
  int uid=std::stoi(inputString("请输入uid："));

  if(friendService_.request(uid) == 0) {
    std::cout<<"好友申请已发送\n";
  } else {
    std::cout<<"发送失败\n";
  }
}

void CliUI::deleteFriend() {
  int uid=std::stoi(inputString("请输入uid："));

  if(friendService_.del(uid) == 0) {
    std::cout<<"删除成功\n";
  } else {
    std::cout<<"删除失败\n";
  }
}

void CliUI::showFriendRequest() {
  if(friendService_.listRequest() != 0) {
    std::cout<<"获取好友申请失败\n";
    return;
  }
  auto list = ctx_.getFriendRequests();
  std::cout << "好友申请:" << std::endl;
  for(auto &u : list) {
    std::cout << u.uid << " " << u.username <<'\n';
  }

  while(true) {
    int uid=std::stoi(inputString("请输入uid(0退出)："));
    if(uid==0) return;
    std::cout
      <<"1.同意\n"
      <<"2.拒绝\n";

    int ch=inputChoice(1,2);
    if(ch==1) {
      friendService_.agree(uid);
    } else {
      friendService_.reject(uid);
    }
  }
}

void CliUI::show(ClientState state) {
  const std::vector<std::string>* menu=nullptr;
  std::string title;

  switch(state) {
    case ClientState::LOGIN:
      menu=&menu_login_;
      title="登录菜单";
      break;

    case ClientState::MAIN_MENU:
      menu=&menu_main_;
      title="主菜单";
      break;

    case ClientState::FRIEND_MENU:
      menu=&menu_friend_;
      title="好友管理";
      break;

    case ClientState::GROUP_MENU:
      menu=&menu_group_;
      title="群聊";
      break;

    default:
      return;
  }

  std::cout
    <<"\n====================\n"
    <<"      "<<title<<"\n"
    <<"====================\n\n";

  for(size_t i = 0;i < menu->size();i++) {
    std::cout
      <<i+1
      <<". "
      <<(*menu)[i]
      <<'\n';
  }

  std::cout<<"\n请选择：";
}

int CliUI::inputChoice(int min,int max) {
  int choice;
  while(true) {
    std::cin>>choice;

    if(std::cin.fail()) {
      std::cin.clear();
      std::cin.ignore(1024,'\n');
      continue;
    }

    if(choice>=min&&choice<=max) return choice;
    std::cout<<"请输入 "<<min<<"-"<<max<<"：";
  }
}

std::string CliUI::inputString(const std::string& prompt) {
  std::cout << prompt;
  std::string s;
  std::cin >> s;
  return s;
}