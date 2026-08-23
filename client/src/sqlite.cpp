#include "sqlite.h"

bool LocalDb::init(const std::string& path) {
  std::lock_guard<std::mutex> lock(mtx_);
  if(db_) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
  if(sqlite3_open(path.c_str(),&db_) != SQLITE_OK) {

    if(db_) {
      sqlite3_close(db_);
      db_ = nullptr;
    }
    return false;
  }
  sqlite3_exec(db_,"PRAGMA journal_mode=WAL;",nullptr,nullptr,nullptr);
  sqlite3_exec(db_,"PRAGMA synchronous=NORMAL;",nullptr,nullptr,nullptr);
  const char* sql =
    "CREATE TABLE IF NOT EXISTS messages ("
    " message_id TEXT PRIMARY KEY,"
    " type TEXT, chat_type TEXT,"
    " from_uid INTEGER, target_id INTEGER,"
    " content TEXT,"
    " msg_time INTEGER, status INTEGER);";
  char* err = nullptr;
  if(sqlite3_exec(db_,sql,nullptr,nullptr,&err) != SQLITE_OK) {

    sqlite3_free(err);
    sqlite3_close(db_);
    db_ = nullptr;
    return false;
  }
  return true;
}

void LocalDb::close() {
  std::lock_guard<std::mutex> lock(mtx_);
  if(db_) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
}

bool LocalDb::save(const Message& msg) {
  std::lock_guard<std::mutex> lock(mtx_);
  if(!db_) return false;
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
    "INSERT OR REPLACE INTO messages "
    "(message_id,type,chat_type,from_uid,target_id,content,msg_time,status) "
    "VALUES (?,?,?,?,?,?,?,?)";
  if(sqlite3_prepare_v2(db_,sql,-1,&stmt,nullptr) != SQLITE_OK) {

    return false;
  }
  sqlite3_bind_text(stmt,1,msg.message_id.c_str(),-1,SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt,2,msg.type.c_str(),-1,SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt,3,msg.chat_type.c_str(),-1,SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt,4,msg.from_uid);
  sqlite3_bind_int64(stmt,5,msg.target_id);
  sqlite3_bind_text(stmt,6,msg.content.c_str(),-1,SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt,7,(sqlite3_int64)msg.time);
  sqlite3_bind_int64(stmt,8,msg.status);
  bool ok = sqlite3_step(stmt) == SQLITE_DONE;
  sqlite3_finalize(stmt);

  return ok;
}

std::vector<Message> LocalDb::loadAll() {
  std::lock_guard<std::mutex> lock(mtx_);
  std::vector<Message> result;
  if(!db_) return result;
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
    "SELECT message_id,type,chat_type,from_uid,target_id,content,msg_time,status"
    " FROM messages";
  if(sqlite3_prepare_v2(db_,sql,-1,&stmt,nullptr) != SQLITE_OK) return result;
  while(sqlite3_step(stmt) == SQLITE_ROW) {
    Message m;
    const unsigned char* t;
    t = sqlite3_column_text(stmt,0); m.message_id = t ? (const char*)t : "";
    t = sqlite3_column_text(stmt,1); m.type = t ? (const char*)t : "";
    t = sqlite3_column_text(stmt,2); m.chat_type = t ? (const char*)t : "";
    m.from_uid = (int)sqlite3_column_int64(stmt,3);
    m.target_id = (int)sqlite3_column_int64(stmt,4);
    t = sqlite3_column_text(stmt,5); m.content = t ? (const char*)t : "";
    m.time = (uint64_t)sqlite3_column_int64(stmt,6);
    m.status = (int)sqlite3_column_int64(stmt,7);
    result.push_back(m);
  }
  sqlite3_finalize(stmt);
  return result;
}
