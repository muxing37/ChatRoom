#include "database.h"
#include <iostream>
#include <glog/logging.h>
#include <fstream>
#include <nlohmann/json.hpp>

bool loadDbConfig(DbConfig& cfg) {
  std::string path = "../server/config/db.config";
  
  std::ifstream f(path);
  if(!f.is_open()) return false;
  nlohmann::json j;
  f >> j;
  cfg.host = j.value("host",cfg.host);
  cfg.port = j.value("port",cfg.port);
  cfg.user = j.value("user",cfg.user);
  cfg.password = j.value("password","");
  cfg.database = j.value("database",cfg.database);
  return true;
}

bool Database::connect(const std::string& host,const std::string& user,const std::string& password,const std::string& database,unsigned int port = 3306) {
  host_ = host;
  user_ = user;
  password_ = password;
  database_ = database;
  port_ = port;
  mysql_ = mysql_init(nullptr);
  if(!mysql_) return false;
  if(!mysql_real_connect(mysql_,host.c_str(),user.c_str(),password.c_str(),database.c_str(),port,nullptr,0)) {
    mysql_close(mysql_);
    mysql_ = nullptr;
    return false;
  }
  mysql_set_character_set(mysql_,"utf8mb4");
  return true;
}

MYSQL* Database::threadConn() {
  thread_local MYSQL* conn = nullptr;
  if(conn && (mysql_ping(conn) != 0)) {
    mysql_close(conn);
    conn = nullptr;
  }
  if(!conn) {
    conn = mysql_init(nullptr);
    if(!mysql_real_connect(conn,host_.c_str(),user_.c_str(),password_.c_str(),database_.c_str(),port_,nullptr,0)) {
      LOG(ERROR) << "[MySQL] thread connect failed: " << mysql_error(conn);
      mysql_close(conn);
      conn = nullptr;
    } else {
      mysql_set_character_set(conn,"utf8mb4");
    }
  }
  return conn;
}

void Database::close() {
  if(mysql_) {
    mysql_close(mysql_);
    mysql_ = nullptr;
  }
}

bool Database::execute(const std::string& sql) {
  MYSQL* c = threadConn();
  if(!c) return false;
  if(mysql_query(c,sql.c_str()) != 0) {
    LOG(ERROR) << "MySQL query failed: " << mysql_error(c);
    return false;
  }
  return true;
}

MYSQL_RES* Database::query(const std::string& sql) {
  MYSQL* c = threadConn();
  if(!c) return nullptr;
  if(mysql_query(c,sql.c_str()) != 0) {
    LOG(ERROR) << "MySQL query failed: " << mysql_error(c);
    return nullptr;
  }
  return mysql_store_result(c);
}

std::string Database::escape(const std::string& s) {
  MYSQL* c = threadConn();
  if(!c) return {};
  std::vector<char> buf(s.size() * 2 + 1);
  mysql_real_escape_string(c,buf.data(),s.data(),s.size());
  return std::string(buf.data());
}

const char* Database::error() {
  if(mysql_) {
    return mysql_error(mysql_);
  } else {
    return "not connexted";
  }
}
