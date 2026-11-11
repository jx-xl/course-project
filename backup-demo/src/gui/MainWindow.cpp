#include "backup/gui/MainWindow.h"

#include "backup/core/FilesystemBackupManager.h"
#include "backup/core/FilesystemRestoreManager.h"
#include "backup/core/ArchiveManager.h"
#include "backup/filter/DefaultFilterManager.h"
#include "backup/scheduler/QtScheduler.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDateTimeEdit>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFile>
#include <QCoreApplication>
#include <QDir>
#include <QFormLayout>
#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QMetaObject>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStatusBar>
#include <QTabWidget>
#include <QThread>
#include <QUuid>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidget>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace backup::gui {
namespace {

template<typename Widget>
class ScrollSafeControl final : public Widget {
public:
    using Widget::Widget;

protected:
    void wheelEvent(QWheelEvent* event) override
    {
        event->ignore();
    }
};

std::filesystem::path pathFromText(const QString& value)
{
    return std::filesystem::path(value.trimmed().toStdWString());
}

QString pathToText(const std::filesystem::path& value)
{
    return QString::fromStdWString(value.wstring());
}

std::vector<std::string> splitStrings(const QString& value, QChar separator)
{
    std::vector<std::string> result;
    const auto parts = value.split(separator, Qt::SkipEmptyParts);
    result.reserve(parts.size());
    for (const auto& part : parts) {
        result.push_back(part.trimmed().toUtf8().toStdString());
    }
    return result;
}

std::vector<std::filesystem::path> splitPaths(const QString& value)
{
    std::vector<std::filesystem::path> result;
    const auto parts = value.split(';', Qt::SkipEmptyParts);
    result.reserve(parts.size());
    for (const auto& part : parts) {
        result.push_back(pathFromText(part));
    }
    return result;
}

QString phaseText(OperationPhase phase)
{
    switch (phase) {
    case OperationPhase::Preparing:
        return QStringLiteral("准备中");
    case OperationPhase::Scanning:
        return QStringLiteral("扫描目录");
    case OperationPhase::Copying:
        return QStringLiteral("复制文件");
    case OperationPhase::Verifying:
        return QStringLiteral("校验文件");
    case OperationPhase::Finalizing:
        return QStringLiteral("整理元数据");
    case OperationPhase::Packing:
        return QStringLiteral("写入单文件备份包");
    case OperationPhase::Unpacking:
        return QStringLiteral("解包与校验");
    }
    return QStringLiteral("处理中");
}

QString frequencyText(ScheduleFrequency frequency)
{
    switch (frequency) {
    case ScheduleFrequency::Once:
        return QStringLiteral("单次");
    case ScheduleFrequency::Daily:
        return QStringLiteral("每日");
    case ScheduleFrequency::Weekly:
        return QStringLiteral("每周");
    case ScheduleFrequency::CustomInterval:
        return QStringLiteral("自定义间隔");
    }
    return {};
}

QDateTime dateTimeFromTimePoint(std::chrono::system_clock::time_point value)
{
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        value.time_since_epoch()).count();
    return QDateTime::fromMSecsSinceEpoch(milliseconds);
}

std::chrono::system_clock::time_point timePointFromDateTime(const QDateTime& value)
{
    return std::chrono::system_clock::time_point(
        std::chrono::milliseconds(value.toMSecsSinceEpoch()));
}

QHBoxLayout* pathRow(QLineEdit* edit, QWidget* parent, const QString& dialogTitle)
{
    auto* layout = new QHBoxLayout;
    layout->setContentsMargins(0, 0, 0, 0);
    auto* browseButton = new QPushButton(QStringLiteral("浏览…"), parent);
    browseButton->setProperty("secondary", true);
    layout->addWidget(edit, 1);
    layout->addWidget(browseButton);
    QObject::connect(browseButton, &QPushButton::clicked, parent, [edit, parent, dialogTitle] {
        const QString selected = QFileDialog::getExistingDirectory(
            parent,
            dialogTitle,
            edit->text());
        if (!selected.isEmpty()) {
            edit->setText(selected);
        }
    });
    return layout;
}

QScrollArea* scrollablePage(QWidget* content, QWidget* parent)
{
    auto* scrollArea = new QScrollArea(parent);
    scrollArea->setObjectName(QStringLiteral("pageScrollArea"));
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setWidget(content);
    return scrollArea;
}

