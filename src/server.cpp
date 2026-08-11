#include "server.h"
#include "shared.h"
#include "manager.h"
#include "imgmeta.h"
#include <poll.h>
#include <random>
#include <sstream>
#include <filesystem>

const std::string SAVEPATH = "./data";
const std::string USRDATA = SAVEPATH + "/usrdata.json";
const std::string FRIENDDATA = SAVEPATH + "/frienddata.json";
const std::string PCHATDATA = SAVEPATH + "/pchatdata.json";
const std::string GROUPDATA = SAVEPATH + "/groupdata.json";

constexpr size_t FILE_CHUNK = 1024 * 1024; // 数据块
constexpr int DATA_ACCEPT_TIMEOUT_MS = 15000; // 连接超时
constexpr int64_t MAX_FILE_SIZE = 2LL * 1024 * 1024 * 1024; // 单文件大小上限（QQ 4GB,微信 1GB）

UsrManager usrManager;
SessionManager sessionManager;
FriendManager friendManager;
MessageManager messageManager;
GroupManager groupManager;
UidGenerator get_uid;
FileManager fileManager;

struct Transfer {
  std::string token;
  int owner_uid;
  std::string file_id;
  bool is_upload;
  uint64_t offset;
  uint64_t total;
};

class TransferRegis {
public:
  void add(const Transfer& tr);
  std::optional<Transfer> take(const std::string& token,int uid);
  void remove(const std::string& token);
private:
  std::unordered_map<std::string,Transfer> map_;
  std::mutex mtx_;
};

TransferRegis transferRegis;

void TransferRegis::add(const Transfer& tr) {
  std::lock_guard<std::mutex> lock(mtx_);
  map_[tr.token] = tr;
}

std::optional<Transfer> TransferRegis::take(const std::string& token,int uid) {
  std::lock_guard<std::mutex> lock(mtx_);
  auto it = map_.find(token);
  if(it == map_.end() || it->second.owner_uid != uid) return std::nullopt;
  auto tr = it->second;
  map_.erase(it);
  return tr;
}

void TransferRegis::remove(const std::string& token) {
  std::lock_guard<std::mutex> lock(mtx_);
  map_.erase(token);
}

static std::string genToken() {
  static std::mutex mtx;
  static std::mt19937_64 rng(std::random_device{}());
  std::ostringstream oss;
  {
    std::lock_guard<std::mutex> lock(mtx);
    oss << std::hex << now_ms() << "_" << rng();
  }
  return oss.str();
}

