#include "ImportExportService.h"
#include "xlsxdocument.h"
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QSet>
#include <QSqlQuery>
#include "../database/DatabaseManager.h"
#include <QSqlError>
#include <QDate>
#include <QHash>
#include <QStringDecoder>
#include <memory>

namespace lib {

ImportExportService::ImportExportService(QSqlDatabase db) : m_db(db) {}

namespace {

// ===================== CSV 解析（RFC4180：支持引号内逗号/换行/转义） =====================
QVector<QStringList> parseCsvText(const QString& text, bool* ok)
{
    QVector<QStringList> records;
    QStringList record;
    QString field;
    bool inQuotes = false;
    bool fieldStarted = false; // 用于区分空行与空字段

    const int n = text.size();
    auto endField = [&]() {
        record << field;
        field.clear();
        fieldStarted = false;
    };
    auto endRecord = [&]() {
        endField();
        // 纯空行且无内容时跳过
        if (!(record.size() == 1 && record.at(0).isEmpty() && records.isEmpty() && field.isEmpty()))
            records << record;
        record.clear();
    };

    for (int i = 0; i < n; ++i) {
        const QChar c = text.at(i);
        if (inQuotes) {
            if (c == QLatin1Char('"')) {
                // 双引号转义 ""
                if (i + 1 < n && text.at(i + 1) == QLatin1Char('"')) {
                    field += QLatin1Char('"');
                    ++i;
                } else {
                    inQuotes = false;
                }
            } else {
                field += c;
            }
        } else if (c == QLatin1Char('"') && field.isEmpty() && !fieldStarted) {
            inQuotes = true;
            fieldStarted = true;
        } else if (c == QLatin1Char(',')) {
            endField();
        } else if (c == QLatin1Char('\n') || c == QLatin1Char('\r')) {
            if (c == QLatin1Char('\r') && i + 1 < n && text.at(i + 1) == QLatin1Char('\n'))
                ++i;
            endRecord();
        } else {
            field += c;
            fieldStarted = true;
        }
    }
    if (inQuotes) { // 引号未闭合
        if (ok) *ok = false;
        return records;
    }
    if (fieldStarted || !field.isEmpty() || !record.isEmpty())
        endRecord();
    if (ok) *ok = true;
    return records;
}

// 编码探测：优先 UTF-8（含 BOM），出现替换符时尝试 GBK
QString decodeBytes(const QByteArray& raw)
{
    QByteArray data = raw;
    if (data.startsWith(QByteArray("\xEF\xBB\xBF")))
        data.remove(0, 3);
    QString utf8 = QString::fromUtf8(data);
    if (!utf8.contains(QChar(0xFFFD)))
        return utf8;
    // 无 ICU 时 GBK 解码器不可用（isValid 为 false），此时回退 UTF-8 结果
    QStringDecoder gbk(QStringLiteral("GBK"));
    if (gbk.isValid())
        return gbk.decode(data);
    return utf8;
}

// 验证日期字段：空串合法（可选字段），非空必须是 yyyy-MM-dd
QString validateDateCell(const QString& cell, const QString& fieldName, QString* valueOut)
{
    const QString v = cell.trimmed();
    if (!v.isEmpty() && QDate::fromString(v, QStringLiteral("yyyy-MM-dd")).isNull()) {
        return QStringLiteral("%1格式应为 yyyy-MM-dd（当前值：%2）").arg(fieldName, v);
    }
    if (valueOut) *valueOut = v;
    return QString();
}

} // namespace

// ===================== 表头映射 =====================
QString ImportExportService::mapHeader(const QStringList& header, bool bookMode,
                                       QHash<QString, int>* colIndex)
{
    colIndex->clear();
    const QStringList required = bookMode
        ? QStringList { QStringLiteral("ISBN"), QStringLiteral("书名"), QStringLiteral("分类") }
        : QStringList { QStringLiteral("条码"), QStringLiteral("ISBN") };
    const QStringList optional = bookMode
        ? QStringList { QStringLiteral("作者"), QStringLiteral("出版社"), QStringLiteral("出版日期"),
                        QStringLiteral("简介"), QStringLiteral("封面路径") }
        : QStringList { QStringLiteral("馆藏位置"), QStringLiteral("入藏日期") };

    for (int i = 0; i < header.size(); ++i)
        colIndex->insert(header.at(i).trimmed(), i);

    QStringList missing;
    for (const QString& col : required)
        if (!colIndex->contains(col))
            missing << col;
    if (!missing.isEmpty())
        return QStringLiteral("缺少必需列：%1").arg(missing.join(QStringLiteral("、")));
    for (const QString& col : optional)
        if (!colIndex->contains(col))
            colIndex->insert(col, -1); // 可选列缺失 → 跳过读取
    return QString();
}

// ===================== CSV 导入公共实现 =====================
Result ImportExportService::importCsv(const OperatorContext& op, const QString& filePath, bool bookMode)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return Result::fail(ErrorCode::FileError,
                            QStringLiteral("无法打开文件：%1").arg(filePath));
    const QString text = decodeBytes(file.readAll());
    file.close();
    if (text.trimmed().isEmpty())
        return Result::fail(ErrorCode::FormatError, QStringLiteral("文件为空"));

