#include "server.h"
#include "shared.h"
#include "manager.h"

const std::string SAVEPATH = "./data";
const std::string USRDATA = SAVEPATH + "/usrdata.json";
const std::string FRIENDDATA = SAVEPATH + "/frienddata.json";

UsrManager usrManager;
SessionManager sessionManager;
FriendManager friendManager;
MessageManager messageManager;
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
  void authServer();
  void friendSever(nlohmann::json j);
  void chatServer(nlohmann::json j);
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

nlohmann::json makeReply(const nlohmann::json& j,int status) {
  nlohmann::json reply;
  reply["msg_type"]="reply";
  reply["request_id"] = j["request_id"];
  reply["status"]=status;
  reply["time"] =time(nullptr);
  return reply;
}

void Session::authServer() {
  User* usr;
  while(true) {
    std::string res;
    if(ctrlSock_->recvMsg(res)!=NetResult::OK) return;
    if(res.empty()) continue;

    auto j = nlohmann::json::parse(res);

    if(j["type"]!="user") {
      auto reply=makeReply(j,1);
      reply["error"]="invalid type";
      ctrlSock_->sendMsg(reply.dump());
      continue;
    }
    nlohmann::json reply= makeReply(j,1);
    int uid;
    if(j["action"]=="register") {
      std::string username = j["data"]["username"];
      std::string password = j["data"]["password"];
      if(usrManager.isExist(username)) {
        reply["error"] ="username exists";
        ctrlSock_->sendMsg(reply.dump());
        continue;
      }
      uid=get_uid.get();
      if(!usrManager.regis(username,password,uid)) {
        reply["error"] = "register failed";
        ctrlSock_->sendMsg(reply.dump());
        continue;
      }
      usr = usrManager.getUser(uid);
      usrManager.save(USRDATA);
      reply["status"]=0;
      reply["data"]={
        {"uid",uid},
        {"username",username}
      };
      ctrlSock_->sendMsg(reply.dump());
      break;
    } else if(j["action"] == "login") {
      std::string username = j["data"]["username"];
      std::string password = j["data"]["password"];
      if(!usrManager.login(username,password,uid)) {
        reply["error"] ="username or password wrong";
        ctrlSock_->sendMsg(reply.dump());
        continue;
      }
      usr = usrManager.getUser(uid);
      reply["status"]=0;
      reply["data"]={
        {"uid",uid},
        {"username",username}
      };
      ctrlSock_->sendMsg(reply.dump());
      break;
    } else {
      ctrlSock_->sendMsg(reply.dump());
    }
  }
  sessionManager.bindUser(usr->uid,ctrlSock_);
}

void Session::friendSever(nlohmann::json j) {
  int self_uid = j["data"]["from_uid"];
  // nlohmann::json reply;
  auto reply=makeReply(j,0);
  reply["type"] = "reply";
  reply["action"] = j["action"];
  if(j["action"] == "request") {
    int to_uid = j["data"]["to_uid"];
    reply["status"] = friendManager.request(to_uid,self_uid);
    ctrlSock_->sendMsg(reply.dump());
  } else if(j["action"] == "del") {
    int to_uid = j["data"]["to_uid"];
    friendManager.del(self_uid,to_uid);
    reply["status"] = 0;
    ctrlSock_->sendMsg(reply.dump());
  } else if(j["action"] == "agree") {
    int to_uid = j["data"]["to_uid"];
    friendManager.agree(self_uid,to_uid);
    reply["status"] = 0;
    ctrlSock_->sendMsg(reply.dump());
  } else if(j["action"] == "reject") {
    int to_uid = j["data"]["to_uid"];
    friendManager.reject(self_uid,to_uid);
    reply["status"] = 0;
    ctrlSock_->sendMsg(reply.dump());
  } else if(j["action"] == "list_friend") {
    reply["status"] = 0;
    auto list = friendManager.list_friend(self_uid);
    for(int uid : list) {
      User *u = usrManager.getUser(uid);
      if(!u) continue;
      reply["data"]["friends"].push_back({
        {"uid",u->uid},
        {"username",u->username}
      });
    }
    ctrlSock_->sendMsg(reply.dump());
  } else if(j["action"] == "list_request") {
    reply["status"] = 0;
    auto list = friendManager.list_request(self_uid);
    for(int uid : list) {
      User *u = usrManager.getUser(uid);
      if(!u) continue;
      reply["data"]["request"].push_back({
        {"uid",u->uid},
        {"username",u->username}
      });
    }
    ctrlSock_->sendMsg(reply.dump());
  } else {
    reply["status"] = -1;
    reply["data"]["message"] = "unknown action";

    ctrlSock_->sendMsg(reply.dump());
  }
}

void Session::chatServer(nlohmann::json j) {
  if(j["action"] == "private_chat") {
    j["data"]["message_id"] = messageManager.getMsgId();
    auto msg = j["data"].get<Message>();
    if(sessionManager.isOnline(msg.target_id)) {
      sessionManager.sendTo(msg.target_id,msg);
      msg.status = 1;
      messageManager.add(msg);
    } else {
      messageManager.addOfflineMsg(msg);
    }
    nlohmann::json reply = makeReply(j,0);
    reply["data"] = msg;
    ctrlSock_->sendMsg(reply.dump());
    //  = makeReply();
    // messageManager.add();
  }
  if(j["action"] == "private_history") {
    // messageManager.getHistory(j["data"]["from_uid"]);
  }
}

void Session::start() {
  std::string recv;
  authServer();
  while(true) {
    if(ctrlSock_->recvMsg(recv) != NetResult::OK) {
      std::cout << "[INFO] client disconnected or recv failed\n";
      break;
    }
    if(recv.empty()) continue;
    nlohmann::json j = nlohmann::json::parse(recv);
    if(j["type"] == "friend") {
      friendSever(j);
      continue;
    }
    if(j["type"] == "chat") {
      chatServer(j);
      continue;
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
