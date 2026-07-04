# SoulCove SSH 远程隧道集成 — 完整开发方案

> 创建日期：2026-06-16 | 基于 V1.6 架构扩展

---

## 一、功能总览

将 SoulCove 从**本地编辑器**升级为 **本地+远程双模式编辑器**：
- 通过 SSH 连接虚拟机/远程服务器
- 远程终端（SSH PTY）— 复用现有 TerminalView
- SFTP 文件浏览/编辑/保存
- 连接管理 + 断线重连

---

## 二、技术选型

### 2.1 SSH 库

| 方案 | 优点 | 缺点 | 推荐度 |
|------|------|------|--------|
| **libssh2** (C库) | 成熟稳定、文档全、Windows支持好 | 需手动封装Qt接口 | ⭐⭐⭐⭐⭐ |
| QSSH (Qt封装) | Qt原生API | 维护不活跃、bug多 | ⭐⭐⭐ |
| libssh (C库) | 功能全面 | API复杂、Windows编译麻烦 | ⭐⭐⭐ |
| 自建socket实现 | 零依赖 | 工作量巨大、安全风险高 | ⭐ |

**最终选择：libssh2 + 自行封装 Qt 接口层**

理由：
- Windows 下 MinGW/GCC 编译友好（有预编译包）
- 异步非阻塞 API，适合 GUI 应用
- 支持 SSH2 全部特性：密钥认证、端口转发、SFTP

### 2.2 依赖获取方式

```cmake
# 方案A：vcpkg（推荐）
vcpkg install libssh2:x64-windows

# 方案B：直接下载预编译
# https://www.libssh.org/download.html → libssh2-1.11.0
# 需要：libssh2.dll + libssh2.lib + 头文件
```

### 2.3 目录规划

```
SoulCoveFinal/
├── src/
│   ├── ui/
│   │   ├── CommandPalette.h/cpp          # 已有
│   │   ├── TerminalView.h/cpp            # 已有（复用于远程终端）
│   │   ├── TerminalBackend.h/cpp         # 已有（新增SSH后端）
│   │   ├── EmbeddedTerminal.h/cpp        # 已有
│   │   ├── RemoteConnectionDialog.h/cpp  # [新建] 连接/编辑对话框
│   │   ├── RemoteFileTree.h/cpp          # [新建] SFTP远程文件树
│   │   └── SftpFileOperator.h/cpp        # [新建] SFTP文件操作接口
│   │
│   ├── core/
│   │   ├── ThemeManager.h/cpp            # 已有
│   │   ├── ConfigManager.h/cpp           # 已有
│   │   ├── ConnectionManager.h/cpp        # [新建] SSH连接管理(单例)
│   │   ├── SshSession.h/cpp              # [新建] libssh2会话封装
│   │   ├── SshChannel.h/cpp              # [新建] SSH通道封装(终端)
│   │   └── SftpClient.h/cpp              # [新建] SFTP客户端封装
│   │
│   └── interfaces/
│       ├── IFileOperator.h               # 已有（扩展远程协议支持）
│       ├── ITerminalBackend.h            # [新建] 终端后端抽象接口
│       └── IRemoteConnection.h           # [新建] 远程连接抽象接口
│
├── thirdparty/
│   └── libssh2/                          # SSH库文件
│       ├── include/                      # 头文件
│       ├── lib/                          # .lib静态库
│       └── bin/                          # .dll动态库
│
├── config/
│   └── ssh_connections.json              # [新建] 保存的连接配置
│
└── CMakeLists.txt                        # 添加libssh2链接
```

---

## 三、架构设计

### 3.1 整体架构图