    bool parseOk = false;
    const QVector<QStringList> records = parseCsvText(text, &parseOk);
    if (!parseOk || records.isEmpty())
        return Result::fail(ErrorCode::FormatError, QStringLiteral("CSV 格式错误（引号未闭合或缺少表头）"));

    QHash<QString, int> cols;
    const QString headerErr = mapHeader(records.first(), bookMode, &cols);
    if (!headerErr.isEmpty())
        return Result::fail(ErrorCode::FormatError, headerErr);

    Result r = Result::success();
    int importedRows = 0;
    QSet<QString> seenKeys; // 文件内 ISBN / 条码去重

    QSqlQuery q(m_db);
    if (!m_db.transaction())
        return Result::fail(ErrorCode::DbError, QStringLiteral("事务开启失败"));

    for (int rowNo = 2; rowNo <= records.size(); ++rowNo) { // 数据行从第 2 行开始（行号含表头）
        const QStringList cells = records.at(rowNo - 1);
        // 全空行跳过（不产生错误行）
        bool rowEmpty = true;
        for (const QString& c : cells)
            if (!c.trimmed().isEmpty()) { rowEmpty = false; break; }
        if (rowEmpty) continue;
        auto cell = [&](const QString& col) -> QString {
            const int idx = cols.value(col, -1);
            return (idx >= 0 && idx < cells.size()) ? cells.at(idx).trimmed() : QString();
        };

        if (bookMode) {
            const QString isbn = cell(QStringLiteral("ISBN"));
            const QString title = cell(QStringLiteral("书名"));
            const QString categoryName = cell(QStringLiteral("分类"));
            // 逐项校验
            if (isbn.isEmpty()) {
                r.list << QVariantMap { { QStringLiteral("row"), rowNo },
                                        { QStringLiteral("reason"), QStringLiteral("ISBN 为空") } };
                continue;
            }
            if (title.isEmpty()) {
                r.list << QVariantMap { { QStringLiteral("row"), rowNo },
                                        { QStringLiteral("reason"), QStringLiteral("书名为空") } };
                continue;
            }
            if (categoryName.isEmpty()) {
                r.list << QVariantMap { { QStringLiteral("row"), rowNo },
                                        { QStringLiteral("reason"), QStringLiteral("分类为空") } };
                continue;
            }
            QString dateVal;
            const QString dateErr = validateDateCell(cell(QStringLiteral("出版日期")),
                                                     QStringLiteral("出版日期"), &dateVal);
            if (!dateErr.isEmpty()) {
                r.list << QVariantMap { { QStringLiteral("row"), rowNo },
                                        { QStringLiteral("reason"), dateErr } };
                continue;
            }
            if (seenKeys.contains(isbn)) {
                r.list << QVariantMap { { QStringLiteral("row"), rowNo },
                                        { QStringLiteral("reason"),
                                          QStringLiteral("ISBN %1 在文件中重复").arg(isbn) } };
                continue;
            }
            // 分类必须存在（外键校验）
            q.prepare(QStringLiteral("SELECT id FROM categories WHERE name = ?"));
            q.addBindValue(categoryName);
            if (!q.exec() || !q.next()) {
                r.list << QVariantMap { { QStringLiteral("row"), rowNo },
                                        { QStringLiteral("reason"),
                                          QStringLiteral("分类「%1」不存在").arg(categoryName) } };
                continue;
            }
            const qint64 categoryId = q.value(0).toLongLong();
            // ISBN 在库重复（ACTIVE 范围）
            q.prepare(QStringLiteral("SELECT COUNT(*) FROM books WHERE isbn = ?"));
            q.addBindValue(isbn);
            if (!q.exec() || !q.next()) {
                m_db.rollback();
                return Result::fail(ErrorCode::DbError, QStringLiteral("ISBN 查重失败"));
            }
            if (q.value(0).toInt() > 0) {
                r.list << QVariantMap { { QStringLiteral("row"), rowNo },
                                        { QStringLiteral("reason"),
                                          QStringLiteral("ISBN %1 已存在").arg(isbn) } };
                continue;
            }
            // 合法行入库
            q.prepare(QStringLiteral(
                "INSERT INTO books (isbn, title, author, publisher, publish_date,"
                " category_id, intro, cover_path, status)"
                " VALUES (?,?,?,?,?,?,?,?,'active')"));
            q.addBindValue(isbn);
            q.addBindValue(title);
            q.addBindValue(cell(QStringLiteral("作者")));
            q.addBindValue(cell(QStringLiteral("出版社")));
            q.addBindValue(dateVal);
            q.addBindValue(categoryId);
            q.addBindValue(cell(QStringLiteral("简介")));
            q.addBindValue(cell(QStringLiteral("封面路径")));
            if (!q.exec()) {
                m_db.rollback();
                return Result::fail(ErrorCode::DbError,
                                    QStringLiteral("导入图书失败(行 %1): %2").arg(rowNo).arg(q.lastError().text()));
            }
            seenKeys.insert(isbn);
            ++importedRows;
        } else {
            // ---- 副本导入 ----
            const QString barcode = cell(QStringLiteral("条码"));
            const QString isbn = cell(QStringLiteral("ISBN"));
            if (barcode.isEmpty()) {
                r.list << QVariantMap { { QStringLiteral("row"), rowNo },
                                        { QStringLiteral("reason"), QStringLiteral("条码为空") } };
                continue;
            }
            if (isbn.isEmpty()) {
                r.list << QVariantMap { { QStringLiteral("row"), rowNo },
                                        { QStringLiteral("reason"), QStringLiteral("ISBN 为空") } };
                continue;
            }
            QString dateVal;
            const QString dateErr = validateDateCell(cell(QStringLiteral("入藏日期")),
                                                     QStringLiteral("入藏日期"), &dateVal);
            if (!dateErr.isEmpty()) {
                r.list << QVariantMap { { QStringLiteral("row"), rowNo },
                                        { QStringLiteral("reason"), dateErr } };
                continue;
            }
            if (seenKeys.contains(barcode)) {
                r.list << QVariantMap { { QStringLiteral("row"), rowNo },
                                        { QStringLiteral("reason"),
                                          QStringLiteral("条码 %1 在文件中重复").arg(barcode) } };
                continue;
            }
            q.prepare(QStringLiteral("SELECT id, status FROM books WHERE isbn = ?"));
            q.addBindValue(isbn);
            if (!q.exec() || !q.next()) {
                r.list << QVariantMap { { QStringLiteral("row"), rowNo },
                                        { QStringLiteral("reason"),
                                          QStringLiteral("ISBN %1 对应图书不存在").arg(isbn) } };
                continue;
            }
            if (q.value(QStringLiteral("status")).toString() == QStringLiteral("shelved_off")) {
                r.list << QVariantMap { { QStringLiteral("row"), rowNo },
                                        { QStringLiteral("reason"),
                                          QStringLiteral("ISBN %1 对应图书已下架").arg(isbn) } };
                continue;
            }
            const qint64 bookId = q.value(0).toLongLong();
            q.prepare(QStringLiteral("SELECT COUNT(*) FROM book_copies WHERE copy_code = ?"));
            q.addBindValue(barcode);
            if (!q.exec() || !q.next()) {
                m_db.rollback();
                return Result::fail(ErrorCode::DbError, QStringLiteral("条码查重失败"));
            }
            if (q.value(0).toInt() > 0) {
                r.list << QVariantMap { { QStringLiteral("row"), rowNo },
                                        { QStringLiteral("reason"),
                                          QStringLiteral("条码 %1 已存在").arg(barcode) } };
                continue;
            }
            q.prepare(QStringLiteral(
                "INSERT INTO book_copies (copy_code, book_id, location, status, acquired_date)"
                " VALUES (?,?,?,'available',?)"));
            q.addBindValue(barcode);
            q.addBindValue(bookId);
            q.addBindValue(cell(QStringLiteral("馆藏位置")));
            q.addBindValue(dateVal);
            if (!q.exec()) {
                m_db.rollback();
                return Result::fail(ErrorCode::DbError,
                                    QStringLiteral("导入副本失败(行 %1): %2").arg(rowNo).arg(q.lastError().text()));
            }
            seenKeys.insert(barcode);
            ++importedRows;
        }
    }
    if (!m_db.commit()) {
        m_db.rollback();
        return Result::fail(ErrorCode::DbError, QStringLiteral("导入事务提交失败"));
    }

