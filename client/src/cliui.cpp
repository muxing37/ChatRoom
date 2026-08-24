#include "cliui.h"
#include <iostream>
#include <limits>
#include <cstdlib>
#include <ctime>
#include <sys/stat.h>
#include <unistd.h>
#include <termios.h>

using json = nlohmann::json;

namespace {
  std::string sendErrorText(const json& reply) {
    std::string e = reply.value("error", std::string());
    if(e == "blocked by receiver") return "对方已将你屏蔽";
    if(e == "not friends") return "对方不是你的好友";
    if(e == "not member") return "你不在该群";
    if(e == "fake request") return "非法请求";
    if(e == "timeout") return "发送超时";
    if(e == "disconnected") return "连接已断开";
    return e.empty() ? "未知错误" : e;
  }
}

CliUI::CliUI(
  AuthService& authService,
  FriendService& friendService,
  ChatService& chatService,
  GroupService& groupService,
  FileService& fileService,
  ClientContext& ctx
) :
  authService_(authService),
  friendService_(friendService),
  chatService_(chatService),
  groupService_(groupService),
  fileService_(fileService),
  ctx_(ctx) {
  chatService_.setSendResultHandler([this](const json& reply,const std::string&,int){
    handleUiPush(json{{"type","send_result"},{"status",reply.value("status",-1)},{"error",reply.value("error","")}});
  });
}

CliUI::~CliUI() {}

bool CliUI::run() {
  while(running_) {
    if(!loginMenu()) return true;
    init();
    bool logout = false;
    while(running_ && !logout) {
      logout = mainMenu();
    }
    if(!running_) break;

    authService_.logout();
    ctx_.reset();
    std::cout << "\n已退出登录，回到登录界面\n";
  }
  return false;
}

bool CliUI::loginMenu() {
  while(true) {
    show(ClientState::LOGIN);
    int choice = inputChoice(1,3);
    if(choice == 3) {
      running_ = false;
      return false;
    }
    std::string username = readLine("用户名:");
    if(username.size() == 0) {
      std::cout << "用户名不能为空" << std::endl;
      continue;
    }
    std::string password = inputPassword("密码:");
    if(password.size() == 0) {
      std::cout << "密码不能为空" << std::endl;
      continue;
    }
    bool ok = false;
    if(choice == 1) {
      ok = authService_.login(username,password);
    } else {
      ok = authService_.regis(username,password);
    }
    if(ok) {
      std::cout << "登录成功！欢迎 " << username << " # " << ctx_.getSelf().uid << "\n";
      return true;
    }
    std::cout << "登录/注册失败，请重试\n";
  }
}

void CliUI::init() {
  std::cout << "正在同步初始数据...\n";
  if(friendService_.listFriend() != 0) std::cout << "  [警告] 同步好友列表失败\n";
  if(groupService_.listMyGroups() != 0) std::cout << "  [警告] 同步群列表失败\n";
  if(chatService_.syncHistory() != 0) std::cout << "  [警告] 同步聊天记录失败\n";
  groupService_.syncJoinRequests();
  friendService_.getBlockList();
  std::cout << "同步完成\n";
  showOfflineMessages();
}

void CliUI::showOfflineMessages() {
  uint64_t logout = ctx_.getLastLogoutTime();
  int self_uid = ctx_.getSelf().uid;
  struct Item {
    int id;
    bool isGroup;
    int count;
  };
  std::vector<Item> items;
  int total = 0;
  auto all = ctx_.getAllMessages();
  for(auto& [id, msgs] : all) {
    if(msgs.empty()) continue;
    bool isGroup = (msgs.front().chat_type == "group");
    int n = 0;
    for(auto& m : msgs) {
      if(m.time > logout && m.from_uid != self_uid) n++;
    }
    if(n > 0) {
      items.push_back({id,isGroup,n});
      total += n;
    }
  }
  if(items.empty()) return;
  std::cout << "[离线消息] 上次离线期间收到 " << total << " 条消息：\n";
  for(auto& it : items) {
    if(it.isGroup) {
      std::string name;
      for(auto& g : ctx_.getGroupList()) {
        if(g.group_id == it.id) { name = g.name; break; }
      }
      if(name.empty()) name = "#" + std::to_string(it.id);
      std::cout << "  群聊 [" << name << "] 有 " << it.count << " 条新消息\n";
    } else {
      auto f = ctx_.getFriend(it.id);
      std::string nm = f ? f->username : ("#" + std::to_string(it.id));
      std::cout << "  私聊 " << nm << "(uid:" << it.id << ") 发来 " << it.count << " 条\n";
    }
  }
}

