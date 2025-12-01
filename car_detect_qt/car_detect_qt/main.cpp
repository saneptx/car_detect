#include <QApplication>
#include <QSurfaceFormat>
#include "monitorclientwidget.h"

int main(int argc, char *argv[])
{
//    // 尝试切换到桌面 OpenGL，有时能解决驱动兼容性问题
//    QSurfaceFormat format;
//    format.setVersion(3, 3); // 尝试更高的版本，例如 3.3
//    format.setProfile(QSurfaceFormat::CoreProfile);
//    QSurfaceFormat::setDefaultFormat(format);
//    // 或者在 QApplication::set... 之前设置
//    QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
//    QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
    QApplication a(argc, argv);
    MonitorClientWidget w;
    w.resize(2000, 1200);
    w.show();
    return a.exec();
}