    r.data.insert(QStringLiteral("total_rows"), records.size() - 1);
    r.data.insert(QStringLiteral("imported_rows"), importedRows);
    r.data.insert(QStringLiteral("error_rows"), r.list.size());
    r.message = QStringLiteral("导入完成：共 %1 行，成功 %2 行，失败 %3 行")
                    .arg(records.size() - 1).arg(importedRows).arg(r.list.size());
    return r;
}

// ===================== XLSX 导入公共实现 =====================
Result ImportExportService::importXlsx(const OperatorContext& op, const QString& filePath, bool bookMode)
{
    QXlsx::Document doc(filePath);
    if (!doc.load())
        return Result::fail(ErrorCode::FormatError, QStringLiteral("XLSX 文件为空或无法读取"));
    const QXlsx::CellRange dim = doc.dimension();
    const int lastRow = dim.lastRow();
    const int lastCol = dim.lastColumn();
    if (lastRow < 1 || lastCol < 1)
        return Result::fail(ErrorCode::FormatError, QStringLiteral("XLSX 文件为空"));

    // 表头
    QStringList header;
    for (int c = 1; c <= lastCol; ++c)
        header << doc.read(1, c).toString();
    bool anyHeader = false;
    for (const QString& h : header)
        if (!h.trimmed().isEmpty()) { anyHeader = true; break; }
    if (!anyHeader)
        return Result::fail(ErrorCode::FormatError, QStringLiteral("XLSX 文件为空（缺少表头）"));

    QHash<QString, int> cols;
    const QString headerErr = mapHeader(header, bookMode, &cols);
    if (!headerErr.isEmpty())
        return Result::fail(ErrorCode::FormatError, headerErr);

    Result r = Result::success();
    int importedRows = 0;
    QSet<QString> seenKeys;
    QSqlQuery q(m_db);
    if (!m_db.transaction())
        return Result::fail(ErrorCode::DbError, QStringLiteral("事务开启失败"));

    for (int rowNo = 2; rowNo <= lastRow; ++rowNo) {
        // 整行读出
        QStringList cells;
        for (int c = 1; c <= lastCol; ++c)
            cells << doc.read(rowNo, c).toString().trimmed();
        auto cell = [&](const QString& col) -> QString {
            const int idx = cols.value(col, -1);
            return (idx >= 0 && idx < cells.size()) ? cells.at(idx) : QString();
        };
        bool rowEmpty = true;
        for (const QString& s : cells)
            if (!s.isEmpty()) { rowEmpty = false; break; }
        if (rowEmpty) continue; // 跳过空行

        if (bookMode) {
            const QString isbn = cell(QStringLiteral("ISBN"));
            const QString title = cell(QStringLiteral("书名"));
            const QString categoryName = cell(QStringLiteral("分类"));
            if (isbn.isEmpty()) { r.list << QVariantMap { { "row", rowNo }, { "reason", "ISBN 为空" } }; continue; }
            if (title.isEmpty()) { r.list << QVariantMap { { "row", rowNo }, { "reason", "书名为空" } }; continue; }
            if (categoryName.isEmpty()) { r.list << QVariantMap { { "row", rowNo }, { "reason", "分类为空" } }; continue; }
            QString dateVal;
            const QString dateErr = validateDateCell(cell(QStringLiteral("出版日期")),
                                                     QStringLiteral("出版日期"), &dateVal);
            if (!dateErr.isEmpty()) { r.list << QVariantMap { { "row", rowNo }, { "reason", dateErr } }; continue; }
            if (seenKeys.contains(isbn)) {
                r.list << QVariantMap { { "row", rowNo },
                                        { "reason", QStringLiteral("ISBN %1 在文件中重复").arg(isbn) } };
                continue;
            }
            q.prepare(QStringLiteral("SELECT id FROM categories WHERE name = ?"));
            q.addBindValue(categoryName);
            if (!q.exec() || !q.next()) {
                r.list << QVariantMap { { "row", rowNo },
                                        { "reason", QStringLiteral("分类「%1」不存在").arg(categoryName) } };
                continue;
            }
            const qint64 categoryId = q.value(0).toLongLong();
            q.prepare(QStringLiteral("SELECT COUNT(*) FROM books WHERE isbn = ?"));
            q.addBindValue(isbn);
            if (!q.exec() || !q.next()) { m_db.rollback(); return Result::fail(ErrorCode::DbError, QStringLiteral("ISBN 查重失败")); }
            if (q.value(0).toInt() > 0) {
                r.list << QVariantMap { { "row", rowNo },
                                        { "reason", QStringLiteral("ISBN %1 已存在").arg(isbn) } };
                continue;
            }
            q.prepare(QStringLiteral(
                "INSERT INTO books (isbn, title, author, publisher, publish_date,"
                " category_id, intro, cover_path, status) VALUES (?,?,?,?,?,?,?,?,'active')"));
            q.addBindValue(isbn);
            q.addBindValue(title);
            q.addBindValue(cell(QStringLiteral("作者")));
            q.addBindValue(cell(QStringLiteral("出版社")));
            q.addBindValue(dateVal);
            q.addBindValue(categoryId);
            q.addBindValue(cell(QStringLiteral("简介")));
            q.addBindValue(cell(QStringLiteral("封面路径")));
            if (!q.exec()) { m_db.rollback(); return Result::fail(ErrorCode::DbError, QStringLiteral("导入图书失败(行 %1)").arg(rowNo)); }
            seenKeys.insert(isbn);
            ++importedRows;
        } else {
            const QString barcode = cell(QStringLiteral("条码"));
            const QString isbn = cell(QStringLiteral("ISBN"));
            if (barcode.isEmpty()) { r.list << QVariantMap { { "row", rowNo }, { "reason", "条码为空" } }; continue; }
            if (isbn.isEmpty()) { r.list << QVariantMap { { "row", rowNo }, { "reason", "ISBN 为空" } }; continue; }
            QString dateVal;
            const QString dateErr = validateDateCell(cell(QStringLiteral("入藏日期")),
                                                     QStringLiteral("入藏日期"), &dateVal);
            if (!dateErr.isEmpty()) { r.list << QVariantMap { { "row", rowNo }, { "reason", dateErr } }; continue; }
            if (seenKeys.contains(barcode)) {
                r.list << QVariantMap { { "row", rowNo },
                                        { "reason", QStringLiteral("条码 %1 在文件中重复").arg(barcode) } };
                continue;
            }
            q.prepare(QStringLiteral("SELECT id, status FROM books WHERE isbn = ?"));
            q.addBindValue(isbn);
            if (!q.exec() || !q.next()) {
                r.list << QVariantMap { { "row", rowNo },
                                        { "reason", QStringLiteral("ISBN %1 对应图书不存在").arg(isbn) } };
                continue;
            }
            if (q.value(QStringLiteral("status")).toString() == QStringLiteral("shelved_off")) {
                r.list << QVariantMap { { "row", rowNo },
                                        { "reason", QStringLiteral("ISBN %1 对应图书已下架").arg(isbn) } };
                continue;
            }
            const qint64 bookId = q.value(0).toLongLong();
            q.prepare(QStringLiteral("SELECT COUNT(*) FROM book_copies WHERE copy_code = ?"));
            q.addBindValue(barcode);
            if (!q.exec() || !q.next()) { m_db.rollback(); return Result::fail(ErrorCode::DbError, QStringLiteral("条码查重失败")); }
            if (q.value(0).toInt() > 0) {
                r.list << QVariantMap { { "row", rowNo },
                                        { "reason", QStringLiteral("条码 %1 已存在").arg(barcode) } };
                continue;
            }
            q.prepare(QStringLiteral(
                "INSERT INTO book_copies (copy_code, book_id, location, status, acquired_date)"
                " VALUES (?,?,?,'available',?)"));
            q.addBindValue(barcode);
            q.addBindValue(bookId);
            q.addBindValue(cell(QStringLiteral("馆藏位置")));
            q.addBindValue(dateVal);
            if (!q.exec()) { m_db.rollback(); return Result::fail(ErrorCode::DbError, QStringLiteral("导入副本失败(行 %1)").arg(rowNo)); }
            seenKeys.insert(barcode);
            ++importedRows;
        }
    }
    if (!m_db.commit()) {
        m_db.rollback();
        return Result::fail(ErrorCode::DbError, QStringLiteral("导入事务提交失败"));
    }
    r.data.insert(QStringLiteral("total_rows"), lastRow - 1);
    r.data.insert(QStringLiteral("imported_rows"), importedRows);
    r.data.insert(QStringLiteral("error_rows"), r.list.size());
    r.message = QStringLiteral("导入完成：共 %1 行，成功 %2 行，失败 %3 行")
                    .arg(lastRow - 1).arg(importedRows).arg(r.list.size());
    return r;
}

