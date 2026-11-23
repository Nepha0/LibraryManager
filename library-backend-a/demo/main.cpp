/*
 * backend_a_demo —— 后端A 控制台演示
 * 演示"建库 → 分类/图书/副本录入 → 检索 → 状态机流转 → 导入导出"核心场景，
 * 供前端联调前的人工验收。运行后会在工作目录生成 demo_library.db 与 demo_out/ 下的导出文件。
 */
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>

#include "database/DatabaseManager.h"
#include "services/BookService.h"
#include "services/CategoryService.h"
#include "services/CopyService.h"
#include "services/SearchService.h"
#include "services/AdvancedSearchService.h"
#include "services/ImportExportService.h"

using namespace lib;

static OperatorContext makeAdmin()
{
    OperatorContext op;
    op.userId = 1;
    op.userName = QStringLiteral("admin");
    op.role = Role::Admin;
    op.authenticated = true;
    return op;
}

static void printResult(const QString& tag, const Result& r)
{
    qDebug().noquote() << QStringLiteral("[%1] code=%2 %3").arg(tag).arg(int(r.code)).arg(r.message);
    if (!r.data.isEmpty())
        qDebug().noquote() << "    data:" << QJsonDocument(QJsonObject::fromVariantMap(r.data)).toJson(QJsonDocument::Compact);
    for (const QVariant& row : r.list)
        qDebug().noquote() << "    row:" << QJsonDocument(QJsonObject::fromVariantMap(row.toMap())).toJson(QJsonDocument::Compact);
    if (r.total > 0)
        qDebug().noquote() << QStringLiteral("    total=%1").arg(r.total);
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    const QString dbPath = QStringLiteral("demo_library.db");
    QFile::remove(dbPath); // 演示库每次重建
    DatabaseManager::instance().open(dbPath);
    QString schemaErr;
    if (!DatabaseManager::instance().initSchema(&schemaErr)) {
        qCritical().noquote() << "建库失败:" << schemaErr;
        return 1;
    }
    qDebug().noquote() << "== 1. 数据库初始化完成 ==";

    QSqlDatabase db = DatabaseManager::instance().db();
    const OperatorContext admin = makeAdmin();

    CategoryService categoryService(db);
    BookService bookService(db);
    CopyService copyService(db);
    SearchService searchService(db);
    AdvancedSearchService advancedSearchService(db);
    ImportExportService importExportService(db);

    qDebug().noquote() << "== 2. 分类与图书录入 ==";
    Result r = categoryService.addCategory(admin, QVariantMap { { "name", "计算机" }, { "description", "计算机科学类" } });
    printResult("分类新增", r);
    const qint64 catId = r.data.value(QStringLiteral("category_id")).toLongLong();
    r = categoryService.addCategory(admin, QVariantMap { { "name", "数学" } });
    printResult("分类新增", r);
    const qint64 catMathId = r.data.value(QStringLiteral("category_id")).toLongLong();

    r = bookService.addBook(admin, QVariantMap {
        { "isbn", "9787111213828" }, { "title", "C++ Primer 中文版" },
        { "author", "Stanley B. Lippman" }, { "publisher", "人民邮电出版社" },
        { "publish_date", "2013-09-01" }, { "category_id", catId },
        { "intro", "C++ 经典入门书" } });
    printResult("图书新增", r);
    const qint64 bookCppId = r.data.value(QStringLiteral("book_id")).toLongLong();

    r = bookService.addBook(admin, QVariantMap {
        { "isbn", "9787115428029" }, { "title", "算法导论" },
        { "author", "Thomas H. Cormen" }, { "publisher", "人民邮电出版社" },
        { "publish_date", "2012-12-01" }, { "category_id", catId } });
    printResult("图书新增", r);
    const qint64 bookAlgoId = r.data.value(QStringLiteral("book_id")).toLongLong();

    r = bookService.addBook(admin, QVariantMap {
        { "isbn", "9787030110929" }, { "title", "具体数学" },
        { "author", "Ronald L. Graham" }, { "publisher", "科学出版社" },
        { "publish_date", "2013-01-01" }, { "category_id", catMathId } });
    printResult("图书新增", r);
    const qint64 bookMathId = r.data.value(QStringLiteral("book_id")).toLongLong();

    qDebug().noquote() << "== 3. 副本录入 ==";
    r = copyService.addCopy(admin, bookCppId, "CP-0001", "A区-1排", QDate(2024, 3, 1));
    printResult("副本新增", r);
    copyService.addCopy(admin, bookCppId, "CP-0002", "A区-1排", QDate(2024, 3, 1));
    copyService.addCopy(admin, bookAlgoId, "AL-0001", "B区-2排", QDate(2024, 4, 10));
    copyService.addCopy(admin, bookMathId, "MA-0001", "C区-3排", QDate(2024, 5, 20));

    qDebug().noquote() << "== 4. 图书详情（含副本统计） ==";
    printResult("图书详情", bookService.getBook(admin, bookCppId));

    qDebug().noquote() << "== 5. 关键词检索 ==";
    printResult("关键词检索[C++]", searchService.search(admin, "C++", 1, 10));
    printResult("关键词检索[空]", searchService.search(admin, " ", 1, 10));

    qDebug().noquote() << "== 6. 多条件高级检索（分类=计算机 且 书名含'算法'） ==";
    QVariantMap cond;
    cond.insert(QStringLiteral("titles"), QStringList { QStringLiteral("算法") });
    cond.insert(QStringLiteral("categoryIds"), QVariantList { catId });
    cond.insert(QStringLiteral("copyStatus"), QStringLiteral("AVAILABLE"));
    printResult("高级检索", advancedSearchService.advancedSearch(admin, cond, QStringLiteral("title"), true, 1, 10));

    qDebug().noquote() << "== 7. 副本状态机流转 ==";
    printResult("在馆可借→借出", copyService.changeCopyStatus(admin, 1, "BORROWED"));
    printResult("借出→预约保留", copyService.changeCopyStatus(admin, 1, "RESERVED"));
    printResult("非法：借出→维修中(不合法)", copyService.changeCopyStatus(admin, 1, "MAINTENANCE"));
    printResult("预约保留→在馆可借", copyService.changeCopyStatus(admin, 1, "AVAILABLE"));

    qDebug().noquote() << "== 8. 下架与删除校验 ==";
    r = bookService.shelveOff(admin, bookMathId);
    printResult("下架", r);
    r = copyService.addCopy(admin, bookMathId, "MA-0002", "C区");
    printResult("下架书新增副本(应被拒)", r);
    r = copyService.changeCopyStatus(admin, 3, "BORROWED"); // AL-0001，未下架
    printResult("在架书副本借出", r);
    r = copyService.changeCopyStatus(admin, 4, "BORROWED"); // MA-0001 所属书已下架
    printResult("下架书副本借出(应被拒)", r);

    qDebug().noquote() << "== 9. 导出 / 导入演示 ==";
    QDir().mkpath(QStringLiteral("demo_out"));
    const QString bookCsv = QStringLiteral("demo_out/books_export.csv");
    r = importExportService.exportBooks(admin, bookCsv);
    printResult("图书导出CSV", r);
    r = importExportService.exportCopies(admin, QStringLiteral("demo_out/copies_export.xlsx"));
    printResult("副本导出XLSX", r);

    // 用一份含错误行的 CSV 演示逐行校验
    {
        QFile f(QStringLiteral("demo_out/books_import_demo.csv"));
        f.open(QIODevice::WriteOnly | QIODevice::Text);
        QTextStream ts(&f);
        ts.setEncoding(QStringConverter::Utf8);
        ts << "ISBN,书名,作者,出版社,出版日期,分类,简介\n";
        ts << "9787111111111,错误样例·缺分类,,人民邮电出版社,2020-01-01,,\n";
        ts << "9787112222222,坏日期样例,某人,某出版社,2020/05/01,数学,\n";
        ts << "9787113333333,合法样例书,某人,某出版社,2020-06-01,数学,\n";
        ts << "9787111213828,ISBN重复样例,某人,某出版社,2020-06-01,计算机,\n";
        f.close();
        r = importExportService.importBooks(admin, QStringLiteral("demo_out/books_import_demo.csv"));
        printResult("图书导入(含错误行)", r);
        for (const QVariant& err : r.list)
            qDebug().noquote() << QStringLiteral("    错误行%1: %2")
                .arg(err.toMap().value(QStringLiteral("row")).toInt())
                .arg(err.toMap().value(QStringLiteral("reason")).toString());
    }

    qDebug().noquote() << "== 演示结束，数据库文件：" << dbPath << "==";
    DatabaseManager::instance().close();
    return 0;
}
