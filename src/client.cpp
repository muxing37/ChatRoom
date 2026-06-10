#include "client.h"

#define MAX_PATH 1024
// int running=0;

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

int start_client() {
    handle_signal();

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

    while(true) {
        std::string now_path;
        Msgpack n_path;
        sock->recvMsgpack(n_path);
        if(n_path.type != MsgType::PATH_INFO) {
            sock->sendMsg("unexpected");
            continue;
        }else {
            sock->sendMsg("yes");
        }
        prompt.clear();
        prompt="ftp client >> server:\033[34m" + n_path.msg + "\033[0m ";
        break;
    }

    while(true) {
        char *inp=NULL;
        inp=readline(prompt.c_str());
        if(inp==NULL) {
            free(inp);
            continue;
        }
        std::string input(inp);
        free(inp);

        if(input.size()==0 || input.empty()) {
            continue;
        }

        add_history(input.c_str());
        if(input.empty()) {
            continue;
        } else {
            sock->sendMsg(input);
        }

        std::string res;
        sock->recvMsg(res);
        if(res != "yes") {
            std::cout << res << std::endl;
            continue;
        }
        if(pasving == false && (input.size() >= 4 && (input.substr(0,4) == "RETR" || input.substr(0,4) == "STOR" || input.substr(0,4) == "LIST"))) {
            std::cout << "请使用 PASV 建立数据连接" << std::endl;
            continue;
        }

        if(pasving) {
            std::string msa;
            pasv->recvMsg(msa);
            std::cout << msa << std::endl;

            if(msa=="start_ls") {
                while(true) {
                    std::string ls_res;
                    pasv->recvMsg(ls_res);
                    if(ls_res=="stop") break;
                    std::cout << ls_res << std::endl;
                }
                pasving = false;
            } else if(msa=="start_stor") {
                std::string server_path;
                pasv->recvMsg(server_path);

                size_t sep = server_path.find('|');
                std::string filename = server_path.substr(0, sep);
                uint64_t offset = 0;
                if(sep != std::string::npos) {
                    offset = std::stoull(server_path.substr(sep + 1));
                }

                std::cout << "Uploading from offset: " << offset << std::endl;

                pasv->sendFile(filename,offset);

                pasving = false;

            } else if(msa=="start_retr") {
                std::string res;
                pasv->recvMsg(res);
                if(res != "ok") {
                    std::cout << res << std::endl;
                    continue;
                }
                std::string server_path;
                pasv->recvMsg(server_path);

                std::filesystem::path p(server_path);
                std::string filename = p.filename().string();

                uint64_t offset = 0;
                struct stat st;
                if(stat(filename.c_str(), &st) == 0) {
                    offset = st.st_size;
                }

                pasv->sendMsg(std::to_string(offset));
                pasv->recvFile(filename, offset);

                pasving = false;
                continue;
            }
        }

        if((input.size()>=2 && input.substr(0, 2) == "cd") || (input.size()>=3 && input.substr(0,3) == "CWD")) {
            while(true) {
                std::string now_path;
                Msgpack n_path;
                std::string res;
                sock->recvMsg(res);
                if(res != "ok") {
                    std::cout << res <<std::endl;
                    break;
                }
                sock->recvMsgpack(n_path);
                if(n_path.type != MsgType::PATH_INFO) {
                    sock->sendMsg("unexpected");
                    continue;
                }else {
                    sock->sendMsg("yes");
                }
                prompt.clear();
                prompt="ftp client >> server:\033[34m" + n_path.msg + "\033[0m ";
                break;
            }
        }

        if(input=="PASV" && pasving==false) {
            pasving=true;
            std::string reply;
            sock->recvMsg(reply);

            int h1,h2,h3,h4,p1,p2;
            sscanf(reply.c_str(),
                "227 entering passive mode (%d,%d,%d,%d,%d,%d)",
                &h1,&h2,&h3,&h4,&p1,&p2
            );

            int port = p1*256 + p2;
            std::string ip =
                std::to_string(h1) + "." +
                std::to_string(h2) + "." +
                std::to_string(h3) + "." +
                std::to_string(h4);

            std::cout << "ip = " << ip << std::endl;
            std::cout << "port = " << port << std::endl;

            if(!dataClient.connectToHost(ip.c_str(),port)) {
                std::cerr << "data connect failed\n";
                return 1;
            }
            std::cout << "[DATA] connected\n";
            pasv = dataClient.getSocket();
            continue;
        }

        if(input=="exit") {
            signal(SIGCHLD,SIG_IGN);
            rl_clear_history();
            break;
        }
    }
    return 0;
}