QHBoxLayout* packageFileRow(QLineEdit* edit, QWidget* parent, bool save)
{
    auto* layout = new QHBoxLayout;
    layout->setContentsMargins(0, 0, 0, 0);
    auto* button = new QPushButton(QStringLiteral("浏览…"), parent);
    button->setProperty("secondary", true);
    layout->addWidget(edit, 1); layout->addWidget(button);
    QObject::connect(button, &QPushButton::clicked, parent, [edit, parent, save] {
        const auto filter = QStringLiteral("DBPACK 备份包 (*.dbpack)");
        auto selected = save ? QFileDialog::getSaveFileName(parent, QStringLiteral("保存新的备份包"), edit->text(), filter)
                             : QFileDialog::getOpenFileName(parent, QStringLiteral("选择备份包"), edit->text(), filter);
        if (save && !selected.isEmpty() && !selected.endsWith(QStringLiteral(".dbpack"), Qt::CaseInsensitive)) selected += QStringLiteral(".dbpack");
        if (!selected.isEmpty()) edit->setText(QDir::toNativeSeparators(selected));
    });
    return layout;
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    auto filterManager = std::make_shared<DefaultFilterManager>();
    backupManager_ = std::make_unique<FilesystemBackupManager>(filterManager);
    restoreManager_ = std::make_unique<FilesystemRestoreManager>();
    scheduler_ = std::make_unique<QtScheduler>(this);
    archiveManager_ = std::make_unique<ArchiveManager>();

    setWindowTitle(QStringLiteral("文件备份系统"));
    resize(980, 720);
    setMinimumSize(800, 620);

    auto* centralWidget = new QWidget(this);
    centralWidget->setObjectName(QStringLiteral("appRoot"));
    auto* rootLayout = new QVBoxLayout(centralWidget);
    rootLayout->setContentsMargins(24, 22, 24, 18);
    rootLayout->setSpacing(14);

    auto* header = new QFrame(centralWidget);
    header->setObjectName(QStringLiteral("headerBar"));
    auto* headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(2, 2, 2, 14);
    headerLayout->setSpacing(3);
    auto* title = new QLabel(QStringLiteral("文件备份系统"), header);
    title->setObjectName(QStringLiteral("appTitle"));
    QFont titleFont = title->font();
    titleFont.setPointSize(20);
    titleFont.setBold(true);
    title->setFont(titleFont);
    auto* subtitle = new QLabel(
        QStringLiteral("本地文件备份与还原"), header);
    subtitle->setObjectName(QStringLiteral("appSubtitle"));
    headerLayout->addWidget(title);
    headerLayout->addWidget(subtitle);
    rootLayout->addWidget(header);

    tabs_ = new QTabWidget(centralWidget);
    tabs_->setObjectName(QStringLiteral("mainTabs"));
    tabs_->setDocumentMode(true);
    tabs_->addTab(
        scrollablePage(createBackupPage(), tabs_),
        QStringLiteral("备份"));
    tabs_->addTab(
        scrollablePage(createRestorePage(), tabs_),
        QStringLiteral("还原"));
    tabs_->addTab(
        scrollablePage(createSchedulePage(), tabs_),
        QStringLiteral("定时任务"));
    historyList_ = new QListWidget(tabs_);
    historyList_->setObjectName(QStringLiteral("historyList"));
    historyList_->setWordWrap(true);
    tabs_->addTab(historyList_, QStringLiteral("执行日志"));
    tabs_->addTab(scrollablePage(createArchivePage(), tabs_), QStringLiteral("打包/解包"));
    rootLayout->addWidget(tabs_, 1);
    rootLayout->addWidget(createProgressPanel());

    setCentralWidget(centralWidget);
    statusBar()->showMessage(QStringLiteral("就绪"));

    scheduler_->setTriggerCallback([this](const ScheduledBackup& schedule) {
        QMetaObject::invokeMethod(this, [this, schedule] {
            if (worker_) {
                appendLog(QStringLiteral("定时任务 %1：已有任务运行，本次跳过")
                    .arg(QString::fromUtf8(schedule.name.c_str())));
                statusBar()->showMessage(
                    QStringLiteral("定时任务“%1”因已有任务运行而跳过")
                        .arg(QString::fromUtf8(schedule.name.c_str())),
                    8000);
                return;
            }
            startBackup(
                schedule.request,
                QStringLiteral("定时备份：%1").arg(QString::fromUtf8(schedule.name.c_str())));
            refreshScheduleList();
        });
    });
    scheduler_->start();
    refreshScheduleList();
    const auto demo = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("demo"));
    unpackDestinationEdit_->setText(QDir::toNativeSeparators(demo + QStringLiteral("/unpacked")));
    packFileEdit_->setText(QDir::toNativeSeparators(demo + QStringLiteral("/packages/backup-")
        + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")) + QStringLiteral(".dbpack")));
    if (QDir(demo + QStringLiteral("/source")).exists()) {
        sourceEdit_->setText(QDir::toNativeSeparators(demo + QStringLiteral("/source")));
        backupDestinationEdit_->setText(QDir::toNativeSeparators(demo + QStringLiteral("/repository")));
        restoreDestinationEdit_->setText(QDir::toNativeSeparators(demo + QStringLiteral("/restored")));
        backupNameEdit_->setText(QStringLiteral("course-demo"));
    }
}

MainWindow::~MainWindow()
{
    scheduler_->stop();
    cancellationRequested_.store(true);
    if (worker_) {
        worker_->wait();
    }
}

QWidget* MainWindow::createBackupPage()
{
    auto* page = new QWidget(this);
    page->setObjectName(QStringLiteral("contentPage"));
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(8, 12, 8, 6);
    layout->setSpacing(10);
    auto* hint = new QLabel(
        QStringLiteral("选择源目录与备份仓库，并按需要设置备份模式和筛选规则。"), page);
    hint->setObjectName(QStringLiteral("pageHint"));
    layout->addWidget(hint);

    auto* basicGroup = new QGroupBox(QStringLiteral("备份配置"), page);
    auto* form = new QFormLayout(basicGroup);
    form->setContentsMargins(16, 20, 16, 14);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setHorizontalSpacing(14);
    form->setVerticalSpacing(11);

    sourceEdit_ = new QLineEdit(basicGroup);
    sourceEdit_->setObjectName(QStringLiteral("sourceEdit"));
    backupDestinationEdit_ = new QLineEdit(basicGroup);
    backupDestinationEdit_->setObjectName(QStringLiteral("backupDestinationEdit"));
    backupNameEdit_ = new QLineEdit(basicGroup);
    backupNameEdit_->setObjectName(QStringLiteral("backupNameEdit"));
    backupNameEdit_->setPlaceholderText(QStringLiteral("留空时使用源目录名称"));
    backupModeCombo_ = new ScrollSafeControl<QComboBox>(basicGroup);
    backupModeCombo_->setObjectName(QStringLiteral("backupModeCombo"));
    backupModeCombo_->addItem(QStringLiteral("完整备份"), static_cast<int>(BackupMode::Full));
    backupModeCombo_->addItem(QStringLiteral("增量备份"), static_cast<int>(BackupMode::Incremental));
    backupModeCombo_->addItem(QStringLiteral("差异备份"), static_cast<int>(BackupMode::Differential));

    form->addRow(
        QStringLiteral("源目录"),
        pathRow(sourceEdit_, basicGroup, QStringLiteral("选择备份源目录")));
    form->addRow(
        QStringLiteral("备份仓库"),
        pathRow(backupDestinationEdit_, basicGroup, QStringLiteral("选择备份保存目录")));
    form->addRow(QStringLiteral("备份名称"), backupNameEdit_);
    form->addRow(QStringLiteral("备份模式"), backupModeCombo_);
    layout->addWidget(basicGroup);

    auto* filterGroup = new QGroupBox(QStringLiteral("筛选规则"), page);
    auto* filterForm = new QFormLayout(filterGroup);
    filterForm->setContentsMargins(16, 20, 16, 14);
    filterForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    filterForm->setHorizontalSpacing(14);
    filterForm->setVerticalSpacing(10);
    includedExtensionsEdit_ = new QLineEdit(filterGroup);
    includedExtensionsEdit_->setObjectName(QStringLiteral("includedExtensionsEdit"));
    includedExtensionsEdit_->setPlaceholderText(QStringLiteral("例如：.docx,.pdf；留空表示全部"));
    excludedExtensionsEdit_ = new QLineEdit(filterGroup);
    excludedExtensionsEdit_->setObjectName(QStringLiteral("excludedExtensionsEdit"));
    excludedExtensionsEdit_->setPlaceholderText(QStringLiteral("例如：.tmp,.log"));
    excludedPathsEdit_ = new QLineEdit(filterGroup);
    excludedPathsEdit_->setObjectName(QStringLiteral("excludedPathsEdit"));
    excludedPathsEdit_->setPlaceholderText(QStringLiteral("相对路径，用分号分隔，例如 cache;temp/logs"));
    minimumSizeSpin_ = new ScrollSafeControl<QDoubleSpinBox>(filterGroup);
    minimumSizeSpin_->setObjectName(QStringLiteral("minimumSizeSpin"));
    maximumSizeSpin_ = new ScrollSafeControl<QDoubleSpinBox>(filterGroup);
    maximumSizeSpin_->setObjectName(QStringLiteral("maximumSizeSpin"));
    for (auto* spin : {minimumSizeSpin_, maximumSizeSpin_}) {
        spin->setRange(0, 1024 * 1024);
        spin->setDecimals(2);
        spin->setSuffix(QStringLiteral(" MB"));
    }
    maximumSizeSpin_->setSpecialValueText(QStringLiteral("不限"));
    includeHiddenCheck_ = new QCheckBox(QStringLiteral("包含隐藏文件和目录"), filterGroup);
    verifyBackupCheck_ = new QCheckBox(QStringLiteral("复制后逐字节校验"), filterGroup);
    verifyBackupCheck_->setObjectName(QStringLiteral("verifyBackupCheck"));

    filterForm->addRow(QStringLiteral("仅包含扩展名"), includedExtensionsEdit_);
    filterForm->addRow(QStringLiteral("排除扩展名"), excludedExtensionsEdit_);
    filterForm->addRow(QStringLiteral("排除路径"), excludedPathsEdit_);
    includedNamesEdit_ = new QLineEdit(filterGroup);
    includedNamesEdit_->setObjectName(QStringLiteral("includedNamesEdit"));
    includedNamesEdit_->setPlaceholderText(QStringLiteral("例如：报告*.txt;*.md；留空不限，只匹配文件名"));
    excludedNamesEdit_ = new QLineEdit(filterGroup);
    excludedNamesEdit_->setObjectName(QStringLiteral("excludedNamesEdit"));
    excludedNamesEdit_->setPlaceholderText(QStringLiteral("例如：*草稿*;~$*；多个规则用分号分隔"));
    filterForm->addRow(QStringLiteral("仅包含文件名"), includedNamesEdit_);
    filterForm->addRow(QStringLiteral("排除文件名"), excludedNamesEdit_);
    auto* nameHint = new QLabel(QStringLiteral("名字：* 匹配任意长度，? 匹配一个字符；多个包含规则满足其一即可，排除优先。不同筛选维度同时生效。"), filterGroup);
    nameHint->setWordWrap(true);
    nameHint->setObjectName(QStringLiteral("pageHint"));
    filterForm->addRow({}, nameHint);
    modifiedAfterCheck_ = new QCheckBox(QStringLiteral("修改时间起始（含）"), filterGroup);
    modifiedBeforeCheck_ = new QCheckBox(QStringLiteral("修改时间截止（含）"), filterGroup);
    modifiedAfterCheck_->setObjectName(QStringLiteral("modifiedAfterCheck"));
    modifiedBeforeCheck_->setObjectName(QStringLiteral("modifiedBeforeCheck"));
    modifiedAfterEdit_ = new ScrollSafeControl<QDateTimeEdit>(QDateTime::currentDateTime().addDays(-7), filterGroup);
    modifiedBeforeEdit_ = new ScrollSafeControl<QDateTimeEdit>(QDateTime::currentDateTime(), filterGroup);
    modifiedAfterEdit_->setObjectName(QStringLiteral("modifiedAfterEdit"));
    modifiedBeforeEdit_->setObjectName(QStringLiteral("modifiedBeforeEdit"));
    for (auto* edit : {modifiedAfterEdit_, modifiedBeforeEdit_}) {
        edit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        edit->setCalendarPopup(true);
        edit->setEnabled(false);
    }
    connect(modifiedAfterCheck_, &QCheckBox::toggled, modifiedAfterEdit_, &QWidget::setEnabled);
    connect(modifiedBeforeCheck_, &QCheckBox::toggled, modifiedBeforeEdit_, &QWidget::setEnabled);
    filterForm->addRow(modifiedAfterCheck_, modifiedAfterEdit_);
    filterForm->addRow(modifiedBeforeCheck_, modifiedBeforeEdit_);
    filterForm->addRow(QStringLiteral("最小文件大小"), minimumSizeSpin_);
    filterForm->addRow(QStringLiteral("最大文件大小"), maximumSizeSpin_);
    filterForm->addRow({}, includeHiddenCheck_);
    filterForm->addRow({}, verifyBackupCheck_);
    layout->addWidget(filterGroup);

    backupButton_ = new QPushButton(QStringLiteral("开始备份"), page);
    backupButton_->setObjectName(QStringLiteral("backupButton"));
    backupButton_->setProperty("primary", true);
    connect(backupButton_, &QPushButton::clicked, this, [this] {
        startBackup(backupRequestFromForm());
    });
    auto* actionLayout = new QHBoxLayout;
    actionLayout->addStretch();
    actionLayout->addWidget(backupButton_);
    layout->addLayout(actionLayout);
    layout->addStretch();
    return page;
}

QWidget* MainWindow::createRestorePage()
{
    auto* page = new QWidget(this);
    page->setObjectName(QStringLiteral("contentPage"));
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(8, 12, 8, 6);
    layout->setSpacing(10);
    auto* pageHint = new QLabel(
        QStringLiteral("从指定快照恢复文件，增量和差异快照会自动解析完整备份链。"), page);
    pageHint->setObjectName(QStringLiteral("pageHint"));
    layout->addWidget(pageHint);

    auto* restoreGroup = new QGroupBox(QStringLiteral("还原配置"), page);
    auto* form = new QFormLayout(restoreGroup);
    form->setContentsMargins(16, 20, 16, 14);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setHorizontalSpacing(14);
    form->setVerticalSpacing(11);

    restoreBackupEdit_ = new QLineEdit(restoreGroup);
    restoreBackupEdit_->setObjectName(QStringLiteral("restoreBackupEdit"));
    restoreDestinationEdit_ = new QLineEdit(restoreGroup);
    restoreDestinationEdit_->setObjectName(QStringLiteral("restoreDestinationEdit"));
    conflictPolicyCombo_ = new ScrollSafeControl<QComboBox>(restoreGroup);
    conflictPolicyCombo_->setObjectName(QStringLiteral("conflictPolicyCombo"));
    conflictPolicyCombo_->addItem(QStringLiteral("覆盖同名文件"), static_cast<int>(ConflictPolicy::Overwrite));
    conflictPolicyCombo_->addItem(QStringLiteral("跳过同名文件"), static_cast<int>(ConflictPolicy::Skip));
    conflictPolicyCombo_->addItem(QStringLiteral("自动重命名"), static_cast<int>(ConflictPolicy::Rename));
    verifyRestoreCheck_ = new QCheckBox(QStringLiteral("还原后逐字节校验"), restoreGroup);
    verifyRestoreCheck_->setObjectName(QStringLiteral("verifyRestoreCheck"));

    form->addRow(
        QStringLiteral("备份目录"),
        pathRow(restoreBackupEdit_, restoreGroup, QStringLiteral("选择一个具体备份目录")));
    form->addRow(
        QStringLiteral("还原到"),
        pathRow(restoreDestinationEdit_, restoreGroup, QStringLiteral("选择还原目标目录")));
    form->addRow(QStringLiteral("同名文件"), conflictPolicyCombo_);
    form->addRow({}, verifyRestoreCheck_);
    layout->addWidget(restoreGroup);

    auto* help = new QLabel(
        QStringLiteral("请选择包含 backup.meta 和 manifest.tsv 的具体备份目录。增量或差异备份会自动读取其备份链。"),
        page);
    help->setWordWrap(true);
    help->setObjectName(QStringLiteral("pageHint"));
    layout->addWidget(help);

    restoreButton_ = new QPushButton(QStringLiteral("开始还原"), page);
    restoreButton_->setObjectName(QStringLiteral("restoreButton"));
    restoreButton_->setProperty("primary", true);
    connect(restoreButton_, &QPushButton::clicked, this, [this] {
        startRestore(restoreRequestFromForm());
    });
    auto* actionLayout = new QHBoxLayout;
    actionLayout->addStretch();
    actionLayout->addWidget(restoreButton_);
    layout->addLayout(actionLayout);
    layout->addStretch();
    return page;
}

QWidget* MainWindow::createSchedulePage()
{
    auto* page = new QWidget(this);
    page->setObjectName(QStringLiteral("contentPage"));
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(8, 12, 8, 6);
    layout->setSpacing(10);
    auto* note = new QLabel(
        QStringLiteral("新任务使用“备份”页当前填写的目录、模式和筛选规则。定时任务会保存在本机设置中。"),
        page);
    note->setWordWrap(true);
    note->setObjectName(QStringLiteral("pageHint"));
    layout->addWidget(note);

    auto* scheduleGroup = new QGroupBox(QStringLiteral("任务配置"), page);
    auto* form = new QFormLayout(scheduleGroup);
    form->setContentsMargins(16, 20, 16, 14);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setHorizontalSpacing(14);
    form->setVerticalSpacing(11);
    scheduleNameEdit_ = new QLineEdit(scheduleGroup);
    scheduleNameEdit_->setObjectName(QStringLiteral("scheduleNameEdit"));
    scheduleFrequencyCombo_ = new ScrollSafeControl<QComboBox>(scheduleGroup);
    scheduleFrequencyCombo_->setObjectName(QStringLiteral("scheduleFrequencyCombo"));
    scheduleFrequencyCombo_->addItem(QStringLiteral("单次"), static_cast<int>(ScheduleFrequency::Once));
    scheduleFrequencyCombo_->addItem(QStringLiteral("每日"), static_cast<int>(ScheduleFrequency::Daily));
    scheduleFrequencyCombo_->addItem(QStringLiteral("每周"), static_cast<int>(ScheduleFrequency::Weekly));
    scheduleFrequencyCombo_->addItem(
        QStringLiteral("自定义间隔"), static_cast<int>(ScheduleFrequency::CustomInterval));
    scheduleIntervalSpin_ = new ScrollSafeControl<QSpinBox>(scheduleGroup);
    scheduleIntervalSpin_->setObjectName(QStringLiteral("scheduleIntervalSpin"));
    scheduleIntervalSpin_->setRange(1, 525600);
    scheduleIntervalSpin_->setValue(60);
    scheduleIntervalSpin_->setSuffix(QStringLiteral(" 分钟"));
    scheduleNextRunEdit_ = new ScrollSafeControl<QDateTimeEdit>(
        QDateTime::currentDateTime().addSecs(60), scheduleGroup);
    scheduleNextRunEdit_->setObjectName(QStringLiteral("scheduleNextRunEdit"));
    scheduleNextRunEdit_->setCalendarPopup(true);
    scheduleNextRunEdit_->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));

    form->addRow(QStringLiteral("任务名称"), scheduleNameEdit_);
    form->addRow(QStringLiteral("频率"), scheduleFrequencyCombo_);
    form->addRow(QStringLiteral("自定义间隔"), scheduleIntervalSpin_);
    form->addRow(QStringLiteral("下次执行"), scheduleNextRunEdit_);
    retentionSpin_ = new ScrollSafeControl<QSpinBox>(scheduleGroup);
    retentionSpin_->setObjectName(QStringLiteral("retentionSpin"));
    retentionSpin_->setRange(0, 10000);
    retentionSpin_->setSpecialValueText(QStringLiteral("不淘汰"));
    retentionSpin_->setSuffix(QStringLiteral(" 个快照（依赖的基线额外保留）"));
    form->addRow(QStringLiteral("保留最新"), retentionSpin_);
    auto* retentionHint = new QLabel(QStringLiteral("淘汰仅用于定时任务成功后的同源、同名快照；0 表示不淘汰。程序关闭后不执行定时备份。"), page);
    retentionHint->setWordWrap(true);
    layout->addWidget(retentionHint);
    layout->addWidget(scheduleGroup);

    auto* addButton = new QPushButton(QStringLiteral("创建定时备份"), page);
    addButton->setObjectName(QStringLiteral("addScheduleButton"));
    addButton->setProperty("primary", true);
    connect(addButton, &QPushButton::clicked, this, [this] { addSchedule(); });
    auto* addActionLayout = new QHBoxLayout;
    addActionLayout->addStretch();
    addActionLayout->addWidget(addButton);
    layout->addLayout(addActionLayout);

    scheduleList_ = new QListWidget(page);
    scheduleList_->setObjectName(QStringLiteral("scheduleList"));
    layout->addWidget(scheduleList_, 1);
    auto* buttons = new QHBoxLayout;
    toggleScheduleButton_ = new QPushButton(QStringLiteral("启用/停用"), page);
    removeScheduleButton_ = new QPushButton(QStringLiteral("删除任务"), page);
    toggleScheduleButton_->setProperty("secondary", true);
    removeScheduleButton_->setProperty("danger", true);
    connect(toggleScheduleButton_, &QPushButton::clicked, this, [this] { toggleSelectedSchedule(); });
    connect(removeScheduleButton_, &QPushButton::clicked, this, [this] { removeSelectedSchedule(); });
    buttons->addWidget(toggleScheduleButton_);
    buttons->addWidget(removeScheduleButton_);
    buttons->addStretch();
    layout->addLayout(buttons);
    return page;
}