```
┌─────────────────────────────────────────────────────┐
│                    UI 层                            │
│                                                     │
│  ┌──────────┐  ┌──────────────┐  ┌──────────────┐  │
│  │侧边栏     │  │  编辑区       │  │  终端面板     │  │
│  │          │  │              │  │              │  │
│  │本地文件树 │  │ 本地/远程     │  │ 本地/远程     │  │
│  │+远程文件树│  │ 标签页编辑    │  │ PTY终端      │  │
│  └────┬─────┘  └──────┬───────┘  └──────┬───────┘  │
│       │               │                  │          │
├───────┼───────────────┼──────────────────┼──────────┤
│       ▼               ▼                  ▼          │
│  ┌──────────┐  ┌──────────────┐  ┌──────────────┐  │
│  │IFileOp   │  │ IFileOp      │  │ ITerminalBk  │  │
│  │ (本地)   │  │ (SFTP)       │  │ (SSH)        │  │
│  └────┬─────┘  └──────┬───────┘  └──────┬───────┘  │
│       │               │                  │          │
├───────┼───────────────┼──────────────────┼──────────┤
│       ▼               ▼                  ▼          │
│  ┌─────────────────────────────────────────────┐   │
│  │              Core 层                         │   │
│  │                                             │   │
│  │  ┌─────────────┐  ┌────────────────────┐   │   │
│  │  │ConnectionMgr │  │  SshSession        │   │   │
│  │  │ (单例)       │→ │  (libssh2封装)     │   │   │
│  │  │ 管理所有连接  │  │  认证/通道/SFTP    │   │   │
│  │  └─────────────┘  └────────────────────┘   │   │
│  │                                             │   │
│  │  ┌─────────────┐  ┌────────────────────┐   │   │
│  │  │ SshChannel   │  │  SftpClient        │   │   │
│  │  │ (PTY终端)    │  │ (文件传输)         │   │   │
│  │  └─────────────┘  └────────────────────┘   │   │
│  └─────────────────────────────────────────────┘   │
│                                                     │
├─────────────────────────────────────────────────────┤
│                  libssh2 (C库)                       │
└─────────────────────────────────────────────────────┘
```

### 3.2 数据流

```
用户操作                    内部处理                     网络层
───────                    ────────                    ──────

打开远程文件:
  双击远程文件树节点
    → SftpFileOperator::openFile(path)
      → SftpClient::downloadToTemp(remotePath)
        → libssh2_sftp_read() ──────→ SSH SFTP 协议 ──→ 远程服务器
        ← 返回临时本地文件路径 ←───────────────────────────
      → MyTextEdit::openLocalFile(tempPath)
      → 记录 remotePath ↔ tempPath 映射

保存远程文件:
  Ctrl+S
    → 检查当前标签是否为远程文件
    → SftpFileOperator::saveFile(tempPath, remotePath)
      → SftpClient::uploadFromFile(tempPath, remotePath)
        → libssh2_sftp_write() ──────→ SSH SFTP 协议 ──→ 远程服务器

远程终端输入:
  在 TerminalView 中打字 + Enter
    → SshChannel::write(input)
      → libssh2_channel_write() ────→ SSH 协议 ──→ 远程 PTY
      ← libssh2_channel_read() ←─── SSH 协议 ←── 远程 stdout
    → TerminalView::appendOutput(data)  // ANSI渲染显示
```

---

## 四、完整待做清单（按执行顺序）

### Phase 1：基础设施（~3个任务）

#### 任务 1.1：引入 libssh2 依赖
- [ ] 下载 libssh2 Windows 预编译包（或 vcpkg install）
- [ ] 放入 `thirdparty/libssh2/` 目录
- [ ] 修改 `CMakeLists.txt`：添加 include 路径 + link 库 + DLL 部署
- [ ] 创建 `src/core/SshSession.h/cpp` 基础骨架
- [ ] 编写最小测试：初始化 libssh2 + 清理，验证编译通过
- **预估工作量**：0.5天
- **产出物**：可编译的空壳 SSH 模块

#### 任务 1.2：SshSession 封装（核心）
创建 `src/core/SshSession.h/cpp`

```cpp
class SshSession : public QObject {
    Q_OBJECT
public:
    // 连接管理
    bool connect(const QString& host, int port,
                 const QString& user, const QString& password);
    bool connectWithKey(const QString& host, int port,
                        const QString& user,
                        const QString& privateKeyPath,
                        const QString& passphrase = "");
    void disconnect();
    bool isConnected() const;

    // 会话信息
    QString host() const;
    int port() const;
    QString user() const;
    QString errorString() const;

    // 创建子对象
    std::unique_ptr<SshChannel> openShell(int cols=80, int rows=24);
    std::unique_ptr<SshChannel> execCommand(const QString& command);
    std::unique_ptr<SftpClient> sftp();

signals:
    void connected();
    void disconnected();
    void errorOccurred(const QString& error);

private:
    LIBSSH2_SESSION* m_session = nullptr;
    SOCKET m_socket = INVALID_SOCKET;
    // ... 内部状态
};
```

