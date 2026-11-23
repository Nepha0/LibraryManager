#include "auth/authservice.h"
#include "auth/permissionservice.h"
#include "books/bookservice.h"
#include "circulation/circulationservice.h"
#include "common/database.h"
#include "common/paramservice.h"
#include "common/operationlogservice.h"
#include "auth/userservice.h"
#include "readers/readerservice.h"
#include "statistics/statisticsservice.h"

#include <QCoreApplication>
#include <QDebug>
#include <QStringList>
#include <QSqlError>
#include <QSqlQuery>

namespace {

bool check(bool condition, const QString &name)
{
    if (condition) {
        qInfo().noquote() << "[PASS]" << name;
    } else {
        qCritical().noquote() << "[FAIL]" << name;
    }
    return condition;
}

}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    bool allPassed = true;
    QString error;

    if (!Database::initializeSmokeTest(&error)) {
        qCritical().noquote()
        << "[FAIL] Qt 通过 QODBC 连接 MySQL：" << error;
        return 1;
    }

    qInfo().noquote() << "[PASS] Qt 通过 QODBC 连接 MySQL";

    const auto admin = AuthService::login(
        QStringLiteral("admin"),
        QStringLiteral("admin123"),
        &error);

    allPassed &= check(
        admin.has_value(),
        QStringLiteral("演示管理员登录成功"));

    if (!admin.has_value()) {
        qCritical().noquote() << "登录错误：" << error;
        Database::close();
        return 2;
    }

    allPassed &= check(
        admin->isAuthenticated() && admin->isAdmin(),
        QStringLiteral("管理员会话状态正确"));

    allPassed &= check(
        PermissionService::hasPermission(
            *admin, Permission::ManageParams),
        QStringLiteral("管理员具有参数管理权限"));

    const auto wrongPassword = AuthService::login(
        QStringLiteral("admin"),
        QStringLiteral("wrong-password"),
        &error);

    allPassed &= check(
        !wrongPassword.has_value()
            && error == QStringLiteral("账号或密码错误"),
        QStringLiteral("错误密码被拒绝"));

    const QString temporarySuffix =
        QString::number(
            QDateTime::currentMSecsSinceEpoch());

    const QString missingUsername =
        QStringLiteral("missing-user-%1")
            .arg(temporarySuffix);

    const auto missingUser = AuthService::login(
        missingUsername,
        QStringLiteral("any-password"),
        &error);

    allPassed &= check(
        !missingUser.has_value()
            && error == QStringLiteral("账号或密码错误"),
        QStringLiteral("不存在账号使用统一错误提示"));

    const QString disabledUsername =
        QStringLiteral("disabled-user-%1")
            .arg(temporarySuffix);
    const QString disabledPassword =
        QStringLiteral("disabled-test-password");
    const QString disabledSalt =
        QStringLiteral("disabled-test-salt");

    QSqlQuery createDisabledUser(
        Database::connection());
    createDisabledUser.prepare(QStringLiteral(
        "INSERT INTO users ("
        "username, display_name, password_hash, "
        "salt, role, active) "
        "VALUES (?, ?, SHA2(CONCAT(?, ?), 256), "
        "?, 'reader', 0)"));
    createDisabledUser.addBindValue(disabledUsername);
    createDisabledUser.addBindValue(
        QStringLiteral("停用测试账号"));
    createDisabledUser.addBindValue(disabledSalt);
    createDisabledUser.addBindValue(disabledPassword);
    createDisabledUser.addBindValue(disabledSalt);

    const bool disabledUserCreated =
        createDisabledUser.exec();

    allPassed &= check(
        disabledUserCreated,
        QStringLiteral("创建临时停用账号"));

    if (!disabledUserCreated) {
        qCritical().noquote()
        << "创建停用账号失败："
        << createDisabledUser.lastError().text();
    } else {
        const auto disabledLogin = AuthService::login(
            disabledUsername,
            disabledPassword,
            &error);

        allPassed &= check(
            !disabledLogin.has_value()
                && error
                       == QStringLiteral(
                           "账号已停用，请联系管理员"),
            QStringLiteral("停用账号登录被拒绝"));

        QSqlQuery deleteDisabledUser(
            Database::connection());
        deleteDisabledUser.prepare(QStringLiteral(
            "DELETE FROM users WHERE username = ?"));
        deleteDisabledUser.addBindValue(
            disabledUsername);

        allPassed &= check(
            deleteDisabledUser.exec(),
            QStringLiteral("清理临时停用账号"));
    }

    ParamService params;
    const QStringList integerKeys = {
        QStringLiteral("borrow_period_days"),
        QStringLiteral("borrow_limit"),
        QStringLiteral("max_renew_times"),
        QStringLiteral("reservation_hold_days"),
        QStringLiteral("renew_period_days")
    };

    for (const QString &key : integerKeys) {
        const auto value = params.getIntParam(key, &error);
        allPassed &= check(
            value.has_value(),
            QStringLiteral("读取系统参数：%1").arg(key));

        if (!value.has_value()) {
            qCritical().noquote() << "参数错误：" << error;
        }
    }

    const QString roleTestPassword =
        QStringLiteral("role-test-password");
    const QString readerRoleUsername =
        QStringLiteral("role-reader-%1")
            .arg(temporarySuffix);
    const QString librarianRoleUsername =
        QStringLiteral("role-librarian-%1")
            .arg(temporarySuffix);

    auto createRoleTestAccount =
        [&](const QString &username,
            const QString &displayName,
            const QString &role) {
            const QString salt =
                QStringLiteral("salt-%1")
                    .arg(username);

            QSqlQuery query(
                Database::connection());
            query.prepare(QStringLiteral(
                "INSERT INTO users ("
                "username, display_name, password_hash, "
                "salt, role, active) "
                "VALUES (?, ?, "
                "SHA2(CONCAT(?, ?), 256), "
                "?, ?, 1)"));
            query.addBindValue(username);
            query.addBindValue(displayName);
            query.addBindValue(salt);
            query.addBindValue(roleTestPassword);
            query.addBindValue(salt);
            query.addBindValue(role);
            return query.exec();
        };

    const bool readerRoleAccountCreated =
        createRoleTestAccount(
            readerRoleUsername,
            QStringLiteral("真实测试读者"),
            QStringLiteral("reader"));

    const bool librarianRoleAccountCreated =
        createRoleTestAccount(
            librarianRoleUsername,
            QStringLiteral("真实测试图书管理员"),
            QStringLiteral("librarian"));

    allPassed &= check(
        readerRoleAccountCreated
            && librarianRoleAccountCreated,
        QStringLiteral("创建真实角色测试账号"));

    const auto readerRoleLogin =
        AuthService::login(
            readerRoleUsername,
            roleTestPassword,
            &error);

    const auto librarianRoleLogin =
        AuthService::login(
            librarianRoleUsername,
            roleTestPassword,
            &error);

    allPassed &= check(
        readerRoleLogin.has_value()
            && librarianRoleLogin.has_value(),
        QStringLiteral("读者和馆员真实登录成功"));

    if (!readerRoleLogin.has_value()
        || !librarianRoleLogin.has_value()) {
        Database::close();
        return 3;
    }

    const UserSession readerSession =
        *readerRoleLogin;
    const UserSession librarianSession =
        *librarianRoleLogin;
    const QVector<Permission> allPermissions = {
        Permission::ViewBooks,
        Permission::ManageBooks,
        Permission::ViewReaders,
        Permission::ManageReaders,
        Permission::ManageCirculation,
        Permission::ManageUsers,
        Permission::ManageParams,
        Permission::ViewOperationLogs,
        Permission::ViewPersonalCenter
    };

    bool adminHasAllPermissions = true;
    for (Permission permission : allPermissions) {
        if (!PermissionService::hasPermission(
                *admin, permission)) {
            adminHasAllPermissions = false;
            break;
        }
    }

    allPassed &= check(
        adminHasAllPermissions,
        QStringLiteral("管理员具有全部权限"));

    const bool librarianPermissionsCorrect =
        PermissionService::hasPermission(
            librarianSession,
            Permission::ManageBooks)
        && PermissionService::hasPermission(
            librarianSession,
            Permission::ManageReaders)
        && PermissionService::hasPermission(
            librarianSession,
            Permission::ManageCirculation)
        && !PermissionService::hasPermission(
            librarianSession,
            Permission::ManageUsers)
        && !PermissionService::hasPermission(
            librarianSession,
            Permission::ManageParams)
        && !PermissionService::hasPermission(
            librarianSession,
            Permission::ViewOperationLogs);

    allPassed &= check(
        librarianPermissionsCorrect,
        QStringLiteral("图书管理员权限矩阵正确"));

    const bool readerPermissionsCorrect =
        PermissionService::hasPermission(
            readerSession,
            Permission::ViewBooks)
        && PermissionService::hasPermission(
            readerSession,
            Permission::ViewPersonalCenter)
        && !PermissionService::hasPermission(
            readerSession,
            Permission::ManageBooks)
        && !PermissionService::hasPermission(
            readerSession,
            Permission::ManageReaders)
        && !PermissionService::hasPermission(
            readerSession,
            Permission::ManageCirculation)
        && !PermissionService::hasPermission(
            readerSession,
            Permission::ManageUsers)
        && !PermissionService::hasPermission(
            readerSession,
            Permission::ManageParams)
        && !PermissionService::hasPermission(
            readerSession,
            Permission::ViewOperationLogs);

    allPassed &= check(
        readerPermissionsCorrect,
        QStringLiteral("读者权限矩阵正确"));

    UserSession anonymousSession;
    QString anonymousError;

    const bool anonymousAllowed =
        PermissionService::requirePermission(
            anonymousSession,
            Permission::ViewBooks,
            &anonymousError);

    allPassed &= check(
        !anonymousAllowed
            && anonymousError
                   == QStringLiteral("请先登录"),
        QStringLiteral("未登录用户调用受限操作被拒绝"));

    BookService bookService;

    const QVector<BookInfo> adminBooks =
        bookService.books(
            *admin,
            &error);

    allPassed &= check(
        error.isEmpty(),
        QStringLiteral(
            "管理员可以查询图书列表"));

    const QVector<AvailableCopyInfo>
        librarianAvailableCopies =
        bookService.availableCopies(
            librarianSession,
            &error);

    allPassed &= check(
        error.isEmpty(),
        QStringLiteral(
            "馆员可以查询可借实体册"));

    const QStringList readerCategories =
        bookService.categories(
            readerSession,
            &error);

    const QVector<BookInfo> readerBooks =
        bookService.books(
            readerSession,
            &error);

    allPassed &= check(
        error.isEmpty()
            && !readerCategories.isEmpty()
            && !readerBooks.isEmpty(),
        QStringLiteral(
            "读者可以查询图书和分类"));

    const QVector<BookInfo> anonymousBooks =
        bookService.books(
            anonymousSession,
            &error);

    allPassed &= check(
        anonymousBooks.isEmpty()
            && error
                   == QStringLiteral("请先登录"),
        QStringLiteral(
            "未登录用户查询图书列表被拒绝"));

    Q_UNUSED(adminBooks);
    Q_UNUSED(librarianAvailableCopies);

    UserService userService;

    const QString testUsername =
        QStringLiteral("user-smoke-%1")
            .arg(temporarySuffix);
    const QString updatedUsername =
        QStringLiteral("user-updated-%1")
            .arg(temporarySuffix);
    const QString testPassword =
        QStringLiteral("initial-password");
    const QString resetPassword =
        QStringLiteral("reset-password");

    const bool userCreated = userService.addUser(
        *admin,
        testUsername,
        QStringLiteral("测试用户"),
        testPassword,
        QStringLiteral("reader"),
        &error);

    allPassed &= check(
        userCreated,
        QStringLiteral("管理员新增用户成功"));

    const bool duplicateCreated =
        userService.addUser(
            *admin,
            testUsername,
            QStringLiteral("重复测试用户"),
            testPassword,
            QStringLiteral("reader"),
            &error);

    allPassed &= check(
        !duplicateCreated
            && error == QStringLiteral("账号已存在"),
        QStringLiteral("重复账号被拒绝"));

    const bool readerCreatedUser =
        userService.addUser(
            readerSession,
            QStringLiteral("reader-attempt-%1")
                .arg(temporarySuffix),
            QStringLiteral("越权创建"),
            testPassword,
            QStringLiteral("reader"),
            &error);

    allPassed &= check(
        !readerCreatedUser
            && error == QStringLiteral("无权限"),
        QStringLiteral("读者新增账号被拒绝"));

    UserQuery userQuery;
    userQuery.keyword = testUsername;
    userQuery.page = 1;
    userQuery.pageSize = 10;

    const UserPage userPage =
        userService.listUsers(
            *admin, userQuery, &error);

    qlonglong testUserId = 0;
    for (const UserInfo &item : userPage.items) {
        if (item.username == testUsername) {
            testUserId = item.id;
            break;
        }
    }

    allPassed &= check(
        testUserId > 0,
        QStringLiteral("管理员分页查询到新增用户"));

    const auto createdUser =
        userService.getUser(
            *admin, testUserId, &error);

    allPassed &= check(
        createdUser.has_value()
            && createdUser->username == testUsername
            && createdUser->role
                   == QStringLiteral("reader")
            && createdUser->enabled,
        QStringLiteral("用户详情数据正确"));

    const bool userUpdated =
        userService.updateUser(
            *admin,
            testUserId,
            updatedUsername,
            QStringLiteral("更新后的测试用户"),
            &error);

    allPassed &= check(
        userUpdated,
        QStringLiteral("管理员编辑用户成功"));

    const bool roleAssigned =
        userService.assignRole(
            *admin,
            testUserId,
            QStringLiteral("librarian"),
            &error);

    allPassed &= check(
        roleAssigned,
        QStringLiteral("管理员分配角色成功"));

    const bool selfDemotionAllowed =
        userService.assignRole(
            *admin,
            admin->userId,
            QStringLiteral("reader"),
            &error);

    allPassed &= check(
        !selfDemotionAllowed
            && error
                   == QStringLiteral(
                       "不能取消当前登录账号的管理员角色"),
        QStringLiteral("管理员不能取消自己的角色"));

    const bool passwordReset =
        userService.resetPassword(
            *admin,
            testUserId,
            resetPassword,
            &error);

    allPassed &= check(
        passwordReset,
        QStringLiteral("管理员重置密码成功"));

    const auto oldPasswordLogin =
        AuthService::login(
            updatedUsername,
            testPassword,
            &error);

    allPassed &= check(
        !oldPasswordLogin.has_value(),
        QStringLiteral("旧密码在重置后失效"));

    const auto resetPasswordLogin =
        AuthService::login(
            updatedUsername,
            resetPassword,
            &error);

    allPassed &= check(
        resetPasswordLogin.has_value()
            && resetPasswordLogin->isLibrarian(),
        QStringLiteral(
            "新密码登录及新角色生效"));

    const bool readerChangedUserStatus =
        userService.setUserStatus(
            readerSession,
            testUserId,
            false,
            &error);

    allPassed &= check(
        !readerChangedUserStatus
            && error == QStringLiteral("无权限"),
        QStringLiteral("读者角色修改账号状态被拒绝"));

    const bool selfDisabled =
        userService.setUserStatus(
            *admin,
            admin->userId,
            false,
            &error);

    allPassed &= check(
        !selfDisabled
            && error
                   == QStringLiteral(
                       "不能停用当前登录账号"),
        QStringLiteral("管理员不能停用当前登录账号"));

    const bool testUserDisabled =
        userService.setUserStatus(
            *admin,
            testUserId,
            false,
            &error);

    allPassed &= check(
        testUserDisabled,
        QStringLiteral("管理员停用用户成功"));

    const auto disabledAccountLogin =
        AuthService::login(
            updatedUsername,
            resetPassword,
            &error);

    allPassed &= check(
        !disabledAccountLogin.has_value()
            && error
                   == QStringLiteral(
                       "账号已停用，请联系管理员"),
        QStringLiteral("停用用户不能登录"));

    QString staleSessionError;
    const bool staleSessionAllowed =
        resetPasswordLogin.has_value()
        && PermissionService::requirePermission(
            *resetPasswordLogin,
            Permission::ViewBooks,
            &staleSessionError);

    allPassed &= check(
        !staleSessionAllowed
            && staleSessionError
                   == QStringLiteral(
                       "登录状态已失效，请重新登录"),
        QStringLiteral(
            "账号停用后已有会话立即失效"));

    const auto disabledUserInfo =
        userService.getUser(
            *admin, testUserId, &error);

    allPassed &= check(
        disabledUserInfo.has_value()
            && !disabledUserInfo->enabled,
        QStringLiteral("用户停用状态已保存"));

    const bool testUserEnabled =
        userService.setUserStatus(
            *admin,
            testUserId,
            true,
            &error);

    allPassed &= check(
        testUserEnabled,
        QStringLiteral("管理员重新启用用户成功"));

    const auto enabledAccountLogin =
        AuthService::login(
            updatedUsername,
            resetPassword,
            &error);

    allPassed &= check(
        enabledAccountLogin.has_value(),
        QStringLiteral("重新启用用户后可以登录"));

    QString reenabledOldSessionError;
    const bool reenabledOldSessionAllowed =
        resetPasswordLogin.has_value()
        && PermissionService::requirePermission(
            *resetPasswordLogin,
            Permission::ViewBooks,
            &reenabledOldSessionError);

    allPassed &= check(
        !reenabledOldSessionAllowed
            && reenabledOldSessionError
                   == QStringLiteral(
                       "登录状态已失效，请重新登录"),
        QStringLiteral(
            "重新启用后旧会话仍然失效"));

    QString reenabledNewSessionError;
    const bool reenabledNewSessionAllowed =
        enabledAccountLogin.has_value()
        && PermissionService::requirePermission(
            *enabledAccountLogin,
            Permission::ViewBooks,
            &reenabledNewSessionError);

    allPassed &= check(
        reenabledNewSessionAllowed
            && reenabledNewSessionError.isEmpty(),
        QStringLiteral(
            "重新启用后的新会话有效"));

    OperationLogService statusLogService;
    OperationLogQuery statusLogQuery;
    statusLogQuery.actionType =
        QStringLiteral("USER_STATUS_UPDATE");
    statusLogQuery.page = 1;
    statusLogQuery.pageSize = 50;

    const OperationLogPage statusLogPage =
        statusLogService.listLogs(
            *admin, statusLogQuery, &error);

    bool foundUserStatusLog = false;
    for (const OperationLogInfo &item :
         statusLogPage.items) {
        if (item.targetId
            == QString::number(testUserId)) {
            foundUserStatusLog = true;
            break;
        }
    }

    allPassed &= check(
        foundUserStatusLog,
        QStringLiteral("用户状态修改自动写入操作日志"));

    OperationLogService userLogService;
    OperationLogQuery userLogQuery;
    userLogQuery.actionType =
        QStringLiteral("USER_CREATE");
    userLogQuery.page = 1;
    userLogQuery.pageSize = 50;

    const OperationLogPage userLogPage =
        userLogService.listLogs(
            *admin, userLogQuery, &error);

    bool foundUserCreateLog = false;
    for (const OperationLogInfo &item :
         userLogPage.items) {
        if (item.targetId
            == QString::number(testUserId)) {
            foundUserCreateLog = true;
            break;
        }
    }

    allPassed &= check(
        foundUserCreateLog,
        QStringLiteral("用户新增自动写入操作日志"));

    QSqlQuery cleanupUser(Database::connection());
    cleanupUser.prepare(QStringLiteral(
        "DELETE FROM users "
        "WHERE username = ? OR username = ?"));
    cleanupUser.addBindValue(testUsername);
    cleanupUser.addBindValue(updatedUsername);

    const bool userCleaned =
        cleanupUser.exec()
        && cleanupUser.numRowsAffected() >= 1;

    allPassed &= check(
        userCleaned,
        QStringLiteral("清理临时测试用户"));

    ReaderService readerService;

    CirculationService circulationService;

    const QVector<ActiveLoanInfo> adminActiveLoans =
        circulationService.activeLoans(
            *admin,
            &error);

    allPassed &= check(
        error.isEmpty(),
        QStringLiteral(
            "管理员可以查询当前借阅记录"));

    const QVector<ActiveLoanInfo> librarianActiveLoans =
        circulationService.activeLoans(
            librarianSession,
            &error);

    allPassed &= check(
        error.isEmpty(),
        QStringLiteral(
            "馆员可以查询当前借阅记录"));

    const QVector<ActiveLoanInfo> readerActiveLoans =
        circulationService.activeLoans(
            readerSession,
            &error);

    allPassed &= check(
        readerActiveLoans.isEmpty()
            && error == QStringLiteral("无权限"),
        QStringLiteral(
            "读者查询全部借阅记录被拒绝"));

    StatisticsService statisticsService;

    const DashboardStats adminDashboard =
        statisticsService.dashboard(
            *admin,
            &error);

    allPassed &= check(
        error.isEmpty(),
        QStringLiteral(
            "管理员可以查询统计概览"));

    const DashboardStats librarianDashboard =
        statisticsService.dashboard(
            librarianSession,
            &error);

    allPassed &= check(
        error.isEmpty(),
        QStringLiteral(
            "馆员可以查询统计概览"));

    const DashboardStats readerDashboard =
        statisticsService.dashboard(
            readerSession,
            &error);

    allPassed &= check(
        error == QStringLiteral("无权限"),
        QStringLiteral(
            "读者查询全局统计概览被拒绝"));

    const QVector<LoanHistoryInfo> adminHistory =
        statisticsService.loanHistory(
            *admin,
            20,
            &error);

    allPassed &= check(
        error.isEmpty(),
        QStringLiteral(
            "管理员可以查询借阅历史"));

    const QVector<LoanHistoryInfo> librarianHistory =
        statisticsService.loanHistory(
            librarianSession,
            20,
            &error);

    allPassed &= check(
        error.isEmpty(),
        QStringLiteral(
            "馆员可以查询借阅历史"));

    const QVector<LoanHistoryInfo> readerHistory =
        statisticsService.loanHistory(
            readerSession,
            20,
            &error);

    allPassed &= check(
        readerHistory.isEmpty()
            && error == QStringLiteral("无权限"),
        QStringLiteral(
            "读者查询全部借阅历史被拒绝"));

    const bool readerExportedHistory =
        statisticsService.exportCsv(
            readerSession,
            QStringLiteral(
                "unauthorized-history.csv"),
            &error);

    allPassed &= check(
        !readerExportedHistory
            && error == QStringLiteral("无权限"),
        QStringLiteral(
            "读者导出全部借阅历史被拒绝"));

    Q_UNUSED(adminDashboard);
    Q_UNUSED(librarianDashboard);
    Q_UNUSED(readerDashboard);
    Q_UNUSED(adminHistory);
    Q_UNUSED(librarianHistory);

    Q_UNUSED(adminActiveLoans);
    Q_UNUSED(librarianActiveLoans);

    const QString testReaderNo =
        QStringLiteral("R-UPDATE-%1").arg(temporarySuffix);
    const QString updatedReaderNo =
        QStringLiteral("R-UPDATED-%1").arg(temporarySuffix);
    const QString duplicateReaderNo =
        QStringLiteral("R-DUPLICATE-%1").arg(temporarySuffix);

    const bool testReaderCreated =
        readerService.addReader(
            *admin,
            testReaderNo,
            QStringLiteral("修改前读者"),
            QStringLiteral("13000000001"),
            &error);

    const bool duplicateReaderCreated =
        readerService.addReader(
            *admin,
            duplicateReaderNo,
            QStringLiteral("占用证号读者"),
            QStringLiteral("13000000002"),
            &error);

    allPassed &= check(
        testReaderCreated && duplicateReaderCreated,
        QStringLiteral("创建读者修改测试数据"));

    const QVector<ReaderInfo> readerItems =
        readerService.readers(*admin, &error);

    const QVector<ReaderInfo> unauthorizedReaderItems =
        readerService.readers(
            readerSession, &error);

    allPassed &= check(
        unauthorizedReaderItems.isEmpty()
            && error == QStringLiteral("无权限"),
        QStringLiteral("读者角色查询读者列表被拒绝"));

    int testReaderId = 0;
    for (const ReaderInfo &item : readerItems) {
        if (item.readerNo == testReaderNo) {
            testReaderId = item.id;
            break;
        }
    }

    allPassed &= check(
        testReaderId > 0,
        QStringLiteral("查询到待读者修改记录"));

    OperationLogQuery readerCreateLogQuery;
    readerCreateLogQuery.actionType =
        QStringLiteral("READER_CREATE");
    readerCreateLogQuery.page = 1;
    readerCreateLogQuery.pageSize = 50;

    const OperationLogPage readerCreateLogPage =
        userLogService.listLogs(
            *admin, readerCreateLogQuery, &error);

    bool foundReaderCreateLog = false;
    for (const OperationLogInfo &item :
         readerCreateLogPage.items) {
        if (item.targetId
            == QString::number(testReaderId)) {
            foundReaderCreateLog = true;
            break;
        }
    }

    allPassed &= check(
        foundReaderCreateLog,
        QStringLiteral("读者新增自动写入操作日志"));

    const ReaderEligibilityResult initialEligibility =
        readerService.checkReaderEligibility(
            *admin, testReaderId, &error);

    allPassed &= check(
        initialEligibility.eligible
            && initialEligibility.readerEnabled
            && initialEligibility.accountEnabled
            && initialEligibility.activeLoans == 0
            && initialEligibility.borrowLimit > 0,
        QStringLiteral("正常读者具有借阅资格"));

    const ReaderEligibilityResult unauthorizedEligibility =
        readerService.checkReaderEligibility(
            readerSession, testReaderId, &error);

    allPassed &= check(
        !unauthorizedEligibility.eligible
            && error == QStringLiteral("无权限"),
        QStringLiteral("读者角色不能调用借阅资格管理接口"));

    const bool readerUpdated =
        readerService.updateReader(
            *admin,
            testReaderId,
            updatedReaderNo,
            QStringLiteral("修改后读者"),
            QStringLiteral("13000000003"),
            &error);

    allPassed &= check(
        readerUpdated,
        QStringLiteral("管理员修改读者成功"));

    const bool duplicateReaderUpdated =
        readerService.updateReader(
            *admin,
            testReaderId,
            duplicateReaderNo,
            QStringLiteral("重复证号测试"),
            QStringLiteral("13000000004"),
            &error);

    allPassed &= check(
        !duplicateReaderUpdated
            && error == QStringLiteral("该读者编号已存在"),
        QStringLiteral("修改读者时重复证号被拒绝"));

    const bool readerRoleUpdatedReader =
        readerService.updateReader(
            readerSession,
            testReaderId,
            updatedReaderNo,
            QStringLiteral("越权修改"),
            QStringLiteral("13000000005"),
            &error);

    allPassed &= check(
        !readerRoleUpdatedReader
            && error == QStringLiteral("无权限"),
        QStringLiteral("读者角色修改读者被拒绝"));

    const QVector<ReaderInfo> updatedReaderItems =
        readerService.readers(*admin, &error);

    bool readerUpdateStored = false;
    for (const ReaderInfo &item : updatedReaderItems) {
        if (item.id == testReaderId
            && item.readerNo == updatedReaderNo
            && item.name == QStringLiteral("修改后读者")
            && item.phone == QStringLiteral("13000000003")) {
            readerUpdateStored = true;
            break;
        }
    }

    allPassed &= check(
        readerUpdateStored,
        QStringLiteral("读者修改结果已保存"));

    ReaderQuery readerNoFilter;
    readerNoFilter.readerNo = updatedReaderNo;

    const QVector<ReaderInfo> readerNoResults =
        readerService.readers(
            *admin,
            readerNoFilter,
            &error);

    bool foundByReaderNo = false;
    for (const ReaderInfo &item : readerNoResults) {
        if (item.id == testReaderId) {
            foundByReaderNo = true;
            break;
        }
    }

    allPassed &= check(
        foundByReaderNo,
        QStringLiteral("按读者证号筛选成功"));

    ReaderQuery readerNameFilter;
    readerNameFilter.name =
        QStringLiteral("修改后读者");

    const QVector<ReaderInfo> readerNameResults =
        readerService.readers(
            *admin,
            readerNameFilter,
            &error);

    bool foundByReaderName = false;
    for (const ReaderInfo &item : readerNameResults) {
        if (item.id == testReaderId) {
            foundByReaderName = true;
            break;
        }
    }

    allPassed &= check(
        foundByReaderName,
        QStringLiteral("按读者姓名筛选成功"));

    const auto readerDetail =
        readerService.getReader(
            *admin,
            testReaderId,
            &error);

    allPassed &= check(
        readerDetail.has_value()
            && readerDetail->info.id == testReaderId
            && readerDetail->info.readerNo
                   == updatedReaderNo
            && readerDetail->info.name
                   == QStringLiteral("修改后读者")
            && readerDetail->info.enabled
            && readerDetail->totalLoans == 0
            && readerDetail->info.activeLoans == 0
            && readerDetail->returnedLoans == 0
            && readerDetail->overdueLoans == 0,
        QStringLiteral("查询单个读者详情成功"));

    const auto unauthorizedReaderDetail =
        readerService.getReader(
            readerSession,
            testReaderId,
            &error);

    allPassed &= check(
        !unauthorizedReaderDetail.has_value()
            && error == QStringLiteral("无权限"),
        QStringLiteral("读者角色查询读者详情被拒绝"));

    const auto missingReaderDetail =
        readerService.getReader(
            *admin,
            2147483647,
            &error);

    allPassed &= check(
        !missingReaderDetail.has_value()
            && error == QStringLiteral("读者不存在"),
        QStringLiteral("不存在的读者详情被正确拒绝"));

    OperationLogQuery readerUpdateLogQuery;
    readerUpdateLogQuery.actionType =
        QStringLiteral("READER_UPDATE");
    readerUpdateLogQuery.page = 1;
    readerUpdateLogQuery.pageSize = 50;

    const OperationLogPage readerUpdateLogPage =
        userLogService.listLogs(
            *admin, readerUpdateLogQuery, &error);

    bool foundReaderUpdateLog = false;
    for (const OperationLogInfo &item :
         readerUpdateLogPage.items) {
        if (item.targetId
            == QString::number(testReaderId)) {
            foundReaderUpdateLog = true;
            break;
        }
    }

    allPassed &= check(
        foundReaderUpdateLog,
        QStringLiteral("读者修改自动写入操作日志"));

    QSqlQuery availableCopyQuery(
        Database::connection());
    const bool availableCopyFound =
        availableCopyQuery.exec(QStringLiteral(
            "SELECT bc.id, bc.book_id "
            "FROM book_copies bc "
            "LEFT JOIN loans l "
            "ON l.copy_id = bc.id "
            "AND l.returned_at IS NULL "
            "WHERE l.id IS NULL "
            "ORDER BY bc.id LIMIT 1"))
        && availableCopyQuery.next();

    const int statusTestCopyId =
        availableCopyFound
            ? availableCopyQuery.value(0).toInt()
            : 0;
    const int statusTestBookId =
        availableCopyFound
            ? availableCopyQuery.value(1).toInt()
            : 0;

    QSqlQuery createStatusTestLoan(
        Database::connection());
    createStatusTestLoan.prepare(QStringLiteral(
        "INSERT INTO loans ("
        "copy_id, reader_id, book_id, borrowed_at, due_at) "
        "VALUES (?, ?, ?, CURRENT_DATE, "
        "DATE_ADD(CURRENT_DATE, INTERVAL 30 DAY))"));
    createStatusTestLoan.addBindValue(statusTestCopyId);
    createStatusTestLoan.addBindValue(testReaderId);
    createStatusTestLoan.addBindValue(statusTestBookId);

    const bool statusTestLoanCreated =
        statusTestCopyId > 0
        && createStatusTestLoan.exec();

    allPassed &= check(
        statusTestLoanCreated,
        QStringLiteral("创建读者未归还借阅测试数据"));

    const auto borrowedReaderDetail =
        readerService.getReader(
            *admin,
            testReaderId,
            &error);

    allPassed &= check(
        borrowedReaderDetail.has_value()
            && borrowedReaderDetail->totalLoans == 1
            && borrowedReaderDetail->info.activeLoans == 1
            && borrowedReaderDetail->returnedLoans == 0
            && borrowedReaderDetail->overdueLoans == 0,
        QStringLiteral("读者详情包含当前借阅概况"));

    const auto eligibilityOriginalLimit =
        params.getIntParam(
            QStringLiteral("borrow_limit"),
            &error);

    const bool eligibilityLimitSet =
        eligibilityOriginalLimit.has_value()
        && params.setParam(
            *admin,
            QStringLiteral("borrow_limit"),
            QStringLiteral("1"),
            &error);

    allPassed &= check(
        eligibilityLimitSet,
        QStringLiteral("设置借阅资格测试上限"));

    const ReaderEligibilityResult limitEligibility =
        readerService.checkReaderEligibility(
            *admin, testReaderId, &error);

    allPassed &= check(
        !limitEligibility.eligible
            && limitEligibility.activeLoans == 1
            && limitEligibility.borrowLimit == 1
            && limitEligibility.reason
                   == QStringLiteral("已达到借阅上限"),
        QStringLiteral("达到借阅上限时无借阅资格"));

    const bool borrowedAtLimit =
        circulationService.borrowBook(
            *admin,
            updatedReaderNo,
            statusTestCopyId,
            &error);

    allPassed &= check(
        !borrowedAtLimit
            && error
                   == QStringLiteral(
                       "已达到借阅上限"),
        QStringLiteral(
            "实际借书入口拒绝达到上限的读者"));

    bool eligibilityLimitRestored = false;
    if (eligibilityOriginalLimit.has_value()) {
        eligibilityLimitRestored =
            params.setParam(
                *admin,
                QStringLiteral("borrow_limit"),
                QString::number(*eligibilityOriginalLimit),
                &error);
    }

    allPassed &= check(
        eligibilityLimitRestored,
        QStringLiteral("恢复借阅资格测试上限"));

    const bool disabledWithActiveLoan =
        readerService.setReaderStatus(
            *admin,
            testReaderId,
            false,
            &error);

    allPassed &= check(
        !disabledWithActiveLoan
            && error
                   == QStringLiteral(
                       "读者有未归还图书，不能停用"),
        QStringLiteral("有未归还借阅的读者不能停用"));

    QSqlQuery cleanupStatusTestLoan(
        Database::connection());
    cleanupStatusTestLoan.prepare(QStringLiteral(
        "DELETE FROM loans WHERE reader_id = ?"));
    cleanupStatusTestLoan.addBindValue(testReaderId);

    const bool statusTestLoanCleaned =
        cleanupStatusTestLoan.exec()
        && cleanupStatusTestLoan.numRowsAffected() >= 1;

    allPassed &= check(
        statusTestLoanCleaned,
        QStringLiteral("清理读者借阅状态测试数据"));

    const bool readerDisabled =
        readerService.setReaderStatus(
            *admin,
            testReaderId,
            false,
            &error);

    allPassed &= check(
        readerDisabled,
        QStringLiteral("管理员停用无借阅读者成功"));

    bool disabledStateStored = false;
    const QVector<ReaderInfo> disabledReaderItems =
        readerService.readers(*admin, &error);

    for (const ReaderInfo &item : disabledReaderItems) {
        if (item.id == testReaderId && !item.enabled) {
            disabledStateStored = true;
            break;
        }
    }

    allPassed &= check(
        disabledStateStored,
        QStringLiteral("读者停用状态已保存"));

    ReaderQuery disabledReaderFilter;
    disabledReaderFilter.enabled = false;

    const QVector<ReaderInfo> disabledReaderResults =
        readerService.readers(
            *admin,
            disabledReaderFilter,
            &error);

    bool foundDisabledReader = false;
    bool disabledFilterCorrect = true;

    for (const ReaderInfo &item :
         disabledReaderResults) {
        if (item.enabled) {
            disabledFilterCorrect = false;
        }

        if (item.id == testReaderId) {
            foundDisabledReader = true;
        }
    }

    allPassed &= check(
        disabledFilterCorrect
            && foundDisabledReader,
        QStringLiteral("按停用状态筛选读者成功"));

    const ReaderEligibilityResult disabledEligibility =
        readerService.checkReaderEligibility(
            *admin, testReaderId, &error);

    allPassed &= check(
        !disabledEligibility.eligible
            && !disabledEligibility.readerEnabled
            && disabledEligibility.reason
                   == QStringLiteral("读者已停用"),
        QStringLiteral("停用读者无借阅资格"));

    const bool disabledReaderBorrowed =
        circulationService.borrowBook(
            *admin,
            updatedReaderNo,
            statusTestCopyId,
            &error);

    allPassed &= check(
        !disabledReaderBorrowed
            && error
                   == QStringLiteral(
                       "读者已停用"),
        QStringLiteral(
            "实际借书入口拒绝停用读者"));

    const bool readerRoleEnabledReader =
        readerService.setReaderStatus(
            readerSession,
            testReaderId,
            true,
            &error);

    allPassed &= check(
        !readerRoleEnabledReader
            && error == QStringLiteral("无权限"),
        QStringLiteral("读者角色启用读者被拒绝"));

    const bool readerEnabled =
        readerService.setReaderStatus(
            *admin,
            testReaderId,
            true,
            &error);

    allPassed &= check(
        readerEnabled,
        QStringLiteral("管理员重新启用读者成功"));

    OperationLogQuery readerStatusLogQuery;
    readerStatusLogQuery.actionType =
        QStringLiteral("READER_STATUS_UPDATE");
    readerStatusLogQuery.page = 1;
    readerStatusLogQuery.pageSize = 50;

    const OperationLogPage readerStatusLogPage =
        userLogService.listLogs(
            *admin, readerStatusLogQuery, &error);

    bool foundReaderStatusLog = false;
    for (const OperationLogInfo &item :
         readerStatusLogPage.items) {
        if (item.targetId
            == QString::number(testReaderId)) {
            foundReaderStatusLog = true;
            break;
        }
    }

    allPassed &= check(
        foundReaderStatusLog,
        QStringLiteral("读者状态修改自动写入操作日志"));

    QSqlQuery cleanupReaders(Database::connection());
    cleanupReaders.prepare(QStringLiteral(
        "DELETE FROM readers "
        "WHERE reader_no IN (?, ?, ?)"));
    cleanupReaders.addBindValue(testReaderNo);
    cleanupReaders.addBindValue(updatedReaderNo);
    cleanupReaders.addBindValue(duplicateReaderNo);

    const bool readersCleaned =
        cleanupReaders.exec()
        && cleanupReaders.numRowsAffected() >= 2;

    allPassed &= check(
        readersCleaned,
        QStringLiteral("清理读者修改测试数据"));

    const QString linkedUsername =
        QStringLiteral("linked-user-%1")
            .arg(temporarySuffix);
    const QString linkedReaderNo =
        QStringLiteral("R-LINKED-%1")
            .arg(temporarySuffix);

    QSqlQuery createLinkedUser(
        Database::connection());
    createLinkedUser.prepare(QStringLiteral(
        "INSERT INTO users ("
        "username, display_name, password_hash, "
        "salt, role, active) "
        "VALUES (?, ?, REPEAT('0', 64), "
        "'smoke-salt', 'reader', 1)"));
    createLinkedUser.addBindValue(linkedUsername);
    createLinkedUser.addBindValue(
        QStringLiteral("关联账号测试用户"));

    const bool linkedUserCreated =
        createLinkedUser.exec();
    const qlonglong linkedUserId =
        linkedUserCreated
            ? createLinkedUser.lastInsertId().toLongLong()
            : 0;

    QSqlQuery createLinkedReader(
        Database::connection());
    createLinkedReader.prepare(QStringLiteral(
        "INSERT INTO readers ("
        "user_id, reader_no, name, phone, active) "
        "VALUES (?, ?, ?, ?, 1)"));
    createLinkedReader.addBindValue(linkedUserId);
    createLinkedReader.addBindValue(linkedReaderNo);
    createLinkedReader.addBindValue(
        QStringLiteral("关联账号测试读者"));
    createLinkedReader.addBindValue(
        QStringLiteral("13000000006"));

    const bool linkedReaderCreated =
        linkedUserId > 0
        && createLinkedReader.exec();
    const int linkedReaderId =
        linkedReaderCreated
            ? createLinkedReader.lastInsertId().toInt()
            : 0;

    allPassed &= check(
        linkedUserCreated
            && linkedReaderCreated
            && linkedUserId > 0
            && linkedReaderId > 0,
        QStringLiteral("创建账号与读者关联测试数据"));

    const bool linkedUserDisabled =
        userService.setUserStatus(
            *admin,
            linkedUserId,
            false,
            &error);

    allPassed &= check(
        linkedUserDisabled,
        QStringLiteral("停用关联读者账号成功"));

    const ReaderEligibilityResult
        disabledAccountEligibility =
        readerService.checkReaderEligibility(
            *admin,
            linkedReaderId,
            &error);

    allPassed &= check(
        !disabledAccountEligibility.eligible
            && !disabledAccountEligibility.accountEnabled
            && disabledAccountEligibility.reason
                   == QStringLiteral("关联账号已停用"),
        QStringLiteral("关联账号停用后读者无借阅资格"));

    const bool disabledAccountBorrowed =
        circulationService.borrowBook(
            *admin,
            linkedReaderNo,
            statusTestCopyId,
            &error);

    allPassed &= check(
        !disabledAccountBorrowed
            && error
                   == QStringLiteral(
                       "关联账号已停用"),
        QStringLiteral(
            "实际借书入口拒绝关联账号停用的读者"));

    const bool linkedUserEnabled =
        userService.setUserStatus(
            *admin,
            linkedUserId,
            true,
            &error);

    allPassed &= check(
        linkedUserEnabled,
        QStringLiteral("重新启用关联读者账号成功"));

    QSqlQuery linkedCopyQuery(
        Database::connection());
    const bool linkedCopyFound =
        linkedCopyQuery.exec(QStringLiteral(
            "SELECT bc.id, bc.book_id "
            "FROM book_copies bc "
            "LEFT JOIN loans l "
            "ON l.copy_id = bc.id "
            "AND l.returned_at IS NULL "
            "WHERE l.id IS NULL "
            "ORDER BY bc.id LIMIT 1"))
        && linkedCopyQuery.next();

    const int linkedCopyId =
        linkedCopyFound
            ? linkedCopyQuery.value(0).toInt()
            : 0;
    const int linkedBookId =
        linkedCopyFound
            ? linkedCopyQuery.value(1).toInt()
            : 0;

    QSqlQuery createLinkedLoan(
        Database::connection());
    createLinkedLoan.prepare(QStringLiteral(
        "INSERT INTO loans ("
        "copy_id, reader_id, book_id, borrowed_at, due_at) "
        "VALUES (?, ?, ?, CURRENT_DATE, "
        "DATE_ADD(CURRENT_DATE, INTERVAL 30 DAY))"));
    createLinkedLoan.addBindValue(linkedCopyId);
    createLinkedLoan.addBindValue(linkedReaderId);
    createLinkedLoan.addBindValue(linkedBookId);

    const bool linkedLoanCreated =
        linkedCopyId > 0
        && linkedReaderId > 0
        && createLinkedLoan.exec();

    allPassed &= check(
        linkedLoanCreated,
        QStringLiteral("创建关联账号未归还借阅数据"));

    const bool disabledLinkedUserWithLoan =
        userService.setUserStatus(
            *admin,
            linkedUserId,
            false,
            &error);

    allPassed &= check(
        !disabledLinkedUserWithLoan
            && error
                   == QStringLiteral(
                       "关联读者有未归还图书，不能停用账号"),
        QStringLiteral(
            "关联读者有未归还借阅时账号不能停用"));

    QSqlQuery cleanupLinkedLoan(
        Database::connection());
    cleanupLinkedLoan.prepare(QStringLiteral(
        "DELETE FROM loans WHERE reader_id = ?"));
    cleanupLinkedLoan.addBindValue(linkedReaderId);
    const bool linkedLoanCleaned =
        cleanupLinkedLoan.exec();

    QSqlQuery cleanupLinkedReader(
        Database::connection());
    cleanupLinkedReader.prepare(QStringLiteral(
        "DELETE FROM readers WHERE id = ?"));
    cleanupLinkedReader.addBindValue(linkedReaderId);
    const bool linkedReaderCleaned =
        cleanupLinkedReader.exec();

    QSqlQuery cleanupLinkedUser(
        Database::connection());
    cleanupLinkedUser.prepare(QStringLiteral(
        "DELETE FROM users WHERE id = ?"));
    cleanupLinkedUser.addBindValue(linkedUserId);
    const bool linkedUserCleaned =
        cleanupLinkedUser.exec();

    allPassed &= check(
        linkedLoanCleaned
            && linkedReaderCleaned
            && linkedUserCleaned,
        QStringLiteral("清理账号与读者关联测试数据"));

    const auto originalBorrowPeriod =
        params.getIntParam(
            QStringLiteral("borrow_period_days"),
            &error);

    const bool testBorrowPeriodSet =
        originalBorrowPeriod.has_value()
        && params.setParam(
            *admin,
            QStringLiteral("borrow_period_days"),
            QStringLiteral("7"),
            &error);

    allPassed &= check(
        testBorrowPeriodSet,
        QStringLiteral("设置测试借阅期限为7天"));

    const QString periodReaderNo =
        QStringLiteral("R-PERIOD-%1")
            .arg(temporarySuffix);
    const QString periodIsbn =
        QStringLiteral("ISBN-PERIOD-%1")
            .arg(temporarySuffix);

    const bool periodReaderCreated =
        readerService.addReader(
            *admin,
            periodReaderNo,
            QStringLiteral("借阅期限测试读者"),
            QStringLiteral("13000000007"),
            &error);

    const bool periodBookCreated =
        bookService.addBook(
            *admin,
            QStringLiteral("借阅期限测试图书"),
            QStringLiteral("测试作者"),
            periodIsbn,
            QStringLiteral("计算机"),
            1,
            &error);

    allPassed &= check(
        periodReaderCreated
            && periodBookCreated,
        QStringLiteral("创建借阅期限测试数据"));

    QSqlQuery periodIdsQuery(
        Database::connection());
    periodIdsQuery.prepare(QStringLiteral(
        "SELECT r.id, b.id, bc.id "
        "FROM readers r "
        "JOIN books b ON b.isbn = ? "
        "JOIN book_copies bc ON bc.book_id = b.id "
        "WHERE r.reader_no = ? "
        "ORDER BY bc.id LIMIT 1"));
    periodIdsQuery.addBindValue(periodIsbn);
    periodIdsQuery.addBindValue(periodReaderNo);

    const bool periodIdsFound =
        periodIdsQuery.exec()
        && periodIdsQuery.next();

    const int periodReaderId =
        periodIdsFound
            ? periodIdsQuery.value(0).toInt()
            : 0;
    const int periodBookId =
        periodIdsFound
            ? periodIdsQuery.value(1).toInt()
            : 0;
    const int periodCopyId =
        periodIdsFound
            ? periodIdsQuery.value(2).toInt()
            : 0;

    allPassed &= check(
        periodReaderId > 0
            && periodBookId > 0
            && periodCopyId > 0,
        QStringLiteral("查询到借阅期限测试数据"));

    const bool periodBookBorrowed =
        circulationService.borrowBook(
            *admin,
            periodReaderNo,
            periodCopyId,
            &error);

    allPassed &= check(
        periodBookBorrowed,
        QStringLiteral("使用修改后的期限办理借阅"));

    QSqlQuery periodLoanQuery(
        Database::connection());
    periodLoanQuery.prepare(QStringLiteral(
        "SELECT id, "
        "DATEDIFF(due_at, borrowed_at) "
        "FROM loans "
        "WHERE reader_id = ? "
        "AND copy_id = ? "
        "AND returned_at IS NULL "
        "ORDER BY id DESC LIMIT 1"));
    periodLoanQuery.addBindValue(periodReaderId);
    periodLoanQuery.addBindValue(periodCopyId);

    const bool periodLoanFound =
        periodLoanQuery.exec()
        && periodLoanQuery.next();

    const int periodLoanId =
        periodLoanFound
            ? periodLoanQuery.value(0).toInt()
            : 0;
    const int actualBorrowDays =
        periodLoanFound
            ? periodLoanQuery.value(1).toInt()
            : 0;

    allPassed &= check(
        periodLoanId > 0
            && actualBorrowDays == 7,
        QStringLiteral(
            "修改借阅期限后应还日期同步变化"));

    const bool periodBookReturned =
        circulationService.returnBook(
            *admin,
            periodLoanId,
            &error);

    allPassed &= check(
        periodBookReturned,
        QStringLiteral("归还借阅期限测试图书"));

    auto operationLogExists =
        [&](const QString &actionType,
            const QString &targetId) {
            OperationLogQuery query;
            query.actionType = actionType;
            query.page = 1;
            query.pageSize = 200;

            const OperationLogPage page =
                userLogService.listLogs(
                    *admin,
                    query,
                    &error);

            for (const OperationLogInfo &item :
                 page.items) {
                if (item.targetId == targetId) {
                    return true;
                }
            }
            return false;
        };

    allPassed &= check(
        operationLogExists(
            QStringLiteral("BOOK_CREATE"),
            QString::number(periodBookId)),
        QStringLiteral("图书新增自动写入操作日志"));

    allPassed &= check(
        operationLogExists(
            QStringLiteral("LOAN_BORROW"),
            QString::number(periodLoanId)),
        QStringLiteral("借阅自动写入操作日志"));

    allPassed &= check(
        operationLogExists(
            QStringLiteral("LOAN_RETURN"),
            QString::number(periodLoanId)),
        QStringLiteral("归还自动写入操作日志"));

    bool borrowPeriodRestored = false;
    if (originalBorrowPeriod.has_value()) {
        borrowPeriodRestored =
            params.setParam(
                *admin,
                QStringLiteral("borrow_period_days"),
                QString::number(
                    *originalBorrowPeriod),
                &error);
    }

    allPassed &= check(
        borrowPeriodRestored,
        QStringLiteral("恢复原借阅期限"));

    QSqlQuery cleanupPeriodData(
        Database::connection());

    cleanupPeriodData.prepare(QStringLiteral(
        "DELETE FROM loans WHERE id = ?"));
    cleanupPeriodData.addBindValue(periodLoanId);
    const bool periodLoanCleaned =
        cleanupPeriodData.exec();

    cleanupPeriodData.prepare(QStringLiteral(
        "DELETE FROM readers WHERE id = ?"));
    cleanupPeriodData.addBindValue(periodReaderId);
    const bool periodReaderCleaned =
        cleanupPeriodData.exec();

    cleanupPeriodData.prepare(QStringLiteral(
        "DELETE FROM book_copies "
        "WHERE book_id = ?"));
    cleanupPeriodData.addBindValue(periodBookId);
    const bool periodCopiesCleaned =
        cleanupPeriodData.exec();

    cleanupPeriodData.prepare(QStringLiteral(
        "DELETE FROM books WHERE id = ?"));
    cleanupPeriodData.addBindValue(periodBookId);
    const bool periodBookCleaned =
        cleanupPeriodData.exec();

    allPassed &= check(
        periodLoanCleaned
            && periodReaderCleaned
            && periodCopiesCleaned
            && periodBookCleaned,
        QStringLiteral("清理借阅期限测试数据"));

    const bool readerChangedParam = params.setParam(
        readerSession,
        QStringLiteral("borrow_limit"),
        QStringLiteral("6"),
        &error);

    allPassed &= check(
        !readerChangedParam
            && error == QStringLiteral("无权限"),
        QStringLiteral("读者修改系统参数被拒绝"));

    const auto originalBorrowLimit =
        params.getIntParam(QStringLiteral("borrow_limit"), &error);

    if (originalBorrowLimit.has_value()) {
        const int temporaryValue = *originalBorrowLimit + 1;

        const bool changed = params.setParam(
            *admin,
            QStringLiteral("borrow_limit"),
            QString::number(temporaryValue),
            &error);

        const auto changedValue =
            params.getIntParam(QStringLiteral("borrow_limit"), &error);

        allPassed &= check(
            changed
                && changedValue.has_value()
                && *changedValue == temporaryValue,
            QStringLiteral("管理员修改系统参数成功"));

        const bool restored = params.setParam(
            *admin,
            QStringLiteral("borrow_limit"),
            QString::number(*originalBorrowLimit),
            &error);

        const auto restoredValue =
            params.getIntParam(QStringLiteral("borrow_limit"), &error);

        allPassed &= check(
            restored
                && restoredValue.has_value()
                && *restoredValue == *originalBorrowLimit,
            QStringLiteral("测试后恢复原借阅上限"));
    } else {
        allPassed = false;
        qCritical().noquote()
            << "[FAIL] 无法读取原借阅上限：" << error;
    }

    OperationLogService logService;
    OperationLogQuery paramLogQuery;
    paramLogQuery.actionType =
        QStringLiteral("PARAM_UPDATE");
    paramLogQuery.page = 1;
    paramLogQuery.pageSize = 20;

    const OperationLogPage paramLogPage =
        logService.listLogs(
            *admin, paramLogQuery, &error);

    bool foundParamLog = false;
    for (const OperationLogInfo &item :
         paramLogPage.items) {
        if (item.targetType
                == QStringLiteral("system_param")
            && item.targetId
                   == QStringLiteral("borrow_limit")) {
            foundParamLog = true;
            break;
        }
    }

    allPassed &= check(
        foundParamLog,
        QStringLiteral("参数修改自动写入操作日志"));
    const QString smokeTargetId =
        QString::number(
            QDateTime::currentMSecsSinceEpoch());

    const bool logRecorded = logService.record(
        *admin,
        QStringLiteral("BACKEND_B_SMOKE_TEST"),
        QStringLiteral("system"),
        smokeTargetId,
        QStringLiteral("后端 B 冒烟测试日志"),
        &error);

    allPassed &= check(
        logRecorded,
        QStringLiteral("登录用户写入操作日志成功"));

    OperationLogQuery readerLogQuery;
    readerLogQuery.actionType =
        QStringLiteral("BACKEND_B_SMOKE_TEST");

    const OperationLogPage readerLogPage =
        logService.listLogs(
            readerSession, readerLogQuery, &error);

    allPassed &= check(
        readerLogPage.items.isEmpty()
            && error == QStringLiteral("无权限"),
        QStringLiteral("读者查询操作日志被拒绝"));

    OperationLogQuery adminLogQuery;
    adminLogQuery.actionType =
        QStringLiteral("BACKEND_B_SMOKE_TEST");
    adminLogQuery.page = 1;
    adminLogQuery.pageSize = 20;

    const OperationLogPage adminLogPage =
        logService.listLogs(
            *admin, adminLogQuery, &error);

    bool foundSmokeLog = false;
    for (const OperationLogInfo &item :
         adminLogPage.items) {
        if (item.targetId == smokeTargetId) {
            foundSmokeLog = true;
            break;
        }
    }

    allPassed &= check(
        foundSmokeLog,
        QStringLiteral("管理员分页查询到操作日志"));

    UserSession logoutSession = *admin;
    AuthService::logout(logoutSession);

    allPassed &= check(
        !logoutSession.isAuthenticated()
            && logoutSession.permissions.isEmpty(),
        QStringLiteral("退出后会话已清空"));

    QSqlQuery cleanupRoleAccounts(
        Database::connection());
    cleanupRoleAccounts.prepare(QStringLiteral(
        "DELETE FROM users "
        "WHERE username = ? OR username = ?"));
    cleanupRoleAccounts.addBindValue(
        readerRoleUsername);
    cleanupRoleAccounts.addBindValue(
        librarianRoleUsername);

    const bool roleAccountsCleaned =
        cleanupRoleAccounts.exec()
        && cleanupRoleAccounts.numRowsAffected() >= 2;

    allPassed &= check(
        roleAccountsCleaned,
        QStringLiteral("清理真实角色测试账号"));

    Database::close();

    if (allPassed) {
        qInfo().noquote()
        << "Backend B smoke test passed.";
        return 0;
    }

    qCritical().noquote()
        << "Backend B smoke test failed.";
    return 3;
}
