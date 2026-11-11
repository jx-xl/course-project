# 文件归属与集成边界

## A：组长与集成

A 维护并最终合并：

- `/CMakeLists.txt`、`/CMakePresets.json`、`/.gitignore`
- `/include/backup/core/` 中的公共请求、结果及备份/还原接口
- `/include/backup/filter/FilterManager.h`
- `/include/backup/scheduler/` 中的公共调度接口
- `/src/app/`、`/docs/`、README、演示、PPT 和最终发布文件

B/C 涉及公共接口的改动由 A 统一确认和合并，避免各模块出现不兼容定义。

## B：核心备份与还原

B 只需提交：

- `/src/core/**`
- `/tests/core/**`
- 确有必要时新增 `/include/backup/core/` 具体实现头文件，但不得破坏已有抽象接口

B 可以读取筛选接口，但不负责实现 `/src/filter/`。

## C：GUI、筛选与调度

C 只需提交：

- `/include/backup/gui/**`
- `/include/backup/filter/DefaultFilterManager.h`
- `/include/backup/scheduler/QtScheduler.h`
- `/src/gui/**`
- `/src/filter/**`
- `/src/scheduler/**`
- `/resources/**`
- `/tests/filter/**`、`/tests/scheduler/**`、`/tests/gui/**`

C 通过 `BackupManager`、`RestoreManager` 调用核心逻辑，不直接复制 B 的私有实现代码。

## 集成约定

1. 各成员只修改分工范围内的文件，公共接口与顶层构建配置由 A 统一维护。
2. `build/`、`.qtcreator/`、`CMakeUserPresets.json` 和 Qt Creator 用户配置不纳入版本控制。
3. 新增源码时同步记录需要加入的 CMake target，由 A 在集成阶段调整顶层 CMake。
4. 模块合并时记录完成项、已知问题和验证方法。
5. 集成完成后统一执行配置、构建、自动化测试和演示验收。
