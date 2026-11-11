#pragma once

#include "backup/core/BackupTypes.h"

#include <QMainWindow>

#include <atomic>
#include <memory>

class QCheckBox;
class QComboBox;
class QDateTimeEdit;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QThread;

namespace backup {
class BackupManager;
class RestoreManager;
class QtScheduler;
class ArchiveManager;
}

namespace backup::gui {

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private:
    QWidget* createBackupPage();
    QWidget* createRestorePage();
    QWidget* createSchedulePage();
    QWidget* createArchivePage();
    QWidget* createProgressPanel();

    BackupRequest backupRequestFromForm() const;
    RestoreRequest restoreRequestFromForm() const;
    void startBackup(const BackupRequest& request, const QString& context = {});
    void startRestore(const RestoreRequest& request);
    void startArchive(bool unpack);
    void cancelOperation();
    void updateProgress(const ProgressInfo& progress);
    void finishOperation(const OperationResult& result, const QString& operationName, bool automatic = false);
    void appendLog(const QString& text);
    void setBusy(bool busy);

    void addSchedule();
    void removeSelectedSchedule();
    void toggleSelectedSchedule();
    void refreshScheduleList();

    std::unique_ptr<BackupManager> backupManager_;
    std::unique_ptr<RestoreManager> restoreManager_;
    std::unique_ptr<QtScheduler> scheduler_;
    std::unique_ptr<ArchiveManager> archiveManager_;

    QTabWidget* tabs_{nullptr};
    QLineEdit* sourceEdit_{nullptr};
    QLineEdit* backupDestinationEdit_{nullptr};
    QLineEdit* backupNameEdit_{nullptr};
    QComboBox* backupModeCombo_{nullptr};
    QLineEdit* includedExtensionsEdit_{nullptr};
    QLineEdit* excludedExtensionsEdit_{nullptr};
    QLineEdit* excludedPathsEdit_{nullptr};
    QLineEdit* includedNamesEdit_{nullptr};
    QLineEdit* excludedNamesEdit_{nullptr};
    QCheckBox* modifiedAfterCheck_{nullptr};
    QCheckBox* modifiedBeforeCheck_{nullptr};
    QDateTimeEdit* modifiedAfterEdit_{nullptr};
    QDateTimeEdit* modifiedBeforeEdit_{nullptr};
    QDoubleSpinBox* minimumSizeSpin_{nullptr};
    QDoubleSpinBox* maximumSizeSpin_{nullptr};
    QCheckBox* includeHiddenCheck_{nullptr};
    QCheckBox* verifyBackupCheck_{nullptr};
    QPushButton* backupButton_{nullptr};

    QLineEdit* restoreBackupEdit_{nullptr};
    QLineEdit* restoreDestinationEdit_{nullptr};
    QComboBox* conflictPolicyCombo_{nullptr};
    QCheckBox* verifyRestoreCheck_{nullptr};
    QPushButton* restoreButton_{nullptr};
    QLineEdit* packSourceEdit_{nullptr};
    QLineEdit* packFileEdit_{nullptr};
    QLineEdit* unpackFileEdit_{nullptr};
    QLineEdit* unpackDestinationEdit_{nullptr};
    QPushButton* packButton_{nullptr};
    QPushButton* unpackButton_{nullptr};

    QLineEdit* scheduleNameEdit_{nullptr};
    QComboBox* scheduleFrequencyCombo_{nullptr};
    QSpinBox* scheduleIntervalSpin_{nullptr};
    QSpinBox* retentionSpin_{nullptr};
    QListWidget* historyList_{nullptr};
    QDateTimeEdit* scheduleNextRunEdit_{nullptr};
    QListWidget* scheduleList_{nullptr};
    QPushButton* removeScheduleButton_{nullptr};
    QPushButton* toggleScheduleButton_{nullptr};

    QProgressBar* progressBar_{nullptr};
    QLabel* operationLabel_{nullptr};
    QLabel* currentPathLabel_{nullptr};
    QLabel* progressDetailsLabel_{nullptr};
    QPushButton* cancelButton_{nullptr};

    QThread* worker_{nullptr};
    std::atomic_bool cancellationRequested_{false};
};

} // namespace backup::gui
