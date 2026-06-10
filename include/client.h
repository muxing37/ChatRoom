#pragma once
#include "socket.h"
#include <memory>
#include <fstream>
#include <string>
#include <signal.h>
#include <readline/readline.h>
#include <readline/history.h>

class TcpClient {
  public:
  TcpClient();

  TcpSocket* getSocket() const { return socket_.get(); }

  bool connectToHost(const char* ip, unsigned short port);

  private:
  std::unique_ptr<TcpSocket> socket_;
};

int start_client();