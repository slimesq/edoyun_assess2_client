#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QMessageBox>
#include <QPushButton>
#include <QTcpSocket>
#include <QFileDialog>
#include <QProgressDialog>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QInputDialog>
#include <QTimer>
#include <QCryptographicHash>
#include <QDebug>
#include <QListWidgetItem>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>

// 文件消息的特殊前缀，用于在 GroupChat payload 中区分普通聊天和文件分享
const QString MainWindow::FILE_MSG_PREFIX = QStringLiteral("[FILE]");

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow),
    socket(new QTcpSocket(this)),
    uploadFile(nullptr),
    uploadTotal(0),
    uploadSent(0),
    downloadFile(nullptr),
    downloadTotal(0),
    downloadReceived(0),
    progressDialog(nullptr) {
    ui->setupUi(this);

    // 初始化进度对话框
    progressDialog = new QProgressDialog(this);
    progressDialog->setWindowTitle("文件传输");
    progressDialog->setLabelText("正在传输文件...");
    progressDialog->setCancelButtonText("取消");
    progressDialog->setMinimum(0);
    progressDialog->setWindowModality(Qt::WindowModal);
    progressDialog->reset();
    connect(progressDialog, &QProgressDialog::canceled, this, &MainWindow::onTransferCanceled);

    // 启动时显示登录页
    ui->stackedWidget->setCurrentWidget(ui->loginPage);

    // 登录按钮
    connect(ui->logBtn, &QPushButton::clicked, this, &MainWindow::onLoginClicked);

    // 连接成功
    connect(socket, &QTcpSocket::connected, this, [this]() {
        qDebug() << "连接成功";
        ui->stackedWidget->setCurrentWidget(ui->homePage);
        addSystemMessage("已连接到服务器");
    });

    // 连接失败
    connect(socket, &QTcpSocket::errorOccurred, this, [this](auto) {
        QMessageBox::warning(this, "警告", "连接服务器失败");
        qDebug() << "[错误详情]：" << socket->errorString();
        ui->stackedWidget->setCurrentWidget(ui->loginPage);
    });

    // 发送按钮
    connect(ui->sendButton, &QPushButton::clicked, this, &MainWindow::onSendBtnClicked);

    // 文件传输按钮
    connect(ui->uploadButton, &QPushButton::clicked, this, &MainWindow::onUploadClicked);

    // 点击聊天消息（用于点击文件消息触发下载）
    connect(ui->chatWidget, &QListWidget::itemClicked, this, &MainWindow::onChatItemClicked);

    // 接收数据
    connect(socket, &QTcpSocket::readyRead, this, [this]() {
        recvBuffer.append(socket->readAll());
        processRecvBuffer();
    });
}

MainWindow::~MainWindow() {
    socket->disconnectFromHost();
    if (uploadFile) {
        uploadFile->close();
        delete uploadFile;
    }
    if (downloadFile) {
        downloadFile->close();
        delete downloadFile;
    }
    delete progressDialog;
    delete ui;
}

// ---- 登录 ----

void MainWindow::onLoginClicked() {
    if (ui->usernameEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "警告", "用户名不能为空");
        return;
    }
    if (ui->ipEdit->text().isEmpty()) {
        QMessageBox::warning(this, "警告", "IP地址不能为空");
        return;
    }
    if (ui->portEdit->text().isEmpty()) {
        QMessageBox::warning(this, "警告", "端口号不能为空");
        return;
    }

    username = ui->usernameEdit->text().trimmed();
    socket->connectToHost(ui->ipEdit->text(), ui->portEdit->text().toUShort());
}

// ---- 消息显示 ----

void MainWindow::addMyMessage(const QString &text) {
    QListWidgetItem *item = new QListWidgetItem(text);
    item->setTextAlignment(Qt::AlignRight);
    item->setBackground(QColor("#9EEA6A"));
    ui->chatWidget->addItem(item);
    ui->chatWidget->scrollToBottom();
}

void MainWindow::addServerMessage(const QString& text) {
    QListWidgetItem *item = new QListWidgetItem(text);
    item->setTextAlignment(Qt::AlignLeft);
    item->setBackground(Qt::white);
    ui->chatWidget->addItem(item);
    ui->chatWidget->scrollToBottom();
}

