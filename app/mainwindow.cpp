#include "mainwindow.h"

#include "../common/database.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDateTime>
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
QLabel *makeLabel(const QString &text, const QString &objectName = {})
{
    auto *label = new QLabel(text);
    if (!objectName.isEmpty()) {
        label->setObjectName(objectName);
    }
    return label;
}

QFrame *makePanel(const QString &objectName = QStringLiteral("panel"))
{
    auto *panel = new QFrame;
    panel->setObjectName(objectName);
    return panel;
}

QTableWidget *makeTable(const QStringList &headers)
{
    auto *table = new QTableWidget;
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setAlternatingRowColors(true);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setSectionResizeMode(
        QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(
        headers.size() - 1, QHeaderView::Stretch);
    return table;
}

QPushButton *makePrimaryButton(const QString &text)
{
    auto *button = new QPushButton(text);
    button->setProperty("role", QStringLiteral("primary"));
    return button;
}

QWidget *makeMetric(const QString &caption, QLabel **valueLabel,
                    const QString &accentName)
{
    auto *panel = makePanel(QStringLiteral("metric"));
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(18, 16, 18, 16);
    auto *captionLabel = makeLabel(caption, QStringLiteral("metricCaption"));
    auto *value = makeLabel(QStringLiteral("0"), QStringLiteral("metricValue"));
    auto *rule = new QFrame;
    rule->setObjectName(QStringLiteral("metricRule"));
    rule->setProperty("accent", accentName);
    rule->setFixedHeight(3);
    layout->addWidget(captionLabel);
    layout->addWidget(value);
    layout->addWidget(rule);
    *valueLabel = value;
    return panel;
}

QString shortDate(const QDateTime &dateTime)
{
    return dateTime.isValid()
               ? dateTime.toString(QStringLiteral("yyyy-MM-dd"))
               : QStringLiteral("-");
}
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("LibraryManager · 馆务工作台"));
    resize(1280, 800);
    setMinimumSize(1080, 680);

    m_rootStack = new QStackedWidget(this);
    m_rootStack->addWidget(buildLoginPage());
    m_rootStack->setCurrentIndex(0);
    setCentralWidget(m_rootStack);
    statusBar()->showMessage(QStringLiteral("系统就绪"));
}

void MainWindow::openDemoSession()
{
    m_usernameEdit->setText(QStringLiteral("admin"));
    m_passwordEdit->setText(QStringLiteral("admin123"));
    handleLogin();
}

