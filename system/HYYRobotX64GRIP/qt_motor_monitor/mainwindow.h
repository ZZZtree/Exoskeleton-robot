#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QGridLayout>
#include <QCheckBox>
#include "chartwidget.h"

/**
 * @brief 主窗口 - 四个电机运动曲线监控界面
 *
 * 通过共享内存读取 ServoDemo 发布的电机数据，
 * 实时绘制位置、速度、力矩曲线。
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private slots:
    /**
     * @brief 定时器回调：从共享内存读取数据并更新曲线
     */
    void onTimerTick();

    /**
     * @brief 清除所有曲线数据
     */
    void onClearData();

    /**
     * @brief 暂停/继续更新
     */
    void onTogglePause();

    /**
     * @brief 自动调整Y轴范围
     */
    void onAutoAdjust();

private:
    void setupUI();
    void initSharedMemory();
    void closeSharedMemory();

    // 共享内存相关
    int m_shmFd;
    void* m_shmPtr;
    uint64_t m_lastSequence;

    // UI 组件
    QTabWidget* m_tabWidget;
    ChartWidget* m_positionChart;
    ChartWidget* m_velocityChart;
    ChartWidget* m_torqueChart;

    // 控制按钮
    QPushButton* m_btnPause;
    QPushButton* m_btnClear;
    QPushButton* m_btnAutoAdjust;

    // 状态显示
    QLabel* m_labelStatus;
    QLabel* m_labelPosition[4];
    QLabel* m_labelVelocity[4];
    QLabel* m_labelTorque[4];

    // 定时器
    QTimer* m_timer;
    int m_intervalMs;

    // 状态
    bool m_paused;
    bool m_shmConnected;
};

#endif // MAINWINDOW_H