bool CliUI::mainMenu() {
  show(ClientState::MAIN_MENU);
  switch(inputChoice(1,6)) {
    case 1: friendMenu(); break;
    case 2: privateChat(); break;
    case 3: groupMenu(); break;
    case 4: fileMenu(); break;
    case 5: deleteAccount(); break; // 注销账户
    case 6: return true; // 退出登录
  }
  return false;
}

void CliUI::friendMenu() {
  while(true) {
    show(ClientState::FRIEND_MENU);
    switch(inputChoice(1,6)) {
      case 1: showFriendList(); break;
      case 2: addFriend(); break;
      case 3: deleteFriend(); break;
      case 4: showFriendRequest(); break;
      case 5: blockMenu(); break;
      case 6: return;
    }
  }
}

void CliUI::showFriendList() {
  if(friendService_.listFriend() != 0) {
    std::cout << "获取好友列表失败\n";
    return;
  }
  auto friends = ctx_.getFriendList();
  std::cout << "\n好友列表(" << friends.size() << "):\n";
  int i = 0;
  for(auto &u : friends) {
    std::cout << "  " << ++i << ". " << u.uid << " " << u.username << (u.online ? " [在线]" : " [离线]") << '\n';
  }
}

void CliUI::addFriend() {
  int uid = inputUid("请输入uid:");
  if(friendService_.request(uid) == 0) {
    std::cout << "好友申请已发送\n";
  } else {
    std::cout << "发送失败\n";
  }
}

void CliUI::deleteFriend() {
  if(friendService_.listFriend() != 0) {
    std::cout << "获取好友列表失败\n";
    return;
  }
  auto friends = ctx_.getFriendList();
  if(friends.empty()) {
    std::cout << "暂无好友\n";
    return;
  }
  showFriendList();
  int choice = inputChoice(0,(int)friends.size(),"请输入要删除的好友序号(0返回)：");
  if(choice == 0) return;
  int uid = friends[choice-1].uid;
  if(friendService_.del(uid) == 0) {
    std::cout << "删除成功\n";
  } else {
    std::cout << "删除失败\n";
  }
}

void CliUI::showFriendRequest() {
  if(friendService_.listRequest() != 0) {
    std::cout << "获取好友申请失败\n";
    return;
  }
  auto list = ctx_.getFriendRequests();
  if(list.empty()) {
    std::cout << "暂无好友申请\n";
    return;
  }
  std::cout << "好友申请:\n";
  int i = 0;
  for(auto &u : list) {
    std::cout << "  " << ++i << ". " << u.uid << " " << u.username << '\n';
  }
  while(true) {
    int choice = inputChoice(0,(int)list.size(),"请输入要处理的申请序号(0退出):");
    if(choice == 0) return;
    int uid = list[choice-1].uid;
    std::cout << "1.同意\n2.拒绝\n";
    int ch = inputChoice(1,2);
    if(ch == 1) {
      if(friendService_.agree(uid) == 0) std::cout << "已同意\n";
      else std::cout << "操作失败\n";
    } else {
      if(friendService_.reject(uid) == 0) std::cout << "已拒绝\n";
      else std::cout << "操作失败\n";
    }
  }
}

