#pragma once
#include "socket.h"
#include <memory>

class TcpClient {
  public:
  TcpClient();

  TcpSocket* getSocket() const { return socket_.get(); }


  bool connectToHost(const char* ip, unsigned short port);

  private:
  std::unique_ptr<TcpSocket> socket_;
};
