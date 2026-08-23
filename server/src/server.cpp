#include "server.h"
#include "shared.h"
#include "manager.h"
#include "imgmeta.h"
#include "database.h"
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

struct UploadSession {
  std::string file_id;
  uint64_t offset;
  uint64_t total; // 总大小
};

struct DownloadSession {
  std::string file_id;
  uint64_t offset;
  uint64_t total;
  int file_fd;
  bool sending = false;
};

namespace {
  std::unordered_map<int,std::shared_ptr<FileConn>> upload_sessions;
  std::mutex upload_mtx;
  std::unordered_map<int, std::shared_ptr<FileConn>> download_sessions;
  std::mutex download_mtx;
}

constexpr uint64_t HEARTBEAT_CHECK_MS = 5000;
constexpr uint64_t HEARTBEAT_PING_MS = 30000;
constexpr uint64_t HEARTBEAT_TIMEOUT_MS = 65000;

void checkHeartbeat(EventLoop* loop) {
  uint64_t now = now_ms();
  nlohmann::json ping;
  ping["msg_type"] = "push";
  ping["type"] = "heartbeat";
  ping["action"] = "ping";
  ping["time"] = now;
  for(auto* conn : loop->conns()) {
    if(now - conn->lastActive() > HEARTBEAT_TIMEOUT_MS) {
      conn->handleClose();
    } else if(now - conn->lastPingMs() > HEARTBEAT_PING_MS) {
      conn->send(ping.dump());
      conn->updateLastPing(now);
    }
  }
}

Database db;
UsrManager usrManager;
SessionManager sessionManager;
FriendManager friendManager;
MessageManager messageManager;
GroupManager groupManager;
UidGenerator get_uid;
FileManager fileManager;
MessageWrite msgWrite(&db);

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

