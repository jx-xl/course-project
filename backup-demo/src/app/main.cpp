#include "backup/gui/MainWindow.h"

#include <QApplication>
#include <QFile>
#include <QStyleFactory>
#include <QSettings>

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("文件备份系统"));
    QApplication::setApplicationVersion(QStringLiteral("1.0.0"));
    QApplication::setOrganizationName(QStringLiteral("课程项目 A 组"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
        QCoreApplication::applicationDirPath() + QStringLiteral("/settings"));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QFile styleFile(QStringLiteral(":/styles/app.qss"));
    if (styleFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        application.setStyleSheet(QString::fromUtf8(styleFile.readAll()));
    }

    backup::gui::MainWindow window;
    window.show();

    return application.exec();
}