void CliUI::blockMenu() {
  while(true) {
    std::cout << "\n----- 屏蔽管理 -----\n" << "1.屏蔽好友\n2.解除屏蔽\n3.查看屏蔽列表\n4.返回\n";
    switch(inputChoice(1,4)) {
      case 1: {
        if(friendService_.listFriend() != 0) {
          std::cout << "获取好友列表失败\n";
          break;
        }
        auto friends = ctx_.getFriendList();
        if(friends.empty()) {
          std::cout << "暂无好友\n";
          break;
        }
        showFriendList();
        int choice = inputChoice(0,(int)friends.size(),"请输入要屏蔽的好友序号(0返回)：");
        if(choice == 0) break;
        if(friendService_.block(friends[choice-1].uid) == 0) std::cout << "已屏蔽\n";
        else std::cout << "屏蔽失败\n";
        break;
      }
      case 2: {
        if(friendService_.getBlockList() != 0) {
          std::cout << "获取屏蔽列表失败\n";
          break;
        }
        auto list = ctx_.getBlockList();
        if(list.empty()) {
          std::cout << "屏蔽列表为空\n";
          break;
        }
        std::cout << "屏蔽列表:\n";
        for(size_t i = 0; i < list.size(); i++) {
          auto f = ctx_.getFriend(list[i]);
          std::string name = f ? f->username : ("#" + std::to_string(list[i]));
          std::cout << "  " << (i+1) << ". " << list[i] << " " << name << '\n';
        }
        int choice = inputChoice(0,(int)list.size(),"请输入要解除屏蔽的序号(0返回)：");
        if(choice == 0) break;
        if(friendService_.unblock(list[choice-1]) == 0) std::cout << "已解除屏蔽\n";
        else std::cout << "解除屏蔽失败\n";
        break;
      }
      case 3: {
        if(friendService_.getBlockList() == 0) {
          auto list = ctx_.getBlockList();
          if(list.empty()) std::cout << "屏蔽列表为空\n";
          else {
            std::cout << "屏蔽列表:\n";
            for(size_t i = 0; i < list.size(); i++) {
              auto f = ctx_.getFriend(list[i]);
              std::string name = f ? f->username : ("#" + std::to_string(list[i]));
              std::cout << "  " << (i+1) << ". " << list[i] << " " << name << '\n';
            }
          }
        } else {
          std::cout << "获取屏蔽列表失败\n";
        }
        break;
      }
      case 4: return;
    }
  }
}

void CliUI::privateChat() {
  if(friendService_.listFriend() != 0) {
    std::cout << "获取好友列表失败\n";
    return;
  }
  auto friends = ctx_.getFriendList();
  if(friends.empty()) {
    std::cout << "暂无好友，请先添加好友\n";
    return;
  }
  std::cout << "\n好友列表:\n";
  int i = 0;
  for(auto &u : friends) {
    std::cout << "  " << ++i << ". " << u.uid << " " << u.username << (u.online ? " [在线]" : " [离线]") << '\n';
  }
  int choice = inputChoice(0,(int)friends.size(),"请输入好友序号(0返回)：");
  if(choice == 0) return;
  chatLoop(friends[choice-1].uid,false);
}

void CliUI::chatLoop(int peerId,bool isGroup) {
  std::string title = isGroup ? "群聊" : "私聊";

  auto hist = ctx_.getMessage(peerId);
  if(!hist.empty()) {
    std::cout << "\n----- 历史消息 -----\n";
    for(auto& m : hist) printMessage(m);
    std::cout << "--------------------\n";
  }
  chatPrinted_ = hist.size();
  std::cout << "进入" << title << "，输入 /exit 退出，/upload 发送文件，/download 下载文件\n";
  inChat_ = true;
  chatPeer_ = peerId;
  chatIsGroup_ = isGroup;
  while(running_) {
    std::string line = readLine(""); //"[" + title + "] "
    if(line == "/exit") break;
    if(line == "/upload") {
      uploadFileToChat(peerId,isGroup);
      continue;
    }
    if(line == "/download") {
      downloadFile();
      continue;
    }
    if(line.empty()) continue;

    int rc;
    if(isGroup) rc = chatService_.sendGroupMessage(peerId,line);
    else rc = chatService_.sendPrivateMessage(peerId,line);
    if(rc != 0) {
      std::cout << "发送失败\n";
    }
  }
  inChat_ = false;
  std::cout << "已退出" << title << "\n";
}

void CliUI::printNewMessages(int peerId) {
  auto msgs = ctx_.getMessagesFrom(peerId,chatPrinted_);
  for(auto& m : msgs) printMessage(m);
  chatPrinted_ += msgs.size();
}

std::vector<std::string> CliUI::formatNewMessages(int peerId) {
  auto msgs = ctx_.getMessagesFrom(peerId,chatPrinted_);
  chatPrinted_ += msgs.size();
  std::vector<std::string> out;
  for(auto& m : msgs) out.push_back(formatMessage(m));
  return out;
}

