#include "backup/scheduler/QtScheduler.h"

#include <QSettings>
#include <QStringList>
#include <QTimer>

#include <algorithm>
#include <chrono>

namespace backup {
namespace {

QString pathToQString(const std::filesystem::path& path)
{
    return QString::fromStdWString(path.wstring());
}

std::filesystem::path qStringToPath(const QString& value)
{
    return std::filesystem::path(value.toStdWString());
}

QStringList stringsToQStringList(const std::vector<std::string>& values)
{
    QStringList result;
    for (const auto& value : values) {
        result.push_back(QString::fromUtf8(value.c_str()));
    }
    return result;
}

std::vector<std::string> qStringListToStrings(const QStringList& values)
{
    std::vector<std::string> result;
    result.reserve(values.size());
    for (const auto& value : values) {
        result.push_back(value.toUtf8().toStdString());
    }
    return result;
}

QStringList pathsToQStringList(const std::vector<std::filesystem::path>& values)
{
    QStringList result;
    for (const auto& value : values) {
        result.push_back(pathToQString(value));
    }
    return result;
}

std::vector<std::filesystem::path> qStringListToPaths(const QStringList& values)
{
    std::vector<std::filesystem::path> result;
    result.reserve(values.size());
    for (const auto& value : values) {
        result.push_back(qStringToPath(value));
    }
    return result;
}

qint64 toMilliseconds(std::chrono::system_clock::time_point value)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        value.time_since_epoch()).count();
}

std::chrono::system_clock::time_point fromMilliseconds(qint64 value)
{
    return std::chrono::system_clock::time_point(std::chrono::milliseconds(value));
}

bool validSchedule(const ScheduledBackup& schedule)
{
    if (schedule.id.empty() || schedule.request.sourceDirectory.empty()
        || schedule.request.destinationDirectory.empty()) {
        return false;
    }
    const auto& filters = schedule.request.filters;
    if (filters.modifiedAfterMs && filters.modifiedBeforeMs
        && *filters.modifiedAfterMs > *filters.modifiedBeforeMs) return false;
    switch (schedule.frequency) {
    case ScheduleFrequency::Once:
    case ScheduleFrequency::Daily:
    case ScheduleFrequency::Weekly:
        return true;
    case ScheduleFrequency::CustomInterval:
        return schedule.customInterval.count() > 0;
    }
    return false;
}

} // namespace

class QtScheduler::Impl {
public:
    explicit Impl(QtScheduler* owner)
    {
        timer.setInterval(1000);
        QObject::connect(&timer, &QTimer::timeout, owner, [this] { checkDueSchedules(); });
        load();
    }

