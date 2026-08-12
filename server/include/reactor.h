#pragma once
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

namespace netNonBlocking {
  constexpr uint32_t MaxFrameSize = 100 * 1024 * 1024;
  
  bool set(int fd);
  ssize_t readN(int fd,void* buf,size_t len);
  ssize_t writeN(int fd,const void* buf,size_t len);
  int accept(int lfd);

  std::string encodeFrame(const std::string& body);
  int tryTakeFrame(const std::string& in,size_t& consumed,std::string& frame);
}

class Channel;

class EventLoop {
public:
  EventLoop();
  ~EventLoop();

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
  void wakeup();
  void handleWakeup();
  void runTask();

private:
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

private:
  EventLoop* main_loop_;
  int next_;
  std::vector<EventLoop*> sub_loops_;
  std::vector<std::thread> threads_;
};
