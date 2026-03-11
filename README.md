# edoyun_assess2_client

易道云提升阶段考核项目——**群聊客户端**。基于 Qt5/Qt6 开发的桌面 GUI 应用，与配套服务端（edoyun_access2_server）通信，支持群聊消息收发和文件上传/下载。

## 功能特性

- **用户登录**：输入用户名、服务器 IP 和端口，通过 TCP 连接到服务器
- **群聊消息**：实时发送和接收群组聊天消息，支持多用户广播
- **文件上传**：选择本地文件，分块上传到服务器，带进度条显示
- **文件下载**：点击聊天记录中的文件消息，自动下载到系统下载目录
- **传输取消**：上传/下载过程中可随时取消，自动清理临时文件
- **SHA1 校验**：文件以 SHA1 哈希值作为唯一标识，服务器按 SHA1 存储和检索

## 界面说明

应用窗口（800×600）分为两个页面，通过 `QStackedWidget` 切换：

### 登录页
| 控件 | 说明 |
|------|------|
| 用户名输入框 | 填写群聊中显示的昵称 |
| 服务器地址输入框 | 填写服务端 IP 地址 |
| 端口号输入框 | 填写服务端监听端口 |
| 登录按钮 | 发起 TCP 连接，成功后跳转主页 |

### 主页（聊天页）
| 控件 | 说明 |
|------|------|
| 聊天列表（上方） | 显示系统消息、自己/他人的聊天消息和文件消息 |
| 输入框（左下） | 输入待发送的文字消息 |
| 上传文件按钮（右上） | 打开文件选择对话框，开始上传 |
| 发送消息按钮（右下） | 发送输入框中的文字消息 |

消息样式：
- 自己发送的消息：右对齐，绿色背景（`#9EEA6A`）
- 他人消息：左对齐，白色背景
- 系统消息：居中，灰色字体
- 文件消息：蓝色字体，点击触发下载

## 通信协议

客户端与服务端使用自定义二进制协议 **Train**，格式如下：

```
[payload_length : 8字节 LE] [msgType : 4字节 LE] [payload : N字节]
```

### 消息类型

| MsgType | 值 | 方向 | 说明 |
|---------|-----|------|------|
| GroupChat | 1001 | 双向 | 群聊文字消息 |
| UploadBegin | 1002 | 客→服 | 开始上传，携带用户名、文件名、SHA1、文件大小 |
| UploadChunk | 1003 | 客→服 | 上传数据块，携带用户名、偏移量、数据内容 |
| DownloadBegin | 1004 | 客→服 | 请求下载，携带文件 SHA1 |
| DownloadEnd | 1005 | 服→客 | 文件下载完成通知 |
| DownloadChunk | 1007 | 服→客 | 下载数据块，携带 SHA1、偏移量、数据内容 |
| FileStatus | 1008 | 服→客 | 操作状态回复（Uncompleted/Completed/Error）|

### 文件消息格式

文件上传完成后，服务端会向所有客户端广播一条 GroupChat 消息，payload 包含文件信息：

```
[发送者用户名] [FILE]<sha1>|<filename>|<filesize>
```

客户端解析后在聊天列表中渲染为可点击的文件消息气泡。

## 文件传输流程

### 上传
1. 选择文件后，后台线程（`QtConcurrent`）异步计算 SHA1
2. 发送 `UploadBegin`（用户名、文件名、SHA1、文件大小）
3. 服务端回复 `FileStatus(Uncompleted)` 表示准备就绪
4. 客户端逐块（4096 字节）发送 `UploadChunk`，通过 `QTimer::singleShot(0)` 驱动，保持 UI 响应
5. 全部数据发送完毕后显示文件气泡，上传完成

### 下载
1. 点击文件气泡，发送 `DownloadBegin`（SHA1）
2. 服务端分块推送 `DownloadChunk`（SHA1、偏移量、数据）
3. 客户端按偏移量写入本地文件，实时更新进度条
4. 收到 `DownloadEnd` 后关闭文件，显示保存路径

## 项目结构

```
edoyun_assess2_client/
├── main.cpp                          # 应用入口，禁用系统代理，启动主窗口
├── mainwindow.h / mainwindow.cpp     # 主窗口：登录、聊天、文件传输逻辑
├── mainwindow.ui                     # Qt Designer UI 布局文件
├── message.h                         # Train 协议定义：消息类型、序列化/反序列化
└── edoyun_assess2_client.pro         # Qt qmake 项目文件
```

### 关键类与文件

**`message.h`** — 协议核心
- `MsgType` 枚举：定义所有消息类型
- `StatusType` 枚举：`Uncompleted / Completed / StatusError`
- `Train` 类：协议帧的序列化（`toWire()`）与反序列化（`fromBuffer()`）
- 辅助函数：`writeField / readField / writeU64 / readU64 / writeI32 / readI32`

**`MainWindow`** — 主界面
- `sendTrain()` — 将 Train 帧写入 TCP socket
- `processRecvBuffer()` — 从接收缓冲区解析完整 Train 帧
- `handleTrain()` — 根据 msgType 分发处理逻辑
- `startUpload()` / `sendNextChunk()` / `cleanupUpload()` — 上传状态机
- `startDownloadBySha1()` / `cleanupDownload()` — 下载状态机
- `calcFileSha1()` — 静态方法，计算文件 SHA1（在线程池中调用）

## 环境要求

- Qt 5.x 或 Qt 6.x
- Qt 模块：`core gui widgets network concurrent`
- C++17 编译器

## 构建方式

使用 Qt Creator 打开 `.pro` 文件，或命令行：

```bash
qmake edoyun_assess2_client.pro
make
```

## 注意事项

- 启动时会禁用系统代理（`QNetworkProxy::NoProxy`），避免代理干扰 TCP 连接
- 下载文件默认保存到系统下载目录（`QStandardPaths::DownloadLocation`），若文件名已存在则自动添加序号后缀
- 同一时刻只允许一个上传或下载任务进行
