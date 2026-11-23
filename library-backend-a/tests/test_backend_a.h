/*
 * test_backend_a.h —— 后端A QTest 测试套件（类声明）
 * 说明：类声明独立成头文件并登记进 tests.pro 的 HEADERS，
 *       保证 qmake 在源码内构建与 Qt Creator shadow build 下都能正确生成 moc。
 */
#pragma once
#include <QObject>
#include "common/contract.h"

class TestBackendA : public QObject {
    Q_OBJECT

private slots:
    void init();            // 每个用例前重建内存库
    void cleanup() {}

    // ---------- BookService ----------
    void bookAddValidation();
    void bookAddGetWithStats();
    void bookUpdate();
    void bookListPaginationAndFilters();
    void bookShelveOffAndDelete();

    // ---------- CopyService ----------
    void copyAddValidation();
    void copyStateMachineLegalPaths();
    void copyStateMachineIllegalPaths();
    void copyMaintenanceLostAdminOnly();
    void copyRulesOnShelvedBook();
    void copyListAndUpdate();

    // ---------- SearchService ----------
    void searchKeyword();

    // ---------- AdvancedSearchService ----------
    void advancedAndOrLogic();
    void advancedFilters();
    void advancedSorting();
    void advancedEdgeCases();

    // ---------- CategoryService ----------
    void categoryCrud();

    // ---------- ImportExportService ----------
    void importCsvBooksErrorRows();
    void importCsvCopies();
    void importEmptyAndBadFormat();
    void exportImportRoundTripCsv();
    void exportImportRoundTripXlsx();
    void exportLargeFile();

private:
    lib::OperatorContext admin()  { return { 1, "admin",     lib::Role::Admin,     true }; }
    lib::OperatorContext libOp()  { return { 2, "librarian", lib::Role::Librarian, true }; }
    lib::OperatorContext reader() { return { 3, "reader",    lib::Role::Reader,    true }; }
    lib::OperatorContext anon()   { return { 0, QString(),   lib::Role::Reader,    false }; }

    // 造数据：返回 (category_id, book_id)
    qint64 mkCategory(const QString& name);
    qint64 mkBook(qint64 catId, const QString& isbn, const QString& title,
                  const QString& author = QString(), const QString& publisher = QString(),
                  const QString& date = QString());
    qint64 mkCopy(qint64 bookId, const QString& barcode, const QString& location = QString());
    void insertReaderAndBorrow(qint64 bookId); // 直插 B/C 表结构数据，用于删除校验
};
