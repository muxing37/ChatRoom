#pragma once
#include "third/httplib.h"
#include "shared.h"
#include "service.h"
#include "context.h"
#include <string>
#include <set>
#include <mutex>
#include <unordered_map>

// WebUI：C++ 客户端内嵌 HTTP + WebSocket 服务器，浏览器经 localhost 回环访问。
// 前端只通过 REST + WebSocket 与此类交互，所有真实网络由 Service 层走 TCP 到服务端。
class WebUI {
public:
  WebUI(
    AuthService& auth,
    FriendService& fri,
    ChatService& chat,
    GroupService& grp,
    FileService& file,
    ClientContext& ctx
  );
  ~WebUI();

  // 启动 HTTP + WebSocket 服务器（阻塞）
  void run(const char* host = "localhost");
  void stop();
  // 广播 JSON 消息给所有已连接的 WebSocket 客户端（推送/文件进度等）
  void broadcast(const nlohmann::json& msg);

private:
  void setupRoutes();
  void addConnection(httplib::ws::WebSocket* ws);
  void removeConnection(httplib::ws::WebSocket* ws);

  AuthService& auth_;
  FriendService& friend_;
  ChatService& chat_;
  GroupService& group_;
  FileService& file_;
  ClientContext& ctx_;

  httplib::Server svr_;
  std::set<httplib::ws::WebSocket*> connections_;
  std::mutex conn_mutex_;

  // 图片预览缓存：file_id -> 文件字节（首次拉取后缓存，避免重复下载）
  std::unordered_map<std::string,std::string> previewCache_;
  std::mutex cacheMutex_;
};