void MainWindow::addSystemMessage(const QString& text) {
    QListWidgetItem* item = new QListWidgetItem("[系统] " + text);
    item->setTextAlignment(Qt::AlignCenter);
    item->setForeground(Qt::gray);
    item->setBackground(QColor("#F0F0F0"));
    ui->chatWidget->addItem(item);
    ui->chatWidget->scrollToBottom();
}

void MainWindow::addFileMessage(const QString& sha1, const QString& fileName, quint64 fileSize, bool isMine) {
    // 友好的文件大小显示
    QString sizeStr;
    if (fileSize < 1024)
        sizeStr = QString::number(fileSize) + " B";
    else if (fileSize < 1024 * 1024)
        sizeStr = QString::number(fileSize / 1024.0, 'f', 1) + " KB";
    else
        sizeStr = QString::number(fileSize / (1024.0 * 1024.0), 'f', 2) + " MB";

    QString display = QString("[文件] %1 (%2)\n点击下载").arg(fileName, sizeStr);

    QListWidgetItem* item = new QListWidgetItem(display);
    item->setTextAlignment(isMine ? Qt::AlignRight : Qt::AlignLeft);
    item->setBackground(isMine ? QColor("#9EEA6A") : QColor("#D0E8FF"));
    item->setForeground(QColor("#0066CC"));

    // 把 sha1、文件名、文件大小存到 item 的自定义数据中，点击时取出
    item->setData(Qt::UserRole, sha1);
    item->setData(Qt::UserRole + 1, fileName);
    item->setData(Qt::UserRole + 2, fileSize);

    ui->chatWidget->addItem(item);
    ui->chatWidget->scrollToBottom();
}

void MainWindow::onChatItemClicked(QListWidgetItem* item) {
    QString sha1 = item->data(Qt::UserRole).toString();
    if (sha1.isEmpty()) return; // 不是文件消息，忽略

    QString suggestedName = item->data(Qt::UserRole + 1).toString();
    quint64 fileSize = item->data(Qt::UserRole + 2).toULongLong();
    startDownloadBySha1(sha1, suggestedName, fileSize);
}

void MainWindow::startDownloadBySha1(const QString& sha1, const QString& suggestedName, quint64 fileSize) {
    if (downloadFile) {
        QMessageBox::warning(this, "警告", "当前有正在进行的下载，请等待完成");
        return;
    }

    // 直接保存到系统下载目录，跳过文件对话框，避免冷启动延迟
    QString downloadDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (downloadDir.isEmpty()) downloadDir = QDir::homePath();

    // 若同名文件已存在，追加序号避免覆盖
    QString savePath = downloadDir + "/" + suggestedName;
    if (QFile::exists(savePath)) {
        QFileInfo fi(suggestedName);
        QString base = fi.completeBaseName();
        QString ext  = fi.suffix().isEmpty() ? "" : "." + fi.suffix();
        int n = 1;
        do {
            savePath = QString("%1/%2_%3%4").arg(downloadDir, base).arg(n++).arg(ext);
        } while (QFile::exists(savePath));
    }

    downloadFile = new QFile(savePath);
    if (!downloadFile->open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, "警告", "无法创建文件: " + savePath);
        delete downloadFile;
        downloadFile = nullptr;
        return;
    }

    // 已知大小时预分配磁盘空间，减少写入碎片
    if (fileSize > 0) downloadFile->resize(fileSize);

    downloadSha1     = sha1;
    downloadTotal    = fileSize;
    downloadReceived = 0;

    addSystemMessage(QString("下载至: %1").arg(savePath));

    sendTrain(Train::downloadBegin(sha1));

    progressDialog->setLabelText(QString("下载: %1").arg(suggestedName));
    progressDialog->setMaximum(fileSize > 0 ? 100 : 0);
    progressDialog->setValue(0);
    progressDialog->show();     // 非模态显示
}

// ---- 发送/接收 ----

