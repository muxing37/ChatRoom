#pragma once
#include "socket.h"
#include <memory>

class TcpServer {
  public:
  TcpServer();
  ~TcpServer();

  unsigned short getPort();

  bool setListen(unsigned short port);

  std::unique_ptr<TcpSocket> acceptConn();

  private:
  int listenfd_;
};
