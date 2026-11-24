# 后端C 接口契约（阶段0 产出物）

> 供前端（F）、后端 A/B 对接使用。所有 Service 方法统一返回 `lib::Result`（与 A 的 `contract.h` 同构）。
> 契约变更须当天通知全员并更新本文档（工作清单第 10 节变更原则）。

## ⚠️ 文档状态：后端C 单方草案，待全组对齐后升为 V1.0 正式契约

本文档由 C 依据工作清单板块四 4.2 完成标准、飞书工作清单第六节「接口统一」基线（表格 jTpgV6 / RuFk9R）以及 A 端 `library-backend-a/docs/接口契约.md` 整理。
评审确认前不具备约束力；对齐后版本号升为 V1.0。

## 0. 依赖基线（与 A 端完全一致，不再复述）

| 项 | 约定 |
|---|---|
| 返回结构 | `Result{code, message, data, list, total}`，分页 `list`=当前页、`total`=总数 |
| 操作者上下文 | `OperatorContext{userId, userName, role, authenticated, session_generation}`，第一个参数 |
| 状态枚举 | 小写字面量：副本 `available/borrowed/reserved/maintenance/lost`；预约 `waiting/ready_to_pick/finished/cancelled/expired` |
| 分页 | `page≥1, pageSize≥1`，非法返回 1016 |
| 错误码 | 复用 A 的 1000~1999 段中通用码；C 自有业务码占用 **3000~3999 段**（见 §1） |

**C 事务与并发总原则**：

1. 副本状态变更**只能**通过 `CopyService::changeCopyStatus(op, copyId, "borrowed"/"available"/"reserved")` 驱动，禁止直接 UPDATE `book_copies`（状态机唯一合法转换源在 A，含乐观锁；并发双借后到者收 1007）。
2. C 拥有并只写 `loans`、`reservations` 两张表；"一副本一有效借阅"由数据库生成列唯一约束 + 业务校验双保险。
3. 事务由 C 的业务层统一控制（QSqlDatabase::transaction / commit / rollback），任一步失败整体回滚，无脏数据。
4. 逾期判定（在借且 `due_at < 今天`）由 C 实时计算，与 A 无耦合；**全程无费用字段**。
5. 课程设计规模不做定时任务：预约超时失效在每次预约队列操作前惰性触发（先释放过期待取，再执行本次操作）。

## 1. C 段错误码（3000~3999，新增须登记）

| code | 枚举 | 含义 |
|---|---|---|
| 0 | Success | 成功 |
| 3001 | ReaderNotEligible | 读者资格不通过（message 逐条列出 B 返回原因） |
| 3002 | CopyNotAvailable | 该副本当前不可借（message 含当前状态中文名） |
| 3003 | LoanNotFound | 借阅记录不存在或已归还 |
| 3004 | RenewRejected | 续借不通过（message 逐条列出全部不通过原因） |
| 3005 | DuplicateReservation | 同一读者同书已有排队中/待取预约 |
| 3006 | ReservationNotAllowed | 该书当前有可借副本，不可提交预约 |
| 3007 | ReservationNotFound | 预约记录不存在 |
| 3008 | IllegalReservationTransition | 预约状态流转非法（message 含当前状态） |
| 3009 | NoQueueToPromote | 无可推进的排队预约 |
| 3999 | NotImplemented | 功能尚未实现（骨架期占位，P1/P2 交付后移除） |
| 1001/1002 | —（复用 A） | 未登录 / 无权限（个人中心越权、非馆员办借还等） |
| 1010/1011/1013 | —（复用 A） | 图书不存在 / 副本不存在 / 图书已下架（透出 A 返回码） |
| 1016/1999 | —（复用 A） | 分页参数非法 / 数据库错误 |

## 2. BorrowService（借阅，P0）