具体实现要求：
- [ ] TCP socket 连接（Winsock2 on Windows）
- [ ] libssh2_session_handshake() 握手
- [ ] 密码认证：libssh2_userauth_password()
- [ ] 密钥认证：libssh2_userauth_publickey_fromfile()
- [ ] host key 验证（首次连接指纹确认）
- [ ] 异步非阻塞模式（libssh2_set_blocking(session, 0)）
- [ ] 心跳保活（keepalive 每30秒）
- [ ] 断线检测与自动清理
- **预估工作量**：1天
- **产出物**：可建立/断开 SSH 连接

#### 任务 1.3：ConnectionManager 单例
创建 `src/core/ConnectionManager.h/cpp`

```cpp
class ConnectionManager : public QObject {
    Q_OBJECT
public:
    static ConnectionManager& instance();

    // 连接 CRUD
    QStringList connectionIds() const;
    SshSession* getSession(const QString& id);  // 已连接则返回
    bool connectTo(const QString& id);          // 触发连接
    void disconnectFrom(const QString& id);

    // 连接配置持久化
    struct ConnectionConfig {
        QString id;             // 唯一ID
        QString name;           // 显示名称 "VM-Ubuntu"
        QString host;           // IP或域名
        int port = 22;          // 端口
        QString username;       // 用户名
        AuthType authType;      // Password / Key / Agent
        QString password;       // 密码（加密存储）
        QString privateKeyPath; // 密钥路径
        QString passphrase;     // 密钥密码
    };
    QList<ConnectionConfig> allConfigs() const;
    void saveConfig(const ConnectionConfig& cfg);
    void removeConfig(const QString& id);
    void loadAllConfigs();      // 启动时从JSON加载

signals:
    void sessionConnected(const QString& id);
    void sessionDisconnected(const QString& id);
    void sessionError(const QString& id, const QString& error);

private:
    QMap<QString, SshSession*> m_sessions;     // id → 活跃会话
    QMap<QString, ConnectionConfig> m_configs; // id → 配置
    QString m_configFilePath;                  // JSON存储路径
};
```

具体实现要求：
- [ ] JSON 格式持久化（复用现有 QJsonDocument 能力）
- [ ] 密码 AES 加密存储（或使用系统凭据管理器）
- [ ] 多连接并行管理
- [ ] 连接状态枚举：Disconnected / Connecting / Connected / Error
- [ ] 信号驱动 UI 更新
- **预估工作量**：0.5天
- **产出物**：连接配置的增删改查 + 多会话管理

---

### Phase 2：远程终端（~2个任务）

#### 任务 2.1：SshChannel 封装（PTY 终端）
创建 `src/core/SshChannel.h/cpp`

```cpp
class SshChannel : public QObject {
    Q_OBJECT
public:
    explicit SshChannel(LIBSSH2_SESSION* session);
    ~SshChannel();

    // 打开 shell / 执行命令
    bool openShell(int cols=80, int rows=24);
    bool execCommand(const QString& cmd);

    // I/O
    qint64 write(const QByteArray& data);
    void resizePty(int cols, int rows);
    void sendSignal(const QString& signal);  // SIGINT/SIGTERM等
    void close();

    bool isOpen() const;
    QString errorString() const;

signals:
    void readyReadStandardOutput(const QByteArray& data);
    void readyReadStandardError(const QByteArray& data);
    void channelClosed();
    void channelError(const QString& error);

private slots:
    void poll();  // 定时轮询 libssh2 通道数据

private:
    LIBSSH2_CHANNEL* m_channel = nullptr;
    QTimer* m_pollTimer = nullptr;  // 10ms轮询
    // ...
};
```

具体实现要求：
- [ ] libssh2_channel_open_session() + request_pty("xterm-256color")
- [ ] libssh2_channel_shell() 或 libssh2_channel_exec()
- [ ] QTimer 10ms 轮询：libssh2_channel_read() 非阻塞读取
- [ ] stdout/stderr 分离读取
- [ ] 窗口大小变化信号：SIGWINCH → libssh2_channel_request_pty_size()
- [ ] Ctrl+C 发送：write("\x03")
- [ ] EOF 检测：libssh2_channel_eof()
- **预估工作量**：1天
- **产出物**：可用的 SSH PTY 通道

