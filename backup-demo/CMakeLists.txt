cmake_minimum_required(VERSION 3.21)

project(DataBackupSoftware
    VERSION 0.1.0
    DESCRIPTION "A Windows desktop file backup application"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Widgets)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Public contracts shared by every module.
add_library(backup_contracts INTERFACE)
add_library(BackupSoftware::Contracts ALIAS backup_contracts)

target_include_directories(backup_contracts
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

add_library(backup_filter STATIC
    src/filter/DefaultFilterManager.cpp
)
add_library(BackupSoftware::Filter ALIAS backup_filter)
target_link_libraries(backup_filter PUBLIC BackupSoftware::Contracts)

add_library(backup_engine STATIC
    src/core/ArchiveManager.cpp
    src/core/BackupStorage.cpp
    src/core/FilesystemBackupManager.cpp
    src/core/FilesystemRestoreManager.cpp
)
add_library(BackupSoftware::Engine ALIAS backup_engine)
target_link_libraries(backup_engine PUBLIC BackupSoftware::Contracts)

add_library(backup_scheduler STATIC
    src/scheduler/QtScheduler.cpp
    include/backup/scheduler/QtScheduler.h
)
add_library(BackupSoftware::Scheduler ALIAS backup_scheduler)
target_link_libraries(backup_scheduler
    PUBLIC
        BackupSoftware::Contracts
        Qt6::Core
)

add_executable(data_backup WIN32
    src/app/main.cpp
    src/gui/MainWindow.cpp
    include/backup/gui/MainWindow.h
    resources/resources.qrc
)

target_link_libraries(data_backup
    PRIVATE
        BackupSoftware::Engine
        BackupSoftware::Filter
        BackupSoftware::Scheduler
        Qt6::Widgets
)

target_include_directories(data_backup PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Make binaries in the build directory directly runnable on Windows. This copies
# the required Qt DLLs, the Windows platform plugin and the MinGW runtime next to
# data_backup.exe after every link step.
if(WIN32)
    add_custom_command(TARGET data_backup POST_BUILD
        COMMAND $<TARGET_FILE:Qt6::windeployqt>
            --dir $<TARGET_FILE_DIR:data_backup>
            --no-translations
            --compiler-runtime
            $<TARGET_FILE:data_backup>
        COMMENT "Deploying the Qt runtime beside data_backup.exe"
        VERBATIM
    )
endif()

if(MINGW)
    foreach(target backup_filter backup_engine backup_scheduler data_backup)
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endforeach()
elseif(MSVC)
    foreach(target backup_filter backup_engine backup_scheduler data_backup)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    endforeach()
endif()

include(GNUInstallDirs)
install(TARGETS data_backup
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

qt_generate_deploy_app_script(
    TARGET data_backup
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})

include(CTest)
if(BUILD_TESTING)
    add_executable(backup_contracts_test
        tests/contracts/ContractCompileTest.cpp
    )
    target_link_libraries(backup_contracts_test PRIVATE BackupSoftware::Contracts)
    add_test(NAME backup.contracts.compile COMMAND backup_contracts_test)

    add_executable(backup_filter_test
        tests/filter/FilterManagerTest.cpp
    )
    target_link_libraries(backup_filter_test PRIVATE BackupSoftware::Filter)
    add_test(NAME backup.filter.rules COMMAND backup_filter_test)

    add_executable(backup_integration_test
        tests/core/BackupRestoreTest.cpp
    )
    target_link_libraries(backup_integration_test
        PRIVATE
            BackupSoftware::Engine
            BackupSoftware::Filter
    )
    add_test(NAME backup.integration.roundtrip COMMAND backup_integration_test)

    add_executable(backup_scheduler_test
        tests/scheduler/SchedulerTest.cpp
    )
    target_link_libraries(backup_scheduler_test PRIVATE BackupSoftware::Scheduler)
    add_test(NAME backup.scheduler.persistence COMMAND backup_scheduler_test)
    add_executable(backup_retention_test tests/core/RetentionTest.cpp)
    target_link_libraries(backup_retention_test PRIVATE BackupSoftware::Engine BackupSoftware::Filter)
    add_test(NAME backup.retention.chains COMMAND backup_retention_test)
    add_executable(backup_name_time_test tests/filter/NameTimeFilterTest.cpp)
    target_link_libraries(backup_name_time_test PRIVATE BackupSoftware::Engine BackupSoftware::Filter)
    add_test(NAME backup.custom.names_time COMMAND backup_name_time_test)
    add_executable(backup_archive_test tests/core/ArchiveTest.cpp)
    target_link_libraries(backup_archive_test PRIVATE BackupSoftware::Engine BackupSoftware::Filter)
    add_test(NAME backup.archive.independent_roundtrip COMMAND backup_archive_test)
    add_executable(backup_statistics_test tests/core/StatisticsTest.cpp)
    target_link_libraries(backup_statistics_test PRIVATE BackupSoftware::Engine BackupSoftware::Filter)
    add_test(NAME backup.statistics.filtered_and_unchanged COMMAND backup_statistics_test)
    add_executable(backup_gui_workflow_test tests/gui/GuiWorkflowTest.cpp
        src/gui/MainWindow.cpp resources/resources.qrc)
    target_link_libraries(backup_gui_workflow_test PRIVATE BackupSoftware::Engine
        BackupSoftware::Filter BackupSoftware::Scheduler Qt6::Widgets)
    add_test(NAME backup.gui.periodic_workflow COMMAND backup_gui_workflow_test)
    set_tests_properties(backup.gui.periodic_workflow PROPERTIES TIMEOUT 110)
endif()
