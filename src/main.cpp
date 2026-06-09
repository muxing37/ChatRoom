#include "ctrl.h"

int main(int argc,char **argv) {
    if(argc != 2) {
        std::cout << "请使用“./ftp server”或“./ftp client”" << std::endl;
        return 0;
    }
    std::string s=argv[1];
    if(s != "server" && s != "client") {
        std::cout << "请使用“./ftp server”或“./ftp client”" << std::endl;
        return 0;
    }
    if(s == "client") start_client();
    if(s == "server") start_server();
    return 0;
}
