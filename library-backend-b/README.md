# 后端 B：读者、账号与权限模块

本文档说明后端 B 已实现的数据库结构、Service 接口、权限规则、运行配置、测试方法和待联调事项。

`src/` 下是后端 B 的源码，`tests/` 下是 MySQL 冒烟测试。主程序和测试通过 `backend-b.pri` 共用源码清单。下文源码路径相对于本目录，数据库迁移和初始化命令以仓库根目录为基准。

## 1. 负责范围

后端 B 负责：

- 登录、退出和会话状态
- 用户账号管理
- 角色与操作权限
- 读者资料管理
- 读者借阅资格判断
- 系统参数
- 操作日志

原 PR 分支：`feature/core-reader-p0`。本地集成分支：`integration/pr1-mysql`。

## 2. MySQL 数据库

数据库名称：

    library_manager

后端 B 的迁移脚本：

    database/migrations/mysql/001_backend_b.sql

脚本包含以下表：

| 表名 | 用途 |
|---|---|
| `users` | 登录账号、密码哈希、角色和状态 |
| `readers` | 读者证号、姓名、联系方式和状态 |
| `system_params` | 借阅业务公共参数 |
| `operation_logs` | 不可修改、不可删除的操作记录 |

项目不再在程序启动时自动创建 SQLite 表。数据库结构统一由迁移 SQL 管理。

## 3. ODBC 运行配置

Qt 通过 `QODBC` 和 64 位用户或系统 DSN 连接 MySQL。DSN 指向 `library_manager`，Qt 套件需提供 `qsqlodbc.dll`。程序默认查找 `LibraryManagerDSN`；本机已配置的是 `library_data`，可通过 `LIBRARYMANAGER_DSN` 环境变量指定。

在项目根目录运行 `.\scripts\init-mysql.ps1 -Dsn library_data`。脚本按顺序执行 `001_backend_b.sql`、`002_demo_catalog.sql` 和 `003_unified_schema_v1.sql`，再导入 `database/seeds/demo_catalog.sql` 中的虚构书目。

若 DSN 已提供登录信息，应用无需额外的账号环境变量。需要覆盖时，在启动环境中设置 `LIBRARYMANAGER_DB_USER` 和 `LIBRARYMANAGER_DB_PASSWORD`。数据库凭据不提交到仓库。

## 4. 已实现 Service

### 4.1 AuthService

文件：

    src/auth/authservice.h
    src/auth/authservice.cpp

已实现：

- `login(username, password, error)`
- `logout(session)`
- 登录状态判断
- 管理员、图书管理员和读者角色判断
- 登录成功后生成权限集
- 退出后清空会话与权限
- SHA-256 加盐密码校验
- 不存在账号与错误密码使用统一提示
- 停用账号禁止登录

主要错误信息：

- `账号或密码错误`
- `账号已停用，请联系管理员`
- `系统异常，请稍后重试`

### 4.2 PermissionService

文件：

    src/auth/permissionservice.h
    src/auth/permissionservice.cpp

已实现：

- `permissionsForRole(role)`
- `hasPermission(session, permission)`
- `requirePermission(session, permission, error)`

权限项：

- `ViewBooks`
- `ManageBooks`
- `ViewReaders`
- `ManageReaders`
- `ManageCirculation`
- `ManageUsers`
- `ManageParams`
- `ViewOperationLogs`
- `ViewPersonalCenter`

角色矩阵：

| 权限 | 管理员 | 图书管理员 | 读者 |
|---|---:|---:|---:|
| 查看图书 | 是 | 是 | 是 |
| 管理图书 | 是 | 是 | 否 |
| 查看读者 | 是 | 是 | 否 |
| 管理读者 | 是 | 是 | 否 |
| 管理借还 | 是 | 是 | 否 |
| 管理用户 | 是 | 否 | 否 |
| 管理参数 | 是 | 否 | 否 |
| 查看操作日志 | 是 | 否 | 否 |
| 查看个人中心 | 是 | 否 | 是 |

未登录调用受限接口返回：

    请先登录

越权调用返回：

    无权限

### 4.3 ParamService

文件：

    src/system/paramservice.h
    src/system/paramservice.cpp

已实现：

