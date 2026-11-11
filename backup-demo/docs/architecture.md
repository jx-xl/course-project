# 项目架构与集成约定

## 分层

```text
Qt Widgets GUI / Scheduler
          │
          ▼
  公共请求、结果与回调
          │
          ▼
BackupManager / RestoreManager
          │
          ▼
目录遍历、筛选、复制、元数据和文件系统
```

- `include/backup/core/`：A 维护的稳定公共契约。
- `src/core/`：B 实现备份、还原、目录遍历和基础异常处理。
- `include/backup/gui/`、`src/gui/`：C 实现界面及接口适配。
- 筛选和调度模块分别位于 `include/backup/filter/`、`src/filter/` 与 `include/backup/scheduler/`、`src/scheduler/`。

## 依赖规则

1. 核心接口只使用 C++17 标准库类型，不包含 Qt 头文件。
2. GUI 可依赖所有公共接口，核心实现不得依赖 GUI。
3. `DefaultFilterManager` 解释 `FilterRules`，核心遍历通过 `FilterManager` 接口逐项应用规则。
4. `Scheduler` 只决定何时发起任务，不重复实现文件复制逻辑。
5. GUI 的耗时调用放到 `QThread` 工作线程，并通过排队调用把进度返回主线程。

## 备份存储模型

- 每个成功快照是备份仓库下的独立目录，包含元数据、完整清单和本次需要保存的文件数据。
- 完整备份保存清单中的全部文件；增量备份以最近成功快照为父节点；差异备份以最近完整备份为父节点。
- 写入先在 `.partial` 临时目录完成，元数据落盘后再原子重命名，失败或取消不会成为有效基线。
- 还原按目标清单逐文件沿父链查找最近的数据副本，并拒绝清单中的绝对路径和 `..` 路径。

## 接口线程约定

- `BackupManager::backup` 和 `RestoreManager::restore` 是同步接口。
- 调用者负责选择执行线程；C 应从工作线程调用。
- 实现默认从执行该方法的线程调用 `ProgressCallback`。
- `CancellationCheck` 应在扫描和每个文件复制边界被轮询。
- 回调不得抛出异常；核心实现需要把文件系统异常转换为 `OperationResult`。

## 集成检查清单

- 公共头文件保持向后兼容，变更前由 A、B、C 共同确认。
- 新源码加入对应的 CMake target，禁止使用 Qt Creator 私有工程文件作为唯一构建入口。
- 提交前运行 Debug 构建并检查编译警告。
- 路径、空目录、大文件、只读文件、同名文件和取消操作至少各覆盖一个测试场景。
- GUI 仅在主窗口的组合根创建具体实现，成员和任务调用均通过抽象接口持有。

## 单文件归档模块

ArchiveManager 接收 PackRequest/UnpackRequest，复用 FilesystemRestoreManager 补齐备份链，并使用 BackupStorage 读写快照清单和元数据。包格式、路径约束、FNV 校验及事务式暂存提交由核心模块实现，不依赖 Qt 或外部归档库。Qt 主窗口仅负责表单、工作线程、进度及操作衔接。格式见 archive-format.md。