#### 任务 2.2：集成到 EmbeddedTerminal（复用 TerminalView）

修改现有文件：

**EmbeddedTerminal.h/cpp 变更：**
- [ ] 新增 `createRemoteSession()` 方法
- [ ] TerminalBackend 改为接口（ITerminalBackend），新增 SshTerminalBackend 实现
- [ ] 或者更简单：让 TerminalBackend 同时支持 QProcess 和 SshChannel（策略模式）

```cpp
// 方案：策略模式（推荐，改动最小）
class TerminalBackend : public QObject {
    enum class BackendType { LocalProcess, SshChannel };
    // ... 现有代码保留
    // 新增：
    void setSshChannel(SshChannel* channel);  // 注入SSH通道
    // write()/start() 根据 m_backendType 分发到 QProcess 或 SshChannel
};
```

**widget.cpp 变更：**
- [ ] 侧边栏新增"远程"按钮（在活动栏底部）
- [ ] 点击后弹出连接列表 / 新建连接对话框
- [ ] 连接成功后自动打开远程终端标签
- [ ] 远程终端标签显示为 "hostname: user@192.168.x.x"
- **预估工作量**：0.5天
- **产出物**：可在 UI 中打开并使用 SSH 远程终端

---

### Phase 3：SFTP 文件操作（~3个任务）

#### 任务 3.1：SftpClient 封装
创建 `src/core/SftpClient.h/cpp`

```cpp
class SftpClient : public QObject {
    Q_OBJECT
public:
    explicit SftpClient(LIBSSH2_SESSION* session);
    ~SftpClient();

    // 目录操作
    struct FileInfo {
        QString name;
        bool isDirectory;
        quint64 size;
        QDateTime lastModified;
        QString permissions;  // rwxr-xr-x
    };
    QList<FileInfo> listDir(const QString& path);
    bool mkdir(const QString& path);
    bool remove(const QString& path);  // 文件或空目录
    bool rename(const QString& oldPath, const QString& newPath);

    // 文件传输
    bool download(const QString& remotePath, const QString& localPath,
                  std::function<void(qint64 done, qint64 total)> progress = nullptr);
    bool upload(const QString& localPath, const QString& remotePath,
                std::function<void(qint64 done, qint64 total)> progress = nullptr);

    // 流式读写（大文件）
    QByteArray readFile(const QString& remotePath, qint64 maxSize = 10*1024*1024);
    bool writeFile(const QString& remotePath, const QByteArray& data);

    bool isAvailable() const;
    QString errorString() const;

signals:
    void transferProgress(qint64 bytesDone, qint64 bytesTotal);
    void transferComplete(const QString& path);
    void transferError(const QString& path, const QString& error);

private:
    LIBSSH2_SFTP* m_sftp = nullptr;
};
```

具体实现要求：
- [ ] libssh2_sftp_init() 初始化
- [ ] libssh2_sftp_opendir() + readdir() 目录遍历
- [ ] libssh2_sftp_open() + read/write 文件传输
- [ ] 进度回调（每4KB触发一次信号）
- [ ] 大文件分块传输
- [ ] 错误处理：权限不足/空间不足/断连恢复
- **预估工作量**：1天
- **产出物**：完整的 SFTP 客户端能力

#### 任务 3.2：远程文件树 UI
创建 `src/ui/RemoteFileTree.h/cpp`

UI 设计：
```
┌──────────────────────────────┐
│ 🖥 远程: VM-Ubuntu          │  ← 连接名称
│ ├─ 📁 home                  │
│ │  └─ 📁 user               │
│ │     ├─ 📁 projects        │
│ │     │  └─ 📄 main.py      │  ← 双击打开
│ │     ├─ 📄 .bashrc         │
│ │     └─ 📄 .profile        │
│ ├─ 📁 etc                   │
│ └─ 📁 var                   │
│                              │
│ [🔄刷新] [📤上传] [⬇下载]    │  ← 工具栏按钮
└──────────────────────────────┘
```

具体实现要求：
- [ ] 继承 QTreeWidget（与本地 SideBar 文件树风格一致）
- [ ] 异步加载目录（避免 UI 卡顿）：点击展开 → 后台 SFTP → 回调填充
- [ ] 图标区分：文件夹/文件/可执行/符号链接
- [ ] 右键菜单：打开/下载/上传/删除/重命名/新建
- [ ] 双击文件 → 触发 downloadToTemp → 打开编辑器标签
- [ ] 标签标题显示远程路径前缀 `[remote] /home/user/main.py`
- [ ] 加载中显示 spinner 动画
- [ ] 断连时显示警告提示
- **预估工作量**：1天
- **产出物**：可视化的远程文件浏览器

