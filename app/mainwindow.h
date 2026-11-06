#pragma once

#include "../auth/authservice.h"
#include "../books/bookservice.h"
#include "../circulation/circulationservice.h"
#include "../readers/readerservice.h"
#include "../statistics/statisticsservice.h"

#include <QMainWindow>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QSpinBox;
class QStackedWidget;
class QTableWidget;
class QWidget;

class MainWindow : public QMainWindow
{
public:
    explicit MainWindow(QWidget *parent = nullptr);
    void openDemoSession();

private:
    QWidget *buildLoginPage();
    QWidget *buildShellPage();
    QWidget *buildDashboardPage();
    QWidget *buildBooksPage();
    QWidget *buildCirculationPage();
    QWidget *buildReadersPage();
    QWidget *buildStatisticsPage();

    void handleLogin();
    void handleLogout();
    void addBook();
    void addReader();
    void borrowBook();
    void returnSelectedBook();
    void exportLoans();
    void refreshAll();
    void refreshDashboard();
    void refreshBooks();
    void refreshCopies();
    void refreshCirculation();
    void refreshReaders();
    void refreshStatistics();
    void filterBooks(const QString &text);
    void setPageTitle(int pageIndex);
    void showStatus(const QString &message, bool success);

    AuthService m_authService;
    BookService m_bookService;
    ReaderService m_readerService;
    CirculationService m_circulationService;
    StatisticsService m_statisticsService;
    UserSession m_session;

    QStackedWidget *m_rootStack = nullptr;
    QListWidget *m_navigation = nullptr;
    QStackedWidget *m_pages = nullptr;
    QLabel *m_pageTitle = nullptr;
    QLabel *m_userLabel = nullptr;
    QLineEdit *m_usernameEdit = nullptr;
    QLineEdit *m_passwordEdit = nullptr;
    QLabel *m_loginError = nullptr;

    QLabel *m_titlesStat = nullptr;
    QLabel *m_availableStat = nullptr;
    QLabel *m_activeLoansStat = nullptr;
    QLabel *m_overdueStat = nullptr;
    QTableWidget *m_dashboardLoans = nullptr;

    QLineEdit *m_bookSearch = nullptr;
    QLineEdit *m_bookTitle = nullptr;
    QLineEdit *m_bookAuthor = nullptr;
    QLineEdit *m_bookIsbn = nullptr;
    QComboBox *m_bookCategory = nullptr;
    QSpinBox *m_bookCopyCount = nullptr;
    QTableWidget *m_booksTable = nullptr;

    QLineEdit *m_borrowReaderNo = nullptr;
    QComboBox *m_borrowCopy = nullptr;
    QTableWidget *m_activeLoansTable = nullptr;

    QLineEdit *m_readerNo = nullptr;
    QLineEdit *m_readerName = nullptr;
    QLineEdit *m_readerPhone = nullptr;
    QComboBox *m_readerType = nullptr;
    QTableWidget *m_readersTable = nullptr;

    QLabel *m_reportTitles = nullptr;
    QLabel *m_reportReaders = nullptr;
    QLabel *m_reportLoans = nullptr;
    QLabel *m_reportOverdue = nullptr;
    QTableWidget *m_historyTable = nullptr;
};
