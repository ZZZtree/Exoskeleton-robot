#include "mainwindow.h"
#include "MotorMonitorData.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>

// 电机轴ID映射（与 ServoDemo.cpp 一致）
static const int AXIS_IDS[4] = {1, 2, 3, 4};

// 根据电机类型获取单位字符串
static const char* unitForPos(int axisType)
{
    return (axisType == MOTOR_TYPE_LINEAR) ? " mm" : " deg";
}
static const char* unitForVel(int axisType)
{
    return (axisType == MOTOR_TYPE_LINEAR) ? " mm/s" : " deg/s";
}
static const char* unitForTorque(int axisType)
{
    return (axisType == MOTOR_TYPE_LINEAR) ? " N" : " Nm";
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_shmFd(-1)
    , m_shmPtr(nullptr)
    , m_lastSequence(0)
    , m_timer(nullptr)
    , m_intervalMs(50)  // 50ms 更新一次（20Hz）
    , m_paused(false)
    , m_shmConnected(false)
{
    setWindowTitle("电机运动曲线监控 - MotorMonitor");
    resize(1400, 900);

    setupUI();
    initSharedMemory();

    // 启动定时器
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &MainWindow::onTimerTick);
    m_timer->start(m_intervalMs);
}

MainWindow::~MainWindow()
{
    if (m_timer)
    {
        m_timer->stop();
    }
    closeSharedMemory();
}

void MainWindow::setupUI()
{
    QWidget* centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    QVBoxLayout* mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setSpacing(5);
    mainLayout->setContentsMargins(5, 5, 5, 5);

    // ========== 顶部控制栏 ==========
    QHBoxLayout* controlLayout = new QHBoxLayout();

    m_btnPause = new QPushButton("暂停", this);
    m_btnPause->setFixedWidth(80);
    m_btnPause->setStyleSheet(
        "QPushButton { background-color: #3498DB; color: white; "
        "border-radius: 4px; padding: 6px; font-weight: bold; }"
        "QPushButton:hover { background-color: #2980B9; }");
    connect(m_btnPause, &QPushButton::clicked, this, &MainWindow::onTogglePause);

    m_btnClear = new QPushButton("清除数据", this);
    m_btnClear->setFixedWidth(100);
    m_btnClear->setStyleSheet(
        "QPushButton { background-color: #E67E22; color: white; "
        "border-radius: 4px; padding: 6px; font-weight: bold; }"
        "QPushButton:hover { background-color: #D35400; }");
    connect(m_btnClear, &QPushButton::clicked, this, &MainWindow::onClearData);

    m_btnAutoAdjust = new QPushButton("自动调整Y轴", this);
    m_btnAutoAdjust->setFixedWidth(120);
    m_btnAutoAdjust->setStyleSheet(
        "QPushButton { background-color: #27AE60; color: white; "
        "border-radius: 4px; padding: 6px; font-weight: bold; }"
        "QPushButton:hover { background-color: #229954; }");
    connect(m_btnAutoAdjust, &QPushButton::clicked, this, &MainWindow::onAutoAdjust);

    m_labelStatus = new QLabel("状态: 未连接", this);
    m_labelStatus->setStyleSheet("QLabel { color: #E74C3C; font-weight: bold; padding: 6px; }");

    controlLayout->addWidget(m_btnPause);
    controlLayout->addWidget(m_btnClear);
    controlLayout->addWidget(m_btnAutoAdjust);
    controlLayout->addWidget(m_labelStatus);
    controlLayout->addStretch();

    mainLayout->addLayout(controlLayout);

    // ========== 实时数值显示 ==========
    QGroupBox* valueGroup = new QGroupBox("实时数值", this);
    QGridLayout* valueLayout = new QGridLayout(valueGroup);

    // 表头（不标注具体单位，因为旋转电机和线性电机单位不同）
    valueLayout->addWidget(new QLabel("电机", this), 0, 0);
    valueLayout->addWidget(new QLabel("位置", this), 0, 1);
    valueLayout->addWidget(new QLabel("速度", this), 0, 2);
    valueLayout->addWidget(new QLabel("力矩", this), 0, 3);

    static const QColor labelColors[4] = {
        QColor("#E74C3C"), QColor("#2ECC71"), QColor("#3498DB"), QColor("#9B59B6")
    };

    for (int i = 0; i < 4; i++)
    {
        QLabel* nameLabel = new QLabel(
            QString("<span style='color:%1; font-weight:bold;'>轴%2</span>")
                .arg(labelColors[i].name()).arg(AXIS_IDS[i]), this);

        m_labelPosition[i] = new QLabel("--", this);
        m_labelPosition[i]->setStyleSheet("font-family: monospace; font-size: 12pt;");
        m_labelPosition[i]->setMinimumWidth(120);

        m_labelVelocity[i] = new QLabel("--", this);
        m_labelVelocity[i]->setStyleSheet("font-family: monospace; font-size: 12pt;");
        m_labelVelocity[i]->setMinimumWidth(120);

        m_labelTorque[i] = new QLabel("--", this);
        m_labelTorque[i]->setStyleSheet("font-family: monospace; font-size: 12pt;");
        m_labelTorque[i]->setMinimumWidth(120);

        valueLayout->addWidget(nameLabel, i + 1, 0);
        valueLayout->addWidget(m_labelPosition[i], i + 1, 1);
        valueLayout->addWidget(m_labelVelocity[i], i + 1, 2);
        valueLayout->addWidget(m_labelTorque[i], i + 1, 3);
    }

    mainLayout->addWidget(valueGroup);

    // ========== 曲线图表（使用 TabWidget 切换） ==========
    m_tabWidget = new QTabWidget(this);

    // 参数：左轴单位（旋转电机），右轴单位（线性电机）
    // 位置：deg / mm，速度：deg/s / mm/s，力矩：Nm（轴4力矩显示有问题，暂时去掉）
    m_positionChart = new ChartWidget("位置曲线", "位置", "deg", "mm", 1000, this);
    m_velocityChart = new ChartWidget("速度曲线", "速度", "deg/s", "mm/s", 1000, this);
    m_torqueChart = new ChartWidget("力矩曲线", "力矩", "Nm", "", 1000, this);

    m_tabWidget->addTab(m_positionChart, "位置");
    m_tabWidget->addTab(m_velocityChart, "速度");
    m_tabWidget->addTab(m_torqueChart, "力矩");

    mainLayout->addWidget(m_tabWidget, 1);  // stretch factor = 1
}

