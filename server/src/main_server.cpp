#include "server.h"
#include <glog/logging.h>

int main(int argc,char **argv) {
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = true;
    unsigned short port = 2100;
    if(argc == 2) {
        port = (unsigned short)std::stoi(argv[1]);
    }
    start_server(port);
    google::ShutdownGoogleLogging();
    return 0;
}