```cpp
explicit BorrowService(QSqlDatabase db);

// 借阅（事务）
// op 需 staff（管理员/图书管理员，对应 ManageCirculation）；预约取书同走本接口
Result borrow(const OperatorContext& op, qint64 readerId, const QString& copyBarcode);
// → data: { loan_id, reader_id, copy_id, book_id, borrowed_at("yyyy-MM-dd"), due_at }

Result getLoan(const OperatorContext& op, qint64 loanId);          // data=借阅记录（含书名/证号/条码冗余字段）
Result listLoans(const OperatorContext& op, const QVariantMap& filter, int page = 1, int pageSize = 10);
// filter 均可省略：reader_id / book_id / barcode(模糊) / reader_no(模糊) / from / to("yyyy-MM-dd")
//                / status: "active"(在借) | "returned"(已还) | "overdue"(在借且应还日期<今天)
// → list 行字段见 §3 借阅记录行结构, total
```

**borrow 事务步骤**（每步失败返回具体码，禁止只报"操作失败"）：

1. `op.authenticated` 校验 → 1001；非 staff → 1002。
2. 调 **B 板块** `checkReaderEligibility(readerId)` → 不通过 → 3001（message 拼接原因列表）。资格判断与副本判断分离，C 不重复实现读者侧规则。
3. A `getCopyByBarcode(barcode)` → 副本不存在 1011；`data.status != "available"` → 3002（含当前状态）；`data.book_status == "shelved_off"` → 1013（A 防御校验，C 透出）。
4. 二次业务校验：该副本已存在有效借阅 → 3002（数据库唯一约束兜底，双保险）。
5. INSERT `loans(reader_id, copy_id, book_id, borrowed_at=CURRENT_DATE, due_at=CURRENT_DATE+borrow_period_days)`。借阅期限从 ParamService `borrow_period_days` 读取，不硬编码。
6. A `changeCopyStatus(op, copy_id, "borrowed")` → 失败（含并发 1007）即整体回滚并透出原因。

## 3. ReturnService（归还，P0）

```cpp
// 归还（事务）。locator 二选一：loan_id 或 copy_barcode（字段名见下）
Result returnBook(const OperatorContext& op, const QVariantMap& locator);
// locator: {"loan_id": qint64} 或 {"copy_barcode": QString}（按条码定位该副本唯一在借记录）
// → data: { loan_id, returned_at, overdue_days(0=未逾期), reservation_triggered(bool),
//           copy_status("available"|"reserved") }
```

**事务步骤**：

1. 权限校验同 borrow。
2. 定位借阅记录：`returned_at IS NULL`，否则 → 3003。
3. UPDATE 记录（`returned_at=CURRENT_DATE`）。
4. 逾期天数 = `returned_at - due_at`（仅展示，无费用）。
5. 查该图书预约队列（`status='waiting'` 按申请时间排序）：
   - 有队首 → A `changeCopyStatus(op, copy_id, "reserved")` + 队首转 `ready_to_pick`、写 `pickup_deadline = 现在 + reservation_hold_days` → `reservation_triggered=true`；
   - 无 → A `changeCopyStatus(op, copy_id, "available")`。
6. 任一步失败整体回滚。

## 4. RenewService（续借，P1）

```cpp
Result renew(const OperatorContext& op, qint64 loanId);
// → data: { loan_id, new_due_at, renew_count }
```

**规则（全部通过才可续借；任一不通过 → 3004，message 逐条列出全部原因）**：

| # | 校验 | 不通过文案（message 用语） |
|---|---|---|
| 1 | 记录处于在借状态 | "该借阅记录已归还" |
| 2 | `renew_count < max_renew_times`（参数） | "已达到最大续借次数（x/x）" |
| 3 | 该图书无**其他读者**的排队中/待取预约 | "该图书存在有效预约，无法续借" |
| 4 | 读者账号与读者状态正常（复用 B 资格判断的账号类原因；借阅上限不参与续借判断） | "读者账号状态异常" |
| 5 | 无未处理逾期（该记录当前不在借且应还日期<今天的状态） | "存在未处理逾期，请先归还" |