QWidget* MainWindow::createArchivePage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(8, 12, 8, 6);
    auto* hint = new QLabel(QStringLiteral("将备份快照导出为一个 .dbpack 文件，解包后即可还原。导出增量、差异快照时会自动补齐依赖文件。"), page);
    hint->setWordWrap(true); hint->setObjectName(QStringLiteral("pageHint"));
    layout->addWidget(hint);
    auto* packGroup = new QGroupBox(QStringLiteral("导出单文件备份包"), page);
    auto* packForm = new QFormLayout(packGroup);
    packSourceEdit_ = new QLineEdit(packGroup);
    packSourceEdit_->setObjectName(QStringLiteral("packSourceEdit"));
    packSourceEdit_->setPlaceholderText(QStringLiteral("选择包含 backup.meta 的具体快照；备份成功后自动填入"));
    packFileEdit_ = new QLineEdit(packGroup);
    packFileEdit_->setObjectName(QStringLiteral("packFileEdit"));
    packForm->addRow(QStringLiteral("备份快照"), pathRow(packSourceEdit_, packGroup, QStringLiteral("选择要打包的快照")));
    packForm->addRow(QStringLiteral("新备份包"), packageFileRow(packFileEdit_, packGroup, true));
    packButton_ = new QPushButton(QStringLiteral("打包导出"), packGroup);
    packButton_->setObjectName(QStringLiteral("packButton"));
    packButton_->setProperty("primary", true);
    connect(packButton_, &QPushButton::clicked, this, [this] { startArchive(false); });
    packForm->addRow({}, packButton_);
    layout->addWidget(packGroup);
    auto* unpackGroup = new QGroupBox(QStringLiteral("解包导入"), page);
    auto* unpackForm = new QFormLayout(unpackGroup);
    unpackFileEdit_ = new QLineEdit(unpackGroup);
    unpackFileEdit_->setObjectName(QStringLiteral("unpackFileEdit"));
    unpackDestinationEdit_ = new QLineEdit(unpackGroup);
    unpackDestinationEdit_->setObjectName(QStringLiteral("unpackDestinationEdit"));
    unpackForm->addRow(QStringLiteral("备份包"), packageFileRow(unpackFileEdit_, unpackGroup, false));
    unpackForm->addRow(QStringLiteral("输出位置"), pathRow(unpackDestinationEdit_, unpackGroup, QStringLiteral("选择解包输出父目录")));
    auto* note = new QLabel(QStringLiteral("解包时校验每个文件，并新建独立快照目录。成功后自动填入“还原”页，再选择目标目录还原文件。"), unpackGroup);
    note->setWordWrap(true);
    unpackForm->addRow(note);
    unpackButton_ = new QPushButton(QStringLiteral("校验并解包"), unpackGroup);
    unpackButton_->setObjectName(QStringLiteral("unpackButton"));
    unpackButton_->setProperty("primary", true);
    connect(unpackButton_, &QPushButton::clicked, this, [this] { startArchive(true); });
    unpackForm->addRow({}, unpackButton_);
    layout->addWidget(unpackGroup);
    layout->addStretch();
    return page;
}

