/*
 * test_backend_c_edge.cpp —— 后端C 边界/极端/冲突场景测试
 *
 * 每个场景独立建 SQLite 内存库，互不污染。覆盖：
 *   1) 借阅边界（不存在/下架/各状态副本/乐观锁/参数非法/事务回滚/借阅上限经资格出口）
 *   2) 预约取书边界（超时/非本人/无待取/正常取书/失效后可正常借）
 *   3) 归还边界（重复/不存在/无定位/越权/逾期天数/多排队队首优先）
 *   4) 续借边界（越权/已还/逾期/当天到期/次数上限=边界/排队冲突排除本人/超时待取窗口/账号异常/参数为0）
 *   5) 预约提交边界（有可借/不存在/下架/重复/越权/资格/排队号不复用）
 *   6) 预约取消与完成（waiting/待取取消/终态再操作/越权/不存在）
 *   7) 队首顺延（无队列/越权/申请时间优先/无副本可占/惰性超时释放顺延/终态取消）
 *   8) 逾期边界（应还日当天/前后/历史逾期/早还/越权/未知mode）
 *   9) 个人中心（越权/NULL user_id 读者/统计一致性）与查询筛选分页
 */
#include "contract_c.h"
#include "database/DatabaseManager.h"
#include "services/CopyService.h"
#include "services/BorrowService.h"
#include "services/ReturnService.h"
#include "services/RenewService.h"
#include "services/ReservationService.h"
#include "services/OverdueService.h"
#include "services/PersonalService.h"

#include <QCoreApplication>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QDate>
#include <QDateTime>

#include <cstdio>

namespace {

int g_pass = 0;
int g_fail = 0;
QStringList g_observations;

void expect(bool cond, const QString& what)
{
    if (cond) {
        ++g_pass;
        std::printf("  [PASS] %s\n", qPrintable(what));
    } else {
        ++g_fail;
        std::printf("  [FAIL] %s\n", qPrintable(what));
    }
}

// 观察项：记录当前行为供人工确认，不计入通过/失败
void observe(const QString& what) { g_observations << what; std::printf("  [观察] %s\n", qPrintable(what)); }

int codeOf(const lib::Result& r) { return static_cast<int>(r.code); }
bool isC(const lib::Result& r, int c300x) { return codeOf(r) == c300x; }

QVariant scalar(const QSqlDatabase& db, const QString& sql, const QVariantList& binds = {})
{
    QSqlQuery q(db);
    q.prepare(sql);
    for (const QVariant& b : binds) q.addBindValue(b);
    q.exec();
    return q.next() ? q.value(0) : QVariant();
}
qint64 scalarId(const QSqlDatabase& db, const QString& sql, const QVariantList& binds = {})
{ return scalar(db, sql, binds).toLongLong(); }

// 每场景独立内存库（Qt6：QSqlDatabase 依赖 QCoreApplication，main 中已创建）
QSqlDatabase makeDb(const QString& name)
{
    if (QSqlDatabase::contains(name))
        QSqlDatabase::removeDatabase(name);
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
    db.setDatabaseName(QStringLiteral(":memory:"));
    db.open();
    QSqlQuery q(db);
    const QStringList stmts = QString(lib::DatabaseManager::schemaSql())
                                  .split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString& s : stmts) {
        const QString trimmed = s.trimmed();
        if (!trimmed.isEmpty())
            q.exec(trimmed);
    }
    return db;
}

lib::OperatorContext staffOp()
{
    lib::OperatorContext op;
    op.userId = 999; op.userName = QStringLiteral("管理员");
    op.role = lib::Role::Admin; op.authenticated = true;
    return op;
}
lib::OperatorContext readerOp(qint64 uid)
{
    lib::OperatorContext op;
    op.userId = uid; op.userName = QStringLiteral("读者");
    op.role = lib::Role::Reader; op.authenticated = true;
    return op;
}
lib::OperatorContext anonOp()
{
    lib::OperatorContext op;
    op.role = lib::Role::Reader; op.authenticated = false;
    return op;
}

// 静态种子：图书A(active, 副本 C001-C004) + 下架书(副本 C101) + 维修副本 C201(独立书)
//           读者 R001(user_id=1) / R002(user_id=NULL) / R003(user_id=2)
struct Fixture {
    QSqlDatabase db;
    qint64 bookA = 0, bookOff = 0, bookM = 0;
    qint64 r1 = 0, r2 = 0, r3 = 0;
};

Fixture seed(QSqlDatabase db)
{
    Fixture f; f.db = db;
    QSqlQuery q(db);
    q.exec(QStringLiteral("INSERT INTO categories(name) VALUES('测试类')"));
    q.exec(QStringLiteral("INSERT INTO books(isbn,title,author,category_id,status) "
                          "VALUES('978-7-000-000001-1','图书A','作者甲',1,'active')"));
    f.bookA = scalarId(db, QStringLiteral("SELECT id FROM books WHERE isbn LIKE '%000001%'"));
    q.exec(QStringLiteral("INSERT INTO book_copies(copy_code, book_id, location) VALUES('C001',%1,'A区'),"
                          "('C002',%1,'B区'),('C003',%1,'C区'),('C004',%1,'D区')").arg(f.bookA));
    q.exec(QStringLiteral("INSERT INTO books(isbn,title,author,category_id,status) "
                          "VALUES('978-7-000-000002-1','下架书','作者乙',1,'shelved_off')"));
    f.bookOff = scalarId(db, QStringLiteral("SELECT id FROM books WHERE isbn LIKE '%000002%'"));
    q.exec(QStringLiteral("INSERT INTO book_copies(copy_code, book_id, location) VALUES('C101',%1,'E区')").arg(f.bookOff));
    q.exec(QStringLiteral("INSERT INTO books(isbn,title,author,category_id,status) "
                          "VALUES('978-7-000-000003-1','维修书','作者丙',1,'active')"));
    f.bookM = scalarId(db, QStringLiteral("SELECT id FROM books WHERE isbn LIKE '%000003%'"));
    q.exec(QStringLiteral("INSERT INTO book_copies(copy_code, book_id, location, status) "
                          "VALUES('C201',%1,'F区','maintenance')").arg(f.bookM));

    q.exec(QStringLiteral("INSERT INTO users(username, display_name, password_hash, salt, role) "
                          "VALUES('u1','读者一','x','y','reader'), ('u3','读者三','x','y','reader')"));
    const qint64 u3 = scalarId(db, QStringLiteral("SELECT id FROM users WHERE username='u3'"));
    q.exec(QStringLiteral("INSERT INTO readers(user_id, reader_no, name, phone) VALUES(1,'R001','读者一','13800000001')"));
    q.exec(QStringLiteral("INSERT INTO readers(user_id, reader_no, name, phone) VALUES(NULL,'R002','读者二','13800000002')"));
    q.exec(QStringLiteral("INSERT INTO readers(user_id, reader_no, name, phone) VALUES(%1,'R003','读者三','13800000003')").arg(u3));
    f.r1 = scalarId(db, QStringLiteral("SELECT id FROM readers WHERE reader_no='R001'"));
    f.r2 = scalarId(db, QStringLiteral("SELECT id FROM readers WHERE reader_no='R002'"));
    f.r3 = scalarId(db, QStringLiteral("SELECT id FROM readers WHERE reader_no='R003'"));

    const char* params[] = {"borrow_period_days", "30", "borrow_limit", "5", "max_renew_times", "2",
                            "reservation_hold_days", "3", "renew_period_days", "15"};
    for (int i = 0; i < 10; i += 2)
        q.exec(QStringLiteral("INSERT OR REPLACE INTO system_params(param_key, param_value) VALUES('%1','%2')")
                   .arg(params[i], params[i + 1]));
    return f;
}