QWidget *MainWindow::buildLoginPage()
{
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("loginPage"));
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(48, 40, 48, 48);
    outer->addStretch();

    auto *content = new QWidget;
    content->setMaximumWidth(920);
    auto *contentLayout = new QHBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(42);

    auto *intro = new QWidget;
    intro->setMinimumWidth(390);
    auto *introLayout = new QVBoxLayout(intro);
    introLayout->setContentsMargins(0, 26, 0, 26);
    auto *eyebrow = makeLabel(QStringLiteral("LIBRARY OPERATIONS"),
                              QStringLiteral("eyebrow"));
    auto *title = makeLabel(QStringLiteral("馆务工作台"),
                            QStringLiteral("loginTitle"));
    auto *subtitle = makeLabel(
        QStringLiteral("统一管理馆藏、读者与借阅流转"),
        QStringLiteral("loginSubtitle"));
    subtitle->setWordWrap(true);
    auto *rule = new QFrame;
    rule->setObjectName(QStringLiteral("titleRule"));
    rule->setFixedSize(56, 4);
    auto *note = makeLabel(
        QStringLiteral("课程演示版 · Qt Widgets + SQLite"),
        QStringLiteral("loginNote"));
    introLayout->addWidget(eyebrow);
    introLayout->addSpacing(12);
    introLayout->addWidget(title);
    introLayout->addWidget(subtitle);
    introLayout->addSpacing(22);
    introLayout->addWidget(rule);
    introLayout->addStretch();
    introLayout->addWidget(note);

    auto *formPanel = makePanel(QStringLiteral("loginPanel"));
    formPanel->setMinimumWidth(390);
    auto *formLayout = new QVBoxLayout(formPanel);
    formLayout->setContentsMargins(36, 34, 36, 34);
    formLayout->setSpacing(12);
    formLayout->addWidget(makeLabel(QStringLiteral("管理员登录"),
                                    QStringLiteral("panelTitle")));
    formLayout->addWidget(makeLabel(QStringLiteral("使用馆员账号进入工作台"),
                                    QStringLiteral("mutedText")));
    formLayout->addSpacing(10);
    formLayout->addWidget(makeLabel(QStringLiteral("账号"),
                                    QStringLiteral("fieldLabel")));
    m_usernameEdit = new QLineEdit(QStringLiteral("admin"));
    m_usernameEdit->setPlaceholderText(QStringLiteral("请输入账号"));
    formLayout->addWidget(m_usernameEdit);
    formLayout->addWidget(makeLabel(QStringLiteral("密码"),
                                    QStringLiteral("fieldLabel")));
    m_passwordEdit = new QLineEdit(QStringLiteral("admin123"));
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setPlaceholderText(QStringLiteral("请输入密码"));
    formLayout->addWidget(m_passwordEdit);
    m_loginError = makeLabel(QString(), QStringLiteral("errorText"));
    m_loginError->setWordWrap(true);
    formLayout->addWidget(m_loginError);
    auto *loginButton = makePrimaryButton(QStringLiteral("进入工作台"));
    loginButton->setMinimumHeight(44);
    formLayout->addWidget(loginButton);
    formLayout->addWidget(makeLabel(
        QStringLiteral("演示账号 admin   /   密码 admin123"),
        QStringLiteral("demoHint")));

    connect(loginButton, &QPushButton::clicked, this, &MainWindow::handleLogin);
    connect(m_passwordEdit, &QLineEdit::returnPressed,
            this, &MainWindow::handleLogin);

    contentLayout->addWidget(intro, 1);
    contentLayout->addWidget(formPanel);
    outer->addWidget(content, 0, Qt::AlignHCenter);
    outer->addStretch();
    return page;
}

QWidget *MainWindow::buildShellPage()
{
    auto *shell = new QWidget;
    auto *shellLayout = new QHBoxLayout(shell);
    shellLayout->setContentsMargins(0, 0, 0, 0);
    shellLayout->setSpacing(0);

    auto *sidebar = new QFrame;
    sidebar->setObjectName(QStringLiteral("sidebar"));
    sidebar->setFixedWidth(230);
    auto *sideLayout = new QVBoxLayout(sidebar);
    sideLayout->setContentsMargins(20, 24, 20, 20);
    sideLayout->setSpacing(8);
    auto *brand = makeLabel(QStringLiteral("LibraryManager"),
                            QStringLiteral("brand"));
    auto *brandSub = makeLabel(QStringLiteral("馆务工作台"),
                               QStringLiteral("brandSub"));
    sideLayout->addWidget(brand);
    sideLayout->addWidget(brandSub);
    sideLayout->addSpacing(24);

    m_navigation = new QListWidget;
    m_navigation->setObjectName(QStringLiteral("navigation"));
    m_navigation->setFrameShape(QFrame::NoFrame);
    const QStringList pageNames = {
        QStringLiteral("工作台"), QStringLiteral("图书与库存"),
        QStringLiteral("借还管理"), QStringLiteral("读者管理"),
        QStringLiteral("统计报表")
    };
    const QList<QStyle::StandardPixmap> icons = {
        QStyle::SP_DesktopIcon, QStyle::SP_DirOpenIcon,
        QStyle::SP_BrowserReload, QStyle::SP_FileDialogDetailedView,
        QStyle::SP_FileDialogInfoView
    };
    for (int index = 0; index < pageNames.size(); ++index) {
        auto *item = new QListWidgetItem(
            style()->standardIcon(icons.at(index)), pageNames.at(index));
        item->setSizeHint(QSize(0, 46));
        m_navigation->addItem(item);
    }
    sideLayout->addWidget(m_navigation, 1);
    auto *logoutButton = new QPushButton(QStringLiteral("退出登录"));
    logoutButton->setObjectName(QStringLiteral("logoutButton"));
    sideLayout->addWidget(logoutButton);

    auto *mainArea = new QWidget;
    auto *mainLayout = new QVBoxLayout(mainArea);
    mainLayout->setContentsMargins(28, 20, 28, 22);
    mainLayout->setSpacing(16);

    auto *header = new QWidget;
    header->setObjectName(QStringLiteral("topBar"));
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    m_pageTitle = makeLabel(QStringLiteral("工作台"),
                            QStringLiteral("pageTitle"));
    m_userLabel = makeLabel(QString(), QStringLiteral("userChip"));
    headerLayout->addWidget(m_pageTitle);
    headerLayout->addStretch();
    headerLayout->addWidget(m_userLabel);

    m_pages = new QStackedWidget;
    m_pages->addWidget(buildDashboardPage());
    m_pages->addWidget(buildBooksPage());
    m_pages->addWidget(buildCirculationPage());
    m_pages->addWidget(buildReadersPage());
    m_pages->addWidget(buildStatisticsPage());

    mainLayout->addWidget(header);
    mainLayout->addWidget(m_pages, 1);
    shellLayout->addWidget(sidebar);
    shellLayout->addWidget(mainArea, 1);

    connect(m_navigation, &QListWidget::currentRowChanged,
            m_pages, &QStackedWidget::setCurrentIndex);
    connect(m_navigation, &QListWidget::currentRowChanged,
            this, &MainWindow::setPageTitle);
    connect(logoutButton, &QPushButton::clicked,
            this, &MainWindow::handleLogout);
    m_navigation->setCurrentRow(0);
    return shell;
}

