# ChatRoom — C++ 聊天室

基于 **C++17** 实现的聊天室系统。服务端采用**主从 Reactor** 网络模型（基于 `epoll` 自研网络库），支持私聊、群聊、好友管理、文件传输（断点续传）等功能；客户端提供 **CLI** 与 **Web** 双界面，并使用 SQLite 缓存本地聊天记录。

---

## 目录

- [一、技术栈](#一技术栈)
- [二、功能特性](#二功能特性)
- [三、使用的第三方库](#三使用的第三方库)
- [四、编译方式](#四编译方式)
- [五、使用方法](#五使用方法)
- [六、项目目录结构](#六项目目录结构)
- [七、MySQL 表结构](#七mysql-表结构)
- [八、重要数据结构](#八重要数据结构)

---

## 一、技术栈

| 类别 | 技术 |
|------|------|
| 语言 / 构建 | C++17 · CMake (≥3.16) |
| 平台 | Linux（epoll / eventfd / timerfd / pthread） |
| 网络模型 | 主从 Reactor（基于 epoll 的自研网络库） |
| 服务端存储 | MySQL（用户 / 好友 / 群组 / 消息 / 文件元数据） |
| 客户端存储 | SQLite3（本地消息缓存） |
| 密码安全 | OpenSSL SHA-256 + 随机盐 |
| 日志 | Google glog（服务端） |
| 数据序列化 | nlohmann/json（协议、配置） |
| Web UI | cpp-httplib（内嵌 HTTP 服务 + 前端页面） |
| 图片处理 | stb_image（图片消息解析宽高） |

### 服务端架构

- **主线程**：只负责接收新连接，通过轮询分发给从 Reactor 线程池；
- **从线程池**：每个线程各跑一个事件循环，负责连接的读写与业务处理；
- **单连接单线程**：每个连接固定归属一个线程，连接内部无需加锁；跨线程推送通过 `runInLoop` 投递到目标连接所属线程执行；
- **心跳保活**：5 秒检查一次，超过 30 秒未 ping 则发心跳，超过 65 秒无数据则强制断开；
- **文件传输**：控制面（业务 JSON 帧）与数据面（独立裸字节流通道）分离，1MB 分块传输、支持断点续传。

---

## 二、功能特性

- **账户系统**：注册、登录、注销（删除账户会级联清理好友、群关系及消息）。
- **好友系统**：
  - 好友申请 / 同意 / 拒绝 / 删除
  - 屏蔽 / 拉黑（被屏蔽方无法发送消息）
  - 在线状态广播（好友上下线实时推送）
- **私聊**：
  - 实时消息推送
  - 离线消息同步
- **群聊**：
  - 创建 / 解散群（解散者须为群主）
  - 入群申请 / 审批（群主或管理员）、主动退出、踢人
  - 管理员设置、消息免打扰（正常通知 / 接收不通知 / 不接收）
- **文件传输**：
  - 上传 / 下载，支持**断点续传**（上传凭 SHA-256 哈希找回未完成任务，下载凭 offset 续传）
  - 1MB 分块传输，内存占用可控，服务端以 `.uploading` 临时文件 + 原子 `rename` 保证只有完整文件可见
  - 图片消息自动解析宽高并附带在消息内容中
  - 客户端传输失败自动重试（最多 3 次）
- **双客户端 UI**：
  - CLI 终端界面（`CHAT_CLI=1` 环境变量开启）
  - Web 网页界面（浏览器访问，页面在编译时内嵌进客户端可执行文件）
- **本地缓存**：客户端用 SQLite 保存历史消息，支持离线浏览

---

## 三、使用的第三方库

### 系统库（Ubuntu / Debian 下通过 apt 安装）

| 库 | 用途 | 安装包 |
|----|------|--------|
| [nlohmann/json](https://github.com/nlohmann/json) | JSON 序列化（协议帧、配置） | `nlohmann-json3-dev` |
| [OpenSSL](https://www.openssl.org/) | 密码 SHA-256 哈希、盐 | `libssl-dev` |
| [glog](https://github.com/google/glog) | 服务端日志 | `libgoogle-glog-dev` |
| MySQL C API | 服务端数据持久化 | `default-libmysqlclient-dev` |
| SQLite3 | 客户端本地消息缓存 | `libsqlite3-dev` |

### 内嵌（源码随项目分发，位于 `include/third/`）

| 库 | 位置 | 用途 |
|----|------|------|
| [cpp-httplib](https://github.com/yhirose/cpp-httplib) | `client/include/third/httplib.h` | Web UI 内嵌 HTTP 服务 |
| [stb_image](https://github.com/nothings/stb) | `server/include/third/stb_image.h` | 图片宽高解析 |

---

## 四、编译方式

### 1. 安装依赖

```bash
# Ubuntu / Debian
sudo apt update
sudo apt install -y cmake g++ \
    nlohmann-json3-dev libssl-dev libgoogle-glog-dev \
    default-libmysqlclient-dev libsqlite3-dev
```

> 说明：`cpp-httplib` 与 `stb_image` 已内嵌在源码中，无需额外安装。

### 2. 初始化数据库

```bash
# 启动 MySQL 后，执行建表脚本（会自动创建 chatroom 库及所有表）
mysql -u root -p < server/sql/schema.sql

# 配置数据库连接（复制示例并填写密码）
cp server/config/db.config.example server/config/db.config
vim server/config/db.config
```

### 3. 编译

```bash
cd ChatRoom

# 配置并构建（默认生成 build/server 与 build/client）
make build
cd .build
cmake ..
make -j$(nproc)
```

编译产物：

- `build/server` —— 聊天室服务端；
- `build/client` —— 聊天室客户端（CLI / Web）。

> 注：构建时会自动把 `client/web/chat.html` 内嵌为头文件 `build/generated/chat_html.h`，因此修改前端页面后需要重新编译客户端。

---

## 五、使用方法

### 1. 启动服务端

```bash
cd build
./server [port]        # port 可选，默认 2100
```

- 服务端启动时会从 `server/config/db.config` 读取 MySQL 配置并连接数据库；
- 日志通过 glog 输出（默认同时打印到终端，并写入 `logs/` 目录）；
- 文件默认存储在 `data/files/` 目录下（自动创建）。

### 2. 启动客户端

```bash
cd build

# Web 界面（默认）：启动后浏览器访问 http://localhost:8080（端口被占用会自动 +1，最多尝试 100 个）
./client [ip] [port]   # ip 默认 127.0.0.1，port 默认 2100

# CLI 界面：设置环境变量 CHAT_CLI=1
CHAT_CLI=1 ./client [ip] [port]
```

- 收到的文件默认保存到 `~/Download/` 目录；

---

## 六、项目目录结构

```
ChatRoom/
├── CMakeLists.txt              # CMake 构建脚本
├── README.md
│
├── common/                     # 客户端 / 服务端共享模块
│   ├── include/
│   │   ├── shared.h            # 消息 / 用户 / 群组 / 文件等公共数据结构 + JSON 序列化
│   │   ├── socket.h            # TcpSocket 封装（业务帧 + 原始字节收发）
│   │   └── hash.h              # SHA-256 密码哈希
│   └── src/
│       ├── socket.cpp
│       └── hash.cpp
│
├── server/                     # 服务端
│   ├── include/
│   │   ├── reactor.h           # 网络库：EventLoop / Channel / Connection / FileConn / Acceptor / EventLoopPool
│   │   ├── manager.h           # 用户 / 好友 / 消息 / 群组 / 文件 各业务管理器
│   │   ├── database.h          # MySQL 封装（连接、事务、转义）
│   │   ├── imgmeta.h           # 图片尺寸解析
│   │   ├── server.h
│   │   └── third/stb_image.h   # 内嵌第三方
│   ├── src/
│   │   ├── main_server.cpp     # 服务端入口（glog 初始化、端口参数）
│   │   ├── server.cpp          # Session 会话处理、心跳、文件上传下载流程
│   │   ├── reactor.cpp         # 网络库实现
│   │   ├── manager.cpp         # 各业务管理器实现
│   │   ├── database.cpp        # MySQL 实现
│   │   └── imgmeta.cpp
│   ├── config/
│   │   └── db.config.example   # 数据库连接配置示例（复制为 db.config 使用）
│   └── sql/
│       └── schema.sql          # MySQL 建表脚本
│
├── client/                     # 客户端
│   ├── include/
│   │   ├── client.h
│   │   ├── service.h           # Auth / Friend / Chat / Group / File 各服务（构造协议请求）
│   │   ├── context.h           # 客户端本地状态（好友、群、消息缓存）
│   │   ├── cliui.h             # CLI 界面
│   │   ├── webui.h             # Web 界面
│   │   ├── sqlite.h            # 本地 SQLite 封装
│   │   └── third/httplib.h     # 内嵌第三方
│   ├── src/
│   │   ├── main_client.cpp     # 客户端入口（IP / 端口参数）
│   │   ├── client.cpp          # 连接管理、推送分发、UI 选择（CHAT_CLI）
│   │   ├── service.cpp
│   │   ├── context.cpp
│   │   ├── cliui.cpp
│   │   ├── webui.cpp
│   │   └── sqlite.cpp
│   └── web/
│       └── chat.html           # Web 前端页面（编译时内嵌）
```

---

## 七、MySQL 表结构

数据库名：`chatroom`（字符集 `utf8mb4`）。完整建表脚本见 `server/sql/schema.sql`。

### users — 用户表

### friends — 好友关系表

### friend_requests — 好友申请表

### blocks — 屏蔽（拉黑）表

### chat_groups — 群组表

### group_members — 群成员表

### group_join_requests — 入群申请表

### messages — 消息表

### file_meta — 文件元数据表

---

## 八、通信协议

统一为 **4 字节大端长度前缀 + JSON 载荷** 的帧格式，帧长上限 100MB。

### 三种消息

| 消息 | 方向 | 说明 |
|------|------|------|
| `request` | 客户端 → 服务端 | 业务请求，携带请求 ID、动作类型与数据 |
| `reply` | 服务端 → 客户端 | 对请求的应答，携带请求 ID 与状态码 |
| `push` | 服务端 → 客户端 | 主动推送（新消息、好友/群事件、心跳等） |

### 文件传输（控制面 / 数据面分离）

1. 客户端发送 `upload_req` / `download_req` 到**业务连接**（JSON 帧）；
2. 服务端校验权限后创建临时数据端口，返回 `ip / port / file_id / offset`；
3. 客户端连接**数据通道**（裸字节流），1MB 分块收发；
4. 传输完成生成 `type=file` 聊天消息推送给接收方。
