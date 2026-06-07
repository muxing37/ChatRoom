#pragma once
#include <nlohmann/json.hpp>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <string>
#include <chrono>
#include <sys/stat.h>
#include <vector>
#include <iomanip>
#include <fcntl.h>

enum class NetResult {
  OK,
  TIMEOUT,
  SEND_ERROR,
  RECV_ERROR,
  FILE_ERROR
};

enum class MsgType {
  CMD,
  PATH_INFO,
  INFO
};

struct Msgpack {
  MsgType type;
  std::string msg;
};

class TcpSocket {
  public:
  TcpSocket(int sockfd);
  ~TcpSocket();

  NetResult sendMsg(std::string msg);
  NetResult recvMsg(std::string& msg);

  NetResult sendMsgpack(Msgpack& msg);
  NetResult recvMsgpack(Msgpack& msg);

  NetResult sendFile(std::string& path,uint64_t offset);
  NetResult recvFile(std::string& path,uint64_t offset);

  private:
  int sockfd_;
};
