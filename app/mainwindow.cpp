#include "mainwindow.h"
#include <QLabel>
#include <QListWidget>
#include <QStackedWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("图书管理系统 · 开发骨架"));
    resize(1050, 680);
    auto *root = new QWidget(this);
    auto *layout = new QHBoxLayout(root);
    auto *navigation = new QListWidget(root);
    navigation->setMaximumWidth(200);
    auto *pages = new QStackedWidget(root);
    const QStringList titles = {
        QStringLiteral("登录与权限 · A"), QStringLiteral("图书与库存 · B"),
        QStringLiteral("借阅与预约 · C"), QStringLiteral("读者管理 · D"),
        QStringLiteral("统计与导出 · D")
    };
    for (const auto &title : titles) {
        navigation->addItem(title);
        auto *page = new QWidget(pages);
        auto *pageLayout = new QVBoxLayout(page);
        auto *heading = new QLabel(title, page);
        auto *hint = new QLabel(QStringLiteral("此页面是开发占位，尚未实现业务。请在对应模块中添加界面和服务，再接入主窗口。"), page);
        hint->setWordWrap(true);
        pageLayout->addWidget(heading);
        pageLayout->addWidget(hint);
        pageLayout->addStretch();
        pages->addWidget(page);
    }
    connect(navigation, &QListWidget::currentRowChanged,
            pages, &QStackedWidget::setCurrentIndex);
    navigation->setCurrentRow(0);
    layout->addWidget(navigation);
    layout->addWidget(pages, 1);
    setCentralWidget(root);
}