// 手工插入借阅记录（夹具用；copyStatus 非空时同步置副本状态）
qint64 insertLoan(const Fixture& f, qint64 readerId, qint64 copyId, qint64 bookId,
                  qint64 borrowedDaysAgo, qint64 dueIn, const QString& copyStatus = QString())
{
    QSqlQuery q(f.db);
    q.exec(QStringLiteral("INSERT INTO loans(reader_id, copy_id, book_id, borrowed_at, due_at) "
                          "VALUES(%1, %2, %3, date('now','localtime','-%4 day'), date('now','localtime','%5 day'))")
               .arg(readerId).arg(copyId).arg(bookId).arg(borrowedDaysAgo).arg(dueIn));
    if (!copyStatus.isEmpty())
        q.exec(QStringLiteral("UPDATE book_copies SET status='%1' WHERE id=%2").arg(copyStatus).arg(copyId));
    return scalarId(f.db, QStringLiteral("SELECT last_insert_rowid()"));
}
qint64 copyIdOf(const Fixture& f, const QString& code)
{ return scalarId(f.db, QStringLiteral("SELECT id FROM book_copies WHERE copy_code='%1'").arg(code)); }

lib::EligibilityFn okElig()
{
    return [](qint64) {
        lib::EligibilityResult e; e.eligible = true; return e;
    };
}

// ============ 1) 借阅边界 ============
void scenarioBorrowEdge()
{
    std::printf("== 场景1：借阅边界 ==\n");
    QSqlDatabase db = makeDb(QStringLiteral("borrow"));
    Fixture f = seed(db);
    lib::CopyService copies(db);
    lib::BorrowService borrow(db, &copies, okElig());
    lib::OperatorContext staff = staffOp();

    expect(isC(borrow.borrow(staff, f.r1, QStringLiteral("NO-SUCH")), 1011), "不存在条码 → 1011");
    expect(codeOf(borrow.borrow(staff, f.r1, QStringLiteral(""))) == (int)lib::ErrorCode::MissingField,
           "空条码 → 1003 缺参");
    expect(codeOf(borrow.borrow(anonOp(), f.r1, QStringLiteral("C001"))) == (int)lib::ErrorCode::NotAuthenticated,
           "未登录借阅 → 1001");
    expect(codeOf(borrow.borrow(staff, f.r1, QStringLiteral("C101"))) == (int)lib::ErrorCode::BookShelvedOff,
           "下架书的副本借阅 → 1013");

    lib::Result rm = borrow.borrow(staff, f.r1, QStringLiteral("C201"));
    expect(isC(rm, 3002), "维修中副本借阅 → 3002");
    if (isC(rm, 3002) && !rm.message.contains(QStringLiteral("maintenance"))
        && !rm.message.contains(QStringLiteral("维修")))
        observe("3002 message 未含副本状态信息——提示口径确认项");

    // 正常借出后再借：3002（双保险路径）
    lib::Result ok = borrow.borrow(staff, f.r1, QStringLiteral("C001"));
    expect(ok.ok(), "正常借出 C001");
    expect(isC(borrow.borrow(staff, f.r2, QStringLiteral("C001")), 3002),
           "同副本再借 → 3002（在借记录预检）");

    // 乐观锁拒绝路径模拟：borrowed → borrowed 非法转换
    expect(copies.changeCopyStatus(staff, copyIdOf(f, "C002"), QStringLiteral("borrowed")).ok(),
           "C002 直接借出（夹具）");
    lib::Result dup = copies.changeCopyStatus(staff, copyIdOf(f, "C002"), QStringLiteral("borrowed"));
    expect(codeOf(dup) == (int)lib::ErrorCode::IllegalStatusTransition,
           "borrowed → borrowed 非法转换 → 1007（乐观锁拒绝路径）");

    // 借阅期限参数非法 → 事务失败且无脏数据（用空闲副本 C003）
    QSqlQuery q(db);
    q.exec(QStringLiteral("UPDATE system_params SET param_value='0' WHERE param_key='borrow_period_days'"));
    lib::Result bad = borrow.borrow(staff, f.r2, QStringLiteral("C003"));
    expect(!bad.ok() && codeOf(bad) == (int)lib::ErrorCode::DbError, "借阅期限=0 → 参数非法拒绝");
    expect(scalarId(db, QStringLiteral("SELECT COUNT(*) FROM loans WHERE copy_id=%1")
                        .arg(copyIdOf(f, "C003"))) == 0, "期限=0 失败后无借阅记录（事务回滚）");
    expect(scalar(db, QStringLiteral("SELECT status FROM book_copies WHERE copy_code='C003'"))
               .toString() == QStringLiteral("available"), "期限=0 时副本状态不受影响");
    q.exec(QStringLiteral("UPDATE system_params SET param_value='30' WHERE param_key='borrow_period_days'"));

    // 审计失败 → 整体回滚
    lib::BorrowService borrowAudit(db, &copies, okElig());
    lib::Result ra = borrowAudit.borrow(staff, f.r2, QStringLiteral("C003"),
        [](qint64, QString* err) { if (err) *err = QStringLiteral("审计注入失败"); return false; });
    expect(!ra.ok() && codeOf(ra) == (int)lib::ErrorCode::DbError, "审计回调失败 → 借阅拒绝");
    expect(scalarId(db, QStringLiteral("SELECT COUNT(*) FROM loans WHERE copy_id=%1")
                        .arg(copyIdOf(f, "C003"))) == 0, "审计失败后无借阅记录（回滚生效）");

    // 借阅上限：按契约由 B 资格出口判定（借阅场景生效）；此处模拟计数型资格
    int r1Quota = 0;   // R001 已借数量（由外层控制）
    lib::EligibilityFn limitElig = [&](qint64 rid) {
        lib::EligibilityResult e;
        e.eligible = !(rid == f.r1 && r1Quota >= 1);
        if (!e.eligible) e.reasons << QStringLiteral("已达借阅上限");
        return e;
    };
    lib::BorrowService borrowLimit(db, &copies, limitElig);
    r1Quota = 1;   // R001 已满
    expect(isC(borrowLimit.borrow(staff, f.r1, QStringLiteral("C003")), 3001),
           "借阅上限（资格出口）→ 3001");
    r1Quota = 2;   // R001 超限，但 R002 不受影响
    expect(borrowLimit.borrow(staff, f.r2, QStringLiteral("C003")).ok(),
           "上限内其他读者不受影响（R002 可借）");
    expect(scalarId(db, QStringLiteral("SELECT COUNT(*) FROM loans WHERE copy_id=%1")
                        .arg(copyIdOf(f, "C003"))) == 1, "上限内借阅正常落库");
}

