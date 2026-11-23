#include "mainwindow.h"
#include "common/contract.h"
#include "services/BookService.h"
#include "services/CategoryService.h"
#include "services/CopyService.h"
#include "services/SearchService.h"
#include "services/ImportExportService.h"
#include "database/DatabaseManager.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QTableView>
#include <QComboBox>
#include <QColor>
#include <QVBoxLayout>

namespace lib {

// ===================== 操作上下文（演示：管理员） =====================
static OperatorContext adminOp()
{
    OperatorContext op;
    op.userId = 1;
    op.userName = QStringLiteral("演示管理员");
    op.role = Role::Admin;
    op.authenticated = true;
    return op;
}

// ===================== BookTableModel =====================
BookTableModel::BookTableModel(QObject* parent) : QAbstractTableModel(parent) {}

void BookTableModel::setRows(const QVariantList& rows)
{
    beginResetModel();
    m_rows = rows;
    endResetModel();
}

QVariantMap BookTableModel::rowAt(int r) const
{
    return m_rows.value(r).toMap();
}

int BookTableModel::rowCount(const QModelIndex&) const { return m_rows.size(); }
int BookTableModel::columnCount(const QModelIndex&) const { return ColCount; }

QVariant BookTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const QVariantMap row = m_rows.at(index.row()).toMap();
    if (role == Qt::ForegroundRole) {
        // 状态色规范：绿=在馆可借/正常，红=异常/下架
        if (index.column() == ColStatus || index.column() == ColCopyAvailable) {
            const bool ok = row.value(QStringLiteral("copy_available")).toInt() > 0;
            if (index.column() == ColStatus)
                return ok ? QColor(0, 128, 0) : QColor(200, 0, 0);
        }
        return {};
    }
    if (role != Qt::DisplayRole)
        return {};
    switch (index.column()) {
    case ColTitle:         return row.value(QStringLiteral("title"));
    case ColAuthor:        return row.value(QStringLiteral("author"));
    case ColIsbn:          return row.value(QStringLiteral("isbn"));
    case ColCategory:      return row.value(QStringLiteral("category_name"));
    case ColCopyTotal:     return row.value(QStringLiteral("copy_total"));
    case ColCopyAvailable: return row.value(QStringLiteral("copy_available"));
    case ColStatus:        return row.value(QStringLiteral("status_text"));
    default:               return {};
    }
}

QVariant BookTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
        return QAbstractTableModel::headerData(section, orientation, role);
    switch (section) {
    case ColTitle:         return QStringLiteral("书名");
    case ColAuthor:        return QStringLiteral("作者");
    case ColIsbn:          return QStringLiteral("ISBN");
    case ColCategory:      return QStringLiteral("分类");
    case ColCopyTotal:     return QStringLiteral("馆藏总数");
    case ColCopyAvailable: return QStringLiteral("可借数量");
    case ColStatus:        return QStringLiteral("状态");
    default:               return {};
    }
}

// ===================== 新增图书对话框 =====================
static bool showAddBookDialog(QWidget* parent, qint64* newBookId)
{
    CategoryService cs(DatabaseManager::instance().db());
    Result catR = cs.listCategories(adminOp());
    if (!catR.ok()) {
        QMessageBox::warning(parent, QStringLiteral("加载分类失败"), catR.message);
        return false;
    }
    QDialog dlg(parent);
    dlg.setWindowTitle(QStringLiteral("新增图书"));
    QFormLayout* form = new QFormLayout(&dlg);
    QLineEdit* isbnEdit = new QLineEdit;
    QLineEdit* titleEdit = new QLineEdit;
    QComboBox* catCombo = new QComboBox;
    for (const QVariant& v : catR.list) {
        const QVariantMap m = v.toMap();
        catCombo->addItem(m.value(QStringLiteral("name")).toString(),
                          m.value(QStringLiteral("category_id")));
    }
    QLineEdit* authorEdit = new QLineEdit;
    QLineEdit* publisherEdit = new QLineEdit;
    QLineEdit* dateEdit = new QLineEdit;
    dateEdit->setPlaceholderText(QStringLiteral("yyyy-MM-dd"));
    form->addRow(QStringLiteral("ISBN *"), isbnEdit);
    form->addRow(QStringLiteral("书名 *"), titleEdit);
    form->addRow(QStringLiteral("分类 *"), catCombo);
    form->addRow(QStringLiteral("作者"), authorEdit);
    form->addRow(QStringLiteral("出版社"), publisherEdit);
    form->addRow(QStringLiteral("出版日期"), dateEdit);
    QDialogButtonBox* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(btns);
    QObject::connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted)
        return false;

    QVariantMap fields;
    fields.insert(QStringLiteral("isbn"), isbnEdit->text().trimmed());
    fields.insert(QStringLiteral("title"), titleEdit->text().trimmed());
    fields.insert(QStringLiteral("category_id"), catCombo->currentData().toLongLong());
    if (!authorEdit->text().trimmed().isEmpty())
        fields.insert(QStringLiteral("author"), authorEdit->text().trimmed());
    if (!publisherEdit->text().trimmed().isEmpty())
        fields.insert(QStringLiteral("publisher"), publisherEdit->text().trimmed());
    if (!dateEdit->text().trimmed().isEmpty())
        fields.insert(QStringLiteral("publish_date"), dateEdit->text().trimmed());

    BookService bs(DatabaseManager::instance().db());
    Result r = bs.addBook(adminOp(), fields);
    if (!r.ok()) {
        // 必填缺失/ISBN 重复等后端原因直接透出
        QMessageBox::warning(parent, QStringLiteral("新增失败"), r.message);
        return false;
    }
    *newBookId = r.data.value(QStringLiteral("book_id")).toLongLong();
    return true;
}