QWidget *MainWindow::buildDashboardPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    auto *introRow = new QHBoxLayout;
    auto *intro = new QVBoxLayout;
    intro->addWidget(makeLabel(QStringLiteral("馆藏运营概览"),
                               QStringLiteral("sectionTitle")));
    intro->addWidget(makeLabel(
        QStringLiteral("借阅状态与关键数据会随业务操作实时更新。"),
        QStringLiteral("mutedText")));
    auto *addButton = makePrimaryButton(QStringLiteral("新增馆藏"));
    auto *borrowButton = new QPushButton(QStringLiteral("办理借阅"));
    borrowButton->setProperty("role", QStringLiteral("secondary"));
    introRow->addLayout(intro);
    introRow->addStretch();
    introRow->addWidget(addButton);
    introRow->addWidget(borrowButton);
    layout->addLayout(introRow);

    auto *metrics = new QHBoxLayout;
    metrics->setSpacing(12);
    metrics->addWidget(makeMetric(QStringLiteral("馆藏书目"), &m_titlesStat,
                                  QStringLiteral("blue")));
    metrics->addWidget(makeMetric(QStringLiteral("当前可借"), &m_availableStat,
                                  QStringLiteral("green")));
    metrics->addWidget(makeMetric(QStringLiteral("借阅中"), &m_activeLoansStat,
                                  QStringLiteral("orange")));
    metrics->addWidget(makeMetric(QStringLiteral("已逾期"), &m_overdueStat,
                                  QStringLiteral("red")));
    layout->addLayout(metrics);

    auto *recentPanel = makePanel();
    auto *recentLayout = new QVBoxLayout(recentPanel);
    recentLayout->setContentsMargins(20, 18, 20, 18);
    auto *recentHeader = new QHBoxLayout;
    recentHeader->addWidget(makeLabel(QStringLiteral("当前借阅"),
                                      QStringLiteral("panelTitle")));
    recentHeader->addStretch();
    recentHeader->addWidget(makeLabel(
        QStringLiteral("按应还时间排序"), QStringLiteral("mutedText")));
    recentLayout->addLayout(recentHeader);
    m_dashboardLoans = makeTable({
        QStringLiteral("读者"), QStringLiteral("读者编号"),
        QStringLiteral("馆藏"), QStringLiteral("册编号"),
        QStringLiteral("应还日期"), QStringLiteral("状态")
    });
    recentLayout->addWidget(m_dashboardLoans);
    layout->addWidget(recentPanel, 1);

    connect(addButton, &QPushButton::clicked, this, [this] {
        m_navigation->setCurrentRow(1);
        m_bookTitle->setFocus();
    });
    connect(borrowButton, &QPushButton::clicked, this, [this] {
        m_navigation->setCurrentRow(2);
        m_borrowReaderNo->setFocus();
    });
    return page;
}

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
    connect(m_bookSearch, &QLineEdit::textChanged,
            this, &MainWindow::filterBooks);
    return page;
}

