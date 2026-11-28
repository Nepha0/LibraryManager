# 图书馆管理系统 · 后端C（借阅流通、预约、逾期与推荐业务）
TEMPLATE = subdirs
CONFIG += ordered
SUBDIRS = src tests edge_tests

edge_tests.file = tests/test_edge.pro
edge_tests.depends = src