static void notifyFileMessage(const std::string& file_id) {
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
  // messageManager.add(msg);
  msgWrite.push(msg);
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

void startUpload(EventLoop* loop,int fd,const std::string& file_id,uint64_t offset,uint64_t total) {
  auto fcon = std::make_shared<FileConn>(loop,fd);
  auto seion = std::make_shared<UploadSession>(UploadSession{file_id,offset,total});
  std::weak_ptr<FileConn> weak = fcon;

  fcon->setReadCallback([seion,weak](const char* data,size_t len) {
    if(auto w = weak.lock()) {
      if(!fileManager.writePart(seion->file_id,seion->offset,data,len)) {
        w->handleClose();
        return;
      }
      seion->offset += len;
      if(seion->offset >= seion->total) {
        fileManager.finishUpload(seion->file_id);
        notifyFileMessage(seion->file_id);
        w->handleClose();
      }
    }
  });
  fcon->setCloseCallback([seion,weak,loop,fd] {
    fileManager.updateFileReceived(seion->file_id,seion->offset);
    loop->queueInLoop([weak,fd] {
      std::lock_guard<std::mutex> lock(upload_mtx);
      upload_sessions.erase(fd);
    });
  });
  {
    std::lock_guard<std::mutex> lock(upload_mtx);
    upload_sessions[fd] = fcon;
  }
}

void trySendNext(std::shared_ptr<DownloadSession> seion,const std::shared_ptr<FileConn>& fcon) {
  while(!seion->sending) {
    if(seion->offset >= seion->total) {
      fcon->handleClose();
      return;
    }
    char buf[FILE_CHUNK];
    ssize_t n = read(seion->file_fd,buf,sizeof(buf));
    if(n <= 0) {
      fcon->handleClose();
      return;
    }
    seion->sending = true;
    seion->offset += (uint64_t)n;
    if(fcon->send(std::string(buf,(size_t)n))) {
      seion->sending = false;
    } else {
      break;
    }
  }
}

void startDownload(EventLoop* loop,int fd,const std::string& file_id,uint64_t offset,uint64_t total) {
  auto me = fileManager.getFileMeta(file_id);
  if(!me) {
    close(fd);
    return;
  }
  int file_fd = open(fileManager.getFullPath(me->storage_path).c_str(),O_RDONLY);
  if(file_fd < 0) {
    close(fd);
    return;
  }
  lseek(file_fd,(off_t)offset,SEEK_SET);
  
  auto fcon = std::make_shared<FileConn>(loop,fd);
  auto seion = std::make_shared<DownloadSession>(DownloadSession{file_id,offset,total,file_fd,false});
  std::weak_ptr<FileConn> weak = fcon;
  fcon->setWriteEmptyCallback([seion,weak] {
    if(auto w = weak.lock()) {
      seion->sending = false;
      trySendNext(seion,w);
    }
  });
  fcon->setCloseCallback([seion,weak,loop,fd] {
    close(seion->file_fd);
    loop->queueInLoop([weak,fd] {
      std::lock_guard<std::mutex> lock(download_mtx);
      download_sessions.erase(fd);
    });
  });
  {
    std::lock_guard<std::mutex> lock(download_mtx);
    download_sessions[fd] = fcon;
  }
  trySendNext(seion,fcon);
}

class Session {
public:
  Session(std::shared_ptr<Connection> conn) : conn_(std::move(conn)) {}

  void onMessage(const std::string& frame);
  void onClose();

private:
  bool returnReply(const nlohmann::json& req,int status,nlohmann::json& reply);
  void friendOnPush();
  void friendOffPush();
  bool handleAuth(const nlohmann::json& j); // 处理登录/注册
  void handleUserAction(const nlohmann::json& j); // 处理登出/注销
  void friendSever(nlohmann::json j);
  void chatServer(nlohmann::json j);
  void groupServer(nlohmann::json j);
  void fileServer(nlohmann::json j);

private:
  User linked_usr_;
  bool authed_ = false;
  std::shared_ptr<Connection> conn_;
};

namespace {
  std::unordered_map<int,std::shared_ptr<Session>> sessions; // fd -> session
  std::mutex sessions_mtx;
}

bool Session::returnReply(const nlohmann::json& req,int status,nlohmann::json& reply) {
  reply["msg_type"] = "reply";
  reply["request_id"] = req["request_id"];
  reply["status"] = status;
  reply["time"] = now_ms();

  conn_->send(reply.dump());
  return true;
}

bool Session::handleAuth(const nlohmann::json& j) {
  nlohmann::json reply;
  int uid;
  std::optional<User> usr;
  try {
    if(j["action"]=="register") {
      std::string username = j["data"]["username"];
      std::string password = j["data"]["password"];
      if(usrManager.isExist(username)) {
        reply["error"] ="username exists";
        returnReply(j,1,reply);
        return false;
      }
      uid=get_uid.get();
      if(!usrManager.regis(username,password,uid)) {
        reply["error"] = "register failed";
        returnReply(j,1,reply);
        return false;
      }
      usr = usrManager.getUser(uid);
      usr->online = true;
      // usrManager.save(USRDATA);
      reply["data"]={
        {"uid",uid},
        {"username",username}
      };
      returnReply(j,0,reply);
      linked_usr_.username = usr->username;
      linked_usr_.uid = usr->uid;
      // 登录时间
      authed_ = true;
      return true;
    } else if(j["action"] == "login") {
      std::string username = j["data"]["username"];
      std::string password = j["data"]["password"];
      if(!usrManager.login(username,password,uid)) {
        reply["error"] ="username or password wrong";
        returnReply(j,1,reply);
        return false;
      }
      usr = usrManager.getUser(uid);
      usr->online = true;
      reply["data"]={
        {"uid",uid},
        {"username",username},
        {"last_logout_time",usrManager.getLastLogout(uid)}
      };
      returnReply(j,0,reply);
      linked_usr_.username = usr->username;
      linked_usr_.uid = usr->uid;
      authed_ = true;
      return true;
    } else {
      returnReply(j,1,reply);
    }
  } catch(const std::exception& e) {
    LOG(ERROR) << "auth 处理异常：" << e.what();
    returnReply(j,-1,reply);
  }
  return false;
}

void Session::handleUserAction(const nlohmann::json& j) {
  try{
    if(j["type"] == "user") {
      if(j["action"] == "logout") {
        usrManager.updateLastLogout(linked_usr_.uid,now_ms());
        friendOffPush();
        sessionManager.unbindUser(linked_usr_.uid,conn_);
        nlohmann::json reply;
        returnReply(j,0,reply);
        authed_ = false;
        linked_usr_.uid = 0;
        linked_usr_.username = "";
        return;
      }
      if(j["action"] == "delete") {
        int del_uid = linked_usr_.uid;
        nlohmann::json reply;
        if(del_uid != j["data"]["uid"]) {
          returnReply(j,-1,reply);
          return;
        }
        if(!usrManager.delUsr(del_uid,j["data"]["password"])) {
          returnReply(j,-1,reply);
          return;
        }
        friendOffPush();
        friendManager.removeUsr(del_uid);
        messageManager.removeUsr(del_uid);
        std::vector<int> disbanded;
        groupManager.removeUser(del_uid,&disbanded);
        for(int gid : disbanded) messageManager.removeGroupMessages(gid);
        sessionManager.unbindUser(del_uid,conn_);
        returnReply(j,0,reply);
        authed_ = false;
        // conn_->handleClose();
        return;
      }
    }
  } catch(const std::exception& e) {
    LOG(ERROR) << "异常消息：" << e.what();
  }
}

void Session::friendSever(nlohmann::json j) {
  int self_uid = linked_usr_.uid;
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
    if(msg.from_uid != linked_usr_.uid) {
      nlohmann::json reply;
      reply["error"] = "fake request";
      returnReply(j,-1,reply);
      return;
    }
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
    msgWrite.push(msg);
    if(sessionManager.isOnline(msg.target_id)) {
      sessionManager.sendChatTo(msg.target_id,msg);
      msg.status = 1;
    } else {
      msg.status = 0;
    }
    if(sessionManager.isOnline(msg.from_uid)) {
      sessionManager.sendChatTo(msg.from_uid,msg);
    }
    nlohmann::json reply;
    reply["data"] = msg;
    returnReply(j,0,reply);
  }
  if(j["action"] == "group_chat") {
    auto msg = j["data"].get<Message>();
    if(msg.from_uid != linked_usr_.uid) {
      nlohmann::json reply;
      reply["error"] = "fake request";
      returnReply(j,-1,reply);
      return;
    }
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
    msgWrite.push(msg);
    auto members = groupManager.getMembers(gid);
    for(auto& m : members) {
      // if (m.uid == self_uid) continue; // 不发给自己
      if(sessionManager.isOnline(m.uid)) {
        sessionManager.sendChatTo(m.uid,msg);
      }
    }
    reply["data"] = msg;
    returnReply(j,0,reply);
  }
  if(j["action"] == "history_all") {
    int self_uid = linked_usr_.uid;
    std::vector<Message> all = messageManager.getAllMsg(self_uid);
    nlohmann::json reply;
    reply["data"]["messages"] = all;
    returnReply(j,0,reply);
  }
  if(j["action"] == "sync_new") {
    uint64_t last_time = j["data"].value("last_time",0ULL);
    int uid = linked_usr_.uid;
    auto msgs = messageManager.getMessagesByTime(uid,last_time);
    nlohmann::json reply;
    reply["data"]["messages"] = msgs;
    returnReply(j,0,reply);
  }
}

