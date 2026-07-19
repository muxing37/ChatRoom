#include "socket.h"

TcpSocket::TcpSocket(int sockfd) : sockfd_(sockfd) {}

TcpSocket::~TcpSocket() {
  close(sockfd_);
  sockfd_ = -1;
}

uint64_t htonll(uint64_t num) {
  uint32_t high = htonl((uint32_t)(num >> 32));
  uint32_t low  = htonl((uint32_t)(num & 0xFFFFFFFF));

  return ((uint64_t)low << 32) | high;
}

uint64_t ntohll(uint64_t num) {
  uint32_t high = ntohl((uint32_t)(num >> 32));
  uint32_t low  = ntohl((uint32_t)(num & 0xFFFFFFFF));

  return ((uint64_t)low << 32) | high;
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
  uint32_t l=msg.size();
  uint32_t len=htonl(l);
  std::cout << "[SEND fd=" << sockfd_ << "] " << msg << std::endl;
  if(send_all(sockfd_,&len,sizeof(len))!=sizeof(len)) {
    return NetResult::SEND_ERROR;
  }
  
  if(send_all(sockfd_,msg.data(),msg.size())!=(int)msg.size()) {
    return NetResult::SEND_ERROR;
  }
  return NetResult::OK;
}

NetResult  TcpSocket::sendMsgpack(Msgpack& msg) {
  uint32_t type = static_cast<uint32_t>(msg.type);
  uint32_t netType = htonl(type);
  if(send_all(sockfd_,&netType,sizeof(netType)) != sizeof(netType)) {
    return NetResult::SEND_ERROR;
  }

  uint32_t len = msg.msg.size();
  uint32_t netLen = htonl(len);
  if(send_all(sockfd_,&netLen,sizeof(netLen)) != sizeof(netLen)) {
    return NetResult::SEND_ERROR;
  }

  if(len > 0) {
    if(send_all(sockfd_,msg.msg.data(),len) != (int)len) {
      return NetResult::SEND_ERROR;
    }
  }

  return NetResult::OK;
}

NetResult TcpSocket::sendFile(std::string& path,uint64_t offset) {
  int fd = open(path.c_str(),O_RDONLY);
  if(fd < 0) {
    return NetResult::FILE_ERROR;
  }
  struct stat st;
  if(fstat(fd, &st) == -1) {
    close(fd);
    return NetResult::FILE_ERROR;
  }

  uint64_t filesize = st.st_size;

  if(offset > filesize) {
    close(fd);
    return NetResult::FILE_ERROR;
  }

  if(offset > 0) {
    lseek(fd, offset, SEEK_SET);
  }

  uint64_t remainSize = filesize - offset;
  uint64_t netSize = htonll(remainSize);

  if(send_all(sockfd_,&netSize,sizeof(netSize))!= sizeof(netSize)) {
    close(fd);
    return NetResult::SEND_ERROR;
  }
  char buf[4096];
  auto start = std::chrono::steady_clock::now();
  uint64_t sent = 0;
  while(true) {
    int n = read(fd,buf,sizeof(buf));
    if(n>0) {
      if(send_all(sockfd_,buf,n) != n) {
        close(fd);
        return NetResult::SEND_ERROR;
      }
    } else if(n==0) {
      break;
    } else {
      close(fd);
      return NetResult::SEND_ERROR;
    }
    sent += n;

    double percent = 100.0 * (offset + sent) / filesize;
    auto now = std::chrono::steady_clock::now();
    double seconds = std::chrono::duration<double>(now-start).count();
    double speed = sent / 1024.0 / 1024.0 / seconds;
    std::cout << "\rUploading: " << std::fixed << std::setprecision(1) << percent << "% " << speed << " MB/s" << std::flush;
 
  }
  std::cout << "\n";
  close(fd);
  return NetResult::OK;
}

int recv_all(int fd,void *buf,size_t len) {
  size_t total=0;
  char *p=(char*)buf;
  while(total<len) {
    int n=recv(fd,p+total,len-total,0);
    std::cout << "need=" << len << " total=" << total << " recv=" << n << std::endl;
    if(n>0) {
      total=total+n;
    } else if(n==0) {
      std::cout << "peer closed\n";
      return total;
    } else {
      std::cout << "errno=" << errno << std::endl;
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
  std::cout << "raw len = " << len << std::endl;
  if(ret==0) return NetResult::RECV_ERROR;
  if(ret!=sizeof(len)) return NetResult::RECV_ERROR;
  
  uint32_t l=ntohl(len);

  std::cout << "host len = " << l << std::endl;

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

  std::cout << "[RECV fd=" << sockfd_ << "] " << "\"" << msg << "\"" << std::endl;

  std::cout << "data: ";
  for(unsigned char c : msg) printf("%02X ", c);
  std::cout << std::endl;

  return NetResult::OK;
}

NetResult TcpSocket::recvMsgpack(Msgpack& msg) {
  uint32_t netType=0;
  int ret = recv_all(sockfd_,&netType,sizeof(netType));
  if(ret != sizeof(netType)) return NetResult::RECV_ERROR;
  msg.type=static_cast<MsgType>(ntohl(netType));

  uint32_t netLen = 0;
  ret = recv_all(sockfd_,&netLen,sizeof(netLen));
  if(ret != sizeof(netLen)) return NetResult::RECV_ERROR;
  uint32_t len = ntohl(netLen);

  const uint32_t MAX_LEN = 100*1024*1024;
  if(len > MAX_LEN) return NetResult::RECV_ERROR;

  msg.msg.resize(len);
  if(len > 0) {
    ret = recv_all(sockfd_,&msg.msg[0],len);
    if(ret != (int)len) return NetResult::RECV_ERROR;
  }

  return NetResult::OK;
}

NetResult TcpSocket::recvFile(std::string& path,uint64_t offset) {
  int fd = open(path.c_str(), O_WRONLY | O_CREAT, 0644);
  if(fd < 0) {
    return NetResult::RECV_ERROR;
  }
  if(offset > 0) {
    lseek(fd, offset, SEEK_SET);
  }

  uint64_t netSize = 0;
  int ret = recv_all(sockfd_, &netSize, sizeof(netSize));

  if(ret != sizeof(netSize)) {
    close(fd);
    return NetResult::RECV_ERROR;
  }

  uint64_t remain = ntohll(netSize);

  std::vector<char> buf(1024 * 1024);
  auto start = std::chrono::steady_clock::now();

  uint64_t received = 0;
  uint64_t totalSize = offset + ntohll(netSize);

  while(remain > 0) {
    int chunk = remain > buf.size() ? buf.size() : remain;
    int n = recv_all(sockfd_, buf.data(), chunk);

    if(n <= 0) {
      close(fd);
      return NetResult::RECV_ERROR;
    }

    int written = write(fd, buf.data(), n);
    if(written != n) {
      close(fd);
      return NetResult::RECV_ERROR;
    }
    remain -= n;

    received += n;
    double percent = 100.0 * (offset + received) / totalSize;
    auto now = std::chrono::steady_clock::now();
    double seconds = std::chrono::duration<double>(now - start).count();
    double speed = received / 1024.0 / 1024.0 / seconds;
    std::cout << "\rDownloading: " << std::fixed << std::setprecision(1) << percent << "% " << speed << " MB/s" << std::flush;
  }
  std::cout << "\n";
  close(fd);
  return NetResult::OK;
}
