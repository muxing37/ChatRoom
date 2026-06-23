#include "client.h"
#include "user.h"
#include "manager.h"
#include <thread>

#define MAX_PATH 1024
// int running=0;
MenuManager menu;

class TcpClient {
  public:
  TcpClient();

  TcpSocket* getSocket() const { return socket_.get(); }

  bool connectToHost(const char* ip, unsigned short port);

  private:
  std::unique_ptr<TcpSocket> socket_;
};

std::string prompt;
void handle_SIGINT(int sig) {
    std::cout << "\n" << prompt << std::flush;
}

void handle_SIGTSTP(int sig) {
    // if(running==1) {
    printf("\n");
    // }
}

void handle_signal(){
    signal(SIGINT,handle_SIGINT);
    signal(SIGTSTP,handle_SIGTSTP);
}

void restore_signal() {
    signal(SIGINT,SIG_DFL);
    signal(SIGTSTP,SIG_DFL);
}

TcpClient::TcpClient() : socket_(nullptr) {}

bool TcpClient::connectToHost(const char* ip, unsigned short port) {
    int fd=socket(AF_INET,SOCK_STREAM,0);

    if(fd<0) {
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    inet_pton(AF_INET,ip,&addr.sin_addr);

    if(connect(fd,(sockaddr*)&addr,sizeof(addr)) < 0) {
        close(fd);
        return false;
    }

    socket_=std::make_unique<TcpSocket>(fd);

    return true;
}

void recvthread(TcpSocket& sock) {
    while(true) {
        std::string res;
        NetResult ret = sock.recvMsg(res);
        if(ret != NetResult::OK) break;
        std::cout << "\n" << res << "\n" <<std::flush;
    }
}

void sendthread(TcpSocket& sock,User usr) {
    while(true) {
        int choice;
        menu.show(ClientState::MAIN_MENU);
        std::cin >> choice;
        while(choice == 1) {
            menu.show(ClientState::FRIEND_MENU);
            std::cin >>choice;
            if(choice == 1) {
                //friend_list();
            } else if(choice == 2) {
                //friend_add();
                std::string id;
                while(id.empty()) {
                    std::cout << "请输入对方uid：\n";
                    std::cin >> id;
                }
                nlohmann::json j;
                j["type"] = "friend";
                j["action"] = "request";
                j["from_uid"] = std::to_string(usr.uid);
                j["to_uid"] = id;
                sock.sendMsg(j.dump());
                std::string res;
                sock.recvMsg(res);
                if(res == "OK") {
                    std::cout << "已发送好友申请，请等待对方同意\n";
                } else {
                    std::cout << res << std::endl;
                }
            } else if(choice == 3) {
                //删除
                std::string id;
                while(id.empty()) {
                    std::cout << "请输入对方uid：\n";
                    std::cin >> id;
                }
                nlohmann::json j;
                j["type"] = "friend";
                j["action"] = "del";
                j["from_uid"] = std::to_string(usr.uid);
                j["to_uid"] = id;
                sock.sendMsg(j.dump());
                std::string res;
                sock.recvMsg(res);
                if(res == "OK") {
                    std::cout << "已删除 uid: " << id << std::endl;
                } else {
                    std::cout << res << std::endl;
                }
            } else if(choice == 4) {
                //好友申请
                nlohmann::json j;
                j["type"] = "friend";
                j["action"] = "check_request";
                j["from_uid"] = std::to_string(usr.uid);
                // j["to_uid"] = id;
                sock.sendMsg(j.dump());
                std::string res;
                sock.recvMsg(res);

                if(res == "OK") {
                    // for() {

                    // }
                    std::string uid;
                    while(uid != "0") {
                        std::cout << "请输入一个用户id(输入“0”退出):\n";
                        if(uid == "0") break;
                        int choice = 0;
                        while(choice < 1 || choice >3) {
                            std::cout << "1.同意\n" << "2.拒绝\n" << "3.取消\n";
                            std::cin >> choice;
                            if(choice == 1) {
                                nlohmann::json js;
                                js["type"] = "friend";
                                js["action"] = "agree";
                                js["from_uid"] = std::to_string(usr.uid);
                                j["to_uid"] = uid;
                                sock.sendMsg(js.dump());
                                std::string result;
                                sock.recvMsg(result);

                            } else if(choice == 2) {
                                nlohmann::json js;
                                js["type"] = "friend";
                                js["action"] = "reject";
                                js["from_uid"] = std::to_string(usr.uid);
                                j["to_uid"] = uid;
                                sock.sendMsg(js.dump());
                                std::string result;
                                sock.recvMsg(result);

                            } else if(choice == 3) {
                                break;
                            }
                        }
                    }
                } else {
                    std::cout << res << std::endl;
                }
            } else if(choice == 5) {
                // continue;
                break;
            }
        }

        // while(true) {
        //     char *inp=NULL;
        //     inp=readline(prompt.c_str());
        //     if(inp == NULL) {
        //         free(inp);
        //         continue;
        //     }
        //     std::string input(inp);
        //     free(inp);
        //     if(input.empty()) {
        //         continue;
        //     } else {
        //         sock.sendMsg(input);
        //     }
        //     if(input == "/exit") {
        //         // signal(SIGCHLD,SIG_IGN);
        //         rl_clear_history();
        //         break;
        //     }
        // }
    }
}

int start_client() {
    // handle_signal();
    TcpClient client;
    if(!client.connectToHost("127.0.0.1", 2100)) {
        std::cerr << "[FAIL] connectToHost failed\n";
        return 1;
    }
    std::cout << "[PASS] connected to server\n";
    TcpSocket* sock = client.getSocket();
    if (!sock) {
        std::cerr << "[FAIL] socket null\n";
        return 1;
    }
    std::string workpath=std::string(getenv("HOME")) + "/Download";
    mkdir(workpath.c_str(),0755);
    chdir(workpath.c_str());

    bool pasving=false;
    TcpClient dataClient;
    TcpSocket* pasv;

    User usr;
    std::string choice;
    while(choice != "1" && choice != "2") {
        // std::cout << "请注册（/register）或登录（/login） >> " ;
        menu.show(ClientState::LOGIN);
        std::cin >> choice;
    }

    while(choice == "1") {
        if(usr.username.empty()) {
            std::cout << "请输入用户名:\n";
            std::cin >> usr.username ;
            if(usr.username.empty()) continue;
        }
        if(usr.password.empty()) {
            std::cout << "请输入密码:\n";
            std::cin >> usr.password;
            if(usr.username.empty()) continue;
        }
        nlohmann::json j;
        j["type"] = "user";
        j["action"] = "login";
        j["username"] = usr.username;
        j["password"] = usr.password;
        sock->sendMsg(j.dump());
        std::string id;
        sock->recvMsg(id);
        if(id == "error") {
            std::cout << "用户名或密码错误，请重试\n";
            usr.username.clear();
            usr.password.clear();
            continue;
        }
        usr.uid = std::stoi(id);

        prompt.clear();
        prompt = usr.username + " " + id + " >> ";

        break;
    }

    while(choice == "2") {
    // while(true) {
        if(usr.username.empty()) {
            std::cout << "请输入用户名:\n";
            std::cin >> usr.username ;
            if(usr.username.empty()) continue;
        }
        if(usr.password.empty()) {
            std::cout << "请输入密码:\n";
            std::cin >> usr.password;
            if(usr.username.empty()) continue;
        }
        nlohmann::json j;
        j["type"] = "user";
        j["action"] = "register";
        j["username"] = usr.username;
        j["password"] = usr.password;
        sock->sendMsg(j.dump());
        std::string id;
        sock->recvMsg(id);
        if(id == "error") {
            std::cout << "用户名已存在，请重试\n";
            usr.username.clear();
            usr.password.clear();
            continue;
        }
        usr.uid = std::stoi(id);

        prompt.clear();
        prompt = usr.username + " " + id + " >> ";

        break;
    }

    std::thread t1(sendthread,std::ref(*sock),std::ref(usr));
    std::thread t2(recvthread,std::ref(*sock));

    t1.join();
    t2.join();

    while(true) {

        // if(pasving) {
        //     std::string msa;
        //     pasv->recvMsg(msa);
        //     std::cout << msa << std::endl;

        //     if(msa=="start_ls") {
        //         while(true) {
        //             std::string ls_res;
        //             pasv->recvMsg(ls_res);
        //             if(ls_res=="stop") break;
        //             std::cout << ls_res << std::endl;
        //         }
        //         pasving = false;
        //     } else if(msa=="start_stor") {
        //         std::string server_path;
        //         pasv->recvMsg(server_path);

        //         size_t sep = server_path.find('|');
        //         std::string filename = server_path.substr(0, sep);
        //         uint64_t offset = 0;
        //         if(sep != std::string::npos) {
        //             offset = std::stoull(server_path.substr(sep + 1));
        //         }

        //         std::cout << "Uploading from offset: " << offset << std::endl;

        //         pasv->sendFile(filename,offset);

        //         pasving = false;

        //     } else if(msa=="start_retr") {
        //         std::string res;
        //         pasv->recvMsg(res);
        //         if(res != "ok") {
        //             std::cout << res << std::endl;
        //             continue;
        //         }
        //         std::string server_path;
        //         pasv->recvMsg(server_path);

        //         std::filesystem::path p(server_path);
        //         std::string filename = p.filename().string();

        //         uint64_t offset = 0;
        //         struct stat st;
        //         if(stat(filename.c_str(), &st) == 0) {
        //             offset = st.st_size;
        //         }

        //         pasv->sendMsg(std::to_string(offset));
        //         pasv->recvFile(filename, offset);

        //         pasving = false;
        //         continue;
        //     }
        // }

        // if(input=="PASV" && pasving==false) {
        //     pasving=true;
        //     std::string reply;
        //     sock->recvMsg(reply);

        //     int h1,h2,h3,h4,p1,p2;
        //     sscanf(reply.c_str(),
        //         "227 entering passive mode (%d,%d,%d,%d,%d,%d)",
        //         &h1,&h2,&h3,&h4,&p1,&p2
        //     );

        //     int port = p1*256 + p2;
        //     std::string ip =
        //         std::to_string(h1) + "." +
        //         std::to_string(h2) + "." +
        //         std::to_string(h3) + "." +
        //         std::to_string(h4);

        //     std::cout << "ip = " << ip << std::endl;
        //     std::cout << "port = " << port << std::endl;

        //     if(!dataClient.connectToHost(ip.c_str(),port)) {
        //         std::cerr << "data connect failed\n";
        //         return 1;
        //     }
        //     std::cout << "[DATA] connected\n";
        //     pasv = dataClient.getSocket();
        //     continue;
        // }

        // if(input=="exit") {
            // signal(SIGCHLD,SIG_IGN);
            // rl_clear_history();
            break;
        // }
    }
    return 0;
}
