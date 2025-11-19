#include <QApplication>
#include "monitorclientwidget.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    MonitorClientWidget w;
    w.resize(2000, 1200);
    w.show();
    return a.exec();
}