std::string CliUI::formatMessage(const Message& msg) {
  std::string from;
  if(msg.chat_type == "group") {
    auto m = ctx_.getGroupMember(msg.target_id,msg.from_uid);
    from = m ? m->usr_name : ("#" + std::to_string(msg.from_uid));
  } else if(msg.from_uid == ctx_.getSelf().uid) {
    from = "我";
  } else {
    auto f = ctx_.getFriend(msg.from_uid);
    from = f ? f->username : ("#" + std::to_string(msg.from_uid));
  }

  std::string out = "[" + formatTime(msg.time) + "] " + from + ": ";
  if(msg.type == "text") {
    out += msg.content;
  } else if(msg.type == "file") {
    try {
      auto fj = json::parse(msg.content);
      out += "[文件] " + fj.value("file_name",std::string("未知文件")) + " (id:" + fj.value("file_id",std::string("")) + ")";
    } catch(...) {
      out += "[文件] " + msg.content;
    }
  } else {
    out += "[" + msg.type + "] " + msg.content;
  }
  return out;
}

void CliUI::printMessage(const Message& msg) {
  std::cout << formatMessage(msg) << '\n';
}

void CliUI::uploadFileToChat(int peerId,bool isGroup) {
  std::string path = readLine("请输入文件路径:");
  if(path.empty()) return;
  std::string chat_type = isGroup ? "group" : "private";
  int rc = fileService_.uploadFile(path,chat_type,peerId,
    [](uint64_t done,uint64_t total){
      int pct = (int)(done*100/total);
      std::cout << "\r上传中: " << pct << "% (" << done << "/" << total << ")  ";
      std::cout.flush();
    });
  if(rc == 0) std::cout << "\n上传完成\n";
  else std::cout << "\n上传失败(code=" << rc << ")\n";
}

void CliUI::groupMenu() {
  while(true) {
    show(ClientState::GROUP_MENU);
    switch(inputChoice(1,8)) {
      case 1: showGroupList(); break;
      case 2: createGroup(); break;
      case 3: joinGroup(); break;
      case 4: groupRoom(); break;
      case 5: groupManage(); break;
      case 6: disbandGroup(); break;
      case 7: leaveGroup(); break;
      case 8: return;
    }
  }
}

void CliUI::showGroupList() {
  if(groupService_.listMyGroups() != 0) {
    std::cout << "获取群聊列表失败\n";
    return;
  }
  auto groups = ctx_.getGroupList();
  if(groups.empty()) {
    std::cout << "还没有加入任何群聊\n";
    return;
  }
  std::cout << "\n我的群聊:\n";
  int i = 0;
  for(auto& g : groups) {
    std::cout << "  " << ++i << ". [" << g.group_id << "] " << g.name << " (群主:" << g.owner_uid << ")\n";
  }
}

void CliUI::createGroup() {
  std::string name = readLine("请输入群名称:");
  if(name.empty()) {
    std::cout << "群名称不能为空\n";
    return;
  }
  int gid = 0;
  if(groupService_.createGroup(name,gid) == 0) {
    std::cout << "创建成功！群号: " << gid << "\n";
  } else {
    std::cout << "创建失败\n";
  }
}

void CliUI::joinGroup() {
  int gid = inputUid("请输入群号:");
  if(groupService_.requestJoin(gid) == 0) {
    std::cout << "入群申请已发送，等待群主/管理员审批\n";
  } else {
    std::cout << "入群申请发送失败\n";
  }
}

void CliUI::groupRoom() {
  if(groupService_.listMyGroups() != 0) {
    std::cout << "获取群聊列表失败\n";
    return;
  }
  auto groups = ctx_.getGroupList();
  if(groups.empty()) {
    std::cout << "还没有加入任何群聊\n";
    return;
  }
  showGroupList();
  int choice = inputChoice(0,(int)groups.size(),"请输入群序号(0返回)：");
  if(choice == 0) return;
  chatLoop(groups[choice-1].group_id,true);
}

