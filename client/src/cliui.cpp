#include "cliui.h"
#include <iostream>
#include <limits>
#include <cstdlib>
#include <ctime>
#include <sys/stat.h>

using json = nlohmann::json;

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
  ctx_(ctx) {}

void CliUI::notifyPush() {
  pushSeq_++;
}

void CliUI::run() {
  while(running_) {
    if(!loginMenu()) break;
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
}

bool CliUI::loginMenu() {
  while(true) {
    show(ClientState::LOGIN);
    int choice = inputChoice(1,3);
    if(choice == 3) {
      running_ = false;
      return false;
    }
    std::string username = inputString("用户名:");
    if(username.size() == 0) {
      std::cout << "用户名不能为空" << std::endl;
      continue;
    }
    std::string password = inputString("密码:");
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
      std::cout << "登录成功！欢迎 " << username << "\n";
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
}

bool CliUI::mainMenu() {
  if(pushSeq_.load() > shownSeq_) {
    shownSeq_ = pushSeq_.load();
    std::cout << "[通知] 有新的消息/申请到达，可进入 私聊/群聊/好友申请 查看\n";
  }
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
  showFriendList();
  int uid = inputUid("请输入要删除的好友uid(0返回):");
  if(uid == 0) return;
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
  for(auto &u : list) {
    std::cout << "  " << u.uid << " " << u.username << '\n';
  }
  while(true) {
    int uid = inputUid("请输入要处理的uid(0退出):");
    if(uid == 0) return;
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
    std::cout << "\n----- 屏蔽管理 -----\n" << "1.屏蔽好友\n2.解除屏蔽\n3.查看屏蔽列表\n4.返回\n请选择：";
    switch(inputChoice(1,4)) {
      case 1: {
        int uid = inputUid("请输入要屏蔽的uid:");
        if(friendService_.block(uid) == 0) std::cout << "已屏蔽\n";
        else std::cout << "屏蔽失败\n";
        break;
      }
      case 2: {
        int uid = inputUid("请输入要解除屏蔽的uid:");
        if(friendService_.unblock(uid) == 0) std::cout << "已解除屏蔽\n";
        else std::cout << "解除屏蔽失败\n";
        break;
      }
      case 3: {
        if(friendService_.getBlockList() == 0) {
          auto list = ctx_.getBlockList();
          if(list.empty()) std::cout << "屏蔽列表为空\n";
          else {
            std::cout << "屏蔽列表:\n";
            for(int id : list) std::cout << "  " << id << '\n';
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
  std::cout << "请输入好友序号(0返回)：";
  int choice = inputChoice(0,(int)friends.size());
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
  shownSeq_ = pushSeq_.load();
  std::cout << "进入" << title << "，输入 /exit 退出，/upload 发送文件，/download 下载文件\n";

  std::string line;
  while(running_) {
    if(!std::getline(std::cin,line)) break;
    if(line == "/exit") break;
    if(line == "/upload") {
      uploadFileToChat(peerId,isGroup);
      printNewMessages(peerId);
      shownSeq_ = pushSeq_.load();
      continue;
    }
    if(line == "/download") {
      downloadFile();
      printNewMessages(peerId);
      shownSeq_ = pushSeq_.load();
      continue;
    }
    if(line.empty()) continue;

    int rc;
    if(isGroup) rc = chatService_.sendGroupMessage(peerId,line);
    else        rc = chatService_.sendPrivateMessage(peerId,line);
    if(rc != 0) {
      std::cout << "发送失败\n";
    } else {
      printNewMessages(peerId);
      shownSeq_ = pushSeq_.load();
    }
  }
  std::cout << "已退出" << title << "\n";
}

void CliUI::printNewMessages(int peerId) {
  auto msgs = ctx_.getMessage(peerId);
  while(chatPrinted_ < msgs.size()) {
    printMessage(msgs[chatPrinted_++]);
  }
}

void CliUI::printMessage(const Message& msg) {
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

  std::cout << "[" << formatTime(msg.time) << "] " << from << ": ";
  if(msg.type == "text") {
    std::cout << msg.content << '\n';
  } else if(msg.type == "file") {
    try {
      auto fj = json::parse(msg.content);
      std::cout << "[文件] " << fj.value("file_name","未知文件") << " (id:" << fj.value("file_id","") << ")\n";
    } catch(...) {
      std::cout << "[文件] " << msg.content << '\n';
    }
  } else {
    std::cout << "[" << msg.type << "] " << msg.content << '\n';
  }
}

void CliUI::uploadFileToChat(int peerId,bool isGroup) {
  std::string path = inputString("请输入文件路径:");
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
  std::string name = inputString("请输入群名称:");
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
  std::cout << "请输入群序号(0返回)：";
  int choice = inputChoice(0,(int)groups.size());
  if(choice == 0) return;
  chatLoop(groups[choice-1].group_id,true);
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
  showGroupList();
  std::cout << "请选择要管理的群(0返回)：";
  int choice = inputChoice(0,(int)groups.size());
  if(choice == 0) return;
  int gid = groups[choice-1].group_id;

  while(true) {
    show(ClientState::GROUP_DETAIL_MENU);
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
  for(auto& u : reqs) {
    std::cout << "  " << u.uid << " " << u.username << '\n';
  }
  while(true) {
    int uid = inputUid("请输入要处理的用户uid(0退出):");
    if(uid == 0) return;
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
  printMembers(ctx_.getGroupAllMembers(gid));
  int uid = inputUid("请输入要设置的成员uid(0返回):");
  if(uid == 0) return;
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
  printMembers(ctx_.getGroupAllMembers(gid));
  int uid = inputUid("请输入要踢出的成员uid(0返回):");
  if(uid == 0) return;
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
  std::cout << "请选择要退出的群(0返回)：";
  int choice = inputChoice(0,(int)groups.size());
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
  showGroupList();
  std::cout << "请选择要解散的群(0返回)：";
  int choice = inputChoice(0,(int)groups.size());
  if(choice == 0) return;
  int gid = groups[choice-1].group_id;
  if(groupService_.disbandGroup(gid) == 0) {
    std::cout << "群已解散\n";
  } else {
    std::cout << "解散失败（仅群主可解散）\n";
  }
}

void CliUI::printMembers(const std::vector<GroupMember>& members) {
  if(members.empty()) {
    std::cout << "群成员为空\n";
    return;
  }
  std::cout << "\n群成员(" << members.size() << "):\n";
  for(auto& m : members) {
    std::string role = (m.permission == 0) ? "群主" : (m.permission == 1) ? "管理员" : "成员";
    std::cout << "  " << m.uid << " " << m.usr_name << " [" << role << "]\n";
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
  std::string path = inputString("请输入文件路径:");
  if(path.empty()) return;
  std::cout << "目标类型: 1=私聊 2=群聊\n";
  std::string chat_type = (inputChoice(1,2) == 1) ? "private" : "group";
  int target_id = inputUid("请输入目标uid/gid:");
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
  std::string file_id = inputString("请输入file_id:");
  std::string file_name = inputString("请输入file_name:");
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
  std::string pwd = inputString("请输入密码确认:");
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
  std::cout << "请选择：";
}

int CliUI::inputChoice(int min,int max) {
  int choice;
  while(true) {
    std::cin >> choice;
    if(std::cin.fail()) {
      std::cin.clear();
      std::cin.ignore(std::numeric_limits<std::streamsize>::max(),'\n');
      continue;
    }
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(),'\n');
    if(choice >= min && choice <= max) return choice;
    std::cout << "请输入 " << min << "-" << max << "：";
  }
}

std::string CliUI::inputString(const std::string& prompt) {
  std::cout << prompt;
  std::string s;
  std::getline(std::cin,s);
  return s;
}

int CliUI::inputUid(const std::string& prompt) {
  while(true) {
    std::string s = inputString(prompt);
    try {
      return std::stoi(s);
    } catch(...) {
      std::cout << "请输入合法数字\n";
    }
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
