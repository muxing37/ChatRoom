#pragma once
#include <glog/logging.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <sys/types.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <cerrno>
#include <cstring>
#include <functional>
#include <thread>
#include <mutex>
#include <vector>
#include <iostream>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <deque>
#include <sys/timerfd.h>
#include <unordered_set>

namespace netNonBlocking {
  constexpr uint32_t MaxFrameSize = 100 * 1024 * 1024;
  
  bool set(int fd);
  ssize_t readN(int fd,void* buf,size_t len);
  ssize_t writeN(int fd,const void* buf,size_t len);
  int acceptN(int lfd);

  std::string encodeFrame(const std::string& body);
  int tryTakeFrame(const std::string& in,size_t& consumed,std::string& frame);
}

class Channel;
class Connection;

class EventLoop {
public:
  EventLoop();
  ~EventLoop();

  void runEvery(int ms,std::function<void()> cb);
  void addConn(Connection* c) { conns_.insert(c); }
  void removeConn(Connection* c) { conns_.erase(c); }
  const std::unordered_set<Connection*>& conns() { return conns_; }

  void resetThreadId() { thread_id_ = std::this_thread::get_id(); }

  void loop();
  void quit();
  void runInLoop(std::function<void()> func);
  void queueInLoop(std::function<void()> func);

  void updateChannel(Channel* ch);
  void removeChannel(Channel* ch);

  bool isInLoopThread() {
    return std::this_thread::get_id() == thread_id_;
  }

private:
  void handleTimer();
  void wakeup();
  void handleWakeup();
  void runTask();

private:
  int timer_fd_;
  Channel* timer_channel_;
  std::function<void()> timer_cb_;
  std::unordered_set<Connection*> conns_;
  int epoll_fd_;
  int wake_fd_;
  std::thread::id thread_id_;
  bool quit_;
  std::vector<epoll_event> read_events_;
  std::mutex task_mtx_;
  std::vector<std::function<void()>> tasks_;
  Channel* wake_channel_;
};

class Channel {
public:
  Channel(EventLoop* loop,int fd);
  ~Channel();

  int fd() { return fd_; }
  EventLoop* loop() { return loop_; }
  uint32_t events() { return events_; }
  bool isInEpoll() { return in_epoll_; }
  void setInEpoll(bool b) { in_epoll_=b; }

  void enableRead() { events_ |= EPOLLIN; update(); }
  void disableRead() { events_ &= ~EPOLLIN; update(); }
  void enableWrite() { events_ |= EPOLLOUT; update(); }
  void disableWrite() { events_ &= ~EPOLLOUT; update(); }

  void setReadBack(std::function<void()> back) { read_back_ = std::move(back); }
  void setWriteBack(std::function<void()> back) { write_back_ = std::move(back); }
  void setCloseBack(std::function<void()> back) { close_back_ = std::move(back); }

  void handleEvent(uint32_t events);
  void update();
  void remove();

private:
  EventLoop* loop_;
  int fd_;
  uint32_t events_ = 0;
  bool in_epoll_;
  std::function<void()> read_back_;
  std::function<void()> write_back_;
  std::function<void()> close_back_;
};

class Acceptor {
public:
  Acceptor(EventLoop* loop,unsigned short port);
  ~Acceptor();

  void setNewConCallback(std::function<void(int fd)> cb) {
    new_con_cb_ = std::move(cb);
  }

private:
  void handleRead();

private:
  EventLoop* loop_;
  int listen_fd_;
  Channel* accept_channel_;
  std::function<void(int fd)> new_con_cb_;
};

class EventLoopPool {
public:
  EventLoopPool(EventLoop* main_loop,int num = 0);
  ~EventLoopPool();

  void start();
  EventLoop* nextLoop();
  void runEveryOnSubLoops(int ms,std::function<void(EventLoop*)> cb);

private:
  EventLoop* main_loop_;
  int next_;
  std::vector<EventLoop*> sub_loops_;
  std::vector<std::thread> threads_;
};

class Connection {
public:
  Connection(EventLoop* loop,int fd);
  ~Connection();
  EventLoop* loop() { return loop_; }
  void send(const std::string& msg);

  void setMessageCallback(std::function<void(const std::string&)> cb);
  void setCloseCallback(std::function<void()> cb);
  void handleClose();
  
  std::string localIp() const;

  uint64_t lastActive() { return last_active_ms_; }
  uint64_t lastPingMs() { return last_ping_ms_; }
  void updateLastPing(uint64_t t) { last_ping_ms_ = t; }

private:
  void handleWrite();
  void handleRead();

private:
  uint64_t last_active_ms_ = 0; // 上次收到数据的时间
  uint64_t last_ping_ms_ = 0; // 上次发 ping 的时间
  int fd_;
  EventLoop* loop_;
  Channel* channel_;
  std::string in_buf_;
  bool closed_ = false;
  std::deque<std::string> send_queue_;
  std::function<void()> close_cb_;
  std::function<void(const std::string&)> message_cb_;
};

class FileConn {
public:
  FileConn(EventLoop* loop,int fd);
  ~FileConn();

  bool send(const std::string& data);
  void setReadCallback(std::function<void(const char*,size_t)> cb);
  void setCloseCallback(std::function<void()> cb);
  void handleClose();
  void setWriteEmptyCallback(std::function<void()> cb) { write_empty_cb_ = std::move(cb); }
  int fd() { return fd_; };

private:
  void handleWrite();
  void handleRead();

private:
  int fd_;
  EventLoop* loop_;
  Channel* channel_;
  bool closed_ = false;
  std::deque<std::string> send_queue_;
  std::function<void()> close_cb_;
  std::function<void()> write_empty_cb_;
  std::function<void(const char*,size_t)> read_cb_;
  std::vector<char> read_buf_;
};
