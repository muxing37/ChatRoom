#pragma once
#include "shared.h"
#include <sqlite3.h>
#include <string>
#include <vector>
#include <mutex>

class LocalDb {
public:
    bool init(const std::string& path);
    void close();
    bool save(const Message& msg);
    std::vector<Message> loadAll();

private:
    sqlite3* db_ = nullptr;
    std::mutex mtx_;
};