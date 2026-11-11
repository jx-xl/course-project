# 模块归档说明

`handoff/packages/` 用于按照小组分工整理代码快照。`team-workspace` 是全组共用的完整可编译工程，`owned-files` 包只保留对应成员负责的文件，方便检查模块边界和进行最终集成。

- `team-workspace.zip`：完整系统项目，B、C 使用同一份即可。
- `member-b-owned-files.zip`：B 负责的核心实现、具体类头文件和核心测试。
- `member-c-owned-files.zip`：C 负责的 GUI、筛选、调度、资源和相关测试。

实际分发时，B、C 都接收 `team-workspace.zip`；需要单独检查或回传个人负责内容时，再使用各自的 `owned-files` 包。

归档前应先完成本地构建和测试，保证各模块对应同一版公共接口。

在仓库根目录运行 `powershell -ExecutionPolicy Bypass -File scripts/New-HandoffPackages.ps1` 可重新整理以上四个归档。脚本会排除 `.git`、构建目录、Qt Creator 私有配置和本机 `CMakeUserPresets.json`。
