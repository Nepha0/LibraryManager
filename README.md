# LibraryManager 图书管理系统

四人课程项目。技术方案：C++17、Qt 6 Widgets、Qt SQL、SQLite；使用 qmake 构建。
当前仅提供可导航的界面骨架，没有实现登录、数据库和借还业务。

## 第一次运行

1. 安装 Qt Creator、Qt 6 桌面套件及匹配的 MinGW 或 MSVC 编译器。
2. 在 Qt Creator 中打开根目录 `LibraryManager.pro`。
3. 选择已安装的 Desktop Kit，使用独立构建目录，点击配置、构建、运行。
4. 能看到五个模块入口即说明骨架启动成功。不要在源码目录中构建。

命令行方式（在匹配的 Qt 开发环境中运行）：

```powershell
mkdir build
cd build
qmake ../LibraryManager.pro
mingw32-make
```

MSVC 套件需要对应的开发者命令行与 nmake/jom，不能混用 MinGW 编译器。
实际借阅功能完成前，导航中的所有页面均为占位页面。

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

不要将课程安装包、真实读者信息、密码或本地数据库提交到仓库。
