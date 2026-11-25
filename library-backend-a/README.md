# library-backend-a · 图书馆管理系统 后端A（图书与馆藏业务）

基于 Qt Widgets 的桌面图书馆管理系统 · 板块二：图书与馆藏业务（BookService / CategoryService / CopyService / SearchService / AdvancedSearchService / ImportExportService）。

## 目录结构

```
library-backend-a/
├── library-backend-a.pro     # 顶层 qmake 工程（subdirs: src / demo / tests）
├── src/                      # 核心静态库 backenda
│   ├── database/             # DatabaseManager + 内嵌 9 表 SCHEMA_SQL
│   ├── services/             # 六个 Service（状态机唯一转换源在 CopyService）
│   └── src.pro
├── third_party/QXlsx/        # QXlsx 源码（xlsx 导入导出，免安装）
├── demo/                     # 控制台演示程序
├── gui/                      # Qt Widgets GUI 演示窗口（可视化验收：列表/检索/新增/下架/副本状态机/导入导出）
├── tests/                    # QTest 测试套件（23 个用例函数）
└── reports/                  # 测试报告 / 验收报告
```

## 构建与运行

```bash
# 环境：Qt 6.x (mingw_64) + MinGW，PATH 需含 Qt bin 与 MinGW bin
qmake library-backend-a.pro
mingw32-make -j4

# 运行测试（25/25 通过）
tests/release/backend_a_tests.exe -o results.txt,txt

# 运行演示（生成 demo_library.db 与 demo_out/ 导出文件）
demo/release/backend_a_demo.exe

# 运行 GUI 演示窗口（首次启动自动灌入演示数据：3 本书 / 4 个副本，含借出、维修中、预约保留各 1 本）
gui/release/backend_a_gui.exe
```

Qt Creator 打开 `library-backend-a.pro` 直接构建即可。

## 快速对接

- 共享的 Result、错误码和状态枚举定义见 [common/contract.h](../common/contract.h)。

- 接口契约（B/C/F 与 A 端必须一致的接触点，定稿基线）：[接口契约.md](../docs/接口契约.md)
  **注意：本文为团队评审后的定稿基线，改动需当天通知全员（对齐要求见契约文档开头）。**
- 数据库表结构（9 表 DDL，统一 V1.1）：[统一MySQL-DDL.md](../docs/统一MySQL-DDL.md)
- 副本状态机：`AVAILABLE ↔ BORROWED / RESERVED / MAINTENANCE / LOST`，唯一合法转换源在 `CopyService::changeCopyStatus`，C 板块借还/预约流程通过该接口驱动。