- `getParam(key, error)`
- `getIntParam(key, error)`
- `setParam(session, key, value, error)`

支持参数：

| 参数键 | 用途 | 默认值 |
|---|---|---:|
| `borrow_period_days` | 借阅期限（天） | 30 |
| `borrow_limit` | 最大借阅数量 | 5 |
| `max_renew_times` | 最大续借次数 | 1 |
| `reservation_hold_days` | 预约待取期限（天） | 3 |
| `renew_period_days` | 续借延长期限（天） | 15 |

参数修改规则：

- 仅管理员可以修改
- 除 `max_renew_times` 可为 0 外，其他参数必须为正整数
- 参数修改与操作日志写入使用同一个事务
- 日志失败时参数修改回滚

### 4.4 OperationLogService

文件：

    src/system/operationlogservice.h
    src/system/operationlogservice.cpp

已实现：

- `record(...)`
- `listLogs(session, query, error)`
- 按操作类型筛选
- 按时间范围筛选
- 分页查询
- 仅管理员可查看
- 不提供修改或删除接口

当前使用的操作类型：

- `PARAM_UPDATE`
- `USER_CREATE`
- `USER_UPDATE`
- `USER_PASSWORD_RESET`
- `USER_ROLE_ASSIGN`
- `USER_STATUS_UPDATE`
- `READER_CREATE`
- `READER_UPDATE`
- `READER_STATUS_UPDATE`

### 4.5 UserService

文件：

    src/auth/userservice.h
    src/auth/userservice.cpp

已实现：

- 新增用户
- 账号唯一性校验
- 分页、关键词、角色和状态筛选
- 查询用户详情
- 编辑账号和姓名
- 重置密码
- 分配角色
- 禁止管理员取消自己的管理员角色
- 关键操作与操作日志同事务
- 启用和停用账号

接口：

- `addUser(session, username, displayName, password, role, error)`
- `updateUser(session, userId, username, displayName, error)`
- `resetPassword(session, userId, newPassword, error)`
- `assignRole(session, userId, role, error)`
- `setUserStatus(session, userId, enabled, error)`
- `getUser(session, userId, error)`
- `listUsers(session, query, error)`

账号状态规则：

- 仅管理员可以管理用户账号
- 管理员不能停用当前登录账号
- 关联读者存在未归还借阅时，不能停用账号
- 停用账号不能登录
- 状态修改与操作日志写入使用同一事务

### 4.6 ReaderService

文件：

    src/readers/readerservice.h
    src/readers/readerservice.cpp

接口：

- `addReader(session, readerNo, name, phone, error)`
- `updateReader(session, readerId, readerNo, name, phone, error)`
- `setReaderStatus(session, readerId, enabled, error)`
- `checkReaderEligibility(session, readerId, error)`
- `getReader(session, readerId, error)`
- `readers(session, query, error)`
- `readers(session, error)`

`ReaderQuery` 支持：

- 按读者证号筛选
- 按读者姓名筛选
- 按启用或停用状态筛选

`ReaderDetail` 提供：

- 读者基本资料
- 当前未归还数量
- 累计借阅数量
- 已归还数量
- 当前逾期数量

已实现：

- 新增、修改、详情和筛选查询
- 读者证号唯一性校验
- 启用和停用读者
- 存在未归还借阅时禁止停用读者
- 根据读者状态、关联账号状态和 `borrow_limit` 判断借阅资格
- 一次返回全部借阅资格拒绝原因
- 实际借书入口强制调用借阅资格判断
- 读者查询、修改和状态管理的业务层权限校验
- 新增、修改和状态变更自动写入操作日志
- 主界面提供筛选、详情、修改和启停的最小验收入口

### 4.7 跨模块业务接入

本次补充了后端 B 与现有业务 Service 的接入：

- `BookService` 的查询接口执行登录与 `ViewBooks` 权限校验
- 图书新增成功后写入 `BOOK_CREATE` 操作日志
- `CirculationService::borrowBook()` 调用读者借阅资格判断
- 借阅期限从 `borrow_period_days` 系统参数读取
- 借阅和归还分别写入 `LOAN_BORROW`、`LOAN_RETURN` 操作日志
- 当前借阅查询要求 `ManageCirculation` 权限
- 统计概览、借阅历史和 CSV 导出要求 `ManageCirculation` 权限
- 停用账号会使已经签发的旧会话立即并永久失效
- 账号重新启用后必须重新登录，旧会话不会恢复