QWidget *MainWindow::buildCirculationPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);

    auto *borrowPanel = makePanel();
    auto *borrowLayout = new QVBoxLayout(borrowPanel);
    borrowLayout->setContentsMargins(20, 18, 20, 18);
    auto *borrowTitle = new QHBoxLayout;
    borrowTitle->addWidget(makeLabel(QStringLiteral("办理借阅"),
                                     QStringLiteral("panelTitle")));
    borrowTitle->addStretch();
    borrowTitle->addWidget(makeLabel(
        QStringLiteral("默认借期以读者类型规则为准"),
        QStringLiteral("mutedText")));
    borrowLayout->addLayout(borrowTitle);
    auto *borrowRow = new QHBoxLayout;
    m_borrowReaderNo = new QLineEdit;
    m_borrowReaderNo->setPlaceholderText(QStringLiteral("读者编号，例如 R001"));
    m_borrowCopy = new QComboBox;
    m_borrowCopy->setMinimumWidth(440);
    auto *borrowButton = makePrimaryButton(QStringLiteral("确认借出"));
    borrowRow->addWidget(m_borrowReaderNo, 2);
    borrowRow->addWidget(m_borrowCopy, 4);
    borrowRow->addWidget(borrowButton);
    borrowLayout->addLayout(borrowRow);
    layout->addWidget(borrowPanel);

    auto *loansPanel = makePanel();
    auto *loansLayout = new QVBoxLayout(loansPanel);
    loansLayout->setContentsMargins(20, 18, 20, 18);
    auto *loansHeader = new QHBoxLayout;
    loansHeader->addWidget(makeLabel(QStringLiteral("当前借阅"),
                                     QStringLiteral("panelTitle")));
    loansHeader->addStretch();
    auto *returnButton = new QPushButton(QStringLiteral("归还所选"));
    returnButton->setProperty("role", QStringLiteral("danger"));
    loansHeader->addWidget(returnButton);
    loansLayout->addLayout(loansHeader);
    m_activeLoansTable = makeTable({
        QStringLiteral("读者"), QStringLiteral("读者编号"),
        QStringLiteral("馆藏"), QStringLiteral("册编号"),
        QStringLiteral("借出日期"), QStringLiteral("应还日期"),
        QStringLiteral("状态")
    });
    loansLayout->addWidget(m_activeLoansTable);
    layout->addWidget(loansPanel, 1);

    connect(borrowButton, &QPushButton::clicked,
            this, &MainWindow::borrowBook);
    connect(m_borrowReaderNo, &QLineEdit::returnPressed,
            this, &MainWindow::borrowBook);
    connect(returnButton, &QPushButton::clicked,
            this, &MainWindow::returnSelectedBook);
    return page;
}

