# LibraryManager 图书管理系统

四人课程项目。技术方案：C++17、Qt 6 Widgets、Qt SQL、SQLite；使用 qmake 构建。
当前提供可运行的课程演示版，已经串通登录、馆藏入库、借书、还书、读者登记、
统计查看和 CSV 导出。

## 第一次运行

演示账号：`admin`，演示密码：`admin123`。

1. 在 Qt Creator 中打开根目录 `LibraryManager.pro`。
2. 选择 `Desktop Qt 6.12.0 MinGW 64-bit` Kit，使用独立构建目录。
3. 点击构建和运行。
4. 首次启动会创建本地 SQLite 数据库、虚构样书和演示读者。

本机也可以只运行下面的命令重新构建并启动：

```powershell
.\scripts\build-demo.ps1 -Run
```

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
..\build-LibraryManager\release\LibraryManager.exe --smoke-test
```

该命令使用临时数据库，不会修改演示数据。

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

不要将课程安装包、真实读者信息、密码或本地数据库提交到仓库。默认数据库位于
当前用户的 Qt `AppDataLocation` 目录，数据库文件已被 `.gitignore` 排除。
