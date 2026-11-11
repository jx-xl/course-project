> 统计修复：备份跳过总数 = 筛选排除 + 未变化免复制；界面和日志显示明细，排除目录中的普通文件也纳入已知计数。不可访问的排除路径另行提示未完整统计。说明见 ../portable/统计修复说明.txt。

> 本版新增打包/解包页和自定义 DBPACK 单文件格式，可导出完整、增量、差异快照并补齐依赖。导入后是可独立还原的完整快照；说明见 ../打包解包现场演示.md，格式见 docs/archive-format.md。

> 新增自定义加分维度：名字通配符和修改时间筛选。现在支持路径、类型、名字、时间、尺寸五个维度；完整演示见 ../新增加分项演示.md。新版已通过 9 项 CTest，包括真实 GUI 名字/时间/路径/类型组合和一分钟定时触发。直接运行 ../portable/Start.cmd。

> 本次已调试交付：直接运行 ../portable/Start.cmd；详细步骤见 ../演示说明.md，测试结论见 ../核验与调试报告.md。GUI 新增执行日志；定时任务支持保留最新 N 个快照并保护备份链。当前推荐构建脚本：scripts/Build-Verified.ps1。下面保留原项目说明，原“本机已配置”描述不代表你的电脑已有该路径。

# 文件备份系统

一个面向 Windows 桌面环境的本地文件备份课程项目，使用 C++17、Qt 6 Widgets、MinGW-w64 和 CMake 开发。系统支持完整备份、增量备份、差异备份、筛选、备份链还原、冲突处理、进度取消和应用内定时任务。

## 1. 项目功能

- 选择任意本地目录作为备份源和备份仓库
- 创建完整、增量或差异快照
- 按扩展名、相对路径、文件名通配符、修改时间、文件大小和隐藏属性筛选
- 保存相对目录结构和空目录
- 从完整或增量/差异备份链还原文件
- 遇到同名文件时选择覆盖、跳过或自动重命名
- 显示当前阶段、文件数、字节数和整体进度
- 在安全边界取消正在进行的备份或还原
- 创建单次、每日、每周或自定义间隔定时任务
- 将定时任务保存在本机设置中
- 可选逐字节校验备份或还原结果

## 2. 小组分工

| 成员 | 主要工作 |
| --- | --- |
| A 组组长 | 公共接口设计、模块集成、Git、CMake、构建环境、需求文档、测试组织、演示和最终提交 |
| B | `BackupManager`、`RestoreManager`、目录遍历、文件复制、备份链、目录结构还原和异常处理 |
| C | Qt GUI、`FilterManager`、自定义筛选、`Scheduler`、定时任务及界面与核心模块对接 |

## 3. 最快运行方式

### 3.1 当前开发电脑直接运行

本机已经配置好 Qt 和 MinGW，并完成构建。直接运行：

```text
build\local-qt-debug\data_backup.exe
```

构建完成时 CMake 会自动执行 `windeployqt`，把 Qt DLL、MinGW 运行库和 `platforms/qwindows.dll` 放到程序旁边。复制程序到另一台电脑时，需要复制整个可执行文件目录，不能只复制一个 `.exe`。

### 3.2 已安装 Qt 的电脑一键构建并运行

在仓库根目录打开 PowerShell：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/Build-And-Run.ps1
```

脚本会自动完成：

1. 检测 `C:\Qt` 和当前用户目录下的 Qt。
2. 查找 Qt 6.8.3、MinGW-w64、CMake 和 Ninja。
3. 生成本机专用的 `CMakeUserPresets.json`。
4. 配置并构建 Debug 版本。
5. 运行全部自动化测试。
6. 测试通过后启动桌面程序。

### 3.3 全新 Windows 电脑自动安装并运行

如果电脑尚未安装 Qt，可运行：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/Build-And-Run.ps1 -InstallMissing
```

该模式需要网络连接和 Windows 的 `winget`。脚本会在缺失时安装 Python，通过 `aqtinstall` 下载 Qt 6.8.3 MinGW 套件、MinGW、CMake、Ninja 和 Qt Creator，然后完成配置、构建、测试和启动。

自动安装默认写入：

```text
%USERPROFILE%\Qt
```

