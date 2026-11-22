# LibraryManager 图书管理系统

四人课程项目。技术方案：C++17、Qt 6 Widgets、Qt SQL、MySQL 8（通过 ODBC）；使用 qmake 构建。
当前提供可运行的课程演示版，已经串通登录、馆藏入库、借书、还书、读者登记、
统计查看和 CSV 导出。

## 第一次运行

演示账号：`admin`，演示密码：`admin123`。

1. 在本机 MySQL 创建 `library_manager`，字符集选 `utf8mb4`。
2. 在 Windows 的 **ODBC 数据源（64 位）**配置 MySQL DSN，数据库选择 `library_manager`。本机的 DSN 名称是 `library_data`；团队成员可换成自己的 DSN 名称。
3. 在项目根目录的 PowerShell 运行：

```powershell
.\scripts\init-mysql.ps1 -Dsn library_data
.\scripts\build-demo.ps1 -Dsn library_data -Run
```

初始化脚本会创建所需表，并写入虚构样书、演示读者和默认登录账号。可以重复执行，已有同名记录不会被覆盖。DSN 可以提供 MySQL 登录信息；如果 DSN 没有提供登录信息，在启动程序和运行脚本的环境中设置 `LIBRARYMANAGER_DB_USER` 和 `LIBRARYMANAGER_DB_PASSWORD`。

使用 Qt Creator 时，打开根目录的 `LibraryManager.pro`，选择 `Desktop Qt 6.12.0 MinGW 64-bit` Kit，在运行环境里添加 `LIBRARYMANAGER_DSN=library_data`。在 VS Code 中可直接用上面的命令；若按 F5，请在调试配置的环境变量中增加同一项。

MSVC 套件需要对应的开发者命令行与 nmake/jom，不能混用 MinGW 编译器。
如果 Qt 安装目录不同，通过 `-QtKit` 和 `-MingwBin` 参数传入实际路径。

## 演示流程

1. 使用演示账号登录，查看工作台统计。
2. 在“图书与库存”新增一本书，设置复本数。
3. 在“读者管理”登记读者，或使用预置读者 `R001`。
4. 在“借还管理”选择读者和实体册办理借阅。
5. 选择当前借阅记录并归还。
6. 在“统计报表”查看借阅流水并导出 CSV。

自动业务测试：

```powershell
$env:LIBRARYMANAGER_DSN = 'library_data'
.\scripts\init-mysql.ps1 -Smoke
$p = Start-Process ..\build-LibraryManager\release\LibraryManager.exe -ArgumentList '--smoke-test' -Wait -PassThru
$p.ExitCode
```

退出码 `0` 表示通过。测试只写入独立的 `library_manager_smoke`，不会修改 `library_manager` 的演示数据。建测试库需要相应权限；测试账号须能访问测试库。

当前界面提供馆务工作台。账号管理、参数修改和操作日志查询已有后端服务，可通过独立测试验证，尚未接入管理界面。

## 目录与责任

```text
LibraryManager/
├── LibraryManager.pro     qmake 工程，A 协调维护
├── app/                   A：入口、主窗口、导航
├── common/                A：连接、公共类型与工具
├── auth/                  A：登录、身份和权限
├── books/                 B：书目、分类、实体书
├── circulation/           C：借还、续借、预约与事务
├── readers/               D：读者资料、类别及个人中心
├── statistics/            D：统计、CSV 导出
├── database/
│   ├── migrations/        版本化建表与升级 SQL
│   └── seeds/             可公开的虚构演示数据
├── resources/
│   ├── icons/             图标
│   └── styles/            公共样式
├── tests/                 业务测试代码和手工验收记录
├── scripts/               构建、打包等辅助脚本
├── docs/                  项目文档和组会记录
└── .github/               PR 模板
```

Git 不保存空目录，因此保留 `.gitkeep`；添加真实文件后可删除对应占位文件。
各成员先在自己的模块添加类，再由 A 将源码加入 `.pro` 并接入主窗口。

## 团队入口

- [Git 协作操作指南](docs/GIT_WORKFLOW.md)
- [分工与交付约定](docs/TEAM.md)
- [数据库与接口约定](docs/ARCHITECTURE.md)
- [阶段计划](docs/ROADMAP.md)
- [手工验收表](tests/ACCEPTANCE.md)

不要将课程安装包、真实读者信息、数据库凭据或数据库文件提交到仓库。新成员用迁移脚本建立自己的数据库。