QWidget* MainWindow::createProgressPanel()
{
    auto* group = new QGroupBox(QStringLiteral("任务状态"), this);
    group->setObjectName(QStringLiteral("taskPanel"));
    auto* layout = new QGridLayout(group);
    operationLabel_ = new QLabel(QStringLiteral("当前无任务"), group);
    currentPathLabel_ = new QLabel(group);
    currentPathLabel_->setWordWrap(true);
    progressDetailsLabel_ = new QLabel(QStringLiteral("文件：0　字节：0"), group);
    progressBar_ = new QProgressBar(group);
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    cancelButton_ = new QPushButton(QStringLiteral("取消"), group);
    cancelButton_->setProperty("danger", true);
    cancelButton_->setEnabled(false);
    connect(cancelButton_, &QPushButton::clicked, this, [this] { cancelOperation(); });

    layout->addWidget(operationLabel_, 0, 0, 1, 2);
    layout->addWidget(currentPathLabel_, 1, 0, 1, 2);
    layout->addWidget(progressDetailsLabel_, 2, 0, 1, 2);
    layout->addWidget(progressBar_, 3, 0);
    layout->addWidget(cancelButton_, 3, 1);
    return group;
}

BackupRequest MainWindow::backupRequestFromForm() const
{
    BackupRequest request;
    request.sourceDirectory = pathFromText(sourceEdit_->text());
    request.destinationDirectory = pathFromText(backupDestinationEdit_->text());
    request.backupName = backupNameEdit_->text().trimmed().toUtf8().toStdString();
    request.mode = static_cast<BackupMode>(backupModeCombo_->currentData().toInt());
    request.filters.includedExtensions = splitStrings(includedExtensionsEdit_->text(), ',');
    request.filters.excludedExtensions = splitStrings(excludedExtensionsEdit_->text(), ',');
    request.filters.excludedPaths = splitPaths(excludedPathsEdit_->text());
    request.filters.includedNamePatterns = splitStrings(includedNamesEdit_->text(), ';');
    request.filters.excludedNamePatterns = splitStrings(excludedNamesEdit_->text(), ';');
    if (modifiedAfterCheck_->isChecked()) {
        const auto seconds = modifiedAfterEdit_->dateTime().toSecsSinceEpoch();
        request.filters.modifiedAfterMs = seconds * 1000;
    }
    if (modifiedBeforeCheck_->isChecked()) {
        const auto seconds = modifiedBeforeEdit_->dateTime().toSecsSinceEpoch();
        request.filters.modifiedBeforeMs = seconds * 1000 + 999;
    }
    constexpr double bytesPerMegabyte = 1024.0 * 1024.0;
    request.filters.minimumFileSize = static_cast<std::uintmax_t>(
        minimumSizeSpin_->value() * bytesPerMegabyte);
    request.filters.maximumFileSize = static_cast<std::uintmax_t>(
        maximumSizeSpin_->value() * bytesPerMegabyte);
    request.filters.includeHiddenFiles = includeHiddenCheck_->isChecked();
    request.verifyAfterCopy = verifyBackupCheck_->isChecked();
    return request;
}

