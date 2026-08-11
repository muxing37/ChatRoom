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
#include <mutex>

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
  TcpSocket(TcpSocket&& other) noexcept;
  TcpSocket& operator=(TcpSocket&& other) noexcept;
  TcpSocket(const TcpSocket&) = delete;
  TcpSocket& operator=(const TcpSocket&) = delete;
  ~TcpSocket();

  void closefd();
  NetResult sendMsg(std::string msg);
  NetResult recvMsg(std::string& msg);
  // 原始字节收发（文件数据通道用）
  int sendRaw(const void* data,size_t len);
  int recvRaw(void* buf,size_t len);

  std::string localIp() const;

  static std::shared_ptr<TcpSocket> connect(const std::string& ip,unsigned short port);

private:
  int sockfd_;
  std::mutex mtx_;
};