QWidget *MainWindow::buildReadersPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);

    auto *formPanel = makePanel();
    auto *formLayout = new QVBoxLayout(formPanel);
    formLayout->setContentsMargins(20, 18, 20, 18);
    formLayout->addWidget(makeLabel(QStringLiteral("新增读者"),
                                    QStringLiteral("panelTitle")));
    auto *form = new QHBoxLayout;
    m_readerNo = new QLineEdit;
    m_readerNo->setPlaceholderText(QStringLiteral("读者编号"));
    m_readerName = new QLineEdit;
    m_readerName->setPlaceholderText(QStringLiteral("姓名"));
    m_readerPhone = new QLineEdit;
    m_readerPhone->setPlaceholderText(QStringLiteral("手机号（可选）"));
    m_readerType = new QComboBox;
    auto *addButton = makePrimaryButton(QStringLiteral("登记读者"));
    form->addWidget(m_readerNo, 2);
    form->addWidget(m_readerName, 2);
    form->addWidget(m_readerPhone, 2);
    form->addWidget(m_readerType, 1);
    form->addWidget(addButton);
    formLayout->addLayout(form);
    layout->addWidget(formPanel);

    auto *readersPanel = makePanel();
    auto *readersLayout = new QVBoxLayout(readersPanel);
    readersLayout->setContentsMargins(20, 18, 20, 18);
    readersLayout->addWidget(makeLabel(QStringLiteral("读者名册"),
                                       QStringLiteral("panelTitle")));
    m_readersTable = makeTable({
        QStringLiteral("读者编号"), QStringLiteral("姓名"),
        QStringLiteral("联系电话"), QStringLiteral("类型"),
        QStringLiteral("借阅中"), QStringLiteral("状态")
    });
    readersLayout->addWidget(m_readersTable);
    layout->addWidget(readersPanel, 1);

    connect(addButton, &QPushButton::clicked,
            this, &MainWindow::addReader);
    connect(m_readerPhone, &QLineEdit::returnPressed,
            this, &MainWindow::addReader);
    return page;
}

QWidget *MainWindow::buildStatisticsPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);

    auto *summary = new QHBoxLayout;
    summary->setSpacing(12);
    summary->addWidget(makeMetric(QStringLiteral("馆藏书目"), &m_reportTitles,
                                  QStringLiteral("blue")));
    summary->addWidget(makeMetric(QStringLiteral("有效读者"), &m_reportReaders,
                                  QStringLiteral("green")));
    summary->addWidget(makeMetric(QStringLiteral("累计借阅"), &m_reportLoans,
                                  QStringLiteral("orange")));
    summary->addWidget(makeMetric(QStringLiteral("当前逾期"), &m_reportOverdue,
                                  QStringLiteral("red")));
    layout->addLayout(summary);

    auto *historyPanel = makePanel();
    auto *historyLayout = new QVBoxLayout(historyPanel);
    historyLayout->setContentsMargins(20, 18, 20, 18);
    auto *historyHeader = new QHBoxLayout;
    historyHeader->addWidget(makeLabel(QStringLiteral("借阅流水"),
                                       QStringLiteral("panelTitle")));
    historyHeader->addStretch();
    auto *exportButton = makePrimaryButton(QStringLiteral("导出 CSV"));
    historyHeader->addWidget(exportButton);
    historyLayout->addLayout(historyHeader);
    m_historyTable = makeTable({
        QStringLiteral("读者编号"), QStringLiteral("读者"),
        QStringLiteral("册编号"), QStringLiteral("馆藏"),
        QStringLiteral("借出日期"), QStringLiteral("应还日期"),
        QStringLiteral("归还日期")
    });
    historyLayout->addWidget(m_historyTable);
    layout->addWidget(historyPanel, 1);

    connect(exportButton, &QPushButton::clicked,
            this, &MainWindow::exportLoans);
    return page;
}

void MainWindow::handleLogin()
{
    m_loginError->clear();
    QString error;
    const auto session =
        m_authService.login(m_usernameEdit->text(),
                            m_passwordEdit->text(), &error);
    if (!session.has_value()) {
        m_loginError->setText(error);
        m_passwordEdit->selectAll();
        m_passwordEdit->setFocus();
        return;
    }

    m_session = *session;
    if (!m_pages) {
        m_rootStack->addWidget(buildShellPage());
    }
    m_userLabel->setText(QStringLiteral("%1  ·  管理员")
                             .arg(m_session.displayName));
    m_rootStack->setCurrentIndex(1);
    m_navigation->setCurrentRow(0);
    m_passwordEdit->clear();
    refreshAll();
    showStatus(QStringLiteral("登录成功，欢迎进入馆务工作台"), true);
}

