#include "client.h"

TcpClient::TcpClient() : socket_(nullptr) {}

bool TcpClient::connectToHost(const char* ip, unsigned short port) {
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

  socket_=std::make_unique<TcpSocket>(fd);

  return true;
}