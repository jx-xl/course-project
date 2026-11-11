# 测试目录

测试按照模块放入 `core/`、`filter/`、`scheduler/` 和 `gui/`，使用临时目录，不读写用户真实文件。顶层 CMake 已通过 CTest 启用接口、筛选、核心集成和调度持久化测试。
