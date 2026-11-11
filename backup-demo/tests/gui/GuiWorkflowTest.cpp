#include "backup/gui/MainWindow.h"
#include "../../src/core/BackupStorage.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
template<class T> T* field(QWidget& window, const char* name) {
    auto* item = window.findChild<T*>(QString::fromLatin1(name));
    require(item != nullptr, name); return item;
}
template<class Predicate> void waitFor(Predicate ready, int timeout, const char* message) {
    QElapsedTimer elapsed; elapsed.start();
    while (!ready() && elapsed.elapsed() < timeout) {
        QApplication::processEvents(QEventLoop::AllEvents, 20); QThread::msleep(10);
    }
    require(ready(), message);
}
std::string read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("BackupWorkflowTest"));
    QCoreApplication::setApplicationName(QStringLiteral("IsolatedGuiTest"));
    QTemporaryDir temporary;
    require(temporary.isValid(), "Cannot create test directory");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    try {
        QFile style(QStringLiteral(":/styles/app.qss"));
        if (style.open(QIODevice::ReadOnly)) app.setStyleSheet(QString::fromUtf8(style.readAll()));
        const fs::path root(temporary.path().toStdWString());
        const auto source = root / fs::u8path(u8"中文演示源");
        const auto repository = root / "repository";
        const auto restored = root / "restored";
        fs::create_directories(source / "cache"); fs::create_directories(source / "empty");
        std::ofstream(source / fs::u8path(u8"示例.txt")) << "GUI roundtrip content";
        std::ofstream(source / "notes.txt") << "notes";
        std::ofstream(source / "ignored.log") << "excluded by type";
        std::ofstream(source / "cache/ignore.txt") << "excluded by path";
        std::ofstream(source / "old.txt") << "excluded by modification time";
        fs::last_write_time(source / "old.txt", fs::file_time_type::clock::now() - std::chrono::hours(24 * 30));
        std::ofstream(source / "draft.txt") << "excluded by name";
        backup::gui::MainWindow window;
        window.resize(1050, 900); window.show();
        auto setPath = [&](const char* name, const fs::path& value) {
            field<QLineEdit>(window, name)->setText(QString::fromStdWString(value.wstring()));
        };
        setPath("sourceEdit", source); setPath("backupDestinationEdit", repository);
        setPath("restoreDestinationEdit", restored);
        field<QLineEdit>(window, "backupNameEdit")->setText(QStringLiteral("gui-demo"));
        field<QLineEdit>(window, "includedExtensionsEdit")->setText(QStringLiteral(".txt"));
        field<QLineEdit>(window, "excludedPathsEdit")->setText(QStringLiteral("cache"));
        field<QLineEdit>(window, "includedNamesEdit")->setText(QStringLiteral("示例*.txt;notes*.txt;old*.txt;draft*.txt"));
        field<QLineEdit>(window, "excludedNamesEdit")->setText(QStringLiteral("draft*"));
        field<QCheckBox>(window, "modifiedAfterCheck")->setChecked(true);
        field<QCheckBox>(window, "modifiedBeforeCheck")->setChecked(true);
        field<QDateTimeEdit>(window, "modifiedAfterEdit")->setDateTime(QDateTime::currentDateTime().addDays(-1));
        field<QDateTimeEdit>(window, "modifiedBeforeEdit")->setDateTime(QDateTime::currentDateTime().addDays(1));
        field<QCheckBox>(window, "verifyBackupCheck")->setChecked(true);
        field<QCheckBox>(window, "verifyRestoreCheck")->setChecked(true);
        int modalCount = 0;
        QTimer closeDialogs;
        QObject::connect(&closeDialogs, &QTimer::timeout, [&] {
            for (auto* widget : QApplication::topLevelWidgets()) {
                if (auto* box = qobject_cast<QMessageBox*>(widget); box && box->isVisible()) {
                    ++modalCount; box->accept();
                }
            }
        });
        closeDialogs.start(30);
        const auto evidence = qEnvironmentVariable("BACKUP_TEST_ARTIFACT_DIR");
        auto capture = [&](const QString& name) {
            if (!evidence.isEmpty()) {
                QDir().mkpath(evidence);
                QApplication::processEvents();
                require(window.grab().save(evidence + '/' + name + QStringLiteral(".png")), "Screenshot save failed");
            }
        };
        capture(QStringLiteral("01-backup"));
        auto* backupButton = field<QPushButton>(window, "backupButton");
        auto* backupPage = qobject_cast<QScrollArea*>(field<QTabWidget>(window, "mainTabs")->widget(0));
        require(backupPage != nullptr, "Backup scroll page missing");
        backupPage->ensureWidgetVisible(field<QDateTimeEdit>(window, "modifiedBeforeEdit"));
        capture(QStringLiteral("05-name-time"));
        // Reject reversed time bounds before launching a worker or creating output.
        field<QDateTimeEdit>(window, "modifiedAfterEdit")->setDateTime(QDateTime::currentDateTime().addDays(2));
        backupButton->click();
        require(backupButton->isEnabled() && !fs::exists(repository), "GUI accepted reversed time bounds");
        field<QDateTimeEdit>(window, "modifiedAfterEdit")->setDateTime(QDateTime::currentDateTime().addDays(-1));
        backupButton->click();
        waitFor([&] { return backupButton->isEnabled(); }, 10000, "GUI backup did not finish");
        const auto initial = backup::detail::findBackups(repository, source);
        const auto backupLog = field<QListWidget>(window, "historyList");
        bool foundStatistics = false;
        for (int i = 0; i < backupLog->count(); ++i) {
            if (backupLog->item(i)->text().contains(QStringLiteral("筛选排除 4 个文件"))) foundStatistics = true;
        }
        require(foundStatistics, "GUI backup result omitted the four filtered-out files");
        require(initial.size() == 1, "GUI backup did not create one valid snapshot");
        require(fs::exists(initial.front() / "data" / fs::u8path(u8"示例.txt"))
            && !fs::exists(initial.front() / "data/ignored.log")
            && !fs::exists(initial.front() / "data/cache/ignore.txt")
            && !fs::exists(initial.front() / "data/old.txt")
            && !fs::exists(initial.front() / "data/draft.txt"), "GUI combined name/time/type/path rules failed");
        auto* tabs = field<QTabWidget>(window, "mainTabs");
        tabs->setCurrentIndex(1);
        field<QPushButton>(window, "restoreButton")->click();
        waitFor([&] { return field<QPushButton>(window, "restoreButton")->isEnabled(); }, 10000, "GUI restore did not finish");
        require(read(restored / fs::u8path(u8"示例.txt")) == "GUI roundtrip content"
            && read(restored / "notes.txt") == "notes"
            && fs::is_directory(restored / "empty") && !fs::exists(restored / "cache"), "GUI restored content mismatch");
        capture(QStringLiteral("02-restore"));
        const auto beforeAutomatic = modalCount;
        tabs->setCurrentIndex(2);
        field<QLineEdit>(window, "scheduleNameEdit")->setText(QStringLiteral("一分钟周期与淘汰演示"));
        field<QComboBox>(window, "scheduleFrequencyCombo")->setCurrentIndex(3);
        field<QSpinBox>(window, "scheduleIntervalSpin")->setValue(1);
        field<QSpinBox>(window, "retentionSpin")->setValue(2);
        field<QDateTimeEdit>(window, "scheduleNextRunEdit")->setDateTime(QDateTime::currentDateTime().addSecs(2));
        field<QPushButton>(window, "addScheduleButton")->click();
        capture(QStringLiteral("03-schedule"));
        auto* history = field<QListWidget>(window, "historyList");
        auto automaticSuccesses = [&] {
            int count = 0;
            for (int i = 0; i < history->count(); ++i) {
                const auto text = history->item(i)->text();
                if (text.contains(QStringLiteral("备份成功")) && text.contains(QStringLiteral("Retention:"))) ++count;
            }
            return count;
        };
        waitFor([&] { return automaticSuccesses() >= 2 && backupButton->isEnabled(); },
            85000, "Periodic GUI backup did not run twice within 85 seconds");
        const auto remaining = backup::detail::findBackups(repository, source);
        require(remaining.size() == 2 && !fs::exists(initial.front()), "GUI periodic retention did not remove the oldest snapshot");
        require(modalCount == beforeAutomatic, "Automatic backup unexpectedly displayed a modal dialog");
        tabs->setCurrentIndex(3); capture(QStringLiteral("04-history"));
        tabs->setCurrentIndex(4);
        const auto packageFile = root / fs::u8path(u8"课堂备份包.dbpack");
        setPath("packFileEdit", packageFile);
        setPath("unpackDestinationEdit", root / "imported");
        field<QPushButton>(window, "packButton")->click();
        waitFor([&] { return field<QPushButton>(window, "packButton")->isEnabled(); }, 10000, "GUI package export did not finish");
        require(fs::is_regular_file(packageFile), "GUI did not create the single package file");
        const fs::path forwarded(field<QLineEdit>(window, "unpackFileEdit")->text().toStdWString());
        require(!forwarded.empty() && fs::equivalent(forwarded, packageFile), "GUI did not forward the exported package to import");
        capture(QStringLiteral("06-archive"));
        field<QPushButton>(window, "unpackButton")->click();
        waitFor([&] { return field<QPushButton>(window, "unpackButton")->isEnabled(); }, 10000, "GUI package import did not finish");
        const fs::path imported(field<QLineEdit>(window, "restoreBackupEdit")->text().toStdWString());
        require(backup::detail::readMetadata(imported).parentBackup.empty(), "Imported GUI snapshot is not independent");
        tabs->setCurrentIndex(1);
        const auto packageRestore = root / "restored-from-package";
        setPath("restoreDestinationEdit", packageRestore);
        field<QPushButton>(window, "restoreButton")->click();
        waitFor([&] { return field<QPushButton>(window, "restoreButton")->isEnabled(); }, 10000, "GUI imported snapshot restore did not finish");
        require(read(packageRestore / fs::u8path(u8"示例.txt")) == "GUI roundtrip content"
            && read(packageRestore / "notes.txt") == "notes"
            && !fs::exists(packageRestore / "old.txt"), "GUI package restore content mismatch");
        capture(QStringLiteral("07-package-restore"));
        tabs->setCurrentIndex(3); capture(QStringLiteral("08-archive-history"));
        if (!evidence.isEmpty()) {
            QFile log(evidence + QStringLiteral("/gui-workflow.log"));
            require(log.open(QIODevice::WriteOnly), "Cannot save GUI evidence");
            for (int i = 0; i < history->count(); ++i) log.write((history->item(i)->text() + '\n').toUtf8());
        }
        std::cout << "GUI filters, restore, periodic retention, single-file package export/import and verified package restore passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
