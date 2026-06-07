#include "server.h"

TcpServer::TcpServer()
    : listenfd_(socket(AF_INET, SOCK_STREAM, 0)) {}

TcpServer::~TcpServer() {
  if (listenfd_ != -1) {
    close(listenfd_);
  }
}

bool TcpServer::setListen(unsigned short port) {
  sockaddr_in addr;
  memset (&addr,0,sizeof(addr));
  addr.sin_family=AF_INET;
  addr.sin_port=htons(port);
  addr.sin_addr.s_addr=INADDR_ANY;
  if(bind(listenfd_,(sockaddr*)&addr,sizeof(addr))==-1) {
    return false;
  }

  if(listen(listenfd_,SOMAXCONN)==-1) {
    return false;
  }
  return true;
}

unsigned short TcpServer::getPort() {
  sockaddr_in addr;
  socklen_t len = sizeof(addr);

  if(getsockname(listenfd_,(sockaddr*)&addr,&len) == -1) {
    return 0;
  }

  return ntohs(addr.sin_port);
}

std::unique_ptr<TcpSocket> TcpServer::acceptConn() {
  sockaddr_in cliaddr;
  socklen_t len = sizeof(cliaddr);
  int fd = accept(listenfd_,(sockaddr*)&cliaddr,&len);
std::cout << fd << std::endl;
  if(fd < 0) {
    return nullptr;
  }

  return std::make_unique<TcpSocket>(fd);
}

// bool TcpServer::acceptConn() {

//   sockaddr_in cliaddr;
//   socklen_t len=sizeof(cliaddr);

//   int connfd=accept(listenfd_,(sockaddr*)&cliaddr,&len);

//   if(connfd<0) {
//     return false;
//   }

//   socket_=std::make_unique<TcpSocket>(connfd);

//   return true;
// }