RestoreRequest MainWindow::restoreRequestFromForm() const
{
    RestoreRequest request;
    request.backupDirectory = pathFromText(restoreBackupEdit_->text());
    request.destinationDirectory = pathFromText(restoreDestinationEdit_->text());
    request.conflictPolicy = static_cast<ConflictPolicy>(conflictPolicyCombo_->currentData().toInt());
    request.verifyAfterRestore = verifyRestoreCheck_->isChecked();
    return request;
}

void MainWindow::startBackup(const BackupRequest& request, const QString& context)
{
    if (worker_) {
        return;
    }
    if (request.sourceDirectory.empty() || request.destinationDirectory.empty()) {
        QMessageBox::warning(this, QStringLiteral("无法开始"), QStringLiteral("请选择源目录和备份仓库。"));
        return;
    }

    if (request.filters.modifiedAfterMs && request.filters.modifiedBeforeMs
        && *request.filters.modifiedAfterMs > *request.filters.modifiedBeforeMs) {
        QMessageBox::warning(this, QStringLiteral("时间范围无效"), QStringLiteral("修改时间起始不能晚于截止时间。"));
        return;
    }

    cancellationRequested_.store(false);
    setBusy(true);
    operationLabel_->setText(context.isEmpty() ? QStringLiteral("正在备份") : context);
    appendLog(context.isEmpty() ? QStringLiteral("手动备份开始") : context);
    const bool automatic = !context.isEmpty();
    worker_ = QThread::create([this, request, automatic] {
        const auto result = backupManager_->backup(
            request,
            [this](const ProgressInfo& progress) {
                QMetaObject::invokeMethod(this, [this, progress] { updateProgress(progress); });
            },
            [this] { return cancellationRequested_.load(); });
        QMetaObject::invokeMethod(this, [this, result, automatic] {
            finishOperation(result, QStringLiteral("备份"), automatic);
        });
    });
    connect(worker_, &QThread::finished, this, [this] {
        worker_->deleteLater();
        worker_ = nullptr;
        setBusy(false);
    });
    worker_->start();
}

