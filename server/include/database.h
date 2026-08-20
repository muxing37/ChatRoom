#pragma once
#include <mysql/mysql.h>
#include <string>
#include <mutex>

struct DbConfig {
  std::string host = "127.0.0.1";
  int port = 3306;
  std::string user = "root";
  std::string password;
  std::string database = "chatroom";
};

bool loadDbConfig(DbConfig& cfg);

class Database {
public:
  bool connect(const std::string& host,const std::string& user,const std::string& password,const std::string& database,unsigned int port);
  bool isConnected() { return mysql_ != nullptr; }
  void close();
  bool execute(const std::string& sql);
  MYSQL_RES* query(const std::string& sql);
  std::string escape(const std::string& s);
  const char* error();

  bool begin() { return execute("START TRANSACTION"); }
  bool commit() { return execute("COMMIT"); }
  bool rollback() { return execute("ROLLBACK"); }

private:
  MYSQL* threadConn();

private:
  MYSQL* mysql_ = nullptr;
  std::string host_, user_, password_, database_;
  unsigned int port_ = 3306;
  std::mutex mtx_;
};