下载体积较大，第一次执行需要一些时间。学校网络或代理环境下载失败时，建议改用下面的 Qt 官方图形安装方式。`aqtinstall` 的命令和组件查询方式可参考其[项目文档](https://aqtinstall.readthedocs.io/en/stable/)。

## 4. 手动配置开发环境

Qt 官方建议桌面新用户使用 Qt Online Installer；官方安装说明见 [Get and Install Qt](https://doc.qt.io/qt-6/get-and-install-qt.html)。

### 4.1 所需组件

- Windows 10 或 Windows 11，64 位
- Qt 6.8.3 MinGW 64-bit（最低兼容版本为 Qt 6.2）
- MinGW-w64 13.1 64-bit
- CMake 3.21 或更高版本
- Ninja
- Qt Creator（推荐，但命令行构建不强制需要）
- Git（用于团队协作）

### 4.2 使用 Qt Online Installer

打开 Qt 安装器后，在组件选择页面勾选：

```text
Qt
└─ Qt 6.8.3
   └─ MinGW 13.1.0 64-bit

Developer and Designer Tools
├─ Qt Creator
├─ MinGW 13.1.0 64-bit
├─ CMake
└─ Ninja
```

建议安装到：

```text
C:\Qt
```

安装完成后的典型目录为：

```text
C:\Qt\6.8.3\mingw_64
C:\Qt\Tools\mingw1310_64
C:\Qt\Tools\CMake_64
C:\Qt\Tools\Ninja
C:\Qt\Tools\QtCreator
```

如果版本目录略有不同，运行环境脚本时可显式指定：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/Setup-Environment.ps1 `
  -QtRoot D:\Qt `
  -QtVersion 6.8.3 `
  -Build `
  -RunTests
```

### 4.3 在 Qt Creator 中打开

1. 启动 Qt Creator。
2. 选择“打开文件或项目”。
3. 选择仓库根目录的 `CMakeLists.txt`。
4. Kit 选择 Qt 6.8.3 MinGW 64-bit。
5. 构建类型选择 Debug 或 Release。
6. 点击“配置项目”。
7. 构建并运行 `data_backup` 目标。

如果没有可选 Kit，进入“编辑 → Preferences → Kits”：

- Qt version 指向 `C:\Qt\6.8.3\mingw_64\bin\qmake.exe`
- C++ compiler 指向 `C:\Qt\Tools\mingw1310_64\bin\g++.exe`
- CMake 指向 `C:\Qt\Tools\CMake_64\bin\cmake.exe`

### 4.4 使用命令行构建

已有 Qt 环境时，推荐先生成本机预设：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/Setup-Environment.ps1
```

然后执行：

```powershell
C:\Qt\Tools\CMake_64\bin\cmake.exe --preset local-qt-debug
C:\Qt\Tools\CMake_64\bin\cmake.exe --build --preset local-qt-debug
C:\Qt\Tools\CMake_64\bin\ctest.exe --test-dir build/local-qt-debug --output-on-failure
```

也可以不用 Preset：

```powershell
cmake -S . -B build/manual -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/mingw_64 `
  -DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw1310_64/bin/g++.exe

cmake --build build/manual
ctest --test-dir build/manual --output-on-failure
```

## 5. 使用说明

### 5.1 创建备份

打开“备份”页并填写：

- 源目录：需要备份的文件夹。
- 备份仓库：保存所有快照的文件夹。
- 备份名称：可选；留空时使用源目录名称。
- 备份模式：完整、增量或差异。

点击“开始备份”后，程序会扫描源目录、应用筛选规则、复制文件、执行可选校验并写入元数据。成功后，新快照会自动填入“还原”页。

备份仓库不能放在源目录内部，否则会造成递归备份，程序会拒绝该请求。

### 5.2 三种备份模式

完整备份保存当前所有符合条件的文件，可独立还原，也是后续备份的基线。

增量备份与最近一次成功快照比较，只保存新增或变化文件。还原时需要完整备份和中间的增量链。

差异备份与最近一次完整备份比较，只保存相对完整基线发生变化的文件。还原时主要依赖完整备份和当前差异快照。

如果没有找到适用基线，增量或差异请求会自动创建完整备份，并显示提示。

### 5.3 筛选规则

- 仅包含扩展名：逗号分隔，例如 `.docx,.pdf,.txt`。
- 排除扩展名：逗号分隔，例如 `.tmp,.log,.bak`。
- 排除路径：相对源目录填写，多个路径用分号分隔，例如 `cache;temp;logs\old`。
- 最小/最大文件大小：单位为 MB，最大值为 0 表示不限。
- 隐藏文件：默认排除，勾选后包含 Windows 隐藏文件和隐藏目录。
- 复制后校验：逐字节验证内容，可靠性更高但耗时更长。

扩展名在 Windows 下不区分大小写。目录不受文件扩展名和文件大小限制，因此空目录也能进入快照。

### 5.4 还原文件

在“还原”页选择一个具体快照目录，该目录中应包含：

```text
backup.meta
manifest.tsv
data\
```

不要只选择最外层备份仓库。选择还原目标目录和冲突策略后，点击“开始还原”。

冲突策略：

- 覆盖：用备份内容替换同名文件。
- 跳过：保留目标目录中的现有文件。
- 自动重命名：生成 `文件名 (restored 1).扩展名`。

选择增量或差异快照时，程序会自动沿父备份链寻找文件，不需要手动逐次还原。不要删除或移动备份链中的基线和中间快照。

### 5.5 定时任务

“定时任务”页使用“备份”页当前配置创建任务，支持：

- 单次执行
- 每日执行
- 每周执行
- 自定义分钟间隔
- 启用、停用和删除

定时任务保存在本机设置中。该功能属于应用内调度，程序关闭后不会在后台运行，也不会注册 Windows 服务。如果到期时已有备份或还原正在执行，本次任务会被跳过并在状态栏提示。

### 5.6 进度和取消

任务状态区域显示当前阶段、当前路径、文件数量、字节数量和进度条。点击“取消”后，核心模块会在扫描或复制安全边界停止；未完成的 `.partial` 目录会被清理，不会成为后续备份基线。

## 6. 示例测试

仓库提供最小测试数据：

```text
resources\demo\source\示例文档.txt
```

测试时选择：

```text
源目录：resources\demo\source
备份仓库：resources\demo\backup-output
还原目录：resources\demo\restore-output
```

推荐流程：

1. 执行一次完整备份。
2. 把示例文档中的“版本：1”改为“版本：2”。
3. 执行增量备份。
4. 再次修改文件并执行差异备份。
5. 选择最新快照并还原到 `restore-output`。
6. 分别测试覆盖、跳过和自动重命名策略。

备份和还原输出已加入 `.gitignore`，不会混入源码。

## 7. 备份目录格式

每次成功备份会生成一个带时间戳的目录：

```text
course-demo-20261008-160000\
├─ backup.meta      # 模式、时间、源路径和父备份信息
├─ manifest.tsv     # 当前快照的完整文件及目录清单
└─ data\            # 本次实际保存的文件内容
```

写入过程先使用 `.partial` 临时目录，元数据和清单写入成功后再完成最终重命名。增量和差异快照的清单描述完整状态，但 `data` 中只保存本次需要的数据。

## 8. 自动化测试

当前 CTest 测试包括：

- 公共接口编译检查
- 筛选规则测试
- 完整、增量和差异备份往返测试
- 空目录、取消、非法嵌套路径测试
- 覆盖、跳过和重命名还原测试
- 定时任务持久化、更新、删除和到期触发测试

运行：

```powershell
C:\Qt\Tools\CMake_64\bin\ctest.exe --test-dir build/local-qt-debug --output-on-failure
```

## 9. 项目结构

```text
.
├─ CMakeLists.txt
├─ CMakePresets.json
├─ include/backup/
│  ├─ core/                 # 公共数据结构、备份与还原接口
│  ├─ filter/               # 筛选接口及实现声明
│  ├─ scheduler/            # 调度接口、数据结构及 Qt 实现声明
│  └─ gui/                  # 主窗口声明
├─ src/
│  ├─ app/                  # 程序入口
│  ├─ core/                 # 备份、还原、清单和元数据
│  ├─ filter/               # 筛选规则实现
│  ├─ scheduler/            # 定时任务实现
│  └─ gui/                  # Qt Widgets 界面
├─ resources/               # 样式、Qt 资源和演示数据
├─ tests/                   # 接口、核心、筛选和调度测试
├─ scripts/                 # 环境、构建和模块归档脚本
├─ docs/                    # 需求、架构和模块边界
└─ handoff/                 # 按分工整理的模块归档说明
```

## 10. 常见问题

### CMake 提示找不到 Qt6

确认 `CMAKE_PREFIX_PATH` 指向 Qt 套件目录，而不是 Qt 根目录：

```text
正确：C:\Qt\6.8.3\mingw_64
错误：C:\Qt
```

也可以直接运行：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/Setup-Environment.ps1
```

### 程序提示缺少 Qt6Widgets.dll

请从构建目录运行程序。CMake 链接完成后会自动调用 `windeployqt`。复制程序时要复制整个目录，包括 DLL 和 `platforms` 子目录。

### 出现 “no Qt platform plugin could be initialized”

检查程序旁边是否存在：

```text
platforms\qwindows.dll
```

重新构建 `data_backup` 通常可以再次触发自动部署。

### 编译器与 Qt 不兼容

MinGW 版本需要与 Qt 套件匹配。不要使用 MSVC 编译器链接 MinGW 版本的 Qt，也不要混用其他 MinGW 版本。运行环境脚本可自动选择同一 Qt 根目录下的工具链。

### PowerShell 禁止执行脚本

只对当前命令临时放开策略：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/Build-And-Run.ps1
```

不需要永久修改系统执行策略。

### 增量备份无法还原

检查完整基线和中间增量目录是否仍在原位置。`backup.meta` 中记录了父备份路径，移动或删除链中目录会导致数据无法定位。

## 11. 项目文档

- [需求说明与演示验收](docs/requirements.md)
- [架构与集成约定](docs/architecture.md)
- [文件归属与集成边界](docs/ownership.md)

## 12. Git 约定

- 不提交 `build/`、Qt Creator 用户配置、可执行文件、DLL、日志和真实备份数据。
- `CMakeUserPresets.json` 保存本机绝对路径，已被 Git 忽略。
- 公共接口由 A 统一维护，B/C 修改接口前先完成组内确认。
- 功能提交保持单一职责，合并前运行构建和全部测试。
