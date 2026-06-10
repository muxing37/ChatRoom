#include "server.h"
#include <thread>

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

std::unique_ptr<TcpSocket> TcpServer::acceptConn() {
  sockaddr_in cliaddr;
  socklen_t len = sizeof(cliaddr);
  int fd = accept(listenfd_,(sockaddr*)&cliaddr,&len);
  std::cout << fd << std::endl;
  if(fd < 0) {
    return nullptr;
  }

  return std::make_unique<TcpSocket>(fd);
}

void FtpSession::start() {
  while(true) {
    std::string res;
    cwd_ = std::filesystem::current_path();
    oldCwd_ = cwd_;
    // auto path=std::filesystem::current_path();
    std::string now_path=cwd_.string();
    Msgpack n_path;
    n_path.type = MsgType::PATH_INFO;
    n_path.msg = now_path;
    ctrlSock_->sendMsgpack(n_path);
    ctrlSock_->recvMsg(res);
    if(res != "yes") {
      std::cout << res << std::endl;
      continue;
    }
    std::cout << res << std::endl;
    break;
  }
  while(true) {
    std::string msg;
    if(ctrlSock_->recvMsg(msg) != NetResult::OK) {
      std::cout << "[INFO] client disconnected or recv failed\n";
    }
    std::cout << "[INFO] recv: " << msg << "\n";

    std::vector<std::string> token;
    token=gettoken(msg);

    if(token.size()==0) {
      continue;
    } else if(token.size()==1 && (token[0] == "RETR" || token[0] == "STOR")) {
      ctrlSock_->sendMsg("请输入文件名");
      continue;
    } else {
      ctrlSock_->sendMsg("yes");
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

    if(token[0]=="cd" || token[0]=="CWD") {
      if(token.size()>2) {
        std::cout << "CWD: 参数太多" << std::endl;
      }
      doCWD(token[1]);
      continue;
    }

    if(token[0]=="exit" || token[0]=="QUIT") {
      signal(SIGCHLD,SIG_IGN);
      break;
    }
  }
}


bool FtpSession::run_cmd(std::vector<std::string> token) {
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
        
  // if(token[0]=="ls" || token[0]=="LIST") {
  //     std::vector<char*> argv;
  //     int argc=0;
  //     now_path=now_path+"/ls";
  //     argv.push_back(now_path.data());
  //     argc++;
  //     for(auto& s : token) {
  //         if(s=="ls" || s=="LIST") continue;
  //         if(s.size()>=2 && s.substr(0,2) == "./") {
  //             s.erase(0,2);
  //         }
  //         argv.push_back(s.data());
  //         argc++;
  //     }
  //     std::vector<std::string> ls_res;
  //     ls_res=startls(argc,argv.data(),cwd_);

  //     NetResult a;
  //     a = pasv->sendMsg("start_ls");

  //     for (const std::string& s : ls_res) {
  //         pasv->sendMsg(s);
  //     }
  //     pasv->sendMsg("stop");

  //     used = true;
  // }
  if(used) pasvReady_=false;
  else pasv->sendMsg("not used");
  return used;
}

std::vector<std::string> FtpSession::gettoken(std::string input) {
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

bool FtpSession::doCWD(const std::string& s) {
  namespace fs = std::filesystem;
  fs::path newPath;
  while(true) {
    if(s == "-") {
      if(oldCwd_.empty()) {
        std::cout << "OLDPWD not set\n";
        ctrlSock_->sendMsg("OLDPWD not set");
        return false;
      }
      std::cout << oldCwd_ << "\n";
      ctrlSock_->sendMsg("ok");
      std::swap(cwd_,oldCwd_);
      break;
    }

    if(!s.empty() && s[0] == '~') {
      const char* home = getenv("HOME");
      if(home == nullptr) {
        ctrlSock_->sendMsg("home == nullptr");
        return false;
      }
      newPath = fs::path(home);
      if(s.size() > 1) {
        newPath /= s.substr(1);
      }
    } else if(fs::path(s).is_absolute()) {
      newPath = fs::path(s);
    } else {
      newPath = cwd_ / s;
    }

    try {
      newPath = fs::weakly_canonical(newPath);
    }
    catch(...) {
      ctrlSock_->sendMsg("error");
      return false;
    }

    if(!fs::exists(newPath)) {
      std::cout << "No such file or directory\n";
      ctrlSock_->sendMsg("No such file or directory");
      return false;
    }

    if(!fs::is_directory(newPath)) {
      std::cout << "Not a directory\n";
      ctrlSock_->sendMsg("Not a directory");
      return false;
    }
    ctrlSock_->sendMsg("ok");
    oldCwd_ = cwd_;
    cwd_ = newPath;
    break;
  }

  while(true) {
    std::string res;
    // auto path=std::filesystem::current_path();
    std::string now_path=cwd_.string();
    Msgpack n_path;
    n_path.type = MsgType::PATH_INFO;
    n_path.msg = now_path;
    ctrlSock_->sendMsgpack(n_path);
    ctrlSock_->recvMsg(res);
    if(res != "yes") {
      std::cout << res << std::endl;
      continue;
    }
    std::cout << res << std::endl;
    break;
  }
  return true;
}

bool FtpSession::doPASV() {
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
    auto sock = server.acceptConn();
    if(!sock) {
      continue;
    } else {
      std::cout << "[PASS] client connected\n";
    }
    std::thread([sock = std::move(sock)]() mutable {
        FtpSession session(std::move(sock));
        session.start();
      }
    ).detach();
  }

  return 0;
}