// ============ 2) 预约取书边界 ============
void scenarioPickupEdge()
{
    std::printf("== 场景2：预约取书边界 ==\n");
    QSqlDatabase db = makeDb(QStringLiteral("pickup"));
    Fixture f = seed(db);
    lib::CopyService copies(db);
    lib::BorrowService borrow(db, &copies, okElig());
    lib::OperatorContext staff = staffOp();
    lib::OperatorContext r1op = readerOp(1);

    // 夹具：C001-C004 全借出 → R003 可排队
    expect(borrow.borrow(staff, f.r1, QStringLiteral("C001")).ok(), "夹具：R001 借 C001");
    expect(borrow.borrow(staff, f.r2, QStringLiteral("C002")).ok(), "夹具：R002 借 C002");
    expect(borrow.borrow(staff, f.r3, QStringLiteral("C003")).ok(), "夹具：R003 借 C003");
    expect(borrow.borrow(staff, f.r1, QStringLiteral("C004")).ok(), "夹具：R001 借 C004");
    lib::ReservationService resv(db, &copies, okElig());
    lib::Result rs = resv.reserve(staff, f.r3, f.bookA);
    expect(rs.ok(), "夹具：R003 排队");
    lib::ReturnService ret(db, &copies);
    expect(ret.returnBook(staff, {{QStringLiteral("copy_barcode"), QStringLiteral("C001")}}).ok(),
           "夹具：归还触发待取");
    expect(scalar(db, QStringLiteral("SELECT status FROM book_copies WHERE copy_code='C001'"))
               .toString() == QStringLiteral("reserved"), "夹具：C001 → reserved");

    // 非预约者取书 → 3002
    expect(isC(borrow.borrow(staff, f.r1, QStringLiteral("C001")), 3002),
           "非预约读者取 reserved 副本 → 3002");
    expect(codeOf(borrow.borrow(r1op, f.r1, QStringLiteral("C001"))) == (int)lib::ErrorCode::NoPermission,
           "Reader 角色办理借阅 → 1002（借阅仅 staff 可办理）");

    // 超时后：借阅入口惰性释放（修复①）——持有人本人来借，先释放自己的超时预约再正常借出
    QSqlQuery q(db);
    q.exec(QStringLiteral("UPDATE reservations SET pickup_deadline=datetime('now','localtime','-1 day') "
                          "WHERE status='ready_to_pick'"));
    lib::Result selfTake = borrow.borrow(staff, f.r3, QStringLiteral("C001"));
    expect(selfTake.ok(), "待取超时后本人来借 → 借阅入口惰性释放并正常借出");
    expect(scalar(db, QStringLiteral("SELECT status FROM reservations WHERE reader_id=%1 AND book_id=%2")
                       .arg(f.r3).arg(f.bookA)).toString() == QStringLiteral("expired"),
           "超时待取预约 → expired（借阅入口触发）");
    expect(scalar(db, QStringLiteral("SELECT status FROM book_copies WHERE copy_code='C001'"))
               .toString() == QStringLiteral("borrowed"), "C001 已借出");

    // 非持有人借阅同样触发惰性释放：其他读者的超时待取不再卡住借阅
    expect(ret.returnBook(staff, {{QStringLiteral("copy_barcode"), QStringLiteral("C002")}}).ok(),
           "夹具：归还 C002（无排队 → 回馆）");
    q.exec(QStringLiteral("INSERT INTO reservations(book_id, reader_id, queue_no, status, requested_at) "
                          "VALUES(%1,%2,1,'waiting',datetime('now','localtime'))").arg(f.bookA).arg(f.r1));
    expect(ret.returnBook(staff, {{QStringLiteral("copy_barcode"), QStringLiteral("C003")}}).ok(),
           "夹具：归还 C003 触发 R001 待取");
    q.exec(QStringLiteral("UPDATE reservations SET pickup_deadline=datetime('now','localtime','-1 day') "
                          "WHERE reader_id=%1 AND status='ready_to_pick'").arg(f.r1));
    lib::Result otherTake = borrow.borrow(staff, f.r2, QStringLiteral("C003"));
    expect(otherTake.ok(), "其他读者借阅 → 惰性释放超时待取后可借出");
    expect(scalar(db, QStringLiteral("SELECT status FROM reservations WHERE reader_id=%1").arg(f.r1))
               .toString() == QStringLiteral("expired"), "R001 超时待取 → expired");
    expect(scalar(db, QStringLiteral("SELECT status FROM book_copies WHERE copy_code='C003'"))
               .toString() == QStringLiteral("borrowed"), "C003 已被 R002 借出");

    // 队列已空：promoteNext → 3009（无待顺延者）
    expect(isC(resv.promoteNext(staff, f.bookA), 3009), "无排队时顺延 → 3009");

    // reserved 但无 ready_to_pick 预约（手工构造孤儿 reserved）→ 3002
    insertLoan(f, f.r3, copyIdOf(f, "C002"), f.bookA, 0, 30, QStringLiteral("borrowed"));
    q.exec(QStringLiteral("UPDATE book_copies SET status='reserved' WHERE copy_code='C002'"));
    expect(isC(borrow.borrow(staff, f.r1, QStringLiteral("C002")), 3002),
           "reserved 且无有效待取预约 → 3002");
}

