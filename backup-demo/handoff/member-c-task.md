# C 模块说明：GUI、筛选与调度

C 负责以下目录和文件：

- `include/backup/gui/**`
- `include/backup/filter/DefaultFilterManager.h`
- `include/backup/scheduler/QtScheduler.h`
- `src/gui/**`
- `src/filter/**`
- `src/scheduler/**`
- `resources/**`
- `tests/filter/**`、`tests/scheduler/**`、`tests/gui/**`

该模块实现源/目标路径选择、自定义筛选、备份与还原入口、工作线程调用核心接口、进度及取消、结果提示、定时任务持久化、增删改查与启停。

GUI 不重复实现文件复制，也不依赖 B 的私有辅助类型。公共接口调整由 A 统一确认。
