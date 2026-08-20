#include "socket.h"

TcpSocket::TcpSocket(int sockfd) : sockfd_(sockfd) {}

TcpSocket::TcpSocket(TcpSocket&& other) noexcept : sockfd_(other.sockfd_) {
  other.sockfd_ = -1;
}

TcpSocket& TcpSocket::operator=(TcpSocket&& other) noexcept {
  if(this != &other) {
    if(sockfd_ != -1) close(sockfd_);
    sockfd_ = other.sockfd_;
    other.sockfd_ = -1;
  }
  return *this;
}

TcpSocket::~TcpSocket() {
  close(sockfd_);
  sockfd_ = -1;
}

void TcpSocket::closefd() {
  if(sockfd_ != -1) {
    shutdown(sockfd_,SHUT_RDWR);
    close(sockfd_);
    sockfd_ = -1;
  }
}

std::string TcpSocket::localIp() const {
  sockaddr_in addr;
  socklen_t len = sizeof(addr);
  if(sockfd_ < 0) return "127.0.0.1";
  if(getsockname(sockfd_,(sockaddr*)&addr,&len) == -1) return "127.0.0.1";
  char buf[INET_ADDRSTRLEN];
  if(inet_ntop(AF_INET,&addr.sin_addr,buf,sizeof(buf)) == nullptr) return "127.0.0.1";
  return buf;
}

int TcpSocket::sendRaw(const void* data,size_t len) {
  std::lock_guard<std::mutex> lock(mtx_);
  const char* p = (const char*)data;
  size_t total = 0;
  while(total < len) {
    ssize_t n = send(sockfd_, p+total, len-total, 0);
    if(n > 0) total += (size_t)n;
    else if(n == 0) return -1;
    else {
      if(errno == EINTR) continue;
      return -1;
    }
  }
  return 0;
}

int TcpSocket::recvRaw(void* buf,size_t len) {
  while(true) {
    ssize_t n = recv(sockfd_, buf, len, 0);
    if(n > 0) return (int)n;
    if(n == 0) return 0;
    if(errno == EINTR) continue;
    return -1;
  }
}

std::shared_ptr<TcpSocket> TcpSocket::connect(const std::string& ip,unsigned short port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if(fd < 0) return nullptr;
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if(inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) { close(fd); return nullptr; }
  if(::connect(fd,(sockaddr*)&addr,sizeof(addr)) < 0) { close(fd); return nullptr; }
  return std::make_shared<TcpSocket>(fd);
}

int send_all(int fd,void *buf,size_t len) {
  size_t total=0;
  char *p=(char*)buf;
  while(total<len) {
    int n=send(fd,p+total,len-total,0);
    if(n>0) {
      total=total+n;
    } else if(n==0) {
      return total;
    } else {
      if (errno == EINTR) continue;
      return -1;
    }
  }
  return total;
}

NetResult TcpSocket::sendMsg(std::string msg) {
  std::lock_guard<std::mutex> lock(mtx_);
  uint32_t l=msg.size();
  uint32_t len=htonl(l);
  if(send_all(sockfd_,&len,sizeof(len))!=sizeof(len)) {
    return NetResult::SEND_ERROR;
  }
  
  if(send_all(sockfd_,msg.data(),msg.size())!=(int)msg.size()) {
    return NetResult::SEND_ERROR;
  }
  return NetResult::OK;
}

int recv_all(int fd,void *buf,size_t len) {
  size_t total=0;
  char *p=(char*)buf;
  while(total<len) {
    int n=recv(fd,p+total,len-total,0);
    if(n>0) {
      total=total+n;
    } else if(n==0) {
      return total;
    } else {
      if(errno == EINTR) continue;
      return -1;
    }
  }
  return total;
}

NetResult TcpSocket::recvMsg(std::string& msg) {
  msg.clear();
  uint32_t len=0;

  int ret=recv_all(sockfd_,&len,sizeof(len));
  if(ret==0) return NetResult::RECV_ERROR;
  if(ret!=sizeof(len)) return NetResult::RECV_ERROR;

  uint32_t l=ntohl(len);
  const uint32_t MAX_LEN=100*1024*1024;
  if(l>MAX_LEN) {
    return NetResult::RECV_ERROR;
  }
  msg.resize(l);
  if(l > 0) {
    ret=recv_all(sockfd_,&msg[0],l);
    if(ret!=(int)l) {
      close(sockfd_);
      sockfd_=-1;
      return NetResult::RECV_ERROR;
    }
  }
  return NetResult::OK;
}
