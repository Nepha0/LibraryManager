-- Fictional sample records for the unified MySQL schema.
-- DEMO-CATALOG-* identifiers are reserved for this dataset.
INSERT INTO categories (name) VALUES
    ('工程技术'), ('艺术设计'), ('经济管理'), ('儿童读物'), ('地方文献'), ('外文')
ON DUPLICATE KEY UPDATE id = categories.id;

INSERT INTO books (isbn, title, author, publisher, publish_date, category_id, intro, status)
SELECT sample.isbn, sample.title, sample.author, sample.publisher, sample.published,
       category.id, sample.intro, sample.status
FROM (
    SELECT 'DEMO-CATALOG-001' AS isbn, '雾港来信' AS title, '沈禾' AS author,
           '灯塔文艺社' AS publisher, '2023-04-12' AS published, '文学' AS category,
           '同名书目的第一版，适合验证书名和 ISBN 检索。' AS intro, 'active' AS status
    UNION ALL SELECT 'DEMO-CATALOG-002', '雾港来信', '沈禾', '灯塔文艺社', '2025-09-18', '文学', '同名书目的修订版，拥有独立副本。', 'active'
    UNION ALL SELECT 'DEMO-CATALOG-003', '从零搭建 Qt 借阅系统', '许澄、陈砚', '格点技术社', '2025-03-08', '计算机', '涵盖 Qt Widgets、数据库连接与桌面应用测试。', 'active'
    UNION ALL SELECT 'DEMO-CATALOG-004', 'SQL & ODBC: A Practical Notebook', 'Lena North', 'Maple Lab Press', '2024-08-21', '计算机', 'A hands-on notebook for SQL queries and ODBC connections.', 'active'
    UNION ALL SELECT 'DEMO-CATALOG-005', '旧城年表', '梁青', '北岸地方文献社', '2016-10-01', '历史', '以年表形式梳理虚构城市的地方史。', 'active'
    UNION ALL SELECT 'DEMO-CATALOG-006', '看得见的星空', '周原', '晨线科普社', '2022-05-20', '科学', '介绍星图、望远镜和夜空观测。', 'active'
    UNION ALL SELECT 'DEMO-CATALOG-007', '桥梁结构绘图入门', '唐棱', '河谷工学社', '2024-02-16', '工程技术', '提供工程制图和桥梁构件示例。', 'active'
    UNION ALL SELECT 'DEMO-CATALOG-008', '藏书票设计手记', '顾斐', '方格设计社', '2021-11-04', '艺术设计', '通过藏书票案例讲解构图和印刷。', 'active'
    UNION ALL SELECT 'DEMO-CATALOG-009', '社区馆预算实务', '宋简', '栖木管理社', '2020-07-15', '经济管理', '介绍社区图书馆预算和活动成本管理。', 'active'
    UNION ALL SELECT 'DEMO-CATALOG-010', '小狐狸的第一张借书证', '白梨', '纸船童书社', '2025-06-01', '儿童读物', '适合亲子阅读的图书馆故事。', 'active'
    UNION ALL SELECT 'DEMO-CATALOG-011', '港湾街巷地图集', '北岸地方志小组', '北岸地方文献社', '2018-12-09', '地方文献', '已建立书目，尚未入藏复本。', 'active'
    UNION ALL SELECT 'DEMO-CATALOG-012', 'Prairie Libraries After Dark', 'Mira Lane', 'Prairie Paper Press', '2019-10-22', '外文', 'An English-language collection of library stories.', 'active'
    UNION ALL SELECT 'DEMO-CATALOG-013', '田野观测散页', '许澄', NULL, NULL, NULL, NULL, 'active'
    UNION ALL SELECT 'DEMO-CATALOG-014', '旧版馆务工作手册', '馆务编写组', '纸桥实训社', '2012-01-01', '地方文献', '已下架的旧版书目，用于演示状态筛选。', 'shelved_off'
) AS sample
LEFT JOIN categories AS category ON category.name = sample.category
ON DUPLICATE KEY UPDATE id = books.id;

INSERT INTO book_copies (copy_code, book_id, location, status, acquired_date)
SELECT CONCAT(sample.isbn, '-C', sample.copy_no), books.id,
       sample.location, sample.status, sample.acquired
FROM (
    SELECT 'DEMO-CATALOG-001' AS isbn, '01' AS copy_no, '一楼文学区 L01' AS location, 'available' AS status, '2025-10-09' AS acquired
    UNION ALL SELECT 'DEMO-CATALOG-001', '02', '一楼文学区 L01', 'available', '2025-10-09'
    UNION ALL SELECT 'DEMO-CATALOG-001', '03', '修复室', 'maintenance', '2025-10-09'
    UNION ALL SELECT 'DEMO-CATALOG-002', '01', '一楼文学区 L01', 'available', '2025-11-08'
    UNION ALL SELECT 'DEMO-CATALOG-003', '01', '二楼技术区 T01', 'available', '2025-09-09'
    UNION ALL SELECT 'DEMO-CATALOG-003', '02', '二楼技术区 T01', 'available', '2025-09-09'
    UNION ALL SELECT 'DEMO-CATALOG-003', '03', '二楼技术区 T01', 'available', '2025-09-09'
    UNION ALL SELECT 'DEMO-CATALOG-004', '01', '二楼技术区 T02', 'available', '2025-07-01'
    UNION ALL SELECT 'DEMO-CATALOG-005', '01', '修复室', 'maintenance', '2024-11-06'
    UNION ALL SELECT 'DEMO-CATALOG-006', '01', '二楼科普区 S01', 'available', '2025-08-12'
    UNION ALL SELECT 'DEMO-CATALOG-006', '02', '二楼科普区 S01', 'lost', '2025-08-12'
    UNION ALL SELECT 'DEMO-CATALOG-007', '01', '二楼技术区 T05', 'available', '2025-06-05'
    UNION ALL SELECT 'DEMO-CATALOG-007', '02', '二楼技术区 T05', 'available', '2025-06-05'
    UNION ALL SELECT 'DEMO-CATALOG-008', '01', '一楼设计区 A02', 'available', '2025-07-25'
    UNION ALL SELECT 'DEMO-CATALOG-009', '01', '三楼经管区 M01', 'lost', '2024-02-04'
    UNION ALL SELECT 'DEMO-CATALOG-010', '01', '一楼少儿区 K01', 'available', '2025-08-31'
    UNION ALL SELECT 'DEMO-CATALOG-010', '02', '一楼少儿区 K01', 'available', '2025-08-31'
    UNION ALL SELECT 'DEMO-CATALOG-012', '01', '三楼外文区 F01', 'available', '2024-12-24'
    UNION ALL SELECT 'DEMO-CATALOG-013', '01', '', 'available', NULL
    UNION ALL SELECT 'DEMO-CATALOG-014', '01', '闭架库', 'maintenance', '2019-08-01'
) AS sample
JOIN books ON books.isbn = sample.isbn
ON DUPLICATE KEY UPDATE id = book_copies.id;
