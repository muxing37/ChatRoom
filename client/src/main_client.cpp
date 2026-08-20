#include "client.h"

int main(int argc,char **argv) {
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = true;
    std::string ip = "127.0.0.1";
    unsigned short port = 2100;
    if(argc == 3) {
        ip = argv[1];
        port = (unsigned short)std::stoi(argv[2]);
    }
    start_client(ip,port);
    return 0;
}
