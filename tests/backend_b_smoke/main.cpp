#include "auth/authservice.h"
#include "auth/permissionservice.h"
#include "common/database.h"
#include "common/paramservice.h"
#include "common/operationlogservice.h"
#include "auth/userservice.h"

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

    if (!Database::initialize(&error)) {
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

    UserSession readerSession;
    readerSession.userId = 999;
    readerSession.username = QStringLiteral("test-reader");
    readerSession.displayName = QStringLiteral("测试读者");
    readerSession.role = QStringLiteral("reader");
    readerSession.permissions =
        PermissionService::permissionsForRole(readerSession.role);
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

    UserSession librarianSession;
    librarianSession.userId = 998;
    librarianSession.username =
        QStringLiteral("test-librarian");
    librarianSession.displayName =
        QStringLiteral("测试图书管理员");
    librarianSession.role =
        QStringLiteral("librarian");
    librarianSession.permissions =
        PermissionService::permissionsForRole(
            librarianSession.role);

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