    void load()
    {
        QSettings settings;
        settings.beginGroup(QStringLiteral("scheduler"));
        const int count = settings.beginReadArray(QStringLiteral("tasks"));
        items.clear();
        items.reserve(static_cast<std::size_t>(count));
        for (int index = 0; index < count; ++index) {
            settings.setArrayIndex(index);
            ScheduledBackup item;
            item.id = settings.value(QStringLiteral("id")).toString().toStdString();
            item.name = settings.value(QStringLiteral("name")).toString().toUtf8().toStdString();
            item.enabled = settings.value(QStringLiteral("enabled"), true).toBool();
            item.frequency = static_cast<ScheduleFrequency>(
                settings.value(QStringLiteral("frequency"), 0).toInt());
            item.nextRun = fromMilliseconds(settings.value(QStringLiteral("nextRun")).toLongLong());
            item.customInterval = std::chrono::minutes(
                settings.value(QStringLiteral("customInterval"), 0).toLongLong());

            item.request.sourceDirectory = qStringToPath(
                settings.value(QStringLiteral("source")).toString());
            item.request.destinationDirectory = qStringToPath(
                settings.value(QStringLiteral("destination")).toString());
            item.request.backupName = settings.value(QStringLiteral("backupName"))
                                          .toString().toUtf8().toStdString();
            item.request.mode = static_cast<BackupMode>(
                settings.value(QStringLiteral("mode"), 0).toInt());
            item.request.preserveTimestamps = settings.value(
                QStringLiteral("preserveTimestamps"), true).toBool();
            item.request.verifyAfterCopy = settings.value(
                QStringLiteral("verifyAfterCopy"), false).toBool();
            item.request.maximumSnapshots = settings.value(QStringLiteral("maximumSnapshots"), 0).toUInt();
            item.request.filters.includedExtensions = qStringListToStrings(
                settings.value(QStringLiteral("includedExtensions")).toStringList());
            item.request.filters.excludedExtensions = qStringListToStrings(
                settings.value(QStringLiteral("excludedExtensions")).toStringList());
            item.request.filters.excludedPaths = qStringListToPaths(
                settings.value(QStringLiteral("excludedPaths")).toStringList());
            item.request.filters.minimumFileSize = settings.value(
                QStringLiteral("minimumFileSize"), 0).toULongLong();
            item.request.filters.maximumFileSize = settings.value(
                QStringLiteral("maximumFileSize"), 0).toULongLong();
            item.request.filters.includeHiddenFiles = settings.value(
                QStringLiteral("includeHiddenFiles"), false).toBool();
            item.request.filters.includedNamePatterns = qStringListToStrings(
                settings.value(QStringLiteral("includedNamePatterns")).toStringList());
            item.request.filters.excludedNamePatterns = qStringListToStrings(
                settings.value(QStringLiteral("excludedNamePatterns")).toStringList());
            if (settings.contains(QStringLiteral("modifiedAfterMs"))) {
                item.request.filters.modifiedAfterMs = settings.value(QStringLiteral("modifiedAfterMs")).toLongLong();
            }
            if (settings.contains(QStringLiteral("modifiedBeforeMs"))) {
                item.request.filters.modifiedBeforeMs = settings.value(QStringLiteral("modifiedBeforeMs")).toLongLong();
            }
            if (validSchedule(item)) {
                items.push_back(std::move(item));
            }
        }
        settings.endArray();
        settings.endGroup();
    }

    void persist() const
    {
        QSettings settings;
        settings.beginGroup(QStringLiteral("scheduler"));
        settings.remove(QStringLiteral("tasks"));
        settings.beginWriteArray(QStringLiteral("tasks"), static_cast<int>(items.size()));
        for (int index = 0; index < static_cast<int>(items.size()); ++index) {
            const auto& item = items[static_cast<std::size_t>(index)];
            settings.setArrayIndex(index);
            settings.setValue(QStringLiteral("id"), QString::fromStdString(item.id));
            settings.setValue(QStringLiteral("name"), QString::fromUtf8(item.name.c_str()));
            settings.setValue(QStringLiteral("enabled"), item.enabled);
            settings.setValue(QStringLiteral("frequency"), static_cast<int>(item.frequency));
            settings.setValue(QStringLiteral("nextRun"), toMilliseconds(item.nextRun));
            settings.setValue(
                QStringLiteral("customInterval"), item.customInterval.count());

            settings.setValue(QStringLiteral("source"), pathToQString(item.request.sourceDirectory));
            settings.setValue(
                QStringLiteral("destination"), pathToQString(item.request.destinationDirectory));
            settings.setValue(
                QStringLiteral("backupName"), QString::fromUtf8(item.request.backupName.c_str()));
            settings.setValue(QStringLiteral("mode"), static_cast<int>(item.request.mode));
            settings.setValue(
                QStringLiteral("preserveTimestamps"), item.request.preserveTimestamps);
            settings.setValue(QStringLiteral("verifyAfterCopy"), item.request.verifyAfterCopy);
            settings.setValue(QStringLiteral("maximumSnapshots"), item.request.maximumSnapshots);
            settings.setValue(
                QStringLiteral("includedExtensions"),
                stringsToQStringList(item.request.filters.includedExtensions));
            settings.setValue(
                QStringLiteral("excludedExtensions"),
                stringsToQStringList(item.request.filters.excludedExtensions));
            settings.setValue(
                QStringLiteral("excludedPaths"),
                pathsToQStringList(item.request.filters.excludedPaths));
            settings.setValue(
                QStringLiteral("minimumFileSize"),
                static_cast<qulonglong>(item.request.filters.minimumFileSize));
            settings.setValue(
                QStringLiteral("maximumFileSize"),
                static_cast<qulonglong>(item.request.filters.maximumFileSize));
            settings.setValue(
                QStringLiteral("includeHiddenFiles"),
                item.request.filters.includeHiddenFiles);
            settings.setValue(QStringLiteral("includedNamePatterns"),
                stringsToQStringList(item.request.filters.includedNamePatterns));
            settings.setValue(QStringLiteral("excludedNamePatterns"),
                stringsToQStringList(item.request.filters.excludedNamePatterns));
            if (item.request.filters.modifiedAfterMs) settings.setValue(QStringLiteral("modifiedAfterMs"),
                static_cast<qlonglong>(*item.request.filters.modifiedAfterMs));
            if (item.request.filters.modifiedBeforeMs) settings.setValue(QStringLiteral("modifiedBeforeMs"),
                static_cast<qlonglong>(*item.request.filters.modifiedBeforeMs));
        }
        settings.endArray();
        settings.endGroup();
        settings.sync();
    }

