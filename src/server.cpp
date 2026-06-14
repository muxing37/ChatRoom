#include "server.h"
#include "user.h"

class UidGenerator {
public:
  int get() {
    return counter_.fetch_add(1);
  }

private:
  std::atomic<int> counter_{10000};
};

// class SessionManager {
// public:
//   void bindUser(int user_id,int fd);
//   void unbindUser(int user_id);
//   int getFd(int user_id);

// private:
//   std::unordered_map<int, int> user_to_fd_;
//   std::mutex mtx_;
// };
class SessionManager {
public:
  void bindUser(int user_id, std::shared_ptr<TcpSocket> sock);
  void unbindUser(int user_id);
  std::shared_ptr<TcpSocket> getSock(int user_id);

  template<typename Func>
  void forEach(Func&& func);

private:
  std::unordered_map<int, std::shared_ptr<TcpSocket>> user_to_sock_;
  std::mutex mtx_;
};

UidGenerator get_uid;
SessionManager sessionManager;

class TcpServer {
  public:
  TcpServer();
  ~TcpServer();

  unsigned short getPort();

  bool setListen(unsigned short port);

  std::shared_ptr<TcpSocket> acceptConn();

  private:
  int listenfd_;
};

class Session {
public:

  Session(std::shared_ptr<TcpSocket> sock) : ctrlSock_(std::move(sock)) {
    pasvReady_ = false;
  }

  void start();

private:
  bool run_cmd(std::vector<std::string> token);

  std::vector<std::string> gettoken(std::string input);

  bool doPASV();

private:
// int fd_;
  std::shared_ptr<TcpSocket> ctrlSock_;
  std::shared_ptr<TcpSocket> pasv;
  TcpServer dataServer;
  std::filesystem::path cwd_;
  std::filesystem::path oldCwd_;
  bool pasvReady_;
};

// void SessionManager::bindUser(int user_id,int fd) {
//   std::lock_guard<std::mutex> lock(mtx_);
//   user_to_fd_[user_id] = fd;
// }

void SessionManager::unbindUser(int user_id) {
  std::lock_guard<std::mutex> lock(mtx_);
  user_to_sock_.erase(user_id);
}

// int SessionManager::getFd(int user_id) {
//   std::lock_guard<std::mutex> lock(mtx_);
//   auto it = user_to_fd_.find(user_id);
//   if (it == user_to_fd_.end()) return -1;
//   return it->second;
// }
void SessionManager::bindUser(int user_id,std::shared_ptr<TcpSocket> sock) {
  std::lock_guard<std::mutex> lock(mtx_);
  user_to_sock_[user_id] = std::move(sock);
}

std::shared_ptr<TcpSocket> SessionManager::getSock(int user_id) {
  std::lock_guard<std::mutex> lock(mtx_);
  auto it = user_to_sock_.find(user_id);
  if (it == user_to_sock_.end()) return nullptr;
  return it->second;
}

template<typename Func>
void SessionManager::forEach(Func&& func) {
  std::lock_guard<std::mutex> lock(mtx_);
  for (auto& [uid, sock] : user_to_sock_) {
    func(uid, sock);
  }
}

TcpServer::TcpServer()
  : listenfd_(socket(AF_INET, SOCK_STREAM, 0)) {}

TcpServer::~TcpServer() {
  if (listenfd_ != -1) {
    close(listenfd_);
  }
}

bool TcpServer::setListen(unsigned short port) {
  sockaddr_in addr;
  memset (&addr,0,sizeof(addr));
  addr.sin_family=AF_INET;
  addr.sin_port=htons(port);
  addr.sin_addr.s_addr=INADDR_ANY;
  if(bind(listenfd_,(sockaddr*)&addr,sizeof(addr))==-1) {
    return false;
  }

  if(listen(listenfd_,SOMAXCONN)==-1) {
    return false;
  }
  return true;
}

unsigned short TcpServer::getPort() {
  sockaddr_in addr;
  socklen_t len = sizeof(addr);

  if(getsockname(listenfd_,(sockaddr*)&addr,&len) == -1) {
    return 0;
  }

  return ntohs(addr.sin_port);
}

std::shared_ptr<TcpSocket> TcpServer::acceptConn() {
  sockaddr_in cliaddr;
  socklen_t len = sizeof(cliaddr);
  int fd = accept(listenfd_,(sockaddr*)&cliaddr,&len);
  std::cout << fd << std::endl;
  if(fd < 0) {
    return nullptr;
  }

  return std::make_unique<TcpSocket>(fd);
}

