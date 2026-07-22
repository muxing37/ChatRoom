#include "client.h"
#include "service.h"

#define MAX_PATH 1024

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

class TcpClient {
  public:
  TcpClient();

  std::shared_ptr<TcpSocket> getSocket() const { return socket_; }

  bool connectToHost(const char* ip, unsigned short port);

  private:
  std::shared_ptr<TcpSocket> socket_;
};

CliUI::CliUI(AuthService& authService,FriendService& friendService,ClientContext& ctx) : authService_(authService),friendService_(friendService),ctx_(ctx) {}

void CliUI::run() {
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
  show(ClientState::LOGIN);
  loginMenu();
  show(ClientState::MAIN_MENU);

  switch(inputChoice(1,5)) {
    case 1:
      friendMenu();
      break;

    case 2:
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
  std::cout<<"\n好友列表:\n";

  for(auto &u:friends) {
    std::cout <<u.uid<<" " <<u.username <<'\n';
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
    std::cout << u.uid << " " <<u.username <<'\n';
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

TcpClient::TcpClient() : socket_(nullptr) {}

bool TcpClient::connectToHost(const char* ip,unsigned short port) {
  int fd=socket(AF_INET,SOCK_STREAM,0);

  if(fd<0) {
    return false;
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);

  inet_pton(AF_INET,ip,&addr.sin_addr);

  if(connect(fd,(sockaddr*)&addr,sizeof(addr)) < 0) {
    close(fd);
    return false;
  }

  socket_=std::make_shared<TcpSocket>(fd);
  return true;
}

int start_client() {

  TcpClient client;
  if(!client.connectToHost("127.0.0.1", 2100)) {
    std::cerr << "[FAIL] connectToHost failed\n";
    return 1;
  }
  std::cout << "[PASS] connected to server\n";
  auto sock = client.getSocket();
  // auto sock = std::make_shared<TcpSocket>(fd);
  if(!sock) {
    std::cerr << "[FAIL] socket null\n";
    return 1;
  }
  std::string workpath=std::string(getenv("HOME")) + "/Download";
  mkdir(workpath.c_str(),0755);
  chdir(workpath.c_str());

  bool pasving=false;
  TcpClient dataClient;
  TcpSocket* pasv;

ClientContext ctx;
ClientNetwork network(sock);
AuthService authService(network,ctx);
FriendService friendService(network,ctx);
CliUI menu(authService,friendService,ctx);
  network.start();

  menu.run();
  
  return 0;
}