void MainWindow::startRestore(const RestoreRequest& request)
{
    if (worker_) {
        return;
    }
    if (request.backupDirectory.empty() || request.destinationDirectory.empty()) {
        QMessageBox::warning(this, QStringLiteral("无法开始"), QStringLiteral("请选择备份目录和还原目标。"));
        return;
    }

    cancellationRequested_.store(false);
    setBusy(true);
    operationLabel_->setText(QStringLiteral("正在还原"));
    worker_ = QThread::create([this, request] {
        const auto result = restoreManager_->restore(
            request,
            [this](const ProgressInfo& progress) {
                QMetaObject::invokeMethod(this, [this, progress] { updateProgress(progress); });
            },
            [this] { return cancellationRequested_.load(); });
        QMetaObject::invokeMethod(this, [this, result] {
            finishOperation(result, QStringLiteral("还原"));
        });
    });
    connect(worker_, &QThread::finished, this, [this] {
        worker_->deleteLater();
        worker_ = nullptr;
        setBusy(false);
    });
    worker_->start();
}

void MainWindow::cancelOperation()
{
    cancellationRequested_.store(true);
    cancelButton_->setEnabled(false);
    statusBar()->showMessage(QStringLiteral("正在安全取消…"));
}

void MainWindow::startArchive(bool unpack)
{
    if (worker_) return;
    const PackRequest packRequest{pathFromText(packSourceEdit_->text()), pathFromText(packFileEdit_->text())};
    const UnpackRequest unpackRequest{pathFromText(unpackFileEdit_->text()), pathFromText(unpackDestinationEdit_->text())};
    if ((!unpack && (packRequest.backupDirectory.empty() || packRequest.archiveFile.empty()))
        || (unpack && (unpackRequest.archiveFile.empty() || unpackRequest.destinationDirectory.empty()))) {
        QMessageBox::warning(this, QStringLiteral("路径未填写"), QStringLiteral("请先填写本次打包或解包所需的路径。"));
        return;
    }
    const auto operation = unpack ? QStringLiteral("解包") : QStringLiteral("打包");
    cancellationRequested_.store(false);
    setBusy(true); operationLabel_->setText(operation + QStringLiteral("进行中"));
    appendLog(operation + QStringLiteral("开始"));
    worker_ = QThread::create([this, packRequest, unpackRequest, unpack, operation] {
        auto progress = [this](const ProgressInfo& info) {
            QMetaObject::invokeMethod(this, [this, info] { updateProgress(info); });
        };
        auto cancelled = [this] { return cancellationRequested_.load(); };
        const auto result = unpack ? archiveManager_->unpack(unpackRequest, progress, cancelled)
                                   : archiveManager_->pack(packRequest, progress, cancelled);
        QMetaObject::invokeMethod(this, [this, result, operation] { finishOperation(result, operation); });
    });
    connect(worker_, &QThread::finished, this, [this] {
        worker_->deleteLater(); worker_ = nullptr; setBusy(false);
    });
    worker_->start();
}