void MainWindow::initSharedMemory()
{
    // 打开共享内存
    m_shmFd = shm_open(MOTOR_SHM_NAME, O_RDONLY, 0666);
    if (m_shmFd < 0)
    {
        m_labelStatus->setText("状态: 未连接 (共享内存不存在，请先启动 ServoDemo)");
        m_labelStatus->setStyleSheet("QLabel { color: #E74C3C; font-weight: bold; padding: 6px; }");
        m_shmConnected = false;
        return;
    }

    // 映射到内存
    m_shmPtr = mmap(NULL, MOTOR_SHM_SIZE, PROT_READ, MAP_SHARED, m_shmFd, 0);
    if (m_shmPtr == MAP_FAILED)
    {
        ::close(m_shmFd);
        m_shmFd = -1;
        m_shmPtr = nullptr;
        m_labelStatus->setText("状态: 共享内存映射失败");
        m_labelStatus->setStyleSheet("QLabel { color: #E74C3C; font-weight: bold; padding: 6px; }");
        m_shmConnected = false;
        return;
    }

    m_shmConnected = true;
    m_labelStatus->setText("状态: 已连接");
    m_labelStatus->setStyleSheet("QLabel { color: #27AE60; font-weight: bold; padding: 6px; }");
}

void MainWindow::closeSharedMemory()
{
    if (m_shmPtr)
    {
        munmap(m_shmPtr, MOTOR_SHM_SIZE);
        m_shmPtr = nullptr;
    }
    if (m_shmFd >= 0)
    {
        ::close(m_shmFd);
        m_shmFd = -1;
    }
}

void MainWindow::onTimerTick()
{
    if (m_paused || !m_shmConnected || !m_shmPtr)
    {
        return;
    }

    // 读取共享内存数据
    MotorMonitorData data;
    memcpy(&data, m_shmPtr, sizeof(MotorMonitorData));

    // 检查数据是否更新
    if (data.sequence == m_lastSequence)
    {
        return;  // 数据未更新，跳过
    }
    m_lastSequence = data.sequence;

    // 将时间戳从微秒转换为秒
    double timestampSec = data.timestamp_us / 1000000.0;

    // 更新实时数值显示（根据电机类型显示不同单位）
    for (int i = 0; i < 4; i++)
    {
        int type = data.axis_type[i];
        m_labelPosition[i]->setText(
            QString("%1%2").arg(data.position_deg[i], 0, 'f', 2).arg(unitForPos(type)));
        m_labelVelocity[i]->setText(
            QString("%1%2").arg(data.velocity_deg_per_s[i], 0, 'f', 2).arg(unitForVel(type)));
        // 轴4力矩显示有问题，暂时显示 "--"
        if (i == 3)
            m_labelTorque[i]->setText("--");
        else
            m_labelTorque[i]->setText(
                QString("%1%2").arg(data.torque_nm[i], 0, 'f', 2).arg(unitForTorque(type)));
    }

    // 更新曲线（横轴为采样点索引）
    for (int i = 0; i < 4; i++)
    {
        int axisType = data.axis_type[i];
        m_positionChart->addDataPoint(i, data.position_deg[i], axisType);
        m_velocityChart->addDataPoint(i, data.velocity_deg_per_s[i], axisType);
        // 轴4力矩显示有问题，不添加到力矩图表
        if (i < 3)
            m_torqueChart->addDataPoint(i, data.torque_nm[i], axisType);
    }

    // 自动调整Y轴量程，使曲线始终在可视范围内
    // 每次更新都调整，确保小数值时也能清晰显示
    m_positionChart->autoAdjustYAxis();
    m_velocityChart->autoAdjustYAxis();
    m_torqueChart->autoAdjustYAxis();

    // 更新状态栏信息
    statusBar()->showMessage(
        QString("序列号: %1 | 时间戳: %2 us | 控制周期: %3 us")
            .arg(data.sequence)
            .arg(data.timestamp_us)
            .arg(data.control_cycle_us));
}

void MainWindow::onClearData()
{
    m_positionChart->clearData();
    m_velocityChart->clearData();
    m_torqueChart->clearData();
    m_lastSequence = 0;
}

void MainWindow::onTogglePause()
{
    m_paused = !m_paused;
    m_btnPause->setText(m_paused ? "继续" : "暂停");
    if (m_paused)
    {
        m_btnPause->setStyleSheet(
            "QPushButton { background-color: #E74C3C; color: white; "
            "border-radius: 4px; padding: 6px; font-weight: bold; }"
            "QPushButton:hover { background-color: #C0392B; }");
    }
    else
    {
        m_btnPause->setStyleSheet(
            "QPushButton { background-color: #3498DB; color: white; "
            "border-radius: 4px; padding: 6px; font-weight: bold; }"
            "QPushButton:hover { background-color: #2980B9; }");
    }
}

void MainWindow::onAutoAdjust()
{
    m_positionChart->autoAdjustYAxis();
    m_velocityChart->autoAdjustYAxis();
    m_torqueChart->autoAdjustYAxis();
}