#### 任务 3.3：远程文件编辑集成
修改现有文件：

**EditorTabBar 变更：**
- [ ] 标签数据结构增加字段：`bool isRemote; QString remotePath; QString tempLocalPath;`
- [ ] 远程标签图标加一个小云朵/链式图标标识
- [ ] 保存时检测 isRemote → 调用 SFTP upload

**MyTextEdit/FileOperator 变更：**
- [ ] 新增 SftpFileOperator 实现 IFileOperator 接口
- [ ] openFile(): SFTP download 到临时文件 → 打开
- [ ] saveFile(): 保存到临时文件 → SFTP upload 回远程
- [ ] 关闭标签时清理临时文件

**widget.cpp 变更：**
- [ ] 侧边栏切换：本地/远程文件树（QStackedWidget 切换）
- [ ] 远程文件保存进度条显示（状态栏或覆盖层）
- [ ] 编辑远程文件时的状态栏显示："已连接 VM-Ubuntu | 远程文件"
- **预估工作量**：1天
- **产出物**：完整的远程文件编辑工作流

---

### Phase 4：连接管理与体验优化（~3个任务）

#### 任务 4.1：连接管理对话框
创建 `src/ui/RemoteConnectionDialog.h/cpp`

UI 设计：
```
┌─ SSH 连接管理 ──────────────────────┐
│                                      │
│ [+ 新建连接]                         │
│                                      │
│ ┌──────────────────────────────────┐ │
│ │ 🟢 VM-Ubuntu  user@192.168.56.101│ │
│ │    [连接] [编辑] [断开] [删除]    │ │
│ ├──────────────────────────────────┤ │
│ │ ⚪ CentOS    root@192.168.56.102 │ │
│ │    [连接] [编辑] [     ] [删除]   │ │
│ ├──────────────────────────────────┤ │
│ │ 🔴 AWS-EC2   ec2-user@52.xx.xx   │ │
│ │    [连接] [编辑] [断开] [删除]    │ │
│ └──────────────────────────────────┘ │
│                                      │
│ ☑ 启动时自动连接上次使用的            │
│                                      │
│              [取消]  [确定]           │
└──────────────────────────────────────┘
```

新建/编辑连接子对话框：
```
┌─ 编辑连接 ──────────────────────────┐
│                                     │
│ 名称:     [VM-Ubuntu_________]      │
│ 主机:     [192.168.56.101______]    │
│ 端口:     [22_______________]       │
│ 用户名:   [user_____________]       │
│                                     │
│ 认证方式:  (●) 密码  ( ) 密钥  ( ) Agent │
│                                     │
│ 密码:     [••••••••_________]  👁   │  ← 显示/隐藏
│                                     │
│ 或 密钥路径: [Browse...____________] │
│ 密钥密码:  [___________________]    │
│                                     │
│ ☑ 保存密码                          │
│                                     │
│      [测试连接]    [取消]  [确定]   │
└─────────────────────────────────────┘
```

具体实现要求：
- [ ] QDialog 子类，暗色主题样式
- [ ] 表单验证（主机必填、端口范围 1-65535）
- [ ] "测试连接" 按钮 → 尝试连接 → 显示成功/失败提示
- [ ] 密码输入框带显示/隐藏切换
- [ ] 密钥文件选择器（QFileDialog）
- [ ] 连接状态指示灯（绿/黄/红）
- **预估工作量**：1天
- **产出物**：完整的连接管理 UI

#### 任务 4.2：设置页集成
修改 SettingsPage：

- [ ] 导航列表新增"远程"分类（第6个）
- [ ] 默认连接选择
- [ ] 终端默认尺寸（列数/行数）
- [ ] SFTP 传输缓冲区大小
- [ ] 断线重连间隔（秒）
- [ ] 超时时间（秒）
- **预估工作量**：0.5天