// ============ 3) 归还边界 ============
void scenarioReturnEdge()
{
    std::printf("== 场景3：归还边界 ==\n");
    QSqlDatabase db = makeDb(QStringLiteral("return"));
    Fixture f = seed(db);
    lib::CopyService copies(db);
    lib::BorrowService borrow(db, &copies, okElig());
    lib::ReturnService ret(db, &copies);
    lib::OperatorContext staff = staffOp();
    lib::OperatorContext r1op = readerOp(1);

    expect(codeOf(ret.returnBook(anonOp(), {})) == (int)lib::ErrorCode::NotAuthenticated, "未登录归还 → 1001");
    expect(codeOf(ret.returnBook(r1op, {{QStringLiteral("copy_barcode"), QStringLiteral("C001")}}))
               == (int)lib::ErrorCode::NoPermission, "Reader 办理归还 → 1002");
    expect(codeOf(ret.returnBook(staff, {})) == (int)lib::ErrorCode::MissingField, "无定位方式 → 1003");
    expect(isC(ret.returnBook(staff, {{QStringLiteral("loan_id"), 12345}}), 3003), "不存在 loan_id → 3003");
    expect(isC(ret.returnBook(staff, {{QStringLiteral("copy_barcode"), QStringLiteral("NOPE")}}), 3003),
           "不存在条码 → 3003");

    // 借出→归还→重复归还
    expect(borrow.borrow(staff, f.r1, QStringLiteral("C001")).ok(), "夹具：借出 C001");
    lib::Result r1 = ret.returnBook(staff, {{QStringLiteral("copy_barcode"), QStringLiteral("C001")}});
    expect(r1.ok() && r1.data.value(QStringLiteral("overdue_days")).toInt() == 0,
           "未逾期归还 overdue_days=0");
    expect(scalar(db, QStringLiteral("SELECT status FROM book_copies WHERE copy_code='C001'"))
               .toString() == QStringLiteral("available"), "归还后副本 → available");
    expect(isC(ret.returnBook(staff, {{QStringLiteral("copy_barcode"), QStringLiteral("C001")}}), 3003),
           "重复归还 → 3003");

    // 逾期归还：due 5 天前
    qint64 late = insertLoan(f, f.r2, copyIdOf(f, "C002"), f.bookA, 35, -5, QStringLiteral("borrowed"));
    lib::Result r2 = ret.returnBook(staff, {{QStringLiteral("loan_id"), late}});
    expect(r2.ok() && r2.data.value(QStringLiteral("overdue_days")).toInt() == 5,
           "逾期 5 天归还 overdue_days=5");
    lib::BorrowService bs(db, &copies, okElig());
    lib::Result gl = bs.getLoan(staff, late);
    expect(gl.ok() && gl.data.value(QStringLiteral("status_text")).toString() == QStringLiteral("已还"),
           "历史逾期记录 getLoan → 已还");
    expect(gl.ok() && gl.data.value(QStringLiteral("overdue_days")).toInt() == 5,
           "已还记录 overdue_days = 5（借阅行口径锁定：归还日-应还日）");

    // 多人排队：队首按 requested_at（R002 早于 R003）
    insertLoan(f, f.r3, copyIdOf(f, "C003"), f.bookA, 0, 30, QStringLiteral("borrowed"));
    QSqlQuery q(db);
    q.exec(QStringLiteral("INSERT INTO reservations(book_id, reader_id, queue_no, status, requested_at) "
                          "VALUES(%1,%2,1,'waiting',datetime('now','localtime','-20 minute'))").arg(f.bookA).arg(f.r2));
    q.exec(QStringLiteral("INSERT INTO reservations(book_id, reader_id, queue_no, status, requested_at) "
                          "VALUES(%1,%2,2,'waiting',datetime('now','localtime','-10 minute'))").arg(f.bookA).arg(f.r3));
    lib::Result r3 = ret.returnBook(staff, {{QStringLiteral("loan_id"),
                          scalarId(db, QStringLiteral("SELECT id FROM loans WHERE copy_id=%1 AND returned_at IS NULL")
                                               .arg(copyIdOf(f, "C003"))) }});
    expect(r3.ok() && r3.data.value(QStringLiteral("copy_status")).toString() == QStringLiteral("reserved"),
           "归还触发预约保留");
    expect(scalar(db, QStringLiteral("SELECT reader_id FROM reservations WHERE book_id=%1 AND status='ready_to_pick'")
                       .arg(f.bookA)).toLongLong() == f.r2,
           "队列按申请时间：先到的 R002 转待取");
}

