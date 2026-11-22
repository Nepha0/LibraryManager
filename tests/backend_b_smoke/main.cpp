#include "auth/authservice.h"
#include "auth/permissionservice.h"
#include "common/database.h"
#include "common/paramservice.h"
#include "common/operationlogservice.h"
#include <QCoreApplication>
#include <QDebug>
#include <QStringList>

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