void MainWindow::handleLogout()
{
    m_session = UserSession{};
    m_usernameEdit->setText(QStringLiteral("admin"));
    m_passwordEdit->setText(QStringLiteral("admin123"));
    m_loginError->clear();
    m_rootStack->setCurrentIndex(0);
    m_usernameEdit->setFocus();
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

void MainWindow::addReader()
{
    QString error;
    if (!m_readerService.addReader(
            m_session, m_readerNo->text(), m_readerName->text(),
            m_readerPhone->text(), m_readerType->currentText(), &error)) {
        QMessageBox::warning(this, QStringLiteral("无法登记读者"), error);
        return;
    }
    const QString name = m_readerName->text().trimmed();
    m_readerNo->clear();
    m_readerName->clear();
    m_readerPhone->clear();
    m_readerNo->setFocus();
    refreshAll();
    showStatus(QStringLiteral("读者“%1”已登记").arg(name), true);
}

void MainWindow::borrowBook()
{
    if (m_borrowCopy->currentData().toInt() <= 0) {
        QMessageBox::warning(this, QStringLiteral("无法办理借阅"),
                             QStringLiteral("当前没有可借的实体册"));
        return;
    }
    QString error;
    if (!m_circulationService.borrowBook(
            m_session, m_borrowReaderNo->text(),
            m_borrowCopy->currentData().toInt(), &error)) {
        QMessageBox::warning(this, QStringLiteral("无法办理借阅"), error);
        return;
    }
    m_borrowReaderNo->clear();
    refreshAll();
    showStatus(QStringLiteral("借阅办理成功"), true);
}

void MainWindow::returnSelectedBook()
{
    const int row = m_activeLoansTable->currentRow();
    if (row < 0) {
        QMessageBox::information(this, QStringLiteral("归还图书"),
                                 QStringLiteral("请先在借阅列表中选择一条记录"));
        return;
    }
    const int loanId =
        m_activeLoansTable->item(row, 0)->data(Qt::UserRole).toInt();
    QString error;
    if (!m_circulationService.returnBook(m_session, loanId, &error)) {
        QMessageBox::warning(this, QStringLiteral("无法归还"), error);
        return;
    }
    refreshAll();
    showStatus(QStringLiteral("归还办理成功，实体册已恢复可借"), true);
}

void MainWindow::exportLoans()
{
    const QString fileName = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出借阅流水"),
        QStringLiteral("借阅流水-%1.csv")
            .arg(QDate::currentDate().toString(QStringLiteral("yyyyMMdd"))),
        QStringLiteral("CSV 文件 (*.csv)"));
    if (fileName.isEmpty()) {
        return;
    }
    QString error;
    if (!m_statisticsService.exportCsv(fileName, &error)) {
        QMessageBox::warning(this, QStringLiteral("导出失败"), error);
        return;
    }
    showStatus(QStringLiteral("借阅流水已导出"), true);
}

void MainWindow::refreshAll()
{
    refreshDashboard();
    refreshBooks();
    refreshCopies();
    refreshCirculation();
    refreshReaders();
    refreshStatistics();
}

void MainWindow::refreshDashboard()
{
    QString error;
    const DashboardStats stats = m_statisticsService.dashboard(&error);
    m_titlesStat->setText(QString::number(stats.titles));
    m_availableStat->setText(QString::number(stats.availableCopies));
    m_activeLoansStat->setText(QString::number(stats.activeLoans));
    m_overdueStat->setText(QString::number(stats.overdueLoans));

    const auto loans = m_circulationService.activeLoans(&error);
    m_dashboardLoans->setRowCount(loans.size());
    for (int row = 0; row < loans.size(); ++row) {
        const auto &loan = loans.at(row);
        m_dashboardLoans->setItem(
            row, 0, new QTableWidgetItem(loan.readerName));
        m_dashboardLoans->setItem(
            row, 1, new QTableWidgetItem(loan.readerNo));
        m_dashboardLoans->setItem(
            row, 2, new QTableWidgetItem(loan.title));
        m_dashboardLoans->setItem(
            row, 3, new QTableWidgetItem(loan.copyCode));
        m_dashboardLoans->setItem(
            row, 4, new QTableWidgetItem(shortDate(loan.dueAt)));
        auto *statusItem = new QTableWidgetItem(
            loan.overdue ? QStringLiteral("已逾期") : QStringLiteral("借阅中"));
        if (loan.overdue) {
            statusItem->setForeground(QColor(QStringLiteral("#B43838")));
        }
        m_dashboardLoans->setItem(row, 5, statusItem);
    }
}