## 5. 后端 B 冒烟测试

测试项目：

    library-backend-b.pro

测试使用独立 Qt Console Application，不依赖主界面。

Qt Creator 运行方法：

1. 打开 `library-backend-b.pro`。
2. 选择 `Desktop Qt 6.12.0 MinGW 64-bit` Kit。
3. 在项目根目录运行 `.\scripts\init-mysql.ps1 -Dsn library_data -Smoke`，然后在 Qt Creator 运行环境中设置 `LIBRARYMANAGER_DSN=library_data`。测试只访问 `library_manager_smoke`。
4. 执行 qmake。
5. 构建并运行。
6. 确认输出以 `Backend B smoke test passed.` 结束。

当前测试覆盖：

- Qt 通过 QODBC 连接 MySQL
- 正确管理员登录
- 错误密码
- 不存在账号
- 停用账号
- 登录与退出状态
- 管理员、图书管理员和读者权限矩阵
- 未登录和越权拒绝
- 五项系统参数读取
- 管理员修改参数
- 参数恢复
- 参数修改自动写入日志
- 操作日志写入和管理员查询
- 用户新增、重复校验、分页和详情
- 用户资料修改
- 密码重置
- 角色分配
- 用户操作日志
- 用户账号启用和停用
- 禁止管理员停用当前登录账号
- 关联读者有未归还借阅时禁止停用账号
- 读者新增、修改、查询和启用停用
- 重复读者证号校验
- 有未归还借阅时禁止停用读者
- 正常、停用、关联账号停用和达到上限时的借阅资格判断
- 读者新增、修改和状态变更日志
- 临时测试数据清理
- 真实管理员、图书管理员和读者账号登录
- 读者证号、姓名和状态筛选
- 单个读者详情及当前借阅概况
- 实际借书入口拒绝停用读者、停用账号和达到上限的读者
- 停用账号后已有会话立即失效
- 账号重新启用后旧会话仍然失效，新会话正常
- 图书、当前借阅、统计概览和借阅历史的角色权限
- `borrow_period_days` 修改后实际应还日期同步变化
- 图书新增、借阅和归还自动写入操作日志

测试会临时修改 `borrow_limit`，完成后自动恢复原值。

测试会创建临时用户、读者和借阅记录，完成后自动清理。操作日志按业务要求保留。

## 6. 与后端 C 的已确认契约

后端 C 的核心借阅表已经由以下脚本提供：

    database/migrations/mysql/002_demo_catalog.sql

`loans` 表当前包含：

- `reader_id`
- `copy_id`
- `borrowed_at`
- `due_at`
- `returned_at`

后端 B 已基于该表实现：

- 当前未归还借阅数量查询
- 有未归还借阅时禁止停用读者
- 有未归还借阅时禁止停用关联账号
- 根据借阅数量上限判断借阅资格

当前数据库尚未提供 `reservations` 表。现阶段后端 B 功能清单中的借阅资格仅判断读者状态、关联账号状态和借阅数量上限，因此不依赖预约表。未来增加预约业务时，需要与后端 C 重新确认预约状态字段和查询接口。

## 7. 前端接入状态

主界面已提供后端 B 的最小验收入口：

- 管理员可以登记读者
- 按读者证号、姓名和状态筛选
- 查看单个读者详情与借阅概况
- 修改读者基本资料
- 启用和停用读者
- 图书、借阅和统计页面调用带登录会话的权限接口

该界面用于验证后端 Service 接入。正式的页面布局、角色菜单、账号管理、参数管理和日志管理界面由前端负责人根据统一接口契约继续整合。

## 8. 安全与协作约定

- 不提交数据库密码。
- 不提交真实读者信息。
- 所有测试数据使用临时虚构数据。
- 参数和账号关键修改必须记录操作日志。
- 不使用 `git add .`，避免混入未完成文件。
- 未确认的新业务表和字段不得自行猜测，必须先与对应模块负责人确认。
- 前端接口未确认前，不伪造已废弃的读者类别兼容逻辑。
