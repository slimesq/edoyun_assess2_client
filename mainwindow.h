#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "message.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class QTcpSocket;
class QFile;
class QProgressDialog;
class QListWidgetItem;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void addMyMessage(const QString& text);
    void addServerMessage(const QString& text);
    void addSystemMessage(const QString& text);

private slots:
    void onLoginClicked();
    void onSendBtnClicked();
    void onUploadClicked();
    void onTransferCanceled();
    void onChatItemClicked(QListWidgetItem* item);

private:
    Ui::MainWindow *ui;
    QTcpSocket* socket;
    QByteArray recvBuffer;

    // 用户名
    QString username;

    // 文件上传相关
    QFile* uploadFile;
    QString uploadFileName;
    QString uploadSha1;
    quint64 uploadTotal;
    quint64 uploadSent;

    // 文件下载相关
    QFile* downloadFile;
    QString downloadSha1;
    quint64 downloadTotal;
    quint64 downloadReceived;

    QProgressDialog* progressDialog;

    // 发送/接收
    void sendTrain(const Train& train);
    void processRecvBuffer();
    void handleTrain(const Train& train);

    // 文件操作
    void startUpload(const QString& filePath);
    void sendNextChunk();
    void cleanupUpload();

    void cleanupDownload();
    void startDownloadBySha1(const QString& sha1, const QString& suggestedName, quint64 fileSize);

    void addFileMessage(const QString& sha1, const QString& fileName, quint64 fileSize, bool isMine);

    static QString calcFileSha1(const QString& filePath);

    // 文件消息标记前缀
    static const QString FILE_MSG_PREFIX;

    static const qint64 CHUNK_SIZE = 4096;
};
#endif // MAINWINDOW_H
