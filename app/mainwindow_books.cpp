#include "mainwindow.h"
#include "windowwidgets.h"
#include "../common/database.h"
#include "../library-backend-a/src/services/BookService.h"

#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QFrame>

using namespace WindowWidgets;

QWidget *MainWindow::buildBooksPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);

    auto *formPanel = makePanel();
    auto *formLayout = new QVBoxLayout(formPanel);
    formLayout->setContentsMargins(20, 18, 20, 18);
    formLayout->addWidget(makeLabel(QStringLiteral("新增馆藏"),
                                    QStringLiteral("panelTitle")));
    formLayout->addWidget(makeLabel(
        QStringLiteral("同时生成实体册，新书可直接进入借阅流程。"),
        QStringLiteral("mutedText")));

    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(8);
    m_bookTitle = new QLineEdit;
    m_bookTitle->setPlaceholderText(QStringLiteral("例如：数据结构"));
    m_bookAuthor = new QLineEdit;
    m_bookAuthor->setPlaceholderText(QStringLiteral("作者"));
    m_bookIsbn = new QLineEdit;
    m_bookIsbn->setPlaceholderText(QStringLiteral("唯一 ISBN"));
    m_bookCategory = new QComboBox;
    m_bookCopyCount = new QSpinBox;
    m_bookCopyCount->setRange(1, 50);
    m_bookCopyCount->setSuffix(QStringLiteral(" 册"));
    auto *addButton = makePrimaryButton(QStringLiteral("保存并入藏"));
    grid->addWidget(makeLabel(QStringLiteral("书名"),
                              QStringLiteral("fieldLabel")), 0, 0);
    grid->addWidget(makeLabel(QStringLiteral("作者"),
                              QStringLiteral("fieldLabel")), 0, 2);
    grid->addWidget(makeLabel(QStringLiteral("ISBN"),
                              QStringLiteral("fieldLabel")), 0, 4);
    grid->addWidget(m_bookTitle, 1, 0, 1, 2);
    grid->addWidget(m_bookAuthor, 1, 2, 1, 2);
    grid->addWidget(m_bookIsbn, 1, 4, 1, 2);
    grid->addWidget(makeLabel(QStringLiteral("分类"),
                              QStringLiteral("fieldLabel")), 2, 0);
    grid->addWidget(makeLabel(QStringLiteral("复本数"),
                              QStringLiteral("fieldLabel")), 2, 2);
    grid->addWidget(m_bookCategory, 3, 0, 1, 2);
    grid->addWidget(m_bookCopyCount, 3, 2);
    grid->addWidget(addButton, 3, 4, 1, 2);
    grid->setColumnStretch(0, 2);
    grid->setColumnStretch(2, 2);
    grid->setColumnStretch(4, 2);
    formLayout->addLayout(grid);
    layout->addWidget(formPanel);

    auto *catalogPanel = makePanel();
    auto *catalogLayout = new QVBoxLayout(catalogPanel);
    catalogLayout->setContentsMargins(20, 18, 20, 18);
    auto *catalogHeader = new QHBoxLayout;
    catalogHeader->addWidget(makeLabel(QStringLiteral("馆藏目录"),
                                       QStringLiteral("panelTitle")));
    catalogHeader->addStretch();
    m_bookSearch = new QLineEdit;
    m_bookSearch->setPlaceholderText(QStringLiteral("搜索书名、作者或 ISBN"));
    m_bookSearch->setMaximumWidth(290);
    catalogHeader->addWidget(m_bookSearch);
    auto *searchButton = new QPushButton(QStringLiteral("查询"));
    auto *reserveButton = new QPushButton(QStringLiteral("预约所选图书"));
    catalogHeader->addWidget(searchButton);
    catalogHeader->addWidget(reserveButton);
    catalogLayout->addLayout(catalogHeader);
    m_booksTable = makeTable({
        QStringLiteral("书名"), QStringLiteral("作者"),
        QStringLiteral("ISBN"), QStringLiteral("分类"),
        QStringLiteral("馆藏"), QStringLiteral("可借")
    });
    catalogLayout->addWidget(m_booksTable);
    layout->addWidget(catalogPanel, 1);

    connect(addButton, &QPushButton::clicked, this, &MainWindow::addBook);
    connect(m_bookIsbn, &QLineEdit::returnPressed, this, &MainWindow::addBook);
    connect(m_bookSearch, &QLineEdit::returnPressed, this, &MainWindow::refreshBooks);
    connect(searchButton, &QPushButton::clicked, this, &MainWindow::refreshBooks);
    connect(reserveButton, &QPushButton::clicked, this, &MainWindow::reserveSelectedBook);
    return page;
}

void MainWindow::addBook()
{
    QString error;
    if (!m_bookService.addBook(
            m_session, m_bookTitle->text(), m_bookAuthor->text(),
            m_bookIsbn->text(), m_bookCategory->currentText(),
            m_bookCopyCount->value(), &error)) {
        QMessageBox::warning(this, QStringLiteral("无法新增馆藏"), error);
        return;
    }
    const QString title = m_bookTitle->text().trimmed();
    m_bookTitle->clear();
    m_bookAuthor->clear();
    m_bookIsbn->clear();
    m_bookCopyCount->setValue(1);
    m_bookTitle->setFocus();
    refreshAll();
    showStatus(QStringLiteral("《%1》已完成入藏").arg(title), true);
}

void MainWindow::refreshBooks()
{
    QString error;
    const QString currentCategory = m_bookCategory->currentText();
    m_bookCategory->clear();
    m_bookCategory->addItems(m_bookService.categories(m_session, &error));
    const int categoryIndex = m_bookCategory->findText(currentCategory);
    if (categoryIndex >= 0) {
        m_bookCategory->setCurrentIndex(categoryIndex);
    }

    m_booksTable->setRowCount(0);
    if (!checkPermission(Permission::ViewBooks)) {
        return;
    }
    lib::BookService service(Database::connection());
    const auto result = service.listBooks(operatorContext(), {
        {QStringLiteral("keyword"), m_bookSearch->text()},
        {QStringLiteral("status"), QStringLiteral("active")}
    }, 1, 1000);
    if (!result.ok()) {
        showStatus(result.message, false);
        return;
    }
    const auto &books = result.list;
    m_booksTable->setRowCount(books.size());
    for (int row = 0; row < books.size(); ++row) {
        const auto book = books.at(row).toMap();
        const QStringList columns{QStringLiteral("title"), QStringLiteral("author"),
            QStringLiteral("isbn"), QStringLiteral("category_name"),
            QStringLiteral("copy_total"), QStringLiteral("copy_available")};
        for (int column = 0; column < columns.size(); ++column) {
            m_booksTable->setItem(row, column, new QTableWidgetItem(book.value(columns.at(column)).toString()));
        }
        m_booksTable->item(row, 0)->setData(Qt::UserRole, book.value(QStringLiteral("book_id")));
    }
}