void MainWindow::refreshBooks()
{
    QString error;
    const QString currentCategory = m_bookCategory->currentText();
    m_bookCategory->clear();
    m_bookCategory->addItems(m_bookService.categories(&error));
    const int categoryIndex = m_bookCategory->findText(currentCategory);
    if (categoryIndex >= 0) {
        m_bookCategory->setCurrentIndex(categoryIndex);
    }

    const auto books = m_bookService.books(&error);
    m_booksTable->setRowCount(books.size());
    for (int row = 0; row < books.size(); ++row) {
        const auto &book = books.at(row);
        m_booksTable->setItem(row, 0, new QTableWidgetItem(book.title));
        m_booksTable->setItem(row, 1, new QTableWidgetItem(book.author));
        m_booksTable->setItem(row, 2, new QTableWidgetItem(book.isbn));
        m_booksTable->setItem(row, 3, new QTableWidgetItem(book.category));
        m_booksTable->setItem(
            row, 4, new QTableWidgetItem(QString::number(book.totalCopies)));
        auto *availableItem =
            new QTableWidgetItem(QString::number(book.availableCopies));
        if (book.availableCopies == 0) {
            availableItem->setForeground(QColor(QStringLiteral("#B43838")));
        }
        m_booksTable->setItem(row, 5, availableItem);
    }
    filterBooks(m_bookSearch->text());
}

void MainWindow::refreshCopies()
{
    QString error;
    const int previousCopyId = m_borrowCopy->currentData().toInt();
    m_borrowCopy->clear();
    const auto copies = m_bookService.availableCopies(&error);
    for (const auto &copy : copies) {
        m_borrowCopy->addItem(
            QStringLiteral("%1  ·  %2 / %3")
                .arg(copy.code, copy.title, copy.author),
            copy.id);
    }
    const int previousIndex = m_borrowCopy->findData(previousCopyId);
    if (previousIndex >= 0) {
        m_borrowCopy->setCurrentIndex(previousIndex);
    }
    if (copies.isEmpty()) {
        m_borrowCopy->addItem(QStringLiteral("暂无可借实体册"), 0);
    }
}

void MainWindow::refreshCirculation()
{
    QString error;
    const auto loans = m_circulationService.activeLoans(&error);
    m_activeLoansTable->setRowCount(loans.size());
    for (int row = 0; row < loans.size(); ++row) {
        const auto &loan = loans.at(row);
        auto *readerItem = new QTableWidgetItem(loan.readerName);
        readerItem->setData(Qt::UserRole, loan.loanId);
        m_activeLoansTable->setItem(row, 0, readerItem);
        m_activeLoansTable->setItem(
            row, 1, new QTableWidgetItem(loan.readerNo));
        m_activeLoansTable->setItem(
            row, 2, new QTableWidgetItem(loan.title));
        m_activeLoansTable->setItem(
            row, 3, new QTableWidgetItem(loan.copyCode));
        m_activeLoansTable->setItem(
            row, 4, new QTableWidgetItem(shortDate(loan.borrowedAt)));
        m_activeLoansTable->setItem(
            row, 5, new QTableWidgetItem(shortDate(loan.dueAt)));
        auto *statusItem = new QTableWidgetItem(
            loan.overdue ? QStringLiteral("已逾期") : QStringLiteral("借阅中"));
        if (loan.overdue) {
            statusItem->setForeground(QColor(QStringLiteral("#B43838")));
        }
        m_activeLoansTable->setItem(row, 6, statusItem);
    }
}

