#pragma once
#include "socket.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/stat.h>
#include <termios.h>
#include <pthread.h>
#include <cstring>
#include <cstdlib>
#include <thread>
#include <atomic>
#include <mutex>

int start_server(unsigned short port);