    void checkDueSchedules()
    {
        const auto now = std::chrono::system_clock::now();
        std::vector<ScheduledBackup> due;
        for (auto& item : items) {
            if (!item.enabled || item.nextRun > now) {
                continue;
            }
            due.push_back(item);
            switch (item.frequency) {
            case ScheduleFrequency::Once:
                item.enabled = false;
                break;
            case ScheduleFrequency::Daily:
                do {
                    item.nextRun += std::chrono::hours(24);
                } while (item.nextRun <= now);
                break;
            case ScheduleFrequency::Weekly:
                do {
                    item.nextRun += std::chrono::hours(24 * 7);
                } while (item.nextRun <= now);
                break;
            case ScheduleFrequency::CustomInterval:
                if (item.customInterval.count() <= 0) {
                    item.enabled = false;
                } else {
                    do {
                        item.nextRun += item.customInterval;
                    } while (item.nextRun <= now);
                }
                break;
            }
        }
        if (!due.empty()) {
            persist();
            if (callback) {
                for (const auto& item : due) {
                    callback(item);
                }
            }
        }
    }

    QTimer timer;
    std::vector<ScheduledBackup> items;
    ScheduledBackupCallback callback;
};

QtScheduler::QtScheduler(QObject* parent)
    : QObject(parent)
    , impl_(std::make_unique<Impl>(this))
{
}

QtScheduler::~QtScheduler() = default;

bool QtScheduler::add(const ScheduledBackup& schedule)
{
    if (!validSchedule(schedule)
        || std::any_of(impl_->items.begin(), impl_->items.end(), [&](const auto& item) {
               return item.id == schedule.id;
           })) {
        return false;
    }
    impl_->items.push_back(schedule);
    impl_->persist();
    return true;
}

bool QtScheduler::update(const ScheduledBackup& schedule)
{
    if (!validSchedule(schedule)) {
        return false;
    }
    const auto found = std::find_if(impl_->items.begin(), impl_->items.end(), [&](const auto& item) {
        return item.id == schedule.id;
    });
    if (found == impl_->items.end()) {
        return false;
    }
    *found = schedule;
    impl_->persist();
    return true;
}

bool QtScheduler::remove(const std::string& id)
{
    const auto oldSize = impl_->items.size();
    impl_->items.erase(
        std::remove_if(impl_->items.begin(), impl_->items.end(), [&](const auto& item) {
            return item.id == id;
        }),
        impl_->items.end());
    if (impl_->items.size() == oldSize) {
        return false;
    }
    impl_->persist();
    return true;
}

std::vector<ScheduledBackup> QtScheduler::schedules() const
{
    return impl_->items;
}

void QtScheduler::setTriggerCallback(ScheduledBackupCallback callback)
{
    impl_->callback = std::move(callback);
}

void QtScheduler::start()
{
    impl_->timer.start();
}

void QtScheduler::stop()
{
    impl_->timer.stop();
}

} // namespace backup
