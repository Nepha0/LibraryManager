# 后端 B：读者、账号与权限模块

本文档说明后端 B 已实现的数据库结构、Service 接口、权限规则、运行配置、测试方法和待联调事项。

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

在项目根目录运行 `.\scripts\init-mysql.ps1 -Dsn library_data`。脚本依次运行 `001_backend_b.sql` 和 `002_demo_catalog.sql`，补齐馆藏和借还所需的表，并导入虚构演示数据。

若 DSN 已提供登录信息，应用无需额外的账号环境变量。需要覆盖时，在启动环境中设置 `LIBRARYMANAGER_DB_USER` 和 `LIBRARYMANAGER_DB_PASSWORD`。数据库凭据不提交到仓库。

## 4. 已实现 Service

### 4.1 AuthService

文件：

    auth/authservice.h
    auth/authservice.cpp

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

    auth/permissionservice.h
    auth/permissionservice.cpp

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

    common/paramservice.h
    common/paramservice.cpp

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

    common/operationlogservice.h
    common/operationlogservice.cpp

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
- 后续读者模块将增加 `READER_CREATE`、`READER_UPDATE` 等类型

### 4.5 UserService

文件：

    auth/userservice.h
    auth/userservice.cpp

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

尚未实现：

- `setUserStatus()`

原因：禁用账号前必须检查关联读者是否仍有未完成借阅，该规则依赖后端 C 的借阅表。

### 4.6 ReaderService

当前集成版支持登记读者、列出读者和查询当前借阅数。借书时检查读者是否停用及是否达到系统借阅额度。读者资料编辑、状态管理和预约资格仍待开发。

## 5. 后端 B 冒烟测试

测试项目：

    tests/backend_b_smoke/backend_b_smoke.pro

测试使用独立 Qt Console Application，不依赖主界面。

Qt Creator 运行方法：

1. 打开 `backend_b_smoke.pro`。
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
- 临时测试数据清理

测试会临时修改 `borrow_limit`，完成后自动恢复原值。

测试会创建临时用户，完成后自动删除。操作日志按业务要求保留。

## 6. 待后端 C 确认的契约

后端 B 需要后端 C 提供最终 MySQL 建表 SQL，至少确认：

### loans

- `reader_id`
- `borrowed_at`
- `due_at`
- `returned_at` 或借阅状态字段
- 续借次数字段

### reservations

- `reader_id`
- 预约状态
- 申请时间
- 待取截止时间

后端 B 将据此实现：

- 当前借阅数
- 逾期数
- 预约数
- `checkReaderEligibility()`
- 读者状态限制
- 用户账号禁用限制
- 读者完整详情

## 7. 前端接入状态

主界面已采用不含读者类别的登记接口。新增馆藏、登记读者和借还采用统一的权限服务。借阅上限和期限从系统参数读取。账号管理、参数修改和日志查询目前仅提供后端接口。

## 8. 安全与协作约定

- 不提交数据库密码。
- 不提交真实读者信息。
- 所有测试数据使用临时虚构数据。
- 参数和账号关键修改必须记录操作日志。
- 不使用 `git add .`，避免混入未完成文件。
- 后端 C 表结构未确认前，不猜测借阅或预约字段。
- 前端接口未确认前，不伪造已废弃的读者类别兼容逻辑。