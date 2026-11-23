# 接口统一 · 以后端A（图书与馆藏）已产出接口为准的对齐清单

> 状态：后端A 已完成并通过 25 项回归测试（代码见 `library-backend-a/`，完整签名见其 `docs/接口契约.md`）。
> 本节只列 **B、C、F 必须与 A 端保持一致的接触点**。以下内容为定稿基线，改动需当天通知全员（需求文档第 10 节变更原则）。

## 1. 全员统一基线（B / C / F 都要用）

| 项 | 统一后的标准 | 各方动作 |
|---|---|---|
| 返回结构 | 所有 Service 方法统一返回 `Result{code, message, data, list, total}`；分页查询 `list`=当前页、`total`=总数 | B、C 的 Service 同样采用此结构；F 按此结构解析 |
| 错误码 | 数字枚举（A 已用 1001~1999 段）；B 补充 2000~2999 段（读者/账号/权限）、C 补充 3000~3999 段（借阅/预约），新增码须登记 | B、C 不再造自己的错误码风格；F 按 code 分支展示 |
| 错误信息 | `message` 已按"可直接透出给用户"撰写 | F 直接展示 message，**不自行拼文案** |
| 操作者上下文 | `OperatorContext{userId, userName, role, authenticated}`：B 的 login 产出，F 从会话构造后作为**第一个参数**传给所有业务方法 | B 定 Session 字段与此结构对应；A/C 方法签名已按此实现 |
| 分页 | `page≥1, pageSize≥1`，非法返回 1016 InvalidPage | F 提交前校验；B/C 照抄此约定 |

## 2. F 端（前端）以 A 端为准的部分

**页面 → 接口对应**（F 只能调这些，不另造接口）：

| 页面 | 调用 A 端接口 |
|---|---|
| 图书列表页 / 检索页 | `listBooks(filter, page, pageSize)`、`search(keyword, page, pageSize)`、`advancedSearch(conditions, orderBy, page, pageSize)` |
| 图书详情弹窗 | `getBook(bookId)`（data 已含 copy_total/copy_available 等统计）+ `copyStatistics(bookId)` |
| 副本管理页 | `addCopy / updateCopy / changeCopyStatus / listCopiesByBook` |
| 分类管理页 | `listCategories / addCategory / updateCategory / deleteCategory` |
| 导入导出 | `exportBooks / exportCopies / importBooks / importCopies` |

**字段与状态约定**：

- 图书列表行字段名（advancedSearch/search/listBooks 返回一致）：`book_id, isbn, title, author, publisher, publish_date, category_name, copy_total, copy_available, borrowable, status_text("可借"/"不可借"), locations_text`——**馆藏状态必须按 `borrowable` 区分可借/不可借**，不允许只显示"有馆藏"。
- 状态枚举与 Badge：图书 `ACTIVE绿/SHELVED_OFF灰`；副本 `AVAILABLE绿(在馆可借)、BORROWED蓝(借出)、RESERVED橙(预约保留待取)、MAINTENANCE/LOST灰红(维修中/遗失报损)`。
- 副本状态变更非法时后端返回 1007 IllegalStatusTransition（message 含当前状态中文名），F 原样弹出即可。
- 导入结果：成功行数在 `data.imported_rows`，错误行汇总在 `list`（每项 `{row, reason}`，行号含表头）。

## 3. C 端（借阅流通，未开工——直接按此实现）

C 的借出/归还/预约**只能通过以下 A 端方法驱动副本状态**，禁止直接 UPDATE copies 表（状态机唯一合法转换源在 CopyService，含并发乐观锁）：

| C 的业务 | 对 A 端的调用顺序 |
|---|---|
| 借阅 borrow | ① `getCopyByBarcode(op, barcode)` → 校验 `data.status=="AVAILABLE"` 且 `data.book_status=="ACTIVE"`；② 插入借阅记录（C 自己的事务）；③ `changeCopyStatus(op, copy_id, "BORROWED")`。②失败须回滚 ③。 |
| 归还 return | 更新借阅记录后：无排队预约 → `changeCopyStatus(copy_id, "AVAILABLE")`；队首转待取 → `changeCopyStatus(copy_id, "RESERVED")`（C 同时写预约表待取截止时间）。 |
| 预约取书 | 预约者取书借阅 → 走借阅流程（副本当前即 RESERVED，合法转换 RESERVED→BORROWED）。 |
| 预约失效/取消 | 释放副本 → `changeCopyStatus(copy_id, "AVAILABLE")`。 |

**C 必须遵守的状态机**（合法转换表，其余一律被 1007 拒绝）：

```
在馆可借 AVAILABLE  → 借出 / 预约保留待取 / 维修中 / 遗失报损
借出 BORROWED       → 在馆可借（归还）/ 预约保留待取（归还且有预约）
预约保留待取 RESERVED → 借出（预约者取书）/ 在馆可借（超时释放）
维修中 / 遗失报损    → 仅管理员角色恢复或终结（OperatorContext.role 传 Admin）
```

- 并发双借同一副本：changeCopyStatus 的乐观锁会让后到者收到 1007 + 当前状态，C 据此回滚借阅记录并提示。
- 下架图书不可借出：A 侧已做防御校验（1013 BookShelvedOff），C 无需重复实现，但要透出该错误。
- 逾期判定（在借且应还日期<今天）由 C 自己的借阅表实现，与 A 无耦合。

## 4. B 端（读者/账号）以 A 端为准的部分

- 采用第 1 节统一基线（Result 结构 / 错误码 2000 段 / OperatorContext / 分页），与 A 完全同构。
- B 不直接调用 A 的业务方法；停用读者禁止新借阅由 B 的 `checkReaderEligibility(读者ID)` 统一出口，C 在借阅事务第一步调用（A、B 判断分离：副本状态归 A，读者资格归 B）。
- 读者详情页"当前借阅/预约"分区数据由 C 提供；"逾期数"由 C 的借阅表实时判定提供——B 侧读者概况接口预留这三个聚合位即可。

---
*完整方法签名、入参出参字段、错误码全表见 `library-backend-a/docs/接口契约.md`；评审通过后本节并入工作清单第 5.1 节（阶段 0 契约产物）。*