static int createDataListener(unsigned short& out_port) {
  int fd = socket(AF_INET,SOCK_STREAM,0);
  if(fd < 0) return -1;
  int opt = 1;
  setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
  sockaddr_in addr;
  memset(&addr,0,sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(out_port);
  if(bind(fd,(sockaddr*)&addr,sizeof(addr)) == -1) {
    close(fd);
    return -1;
  }
  if(listen(fd,8) == -1) {
    close(fd);
    return -1;
  }
  socklen_t len = sizeof(addr);
  if(getsockname(fd,(sockaddr*)&addr,&len) == 0) out_port = ntohs(addr.sin_port);
  return fd;
}

static int acceptWithTimeout(int lfd,int timeout_ms) {
  pollfd pfd{lfd, POLLIN, 0};
  int r = poll(&pfd,1,timeout_ms);
  if(r <= 0) return -1;
  sockaddr_in cli;
  socklen_t len = sizeof(cli);
  return accept(lfd,(sockaddr*)&cli,&len);
}

class FileSession {
public:
  static std::unique_ptr<FileSession> acceptAndAuth(int lfd,const std::string& token);
  void start();

private:
  FileSession(TcpSocket sock,const Transfer& info);

  void uploadLoop();
  void downloadLoop();
  bool uploadStep();
  bool downloadStep(int fd);

  static void notifyFileMessage(const std::string& file_id);

private:
  TcpSocket sock_;
  Transfer info_;
  std::optional<FileMeta> meta_;
  uint64_t offset_;
  char buf_[FILE_CHUNK];
};

FileSession::FileSession(TcpSocket sock,const Transfer& info) : sock_(std::move(sock)),info_(info),offset_(info.offset) {}

std::unique_ptr<FileSession> FileSession::acceptAndAuth(int lfd,const std::string& token) {
  int cfd = acceptWithTimeout(lfd,DATA_ACCEPT_TIMEOUT_MS);
  close(lfd);
  if(cfd < 0) {
    transferRegis.remove(token);
    return nullptr;
  }
  TcpSocket sock(cfd);

  std::string hs;
  if(sock.recvMsg(hs) != NetResult::OK) return nullptr;
  nlohmann::json j;
  try {
    j = nlohmann::json::parse(hs);
  } catch(...) {
    sock.sendMsg(R"({"status":-1})");
    return nullptr;
  }
  auto tr = transferRegis.take(j.value("token",""),j.value("uid",0));
  if(!tr) {
    sock.sendMsg(R"({"status":-1})");
    return nullptr;
  }
  sock.sendMsg(R"({"status":0})");

  auto fs = std::unique_ptr<FileSession>(new FileSession(std::move(sock),*tr));
  if(!tr->is_upload) {
    fs->meta_ = fileManager.getFileMeta(tr->file_id);
  }
  return fs;
}

void FileSession::start() {
  if(info_.is_upload) uploadLoop();
  else downloadLoop();
}

void FileSession::uploadLoop() {
  while(uploadStep()) {}
  if(offset_ == info_.total && fileManager.finishUpload(info_.file_id)) {
    notifyFileMessage(info_.file_id);
  }
}

bool FileSession::uploadStep() {
  if(offset_ >= info_.total) return false;
  int n = sock_.recvRaw(buf_,sizeof(buf_));
  if(n <= 0) return false;
  if(!fileManager.writePart(info_.file_id,offset_,buf_,(size_t)n)) return false;
  offset_ += (uint64_t)n;
  return offset_ < info_.total;
}

void FileSession::downloadLoop() {
  if(!meta_) return;
  std::string full = fileManager.getFullPath(meta_->storage_path);
  int fd = open(full.c_str(),O_RDONLY);
  if(fd < 0) return;
  if(lseek(fd,(off_t)info_.offset,SEEK_SET) == (off_t)-1) {
    close(fd);
    return;
  }
  offset_ = info_.offset;
  while(downloadStep(fd)) {}
  close(fd);
}

bool FileSession::downloadStep(int fd) {
  if(offset_ >= (uint64_t)meta_->file_size) return false;
  uint64_t remain = (uint64_t)meta_->file_size - offset_;
  size_t chunk;
  if(remain < sizeof(buf_)) {
    chunk = (size_t)remain;
  } else {
    chunk = sizeof(buf_);
  }
  ssize_t n = read(fd,buf_,chunk);
  if(n <= 0) return false;
  if(sock_.sendRaw(buf_,(size_t)n) != 0) return false;
  offset_ += (uint64_t)n;
  return offset_ < (uint64_t)meta_->file_size;
}

void FileSession::notifyFileMessage(const std::string& file_id) {
  auto me = fileManager.getFileMeta(file_id);
  if(!me) return;
  FileMeta meta = *me;
  Message msg;
  msg.message_id = messageManager.getMsgId();
  msg.type = "file";
  msg.chat_type = meta.chat_type;
  msg.from_uid = meta.uploader_uid;
  msg.target_id = meta.target_id;
  nlohmann::json cont;
  cont["file_id"] = meta.file_id;
  cont["file_name"] = meta.file_name;
  cont["file_size"] = meta.file_size;
  if(isImageName(meta.file_name)) {
    int w=0,h=0;
    if(imageSizeOfFile(fileManager.getFullPath(meta.storage_path),w,h) && w>0 && h>0){
      cont["width"] = w;
      cont["height"] = h;
    }
  }
  if(!meta.file_hash.empty()) cont["file_hash"] = meta.file_hash;
  msg.content = cont.dump();
  msg.time = now_ms();
  
  if(meta.chat_type == "private" && !sessionManager.isOnline(meta.target_id)) {
    msg.status = 0;
  } else {
    msg.status = 1;
  }
  messageManager.add(msg);
  if(meta.chat_type == "private") {
    if(sessionManager.isOnline(meta.target_id)) sessionManager.sendChatTo(meta.target_id,msg);
    if(sessionManager.isOnline(meta.uploader_uid)) sessionManager.sendChatTo(meta.uploader_uid,msg);
  } else {
    auto members = groupManager.getMembers(meta.target_id);
    for(auto& m : members) {
      if(sessionManager.isOnline(m.uid)) sessionManager.sendChatTo(m.uid,msg);
    }
  }
}

class TcpServer {
public:
  TcpServer();
  ~TcpServer();

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
  void fileServer(nlohmann::json j);

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
  std::optional<User> usr;
  while(true) {
    std::string res;
    if(ctrlSock_->recvMsg(res)!=NetResult::OK) return;
    if(res.empty()) continue;

    nlohmann::json j;
    try {
      j = nlohmann::json::parse(res);
    } catch(...) {
      continue;
    }

    if(j["type"]!="user") {
      nlohmann::json reply;
      reply["error"]="invalid type";
      returnReply(j,1,reply);
      continue;
    }
    nlohmann::json reply;
    int uid;
    try {
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
    } catch(const std::exception& e) {
      std::cerr << "[Server] auth 处理异常: " << e.what() << std::endl;
      returnReply(j,-1,reply);
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
      auto usr = usrManager.getUser(self_uid);
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
    if(sessionManager.isOnline(to_uid)) {
      nlohmann::json push;
      auto usr = usrManager.getUser(self_uid);
      push["msg_type"] = "push";
      push["type"] = "friend";
      push["action"] = "del";
      push["time"] = now_ms();
      push["data"] = {
        {"username",usr->username},
        {"uid",usr->uid}
      };
      sessionManager.sendPushTo(to_uid,push);
    }
    returnReply(j,0,reply);
  } else if(j["action"] == "agree") {
    int to_uid = j["data"]["to_uid"];
    friendManager.agree(self_uid,to_uid);
    returnReply(j,0,reply);
    if(sessionManager.isOnline(to_uid)) {
      nlohmann::json push;
      auto usr = usrManager.getUser(self_uid);
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
      auto usr = usrManager.getUser(self_uid);
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
      auto u = usrManager.getUser(uid);
      if(!u) continue;
      u->online = sessionManager.isOnline(u->uid);
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
      auto u = usrManager.getUser(uid);
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
    if(!friendManager.isFriend(msg.from_uid, msg.target_id)) {
      nlohmann::json reply;
      reply["error"] = "not friends";
      returnReply(j,-1,reply);
      return;
    }
    if(friendManager.isBlocked(msg.target_id,msg.from_uid)) {
      nlohmann::json reply;
      reply["error"] = "blocked by receiver";
      returnReply(j,-1,reply);
      return;
    }
    msg.message_id = messageManager.getMsgId();
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
  if(j["action"] == "group_chat") {
    auto msg = j["data"].get<Message>();
    msg.chat_type = "group";
    msg.time = now_ms();
    msg.message_id = messageManager.getMsgId();
    nlohmann::json reply;
    int gid = msg.target_id;
    int self_uid = msg.from_uid;
    if(!groupManager.isMember(gid,self_uid)) {
      returnReply(j,-1,reply);  // 非成员
      return;
    }
    messageManager.add(msg);
    auto members = groupManager.getMembers(gid);
// 此处需优化
    for(auto& m : members) {
      if (m.uid == self_uid) continue; // 不发给自己
      if(sessionManager.isOnline(m.uid)) {
        sessionManager.sendChatTo(m.uid,msg);
      }
    }
    reply["data"] = msg;
    returnReply(j,0,reply);
// 此处需优化
  }
  if(j["action"] == "history_all") {
    int self_uid = j["data"]["from_uid"];
    std::vector<Message> all = messageManager.getAllMsg(self_uid); // 此处需优化getAllMsg，加入过滤
    std::vector<Message> result;
    nlohmann::json reply;
    for(auto& msg : all) {
      if(msg.chat_type == "group") {
        if(!groupManager.isMember(msg.target_id,self_uid)) continue;
        uint64_t join_time = groupManager.getJoinTime(msg.target_id,self_uid);
        if(msg.time < join_time) continue;
      }
      result.push_back(msg);
    }
    reply["data"]["messages"] = result;
    returnReply(j,0,reply);
  }
  if(j["action"] == "sync_new") {
    uint64_t last_time = j["data"].value("last_time",0ULL);
    int uid = j["data"]["from_uid"];
    auto msgs = messageManager.getMessagesByTime(uid,last_time); // 同上，此处需优化，加入过滤
    std::vector<Message> result;
    nlohmann::json reply;
    for(auto& msg : msgs) {
      if(msg.chat_type == "group") {
        if(!groupManager.isMember(msg.target_id,uid)) continue;
        uint64_t join_time = groupManager.getJoinTime(msg.target_id,uid);
        if(msg.time < join_time) continue;
      }
      result.push_back(msg);
    }
    reply["data"]["messages"] = result;
    returnReply(j,0,reply);
  }
}

void Session::groupServer(nlohmann::json j) {
  nlohmann::json reply;
  int self_uid = j["data"]["from_uid"];
  if(j["action"] == "create") {
    std::string name = j["data"]["group_name"];
    int gid;
    int sta = groupManager.createGroup(*(usrManager.getUser(self_uid)),name,gid);
    if(sta == 0) {
      GroupInfo info = groupManager.getGroupInfo(gid);
      reply["data"]["group_id"] = gid;
      reply["data"]["name"] = info.name;
      reply["data"]["owner_uid"] = info.owner_uid;
      reply["data"]["create_time"] = info.create_time;
    }
    returnReply(j,sta,reply);
  }
  if(j["action"] == "disband") {
    int gid = j["data"]["group_id"];
    int sta = groupManager.disbandGroup(gid,self_uid);
    if(sta == 0) {
      GroupInfo g = groupManager.getGroupInfo(gid);
      auto members = groupManager.getMembers(gid);
      for(auto& m : members) {
        nlohmann::json push;
        push["msg_type"] = "push";
        push["type"] = "group";
        push["action"] = "member_update";
        push["data"] = {
          {"group_id",gid},
          {"group_name",g.name},
          {"update_type","disbanded"}
        };
        sessionManager.sendPushTo(m.uid, push);
      }
    }
    returnReply(j,sta,reply);
  }
  if(j["action"] == "join_request") {
    int gid = j["data"]["group_id"];
    int sta = groupManager.joinRequest(gid,self_uid);
    if(sta == 0) {
      auto members = groupManager.getMembers(gid);
      auto apply = usrManager.getUser(self_uid);
      GroupInfo info = groupManager.getGroupInfo(gid);
      nlohmann::json push;
      push["msg_type"] = "push";
      push["type"] = "group";
      push["action"] = "join_request";
      push["data"] = {
        {"group_id",gid},
        {"group_name",info.name},
        {"apply_uid",self_uid},
        {"apply_name",apply->username}
      };
      for(auto& m : members) {
        if(m.permission <= 1 && m.uid != self_uid) {
          sessionManager.sendPushTo(m.uid,push);
        }
      }
    }
    returnReply(j,sta,reply);
  }
  if(j["action"] == "handle_request") {
    int gid = j["data"]["group_id"];
    int target_uid = j["data"]["target_uid"];
    bool approval = j["data"]["approval"];
    auto target_user = usrManager.getUser(target_uid);
    int sta = groupManager.handleRequest(gid,self_uid,*target_user,approval);
    if(sta == 0) {
      nlohmann::json push;
      push["msg_type"] = "push";
      push["type"] = "group";
      push["action"] = "join_result";
      GroupInfo g = groupManager.getGroupInfo(gid);
      push["data"] = {
        {"group_id",gid},
        {"approval",approval},
        {"group_name",g.name}
      };
      if(approval) {
        push["data"]["create_time"] = g.create_time;
        push["data"]["owner_uid"] = g.owner_uid;
        auto members = groupManager.getMembers(gid);
        nlohmann::json res = nlohmann::json::array();
        for(auto& m : members) {
          res.push_back(m);
        }
        push["data"]["members"] = res;
        nlohmann::json update_push;
        update_push["msg_type"] = "push";
        update_push["type"] = "group";
        update_push["action"] = "member_update";
        update_push["data"] = {
          {"group_id",gid},
          {"update_type","join"},
          {"member_uid",target_uid},
          {"member_name",target_user->username}
        };
        for(auto& m : members) {
          if(m.uid != target_uid && sessionManager.isOnline(m.uid)) sessionManager.sendPushTo(m.uid,update_push);
        }
      }
      sessionManager.sendPushTo(target_uid,push);
    }
    returnReply(j,sta,reply);
  }
  if(j["action"] == "leave") {
    int gid = j["data"]["group_id"];
    int sta = groupManager.leaveGroup(gid,self_uid);
    if(sta == 0) {
      nlohmann::json update_push;
      update_push["msg_type"] = "push";
      update_push["type"] = "group";
      update_push["action"] = "member_update";
      update_push["data"] = {
        {"group_id",gid},
        {"update_type","leave"},
        {"member_uid",self_uid},
        {"member_name",usrManager.getUser(self_uid)->username}
      };
      auto members = groupManager.getMembers(gid);
      for(auto& m : members) {
        if(m.uid != self_uid && sessionManager.isOnline(m.uid)) sessionManager.sendPushTo(m.uid,update_push);
      }
    }
    returnReply(j,sta,reply);
  }
  if(j["action"] == "kick") {
    int gid = j["data"]["group_id"];
    int target_uid = j["data"]["target_uid"];
    auto target_user = usrManager.getUser(target_uid);
    int sta = groupManager.kickMember(gid,self_uid,target_uid);
    if(sta == 0) {
      nlohmann::json push;
      push["msg_type"] = "push";
      push["type"] = "group";
      push["action"] = "kicked";
      push["data"] = {
        {"group_id",gid},
        {"group_name",groupManager.getGroupInfo(gid).name}
      };
      sessionManager.sendPushTo(target_uid, push);
    
      nlohmann::json update_push;
      update_push["msg_type"] = "push";
      update_push["type"] = "group";
      update_push["action"] = "member_update";
      update_push["data"] = {
        {"group_id",gid},
        {"update_type","kick"},
        {"member_uid",target_uid},
        {"member_name",usrManager.getUser(target_uid)->username}
      };
      auto members = groupManager.getMembers(gid);
      for(auto& m : members) {
        if(m.uid != target_uid && sessionManager.isOnline(m.uid)) sessionManager.sendPushTo(m.uid,update_push);
      }
    }
    returnReply(j,sta,reply);
  }
  if(j["action"] == "set_admin") {
    int gid = j["data"]["group_id"];
    int target_uid = j["data"]["target_uid"];
    bool admin = j["data"]["is_admin"];
    int sta = groupManager.setAdmin(gid,self_uid,target_uid,admin);
    if(sta == 0) {
      reply["data"] = j["data"];
      nlohmann::json push;
      push["msg_type"] = "push";
      push["type"] = "group";
      push["action"] = "permission_changed";
      push["data"]["group_id"] = gid;
      int perm;
      if(admin) {
        perm = 1;
        push["data"]["new_permission"] = 1;
      } else {
        perm = 2;
        push["data"]["new_permission"] = 2;
      }
      sessionManager.sendPushTo(target_uid, push);

      nlohmann::json update_push;
      update_push["msg_type"] = "push";
      update_push["type"] = "group";
      update_push["action"] = "member_update";
      update_push["data"] = {
        {"group_id",gid},
        {"update_type","set_admin"},
        {"member_uid",target_uid},
        {"member_name",usrManager.getUser(target_uid)->username},
        {"new_permission",perm}
      };
      auto members = groupManager.getMembers(gid);
      for(auto& m : members) {
        if(m.uid != target_uid && sessionManager.isOnline(m.uid)) sessionManager.sendPushTo(m.uid,update_push);
      }
    }  
    returnReply(j,sta,reply);
  }
  if(j["action"] == "set_remind") {
    int gid = j["data"]["group_id"];
    int remind = j["data"]["remind"];
    int sta = groupManager.setRemind(gid,self_uid,remind);
    returnReply(j,sta,reply);
  }
  if(j["action"] == "list_my_groups") {
    auto gids = groupManager.getUserGroup(self_uid);
    nlohmann::json res = nlohmann::json::array();
    for(int gid : gids) {
      GroupInfo info = groupManager.getGroupInfo(gid);
      if(info.group_id != 0) {
        res.push_back({
          {"group_id",info.group_id},
          {"group_name",info.name},
          {"owner_uid",info.owner_uid},
          {"create_time",info.create_time}
        });
      }
    }
    reply["data"]["groups"] = res;
    returnReply(j,0,reply);
  }
  if(j["action"] == "list_join_requests") {
    int gid = j["data"]["group_id"];
    if(groupManager.getPermission(gid,self_uid) > 1) {
      returnReply(j,-1,reply);
      return;
    }
    auto uid_list = groupManager.listJoinRequests(gid);
    for(int uid : uid_list) {
      auto u = usrManager.getUser(uid);
      if(!u) continue;
      reply["data"]["requests"].push_back(
        {
          {"uid",u->uid},
          {"username",u->username}
        }
      );
    }
    returnReply(j,0,reply);
  }
  if(j["action"] == "list_all_join_requests") {
    auto gids = groupManager.getUserGroup(self_uid);
    nlohmann::json arr = nlohmann::json::array();
    for(int gid : gids) {
      if(groupManager.getPermission(gid,self_uid) > 1) continue;
      auto uid_list = groupManager.listJoinRequests(gid);
      if(uid_list.empty()) continue;
      nlohmann::json reqs = nlohmann::json::array();
      for(int uid : uid_list) {
        auto u = usrManager.getUser(uid);
        if(!u) continue;
        reqs.push_back({{"uid",u->uid},{"username",u->username}});
      }
      if(reqs.empty()) continue;
      GroupInfo info = groupManager.getGroupInfo(gid);
      arr.push_back({
        {"gid",gid},
        {"group_name",info.name},
        {"requests",reqs}
      });
    }
    reply["data"]["groups"] = arr;
    returnReply(j,0,reply);
  }
  if(j["action"] == "list_members") {
    int gid = j["data"]["group_id"];
    auto members = groupManager.getMembers(gid);
    nlohmann::json res = nlohmann::json::array();
    for(auto& m : members) {
      res.push_back(m);
    }
    reply["data"]["members"] = res;
    returnReply(j,0,reply);
  }
}

void Session::fileServer(nlohmann::json j) {
  int self_uid = j["data"]["from_uid"];
  nlohmann::json reply;
  if(j["action"] == "upload_req") {
    std::string chat_type = j["data"].value("chat_type",std::string());
    int target_id = j["data"].value("target_id",0);
    std::string file_name = j["data"].value("file_name",std::string());
    int64_t file_size = j["data"].value("file_size",(int64_t)0);

    // 权限校验
    if(chat_type == "private") {
      if(!friendManager.isFriend(self_uid,target_id)) {
        reply["error"]="not friends"; returnReply(j,-1,reply);
        return;
      }
    } else if(chat_type == "group") {
      if(!groupManager.isMember(target_id,self_uid)) {
        reply["error"]="not member"; returnReply(j,-1,reply);
        return;
      }
    } else {
      reply["error"]="invalid chat_type"; returnReply(j,-1,reply);
      return;
    }
    if(file_name.empty() || file_size <= 0 || file_size > MAX_FILE_SIZE) {
      reply["error"]="invalid file"; returnReply(j,-1,reply);
      return;
    }

    FileMeta meta;
    uint64_t offset = 0;
    std::string file_id = j["data"].value("file_id",std::string());
    bool is_resume = !file_id.empty();
    // 未带 file_id 但带有 hash，凭 哈希、上传者、目标 找回未完成任务，实现续传
    if(!is_resume) {
      std::string req_hash = j["data"].value("file_hash",std::string());
      if(!req_hash.empty()) {
        auto found = fileManager.findIncomplete(req_hash,self_uid,chat_type,target_id);
        if(found) {
          file_id = found->file_id;
          is_resume = true;
        }
      }
    }
    if(is_resume) {
      // 断点续传
      auto me = fileManager.getFileMeta(file_id);
      if(!me) {
        reply["error"]="file not found";
        returnReply(j,-1,reply); return;
      }
      if(me->uploader_uid != self_uid) {
        reply["error"]="no permission";
        returnReply(j,-1,reply);
        return;
      }
      if(me->status != 0) {
        reply["error"]="already done";
        returnReply(j,-1,reply);
        return;
      }
      if(file_size != me->file_size) {
        reply["error"]="file changed";
        returnReply(j,-1,reply);
        return;
      }
      std::string client_hash = j["data"].value("file_hash",std::string());
      if(!me->file_hash.empty() && !client_hash.empty() && client_hash != me->file_hash) {
        reply["error"]="file changed";
        returnReply(j,-1,reply);
        return;
      }
      meta = *me;
      if(file_name != me->file_name) {
        // 如果文件名变了但内容未变（hash 相同），则以本次上传的文件名为准，消息显示新名
        meta.file_name = file_name;
        fileManager.updateFileMeta(meta);
      }
      offset = fileManager.getPartSize(file_id);
      fileManager.createPart(file_id);
    } else {
      // 新上传
      file_id = fileManager.generateFileId();
      meta.file_id = file_id;
      meta.file_name = file_name;
      meta.file_hash = j["data"].value("file_hash",std::string());
      meta.file_size = file_size;
      meta.uploader_uid = self_uid;
      meta.upload_time = now_ms();
      meta.storage_path = fileManager.makeStoragePath(file_id);
      meta.chat_type = chat_type;
      meta.target_id = target_id;
      meta.status = 0;
      meta.received = 0;
      if(!fileManager.addFileMeta(meta) || !fileManager.createPart(file_id)) {
        reply["error"]="create failed";
        returnReply(j,-1,reply);
        return;
      }
    }

    // 开数据端口 + 一次性 token
    unsigned short port = 0;
    int lfd = createDataListener(port);
    if(lfd < 0) {
      reply["error"]="no port";
      returnReply(j,-1,reply);
      return;
    }
    std::string token = genToken();
    Transfer t = {
      token,
      self_uid,
      file_id,
      true,
      offset,
      (uint64_t)file_size
    };
    transferRegis.add(t);

    reply["data"] = {
      {"ip",ctrlSock_->localIp()},
      {"port",port},
      {"token",token},
      {"file_id",file_id},
      {"offset",offset}
    };
    returnReply(j,0,reply);
    std::thread([lfd,token]() {
      auto fs = FileSession::acceptAndAuth(lfd,token);
      if(fs) fs->start();
    }).detach();

  } else if(j["action"] == "download_req") {
    std::string file_id = j["data"].value("file_id",std::string());
    auto me = fileManager.getFileMeta(file_id);
    if(!me) {
      reply["error"] = "file not found";
      returnReply(j,-1,reply);
      return;
    }
    FileMeta meta = *me;
    if(meta.status != 1) {
      reply["error"]="not ready";
      returnReply(j,-1,reply);
      return;
    }

    if(meta.chat_type == "private") {
      if(meta.uploader_uid != self_uid && meta.target_id != self_uid) {
        reply["error"]="no permission";
        returnReply(j,-1,reply);
        return;
      }
    } else if(meta.chat_type == "group") {
      if(!groupManager.isMember(meta.target_id,self_uid)) {
        reply["error"]="no permission";
        returnReply(j,-1,reply);
        return;
      }
    }

    uint64_t offset = j["data"].value("offset",(uint64_t)0);
    if(offset > (uint64_t)meta.file_size) {
      reply["error"]="bad offset";
      returnReply(j,-1,reply);
      return;
    }
    std::string full = fileManager.getFullPath(meta.storage_path);
    if(access(full.c_str(),R_OK) != 0) {
      reply["error"]="file missing";
      returnReply(j,-1,reply);
      return;
    }
    unsigned short port = 0;
    int lfd = createDataListener(port);
    if(lfd < 0) {
      reply["error"]="no port";
      returnReply(j,-1,reply);
      return;
    }
    std::string token = genToken();
    Transfer t = {
      token,
      self_uid,
      file_id,
      false,
      offset,
      (uint64_t)meta.file_size
    };
    transferRegis.add(t);

    reply["data"] = {
      {"ip",ctrlSock_->localIp()},
      {"port",port},
      {"token",token},
      {"file_id",file_id},
      {"offset",offset},
      {"file_size",meta.file_size}
    };
    returnReply(j,0,reply);

    std::thread([lfd,token]() {
      auto fs = FileSession::acceptAndAuth(lfd, token);
      if(fs) fs->start();
    }).detach();
  } else {
    reply["error"]="unknown action";
    returnReply(j,-1,reply);
  }
}

void Session::friendOnPush() {
  std::vector<int> fri = friendManager.list_friend(linked_usr_.uid);
  for(int& id : fri) {
    if(sessionManager.isOnline(id)) {
      nlohmann::json push;
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

    nlohmann::json j;
    try {
      j = nlohmann::json::parse(recv);
    } catch(...) {
      continue;
    }

    try {
      if(j["type"] == "friend") {
        friendSever(j);
        continue;
      }
      if(j["type"] == "chat") {
        chatServer(j);
        continue;
      }
      if(j["type"] == "group") {
        groupServer(j);
        continue;
      }
      if(j["type"] == "file") {
        fileServer(j);
        continue;
      }
      if(j["type"] == "user") {
        if(j["action"] == "logout") {
          friendOffPush();
          sessionManager.unbindUser(linked_usr_.uid);
          nlohmann::json reply;
          returnReply(j,0,reply);
          authServer();
          sessionManager.bindUser(linked_usr_.uid,ctrlSock_);
          friendOnPush();
          continue;
        }
        if(j["action"] == "delete") {
          int del_uid = linked_usr_.uid;
          nlohmann::json reply;
          if(del_uid != j["data"]["uid"]) {
            returnReply(j,-1,reply);
            continue;
          }
          if(!usrManager.delUsr(del_uid,j["data"]["password"])) {
            returnReply(j,-1,reply);
            continue;
          }
          friendOffPush();
          friendManager.removeUsr(del_uid);
          messageManager.removeUsr(del_uid);
          groupManager.removeUser(del_uid);
          sessionManager.unbindUser(del_uid);
          returnReply(j,0,reply);
          ctrlSock_->closefd();
          break;
        }
      }
    } catch(const std::exception& e) {
      std::cerr << "[Server] 处理消息异常(已跳过): " << e.what() << std::endl;
    }
  }
  friendOffPush();
  sessionManager.unbindUser(linked_usr_.uid);
}

int start_server() {
  std::filesystem::create_directories(SAVEPATH);

  usrManager.load(USRDATA);
  friendManager.load(FRIENDDATA);
  messageManager.load(PCHATDATA);
  groupManager.load(GROUPDATA);
  int max_uid = usrManager.getMaxUid();
  get_uid.init(max_uid + 1);
  fileManager.init(); // 文件系统初始化
  TcpServer server;

  if(!server.setListen(2100)) {
    std::cerr << "[FAIL] setListen failed\n";
    return 1;
  }

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