void MainWindow::refreshReaders()
{
    QString error;
    const QString previousType = m_readerType->currentText();
    m_readerType->clear();
    m_readerType->addItems(m_readerService.readerTypes(&error));
    const int typeIndex = m_readerType->findText(previousType);
    if (typeIndex >= 0) {
        m_readerType->setCurrentIndex(typeIndex);
    }

    const auto readers = m_readerService.readers(&error);
    m_readersTable->setRowCount(readers.size());
    for (int row = 0; row < readers.size(); ++row) {
        const auto &reader = readers.at(row);
        m_readersTable->setItem(
            row, 0, new QTableWidgetItem(reader.readerNo));
        m_readersTable->setItem(
            row, 1, new QTableWidgetItem(reader.name));
        m_readersTable->setItem(
            row, 2, new QTableWidgetItem(reader.phone));
        m_readersTable->setItem(
            row, 3, new QTableWidgetItem(reader.type));
        m_readersTable->setItem(
            row, 4, new QTableWidgetItem(QString::number(reader.activeLoans)));
        m_readersTable->setItem(
            row, 5, new QTableWidgetItem(
                         reader.enabled ? QStringLiteral("正常")
                                        : QStringLiteral("停用")));
    }
}

void MainWindow::refreshStatistics()
{
    QString error;
    const DashboardStats stats = m_statisticsService.dashboard(&error);
    const auto history = m_statisticsService.loanHistory(500, &error);
    m_reportTitles->setText(QString::number(stats.titles));
    m_reportReaders->setText(QString::number(stats.readers));
    m_reportLoans->setText(QString::number(history.size()));
    m_reportOverdue->setText(QString::number(stats.overdueLoans));

    m_historyTable->setRowCount(history.size());
    for (int row = 0; row < history.size(); ++row) {
        const auto &loan = history.at(row);
        m_historyTable->setItem(
            row, 0, new QTableWidgetItem(loan.readerNo));
        m_historyTable->setItem(
            row, 1, new QTableWidgetItem(loan.readerName));
        m_historyTable->setItem(
            row, 2, new QTableWidgetItem(loan.copyCode));
        m_historyTable->setItem(
            row, 3, new QTableWidgetItem(loan.title));
        m_historyTable->setItem(
            row, 4, new QTableWidgetItem(shortDate(loan.borrowedAt)));
        m_historyTable->setItem(
            row, 5, new QTableWidgetItem(shortDate(loan.dueAt)));
        m_historyTable->setItem(
            row, 6, new QTableWidgetItem(
                         loan.returnedAt.isValid()
                             ? shortDate(loan.returnedAt)
                             : QStringLiteral("未归还")));
    }
}

void MainWindow::filterBooks(const QString &text)
{
    const QString needle = text.trimmed();
    for (int row = 0; row < m_booksTable->rowCount(); ++row) {
        bool match = needle.isEmpty();
        for (int column = 0; column < 3 && !match; ++column) {
            match = m_booksTable->item(row, column)
                    && m_booksTable->item(row, column)->text().contains(
                        needle, Qt::CaseInsensitive);
        }
        m_booksTable->setRowHidden(row, !match);
    }
}

void MainWindow::setPageTitle(int pageIndex)
{
    static const QStringList titles = {
        QStringLiteral("工作台"), QStringLiteral("图书与库存"),
        QStringLiteral("借还管理"), QStringLiteral("读者管理"),
        QStringLiteral("统计报表")
    };
    if (pageIndex >= 0 && pageIndex < titles.size()) {
        m_pageTitle->setText(titles.at(pageIndex));
    }
}

void MainWindow::showStatus(const QString &message, bool success)
{
    statusBar()->setProperty("success", success);
    statusBar()->style()->unpolish(statusBar());
    statusBar()->style()->polish(statusBar());
    statusBar()->showMessage(message, 5000);
}