int CliUI::myGroupPermission(int gid) {
  int perm = ctx_.getSelfPermission(gid);
  if(perm >= 0) return perm;
  if(groupService_.listMembers(gid) != 0) return -1;
  return ctx_.getSelfPermission(gid);
}

void CliUI::groupManage() {
  if(groupService_.listMyGroups() != 0) {
    std::cout << "获取群聊列表失败\n";
    return;
  }
  auto groups = ctx_.getGroupList();
  if(groups.empty()) {
    std::cout << "还没有加入任何群聊\n";
    return;
  }
  std::vector<GroupInfo> manageable;
  std::vector<int> perms;
  for(auto& g : groups) {
    int perm;
    if(g.owner_uid == ctx_.getSelf().uid) {
      perm = 0;
    } else {
      perm = myGroupPermission(g.group_id);
      if(perm != 1) continue;
    }
    manageable.push_back(g);
    perms.push_back(perm);
  }
  if(manageable.empty()) {
    std::cout << "没有你有权限管理的群\n";
    return;
  }
  std::cout << "\n可管理的群:\n";
  for(size_t i = 0;i < manageable.size();i++) {
    std::string role = (perms[i] == 0) ? "群主" : "管理员";
    std::cout << "  " << (i+1) << ". [" << manageable[i].group_id << "] " << manageable[i].name << " (" << role << ")\n";
  }
  int choice = inputChoice(0,(int)manageable.size(),"请选择要管理的群(0返回)：");
  if(choice == 0) return;
  int gid = manageable[choice-1].group_id;
  bool isOwner = (perms[choice-1] == 0);

  while(true) {
    std::cout << "\n--- 群管理 ---\n";
    if(isOwner) {
      std::cout << "1.查看群成员\n2.处理入群申请\n3.设置管理员\n4.踢出成员\n5.返回\n";
      switch(inputChoice(1,5)) {
        case 1:
          groupService_.listMembers(gid);
          printMembers(ctx_.getGroupAllMembers(gid));
          break;
        case 2: handleGroupJoinRequest(gid); break;
        case 3: setAdmin(gid); break;
        case 4: kickMember(gid); break;
        case 5: return;
      }
    } else {
      std::cout << "1.查看群成员\n2.处理入群申请\n3.踢出成员\n4.返回\n";
      switch(inputChoice(1,4)) {
        case 1:
          groupService_.listMembers(gid);
          printMembers(ctx_.getGroupAllMembers(gid));
          break;
        case 2: handleGroupJoinRequest(gid); break;
        case 3: kickMember(gid); break;
        case 4: return;
      }
    }
  }
}

void CliUI::handleGroupJoinRequest(int gid) {
  std::vector<User> reqs;
  if(groupService_.listJoinRequests(gid,reqs) != 0) {
    std::cout << "获取入群申请失败\n";
    return;
  }
  if(reqs.empty()) {
    std::cout << "暂无入群申请\n";
    return;
  }
  std::cout << "入群申请:\n";
  int i = 0;
  for(auto& u : reqs) {
    std::cout << "  " << ++i << ". " << u.uid << " " << u.username << '\n';
  }
  while(true) {
    int choice = inputChoice(0,(int)reqs.size(),"请输入要处理的申请序号(0退出):");
    if(choice == 0) return;
    int uid = reqs[choice-1].uid;
    std::cout << "1.同意\n2.拒绝\n";
    int ch = inputChoice(1,2);
    if(groupService_.handleJoinRequest(gid,uid,ch==1) == 0) {
      std::cout << "操作成功\n";
    } else {
      std::cout << "操作失败\n";
    }
  }
}

void CliUI::setAdmin(int gid) {
  groupService_.listMembers(gid);
  auto members = ctx_.getGroupAllMembers(gid);
  if(members.empty()) {
    std::cout << "群成员为空\n";
    return;
  }
  printMembers(members);
  int choice = inputChoice(0,(int)members.size(),"请输入要设置的成员序号(0返回):");
  if(choice == 0) return;
  int uid = members[choice-1].uid;
  std::cout << "1.设为管理员\n2.取消管理员\n";
  int ch = inputChoice(1,2);
  if(groupService_.setAdmin(gid,uid,ch==1) == 0) {
    std::cout << "操作成功\n";
  } else {
    std::cout << "操作失败\n";
  }
}