新应还日期 = 原应还日期 + `renew_period_days`（参数）；成功后 `renew_count + 1`。

**权限**：staff 可续借任意记录；Reader 角色仅可续借本人记录（按 `readers.user_id == op.userId` 判定，否则 1002）。

## 5. ReservationService（预约，P1）

```cpp
Result reserve(const OperatorContext& op, qint64 readerId, qint64 bookId);
// → data: { reservation_id, queue_no, requested_at }

Result cancelReservation(const OperatorContext& op, qint64 reservationId);
// waiting→cancelled 直接取消；ready_to_pick→cancelled 需先释放副本（A: changeCopyStatus → available）

Result promoteNext(const OperatorContext& op, qint64 bookId);
// "处理"：队首 waiting→ready_to_pick；从该书当前 available 副本中选一本
// changeCopyStatus → "reserved"，写 pickup_deadline；无排队 → 3009；无可用副本 → 3002

Result finishReservation(const OperatorContext& op, qint64 reservationId);
// ready_to_pick→finished（预约者取书借阅成功时由 borrow 流程自动完成，亦可手工确认）

Result listReservationQueue(const OperatorContext& op, qint64 bookId, int page = 1, int pageSize = 10);
// 预约管理页队列表。行: reservation_id, reader_id, reader_no, reader_name,
//                      queue_no, status, requested_at, pickup_deadline

Result listReservations(const OperatorContext& op, const QVariantMap& filter, int page = 1, int pageSize = 10);
// filter: reader_id / book_id / status(waiting|ready_to_pick|finished|cancelled|expired)
// 个人中心、读者详情页用
```

**reserve 规则**：仅当该书 `copy_available == 0`（A `copyStatistics`）时可提交 → 否则 3006；同一读者同书不可重复排队（业务校验 → 3005；`uk_active_reservation` 唯一约束兜底）；读者须有资格（复用 B 资格判断账号类原因 → 3001）。`queue_no` = 该书当前最大排队号 + 1。

**超时失效（惰性触发）**：`ready_to_pick` 且 `pickup_deadline < 现在` → 状态转 `expired`、副本 A `changeCopyStatus → "available"`，并自动 `promoteNext` 顺延下一位。

**状态机**（唯一合法转换源：ReservationService）：

```
waiting      → ready_to_pick（归还触发/手工处理）| cancelled（取消）
ready_to_pick → finished（待取借出完成）| cancelled（取消，释放副本）
finished / cancelled / expired → 终态
```

## 6. OverdueService（逾期识别与展示，P1）

```cpp
Result listOverdue(const OperatorContext& op, const QVariantMap& filter, int page = 1, int pageSize = 10);
// filter: mode: "current"(默认，仅逾期在借) | "all"(含已归还的历史逾期)；reader_id / book_id 可选
// → list 行: loan_id, reader_id, reader_no, reader_name, book_id, title,
//            barcode, due_at, overdue_days, status_text("在借-逾期"/"已归还")
```

实时判定：`mode=current` → `returned_at IS NULL AND due_at < CURRENT_DATE`；`mode=all` 追加已还但归还日晚于应还日的记录。**无费用列、无结清操作**。

## 7. PersonalService（个人中心聚合，P1）

```cpp
Result getPersonalOverview(const OperatorContext& op, qint64 readerId);
// → data 分区:
//   profile:      { reader_id, reader_no, name, phone, status, registered_at }   // 读 B 侧表
//   active_loans: [ { loan_id, book_id, title, barcode, borrowed_at, due_at, overdue_days, renew_count } ]
//   history:      [ 同上 + returned_at ]
//   reservations: [ { reservation_id, book_id, title, queue_no, status, requested_at, pickup_deadline } ]
//   overdue:      [ { loan_id, title, due_at, overdue_days } ]
```