void MainWindow::updateProgress(const ProgressInfo& progress)
{
    operationLabel_->setText(phaseText(progress.phase));
    currentPathLabel_->setText(pathToText(progress.currentPath));
    const auto& locale = QLocale();
    const QString totalFiles = progress.totalFiles == 0
        ? QStringLiteral("?")
        : locale.toString(static_cast<qulonglong>(progress.totalFiles));
    const QString totalBytes = progress.totalBytes == 0
        ? QStringLiteral("?")
        : locale.formattedDataSize(static_cast<qint64>(progress.totalBytes));
    progressDetailsLabel_->setText(
        QStringLiteral("文件：%1 / %2　字节：%3 / %4")
            .arg(locale.toString(static_cast<qulonglong>(progress.processedFiles)))
            .arg(totalFiles)
            .arg(locale.formattedDataSize(static_cast<qint64>(progress.processedBytes)))
            .arg(totalBytes));
    int percentage = 0;
    if (progress.totalBytes > 0) {
        percentage = static_cast<int>(std::min<std::uintmax_t>(
            100,
            progress.processedBytes * 100 / progress.totalBytes));
    } else if (progress.totalFiles > 0) {
        percentage = static_cast<int>(std::min<std::uint64_t>(
            100,
            progress.processedFiles * 100 / progress.totalFiles));
    }
    progressBar_->setValue(percentage);
}

void MainWindow::appendLog(const QString& text)
{
    const auto line = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss ")) + text;
    historyList_->addItem(line);
    historyList_->scrollToBottom();
    QFile file(QCoreApplication::applicationDirPath() + QStringLiteral("/operations.log"));
    if (file.open(QIODevice::Append | QIODevice::Text)) file.write((line + '\n').toUtf8());
}