void Session::start() {
  User usr;
  std::string msg;
  while(true) {
    // nlohmann::json j;
    std::string res;
    ctrlSock_->recvMsg(res);
    nlohmann::json j = nlohmann::json::parse(res);
    usr.uid = get_uid.get();
    ctrlSock_->sendMsg(std::to_string(usr.uid));
    sessionManager.bindUser(usr.uid,ctrlSock_);
    break;
  }
  
  while(true) {
    if(ctrlSock_->recvMsg(msg) != NetResult::OK) {
      std::cout << "[INFO] client disconnected or recv failed\n";
    }
    std::cout << "[INFO] recv: " << msg << "\n";
    // sessionManager.forEach();
    sessionManager.forEach([&](int uid, auto sock){
      sock->sendMsg(msg);
    });
    std::vector<std::string> token;
    token=gettoken(msg);

    if(token.size()==0) {
      continue;
    } else if(token.size()==1 && (token[0] == "RETR" || token[0] == "STOR")) {
      ctrlSock_->sendMsg("请输入文件名");
      continue;
    } else {
      // ctrlSock_->sendMsg("yes");
      continue;
    }

    if(token[0]=="PASV" || pasvReady_==true) {
      if(pasvReady_==false) {
        doPASV();
        continue;
      }
      if(pasvReady_==true) {
        if(run_cmd(token)) continue;
      }
    }

    // if(token[0]=="cd" || token[0]=="CWD") {
    //   if(token.size()>2) {
    //     std::cout << "CWD: 参数太多" << std::endl;
    //   }
    //   doCWD(token[1]);
    //   continue;
    // }

    if(token[0]=="exit" || token[0]=="QUIT") {
      signal(SIGCHLD,SIG_IGN);
      break;
    }
  }
}

bool Session::run_cmd(std::vector<std::string> token) {
  bool used = false;
  std::string now_path=cwd_.string();
  int status;
  if(token[0]=="STOR") {
    pasv->sendMsg("start_stor");
    std::string path=now_path;
    if(token[1].size()>=2 && token[1].substr(0,2) == "./") {
      token[1].erase(0,2);
      path += "/" + token[1];
    } else if (token[1].size()>=1 && token[1].substr(0,1) == "/") {
      path.clear();
      path = token[1];
    } else {
      path += "/" + token[1];
    }
    uint64_t offset = 0;
    struct stat st;
    if(stat(path.c_str(), &st) == 0) {
      offset = st.st_size;
    }

    pasv->sendMsg(token[1] + "|" + std::to_string(offset));

    pasv->recvFile(path,offset);
    used = true;
  }

  if(token[0]=="RETR") {
    pasv->sendMsg("start_retr");
    std::string path=now_path;
    if(token[1].size()>=2 && token[1].substr(0,2) == "./") {
      token[1].erase(0,2);
      path += "/" + token[1];
    } else if (token[1].size()>=1 && token[1].substr(0,1) == "/") {
      path.clear();
      path = token[1];
    } else {
      path += "/" + token[1];
    }
    struct stat st;
    if(stat(path.c_str(), &st) != 0) {
      pasv->sendMsg("error:file_not_found");
      return false;
    } else {
      pasv->sendMsg("ok");
    }
    pasv->sendMsg(path);
    uint64_t filesize = st.st_size;
    std::string offsetMsg;
    pasv->recvMsg(offsetMsg);

    uint64_t offset = std::stoull(offsetMsg);

    if(offset > filesize) {
      offset = 0;
    }

    pasv->sendFile(path,offset);
    used = true;
  }
  if(used) pasvReady_=false;
  else pasv->sendMsg("not used");
  return used;
}

std::vector<std::string> Session::gettoken(std::string input) {
  std::vector<std::string> token;
  std::string current;

  for(char c : input) {
    if(c == ' ') {
      if(!current.empty()) {
        token.push_back(current);
        current.clear();
      }
    } else {
      current += c;
    }
  }
  if(!current.empty()) {
    token.push_back(current);
  }
  return token;
}

bool Session::doPASV() {
  sockaddr_in addr;
  if(!dataServer.setListen(0)) {
    std::cerr << "data listen failed\n";
  }

  std::cout << "[INFO] data listening...\n";
  unsigned short dataPort = dataServer.getPort();
  if(dataPort != 0) {
    int p1 = dataPort/256;
    int p2 = dataPort%256;
    std::string reply = "227 entering passive mode (127,0,0,1," 
      + std::to_string(p1) + "," + std::to_string(p2) + ")";

    ctrlSock_->sendMsg(reply);
  }

  pasv = dataServer.acceptConn();

  std::cout << "[PASS] client connected\n";
  std::cout << "[DATA] waiting data connection...\n";

  pasvReady_ = true;
  return true;
}


int start_server() {
  chdir(getenv("HOME"));
  TcpServer server;

  if(!server.setListen(2100)) {
    std::cerr << "[FAIL] setListen failed\n";
    return 1;
  }
  std::cout << "[INFO] server listening on port 2100...\n";

  while(true) {
    std::cout << "[INFO] server listening on port 2100...\n";
    auto sock = server.acceptConn();
    if(!sock) {
      continue;
    } else {
      std::cout << "[PASS] client connected\n";
    }
    std::thread([sock = std::move(sock)]() mutable {
        Session session(std::move(sock));
        session.start();
      }
    ).detach();
  }

  return 0;
}