void CliUI::kickMember(int gid) {
  groupService_.listMembers(gid);
  auto members = ctx_.getGroupAllMembers(gid);
  if(members.empty()) {
    std::cout << "群成员为空\n";
    return;
  }
  printMembers(members);
  int choice = inputChoice(0,(int)members.size(),"请输入要踢出的成员序号(0返回):");
  if(choice == 0) return;
  int uid = members[choice-1].uid;
  if(groupService_.kickMember(gid,uid) == 0) {
    std::cout << "已踢出\n";
  } else {
    std::cout << "踢出失败\n";
  }
}

void CliUI::leaveGroup() {
  if(groupService_.listMyGroups() != 0) {
    std::cout << "获取群聊列表失败\n";
    return;
  }
  auto groups = ctx_.getGroupList();
  if(groups.empty()) {
    std::cout << "还没有加入任何群聊\n";
    return;
  }
  showGroupList();
  int choice = inputChoice(0,(int)groups.size(),"请选择要退出的群(0返回)：");
  if(choice == 0) return;
  int gid = groups[choice-1].group_id;
  if(groupService_.leaveGroup(gid) == 0) {
    std::cout << "已退出群聊\n";
  } else {
    std::cout << "退出失败\n";
  }
}

void CliUI::disbandGroup() {
  if(groupService_.listMyGroups() != 0) {
    std::cout << "获取群聊列表失败\n";
    return;
  }
  auto groups = ctx_.getGroupList();
  if(groups.empty()) {
    std::cout << "还没有加入任何群聊\n";
    return;
  }
  std::vector<GroupInfo> ownable;
  for(auto& g : groups) {
    if(g.owner_uid == ctx_.getSelf().uid) ownable.push_back(g);
  }
  if(ownable.empty()) {
    std::cout << "没有你有权解散的群（仅群主可解散）\n";
    return;
  }
  std::cout << "\n可解散的群:\n";
  for(size_t i = 0; i < ownable.size(); i++) {
    std::cout << "  " << (i+1) << ". [" << ownable[i].group_id << "] " << ownable[i].name << "\n";
  }
  int choice = inputChoice(0,(int)ownable.size(),"请选择要解散的群(0返回)：");
  if(choice == 0) return;
  int gid = ownable[choice-1].group_id;
  if(groupService_.disbandGroup(gid) == 0) {
    std::cout << "群已解散\n";
  } else {
    std::cout << "解散失败\n";
  }
}

void CliUI::printMembers(const std::vector<GroupMember>& members) {
  if(members.empty()) {
    std::cout << "群成员为空\n";
    return;
  }
  std::cout << "\n群成员(" << members.size() << "):\n";
  int i = 0;
  for(auto& m : members) {
    std::string role = (m.permission == 0) ? "群主" : (m.permission == 1) ? "管理员" : "成员";
    std::cout << "  " << ++i << ". " << m.uid << " " << m.usr_name << " [" << role << "]\n";
  }
}

void CliUI::fileMenu() {
  while(true) {
    show(ClientState::FILE_MENU);
    switch(inputChoice(1,3)) {
      case 1: uploadFile(); break;
      case 2: downloadFile(); break;
      case 3: return;
    }
  }
}

void CliUI::uploadFile() {
  std::string path = readLine("请输入文件路径:");
  if(path.empty()) return;
  std::cout << "目标类型: 1=私聊 2=群聊\n";
  std::string chat_type = (inputChoice(1,2) == 1) ? "private" : "group";
  int target_id = 0;
  if(chat_type == "private") {
    if(friendService_.listFriend() != 0) {
      std::cout << "获取好友列表失败\n";
      return;
    }
    auto friends = ctx_.getFriendList();
    if(friends.empty()) {
      std::cout << "暂无好友\n";
      return;
    }
    showFriendList();
    int choice = inputChoice(0,(int)friends.size(),"请输入好友序号(0取消)：");
    if(choice == 0) return;
    target_id = friends[choice-1].uid;
  } else {
    if(groupService_.listMyGroups() != 0) {
      std::cout << "获取群聊列表失败\n";
      return;
    }
    auto groups = ctx_.getGroupList();
    if(groups.empty()) {
      std::cout << "还没有加入任何群聊\n";
      return;
    }
    showGroupList();
    int choice = inputChoice(0,(int)groups.size(),"请输入群序号(0取消)：");
    if(choice == 0) return;
    target_id = groups[choice-1].group_id;
  }
  int rc = fileService_.uploadFile(path,chat_type,target_id,
    [](uint64_t done,uint64_t total){
      int pct = (int)(done*100/total);
      std::cout << "\r上传中: " << pct << "% (" << done << "/" << total << ")  ";
      std::cout.flush();
    });
  if(rc == 0) std::cout << "\n上传完成\n";
  else std::cout << "\n上传失败(code=" << rc << ")\n";
}

