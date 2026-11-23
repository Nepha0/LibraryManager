#ifndef LIB_MAINWINDOW_H
#define LIB_MAINWINDOW_H
/*
 * MainWindow —— 后端A GUI 演示窗口（可视化验收用，非正式前端）
 * 演示：图书列表(QTableView+Model) / 关键词检索 / 新增图书 / 下架 /
 *       副本管理弹窗（状态机合法与非法转换） / CSV 导入导出
 * 所有操作均走真实 Service，错误提示直接透出 Result.message
 */
#include <QAbstractTableModel>
#include <QMainWindow>
#include <QVariantList>

class QLineEdit;
class QTableView;

namespace lib {

class BookTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Col { ColTitle, ColAuthor, ColIsbn, ColCategory, ColCopyTotal, ColCopyAvailable, ColStatus, ColCount };
    explicit BookTableModel(QObject* parent = nullptr);
    void setRows(const QVariantList& rows);
    QVariantMap rowAt(int r) const;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
private:
    QVariantList m_rows;
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void refresh();
    void addBook();
    void shelveOffSelected();
    void openCopyManager();
    void exportCsv();
    void importCsv();

private:
    qint64 selectedBookId(bool* ok) const;

    QLineEdit*  m_searchEdit = nullptr;
    QTableView* m_table      = nullptr;
    BookTableModel* m_model  = nullptr;
};

} // namespace lib
#endif // LIB_MAINWINDOW_H