#### 任务 4.3：断线重连 + 健壮性
- [ ] SSH 断连检测（poll 返回错误 / socket 异常）
- [ ] 自动重连机制（指数退避：1s → 2s → 4s → 8s → 最大30s）
- [ ] 重连中 UI 状态提示（终端区显示"正在重连..."）
- [ ] 重连成功后恢复 SFTP 文件树
- [ ] 操作中断保护（上传/下载中途断连 → 暂停 → 重连后续传）
- [ ] 网络异常友好提示（不可达/拒绝/超时/认证失败）
- **预估工作量**：1天

---

### Phase 5：高级功能（可选，MVP 之后）

| # | 功能 | 说明 | 优先级 |
|---|------|------|--------|
| 5.1 | **端口转发** | SSH -L/-R 端口映射，调试远程服务 | 低 |
| 5.2 | **SCP 批量传输** | 整文件夹上传/下载 | 低 |
| 5.3 | **跳板机/Jump Host** | A → B → C 多跳连接 | 低 |
| 5.4 | **SSH Config 解析** | 读取 ~/.ssh/config 自动填充 | 中 |
| 5.5 | **文件同步** | 本地↔远程双向同步（类似 rsync） | 低 |
| 5.6 | **终端分屏** | 远程终端也支持水平分割 | 低 |

---

## 五、任务依赖关系

```
Phase 1 (基础)
  1.1 引入libssh2 ──────────────────────┐
       ↓                                 │
  1.2 SshSession封装 ◄──────────────────┘
       ↓
  1.3 ConnectionManager ◄─── 需要 1.2
       ↓
Phase 2 (远程终端)
  2.1 SshChannel封装 ◄──── 需要 1.2
       ↓
  2.2 集成到EmbeddedTerminal ◄─ 需要 2.1 + 1.3
       ↓
Phase 3 (SFTP文件)
  3.1 SftpClient封装 ◄───── 需要 1.2
       ↓
  3.2 远程文件树UI ◄─────── 需要 3.1 + 1.3
       ↓
  3.3 远程文件编辑集成 ◄─── 需要 3.2 + 2.2
       ↓
Phase 4 (体验)
  4.1 连接管理对话框 ◄──── 需要 1.3
  4.2 设置页集成 ◄──────── 需要 1.3
  4.3 断线重连 ◄────────── 需要 1.2 + 2.1 + 3.1
```

**关键路径（最短完成时间）**：
```
1.1 → 1.2 → (2.1 → 2.2) || (3.1 → 3.2 → 3.3) → 4.1
```
**MVP 最小可用版本**：Phase 1 + Phase 2（约 3 天工作量）

---

## 六、工作量估算汇总

| Phase | 任务数 | 预估工时 | 累计 |
|-------|--------|---------|------|
| Phase 1: 基础设施 | 3 | 2天 | 2天 |
| Phase 2: 远程终端 | 2 | 1.5天 | 3.5天 |
| Phase 3: SFTP 文件 | 3 | 3天 | 6.5天 |
| Phase 4: 连接管理 | 3 | 2.5天 | 9天 |
| Phase 5: 高级功能 | 6 | 可选 | — |
| **总计 (MVP)** | **8 个核心任务** | **~6.5天** | — |

---

## 七、验收标准（MVP 版本）

- [ ] 可以添加/编辑/删除 SSH 连接配置
- [ ] 可以通过密码或密钥认证连接到虚拟机
- [ ] 连接后在底部面板打开远程终端，可以正常执行命令
- [ ] 远程终端 ANSI 输出正确渲染（颜色/光标）
- [ ] 左侧显示远程文件树，可以浏览目录
- [ ] 双击远程文件可以在编辑器中打开并编辑
- [ ] Ctrl+S 保存远程文件（上传回服务器）
- [ ] 断线时有明确提示，重连后可恢复
- [ ] 切换主题时远程相关 UI 样式跟随变化

---

## 八、风险与缓解

| 风险 | 概率 | 影响 | 缓解措施 |
|------|------|------|---------|
| libssh2 Windows 编译问题 | 中 | 高 | 使用预编译包 / vcpkg |
| SSH 连接被防火墙阻挡 | 中 | 中 | 设置页添加代理选项 |
| 大文件传输内存溢出 | 低 | 高 | 分块流式传输（4KB块） |
| PTY 终端兼容性差 | 低 | 中 | fallback 到 exec mode |
| 密码安全性 | 中 | 中 | Windows DPAPI 加密存储 |
