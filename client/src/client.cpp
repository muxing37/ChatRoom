#include "client.h"
#include "service.h"
#include "context.h"
#include "webui.h"
#include "cliui.h"

#define MAX_PATH 1024

class TcpClient {
public:
  TcpClient();
  std::shared_ptr<TcpSocket> getSocket() const { return socket_; }
  bool connectToHost(const char* ip, unsigned short port);

private:
  std::shared_ptr<TcpSocket> socket_;
};

TcpClient::TcpClient() : socket_(nullptr) {}

bool TcpClient::connectToHost(const char* ip,unsigned short port) {
  int fd = socket(AF_INET,SOCK_STREAM,0);

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

  socket_ = std::make_shared<TcpSocket>(fd);
  return true;
}

void dispatchPush(
  const nlohmann::json& push,
  FriendService& friendService,
  ChatService& chatService,
  GroupService& groupService
) {
  auto type = push.value("type","");
  try {
    if(type == "chat") {
      chatService.handlePush(push);
    } else if(type == "friend") {
      friendService.handlePush(push);
    } else if(type == "group") {
      groupService.handlePush(push);
    }
  } catch(const std::exception& e) {
    LOG(ERROR) << "推送处理异常: " << e.what();
  }
}

int start_client(const std::string& ip,unsigned short port) {
  TcpClient client;
  if(!client.connectToHost(ip.c_str(),port)) {
    LOG(ERROR) << "connectToHost failed";
    return 1;
  }
  LOG(INFO) << "connected to server";
  auto sock = client.getSocket();
  if(!sock) {
    LOG(ERROR) << "socket null";
    return 1;
  }
  std::string workpath=std::string(getenv("HOME")) + "/Download";
  mkdir(workpath.c_str(),0755);

  ClientContext ctx;
  ClientNetwork network(sock);
  AuthService authService(network,ctx);
  FriendService friendService(network,ctx);
  ChatService chatService(network,ctx);
  GroupService groupService(network,ctx);
  FileService fileService(network,ctx);

  bool usecli = (getenv("CHAT_CLI") != nullptr);
  network.start();

  if(usecli) {
    CliUI cli(authService,friendService,chatService,groupService,fileService,ctx);
    network.setPushHandler([&](const nlohmann::json& push) {
      dispatchPush(push,friendService,chatService,groupService);
      cli.notifyPush();
    });
    cli.run();
    network.stop();
  } else {
    WebUI web(authService,friendService,chatService,groupService,fileService,ctx);
    network.setPushHandler([&](const nlohmann::json& push) {
      dispatchPush(push,friendService,chatService,groupService);
      web.broadcast(push);
    });
    web.run("localhost");
  }
  return 0;
}