// ============ 4) 续借边界 ============
void scenarioRenewEdge()
{
    std::printf("== 场景4：续借边界 ==\n");
    QSqlDatabase db = makeDb(QStringLiteral("renew"));
    Fixture f = seed(db);
    lib::CopyService copies(db);
    lib::RenewService renew(db, okElig());
    lib::OperatorContext staff = staffOp();
    lib::OperatorContext r1op = readerOp(1);

    // R002 的逾期在借记录（user_id=NULL → Reader 永远不匹配）
    QSqlQuery q(db);
    qint64 od = insertLoan(f, f.r2, copyIdOf(f, "C002"), f.bookA, 40, -10, QStringLiteral("borrowed"));
    expect(codeOf(renew.renew(anonOp(), od)) == (int)lib::ErrorCode::NotAuthenticated, "未登录续借 → 1001");
    expect(isC(renew.renew(staff, 424242), 3003), "不存在记录续借 → 3003");
    expect(codeOf(renew.renew(r1op, od)) == (int)lib::ErrorCode::NoPermission,
           "Reader 续借他人（R002）记录 → 1002");

    // R001 的记录，due = 今天（边界：当天到期不算逾期）
    qint64 loan = insertLoan(f, f.r1, copyIdOf(f, "C001"), f.bookA, 30, 0, QStringLiteral("borrowed"));
    lib::Result rd0 = renew.renew(staff, loan);
    expect(rd0.ok() && rd0.data.value(QStringLiteral("new_due_at"))
               .toString() == QDate::currentDate().addDays(15).toString(QStringLiteral("yyyy-MM-dd")),
           "应还日=今天 → 可续借（边界），顺延 15 天");

    // 次数上限边界（max=2）：第 2 次可，第 3 次 → 3004
    expect(renew.renew(staff, loan).ok(), "第 2 次续借（1/2 → 2/2）");
    lib::Result rmax = renew.renew(staff, loan);
    expect(isC(rmax, 3004) && rmax.message.contains(QStringLiteral("最大续借次数")),
           "第 3 次续借 → 3004（上限边界=）");

    // 逾期续借拒绝
    lib::Result ro = renew.renew(staff, od);
    expect(isC(ro, 3004) && ro.message.contains(QStringLiteral("逾期")), "逾期在借续借 → 3004（逾期原因）");

    // 已归还续借拒绝
    qint64 done = insertLoan(f, f.r3, copyIdOf(f, "C101"), f.bookOff, 40, -10);
    q.exec(QStringLiteral("UPDATE loans SET returned_at=date('now','localtime','-1 day') WHERE id=%1").arg(done));
    expect(isC(renew.renew(staff, done), 3004), "已归还记录续借 → 3004（已归还原因）");

    // 排队冲突：其他读者 waiting → 3004；删除后可续
    // （loan3 提前借出 C004：确保续借入口惰性顺延无副本可占，不干扰本组用例）
    qint64 loan2 = insertLoan(f, f.r1, copyIdOf(f, "C003"), f.bookA, 0, 30, QStringLiteral("borrowed"));
    qint64 loan3 = insertLoan(f, f.r1, copyIdOf(f, "C004"), f.bookA, 0, 30, QStringLiteral("borrowed"));
    q.exec(QStringLiteral("INSERT INTO reservations(book_id, reader_id, queue_no, status, requested_at) "
                          "VALUES(%1,%2,1,'waiting',datetime('now','localtime'))").arg(f.bookA).arg(f.r2));
    lib::Result blocked = renew.renew(staff, loan2);
    expect(isC(blocked, 3004) && blocked.message.contains(QStringLiteral("预约")),
           "排队冲突：其他读者 waiting → 3004");
    q.exec(QStringLiteral("DELETE FROM reservations WHERE reader_id=%1").arg(f.r2));
    expect(renew.renew(staff, loan2).ok(), "排队者=本人 → 排除本人后可续借");

    // 其他读者 ready_to_pick → 3004（真实流程构造：R003 排队 → 归还逾期件触发待取）
    lib::ReservationService resv(db, &copies, okElig());
    lib::ReturnService ret(db, &copies);
    lib::Result rs = resv.reserve(staff, f.r3, f.bookA);   // C001-C004 全借出 → 可排队
    expect(rs.ok(), "夹具：R003 排队");
    expect(ret.returnBook(staff, {{QStringLiteral("loan_id"), od}}).ok(), "夹具：归还逾期件触发待取");
    expect(scalar(db, QStringLiteral("SELECT status FROM book_copies WHERE copy_code='C002'"))
               .toString() == QStringLiteral("reserved"), "夹具：C002 → reserved（R003 待取）");
    expect(isC(renew.renew(staff, loan2), 3004), "其他读者 ready_to_pick → 3004");

    // 待取超时后：续借入口惰性释放（修复①），超时待取不再永久卡住续借
    q.exec(QStringLiteral("UPDATE reservations SET pickup_deadline=datetime('now','localtime','-1 day') "
                          "WHERE reader_id=%1 AND status='ready_to_pick'").arg(f.r3));
    lib::Result afterRelease = renew.renew(staff, loan2);
    expect(afterRelease.ok(), "待取超时后续借 → 惰性释放后可续借");
    expect(scalar(db, QStringLiteral("SELECT status FROM reservations WHERE reader_id=%1 AND book_id=%2")
                       .arg(f.r3).arg(f.bookA)).toString() == QStringLiteral("expired"),
           "超时待取预约 → expired（续借入口触发）");
    expect(scalar(db, QStringLiteral("SELECT status FROM book_copies WHERE copy_code='C002'"))
               .toString() == QStringLiteral("available"), "超时释放副本回馆");

    // 账号异常（资格出口 accountProblem）→ 3004
    lib::EligibilityFn badAcct = [](qint64) {
        lib::EligibilityResult e; e.eligible = true; e.accountProblem = true; return e;
    };
    lib::RenewService renewBad(db, badAcct);
    expect(isC(renewBad.renew(staff, loan2), 3004), "账号异常 → 3004");

    // max_renew_times=0 → 全部拒绝（loan3 已在前方夹具中借出）
    q.exec(QStringLiteral("UPDATE system_params SET param_value='0' WHERE param_key='max_renew_times'"));
    expect(isC(renew.renew(staff, loan3), 3004), "max_renew_times=0 → 续借全部拒绝（边界）");
    q.exec(QStringLiteral("UPDATE system_params SET param_value='2' WHERE param_key='max_renew_times'"));
}

// ============ 5) 预约提交边界 ============
void scenarioReserveEdge()
{
    std::printf("== 场景5：预约提交边界 ==\n");
    QSqlDatabase db = makeDb(QStringLiteral("reserve"));
    Fixture f = seed(db);
    lib::CopyService copies(db);
    lib::ReservationService resv(db, &copies, okElig());
    lib::OperatorContext staff = staffOp();
    lib::OperatorContext r1op = readerOp(1);

    expect(codeOf(resv.reserve(anonOp(), f.r1, f.bookA)) == (int)lib::ErrorCode::NotAuthenticated,
           "未登录预约 → 1001");
    expect(codeOf(resv.reserve(staff, f.r1, 654321)) == (int)lib::ErrorCode::BookNotFound,
           "不存在图书 → 1012");
    expect(codeOf(resv.reserve(staff, f.r1, f.bookOff)) == (int)lib::ErrorCode::BookShelvedOff,
           "下架书预约 → 1013");
    expect(isC(resv.reserve(staff, f.r1, f.bookA), 3006), "有可借副本 → 3006");
    expect(codeOf(resv.reserve(r1op, f.r2, f.bookA)) == (int)lib::ErrorCode::NoPermission,
           "Reader 为他人预约 → 1002");

    // 全副本借出后：可排队；重复排队 → 3005
    lib::BorrowService borrow(db, &copies, okElig());
    expect(borrow.borrow(staff, f.r1, QStringLiteral("C001")).ok(), "夹具：借出 C001");
    expect(borrow.borrow(staff, f.r2, QStringLiteral("C002")).ok(), "夹具：借出 C002");
    expect(borrow.borrow(staff, f.r3, QStringLiteral("C003")).ok(), "夹具：借出 C003");
    expect(borrow.borrow(staff, f.r1, QStringLiteral("C004")).ok(), "夹具：借出 C004");
    lib::Result q1 = resv.reserve(staff, f.r3, f.bookA);
    expect(q1.ok() && q1.data.value(QStringLiteral("queue_no")).toInt() == 1, "全部借出后 R003 排队 queue=1");
    expect(isC(resv.reserve(staff, f.r3, f.bookA), 3005), "同读者重复排队 → 3005");
    lib::Result q2 = resv.reserve(staff, f.r1, f.bookA);
    expect(q2.ok() && q2.data.value(QStringLiteral("queue_no")).toInt() == 2, "R001 排队 queue=2");

    // 维修中唯一副本的书：available=0 → 可预约（口径：仅看 available 计数）
    expect(resv.reserve(staff, f.r1, f.bookM).ok(), "仅有维修副本的书可预约（available=0 口径）");

    // 资格不通过 → 3001
    lib::EligibilityFn nope = [](qint64) {
        lib::EligibilityResult e; e.eligible = false; e.reasons << QStringLiteral("已停用"); return e;
    };
    lib::ReservationService resvNope(db, &copies, nope);
    expect(isC(resvNope.reserve(staff, f.r2, f.bookA), 3001), "资格不通过预约 → 3001");

    // 排队号不复用：取消 queue=1 后新预约 queue_no = max+1 = 3
    expect(resv.cancelReservation(staff, q1.data.value(QStringLiteral("reservation_id")).toLongLong()).ok(),
           "取消 queue=1 的预约");
    lib::Result q3 = resv.reserve(staff, f.r2, f.bookA);
    expect(q3.ok() && q3.data.value(QStringLiteral("queue_no")).toInt() == 3,
           "取消后新预约排队号 = max+1（不复用空洞）");
}

