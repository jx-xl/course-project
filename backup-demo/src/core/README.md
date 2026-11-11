# B：备份与还原实现目录

该目录由 B 负责，实现文件系统备份、还原和备份链管理：

```text
src/core/
├─ FilesystemBackupManager.cpp
├─ FilesystemRestoreManager.cpp
└─ BackupStorage.h/.cpp
```

主要实现：

- `FilesystemBackupManager` 实现 `backup::BackupManager`。
- `FilesystemRestoreManager` 实现 `backup::RestoreManager`。
- 扫描结果必须使用相对路径，以便还原目录结构。
- 应用 `FilterManager` 判断文件是否进入备份集合。
- 捕获 `std::filesystem::filesystem_error`，转换为 `OperationResult`。
- 在扫描及每个文件复制边界检查取消回调，并持续报告进度。
- 私有辅助头文件留在本目录；只有需要跨模块使用的类型才能进入 `include/`。

对应具体实现声明位于 `include/backup/core/`，端到端测试位于 `tests/core/`。核心模块只依赖 C++17 标准库和筛选接口，不依赖 Qt 或界面代码。