// ===================== 导入入口（按扩展名分派） =====================
Result ImportExportService::importBooks(const OperatorContext& op, const QString& filePath)
{
    if (!op.authenticated)
        return Result::fail(ErrorCode::NotAuthenticated, QStringLiteral("未登录，禁止操作"));
    if (!op.isStaff())
        return Result::fail(ErrorCode::NoPermission, QStringLiteral("无权限：仅管理员/图书管理员可导入图书"));
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    if (suffix == QStringLiteral("csv")) return importCsv(op, filePath, true);
    if (suffix == QStringLiteral("xlsx")) return importXlsx(op, filePath, true);
    return Result::fail(ErrorCode::FormatError,
                        QStringLiteral("不支持的文件格式：.%1（仅支持 .csv / .xlsx）").arg(suffix));
}

Result ImportExportService::importCopies(const OperatorContext& op, const QString& filePath)
{
    if (!op.authenticated)
        return Result::fail(ErrorCode::NotAuthenticated, QStringLiteral("未登录，禁止操作"));
    if (!op.isStaff())
        return Result::fail(ErrorCode::NoPermission, QStringLiteral("无权限：仅管理员/图书管理员可导入副本"));
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    if (suffix == QStringLiteral("csv")) return importCsv(op, filePath, false);
    if (suffix == QStringLiteral("xlsx")) return importXlsx(op, filePath, false);
    return Result::fail(ErrorCode::FormatError,
                        QStringLiteral("不支持的文件格式：.%1（仅支持 .csv / .xlsx）").arg(suffix));
}