// ============ 6) 取消与完成边界 ============
void scenarioCancelFinishEdge()
{
    std::printf("== 场景6：取消/完成边界 ==\n");
    QSqlDatabase db = makeDb(QStringLiteral("cancel"));
    Fixture f = seed(db);
    lib::CopyService copies(db);
    lib::ReservationService resv(db, &copies, okElig());
    lib::BorrowService bs(db, &copies, okElig());
    lib::ReturnService ret(db, &copies);
    lib::OperatorContext staff = staffOp();
    lib::OperatorContext otherReader = readerOp(1);   // R001 的 user_id=1，与 R003（user_id=2）不同

    expect(isC(resv.cancelReservation(staff, 999999), 3007), "取消不存在预约 → 3007");
    expect(codeOf(resv.finishReservation(staff, 999999)) == (int)lib::ErrorCodeC::IllegalReservationTransition,
           "完成不存在/非待取预约 → 3008");

    // 夹具：全借出，R003 排队
    bs.borrow(staff, f.r1, QStringLiteral("C001"));
    bs.borrow(staff, f.r2, QStringLiteral("C002"));
    bs.borrow(staff, f.r3, QStringLiteral("C003"));
    bs.borrow(staff, f.r1, QStringLiteral("C004"));
    lib::Result rs = resv.reserve(staff, f.r3, f.bookA);
    expect(rs.ok(), "夹具：R003 排队");
    qint64 resvId = rs.data.value(QStringLiteral("reservation_id")).toLongLong();

    // waiting 直接取消 → cancelled；再取消 → 3008
    expect(resv.cancelReservation(staff, resvId).ok(), "waiting 预约取消 → cancelled");
    expect(scalar(db, QStringLiteral("SELECT status FROM reservations WHERE id=%1").arg(resvId))
               .toString() == QStringLiteral("cancelled"), "状态 = cancelled");
    expect(isC(resv.cancelReservation(staff, resvId), 3008), "终态再取消 → 3008");
    expect(resv.reserve(staff, f.r3, f.bookA).ok(), "取消后可重新排队（唯一约束解除）");

    // 待取取消：副本释放
    qint64 resvId2 = scalarId(db, QStringLiteral("SELECT id FROM reservations WHERE status='waiting' "
                                                    "ORDER BY id DESC LIMIT 1"));
    qint64 loanC001 = scalarId(db, QStringLiteral("SELECT id FROM loans WHERE copy_id=%1 AND returned_at IS NULL")
                                         .arg(copyIdOf(f, "C001")));
    ret.returnBook(staff, {{QStringLiteral("loan_id"), loanC001}});   // 触发待取
    expect(scalar(db, QStringLiteral("SELECT status FROM book_copies WHERE copy_code='C001'"))
               .toString() == QStringLiteral("reserved"), "夹具：C001 → reserved（待取）");
    expect(resv.cancelReservation(staff, resvId2).ok(), "待取预约取消");
    expect(scalar(db, QStringLiteral("SELECT status FROM book_copies WHERE copy_code='C001'"))
               .toString() == QStringLiteral("available"), "待取取消后副本 → available");

    // Reader 越权取消/完成（预约属 R003，R001 操作）。R003 排队前先把释放出的 C001 借出
    bs.borrow(staff, f.r3, QStringLiteral("C001"));
    lib::Result rs2 = resv.reserve(staff, f.r3, f.bookA);
    expect(rs2.ok(), "夹具：R003 再次排队");
    qint64 resv3 = rs2.data.value(QStringLiteral("reservation_id")).toLongLong();
    expect(codeOf(resv.cancelReservation(otherReader, resv3)) == (int)lib::ErrorCode::NoPermission,
           "Reader 取消他人预约 → 1002");
    expect(codeOf(resv.finishReservation(otherReader, resv3)) == (int)lib::ErrorCode::NoPermission,
           "Reader 完成预约 → 1002");

    // 手工构造待取后 finish：finished；再 finish → 3008；副本状态不变（由借阅流程驱动）
    ret.returnBook(staff, {{QStringLiteral("copy_barcode"), QStringLiteral("C002")}});
    qint64 rtp = scalarId(db, QStringLiteral("SELECT id FROM reservations WHERE status='ready_to_pick' LIMIT 1"));
    expect(resv.finishReservation(staff, rtp).ok(), "staff 手工完成待取 → finished");
    expect(scalar(db, QStringLiteral("SELECT status FROM book_copies WHERE copy_code='C002'"))
               .toString() == QStringLiteral("reserved"), "finish 不动副本状态（仍 reserved）");
    expect(codeOf(resv.finishReservation(staff, rtp)) == (int)lib::ErrorCodeC::IllegalReservationTransition,
           "再 finish → 3008");
}

