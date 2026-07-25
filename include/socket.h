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
#include <thread>

enum class NetResult {
  OK,
  TIMEOUT,
  SEND_ERROR,
  RECV_ERROR,
  FILE_ERROR
};

class TcpSocket {
public:
  TcpSocket(int sockfd);
  ~TcpSocket();

  void closefd();
  NetResult sendMsg(std::string msg);
  NetResult recvMsg(std::string& msg);

  NetResult sendFile(std::string& path,uint64_t offset);
  NetResult recvFile(std::string& path,uint64_t offset);

private:
  int sockfd_;
};
