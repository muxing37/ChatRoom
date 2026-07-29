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
GroupManager groupManager;
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

  Session(std::shared_ptr<TcpSocket> sock) : ctrlSock_(std::move(sock)) {}

  void start();

private:
  bool returnReply(const nlohmann::json& req,int status,nlohmann::json& reply);
  void friendOnPush();
  void friendOffPush();
  void authServer();
  void friendSever(nlohmann::json j);
  void chatServer(nlohmann::json j);
  void groupServer(nlohmann::json j);

private:
  User linked_usr_;
  std::shared_ptr<TcpSocket> ctrlSock_;
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

bool Session::returnReply(const nlohmann::json& req,int status,nlohmann::json& reply) {
  reply["msg_type"] = "reply";
  reply["request_id"] = req["request_id"];
  reply["status"] = status;
  reply["time"] = now_ms();

  ctrlSock_->sendMsg(reply.dump());
  return true;
}

void Session::authServer() {
  User* usr;
  while(true) {
    std::string res;
    if(ctrlSock_->recvMsg(res)!=NetResult::OK) return;
    if(res.empty()) continue;

    auto j = nlohmann::json::parse(res);

    if(j["type"]!="user") {
      nlohmann::json reply;
      reply["error"]="invalid type";
      returnReply(j,1,reply);
      continue;
    }
    nlohmann::json reply;
    int uid;
    if(j["action"]=="register") {
      std::string username = j["data"]["username"];
      std::string password = j["data"]["password"];
      if(usrManager.isExist(username)) {
        reply["error"] ="username exists";
        returnReply(j,1,reply);
        continue;
      }
      uid=get_uid.get();
      if(!usrManager.regis(username,password,uid)) {
        reply["error"] = "register failed";
        returnReply(j,1,reply);
        continue;
      }
      usr = usrManager.getUser(uid);
      usr->online = true;
      usrManager.save(USRDATA);
      reply["data"]={
        {"uid",uid},
        {"username",username}
      };
      returnReply(j,0,reply);
      break;
    } else if(j["action"] == "login") {
      std::string username = j["data"]["username"];
      std::string password = j["data"]["password"];
      if(!usrManager.login(username,password,uid)) {
        reply["error"] ="username or password wrong";
        returnReply(j,1,reply);
        continue;
      }
      usr = usrManager.getUser(uid);
      usr->online = true;
      reply["data"]={
        {"uid",uid},
        {"username",username}
      };
      returnReply(j,0,reply);
      break;
    } else {
      returnReply(j,1,reply);
    }
  }
  linked_usr_.username = usr->username;
  linked_usr_.uid = usr->uid;
}

void Session::friendSever(nlohmann::json j) {
  int self_uid = j["data"]["from_uid"];
  nlohmann::json reply;
  reply["type"] = "reply";
  reply["action"] = j["action"];
  if(j["action"] == "request") {
    int to_uid = j["data"]["to_uid"];
    int sta = friendManager.request(self_uid,to_uid);
    returnReply(j,sta,reply);
    if(sessionManager.isOnline(to_uid)) {
      nlohmann::json push;
      User* usr = usrManager.getUser(self_uid);
      push["msg_type"] = "push";
      push["type"] = "friend";
      push["action"] = "request";
      push["time"] = now_ms();
      push["data"] = {
        {"username",usr->username},
        {"uid",usr->uid}
      };
      sessionManager.sendPushTo(to_uid,push);
    }
  } else if(j["action"] == "del") {
    int to_uid = j["data"]["to_uid"];
    friendManager.del(self_uid,to_uid);
    returnReply(j,0,reply);
  } else if(j["action"] == "agree") {
    int to_uid = j["data"]["to_uid"];
    friendManager.agree(self_uid,to_uid);
    returnReply(j,0,reply);
    if(sessionManager.isOnline(to_uid)) {
      nlohmann::json push;
      User* usr = usrManager.getUser(self_uid);
      push["msg_type"] = "push";
      push["type"] = "friend";
      push["action"] = "agree";
      push["time"] = now_ms();
      push["data"] = {
        {"username",usr->username},
        {"uid",usr->uid}
      };
      sessionManager.sendPushTo(to_uid,push);
    }
  } else if(j["action"] == "reject") {
    int to_uid = j["data"]["to_uid"];
    friendManager.reject(self_uid,to_uid);
    returnReply(j,0,reply);
    if(sessionManager.isOnline(to_uid)) {
      nlohmann::json push;
      User* usr = usrManager.getUser(self_uid);
      push["msg_type"] = "push";
      push["type"] = "friend";
      push["action"] = "reject";
      push["time"] = now_ms();
      push["data"] = {
        {"username",usr->username},
        {"uid",usr->uid}
      };
      sessionManager.sendPushTo(to_uid,push);
    }
  } else if(j["action"] == "list_friend") {
    auto list = friendManager.list_friend(self_uid);
    for(int uid : list) {
      User *u = usrManager.getUser(uid);
      u->online = sessionManager.isOnline(u->uid);
      if(!u) continue;
      reply["data"]["friends"].push_back({
        {"uid",u->uid},
        {"username",u->username},
        {"online",u->online}
      });
    }
    returnReply(j,0,reply);
  } else if(j["action"] == "list_request") {
    auto list = friendManager.list_request(self_uid);
    for(int uid : list) {
      User *u = usrManager.getUser(uid);
      if(!u) continue;
      reply["data"]["request"].push_back({
        {"uid",u->uid},
        {"username",u->username}
      });
    }
    returnReply(j,0,reply);
  } else if (j["action"] == "block") {
    int to_uid = j["data"]["to_uid"];
    int sta = friendManager.block(self_uid,to_uid);
    returnReply(j,sta,reply);
  } else if (j["action"] == "unblock") {
    int to_uid = j["data"]["to_uid"];
    int sta = friendManager.unblock(self_uid,to_uid);
    returnReply(j,sta,reply);
  } else if (j["action"] == "list_block") {
    auto list = friendManager.getBlockList(self_uid);
    reply["data"]["block_list"] = list;
    returnReply(j,0,reply);
  } else {
    // reply["status"] = -1;
    reply["data"]["message"] = "unknown action";
    returnReply(j,-1,reply);
  }
}

