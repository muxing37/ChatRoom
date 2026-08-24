#include "webui.h"
#include "chat_html.h"
#include <iostream>
#include <ctime>
#include <unordered_set>
#include <fstream>
#include <sstream>
#include <cctype>
#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdlib>

using json = nlohmann::json;

namespace {
  std::string loadFile(const std::string& path) {
    std::ifstream f(path);
    if(!f.is_open()) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
  }

  // 同名文件自动加递增后缀，避免覆盖
  std::string uniquePath(const std::string& dir,const std::string& name) {
    std::string path = dir + "/" + name;
    int n = 1;
    while(access(path.c_str(),F_OK) == 0) {
      auto dot = name.find_last_of('.');
      std::string base = (dot == std::string::npos) ? name : name.substr(0,dot);
      std::string ext = (dot == std::string::npos) ? "" : name.substr(dot);
      path = dir + "/" + base + " (" + std::to_string(n++) + ")" + ext;
    }
    return path;
  }

  std::string mimeForFile(const std::string& name) {
    auto dot = name.find_last_of('.');
    std::string ext = (dot == std::string::npos) ? "" : name.substr(dot+1);
    for(auto& c : ext) c = (char)tolower((unsigned char)c);
    if(ext == "png") return "image/png";
    if(ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if(ext == "gif") return "image/gif";
    if(ext == "webp") return "image/webp";
    if(ext == "bmp") return "image/bmp";
    if(ext == "svg") return "image/svg+xml";
    return "application/octet-stream";
  }

  std::mutex logmtx;
  std::string logTimeStr() {
    char buf[32];
    time_t t = time(nullptr);
    struct tm tmv;
    localtime_r(&t,&tmv);
    strftime(buf,sizeof(buf),"%H:%M:%S",&tmv);
    return buf;
  }

  void logLine(const std::string& msg) {
    std::lock_guard<std::mutex> lock(logmtx);
    std::cout << "[" << logTimeStr() << "] " << msg << std::endl;
  }
}

WebUI::WebUI(
  AuthService& auth,
  FriendService& fri,
  ChatService& chat,
  GroupService& grp,
  FileService& file,
  ClientContext& ctx
)
  : auth_(auth),friend_(fri),chat_(chat),group_(grp),file_(file),ctx_(ctx) {
  chat_.setSendResultHandler([this](const nlohmann::json& reply,const std::string& chat_type,int target){
    nlohmann::json ev = {
      {"type","send_result"},
      {"status",reply.value("status",-1)},
      {"error",reply.value("error","")},
      {"chat_type",chat_type},
      {"target_id",target}
    };
    broadcast(ev);
  });
}

WebUI::~WebUI() {
  svr_.stop();
}

void WebUI::stop() {
  {
    std::lock_guard<std::mutex> lock(conn_mutex_);
    for(auto* ws : connections_) {
      try { ws->close(); } catch(...) {}
    }
  }
  svr_.stop();
}

bool isPortAvailable(const char* host,int port) {
  int sock = socket(AF_INET,SOCK_STREAM,0);
  if(sock < 0) return false;
  int opt = 1;
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  inet_pton(AF_INET,host,&addr.sin_addr);
  int res = bind(sock,(sockaddr*)&addr,sizeof(addr));
  close(sock);
  return res == 0;
}

void WebUI::run(const char* host) {
  // 限制上传上限
  svr_.set_payload_max_length(MAX_FILE_SIZE);
  setupRoutes();
  int start_port = 8080;
  int port = start_port;
  while(!isPortAvailable(host,port)) {
    port++;
    if(port > start_port + 100) {
      std::cout << "无可用端口" << std::endl;
      return;
    }
  }
  std::cout << "ChatRoom WebUI\n"
    << "请访问：http://" << host << ":" << port << "\n"
    << "使用 Ctrl+C 退出\n";
  svr_.set_logger([](const httplib::Request& req, const httplib::Response& res) {
    if(req.path == "/api/ping") return;
    logLine(req.method + " " + req.path + " -> " + std::to_string(res.status));
  });
  svr_.set_idle_interval(0,100000);
  svr_.listen(host, port);
}

void WebUI::setupRoutes() {
  std::string html = loadFile("../client/web/chat.html");
  svr_.Get("/",[html](const httplib::Request&,httplib::Response& res) {
    // res.set_content(html,"text/html");
    res.set_content(CHAT_HTML_CONTENT,"text/html");
  });

  // 心跳，前端定时请求，检测本客户端进程是否存活
  svr_.Get("/api/ping",[&](const httplib::Request&,httplib::Response& res) {
    res.set_content(R"({"status":0})","application/json");
  });

  // 账户
  svr_.Post("/api/login",[&](const httplib::Request& req,httplib::Response& res) {
    try {
      auto j = json::parse(req.body);
      bool ok = auth_.login(j["username"].get<std::string>(),j["password"].get<std::string>());
      json resp = {{"status", ok ? 0 : -1}};
      if(ok) {
        resp["uid"] = ctx_.getSelf().uid;
        resp["username"] = ctx_.getSelf().username;
        resp["last_logout_time"] = ctx_.getLastLogoutTime();
        // 登录后同步
        friend_.listFriend();
        group_.listMyGroups();
        chat_.syncHistory();
        logLine("用户登录: " + ctx_.getSelf().username + " (uid=" + std::to_string(ctx_.getSelf().uid) + ")");
      } else {
        logLine("登录失败");
      }
      res.set_content(resp.dump(),"application/json");
    } catch(...) {
      res.status = 400;
      res.set_content(R"({"status":-1,"error":"invalid request"})","application/json");
    }
  });

  svr_.Post("/api/register",[&](const httplib::Request& req,httplib::Response& res) {
    try {
      auto j = json::parse(req.body);
      bool ok = auth_.regis(j["username"].get<std::string>(),j["password"].get<std::string>());
      json resp = {{"status", ok ? 0 : -1}};
      if(ok) {
        resp["uid"] = ctx_.getSelf().uid;
        resp["username"] = ctx_.getSelf().username;
        resp["last_logout_time"] = ctx_.getLastLogoutTime();
        friend_.listFriend();
        group_.listMyGroups();
        chat_.syncHistory();
        logLine("新用户注册: " + ctx_.getSelf().username + " (uid=" + std::to_string(ctx_.getSelf().uid) + ")");
      }
      res.set_content(resp.dump(),"application/json");
    } catch(...) {
      res.status = 400;
      res.set_content(R"({"status":-1,"error":"invalid request"})","application/json");
    }
  });

  svr_.Post("/api/logout",[&](const httplib::Request&,httplib::Response& res) {
    auth_.logout(); // 服务端解除绑定，保持连接，可再次登录
    ctx_.reset();
    logLine("用户登出");
    res.set_content(R"({"status":0})", "application/json");
  });

  // 注销账户，需密码验证，成功后服务端删除相关数据并断开连接
  svr_.Post("/api/account/delete",[&](const httplib::Request& req,httplib::Response& res) {
    try {
      auto j = json::parse(req.body);
      bool ok = auth_.delauth(j["password"].get<std::string>());
      if(ok) {
        ctx_.reset();
        logLine("账户已注销");
      }
      res.set_content(json{{"status", ok ? 0 : -1}}.dump(), "application/json");
    } catch(...) {
      res.status=400;
      res.set_content(R"({"status":-1})","application/json");
    }
  });

  // 页面刷新后判断当前是否已登录
  svr_.Get("/api/session",[&](const httplib::Request&,httplib::Response& res) {
    User me = ctx_.getSelf();
    if(me.uid <= 0 || me.username.empty()) {
      res.set_content(R"({"status":-1})","application/json");
      return;
    }
    res.set_content(json{{"status",0},{"uid",me.uid},{"username",me.username},{"last_logout_time",ctx_.getLastLogoutTime()}}.dump(),"application/json");
  });

  // 好友
  svr_.Get("/api/friends",[&](const httplib::Request&,httplib::Response& res) {
    if(friend_.listFriend() != 0) {
      res.set_content(R"({"status":-1,"error":"获取好友失败"})","application/json");
      return;
    }
    json arr = json::array();
    auto infoFromVec = [&](const std::vector<Message>& msgs)->nlohmann::json {
      nlohmann::json out = {{"time",(uint64_t)0},{"preview",""},{"from",""},{"unread",0}};
      const Message* best = nullptr;
      int unread = 0;
      for(auto& m : msgs) {
        if(m.status == 0) unread++;
        if(!best || m.time > best->time) best = &m;
      }
      out["unread"] = unread;
      if(!best) return out;
      out["time"] = best->time;
      if(best->type == "file") {
        std::string nm;
        try { nm = json::parse(best->content).value("file_name",std::string()); } catch(...) {}
        bool img = false;
        auto dot = nm.find_last_of('.');
        if(dot != std::string::npos) {
          std::string ext = nm.substr(dot+1);
          for(auto& c : ext) c = (char)tolower((unsigned char)c);
          img = (ext=="png"||ext=="jpg"||ext=="jpeg"||ext=="gif"||ext=="webp"||ext=="bmp"||ext=="svg");
        }
        out["preview"] = img ? "[图片]" : "[文件]";
      } else {
        out["preview"] = best->content;
      }
      if(best->from_uid == ctx_.getSelf().uid) out["from"] = "我";
      else {
        auto u = ctx_.getFriend(best->from_uid);
        if(u) out["from"] = u->username;
      }
      return out;
    };
    auto friends = ctx_.getFriendList();
    for(auto& u : friends) {
      auto lm = infoFromVec(ctx_.getMessage(u.uid));
      arr.push_back({{"uid",u.uid},{"username",u.username},{"online",u.online},{"last_time",lm["time"]},{"last_msg",lm["preview"]},{"last_from",lm["from"]},{"history",false},{"unread",lm["unread"]}});
    }
    res.set_content(json{{"status",0},{"friends",arr}}.dump(),"application/json");
  });

  svr_.Post("/api/friend/request",[&](const httplib::Request& req,httplib::Response& res) {
    try {
      auto j = json::parse(req.body);
      res.set_content(json{{"status",friend_.request(j["uid"].get<int>())}}.dump(),"application/json");
    } catch(...) {
      res.status=400;
      res.set_content(R"({"status":-1})","application/json");
    }
  });

  svr_.Post("/api/friend/delete",[&](const httplib::Request& req,httplib::Response& res) {
    try {
      auto j = json::parse(req.body);
      int ret = friend_.del(j["uid"].get<int>());
      if(ret == 0) ctx_.delFriend(j["uid"].get<int>());
      res.set_content(json{{"status",ret}}.dump(),"application/json");
    } catch(...) {
      res.status=400;
      res.set_content(R"({"status":-1})","application/json");
    }
  });

  svr_.Get("/api/friend/requests",[&](const httplib::Request&,httplib::Response& res) {
    if(friend_.listRequest() != 0) {
      res.set_content(R"({"status":-1,"error":"获取申请失败"})", "application/json");
      return;
    }
    json arr = json::array();
    for(auto& u : ctx_.getFriendRequests()) {
      arr.push_back({{"uid",u.uid},{"username",u.username}});
    }
    res.set_content(json{{"status",0},{"requests",arr}}.dump(),"application/json");
  });

  svr_.Post("/api/friend/accept",[&](const httplib::Request& req,httplib::Response& res) {
    try {
      auto j = json::parse(req.body);
      int uid = j["uid"].get<int>();
      int ret = friend_.agree(uid);
      if(ret == 0) {
        for(auto& u : ctx_.getFriendRequests()) {
          if(u.uid == uid) {
            ctx_.addFriend(u);
            break;
          }
        }
        ctx_.removeFriendRequest(uid);
      }
      res.set_content(json{{"status",ret}}.dump(), "application/json");
    } catch(...) {
      res.status=400;
      res.set_content(R"({"status":-1})","application/json");
    }
  });

  svr_.Post("/api/friend/reject",[&](const httplib::Request& req,httplib::Response& res) {
    try {
      auto j = json::parse(req.body);
      int ret = friend_.reject(j["uid"].get<int>());
      if(ret == 0) ctx_.removeFriendRequest(j["uid"].get<int>());
      res.set_content(json{{"status",ret}}.dump(), "application/json");
    } catch(...) {
      res.status=400;
      res.set_content(R"({"status":-1})","application/json");
    }
  });

  svr_.Post("/api/friend/block",[&](const httplib::Request& req,httplib::Response& res) {
    try {
      auto j = json::parse(req.body);
      int ret = friend_.block(j["uid"].get<int>());
      if(ret == 0) ctx_.addBlock(j["uid"].get<int>());
      res.set_content(json{{"status",ret}}.dump(),"application/json");
    } catch(...) {
      res.status=400;
      res.set_content(R"({"status":-1})","application/json");
    }
  });

  svr_.Post("/api/friend/unblock",[&](const httplib::Request& req,httplib::Response& res) {
    try {
      auto j = json::parse(req.body);
      int ret = friend_.unblock(j["uid"].get<int>());
      if(ret == 0) ctx_.unBlock(j["uid"].get<int>());
      res.set_content(json{{"status",ret}}.dump(),"application/json");
    } catch(...) {
      res.status=400;
      res.set_content(R"({"status":-1})","application/json");
    }
  });

  svr_.Get("/api/friend/blocks",[&](const httplib::Request&,httplib::Response& res) {
    if(friend_.getBlockList() != 0) {
      res.set_content(R"({"status":-1})","application/json");
      return;
    }
    res.set_content(json{{"status",0},{"blocks",ctx_.getBlockList()}}.dump(), "application/json");
  });

  // 聊天
  svr_.Post("/api/message/send",[&](const httplib::Request& req,httplib::Response& res) {
    try {
      auto j = json::parse(req.body);
      int ret = chat_.sendPrivateMessage(j["to_uid"].get<int>(),j["content"].get<std::string>());
      res.set_content(json{{"status",ret}}.dump(),"application/json");
    } catch(...) {
      res.status=400;
      res.set_content(R"({"status":-1})","application/json");
    }
  });

  svr_.Post("/api/message/group",[&](const httplib::Request& req,httplib::Response& res) {
    try {
      auto j = json::parse(req.body);
      int ret = chat_.sendGroupMessage(j["gid"].get<int>(), j["content"].get<std::string>());
      res.set_content(json{{"status",ret}}.dump(), "application/json");
    } catch(...) {
      res.status=400;
      res.set_content(R"({"status":-1})","application/json");
    }
  });

  svr_.Post("/api/messages/read",[&](const httplib::Request& req,httplib::Response& res) {
    try {
      auto j = json::parse(req.body);
      ctx_.markRead(j["peer_id"].get<int>());
      res.set_content(json{{"status",0}}.dump(),"application/json");
    } catch(...) {
      res.status=400;
      res.set_content(R"({"status":-1})","application/json");
    }
  });
  // 私聊历史
  svr_.Get("/api/messages",[&](const httplib::Request& req,httplib::Response& res) {
    if(!req.has_param("peer_uid")) {
      res.status=400;
      res.set_content(R"({"status":-1})","application/json");
      return;
    }
    int peer = std::stoi(req.get_param_value("peer_uid"));
    json arr = json::array();
    for(auto& m : ctx_.getMessage(peer)) {
      json item;
      to_json(item, m);
      arr.push_back(item);
    }
    res.set_content(json{{"status",0},{"messages",arr}}.dump(),"application/json");
  });

  // 群聊历史
  svr_.Get("/api/messages/group",[&](const httplib::Request& req,httplib::Response& res) {
    if(!req.has_param("gid")) {
      res.status=400;
      res.set_content(R"({"status":-1})","application/json");
      return;
    }
    int gid = std::stoi(req.get_param_value("gid"));
    json arr = json::array();
    for(auto& m : ctx_.getMessage(gid)) {
      json item; to_json(item, m);
      arr.push_back(item);
    }
    res.set_content(json{{"status",0},{"messages",arr}}.dump(), "application/json");
  });

  svr_.Post("/api/message/sync",[&](const httplib::Request&,httplib::Response& res) {
    res.set_content(json{{"status",chat_.syncHistory()}}.dump(),"application/json");
  });

  // 群组
  svr_.Get("/api/groups",[&](const httplib::Request&,httplib::Response& res) {
    if(group_.listMyGroups() != 0) {
      res.set_content(R"({"status":-1,"error":"获取群失败"})","application/json");
      return;
    }
    json arr = json::array();
    auto infoFromVec = [&](int gid, const std::vector<Message>& msgs)->nlohmann::json {
      nlohmann::json out = {{"time",(uint64_t)0},{"preview",""},{"from",""},{"uid",0},{"unread",0}};
      const Message* best = nullptr;
      int unread = 0;
      for(auto& m : msgs) {
        if(m.status == 0) unread++;
        if(!best || m.time > best->time) best = &m;
      }
      out["unread"] = unread;
      if(!best) return out;
      out["time"] = best->time;
      if(best->type == "file") {
        std::string nm;
        try { nm = json::parse(best->content).value("file_name",std::string()); } catch(...) {}
        bool img = false;
        auto dot = nm.find_last_of('.');
        if(dot != std::string::npos) {
          std::string ext = nm.substr(dot+1);
          for(auto& c : ext) c = (char)tolower((unsigned char)c);
          img = (ext=="png"||ext=="jpg"||ext=="jpeg"||ext=="gif"||ext=="webp"||ext=="bmp"||ext=="svg");
        }
        out["preview"] = img ? "[图片]" : "[文件]";
      } else {
        out["preview"] = best->content;
      }
      out["uid"] = best->from_uid;
      if(best->from_uid == ctx_.getSelf().uid) out["from"] = "我";
      else {
        if(ctx_.getGroupAllMembers(gid).empty()) {
          group_.listMembers(gid);
        }
        auto m = ctx_.getGroupMember(gid, best->from_uid);
        out["from"] = m ? m->usr_name : ("#"+std::to_string(best->from_uid));
      }
      return out;
    };
    auto groups = ctx_.getGroupList();
    for(auto& g : groups) {
      auto lm = infoFromVec(g.group_id, ctx_.getMessage(g.group_id));
      arr.push_back({{"group_id",g.group_id},{"name",g.name},{"owner_uid",g.owner_uid},{"last_time",lm["time"]},{"last_msg",lm["preview"]},{"last_from",lm["from"]},{"last_from_uid",lm["uid"]},{"history",false},{"unread",lm["unread"]}});
    }
    {
      std::unordered_set<int> gset;
      for(auto& g : groups) gset.insert(g.group_id);
      for(auto& [id, msgs] : ctx_.getAllMessages()) {
        if(msgs.empty() || msgs.front().chat_type != "group") continue;
        if(gset.count(id)) continue;
        auto lm = infoFromVec(id, msgs);
        arr.push_back({{"group_id",id},{"name",""},{"owner_uid",0},{"last_time",lm["time"]},{"last_msg",lm["preview"]},{"last_from",lm["from"]},{"last_from_uid",lm["uid"]},{"history",true},{"unread",lm["unread"]}});
      }
    }
    res.set_content(json{{"status",0},{"groups",arr}}.dump(),"application/json");
  });

  svr_.Post("/api/group/create",[&](const httplib::Request& req,httplib::Response& res) {
    try {
      auto j = json::parse(req.body);
      int gid = 0;
      int ret = group_.createGroup(j["name"].get<std::string>(),gid);
      res.set_content(json{{"status",ret},{"group_id",gid}}.dump(),"application/json");
    } catch(...) {
      res.status=400;
      res.set_content(R"({"status":-1})","application/json");
    }
  });

  svr_.Post("/api/group/disband",[&](const httplib::Request& req,httplib::Response& res) {
    try {
      auto j = json::parse(req.body);
      res.set_content(json{{"status",group_.disbandGroup(j["gid"].get<int>())}}.dump(),"application/json");
    } catch(...) {
      res.status=400;
      res.set_content(R"({"status":-1})","application/json");
    }
  });

  svr_.Post("/api/group/join",[&](const httplib::Request& req,httplib::Response& res) {
    try {
      auto j = json::parse(req.body);
      res.set_content(json{{"status",group_.requestJoin(j["gid"].get<int>())}}.dump(),"application/json");
    } catch(...) {
      res.status=400;
      res.set_content(R"({"status":-1})","application/json");
    }
  });

  svr_.Get("/api/group/members",[&](const httplib::Request& req,httplib::Response& res) {
    if(!req.has_param("gid")) {
      res.status=400;
      res.set_content(R"({"status":-1})","application/json");
      return;
    }
    int gid = std::stoi(req.get_param_value("gid"));
    if(group_.listMembers(gid) != 0) {
      res.set_content(R"({"status":-1})","application/json");
      return;
    }
    json arr = json::array();
    for(auto& m : ctx_.getGroupAllMembers(gid)) {
      json item;
      to_json(item, m);
      arr.push_back(item);
    }
    res.set_content(json{{"status",0},{"members",arr}}.dump(),"application/json");
  });

  svr_.Post("/api/group/leave",[&](const httplib::Request& req,httplib::Response& res) {
    try {
      auto j = json::parse(req.body);
      res.set_content(json{{"status",group_.leaveGroup(j["gid"].get<int>())}}.dump(),"application/json");
    } catch(...) {
      res.status=400;
      res.set_content(R"({"status":-1})","application/json");
    }
  });

  svr_.Post("/api/group/kick",[&](const httplib::Request& req,httplib::Response& res) {
    try {
      auto j = json::parse(req.body);
      res.set_content(json{{"status",group_.kickMember(j["gid"].get<int>(),j["target_uid"].get<int>())}}.dump(),"application/json");
    } catch(...) {
      res.status=400;
      res.set_content(R"({"status":-1})","application/json");
    }
  });

  svr_.Post("/api/group/set_admin",[&](const httplib::Request& req,httplib::Response& res) {
    try {
      auto j = json::parse(req.body);
      res.set_content(json{{"status",group_.setAdmin(j["gid"].get<int>(),j["target_uid"].get<int>(),j["is_admin"].get<bool>())}}.dump(),"application/json");
    } catch(...) {
      res.status=400;
      res.set_content(R"({"status":-1})","application/json");
    }
  });

  svr_.Post("/api/group/handle_join",[&](const httplib::Request& req,httplib::Response& res) {
    try {
      auto j = json::parse(req.body);
      res.set_content(json{{"status",group_.handleJoinRequest(j["gid"].get<int>(),j["target_uid"].get<int>(),j["approval"].get<bool>())}}.dump(),"application/json");
    } catch(...) {
      res.status=400;
      res.set_content(R"({"status":-1})","application/json");
    }
  });

  svr_.Get("/api/group/join_requests",[&](const httplib::Request& req,httplib::Response& res) {
    if(!req.has_param("gid")) {
      res.status=400;
      res.set_content(R"({"status":-1})","application/json");
      return;
    }
    int gid = std::stoi(req.get_param_value("gid"));
    std::vector<User> reqs;
    if(group_.listJoinRequests(gid, reqs) != 0) {
      res.set_content(R"({"status":-1})","application/json");
      return;
    }
    json arr = json::array();
    for(auto& u : reqs) arr.push_back({{"uid",u.uid},{"username",u.username}});
    res.set_content(json{{"status",0},{"requests",arr}}.dump(),"application/json");
  });

  // 登录同步（群申请）
  svr_.Get("/api/group/join_requests/all",[&](const httplib::Request&,httplib::Response& res) {
    if(group_.syncJoinRequests() != 0) {
      res.set_content(R"({"status":-1})", "application/json");
      return;
    }
    auto reqs = ctx_.getGroupJoinRequests();
    json arr = json::array();
    for(auto& [gid,list] : reqs) {
      json item;
      item["gid"] = gid;
      item["requests"] = json::array();
      for(auto& u : list) item["requests"].push_back({{"uid",u.uid},{"username",u.username}});
      arr.push_back(item);
    }
    res.set_content(json{{"status",0},{"groups",arr}}.dump(),"application/json");
  });

  svr_.Post("/api/group/remind",[&](const httplib::Request& req,httplib::Response& res) {
    try {
      auto j = json::parse(req.body);
      res.set_content(json{{"status",group_.setRemind(j["gid"].get<int>(),j["remind"].get<int>())}}.dump(),"application/json");
    } catch(...) {
      res.status=400;
      res.set_content(R"({"status":-1})","application/json");
    }
  });

  // 文件
  // 浏览器拖拽上传：前端直接流式发送文件原始字节（octet-stream）
  // 边收边写临时文件，避免整个文件读入内存导致 OOM
  svr_.Post("/api/file/upload",[&](const httplib::Request& req,httplib::Response& res,const httplib::ContentReader& content_reader) {
    try {
      std::string chat_type = req.get_param_value("chat_type");
      int target_id = 0;
      try { target_id = std::stoi(req.get_param_value("target_id")); } catch(...) {}
      std::string filename = req.get_param_value("filename");
      if(filename.empty()) filename = "unnamed";
      if(chat_type.empty() || target_id == 0) {
        res.status=400;
        res.set_content(R"({"status":-2})","application/json");
        return;
      }
      std::string tmp = "/tmp/chat_upload_" + std::to_string(ctx_.getSelf().uid) + "_" + std::to_string(now_ms());
      uint64_t received = 0;
      {
        std::ofstream ofs(tmp,std::ios::binary);
        bool ok = content_reader([&](const char* data,size_t data_length) {
          if(received + data_length > (uint64_t)MAX_FILE_SIZE) return false;   // 超过上限，中断接收
          received += data_length;
          ofs.write(data,(std::streamsize)data_length);
          return true;
        });
        ofs.close();
        if(!ok || received == 0) {
          unlink(tmp.c_str());
          if(received > (uint64_t)MAX_FILE_SIZE) {
            res.status=413;
            res.set_content(R"({"status":-3})","application/json");
          } else {
            res.set_content(R"({"status":-2})","application/json");
          }
          return;
        }
      }
      struct TmpGuard {
        std::string path;
        ~TmpGuard(){
          if(!path.empty()) unlink(path.c_str());
        }
      } guard{tmp};
      int ret = file_.uploadFile(tmp,chat_type,target_id,
        [&](uint64_t offset, uint64_t total) {
          broadcast(json{{"type","file_progress"},{"offset",offset},{"total",total}});
        },filename);
      broadcast(json{{"type","file_done"},{"status",ret}});
      logLine("上传文件: " + filename + " size=" + std::to_string(received)
        + " 目标=" + chat_type + ":" + std::to_string(target_id)
        + " 结果=" + std::to_string(ret));
      res.set_content(json{{"status",ret}}.dump(),"application/json");
    } catch(const std::exception& e) {
      std::cerr << "[WebUI] /api/file/upload 异常: " << e.what() << std::endl;
      res.status=400;
      res.set_content(R"({"status":-1})","application/json");
    }
  });

  // 下载到 ~/Download ，先写 <file_id>.downloading 临时文件，完成后重命名
  svr_.Post("/api/file/download",[&](const httplib::Request& req,httplib::Response& res) {
    try {
      auto j = json::parse(req.body);
      std::string file_id = j["file_id"].get<std::string>();
      std::string file_name = j.value("file_name",std::string());
      std::string dir = std::string(getenv("HOME")) + "/Download";
      mkdir(dir.c_str(),0755);
      std::string tmp = dir + "/" + file_id + ".downloading";

      uint64_t resume = 0;
      struct stat st;
      if(stat(tmp.c_str(), &st) == 0 && S_ISREG(st.st_mode)) resume = (uint64_t)st.st_size;

      int ret = file_.downloadFile(file_id,tmp,
        [&](uint64_t offset, uint64_t total) {
          broadcast(json{{"type","file_progress"},{"offset",offset},{"total",total}});
        }, resume);

      if(ret == 0) {
        std::string final_path = uniquePath(dir,file_name);
        if(std::rename(tmp.c_str(),final_path.c_str()) != 0) {
          ret = -2;
        } else {
          logLine("下载完成: " + final_path);
          broadcast(json{{"type","file_done"},{"status",0},{"save_path",final_path}});
          res.set_content(json{{"status",0},{"save_path",final_path}}.dump(),"application/json");
          return;
        }
      }
      // 如果失败，保留 .downloading 供下次续传
      logLine("下载文件: " + file_id + " 结果=" + std::to_string(ret));
      broadcast(json{{"type","file_done"},{"status",ret}});
      res.set_content(json{{"status",ret}}.dump(),"application/json");
    } catch(...) {
      res.status=400;
      res.set_content(R"({"status":-1})","application/json");
    }
  });

  // 图片预览
  svr_.Get("/api/file/preview",[&](const httplib::Request& req,httplib::Response& res) {
    if(!req.has_param("file_id")) {
      res.status=400;
      res.set_content("","text/plain");
      return;
    }
    std::string file_id = req.get_param_value("file_id");
    std::string name = req.has_param("name") ? req.get_param_value("name") : "preview.png";
    std::string mime = mimeForFile(name);
    {
      std::lock_guard<std::mutex> lock(cacheMutex_);
      auto it = previewCache_.find(file_id);
      if(it != previewCache_.end()) {
        res.set_content(it->second.data(), it->second.size(), mime);
        return;
      }
    }
    std::string tmp = "/tmp/chat_preview_" + file_id;
    int dl_ret = -1;
    for(int i = 0;i < 2 && dl_ret != 0;i++) {
      dl_ret = file_.downloadFile(file_id,tmp,nullptr);
    }
    if(dl_ret != 0) {
      res.status = 404;
      res.set_content("not found","text/plain");
      return;
    }
    std::ifstream f(tmp,std::ios::binary);
    std::string data((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());
    {
      std::lock_guard<std::mutex> lock(cacheMutex_);
      previewCache_[file_id] = data;
    }
    res.set_content(data.data(),data.size(),mime);
  });

  // WebSocket
  svr_.WebSocket("/ws",[&](const httplib::Request&, httplib::ws::WebSocket& ws) {
    logLine("浏览器已连接 (WebSocket)");
    addConnection(&ws);
    std::string msg;
    while(ws.read(msg)) {}
    removeConnection(&ws);
    logLine("浏览器已断开 (WebSocket)");
  });
}

void WebUI::addConnection(httplib::ws::WebSocket* ws) {
  std::lock_guard<std::mutex> lock(conn_mutex_);
  connections_.insert(ws);
}

void WebUI::removeConnection(httplib::ws::WebSocket* ws) {
  std::lock_guard<std::mutex> lock(conn_mutex_);
  connections_.erase(ws);
}

void WebUI::broadcast(const nlohmann::json& msg) {
  std::string data = msg.dump();
  std::lock_guard<std::mutex> lock(conn_mutex_);
  for(auto* ws : connections_) {
    try {
      ws->send(data);
    } catch(...) {}
  }
}
