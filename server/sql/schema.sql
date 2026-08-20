CREATE DATABASE IF NOT EXISTS chatroom DEFAULT CHARACTER SET utf8mb4;
USE chatroom;

-- 用户
CREATE TABLE IF NOT EXISTS users (
    uid INT PRIMARY KEY,
    username VARCHAR(20) NOT NULL UNIQUE,
    password_hash VARCHAR(128) NOT NULL,
    salt VARCHAR(64) NOT NULL,
    last_logout_time BIGINT NOT NULL DEFAULT 0
);

-- 好友
CREATE TABLE IF NOT EXISTS friends (
    uid1 INT NOT NULL,
    uid2 INT NOT NULL,
    PRIMARY KEY (uid1,uid2),
    FOREIGN KEY (uid1) REFERENCES users(uid) ON DELETE CASCADE,
    FOREIGN KEY (uid2) REFERENCES users(uid) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS friend_requests (
    from_uid INT NOT NULL,
    to_uid INT NOT NULL,
    apply_time BIGINT NOT NULL,
    PRIMARY KEY (from_uid,to_uid),
    FOREIGN KEY (from_uid) REFERENCES users(uid) ON DELETE CASCADE,
    FOREIGN KEY (to_uid) REFERENCES users(uid) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS blocks (
    uid INT NOT NULL,
    block_uid INT NOT NULL,
    PRIMARY KEY (uid,block_uid),
    FOREIGN KEY (uid) REFERENCES users(uid) ON DELETE CASCADE,
    FOREIGN KEY (block_uid) REFERENCES users(uid) ON DELETE CASCADE
);

-- 群组
CREATE TABLE IF NOT EXISTS chat_groups (
    group_id INT PRIMARY KEY,
    name VARCHAR(128) NOT NULL,
    owner_uid INT NOT NULL,
    create_time BIGINT NOT NULL
);

CREATE TABLE IF NOT EXISTS group_members (
    group_id INT NOT NULL,
    uid INT NOT NULL,
    permission INT NOT NULL DEFAULT 2,
    join_time BIGINT NOT NULL,
    remind INT NOT NULL DEFAULT 0,
    PRIMARY KEY (group_id,uid),
    FOREIGN KEY (group_id) REFERENCES chat_groups(group_id) ON DELETE CASCADE,
    FOREIGN KEY (uid) REFERENCES users(uid) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS group_join_requests (
    group_id INT NOT NULL,
    uid INT NOT NULL,
    apply_time BIGINT NOT NULL,
    PRIMARY KEY (group_id,uid),
    FOREIGN KEY (group_id) REFERENCES chat_groups(group_id) ON DELETE CASCADE,
    FOREIGN KEY (uid) REFERENCES users(uid) ON DELETE CASCADE
);

-- 消息
CREATE TABLE IF NOT EXISTS messages (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    message_id VARCHAR(64) NOT NULL,
    type VARCHAR(16) NOT NULL,
    chat_type VARCHAR(16) NOT NULL,
    from_uid INT NOT NULL,
    target_id INT NOT NULL,
    content TEXT NOT NULL,
    msg_time BIGINT NOT NULL,
    status INT NOT NULL,
    KEY idx_chat (chat_type,target_id,msg_time)
);

-- 文件元数据
CREATE TABLE IF NOT EXISTS file_meta (
    file_id VARCHAR(64) PRIMARY KEY,
    file_name VARCHAR(256) NOT NULL,
    file_size BIGINT NOT NULL,
    uploader_uid INT NOT NULL,
    upload_time BIGINT NOT NULL,
    storage_path VARCHAR(256) NOT NULL,
    file_hash VARCHAR(128),
    chat_type VARCHAR(16),
    target_id INT,
    status INT NOT NULL,
    received BIGINT NOT NULL
);