void MainWindow::sendTrain(const Train& train) {
    if (!socket || socket->state() != QAbstractSocket::ConnectedState) {
        addSystemMessage("未连接到服务器，无法发送");
        return;
    }

    QByteArray wire = train.toWire();
    qint64 written = socket->write(wire);
    if (written == -1) {
        addSystemMessage("发送失败: " + socket->errorString());
    } else {
        socket->flush();
    }
}

void MainWindow::processRecvBuffer() {
    while (true) {
        Train train;
        int consumed = Train::fromBuffer(recvBuffer, train);
        if (consumed == 0) break; // 数据不完整，等待更多
        recvBuffer.remove(0, consumed);
        handleTrain(train);
    }
}

void MainWindow::handleTrain(const Train& train) {
    switch (train.msgType) {

    // ---- 群聊消息 ----
    case GroupChat: {
        QString msg = QString::fromUtf8(train.payload);
        qDebug() << "[GroupChat]" << msg;
        int fileIdx = msg.indexOf(FILE_MSG_PREFIX);
        if (fileIdx != -1) {
            addServerMessage(msg);
            // 文件分享消息: [FILE]sha1|filename|filesize
            QString body = msg.mid(fileIdx + FILE_MSG_PREFIX.length());
            int sep1 = body.indexOf('|');
            int sep2 = body.lastIndexOf('|');
            if (sep1 != -1 && sep2 != -1 && sep1 != sep2) {
                QString sha1     = body.left(sep1);
                QString fileName = body.mid(sep1 + 1, sep2 - sep1 - 1);
                quint64 fileSize = body.mid(sep2 + 1).toULongLong();
                addFileMessage(sha1, fileName, fileSize, false);
            }

        } else {
            addServerMessage(msg);
        }
        break;
    }

    // ---- 文件状态回复 ----
    case FileStatus: {
        auto status = train.parseFileStatus();

        if (status.originMsgType == UploadBegin) {
            if (status.statusType == Uncompleted) {
                // 服务端准备好了，开始发送数据块
                addSystemMessage("服务端已就绪，开始上传...");
                sendNextChunk();
            } else if (status.statusType == StatusError) {
                addSystemMessage("上传失败：服务端拒绝");
                cleanupUpload();
            }
        }
        else if (status.originMsgType == UploadChunk) {
            // 服务端只在写入出错时回复 FileStatus(error)
            if (status.statusType == StatusError) {
                addSystemMessage("上传出错：服务端写入失败");
                cleanupUpload();
            }
        }
        else if (status.originMsgType == DownloadBegin) {
            // 文件不存在等错误
            if (status.statusType == StatusError) {
                addSystemMessage("下载失败：文件不存在");
                cleanupDownload();
            }
        }
        break;
    }

    // ---- 下载数据块 ----
    case DownloadChunk: {
        auto chunk = train.parseDownloadChunk();
        if (downloadFile && downloadFile->isOpen()) {
            downloadFile->seek(chunk.offset);
            downloadFile->write(chunk.data);
            downloadReceived += chunk.data.size();
            // 更新进度条百分比
            if (downloadTotal > 0) {
                progressDialog->setValue(static_cast<int>(downloadReceived * 100 / downloadTotal));
            }
        }
        break;
    }

    // ---- 下载结束 ----
    case DownloadEnd: {
        auto end = train.parseDownloadEnd();
        if (downloadFile) {
            QString fileName = downloadFile->fileName();
            addSystemMessage(QString("文件下载完成: %1 (共 %2 字节)")
                             .arg(QFileInfo(fileName).fileName())
                             .arg(downloadReceived));
            cleanupDownload();
        }
        break;
    }

    default:
        qDebug() << "收到未知消息类型:" << train.msgType;
        break;
    }
}

// ---- 群聊发送 ----

void MainWindow::onSendBtnClicked() {
    QString text = ui->inputEdit->toPlainText().trimmed();
    if (text.isEmpty()) return;

    // 服务端广播给除发送者外的所有人，所以本地直接显示自己的消息
    addMyMessage(text);

    // payload 就是原始聊天内容，把用户名和消息组合在一起
    QString fullMsg = QString("[%1] %2").arg(username, text);
    Train train = Train::groupChat(fullMsg.toUtf8());
    sendTrain(train);

    ui->inputEdit->clear();
}

// ---- 文件上传 ----

