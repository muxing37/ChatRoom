#include "client.h"

int main(int argc,char **argv) {
    std::string ip = "127.0.0.1";
    unsigned short port = 2100;
    if(argc == 2) {
        port = (unsigned short)std::stoi(argv[1]);
    }
    if(argc == 3) {
        ip = argv[1];
        port = (unsigned short)std::stoi(argv[2]);
    }
    start_client(ip,port);
    return 0;
}