// ===================== 副本管理弹窗 =====================
class CopyManagerDialog : public QDialog {
public:
    CopyManagerDialog(QWidget* parent, qint64 bookId, const QString& title)
        : QDialog(parent), m_bookId(bookId)
    {
        setWindowTitle(QStringLiteral("副本管理：%1").arg(title));
        resize(560, 420);
        QVBoxLayout* lay = new QVBoxLayout(this);
        m_statsLabel = new QLabel(this);
        lay->addWidget(m_statsLabel);

        m_table = new QTableView(this);
        m_model = new CopyTableModel(this);
        m_table->setModel(m_model);
        m_table->horizontalHeader()->setStretchLastSection(true);
        m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_table->setSelectionMode(QAbstractItemView::SingleSelection);
        lay->addWidget(m_table);

        QHBoxLayout* ops = new QHBoxLayout;
        QPushButton* addBtn = new QPushButton(QStringLiteral("新增副本"), this);
        QPushButton* changeBtn = new QPushButton(QStringLiteral("变更选中副本状态"), this);
        ops->addWidget(addBtn);
        ops->addWidget(changeBtn);
        lay->addLayout(ops);

        connect(addBtn, &QPushButton::clicked, this, [this] { addCopy(); });
        connect(changeBtn, &QPushButton::clicked, this, [this] { changeStatus(); });
        reload();
    }

private:
    class CopyTableModel : public QAbstractTableModel {
    public:
        explicit CopyTableModel(QObject* parent) : QAbstractTableModel(parent) {}
        void setRows(const QVariantList& rows)
        {
            beginResetModel();
            m_rows = rows;
            endResetModel();
        }
        QVariantMap rowAt(int r) const { return m_rows.value(r).toMap(); }
        int rowCount(const QModelIndex& = {}) const override { return m_rows.size(); }
        int columnCount(const QModelIndex& = {}) const override { return 4; }
        QVariant data(const QModelIndex& i, int role) const override
        {
            if (!i.isValid() || i.row() >= m_rows.size() || (role != Qt::DisplayRole && role != Qt::ForegroundRole))
                return {};
            const QVariantMap m = m_rows.at(i.row()).toMap();
            if (role == Qt::ForegroundRole) {
                const QString st = m.value(QStringLiteral("status")).toString();
                const bool good = st == QLatin1String("AVAILABLE");
                if (i.column() == 2) return good ? QColor(0, 128, 0) : QColor(200, 0, 0);
                return {};
            }
            switch (i.column()) {
            case 0: return m.value(QStringLiteral("barcode"));
            case 1: return m.value(QStringLiteral("location"));
            case 2: return m.value(QStringLiteral("status_text"));
            case 3: return m.value(QStringLiteral("acquired_date"));
            }
            return {};
        }
        QVariant headerData(int s, Qt::Orientation o, int role) const override
        {
            if (role != Qt::DisplayRole || o != Qt::Horizontal)
                return {};
            switch (s) {
            case 0: return QStringLiteral("条码");
            case 1: return QStringLiteral("馆藏位置");
            case 2: return QStringLiteral("状态");
            case 3: return QStringLiteral("入藏日期");
            }
            return {};
        }
    private:
        QVariantList m_rows;
    };

    void reload()
    {
        CopyService cs(DatabaseManager::instance().db());
        Result r = cs.listCopiesByBook(adminOp(), m_bookId, 1, 100);
        if (!r.ok()) {
            QMessageBox::warning(this, QStringLiteral("查询副本失败"), r.message);
            return;
        }
        m_model->setRows(r.list);
        QStringList stats;
        for (auto it = r.data.begin(); it != r.data.end(); ++it)
            stats << QStringLiteral("%1×%2").arg(it.key()).arg(it.value().toInt());
        m_statsLabel->setText(QStringLiteral("<b>副本统计：</b>") + (stats.isEmpty()
                                  ? QStringLiteral("（无副本）") : stats.join(QStringLiteral("　"))));
    }

