# 后端C：借阅流通、预约、逾期与推荐业务

> 负责人：C。契约基线：`docs/接口契约-C.md`（与 `library-backend-a/docs/接口契约.md`、
> 飞书工作清单第六节「接口统一」对齐）。

## 当前进度

| 功能 | 优先级 | 状态 |
|---|---|---|
| BorrowService（借阅 + 借阅记录查询） | P0 | ✅ 已实装（borrow / getLoan / listLoans） |
| ReturnService（归还 + 触发预约） | P0 | ✅ 已实装（returnBook + 条码归还 + 待取超时惰性释放） |
| RenewService（续借） | P1 | ✅ 已实装 |
| ReservationService（预约队列） | P1 | ✅ 已实装 |
| OverdueService（逾期识别） | P1 | ✅ 已实装 |
| PersonalService（个人中心聚合） | P1 | ✅ 已实装 |
| RecommendService（规则化推荐） | P2 | 骨架占位（3999） |

## 结构

```text
library-backend-c/
├── library-backend-c.pro     subdirs: src + tests
├── src/
│   ├── src.pro               静态库 backendc（复用 A 端 contract/DatabaseManager/CopyService）
│   ├── contract_c.h          C 段错误码 3000~3999、预约状态枚举、资格端口、参数读取辅助
│   └── services/             7 个 Service（P0/P1 六个已实装，Recommend P2 骨架）
├── tests/
│   ├── tests.pro             冒烟测试（无需数据库）
│   └── test_backend_c.cpp
└── docs/
    └── 接口契约-C.md          契约草案（评审后升 V1.0）
```

## 构建与测试

```powershell
qmake library-backend-c.pro
nmake          # 或 mingw32-make / jom
cd tests && nmake && .\backend_c_tests.exe
# 输出以 "Backend C smoke test passed." 结尾即通过（退出码 0）
```

## 对外依赖（联调前需拍板）

1. **B 板块适配**：`contract_c.h` 的 `EligibilityFn` 是 B 端 `checkReaderEligibility`
   的接入点——B 现为 `UserSession + bool` 风格，需 B 迁移到统一基线或提供适配函数。
2. **A 板块调用**：副本状态变更只走 `CopyService::changeCopyStatus`；C 侧无任何
   直接 UPDATE `book_copies` 的语句。
3. **表**：`loans` / `reservations` 以 `database/migrations/mysql/003_unified_schema_v1.sql`
   为准（C 牵头，已评审）。