void CliUI::downloadFile() {
  std::string file_id = readLine("请输入file_id:");
  std::string file_name = readLine("请输入file_name:");
  if(file_id.empty()) return;
  std::string dir = std::string(getenv("HOME")) + "/Download";
  mkdir(dir.c_str(),0755);
  std::string save_path = dir + "/" + file_id + ".downloading";
  std::string final_path = dir + "/" + file_name;
  uint64_t resume = 0;
  struct stat st;
  if(stat(save_path.c_str(),&st) == 0 && S_ISREG(st.st_mode)) resume = (uint64_t)st.st_size;
  int rc = fileService_.downloadFile(file_id,save_path,
    [](uint64_t done,uint64_t total){
      int pct = (int)(done*100/total);
      std::cout << "\r下载中: " << pct << "% (" << done << "/" << total << ")  ";
      std::cout.flush();
    },resume);
  std::rename(save_path.c_str(),final_path.c_str());
  if(rc == 0) std::cout << "\n下载完成: " << final_path << "\n";
  else std::cout << "\n下载失败(code=" << rc << ")\n";
}

void CliUI::deleteAccount() {
  std::cout << "警告：注销账户将删除账号，且不可恢复！\n";
  std::string pwd = inputPassword("请输入密码确认:");
  if(authService_.delauth(pwd)) {
    std::cout << "账户已注销\n";
    running_ = false;
  } else {
    std::cout << "注销失败\n";
  }
}

void CliUI::show(ClientState state) {
  const std::vector<std::string>* menu = nullptr;
  std::string title;

  switch(state) {
    case ClientState::LOGIN: menu=&menu_login_; title="登录菜单"; break;
    case ClientState::MAIN_MENU: menu=&menu_main_; title="主菜单"; break;
    case ClientState::FRIEND_MENU: menu=&menu_friend_; title="好友管理"; break;
    case ClientState::GROUP_MENU: menu=&menu_group_; title="群聊"; break;
    case ClientState::GROUP_DETAIL_MENU: menu=&menu_group_detail_; title="群管理"; break;
    case ClientState::FILE_MENU: menu=&menu_file_; title="文件传输"; break;
    default: return;
  }

  std::cout << "\n--- " << title << " ---\n";
  for(size_t i = 0;i < menu->size();i++) {
    std::cout << i+1 << ". " << (*menu)[i] << '\n';
  }
}

int CliUI::inputChoice(int min,int max,const std::string& prompt) {
  while(true) {
    std::string s = readLine(prompt);
    try {
      int c = std::stoi(s);
      if(c >= min && c <= max) return c;
    } catch(...) {}
    std::cout << "请输入 " << min << "-" << max << "\n";
  }
}

// std::string CliUI::inputString(const std::string& prompt) {
//   return readLine(prompt);
// }

std::string CliUI::inputPassword(const std::string& prompt) {
  return readLine(prompt,true);
}

int CliUI::inputUid(const std::string& prompt) {
  while(true) {
    std::string s = readLine(prompt);
    try {
      return std::stoi(s);
    } catch(...) {
      std::cout << "请输入合法数字\n";
    }
  }
}

static bool hasSpecialKey(const std::string& line) {
  for(unsigned char c : line) {
    if(c == 0x1B || c < 0x20 || c == 0x7F) return true;
  }
  return false;
}