    void addCopy()
    {
        bool ok = false;
        const QString barcode = QInputDialog::getText(this, QStringLiteral("新增副本"),
                                QStringLiteral("条码（全局唯一）："), QLineEdit::Normal, {}, &ok);
        if (!ok || barcode.trimmed().isEmpty()) return;
        const QString loc = QInputDialog::getText(this, QStringLiteral("新增副本"),
                              QStringLiteral("馆藏位置："), QLineEdit::Normal, {}, &ok);
        CopyService cs(DatabaseManager::instance().db());
        Result r = cs.addCopy(adminOp(), m_bookId, barcode.trimmed(), loc.trimmed());
        if (!r.ok()) {
            // 条码重复等后端原因直接透出
            QMessageBox::warning(this, QStringLiteral("新增失败"), r.message);
            return;
        }
        reload();
    }

    void changeStatus()
    {
        const QModelIndex idx = m_table->currentIndex();
        if (!idx.isValid()) {
            QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请先选中一个副本"));
            return;
        }
        const QVariantMap row = m_model->rowAt(idx.row());
        const QStringList statusList = {
            QStringLiteral("AVAILABLE"), QStringLiteral("BORROWED"), QStringLiteral("RESERVED"),
            QStringLiteral("MAINTENANCE"), QStringLiteral("LOST")
        };
        bool ok = false;
        const QString target = QInputDialog::getItem(this, QStringLiteral("状态变更"),
                QStringLiteral("把副本 %1（当前：%2）改为：")
                    .arg(row.value(QStringLiteral("barcode")).toString(),
                         row.value(QStringLiteral("status_text")).toString()),
                statusList, 0, false, &ok);
        if (!ok) return;
        CopyService cs(DatabaseManager::instance().db());
        Result r = cs.changeCopyStatus(adminOp(),
                     row.value(QStringLiteral("copy_id")).toLongLong(), target);
        if (!r.ok()) {
            // 非法状态转换：message 含当前状态，直接透出演示"状态机拦截"
            QMessageBox::warning(this, QStringLiteral("状态变更被拒绝"), r.message);
            return;
        }
        reload();
    }

    class QAbstractTableModelHelper {};
    QTableView* m_table = nullptr;
    CopyTableModel* m_model = nullptr;
    QLabel* m_statsLabel = nullptr;
    qint64 m_bookId = 0;
};

// ===================== 主窗口 =====================
MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("图书馆管理系统 · 后端A 业务演示（Qt GUI）"));
    resize(860, 520);

    QWidget* central = new QWidget(this);
    QVBoxLayout* lay = new QVBoxLayout(central);

    QHBoxLayout* top = new QHBoxLayout;
    m_searchEdit = new QLineEdit(central);
    m_searchEdit->setPlaceholderText(QStringLiteral("关键词检索（书名 / 作者 / ISBN，回车查询）"));
    QPushButton* searchBtn = new QPushButton(QStringLiteral("检索"), central);
    QPushButton* refreshBtn = new QPushButton(QStringLiteral("刷新"), central);
    top->addWidget(m_searchEdit, 1);
    top->addWidget(searchBtn);
    top->addWidget(refreshBtn);
    lay->addLayout(top);

    m_model = new BookTableModel(this);
    m_table = new QTableView(central);
    m_table->setModel(m_model);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    lay->addWidget(m_table, 1);

    QHBoxLayout* bottom = new QHBoxLayout;
    QPushButton* addBtn = new QPushButton(QStringLiteral("新增图书"), central);
    QPushButton* copiesBtn = new QPushButton(QStringLiteral("副本管理（选中图书）"), central);
    QPushButton* shelveBtn = new QPushButton(QStringLiteral("下架（选中图书）"), central);
    QPushButton* exportBtn = new QPushButton(QStringLiteral("导出图书 CSV"), central);
    QPushButton* importBtn = new QPushButton(QStringLiteral("导入 CSV"), central);
    bottom->addWidget(addBtn);
    bottom->addWidget(copiesBtn);
    bottom->addWidget(shelveBtn);
    bottom->addStretch();
    bottom->addWidget(exportBtn);
    bottom->addWidget(importBtn);
    lay->addLayout(bottom);

    setCentralWidget(central);
    statusBar()->showMessage(QStringLiteral("所有操作均调用后端A真实 Service（BookService / CopyService / SearchService / ImportExportService）"));

    connect(searchBtn, &QPushButton::clicked, this, &MainWindow::refresh);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &MainWindow::refresh);
    connect(refreshBtn, &QPushButton::clicked, this, [this] {
        m_searchEdit->clear();
        refresh();
    });
    connect(addBtn, &QPushButton::clicked, this, &MainWindow::addBook);
    connect(copiesBtn, &QPushButton::clicked, this, &MainWindow::openCopyManager);
    connect(shelveBtn, &QPushButton::clicked, this, &MainWindow::shelveOffSelected);
    connect(exportBtn, &QPushButton::clicked, this, &MainWindow::exportCsv);
    connect(importBtn, &QPushButton::clicked, this, &MainWindow::importCsv);

    refresh();
}