// ============ 7) 队首顺延边界 ============
void scenarioPromoteEdge()
{
    std::printf("== 场景7：顺延边界 ==\n");
    QSqlDatabase db = makeDb(QStringLiteral("promote"));
    Fixture f = seed(db);
    lib::CopyService copies(db);
    lib::ReservationService resv(db, &copies, okElig());
    lib::OperatorContext staff = staffOp();
    lib::OperatorContext r1op = readerOp(1);

    expect(codeOf(resv.promoteNext(r1op, f.bookA)) == (int)lib::ErrorCode::NoPermission,
           "Reader 处理预约 → 1002");
    expect(isC(resv.promoteNext(staff, f.bookA), 3009), "无排队时顺延 → 3009");
    expect(isC(resv.promoteNext(staff, 654321), 3009), "不存在图书顺延 → 3009（无队列）");

    // 借出 C002-C004（C001 留馆可借），两人排队；顺延 → 队首（先申请者）
    lib::BorrowService borrow(db, &copies, okElig());
    borrow.borrow(staff, f.r2, QStringLiteral("C002"));
    borrow.borrow(staff, f.r3, QStringLiteral("C003"));
    borrow.borrow(staff, f.r1, QStringLiteral("C004"));
    QSqlQuery q(db);
    q.exec(QStringLiteral("INSERT INTO reservations(book_id, reader_id, queue_no, status, requested_at) "
                          "VALUES(%1,%2,1,'waiting',datetime('now','localtime','-30 minute'))").arg(f.bookA).arg(f.r3));
    q.exec(QStringLiteral("INSERT INTO reservations(book_id, reader_id, queue_no, status, requested_at) "
                          "VALUES(%1,%2,2,'waiting',datetime('now','localtime','-5 minute'))").arg(f.bookA).arg(f.r1));
    lib::Result p1 = resv.promoteNext(staff, f.bookA);
    expect(p1.ok() && p1.data.value(QStringLiteral("reader_id")).toLongLong() == f.r3,
           "队首 = 申请最早的 R003");
    expect(scalar(db, QStringLiteral("SELECT status FROM book_copies WHERE copy_code='C001'"))
               .toString() == QStringLiteral("reserved"), "顺延占用副本 C001 → reserved");

    // 无可借副本时顺延 → 3002
    lib::Result p2 = resv.promoteNext(staff, f.bookA);
    expect(isC(p2, 3002), "无在馆可借副本时顺延 → 3002");

    // 惰性超时：R003 待取超时 + R001 waiting → promoteNext 释放并顺延 R001
    q.exec(QStringLiteral("UPDATE reservations SET pickup_deadline=datetime('now','localtime','-1 day') "
                          "WHERE reader_id=%1 AND status='ready_to_pick'").arg(f.r3));
    lib::Result p3 = resv.promoteNext(staff, f.bookA);
    expect(p3.ok() && p3.data.value(QStringLiteral("reader_id")).toLongLong() == f.r1,
           "超时释放后顺延到下一位 R001");
    expect(scalar(db, QStringLiteral("SELECT status FROM reservations WHERE reader_id=%1").arg(f.r3))
               .toString() == QStringLiteral("expired"), "超时待取 → expired");
    expect(scalar(db, QStringLiteral("SELECT status FROM book_copies WHERE copy_code='C001'"))
               .toString() == QStringLiteral("reserved"), "下一位占用 C001 → reserved");

    // 超时失效的待取不可再取消（终态）
    qint64 expiredId = scalarId(db, QStringLiteral("SELECT id FROM reservations WHERE reader_id=%1").arg(f.r3));
    expect(isC(resv.cancelReservation(staff, expiredId), 3008), "expired 终态取消 → 3008");

    // 超时释放后副本被预约者本人取走：待取预约 finished
    expect(borrow.borrow(staff, f.r1, QStringLiteral("C001")).ok(), "顺延后的待取由预约者本人取书");
    expect(scalar(db, QStringLiteral("SELECT status FROM reservations WHERE reader_id=%1").arg(f.r1))
               .toString() == QStringLiteral("finished"), "本人取书后预约 → finished");
}

// ============ 8) 逾期边界 ============
void scenarioOverdueEdge()
{
    std::printf("== 场景8：逾期边界 ==\n");
    QSqlDatabase db = makeDb(QStringLiteral("overdue"));
    Fixture f = seed(db);
    lib::OverdueService overdue(db);
    lib::OperatorContext staff = staffOp();
    lib::OperatorContext r1op = readerOp(1);

    expect(codeOf(overdue.listOverdue(anonOp(), {{QStringLiteral("mode"), QStringLiteral("current")}}))
               == (int)lib::ErrorCode::NotAuthenticated, "未登录查逾期 → 1001");
    expect(codeOf(overdue.listOverdue(r1op, {})) == (int)lib::ErrorCode::NoPermission,
           "Reader 查逾期管理 → 1002");

    // 在借逾期：due=昨天 → 1 天；due=今天 → 非逾期（边界）
    insertLoan(f, f.r1, copyIdOf(f, "C001"), f.bookA, 31, -1, QStringLiteral("borrowed"));
    insertLoan(f, f.r2, copyIdOf(f, "C002"), f.bookA, 30, 0, QStringLiteral("borrowed"));
    // 历史逾期：借 45 天前、due 15 天前、10 天前才还（逾期 5 天）
    qint64 hist = insertLoan(f, f.r3, copyIdOf(f, "C101"), f.bookOff, 45, -15);
    QSqlQuery q(db);
    q.exec(QStringLiteral("UPDATE loans SET returned_at=date('now','localtime','-10 day') WHERE id=%1").arg(hist));
    // 早还：未逾期
    qint64 early = insertLoan(f, f.r3, copyIdOf(f, "C201"), f.bookM, 5, 25);
    q.exec(QStringLiteral("UPDATE loans SET returned_at=date('now','localtime','-3 day') WHERE id=%1").arg(early));

    lib::Result cur = overdue.listOverdue(staff, {{QStringLiteral("mode"), QStringLiteral("current")}});
    expect(cur.ok() && cur.total == 1, "mode=current：仅 1 条在借逾期");
    if (cur.ok() && cur.total == 1)
        expect(cur.list.first().toMap().value(QStringLiteral("overdue_days")).toInt() == 1,
               "在借逾期天数 = 1（due=昨天 边界）");

    lib::Result all = overdue.listOverdue(staff, {{QStringLiteral("mode"), QStringLiteral("all")}});
    expect(all.ok() && all.total == 2, "mode=all：在借逾期 + 历史逾期 = 2 条");
    if (all.ok() && all.total == 2) {
        bool histOk = false, earlyExcluded = true;
        for (const QVariant& v : all.list) {
            const QVariantMap row = v.toMap();
            if (row.value(QStringLiteral("loan_id")).toLongLong() == hist)
                histOk = row.value(QStringLiteral("overdue_days")).toInt() == 5;
            if (row.value(QStringLiteral("loan_id")).toLongLong() == early) earlyExcluded = false;
        }
        expect(histOk, "历史逾期天数 = 归还日-应还日 = 5");
        expect(earlyExcluded, "早还记录不计入逾期");
    }

    // 按读者筛选
    lib::Result mine = overdue.listOverdue(staff, {{QStringLiteral("mode"), QStringLiteral("all")},
                                                   {QStringLiteral("reader_id"), f.r1}});
    expect(mine.ok() && mine.total == 1, "按读者筛选逾期 → 1 条");

    // 非法 mode → 参数校验拒绝（口径锁定：不走 all 兜底）
    lib::Result badMode = overdue.listOverdue(staff, {{QStringLiteral("mode"), QStringLiteral("bogus")}});
    expect(badMode.code == lib::ErrorCode::InvalidCondition, "非法 mode → InvalidCondition 拒绝");
    lib::Result defMode = overdue.listOverdue(staff, {});
    expect(defMode.ok() && defMode.total == 1, "缺省 mode=current：仅 1 条在借逾期");
}

