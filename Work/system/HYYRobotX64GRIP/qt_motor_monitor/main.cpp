#include <QApplication>
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("MotorMonitor");
    app.setApplicationVersion("1.0.0");

    // 设置全局样式
    app.setStyleSheet(
        "QMainWindow { background-color: #2C3E50; }"
        "QWidget { color: #ECF0F1; font-size: 10pt; }"
        "QGroupBox { "
        "  border: 1px solid #7F8C8D; "
        "  border-radius: 4px; "
        "  margin-top: 8px; "
        "  padding-top: 12px; "
        "  font-weight: bold; "
        "}"
        "QGroupBox::title { "
        "  subcontrol-origin: margin; "
        "  left: 10px; "
        "  padding: 0 5px; "
        "}"
        "QTabWidget::pane { "
        "  border: 1px solid #7F8C8D; "
        "  border-radius: 4px; "
        "}"
        "QTabBar::tab { "
        "  background-color: #34495E; "
        "  color: #BDC3C7; "
        "  padding: 8px 16px; "
        "  border: 1px solid #7F8C8D; "
        "  border-bottom: none; "
        "  border-top-left-radius: 4px; "
        "  border-top-right-radius: 4px; "
        "}"
        "QTabBar::tab:selected { "
        "  background-color: #2C3E50; "
        "  color: #ECF0F1; "
        "  border-bottom: 1px solid #2C3E50; "
        "}"
        "QStatusBar { "
        "  background-color: #34495E; "
        "  color: #BDC3C7; "
        "}"
        "QTableWidget { "
        "  background-color: #34495E; "
        "  gridline-color: #7F8C8D; "
        "}"
    );

    MainWindow window;
    window.show();

    return app.exec();
}