void MainWindow::refresh()
{
    BookService bs(DatabaseManager::instance().db());
    QVariantMap filter;
    const QString kw = m_searchEdit->text().trimmed();
    if (!kw.isEmpty())
        filter.insert(QStringLiteral("keyword"), kw);
    Result r = bs.listBooks(adminOp(), filter, 1, 100);
    if (!r.ok()) {
        statusBar()->showMessage(QStringLiteral("查询失败：%1").arg(r.message), 5000);
        return;
    }
    m_model->setRows(r.list);
    statusBar()->showMessage(QStringLiteral("共 %1 条记录").arg(r.total));
}

void MainWindow::addBook()
{
    qint64 newId = 0;
    if (showAddBookDialog(this, &newId)) {
        refresh();
        statusBar()->showMessage(QStringLiteral("新增成功 book_id=%1").arg(newId));
    }
}

qint64 MainWindow::selectedBookId(bool* ok) const
{
    const QModelIndex idx = m_table->currentIndex();
    if (!idx.isValid()) {
        QMessageBox::information(const_cast<MainWindow*>(this), QStringLiteral("提示"),
                                 QStringLiteral("请先在表格中选中一本图书"));
        if (ok) *ok = false;
        return 0;
    }
    if (ok) *ok = true;
    return m_model->rowAt(idx.row()).value(QStringLiteral("book_id")).toLongLong();
}

void MainWindow::shelveOffSelected()
{
    bool ok = false;
    const qint64 id = selectedBookId(&ok);
    if (!ok) return;
    const auto ret = QMessageBox::question(this, QStringLiteral("确认下架"),
        QStringLiteral("下架后图书不可再借出（逻辑删除）。确认下架？"));
    if (ret != QMessageBox::Yes) return;
    BookService bs(DatabaseManager::instance().db());
    Result r = bs.shelveOff(adminOp(), id);
    if (!r.ok()) {
        QMessageBox::warning(this, QStringLiteral("下架失败"), r.message);
        return;
    }
    refresh();
    statusBar()->showMessage(QStringLiteral("下架成功"));
}

void MainWindow::openCopyManager()
{
    bool ok = false;
    const qint64 id = selectedBookId(&ok);
    if (!ok) return;
    const QVariantMap row = m_model->rowAt(m_table->currentIndex().row());
    CopyManagerDialog dlg(this, id, row.value(QStringLiteral("title")).toString());
    dlg.exec();
    refresh(); // 回来后统计行即时刷新
}

void MainWindow::exportCsv()
{
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出图书 CSV"),
        QStringLiteral("books_export.csv"), QStringLiteral("CSV (*.csv)"));
    if (path.isEmpty()) return;
    ImportExportService s(DatabaseManager::instance().db());
    Result r = s.exportBooks(adminOp(), path);
    if (!r.ok()) {
        QMessageBox::warning(this, QStringLiteral("导出失败"), r.message);
        return;
    }
    QMessageBox::information(this, QStringLiteral("导出成功"),
        QStringLiteral("导出 %1 行 → %2")
            .arg(r.data.value(QStringLiteral("exported_rows")).toInt()).arg(path));
}

void MainWindow::importCsv()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("导入 CSV"),
        {}, QStringLiteral("CSV (*.csv)"));
    if (path.isEmpty()) return;
    ImportExportService s(DatabaseManager::instance().db());
    Result r = s.importBooks(adminOp(), path);
    if (!r.ok()) {
        QMessageBox::warning(this, QStringLiteral("导入失败"), r.message);
        return;
    }
    // 错误行汇总（r.list：行号 + 原因）展示
    QStringList errs;
    for (const QVariant& e : r.list)
        errs << QStringLiteral("第 %1 行：%2")
                    .arg(e.toMap().value(QStringLiteral("row")).toInt())
                    .arg(e.toMap().value(QStringLiteral("reason")).toString());
    refresh();
    QMessageBox::information(this, QStringLiteral("导入完成"),
        QStringLiteral("成功 %1 行，错误 %2 行%3")
            .arg(r.data.value(QStringLiteral("imported_rows")).toInt())
            .arg(r.data.value(QStringLiteral("error_rows")).toInt())
            .arg(errs.isEmpty() ? QString() : QStringLiteral("\n\n") + errs.join(QStringLiteral("\n"))));
}

} // namespace lib
