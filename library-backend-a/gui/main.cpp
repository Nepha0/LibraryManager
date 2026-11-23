#include "mainwindow.h"
#include "common/contract.h"
#include "database/DatabaseManager.h"
#include "services/BookService.h"
#include "services/CategoryService.h"
#include "services/CopyService.h"

#include <QApplication>
#include <QDate>
#include <QSqlQuery>
#include <QVariantMap>

using namespace lib;

// 首次启动时灌入演示数据，保证打开窗口就能看到状态机各状态的副本
static void seedDemoData()
{
    QSqlQuery q(DatabaseManager::instance().db());
    q.exec(QStringLiteral("SELECT COUNT(*) FROM books"));
    q.next();
    if (q.value(0).toInt() > 0)
        return;

    OperatorContext op;
    op.userId = 1;
    op.userName = QStringLiteral("演示管理员");
    op.role = Role::Admin;
    op.authenticated = true;

    CategoryService cs(DatabaseManager::instance().db());
    const qint64 catCs = cs.addCategory(op, QVariantMap {
        { "name", QStringLiteral("计算机") }, { "description", QStringLiteral("编程与技术") }
    }).data.value(QStringLiteral("category_id")).toLongLong();
    const qint64 catLit = cs.addCategory(op, QVariantMap {
        { "name", QStringLiteral("文学") }
    }).data.value(QStringLiteral("category_id")).toLongLong();

    BookService bs(DatabaseManager::instance().db());
    const qint64 b1 = bs.addBook(op, QVariantMap {
        { "isbn", "978-7-111-11111-1" }, { "title", QStringLiteral("C++ Primer（第5版）") },
        { "author", QStringLiteral("Lippman") }, { "publisher", QStringLiteral("机械工业出版社") },
        { "publish_date", "2013-09-01" }, { "category_id", catCs } })
        .data.value(QStringLiteral("book_id")).toLongLong();
    const qint64 b2 = bs.addBook(op, QVariantMap {
        { "isbn", "978-7-111-22222-2" }, { "title", QStringLiteral("算法导论") },
        { "author", QStringLiteral("Cormen") }, { "publisher", QStringLiteral("机械工业出版社") },
        { "publish_date", "2013-01-01" }, { "category_id", catCs } })
        .data.value(QStringLiteral("book_id")).toLongLong();
    bs.addBook(op, QVariantMap {
        { "isbn", "978-7-02-33333-3" }, { "title", QStringLiteral("平凡的世界") },
        { "author", QStringLiteral("路遥") }, { "publisher", QStringLiteral("人民文学出版社") },
        { "publish_date", "2005-01-01" }, { "category_id", catLit } });

    CopyService cps(DatabaseManager::instance().db());
    cps.addCopy(op, b1, "B0001", QStringLiteral("东馆一楼"));
    cps.addCopy(op, b1, "B0002", QStringLiteral("西馆二楼"));
    cps.addCopy(op, b2, "B0003", QStringLiteral("东馆一楼"));
    cps.addCopy(op, b2, "B0004", QStringLiteral("东馆一楼"));
    // 制造状态机多样性：借出 1 本、维修中 1 本、预约保留 1 本
    cps.changeCopyStatus(op, 2, "BORROWED");
    cps.changeCopyStatus(op, 3, "MAINTENANCE");
    cps.changeCopyStatus(op, 4, "RESERVED");
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("backend_a_gui"));

    auto& mgr = DatabaseManager::instance();
    if (!mgr.open(QStringLiteral("gui_demo_library.db"))) {
        qFatal("打开数据库失败");
        return 1;
    }
    if (!mgr.initSchema()) {
        qFatal("初始化表结构失败");
        return 1;
    }
    seedDemoData();

    lib::MainWindow w;
    w.show();
    return app.exec();
}
