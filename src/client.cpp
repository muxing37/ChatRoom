#include "client.h"
#include "service.h"
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
