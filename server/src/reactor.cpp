#include "reactor.h"

namespace netNonBlocking {  
  bool set(int fd) {
    int flags = fcntl(fd,F_GETFL,0);
    if(flags < 0) return false;
    if(fcntl(fd,F_SETFL,flags | O_NONBLOCK) == 0) {
      return true;
    }
    return false;
  }

  ssize_t readN(int fd,void* buf,size_t len) {
    while(true) {
      ssize_t n = recv(fd,buf,len,0);
      if(n > 0) return n;
      if(n == 0) return 0;
      if(errno == EINTR) continue;
      return -1;
    }
  }

  ssize_t writeN(int fd,const void* buf,size_t len) {
    while(true) {
      ssize_t n = send(fd,buf,len,MSG_NOSIGNAL);
      if(n > 0) return n;
      if(n < 0) {
        if(errno == EINTR) continue;
        return -1;
      }
      return 0;
    }
  }

  int accept(int lfd) {
    while(true) {
      int fd = accept(lfd);
      if(fd > 0) return fd;
      if(errno == EINTR) continue;
      return -1;
    }
  }

  std::string encodeFrame(const std::string& body) {
    std::string res;
    res.reserve(body.size() + 4);
    uint32_t n = htonl(static_cast<uint32_t>(body.size()));
    res.append(reinterpret_cast<const char*>(&n),sizeof(n));
    res.append(body);
    return res;
  }

  int tryTakeFrame(const std::string& in,size_t& consumed,std::string& frame) {
    constexpr size_t HeaderSize = 4;
    if(in.size() < HeaderSize) return 0;
    uint32_t n = 0;
    std::memcpy(&n,in.data(),HeaderSize);
    n = ntohl(n);
    if(n > MaxFrameSize) return -1;
    if(in.size() < HeaderSize + n) return 0;
    frame.assign(in.data() + HeaderSize, n);
    consumed = HeaderSize + n;
    return 1;
  }
}

Channel::Channel(EventLoop* loop,int fd) : loop_(loop),fd_(fd),events_(0),in_epoll_(false) {}

Channel::~Channel() {}

void Channel::update() {
  loop_->updateChannel(this);
}

void Channel::remove() {
  loop_->removeChannel(this);
  in_epoll_ = false;
}

void Channel::handleEvent(uint32_t events) {
  if(events & (EPOLLERR | EPOLLHUP)) {
    if(close_back_) {
      close_back_();
      return;
    }
  }
  if(events & (EPOLLIN | EPOLLHUP)) {
    if(read_back_) read_back_();
  }
  if(events & EPOLLOUT) {
    if(write_back_) write_back_();
  }
}

EventLoop::EventLoop() : 
  epoll_fd_(epoll_create1(EPOLL_CLOEXEC)),
  wake_fd_(eventfd(0,EFD_NONBLOCK | EFD_CLOEXEC)),
  thread_id_(std::this_thread::get_id()),
  quit_(false),
  wake_channel_(nullptr)
{
  read_events_.resize(1024);
  wake_channel_ = new Channel(this,wake_fd_);
  wake_channel_->setReadBack([this] { handleWakeup(); });
  wake_channel_->enableRead();
}

EventLoop::~EventLoop() {
  if(wake_channel_) {
    wake_channel_->remove();
    delete wake_channel_;
  }
  close(wake_fd_);
  close(epoll_fd_);
}

void EventLoop::loop() {
  quit_ = false;
  while(!quit_) {
    int n = epoll_wait(epoll_fd_,read_events_.data(),(int)read_events_.size(),1000);
    if(n < 0) {
      if(errno == EINTR) continue;
      break;
    }
    for(int i = 0;i < n;i++) {
      auto* ch = static_cast<Channel*>(read_events_[i].data.ptr);
      ch->handleEvent(read_events_[i].events);
    }
    runTask();
  }
}

void EventLoop::quit() {
  quit_ = true;
  wakeup();
}

void EventLoop::runInLoop(std::function<void()> func) {
  if(isInLoopThread()) {
    func();
  } else {
    queueInLoop(std::move(func));
  }
}

void EventLoop::queueInLoop(std::function<void()> func) {
  {
    std::lock_guard<std::mutex> lock(task_mtx_);
    tasks_.push_back(std::move(func));
  }
  wakeup();
}

void EventLoop::updateChannel(Channel* ch) {
  epoll_event ev;
  ev.events = ch->events();
  ev.data.ptr = ch;
  if(ch->isInEpoll()) {
    epoll_ctl(epoll_fd_,EPOLL_CTL_MOD,ch->fd(),&ev);
  } else {
    epoll_ctl(epoll_fd_,EPOLL_CTL_ADD,ch->fd(),&ev);
    ch->setInEpoll(true);
  }
}

void EventLoop::removeChannel(Channel* ch) {
  if(ch->isInEpoll()) {
    epoll_ctl(epoll_fd_,EPOLL_CTL_DEL,ch->fd(),nullptr);
    ch->setInEpoll(false);
  }
}

void EventLoop::wakeup() {
  uint64_t one = 1;
  write(wake_fd_,&one,sizeof(one));
}

void EventLoop::handleWakeup() {
  uint64_t one = 0;
  read(wake_fd_,&one,sizeof(one));
}

void EventLoop::runTask() {
  std::vector<std::function<void()>> tasks;
  {
    std::lock_guard<std::mutex> lock(task_mtx_);
    tasks.swap(tasks_);
  }
  for(auto& f : tasks) {
    f();
  }
}

Acceptor::Acceptor(EventLoop* loop,unsigned short port) 
  : loop_(loop),listen_fd_(-1),accept_channel_(nullptr)
{
  listen_fd_ = socket(AF_INET,SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,0);
  int opt = 1;
  setsockopt(listen_fd_,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);
  bind(listen_fd_,(sockaddr*)&addr,sizeof(addr));
  listen(listen_fd_,SOMAXCONN);

  accept_channel_ = new Channel(loop_,listen_fd_);
  accept_channel_->setReadBack([this] { handleRead(); });
  accept_channel_->enableRead();
}

Acceptor::~Acceptor() {
  if(accept_channel_) {
    accept_channel_->remove();
    delete accept_channel_;
  }
  close(listen_fd_);
}

void Acceptor::handleRead() {
  while(true) {
    int fd = accept4(listen_fd_,nullptr,nullptr,SOCK_NONBLOCK | SOCK_CLOEXEC);
    if(fd < 0) {
      if(errno == EINTR) continue;
      if(errno == EAGAIN || errno == EWOULDBLOCK) break;
      break;
    }
    if(new_con_cb_) new_con_cb_(fd);
  }
}

EventLoopPool::EventLoopPool(EventLoop* main_loop,int num)
  : main_loop_(main_loop),next_(0)
{
  if(num <= 0) {
    num = std::thread::hardware_concurrency();
    if(num <= 0) num = 4;
  }
  for(int i = 0;i < num;i++) {
    sub_loops_.push_back(new EventLoop);
  }
}

EventLoopPool::~EventLoopPool() {
  for(auto* loop : sub_loops_) loop->quit();
  for(auto& t : threads_) t.join();
  for(auto* loop : sub_loops_) delete loop;
}

void EventLoopPool::start() {
  for(auto* loop : sub_loops_) {
    threads_.emplace_back([loop] {
      loop->resetThreadId();
      loop->loop();
    });
  }
}

EventLoop* EventLoopPool::nextLoop() {
  if(sub_loops_.empty()) return main_loop_;
  EventLoop* loop = sub_loops_[next_];
  next_ = (next_ + 1) % (int)sub_loops_.size();
  return loop;
}