void Session::chatServer(nlohmann::json j) {
  if(j["action"] == "private_chat") {
    auto msg = j["data"].get<Message>();
    if (friendManager.isBlocked(msg.target_id,msg.from_uid)) {
      nlohmann::json reply;
      reply["error"] = "blocked by receiver";
      returnReply(j,-1,reply);
      return;
    }
    j["data"]["message_id"] = messageManager.getMsgId();
    msg.time = now_ms();
    if(sessionManager.isOnline(msg.target_id)) {
      sessionManager.sendChatTo(msg.target_id,msg);
      msg.status = 1;
      messageManager.add(msg);
    } else {
      msg.status = 0;
      messageManager.add(msg);
    }
    nlohmann::json reply;
    reply["data"] = msg;
    returnReply(j,0,reply);
  }
  if(j["action"] == "private_history_all") {
    std::vector<Message> result;
    result = messageManager.getAllMsg(j["data"]["from_uid"]);
    nlohmann::json reply;
    for(auto& msg : result) {
      reply["data"].push_back(msg);
    }
    returnReply(j,0,reply);
  }
  if(j["action"] == "private_sync_new") {
    uint64_t last_time = j["data"].value("last_time",0ULL);
    int uid = j["data"]["from_uid"];
    auto msgs = messageManager.getMessagesByTime(uid,last_time);
    nlohmann::json reply;
    reply["data"]["messages"] = msgs;
    returnReply(j,0,reply);
  }
}

void Session::groupServer(nlohmann::json j) {

}

void Session::friendOnPush() {
  std::vector<int> fri = friendManager.list_friend(linked_usr_.uid);
  for(int& id : fri) {
    if(sessionManager.isOnline(id)) {
      nlohmann::json push;
      // User* usr = usrManager.getUser(id);
      push["msg_type"] = "push";
      push["type"] = "friend";
      push["action"] = "online";
      push["time"] = now_ms();
      push["data"] = {
        {"username",linked_usr_.username},
        {"uid",linked_usr_.uid}
      };
      sessionManager.sendPushTo(id,push);
    }
  }
}

void Session::friendOffPush() {
  std::vector<int> fri = friendManager.list_friend(linked_usr_.uid);
  for(int& id : fri) {
    if(sessionManager.isOnline(id)) {
      nlohmann::json push;
      // User* usr = usrManager.getUser(id);
      push["msg_type"] = "push";
      push["type"] = "friend";
      push["action"] = "offline";
      push["time"] = now_ms();
      push["data"] = {
        {"username",linked_usr_.username},
        {"uid",linked_usr_.uid}
      };
      sessionManager.sendPushTo(id,push);
    }
  }
}

void Session::start() {
  std::string recv;
  authServer();
  sessionManager.bindUser(linked_usr_.uid,ctrlSock_);
  friendOnPush();
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
  friendOffPush();
  sessionManager.unbindUser(linked_usr_.uid);
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
