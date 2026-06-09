#pragma once
#include "socket.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/stat.h>
#include <termios.h>
#include <pthread.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <cstring>
#include <cstdlib>

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

class FtpSession {
public:

  FtpSession(std::unique_ptr<TcpSocket> sock) : ctrlSock_(std::move(sock)) {
    pasvReady_ = false;
  }

  void start();

private:

  bool run_cmd(std::vector<std::string> token);

  std::vector<std::string> gettoken(std::string input);

  bool doCWD(const std::string& s);
  bool doPASV();


private:
  std::unique_ptr<TcpSocket> ctrlSock_;
  std::unique_ptr<TcpSocket> pasv;
  TcpServer dataServer;
  std::filesystem::path cwd_;
  std::filesystem::path oldCwd_;
  bool pasvReady_;
};