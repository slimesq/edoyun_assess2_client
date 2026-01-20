#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QPushButton>
#include <warning.h>
#include <QMessageBox>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // When the program starts, the login page is displayed.
    ui->stackedWidget->setCurrentWidget(ui->loginPage);

    // login button
    connect(ui->logBtn,&QPushButton::clicked,this,&MainWindow::onLoginClicked);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::onLoginClicked()
{
    if (ui->ipEdit->text().isEmpty()) {
        QMessageBox::warning(this, "警告", "ip地址不能为空");
        return;
    }

    if (ui->portEdit->text().isEmpty()) {
        QMessageBox::warning(this, "警告", "端口号不能为空");
        return;
    }


    // change to homePage
   ui->stackedWidget->setCurrentWidget(ui->homePage);
}