void Session::groupServer(nlohmann::json j) {
  nlohmann::json reply;
  int self_uid = linked_usr_.uid;
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
      messageManager.removeGroupMessages(gid);
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
  int self_uid = linked_usr_.uid;
  nlohmann::json reply;
  if(j["action"] == "upload_req") {
    std::string chat_type = j["data"].value("chat_type",std::string());
    int target_id = j["data"].value("target_id",0);
    std::string file_name = j["data"].value("file_name",std::string());
    int64_t file_size = j["data"].value("file_size",(int64_t)0);

    // 权限校验
    if(chat_type == "private") {
      if(!friendManager.isFriend(self_uid,target_id)) {
        reply["error"]="not friends";
        returnReply(j,-1,reply);
        return;
      }
      if(friendManager.isBlocked(target_id,self_uid)) {
        nlohmann::json reply;
        reply["error"] = "blocked by receiver";
        returnReply(j,-1,reply);
        return;
      }
    } else if(chat_type == "group") {
      if(!groupManager.isMember(target_id,self_uid)) {
        reply["error"]="not member";
        returnReply(j,-1,reply);
        return;
      }
    } else {
      reply["error"]="invalid chat_type";
      returnReply(j,-1,reply);
      return;
    }
    if(file_name.empty() || file_size <= 0 || file_size > MAX_FILE_SIZE) {
      reply["error"]="invalid file";
      returnReply(j,-1,reply);
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
        returnReply(j,-1,reply);
        return;
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

    unsigned short port = 0;
    int lfd = createDataListener(port);
    if(lfd < 0) {
      reply["error"]="no port";
      returnReply(j,-1,reply);
      return;
    }

    reply["data"] = {
      {"ip",conn_->localIp()},
      {"port",port},
      {"file_id",file_id},
      {"offset",offset}
    };
    returnReply(j,0,reply);

    EventLoop* loop = conn_->loop();
    auto* acceptC = new Channel(loop,lfd);
    acceptC->setReadBack([loop,acceptC,lfd,file_id,offset,file_size] {
      int fd = accept4(lfd,nullptr,nullptr,SOCK_NONBLOCK | SOCK_CLOEXEC);
      close(lfd);
      loop->queueInLoop([acceptC] {
        acceptC->remove();
        delete acceptC;
      });
      if(fd < 0) return;
      startUpload(loop,fd,file_id,offset,(uint64_t)file_size);
    });
    acceptC->enableRead();
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
    int64_t file_size = meta.file_size;
    reply["data"] = {
      {"ip",conn_->localIp()},
      {"port",port},
      {"file_id",file_id},
      {"offset",offset},
      {"file_size",file_size}
    };
    returnReply(j,0,reply);

    EventLoop* loop = conn_->loop();
    auto* acceptC = new Channel(loop,lfd);
    acceptC->setReadBack([loop,acceptC,lfd,file_id,offset,file_size] {
      int fd = accept4(lfd,nullptr,nullptr,SOCK_NONBLOCK | SOCK_CLOEXEC);
      close(lfd);
      loop->queueInLoop([acceptC] {
        acceptC->remove();
        delete acceptC;
      });
      if(fd < 0) return;
      startDownload(loop,fd,file_id,offset,(uint64_t)file_size);
    });
    acceptC->enableRead();
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

void Session::onMessage(const std::string& frame) {
  nlohmann::json j;
  try {
    j = nlohmann::json::parse(frame);
  } catch(...) {
    LOG(WARNING) << "无法解析的帧 uid=" << linked_usr_.uid 
      << " size=" << frame.size() 
      << " prefix=" << frame.substr(0, 64);
    return;
  }
  LOG(INFO) << "action=" << j.value("action","?");
  if(!authed_) {
    std::string type = j.value("type",std::string());
    if(type == "user") {
      std::string action = j.value("action",std::string());
      if(action == "register" || action == "login") {
        if(handleAuth(j)) {
          if(auto old = sessionManager.getConn(linked_usr_.uid)) {
            if(old != conn_) {
              nlohmann::json kick;
              kick["msg_type"] = "push";
              kick["type"] = "system";
              kick["action"] = "kicked";
              kick["data"] = {{"reason","该账号已在其他设备登录"}};
              auto loop = old->loop();
              loop->runInLoop([old,kick] {
                old->send(kick.dump());
                old->handleClose();
              });
            }
          }
          sessionManager.bindUser(linked_usr_.uid,conn_);
          friendOnPush();
        }
      }
    }
    return;
  }

  try {
    if(j["type"] == "friend") {
      friendSever(j);
      return;
    }
    if(j["type"] == "chat") {
      chatServer(j);
      return;
    }
    if(j["type"] == "group") {
      groupServer(j);
      return;
    }
    if(j["type"] == "file") {
      fileServer(j);
      return;
    }
    if(j["type"] == "user") {
      handleUserAction(j);
      return;
    }
  } catch(const std::exception& e) {
    LOG(ERROR) << "处理异常 uid=" << linked_usr_.uid
      << " type=" << j.value("type","?")
      << " action=" << j.value("action","?")
      << " request_id=" << j.value("request_id","?")
      << " err=" << e.what();
  }
}

void Session::onClose() {
  if(authed_) {
    if(sessionManager.isBoundTo(linked_usr_.uid, conn_)) {
      usrManager.updateLastLogout(linked_usr_.uid,conn_->lastActive());
      friendOffPush();
      sessionManager.unbindUser(linked_usr_.uid,conn_);
    }
    linked_usr_.uid = 0;
    linked_usr_.username = "";
    authed_ = false;
  }
}

int start_server(unsigned short port) {
  std::filesystem::create_directories(SAVEPATH);
  DbConfig dbcfg;
  if(!loadDbConfig(dbcfg)) {
    LOG(WARNING) << "db.conf not found";
  }
  if(!db.connect(dbcfg.host,dbcfg.user,dbcfg.password,dbcfg.database,dbcfg.port)) {
    LOG(ERROR) << "MySQL connect failed";
    return 1;
  }
  LOG(INFO) << "MySQL connected";

  usrManager.setDb(&db);
  usrManager.load();
  friendManager.setDb(&db);
  friendManager.load();
  groupManager.setDb(&db);
  groupManager.load();
  messageManager.setDb(&db);
  fileManager.setDb(&db);
  fileManager.init();
  msgWrite.start();
  int max_uid = usrManager.getMaxUid();
  get_uid.init(max_uid + 1);

  EventLoop main_loop;
  EventLoopPool pool(&main_loop);
  pool.runEveryOnSubLoops(HEARTBEAT_CHECK_MS,checkHeartbeat);
  pool.start();
  Acceptor acceptor(&main_loop,port);
  acceptor.setNewConCallback([&pool](int fd) {
    EventLoop* sub = pool.nextLoop();
    sub->runInLoop([sub,fd] {
      // auto conn = std::make_shared<Connection>(sub,fd);
      auto conn = std::shared_ptr<Connection>(
        new Connection(sub,fd),[sub](Connection* p) {
          sub->runInLoop([p] { delete p; });
        }
      );
      auto session = std::make_shared<Session>(conn);
      std::weak_ptr<Session> weak = session;
      conn->setMessageCallback([weak](const std::string& frame) {
        if(auto s = weak.lock()) s->onMessage(frame);
      });
      conn->setCloseCallback([weak,sub,fd] {
        if(auto s = weak.lock()) s->onClose();
        sub->queueInLoop([weak,fd] {
          std::lock_guard<std::mutex> lock(sessions_mtx);
          // sessions.erase(fd);
          auto it = sessions.find(fd);
          if(it != sessions.end() && it->second == weak.lock()) {
            sessions.erase(it);
          }
        });
      });
      {
        std::lock_guard<std::mutex> lock(sessions_mtx);
        sessions[fd] = session;
      }
    });
  });

  LOG(INFO) << "server start listening on port " << port;
  main_loop.loop();
  msgWrite.stop();
  return 0;
}