std::string CliUI::readLine(const std::string& prompt,bool hidden) {
  termios oldt{}, newt{};
  bool tty = isatty(STDIN_FILENO) == 1;
  if(hidden && tty) {
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
  }
  if(!prompt.empty()) {
    std::lock_guard<std::mutex> lock(outMtx_);
    std::cout << prompt;
    std::cout.flush();
  }
  std::string line;
  while(true) {
    std::getline(std::cin, line);
    if(std::cin.eof()) { running_ = false; line.clear(); break; }
    if(!hasSpecialKey(line)) break;
    {
      std::lock_guard<std::mutex> lock(outMtx_);
      std::cout << "\n检测到特殊按键，已忽略，请重新输入\n" << prompt;
      std::cout.flush();
    }
    line.clear();
  }
  if(hidden && tty) {
    tcsetattr(STDIN_FILENO,TCSANOW,&oldt);
    std::cout << "\n";
  }
  return line;
}

bool CliUI::isChatOf(const Message& msg) {
  if(chatIsGroup_) return msg.chat_type == "group" && msg.target_id == chatPeer_;
  return msg.chat_type == "private" && (msg.from_uid == chatPeer_ || msg.target_id == chatPeer_);
}

std::string CliUI::chatSourceName(const Message& msg) {
  if(msg.chat_type == "group") {
    std::string name;
    for(auto& g : ctx_.getGroupList()) {
      if(g.group_id == msg.target_id) {
        name = g.name;
        break;
      }
    }
    if(name.empty()) return "群聊 #" + std::to_string(msg.target_id);
    return "群聊 [" + name + "] (gid:" + std::to_string(msg.target_id) + ")";
  }
  if(msg.from_uid == ctx_.getSelf().uid) {
    auto f = ctx_.getFriend(msg.target_id);
    if(f) return "私聊 发给 " + f->username + " (uid:" + std::to_string(msg.target_id) + ")";
    return "私聊 发给 #" + std::to_string(msg.target_id);
  }
  auto f = ctx_.getFriend(msg.from_uid);
  if(f) return "私聊 来自 " + f->username + " (uid:" + std::to_string(msg.from_uid) + ")";
  return "私聊 来自 #" + std::to_string(msg.from_uid);
}

void CliUI::handleChatPush(const json& push) {
  Message m;
  try {
    m = push["data"].get<Message>();
  } catch(...) {
    return;
  }
  if(m.chat_type == "private" && m.from_uid != ctx_.getSelf().uid && !ctx_.getFriend(m.from_uid)) {
    return;
  }
  std::lock_guard<std::mutex> lock(outMtx_);
  if(inChat_ && isChatOf(m)) {
    std::cout << "\n" << formatMessage(m) << "\n";
  } else {
    std::cout << "\n[" << formatTime(m.time) << "] " << chatSourceName(m) << "\n";
  }
  std::cout.flush();
}

void CliUI::handleUiPush(const json& push) {
  std::string type = push.value("type","");
  if(type == "send_result") {
    if(push.value("status",-1) != 0) {
      std::lock_guard<std::mutex> lock(outMtx_);
      std::cout << "发送失败：" << sendErrorText(push) << "\n";
    }
    return;
  }
  if(type == "friend") {
    std::cout << "\n";
    std::lock_guard<std::mutex> lock(outMtx_);
    std::string action = push.value("action","");
    std::string name = push.value("data",json()).value("username",std::string());
    if(action == "request") std::cout << "[好友申请] " << name << " 请求添加你为好友\n";
    else if(action == "agree") std::cout << "[好友] " << name << " 已同意你的好友申请\n";
    else if(action == "del") std::cout << "[好友] " << name << " 已删除好友关系\n";
    else if(action == "online") std::cout << "[好友] " << name << " 上线了\n";
    else if(action == "offline") std::cout << "[好友] " << name << " 下线了\n";
    return;
  }
  if(type == "group") {
    std::lock_guard<std::mutex> lock(outMtx_);
    std::cout << "\n";
    std::cout << "[群聊] 有新的动态（" << push.value("action",std::string("?")) << "）\n";
    return;
  }
}

std::string CliUI::formatTime(uint64_t t) {
  if(t == 0) return "--:--";
  time_t sec = (time_t)(t / 1000);
  struct tm tmv;
  localtime_r(&sec,&tmv);
  char buf[32];
  strftime(buf,sizeof(buf),"%m-%d %H:%M",&tmv);
  return buf;
}