// ===================== 导出（分批查询，支持大文件） =====================
namespace {
constexpr int kExportBatchSize = 500;
}

Result ImportExportService::exportBooks(const OperatorContext& op, const QString& filePath)
{
    if (!op.authenticated)
        return Result::fail(ErrorCode::NotAuthenticated, QStringLiteral("未登录"));
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    if (suffix != QStringLiteral("csv") && suffix != QStringLiteral("xlsx"))
        return Result::fail(ErrorCode::FormatError,
                            QStringLiteral("不支持的导出格式：.%1（仅支持 .csv / .xlsx）").arg(suffix));

    const QStringList header = { QStringLiteral("ISBN"), QStringLiteral("书名"),
                                 QStringLiteral("作者"), QStringLiteral("出版社"),
                                 QStringLiteral("出版日期"), QStringLiteral("分类"),
                                 QStringLiteral("简介"), QStringLiteral("封面路径"),
                                 QStringLiteral("馆藏总数"), QStringLiteral("可借数量"),
                                 QStringLiteral("状态") };

    // CSV 写入器（UTF-8 BOM，Excel 兼容；引号转义）
    struct CsvWriter {
        QFile file;
        QTextStream ts;
        bool openOk = false;
        explicit CsvWriter(const QString& path) : file(path)
        {
            openOk = file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
            if (openOk) {
                file.write(QByteArray("\xEF\xBB\xBF"));
                ts.setDevice(&file);
                ts.setEncoding(QStringConverter::Utf8);
            }
        }
        void writeRow(const QStringList& cells)
        {
            QStringList escaped;
            for (const QString& c : cells) {
                if (c.contains(QLatin1Char(',')) || c.contains(QLatin1Char('"'))
                    || c.contains(QLatin1Char('\n')))
                    escaped << QStringLiteral("\"%1\"").arg(QString(c).replace(QLatin1Char('"'), QStringLiteral("\"\"")));
                else
                    escaped << c;
            }
            ts << escaped.join(QLatin1Char(',')) << "\r\n";
        }
    };

    std::unique_ptr<CsvWriter> csv;
    std::unique_ptr<QXlsx::Document> xlsx;
    if (suffix == QStringLiteral("csv")) {
        csv = std::make_unique<CsvWriter>(filePath);
        if (!csv->openOk)
            return Result::fail(ErrorCode::FileError, QStringLiteral("无法写入文件：%1").arg(filePath));
        csv->writeRow(header);
    } else {
        xlsx = std::make_unique<QXlsx::Document>();
        for (int c = 0; c < header.size(); ++c)
            xlsx->write(1, c + 1, header.at(c));
    }

    int offset = 0, exported = 0, rowNo = 1;
    while (true) {
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral(
            "SELECT b.isbn, b.title, b.author, b.publisher, b.publish_date, c.name AS category_name,"
            " b.intro, b.cover_path, b.status,"
            " (SELECT COUNT(*) FROM book_copies cp WHERE cp.book_id = b.id) AS copy_total,"
            " (SELECT COUNT(*) FROM book_copies cp WHERE cp.book_id = b.id AND cp.status = 'available') AS available_count"
            " FROM books b LEFT JOIN categories c ON c.id = b.category_id"
            " ORDER BY b.id LIMIT %1 OFFSET %2").arg(kExportBatchSize).arg(offset));
        if (!q.exec())
            return Result::fail(ErrorCode::DbError,
                                QStringLiteral("导出查询失败: %1").arg(q.lastError().text()));
        int fetched = 0;
        while (q.next()) {
            ++fetched;
            const QStringList cells = {
                q.value(QStringLiteral("isbn")).toString(),
                q.value(QStringLiteral("title")).toString(),
                q.value(QStringLiteral("author")).toString(),
                q.value(QStringLiteral("publisher")).toString(),
                q.value(QStringLiteral("publish_date")).toString(),
                q.value(QStringLiteral("category_name")).toString(),
                q.value(QStringLiteral("intro")).toString(),
                q.value(QStringLiteral("cover_path")).toString(),
                QString::number(q.value(QStringLiteral("copy_total")).toInt()),
                QString::number(q.value(QStringLiteral("available_count")).toInt()),
                bookStatusDisplayName(q.value(QStringLiteral("status")).toString())
            };
            if (csv) csv->writeRow(cells);
            else { ++rowNo; for (int c = 0; c < cells.size(); ++c) xlsx->write(rowNo, c + 1, cells.at(c)); }
            ++exported;
        }
        if (fetched < kExportBatchSize) break;
        offset += kExportBatchSize;
    }

    if (xlsx && !xlsx->saveAs(filePath))
        return Result::fail(ErrorCode::FileError, QStringLiteral("写入 XLSX 文件失败：%1").arg(filePath));
    if (csv) csv->ts.flush();
    Result r = Result::success(QStringLiteral("图书导出完成：共 %1 条").arg(exported));
    r.data.insert(QStringLiteral("exported_rows"), exported);
    return r;
}

