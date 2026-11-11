# File Backup System

A Windows desktop file-backup course project developed by a three-member team with C++17, Qt 6 Widgets, MinGW-w64, and CMake. It supports full, incremental, and differential backups; filtering; chained restore; conflict policies; progress/cancellation; and persistent in-app schedules.

## Build

Requirements: Qt 6.2+, a MinGW-w64 Qt kit, and CMake 3.21+.

Open the root `CMakeLists.txt` in Qt Creator and select a Qt 6 MinGW 64-bit kit, or build from a Qt MinGW terminal:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=C:/Qt/6.x.x/mingw_64
cmake --build build
```

The executable target is `data_backup`. The Chinese [README](README.md) contains the module ownership, directory layout, interface conventions, and Git workflow.

Run tests with:

```powershell
ctest --test-dir build --output-on-failure
```

The repository also contains shared Debug/Release CMake presets, a [requirements specification](docs/requirements.md), and an [architecture guide](docs/architecture.md). The current development machine has successfully built the project with Qt 6.8.3, MinGW 13.1.0, CMake 3.30.5, Ninja 1.12.1, and Qt Creator 20.0.2.

On Windows, every link step runs `windeployqt` automatically so the executable can find the required Qt/MinGW DLLs and the Windows platform plugin. Keep the generated DLLs and plugin directories beside `data_backup.exe` when copying the application.

Each snapshot contains `backup.meta`, `manifest.tsv`, and `data/`. Incremental and differential snapshots depend on their recorded parent/base directories, so keep a backup chain together. Scheduled tasks run while the desktop application is open; this version does not install a Windows service.