void MainWindow::finishOperation(const OperationResult& result, const QString& operationName, bool automatic)
{
    QString message = QStringLiteral("%1\n处理文件：%2　跳过文件：%3　处理数据：%4")
                                .arg(QString::fromUtf8(result.message.c_str()))
                                .arg(result.processedFiles)
                                .arg(result.skippedFiles)
                                .arg(QLocale().formattedDataSize(
                                    static_cast<qint64>(result.processedBytes)));
    if (operationName == QStringLiteral("备份")) {
        message += QStringLiteral("\n跳过明细：筛选排除 %1 个文件；未变化免复制 %2 个文件。")
            .arg(result.filteredFiles).arg(result.unchangedFiles);
        if (result.uncountedExcludedPaths != 0) {
            message += QStringLiteral("\n另有 %1 处已排除路径无法完整读取，内部未统计文件不计入上述数字。")
                .arg(result.uncountedExcludedPaths);
        }
    }
    appendLog(operationName + (result.succeeded() ? QStringLiteral("成功：") : QStringLiteral("未完成："))
        + message + QStringLiteral(" 输出：") + pathToText(result.outputPath));
    if (result.status == OperationStatus::Success) {
        progressBar_->setValue(100);
        operationLabel_->setText(operationName + QStringLiteral("完成"));
        currentPathLabel_->setText(
            result.outputPath.empty() ? QString() : pathToText(result.outputPath));
        statusBar()->showMessage(message, 10000);
        if (!automatic) QMessageBox::information(this, operationName + QStringLiteral("完成"), message);
        if (operationName == QStringLiteral("备份") && !result.outputPath.empty()) {
            restoreBackupEdit_->setText(pathToText(result.outputPath));
            packSourceEdit_->setText(pathToText(result.outputPath));
            packFileEdit_->setText(QDir::toNativeSeparators(QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(
                QStringLiteral("demo/packages/") + pathToText(result.outputPath.filename()) + QStringLiteral(".dbpack"))));
        } else if (operationName == QStringLiteral("打包")) {
            unpackFileEdit_->setText(pathToText(result.outputPath));
        } else if (operationName == QStringLiteral("解包")) {
            restoreBackupEdit_->setText(pathToText(result.outputPath));
            packSourceEdit_->setText(pathToText(result.outputPath));
        }
    } else if (result.status == OperationStatus::Cancelled) {
        operationLabel_->setText(operationName + QStringLiteral("已取消"));
        statusBar()->showMessage(message, 8000);
    } else {
        operationLabel_->setText(operationName + QStringLiteral("失败"));
        statusBar()->showMessage(message, 10000);
        if (!automatic) QMessageBox::critical(this, operationName + QStringLiteral("失败"), message);
    }
}

void MainWindow::setBusy(bool busy)
{
    backupButton_->setEnabled(!busy);
    restoreButton_->setEnabled(!busy);
    packButton_->setEnabled(!busy);
    unpackButton_->setEnabled(!busy);
    cancelButton_->setEnabled(busy);
    tabs_->setTabEnabled(2, !busy);
    if (!busy) {
        cancellationRequested_.store(false);
    }
}

void MainWindow::addSchedule()
{
    const auto request = backupRequestFromForm();
    if (request.filters.modifiedAfterMs && request.filters.modifiedBeforeMs
        && *request.filters.modifiedAfterMs > *request.filters.modifiedBeforeMs) {
        QMessageBox::warning(this, QStringLiteral("时间范围无效"), QStringLiteral("请先修正备份页的修改时间范围。"));
        return;
    }
    if (request.sourceDirectory.empty() || request.destinationDirectory.empty()) {
        QMessageBox::warning(
            this,
            QStringLiteral("无法创建任务"),
            QStringLiteral("请先在“备份”页填写源目录和备份仓库。"));
        return;
    }

    ScheduledBackup schedule;
    schedule.id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    schedule.name = scheduleNameEdit_->text().trimmed().toUtf8().toStdString();
    if (schedule.name.empty()) {
        schedule.name = "Scheduled backup";
    }
    schedule.request = request;
    schedule.request.maximumSnapshots = static_cast<std::uint32_t>(retentionSpin_->value());
    schedule.frequency = static_cast<ScheduleFrequency>(
        scheduleFrequencyCombo_->currentData().toInt());
    schedule.customInterval = std::chrono::minutes(scheduleIntervalSpin_->value());
    schedule.nextRun = timePointFromDateTime(scheduleNextRunEdit_->dateTime());
    schedule.enabled = true;
    if (!scheduler_->add(schedule)) {
        QMessageBox::warning(this, QStringLiteral("创建失败"), QStringLiteral("定时任务参数无效。"));
        return;
    }
    scheduleNameEdit_->clear();
    refreshScheduleList();
    appendLog(QStringLiteral("创建定时任务：%1；保留数量：%2")
        .arg(QString::fromUtf8(schedule.name.c_str())).arg(schedule.request.maximumSnapshots));
}

void MainWindow::removeSelectedSchedule()
{
    auto* item = scheduleList_->currentItem();
    if (!item) {
        return;
    }
    (void)scheduler_->remove(item->data(Qt::UserRole).toString().toStdString());
    refreshScheduleList();
}

void MainWindow::toggleSelectedSchedule()
{
    auto* selected = scheduleList_->currentItem();
    if (!selected) {
        return;
    }
    const auto id = selected->data(Qt::UserRole).toString().toStdString();
    auto schedules = scheduler_->schedules();
    const auto found = std::find_if(schedules.begin(), schedules.end(), [&](const auto& item) {
        return item.id == id;
    });
    if (found != schedules.end()) {
        found->enabled = !found->enabled;
        (void)scheduler_->update(*found);
    }
    refreshScheduleList();
}

void MainWindow::refreshScheduleList()
{
    scheduleList_->clear();
    for (const auto& schedule : scheduler_->schedules()) {
        const QString text = QStringLiteral("%1 | %2 | %3 | %4")
                                 .arg(QString::fromUtf8(schedule.name.c_str()))
                                 .arg(schedule.enabled ? QStringLiteral("启用") : QStringLiteral("停用"))
                                 .arg(frequencyText(schedule.frequency))
                                 .arg(dateTimeFromTimePoint(schedule.nextRun).toString(
                                     QStringLiteral("yyyy-MM-dd HH:mm:ss")));
        auto* item = new QListWidgetItem(text, scheduleList_);
        item->setData(Qt::UserRole, QString::fromStdString(schedule.id));
    }
}

} // namespace backup::gui