Result ImportExportService::exportCopies(const OperatorContext& op, const QString& filePath)
{
    if (!op.authenticated)
        return Result::fail(ErrorCode::NotAuthenticated, QStringLiteral("未登录"));
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    if (suffix != QStringLiteral("csv") && suffix != QStringLiteral("xlsx"))
        return Result::fail(ErrorCode::FormatError,
                            QStringLiteral("不支持的导出格式：.%1（仅支持 .csv / .xlsx）").arg(suffix));

    const QStringList header = { QStringLiteral("条码"), QStringLiteral("ISBN"),
                                 QStringLiteral("书名"), QStringLiteral("馆藏位置"),
                                 QStringLiteral("状态"), QStringLiteral("入藏日期") };

    QFile outFile(filePath);
    std::unique_ptr<QTextStream> csvTs;
    if (suffix == QStringLiteral("csv")) {
        if (!outFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
            return Result::fail(ErrorCode::FileError, QStringLiteral("无法写入文件：%1").arg(filePath));
        outFile.write(QByteArray("\xEF\xBB\xBF"));
        csvTs = std::make_unique<QTextStream>(&outFile);
        csvTs->setEncoding(QStringConverter::Utf8);
        QStringList hdr;
        for (const QString& h : header) {
            if (h.contains(QLatin1Char(',')) || h.contains(QLatin1Char('"')))
                hdr << QStringLiteral("\"%1\"").arg(h);
            else
                hdr << h;
        }
        *csvTs << hdr.join(QLatin1Char(',')) << "\r\n";
    }
    std::unique_ptr<QXlsx::Document> xlsx;
    if (suffix == QStringLiteral("xlsx")) {
        xlsx = std::make_unique<QXlsx::Document>();
        for (int c = 0; c < header.size(); ++c)
            xlsx->write(1, c + 1, header.at(c));
    }

    int offset = 0, exported = 0, rowNo = 1;
    while (true) {
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral(
            "SELECT cp.copy_code AS barcode, b.isbn, b.title AS book_title, cp.location, cp.status, cp.acquired_date"
            " FROM book_copies cp LEFT JOIN books b ON b.id = cp.book_id"
            " ORDER BY cp.id LIMIT %1 OFFSET %2").arg(kExportBatchSize).arg(offset));
        if (!q.exec())
            return Result::fail(ErrorCode::DbError,
                                QStringLiteral("导出查询失败: %1").arg(q.lastError().text()));
        int fetched = 0;
        while (q.next()) {
            ++fetched;
            const QStringList cells = {
                q.value(QStringLiteral("barcode")).toString(),
                q.value(QStringLiteral("isbn")).toString(),
                q.value(QStringLiteral("book_title")).toString(),
                q.value(QStringLiteral("location")).toString(),
                copyStatusDisplayName(q.value(QStringLiteral("status")).toString()),
                q.value(QStringLiteral("acquired_date")).toString()
            };
            if (csvTs) {
                QStringList escaped;
                for (const QString& c : cells) {
                    if (c.contains(QLatin1Char(',')) || c.contains(QLatin1Char('"'))
                        || c.contains(QLatin1Char('\n')))
                        escaped << QStringLiteral("\"%1\"").arg(QString(c).replace(QLatin1Char('"'), QStringLiteral("\"\"")));
                    else
                        escaped << c;
                }
                *csvTs << escaped.join(QLatin1Char(',')) << "\r\n";
            } else {
                ++rowNo;
                for (int c = 0; c < cells.size(); ++c)
                    xlsx->write(rowNo, c + 1, cells.at(c));
            }
            ++exported;
        }
        if (fetched < kExportBatchSize) break;
        offset += kExportBatchSize;
    }

    if (xlsx && !xlsx->saveAs(filePath))
        return Result::fail(ErrorCode::FileError, QStringLiteral("写入 XLSX 文件失败：%1").arg(filePath));
    if (csvTs) csvTs->flush();
    outFile.close();
    Result r = Result::success(QStringLiteral("副本导出完成：共 %1 条").arg(exported));
    r.data.insert(QStringLiteral("exported_rows"), exported);
    return r;
}

} // namespace lib
