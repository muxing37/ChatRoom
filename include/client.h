#pragma once
#include "socket.h"
#include <memory>
#include <fstream>
#include <string>
#include <signal.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <string>
#include <thread>

int start_client();