**越权强制**：`op.role == Reader` 时必须 `readers.user_id == op.userId`，否则 1002（业务层强制，非仅前端隐藏）；不提供任何切换读者的查询参数。

## 8. RecommendService（规则化推荐，P2）

```cpp
Result recommend(const OperatorContext& op, qint64 readerId);
// → list 行复用 A §10 检索结果行结构（book_id, isbn, title, author, publisher, publish_date,
//   category_name, copy_total, available_count, borrowable, status_text, locations_text）
```

规则：优先"读者历史借阅中出现最多的 2~3 个分类下的在馆可借图书"；不足回退"全馆近期借阅热度 Top N"（冷启动）；排除已借过、已下架图书；结果全部来自真实馆藏，不做模型训练。接口失败由前端回退检索入口。权限同 PersonalService（只能给自己推荐）。

## 9. F 端页面 ↔ C 接口对应

| 页面 | 调用 |
|---|---|
| 借阅办理页 | BorrowService::borrow（校验失败文案=code 分支 + message） |
| 归还办理页 | ReturnService::returnBook（"逾期 x 天"= data.overdue_days；"转预约保留"= data.reservation_triggered） |
| 借阅记录页 | BorrowService::listLoans + RenewService::renew |
| 预约管理页 | ReservationService::listReservationQueue / promoteNext / cancelReservation / finishReservation |
| 逾期管理页 | OverdueService::listOverdue（mode 筛选） |
| 个人中心 | PersonalService::getPersonalOverview + RenewService::renew（"申请续借"）+ RecommendService::recommend |
| 首页待处理摘要 | OverdueService::listOverdue(mode=current, 仅统计) + ReservationService::listReservations(status=waiting/ready_to_pick) |

## 10. 与 A / B 的对接点与待对齐事项

- **对 A**：C 全部副本状态变更走 `changeCopyStatus`（§0）；`getCopyByBarcode` / `copyStatistics` 用于借阅与预约校验；A 端错误码 1007/1011/1013 原样透出给 F。
- **对 B**：借阅与预约第一步调用 `checkReaderEligibility(读者ID)`，只消费"账号/读者状态"类原因（借阅上限仅借阅场景生效）。⚠️ B 当前实现为 `UserSession + bool + QString*error` 风格，与统一基线的 `OperatorContext + Result` 不同构——**需 B 确认迁移时间表，或由 C 侧薄适配层过渡，联调前拍板**。
- **对 A（集成）**：根目录 `circulation/circulationservice.cpp` 为演示版遗留（直接 UPDATE book_copies、无续借/预约/逾期），C 按 4.2 完成标准重写后，旧文件与 `LibraryManager.pro` 的取舍由 A 统一集成处理。
- **表结构**：`loans`（含 book_id、renew_count，borrowed_at/due_at 为 DATE）与 `reservations`（queue_no、pickup_deadline、5 态、active_rid 生成列唯一约束）以 `database/migrations/mysql/003_unified_schema_v1.sql` 为准，C 已评审无异议。

## 11. 测试承诺（对应工作清单 4.2）

- 借阅：正常借阅 / 并发双借被拒（1007 回滚）/ 停用读者被拒 / 超上限被拒 / 下架书被拒 / 事务回滚无脏数据。
- 归还：正常 / 逾期（天数正确）/ 触发预约转待取 / 三处联动一致。
- 续借：5 条拒绝原因各至少 1 条用例 + 正常续借次数与日期顺延。
- 预约：可借时被拒 / 重复排队被拒 / 完整队列流转（排队→待取→借出完成 / 超时失效顺延）。
- 逾期：跨应还日期边界识别正确。
- 个人中心：越权查询返回 1002。
- 推荐：有历史 / 新读者 / 无数据三场景结果合理。

---

*评审通过后本文档并入工作清单第 5.1 节契约产物，与 A 端契约同级入库共享。*