void MainWindow::onUploadClicked() {
    if (uploadFile) {
        QMessageBox::warning(this, "警告", "当前有正在进行的上传，请等待完成");
        return;
    }

    QString filePath = QFileDialog::getOpenFileName(this, "选择要上传的文件", QDir::homePath());
    if (!filePath.isEmpty()) {
        startUpload(filePath);
    }
}

void MainWindow::startUpload(const QString& filePath) {
    if (!QFile::exists(filePath)) {
        QMessageBox::warning(this, "警告", "文件不存在");
        return;
    }

    uploadFile = new QFile(filePath);
    if (!uploadFile->open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "警告", "无法读取文件");
        delete uploadFile;
        uploadFile = nullptr;
        return;
    }

    uploadFileName = QFileInfo(filePath).fileName();
    uploadTotal    = uploadFile->size();
    uploadSent     = 0;

    // 先显示进度框，异步计算 SHA1，避免阻塞 UI
    progressDialog->setLabelText(QString("计算校验值: %1").arg(uploadFileName));
    progressDialog->setMaximum(0);
    progressDialog->setValue(0);
    progressDialog->show();

    auto* watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher]() {
        QString sha1 = watcher->result();
        watcher->deleteLater();

        if (sha1.isEmpty()) {
            QMessageBox::warning(this, "警告", "无法计算文件校验值");
            cleanupUpload();
            return;
        }

        uploadSha1 = sha1;
        addSystemMessage(QString("开始上传: %1 (大小: %2)").arg(uploadFileName).arg(uploadTotal));

        progressDialog->setLabelText(QString("上传: %1").arg(uploadFileName));
        progressDialog->setMaximum(100);
        progressDialog->setValue(0);

        sendTrain(Train::uploadBegin(username, uploadFileName, uploadSha1, uploadTotal));
    });
    watcher->setFuture(QtConcurrent::run(MainWindow::calcFileSha1, filePath));
}

void MainWindow::sendNextChunk() {
    if (!uploadFile || !uploadFile->isOpen()) return;

    QByteArray chunk = uploadFile->read(CHUNK_SIZE);
    if (chunk.isEmpty()) {
        // 所有数据已发送，服务端会自动广播 [FILE] 消息给其他客户端
        // 本地显示文件卡片
        addFileMessage(uploadSha1, uploadFileName, uploadTotal, true);
        addSystemMessage("文件上传完成！");
        cleanupUpload();
        return;
    }

    Train train = Train::uploadChunk(username, uploadSent, chunk);
    sendTrain(train);

    uploadSent += chunk.size();
    if (uploadTotal > 0) {
        progressDialog->setValue(static_cast<int>(uploadSent * 100 / uploadTotal));
    }

    // 不需要等待服务端回复，继续发送下一块（通过事件循环调度，保持 UI 响应）
    QTimer::singleShot(0, this, &MainWindow::sendNextChunk);
}

void MainWindow::cleanupUpload() {
    if (uploadFile) {
        uploadFile->close();
        delete uploadFile;
        uploadFile = nullptr;
    }
    uploadSha1.clear();
    uploadFileName.clear();
    uploadTotal = 0;
    uploadSent = 0;
    progressDialog->reset();
}

void MainWindow::cleanupDownload() {
    if (downloadFile) {
        downloadFile->close();
        delete downloadFile;
        downloadFile = nullptr;
    }
    downloadSha1.clear();
    downloadTotal = 0;
    downloadReceived = 0;
    progressDialog->reset();
}

// ---- 取消传输 ----

void MainWindow::onTransferCanceled() {
    if (uploadFile) {
        addSystemMessage("上传已取消");
        cleanupUpload();
    }
    if (downloadFile) {
        QString path = downloadFile->fileName();
        cleanupDownload();
        // 删除不完整的下载文件
        if (QFile::exists(path)) {
            QFile::remove(path);
        }
        addSystemMessage("下载已取消");
    }
}

// ---- SHA1 计算 ----

QString MainWindow::calcFileSha1(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return {};

    QCryptographicHash hash(QCryptographicHash::Sha1);
    while (!file.atEnd()) {
        hash.addData(file.read(8192));
    }
    file.close();
    return hash.result().toHex();
}
