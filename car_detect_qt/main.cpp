#include <QApplication>
#include "monitorclientwidget.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    MonitorClientWidget w;
    w.resize(700, 500);
    w.show();
    return a.exec();
}