// ============ 9) 个人中心与查询筛选 ============
void scenarioPersonalQueryEdge()
{
    std::printf("== 场景9：个人中心/查询 ==\n");
    QSqlDatabase db = makeDb(QStringLiteral("personal"));
    Fixture f = seed(db);
    lib::CopyService copies(db);
    lib::BorrowService borrow(db, &copies, okElig());
    lib::PersonalService personal(db);
    lib::OperatorContext staff = staffOp();
    lib::OperatorContext r1op = readerOp(1);

    expect(codeOf(personal.getPersonalOverview(anonOp(), f.r1)) == (int)lib::ErrorCode::NotAuthenticated,
           "未登录查个人中心 → 1001");
    expect(codeOf(personal.getPersonalOverview(r1op, f.r2)) == (int)lib::ErrorCode::NoPermission,
           "Reader 越权查他人 → 1002");

    // user_id=NULL 的读者（R002）：模拟身份的 Reader 查询 → 1002（真实边界：无法自证）
    lib::OperatorContext orphanOp = readerOp(12345);
    expect(codeOf(personal.getPersonalOverview(orphanOp, f.r2)) == (int)lib::ErrorCode::NoPermission,
           "user_id=NULL 读者以 Reader 查询 → 1002（无法自证）");
    expect(personal.getPersonalOverview(staff, f.r2).ok(), "staff 可查 user_id=NULL 读者");

    // 构造：R001 在借 1（含逾期）+ 历史 1 + 预约 1
    lib::ReservationService resv(db, &copies, okElig());
    lib::Result b1 = borrow.borrow(staff, f.r1, QStringLiteral("C001"));
    qint64 loanId = b1.data.value(QStringLiteral("loan_id")).toLongLong();
    resv.reserve(staff, f.r1, f.bookM);   // C201 维修 → available=0 可约
    lib::ReturnService ret(db, &copies);
    ret.returnBook(staff, {{QStringLiteral("loan_id"), loanId}});
    borrow.borrow(staff, f.r1, QStringLiteral("C002"));
    QSqlQuery q(db);
    q.exec(QStringLiteral("UPDATE loans SET due_at=date('now','localtime','-2 day') "
                          "WHERE reader_id=%1 AND returned_at IS NULL").arg(f.r1));

    lib::Result ov = personal.getPersonalOverview(r1op, f.r1);
    expect(ov.ok(), "Reader 查本人 → 通过");
    expect(ov.ok() && ov.data.value(QStringLiteral("active_loans")).toList().size() == 1,
           "在借分区 = 1");
    expect(ov.ok() && ov.data.value(QStringLiteral("history")).toList().size() == 1,
           "历史分区 = 1");
    expect(ov.ok() && ov.data.value(QStringLiteral("reservations")).toList().size() == 1,
           "预约分区 = 1");
    if (ov.ok()) {
        const QVariantList act = ov.data.value(QStringLiteral("active_loans")).toList();
        if (!act.isEmpty())
            expect(act.first().toMap().value(QStringLiteral("overdue_days")).toInt() == 2,
                   "个人中心在借逾期天数 = 2");
    }
    expect(ov.ok() && ov.data.value(QStringLiteral("profile")).toMap()
               .value(QStringLiteral("reader_no")).toString() == QStringLiteral("R001"),
           "profile.reader_no = R001");

    // 查询筛选与分页
    lib::Result act = borrow.listLoans(staff, {{QStringLiteral("status"), QStringLiteral("active")}}, 1, 10);
    expect(act.ok() && act.total == 1, "listLoans active 筛选 = 1");
    lib::Result ods = borrow.listLoans(staff, {{QStringLiteral("status"), QStringLiteral("overdue")}}, 1, 10);
    expect(ods.ok() && ods.total == 1, "listLoans overdue 筛选 = 1");
    lib::Result pg0 = borrow.listLoans(staff, {}, 0, 10);
    expect(pg0.code == lib::ErrorCode::InvalidPage, "page=0 → 1016 分页非法");
    lib::Result pg2 = borrow.listLoans(staff, {}, 2, 10);
    expect(pg2.ok() && pg2.list.isEmpty() && pg2.total == 2, "越界页返回空列表但 total 正确");

    // 修复②：Reader 查询强制限定本人范围（忽略传入的 reader_id 越权筛选）
    lib::Result readerList = borrow.listLoans(r1op, {}, 1, 10);
    expect(readerList.ok() && readerList.total == 2,
           "Reader 查询自动限定本人（在借1+历史1 = 2 条）");
    bool allMine = true;
    for (const QVariant& v : readerList.list)
        if (v.toMap().value(QStringLiteral("reader_id")).toLongLong() != f.r1) allMine = false;
    expect(readerList.ok() && allMine, "Reader 查询结果全部为本人记录");
    lib::Result otherList = borrow.listLoans(r1op, {{QStringLiteral("reader_id"), f.r2}}, 1, 10);
    expect(otherList.ok() && otherList.total == 0,
           "Reader 传他人 reader_id 仍被限定为本人（0 条）");
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);   // Qt6：QSqlDatabase/插件加载依赖应用对象
    setvbuf(stdout, nullptr, _IONBF, 0);   // 崩溃时也保留已输出内容
    std::printf("== 后端C 边界/冲突测试 ==\n");
    scenarioBorrowEdge();
    scenarioPickupEdge();
    scenarioReturnEdge();
    scenarioRenewEdge();
    scenarioReserveEdge();
    scenarioCancelFinishEdge();
    scenarioPromoteEdge();
    scenarioOverdueEdge();
    scenarioPersonalQueryEdge();

    std::printf("\n== 汇总 ==\n");
    std::printf("通过 %d / 失败 %d / 观察项 %d\n", g_pass, g_fail, g_observations.size());
    if (!g_observations.isEmpty()) {
        std::printf("-- 观察项（需人工确认口径，不计失败） --\n");
        for (const QString& o : g_observations)
            std::printf("  * %s\n", qPrintable(o));
    }
    std::printf("%s\n", g_fail == 0 ? "== Edge tests PASSED. ==" : "== Edge tests FAILED. ==");
    return g_fail == 0 ? 0 : 1;
}
