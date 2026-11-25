#include "mainwindow.h"
#include "windowwidgets.h"
#include "../library-backend-b/src/auth/permissionservice.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QVBoxLayout>

using namespace WindowWidgets;

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

bool MainWindow::openDemoSession()
{
    m_usernameEdit->setText(QStringLiteral("admin"));
    m_passwordEdit->setText(QStringLiteral("admin123"));
    handleLogin();
    return m_rootStack->currentIndex() == 1;
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
        QStringLiteral("课程演示版 · Qt Widgets + MySQL"),
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
    const struct {
        QString title;
        QStyle::StandardPixmap icon;
        QWidget *(MainWindow::*build)();
    } pages[] = {
        {QStringLiteral("工作台"), QStyle::SP_DesktopIcon, &MainWindow::buildDashboardPage},
        {QStringLiteral("图书与库存"), QStyle::SP_DirOpenIcon, &MainWindow::buildBooksPage},
        {QStringLiteral("借还管理"), QStyle::SP_BrowserReload, &MainWindow::buildCirculationPage},
        {QStringLiteral("读者管理"), QStyle::SP_FileDialogDetailedView, &MainWindow::buildReadersPage},
        {QStringLiteral("统计报表"), QStyle::SP_FileDialogInfoView, &MainWindow::buildStatisticsPage},
        {QStringLiteral("预约管理"), QStyle::SP_FileDialogListView, &MainWindow::buildReservationsPage},
    };
    m_pages = new QStackedWidget;
    for (const auto &page : pages) {
        auto *item = new QListWidgetItem(style()->standardIcon(page.icon), page.title);
        item->setSizeHint(QSize(0, 46));
        m_navigation->addItem(item);
        m_pages->addWidget((this->*page.build)());
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
    m_pageTitle = makeLabel(QString(), QStringLiteral("pageTitle"));
    m_userLabel = makeLabel(QString(), QStringLiteral("userChip"));
    headerLayout->addWidget(m_pageTitle);
    headerLayout->addStretch();
    headerLayout->addWidget(m_userLabel);

    mainLayout->addWidget(header);
    mainLayout->addWidget(m_pages, 1);
    shellLayout->addWidget(sidebar);
    shellLayout->addWidget(mainArea, 1);

    connect(m_navigation, &QListWidget::currentRowChanged,
            m_pages, &QStackedWidget::setCurrentIndex);
    connect(m_navigation, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *item) {
        m_pageTitle->setText(item ? item->text() : QString());
    });
    connect(logoutButton, &QPushButton::clicked,
            this, &MainWindow::handleLogout);
    m_navigation->setCurrentRow(0);
    return shell;
}

void MainWindow::handleLogin()
{
    m_loginError->clear();
    const lib::Result result = m_authService.login(
        m_usernameEdit->text(), m_passwordEdit->text());
    if (!result.ok()) {
        m_loginError->setText(result.message);
        m_passwordEdit->selectAll();
        m_passwordEdit->setFocus();
        return;
    }

    const QString roleName = result.data.value(
        QStringLiteral("role")).toString();
    const lib::Role role = roleName == QStringLiteral("admin")
        ? lib::Role::Admin
        : (roleName == QStringLiteral("librarian")
               ? lib::Role::Librarian
               : lib::Role::Reader);
    const lib::OperatorContext context{
        result.data.value(QStringLiteral("user_id")).toLongLong(),
        result.data.value(QStringLiteral("user_name")).toString(),
        role,
        result.data.value(QStringLiteral("authenticated")).toBool(),
        result.data.value(QStringLiteral("session_generation")).toLongLong()};

    if (!context.isStaff()) {
        m_loginError->setText(QStringLiteral("馆务工作台仅供管理员和馆员使用"));
        return;
    }

    m_session = UserSession::fromOperatorContext(context);
    m_session.displayName = result.data.value(
        QStringLiteral("display_name")).toString();
    if (!m_pages) {
        m_rootStack->addWidget(buildShellPage());
    }
    m_userLabel->setText(QStringLiteral("%1  ·  %2")
                             .arg(m_session.displayName,
                                  m_session.isAdmin() ? QStringLiteral("管理员")
                                                      : QStringLiteral("馆员")));
    m_rootStack->setCurrentIndex(1);
    m_navigation->setCurrentRow(0);
    m_passwordEdit->clear();
    refreshAll();
    showStatus(QStringLiteral("登录成功，欢迎进入馆务工作台"), true);
}

void MainWindow::handleLogout()
{
    lib::OperatorContext context = operatorContext();
    m_authService.logout(context);
    m_session.clear();
    m_usernameEdit->setText(QStringLiteral("admin"));
    m_passwordEdit->setText(QStringLiteral("admin123"));
    m_loginError->clear();
    m_rootStack->setCurrentIndex(0);
    m_usernameEdit->setFocus();
}

bool MainWindow::checkPermission(Permission permission)
{
    const lib::Result result = PermissionService::requirePermission(
        operatorContext(), permission);
    if (result.ok()) {
        return true;
    }
    showStatus(result.message, false);
    return false;
}

void MainWindow::refreshAll()
{
    refreshDashboard();
    refreshBooks();
    refreshCopies();
    refreshCirculation();
    refreshReaders();
    refreshStatistics();
    refreshReservations();
}

void MainWindow::showStatus(const QString &message, bool success)
{
    statusBar()->setProperty("success", success);
    statusBar()->style()->unpolish(statusBar());
    statusBar()->style()->polish(statusBar());
    statusBar()->showMessage(message, 5000);
}
