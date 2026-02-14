#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QMessageBox>
#include <QPushButton>
#include <QTcpSocket.h>
#include <warning.h>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow),
    socket(new QTcpSocket(parent)) {
    ui->setupUi(this);

    // When the program starts, the login page is displayed.
    ui->stackedWidget->setCurrentWidget(ui->loginPage);

    // login button
    connect(ui->logBtn, &QPushButton::clicked, this, &MainWindow::onLoginClicked);

    // connect
    connect(socket, &QTcpSocket::connected, this, [this]() {
        qDebug() << "连接成功";
        // change to homePage
        ui->stackedWidget->setCurrentWidget(ui->homePage);
    });
    connect(socket, &QTcpSocket::errorOccurred, this, [this](auto) {
        QMessageBox::warning(this, "警告", "链接服务器失败");
        qDebug() << "[错误详情]：" << socket->errorString();
        ui->stackedWidget->setCurrentWidget(ui->loginPage);
    });

    // send
    connect(ui->sendButton, &QPushButton::clicked, this, &MainWindow::onSendBtnClicked);

    // recv
    connect(socket, &QTcpSocket::readyRead, this, [this]() {
        while (socket->canReadLine()) {
            QString msg = QString::fromUtf8(socket->readLine()).trimmed();
            msg = "from Server:" + msg;
            addServerMessage(msg);   // 显示在左边
        }
    });
}

MainWindow::~MainWindow() { delete ui; }

void MainWindow::onLoginClicked() {
    if (ui->ipEdit->text().isEmpty()) {
        QMessageBox::warning(this, "警告", "ip地址不能为空");
        return;
    }

    if (ui->portEdit->text().isEmpty()) {
        QMessageBox::warning(this, "警告", "端口号不能为空");
        return;
    }

    // login
    socket->connectToHost(ui->ipEdit->text(), ui->portEdit->text().toShort());
}

void MainWindow::addMyMessage(const QString &text) {
    QListWidgetItem *item = new QListWidgetItem(text);
    item->setTextAlignment(Qt::AlignRight);
    item->setBackground(QColor("#9EEA6A")); // QQ绿
    ui->chatWidget->addItem(item);
    ui->chatWidget->scrollToBottom();
}

void MainWindow::addServerMessage(const QString &text) {
    QListWidgetItem *item = new QListWidgetItem(text);
    item->setTextAlignment(Qt::AlignLeft);
    item->setBackground(Qt::white);
    ui->chatWidget->addItem(item);

    ui->chatWidget->scrollToBottom();
}

void MainWindow::onSendBtnClicked() {
    QString text = ui->inputEdit->toPlainText().trimmed();
    if (text.isEmpty())
        return;

    addMyMessage(text);                  // 显示在右边
    socket->write(text.toUtf8() + "\n"); // 发给服务器
    ui->inputEdit->clear();
}
