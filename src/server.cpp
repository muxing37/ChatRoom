#include "server.h"
#include "user.h"
#include "manager.h"

const std::string SAVEPATH = "./data";
const std::string USRDATA = SAVEPATH + "/usrdata.json";
const std::string FRIENDDATA = SAVEPATH + "/frienddata.json";

UsrManager usrManager;
SessionManager sessionManager;
FriendManager friendManager;
UidGenerator get_uid;

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
  std::shared_ptr<TcpSocket> ctrlSock_;
  std::shared_ptr<TcpSocket> pasv;
  TcpServer dataServer;
  std::filesystem::path cwd_;
  std::filesystem::path oldCwd_;
  bool pasvReady_;
};

TcpServer::TcpServer()
  : listenfd_(socket(AF_INET, SOCK_STREAM, 0)) {}

TcpServer::~TcpServer() {
  if (listenfd_ != -1) {
    close(listenfd_);
  }
}

bool TcpServer::setListen(unsigned short port) {
  sockaddr_in addr;
  memset(&addr,0,sizeof(addr));
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
  User* usr;
  std::string recv;
  while(true) {
    int uid;
    std::string res;
    if(ctrlSock_->recvMsg(res) != NetResult::OK) return;
    if(res.empty()) {
      NetResult ret = ctrlSock_->sendMsg("error");
      if(ret != NetResult::OK) {
        // exit(0);
        return;
      } else {
        continue;
      }
    }
    nlohmann::json j = nlohmann::json::parse(res);
    // std::cout << res << std::endl;
    if(j["type"] != "user") {
      ctrlSock_->sendMsg("非预期的请求");
      continue;
    }
    if(j["action"] == "register") {
      if(usrManager.isExist(j["username"])) {
        ctrlSock_->sendMsg("error");
        continue;
      } else {
        uid = get_uid.get();
      }

      if(usrManager.regis(j["username"],j["password"],uid)) {
        ctrlSock_->sendMsg(std::to_string(uid));
        usr = usrManager.getUser(uid);
        usrManager.save(USRDATA);
        break;
      } else {
        ctrlSock_->sendMsg("error");
        continue;
      }
      // sessionManager.bindUser(usr.uid,ctrlSock_);
    } else if(j["action"] == "login") {
      if(usrManager.login(j["username"],j["password"],uid)) {
        ctrlSock_->sendMsg(std::to_string(uid));
        usr = usrManager.getUser(uid);
        break;
      } else {
        ctrlSock_->sendMsg("error");
        continue;
      }
    } else {
      ctrlSock_->sendMsg("非预期的请求");
    }
    // break;
  }
  sessionManager.bindUser(usr->uid,ctrlSock_);
  while(true) {
    // std::cout << usr->uid << std::endl;
    // std::cout << usr->username << std::endl;
    if(ctrlSock_->recvMsg(recv) != NetResult::OK) {
      std::cout << "[INFO] client disconnected or recv failed\n";
      break;
    }
    std::cout << "recv: " << recv << "\n";
    // sessionManager.forEach();
    if(recv.empty()) continue;
    nlohmann::json j = nlohmann::json::parse(recv);
    if(j["type"] == "friend") {
      int self_uid = std::stoi(j["from_uid"].get<std::string>());
      nlohmann::json reply;
      reply["type"] = "reply";
      reply["action"] = j["action"];
      if(j["action"] == "request") {
        int to_uid = std::stoi(j["to_uid"].get<std::string>());
        reply["status"] = friendManager.request(to_uid, self_uid);
        ctrlSock_->sendMsg(reply.dump());
      } else if(j["action"] == "del") {
        int to_uid = std::stoi(j["to_uid"].get<std::string>());
        friendManager.del(self_uid, to_uid);
        reply["status"] = 0;
        ctrlSock_->sendMsg(reply.dump());
      } else if(j["action"] == "agree") {
        int to_uid = std::stoi(j["to_uid"].get<std::string>());
        friendManager.agree(self_uid, to_uid);
        reply["status"] = 0;
        ctrlSock_->sendMsg(reply.dump());
      } else if(j["action"] == "reject") {
        int to_uid = std::stoi(j["to_uid"].get<std::string>());
        friendManager.reject(self_uid, to_uid);
        reply["status"] = 0;
        ctrlSock_->sendMsg(reply.dump());
      } else if(j["action"] == "list_friend") {
        reply["status"] = 0;
        auto list = friendManager.list_friend(self_uid);
        reply["friends"] = nlohmann::json::array();
        for(int uid : list) {
          User *u = usrManager.getUser(uid);
          if(!u) continue;
          reply["friends"].push_back({
            {"uid", u->uid},
            {"username", u->username}
          });
        }
        ctrlSock_->sendMsg(reply.dump());
      } else if(j["action"] == "check_request") {
        reply["status"] = 0;
        auto list = friendManager.list_request(self_uid);
        reply["requests"] = nlohmann::json::array();
        for(int uid : list) {
          User *u = usrManager.getUser(uid);
          if (!u) continue;
          reply["requests"].push_back({
            {"uid", u->uid},
            {"username", u->username}
          });
        }
        ctrlSock_->sendMsg(reply.dump());
      }
      else {
        reply["status"] = -1;
        reply["message"] = "unknown action";

        ctrlSock_->sendMsg(reply.dump());
      }
      continue;
    }
    std::vector<std::string> token;
    // token=gettoken(msg);

    if(token.size()==0) {
      continue;
    } else if(token.size()==1 && (token[0] == "RETR" || token[0] == "STOR")) {
      ctrlSock_->sendMsg("请输入文件名");
      continue;
    } else {
      // ctrlSock_->sendMsg("yes");
      // continue;
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

    if(token[0]=="/exit" || token[0]=="QUIT") {
      // signal(SIGCHLD,SIG_IGN);
      // std::cout << "111\n";
      ctrlSock_->~TcpSocket();
      sessionManager.unbindUser(usr->uid);
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
  std::filesystem::create_directories(SAVEPATH);

  usrManager.load(USRDATA);
  friendManager.load(FRIENDDATA);
  int max_uid = usrManager.getMaxUid();
  get_uid.init(max_uid + 1);
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
    std::thread([sock = std::move(sock)]() 
      mutable {
        Session session(std::move(sock));
        session.start();
      }
    ).detach();
  }

  return 0;
}
