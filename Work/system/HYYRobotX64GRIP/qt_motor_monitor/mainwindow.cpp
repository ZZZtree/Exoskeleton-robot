#include "mainwindow.h"
#include "MotorMonitorData.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <cmath>

// 电机轴ID映射
static const int AXIS_IDS[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

// 电机中文名称
static const QString AXIS_NAMES[10] = {
    "轴1(右踝)", "轴2(右小腿)", "轴3(右膝)", "轴4(右髋)", "轴5(左髋)",
    "轴6(左大腿)", "轴7(左膝)", "轴8(左小腿)", "轴9(左踝)", "轴10(右大腿)"
};

// 电机类型描述
static const QString AXIS_TYPE_STR[10] = {
    "旋转", "线性", "旋转", "旋转", "旋转",
    "线性", "旋转", "线性", "旋转", "线性"
};

// 电机颜色
static const QColor AXIS_COLORS[10] = {
    QColor("#E74C3C"),  // 红色   - 轴1
    QColor("#E67E22"),  // 橙色   - 轴2
    QColor("#F1C40F"),  // 黄色   - 轴3
    QColor("#2ECC71"),  // 绿色   - 轴4
    QColor("#1ABC9C"),  // 青色   - 轴5
    QColor("#3498DB"),  // 蓝色   - 轴6
    QColor("#9B59B6"),  // 紫色   - 轴7
    QColor("#E91E63"),  // 粉色   - 轴8
    QColor("#795548"),  // 棕色   - 轴9
    QColor("#607D8B")   // 灰色   - 轴10
};

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
    , m_intervalMs(50)
    , m_currentAxis(0)
    , m_paused(false)
    , m_shmConnected(false)
    , m_hasCachedData(false)
    , m_cachedSeqGroupCount(0)
    , m_cachedSeqCurrentGroup(0)
    , m_cachedSeqStatus(0)
    , m_udpSender(nullptr)
    , m_udpGroup(nullptr)
    , m_udpHostInput(nullptr)
    , m_udpPortInput(nullptr)
    , m_udpReceiver(nullptr)
    , m_btnUdpStart(nullptr)
    , m_btnUdpStop(nullptr)
    , m_labelUdpStatus(nullptr)
    , m_labelIMU_d10(nullptr)
    , m_labelIMU_d20(nullptr)
    , m_labelIMU_d30(nullptr)
    , m_labelIMU_d40(nullptr)
    , m_btnResponseMode(nullptr)
    , m_responseMode(false)
    , m_hipRefPos(0.0)
    , m_kneeRefPos(0.0)
    , m_bufIdx(0)
    , m_lastTargetHip(0.0)
    , m_lastTargetKnee(0.0)
{
    memset(m_cachedPos, 0, sizeof(m_cachedPos));
    memset(m_cachedVel, 0, sizeof(m_cachedVel));
    memset(m_cachedTor, 0, sizeof(m_cachedTor));
    memset(m_cachedAxisType, 0, sizeof(m_cachedAxisType));

    setWindowTitle("电机运动曲线监控 - MotorMonitor (10轴切换 + 控制)");
    resize(1500, 950);

    setupUI();
    initSharedMemory();

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &MainWindow::onTimerTick);
    m_timer->start(m_intervalMs);
}

MainWindow::~MainWindow()
{
    if (m_timer) m_timer->stop();
    closeSharedMemory();
}

void MainWindow::setupUI()
{
    QWidget* centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    QHBoxLayout* mainLayout = new QHBoxLayout(centralWidget);
    mainLayout->setSpacing(6);
    mainLayout->setContentsMargins(6, 6, 6, 6);

    // ========== 左侧：多轴控制面板（电机选择 + 目标位置设置） ==========
    QWidget* leftPanel = new QWidget(this);
    leftPanel->setFixedWidth(340);
    QVBoxLayout* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setSpacing(0);
    leftLayout->setContentsMargins(0, 0, 0, 0);

    // 标题
    QLabel* panelTitle = new QLabel("<b>多轴位置控制</b>", this);
    panelTitle->setStyleSheet("font-size: 12pt; color: #ECF0F1; padding: 4px;");
    leftLayout->addWidget(panelTitle);

    // 表头
    QHBoxLayout* headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(4);
    QLabel* hdrBtn = new QLabel("选择", this);
    hdrBtn->setFixedWidth(30);
    hdrBtn->setStyleSheet("color: #ECF0F1; font-weight: bold; font-size: 9pt;");
    QLabel* hdrName = new QLabel("轴名称", this);
    hdrName->setFixedWidth(110);
    hdrName->setStyleSheet("color: #ECF0F1; font-weight: bold; font-size: 9pt;");
    QLabel* hdrTarget = new QLabel("目标位置", this);
    hdrTarget->setFixedWidth(90);
    hdrTarget->setStyleSheet("color: #ECF0F1; font-weight: bold; font-size: 9pt;");
    QLabel* hdrStatus = new QLabel("状态", this);
    hdrStatus->setStyleSheet("color: #ECF0F1; font-weight: bold; font-size: 9pt;");
    headerLayout->addWidget(hdrBtn);
    headerLayout->addWidget(hdrName);
    headerLayout->addWidget(hdrTarget);
    headerLayout->addWidget(hdrStatus);
    headerLayout->addStretch();
    leftLayout->addLayout(headerLayout);

    m_axisButtonGroup = new QButtonGroup(this);
    m_axisButtonGroup->setExclusive(true);

    // 10个电机行：勾选框 + 按钮(可点击切换) + 目标位置输入 + 状态
    // 使用 QGridLayout 让10行均匀分布
    QGridLayout* gridLayout = new QGridLayout();
    gridLayout->setSpacing(2);
    gridLayout->setContentsMargins(2, 2, 2, 2);

    for (int i = 0; i < 10; i++)
    {
        // 多轴勾选框
        m_multiActive[i] = new QCheckBox(this);
        m_multiActive[i]->setFixedWidth(25);
        m_multiActive[i]->setStyleSheet(
            "QCheckBox { color: #ECF0F1; }"
            "QCheckBox::indicator { width: 14px; height: 14px; }");

        // 电机选择按钮（点击切换当前电机，同时作为轴名称显示）
        m_axisButtons[i] = new QPushButton(
            QString("%1").arg(AXIS_NAMES[i]), this);
        m_axisButtons[i]->setCheckable(true);
        m_axisButtons[i]->setMinimumHeight(24);
        m_axisButtons[i]->setStyleSheet(
            QString("QPushButton { "
                    "  background-color: %1; color: white; "
                    "  border-radius: 3px; padding: 1px; font-weight: bold; "
                    "  font-size: 8pt; text-align: left; padding-left: 6px; "
                    "}"
                    "QPushButton:hover { opacity: 0.8; }"
                    "QPushButton:checked { "
                    "  border: 3px solid white; "
                    "}")
                .arg(AXIS_COLORS[i].name()));
        m_axisButtonGroup->addButton(m_axisButtons[i], i);

        // 目标位置输入
        m_multiTargetPos[i] = new QDoubleSpinBox(this);
        m_multiTargetPos[i]->setRange(-99999.0, 99999.0);
        m_multiTargetPos[i]->setDecimals(1);
        m_multiTargetPos[i]->setSingleStep(1.0);
        m_multiTargetPos[i]->setValue(0.0);
        m_multiTargetPos[i]->setFixedWidth(85);
        m_multiTargetPos[i]->setStyleSheet(
            "QDoubleSpinBox { background-color: #2C3E50; color: #ECF0F1; "
            "border: 1px solid #7F8C8D; border-radius: 3px; padding: 1px; "
            "font-size: 8pt; font-family: monospace; }");

        // 状态标签
        m_multiAxisStatusLabel[i] = new QLabel("--", this);
        m_multiAxisStatusLabel[i]->setStyleSheet("color: #7F8C8D; font-size: 8pt;");
        m_multiAxisStatusLabel[i]->setMinimumWidth(50);

        // 第0列：勾选框，第1列：按钮，第2列：目标位置，第3列：状态
        gridLayout->addWidget(m_multiActive[i], i, 0, Qt::AlignVCenter);
        gridLayout->addWidget(m_axisButtons[i], i, 1);
        gridLayout->addWidget(m_multiTargetPos[i], i, 2, Qt::AlignVCenter);
        gridLayout->addWidget(m_multiAxisStatusLabel[i], i, 3, Qt::AlignVCenter);

        // 设置行拉伸因子，让10行均匀分布
        gridLayout->setRowStretch(i, 1);
    }
    // 第1列（按钮列）拉伸
    gridLayout->setColumnStretch(1, 1);

    leftLayout->addLayout(gridLayout, 1);

    // ========== 预设姿势快捷按钮 ==========
    QLabel* presetTitle = new QLabel("<b>预设姿势</b>", this);
    presetTitle->setStyleSheet("font-size: 10pt; color: #ECF0F1; padding: 4px; margin-top: 4px;");
    leftLayout->addWidget(presetTitle);

    QGridLayout* presetGrid = new QGridLayout();
    presetGrid->setSpacing(3);

    // 共4个按钮，2行2列
    m_btnPreset[0] = new QPushButton("预设1", this);
    m_btnPreset[1] = new QPushButton("预设2", this);
    m_btnPreset[2] = new QPushButton("预设3", this);
    m_btnPreset[3] = new QPushButton("预设4", this);

    QString presetBtnStyle =
        "QPushButton { background-color: #8E44AD; color: white; "
        "border-radius: 4px; padding: 6px; font-weight: bold; font-size: 9pt; }"
        "QPushButton:hover { background-color: #7D3C98; }";

    for (int i = 0; i < 4; i++)
    {
        m_btnPreset[i]->setFixedHeight(32);
        m_btnPreset[i]->setStyleSheet(presetBtnStyle);
        presetGrid->addWidget(m_btnPreset[i], i / 2, i % 2);
    }

    connect(m_btnPreset[0], &QPushButton::clicked, this, &MainWindow::onPreset1);
    connect(m_btnPreset[1], &QPushButton::clicked, this, &MainWindow::onPreset2);
    connect(m_btnPreset[2], &QPushButton::clicked, this, &MainWindow::onPreset3);
    connect(m_btnPreset[3], &QPushButton::clicked, this, &MainWindow::onPreset4);

    leftLayout->addLayout(presetGrid);

    // 多轴运行按钮 + 使能/失能
    QHBoxLayout* actionLayout = new QHBoxLayout();
    actionLayout->setSpacing(4);

    m_btnMultiRun = new QPushButton("🚀 多轴运行", this);
    m_btnMultiRun->setFixedHeight(34);
    m_btnMultiRun->setStyleSheet(
        "QPushButton { background-color: #E74C3C; color: white; "
        "border-radius: 5px; padding: 4px; font-weight: bold; font-size: 10pt; }"
        "QPushButton:hover { background-color: #C0392B; }"
        "QPushButton:pressed { background-color: #A93226; }");
    connect(m_btnMultiRun, &QPushButton::clicked, this, &MainWindow::onMultiAxisRun);

    m_btnEnable = new QPushButton("使能", this);
    m_btnEnable->setFixedHeight(34);
    m_btnEnable->setStyleSheet(
        "QPushButton { background-color: #27AE60; color: white; "
        "border-radius: 4px; padding: 4px; font-weight: bold; font-size: 9pt; }"
        "QPushButton:hover { background-color: #229954; }");
    connect(m_btnEnable, &QPushButton::clicked, this, &MainWindow::onEnable);

    m_btnDisable = new QPushButton("失能", this);
    m_btnDisable->setFixedHeight(34);
    m_btnDisable->setStyleSheet(
        "QPushButton { background-color: #95A5A6; color: white; "
        "border-radius: 4px; padding: 4px; font-weight: bold; font-size: 9pt; }"
        "QPushButton:hover { background-color: #7F8C8D; }");
    connect(m_btnDisable, &QPushButton::clicked, this, &MainWindow::onDisable);

    actionLayout->addWidget(m_btnMultiRun, 2);
    actionLayout->addWidget(m_btnEnable, 1);
    actionLayout->addWidget(m_btnDisable, 1);
    leftLayout->addLayout(actionLayout);

    // 多轴状态显示
    m_labelMultiStatus = new QLabel("就绪", this);
    m_labelMultiStatus->setStyleSheet(
        "QLabel { color: #7F8C8D; font-weight: bold; padding: 2px; "
        "font-size: 9pt; }");
    leftLayout->addWidget(m_labelMultiStatus);

#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    connect(m_axisButtonGroup, &QButtonGroup::idClicked,
            this, &MainWindow::onAxisButtonClicked);
#else
    connect(m_axisButtonGroup,
            static_cast<void (QButtonGroup::*)(int)>(&QButtonGroup::buttonClicked),
            this, &MainWindow::onAxisButtonClicked);
#endif

    mainLayout->addWidget(leftPanel);

    // ========== 右侧：主显示区域 ==========
    QWidget* rightPanel = new QWidget(this);
    QVBoxLayout* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setSpacing(4);
    rightLayout->setContentsMargins(0, 0, 0, 0);

    // ========== 顶部控制栏 ==========
    QHBoxLayout* controlLayout = new QHBoxLayout();
    controlLayout->setSpacing(6);

    m_btnPause = new QPushButton("暂停", this);
    m_btnPause->setFixedWidth(70);
    m_btnPause->setStyleSheet(
        "QPushButton { background-color: #3498DB; color: white; "
        "border-radius: 4px; padding: 5px; font-weight: bold; }"
        "QPushButton:hover { background-color: #2980B9; }");
    connect(m_btnPause, &QPushButton::clicked, this, &MainWindow::onTogglePause);

    m_btnClear = new QPushButton("清除数据", this);
    m_btnClear->setFixedWidth(90);
    m_btnClear->setStyleSheet(
        "QPushButton { background-color: #E67E22; color: white; "
        "border-radius: 4px; padding: 5px; font-weight: bold; }"
        "QPushButton:hover { background-color: #D35400; }");
    connect(m_btnClear, &QPushButton::clicked, this, &MainWindow::onClearData);

    m_btnAutoAdjust = new QPushButton("自动调整Y轴", this);
    m_btnAutoAdjust->setFixedWidth(110);
    m_btnAutoAdjust->setStyleSheet(
        "QPushButton { background-color: #27AE60; color: white; "
        "border-radius: 4px; padding: 5px; font-weight: bold; }"
        "QPushButton:hover { background-color: #229954; }");
    connect(m_btnAutoAdjust, &QPushButton::clicked, this, &MainWindow::onAutoAdjust);

    m_labelStatus = new QLabel("状态: 未连接", this);
    m_labelStatus->setStyleSheet("QLabel { color: #E74C3C; font-weight: bold; padding: 4px; }");

    controlLayout->addWidget(m_btnPause);
    controlLayout->addWidget(m_btnClear);
    controlLayout->addWidget(m_btnAutoAdjust);
    controlLayout->addWidget(m_labelStatus);
    controlLayout->addStretch();

    rightLayout->addLayout(controlLayout);

    // ========== 当前电机信息显示 ==========
    QGroupBox* infoGroup = new QGroupBox("当前电机信息", this);
    QHBoxLayout* infoLayout = new QHBoxLayout(infoGroup);
    infoLayout->setSpacing(8);

    m_labelAxisInfo = new QLabel("当前: 轴1(右踝) | 类型: 旋转", this);
    m_labelAxisInfo->setStyleSheet("font-size: 10pt; font-weight: bold; color: #ECF0F1;");
    m_labelAxisInfo->setMinimumWidth(220);

    m_labelPosition = new QLabel("位置: --", this);
    m_labelPosition->setStyleSheet("font-family: monospace; font-size: 10pt; color: #E74C3C;");
    m_labelPosition->setMinimumWidth(130);

    m_labelVelocity = new QLabel("速度: --", this);
    m_labelVelocity->setStyleSheet("font-family: monospace; font-size: 10pt; color: #3498DB;");
    m_labelVelocity->setMinimumWidth(130);

    m_labelTorque = new QLabel("力矩: --", this);
    m_labelTorque->setStyleSheet("font-family: monospace; font-size: 10pt; color: #2ECC71;");
    m_labelTorque->setMinimumWidth(130);

    infoLayout->addWidget(m_labelAxisInfo);
    infoLayout->addWidget(m_labelPosition);
    infoLayout->addWidget(m_labelVelocity);
    infoLayout->addWidget(m_labelTorque);
    infoLayout->addStretch();

    rightLayout->addWidget(infoGroup);

    // ========== 速度模式控制面板 ==========
    m_velocityGroup = new QGroupBox("速度模式（点击箭头持续运动，实时显示距离）", this);
    QHBoxLayout* velLayout = new QHBoxLayout(m_velocityGroup);
    velLayout->setSpacing(6);

    QLabel* lblVelSpeed = new QLabel("速度:", this);
    lblVelSpeed->setStyleSheet("color: #ECF0F1; font-weight: bold;");
    m_spinVelSpeed = new QDoubleSpinBox(this);
    m_spinVelSpeed->setRange(0.1, 99999.0);
    m_spinVelSpeed->setDecimals(1);
    m_spinVelSpeed->setSingleStep(5.0);
    m_spinVelSpeed->setValue(1.0);
    m_spinVelSpeed->setFixedWidth(90);
    m_spinVelSpeed->setSuffix(" deg/s");
    m_spinVelSpeed->setStyleSheet(
        "QDoubleSpinBox { background-color: #2C3E50; color: #ECF0F1; "
        "border: 1px solid #7F8C8D; border-radius: 3px; padding: 3px; "
        "font-size: 10pt; font-family: monospace; }");

    m_btnVelPos = new QPushButton("▶ +", this);
    m_btnVelPos->setFixedHeight(36);
    m_btnVelPos->setMinimumWidth(70);
    m_btnVelPos->setStyleSheet(
        "QPushButton { background-color: #27AE60; color: white; "
        "border-radius: 5px; padding: 6px; font-weight: bold; font-size: 11pt; }"
        "QPushButton:hover { background-color: #229954; }"
        "QPushButton:pressed { background-color: #1E8449; }");
    connect(m_btnVelPos, &QPushButton::clicked, this, &MainWindow::onVelocityPositive);

    m_btnVelNeg = new QPushButton("◀ -", this);
    m_btnVelNeg->setFixedHeight(36);
    m_btnVelNeg->setMinimumWidth(70);
    m_btnVelNeg->setStyleSheet(
        "QPushButton { background-color: #E74C3C; color: white; "
        "border-radius: 5px; padding: 6px; font-weight: bold; font-size: 11pt; }"
        "QPushButton:hover { background-color: #C0392B; }"
        "QPushButton:pressed { background-color: #A93226; }");
    connect(m_btnVelNeg, &QPushButton::clicked, this, &MainWindow::onVelocityNegative);

    m_btnVelStop = new QPushButton("■ 停止", this);
    m_btnVelStop->setFixedHeight(36);
    m_btnVelStop->setMinimumWidth(70);
    m_btnVelStop->setStyleSheet(
        "QPushButton { background-color: #95A5A6; color: white; "
        "border-radius: 5px; padding: 6px; font-weight: bold; font-size: 11pt; }"
        "QPushButton:hover { background-color: #7F8C8D; }");
    connect(m_btnVelStop, &QPushButton::clicked, this, &MainWindow::onVelocityStop);

    m_labelVelDistance = new QLabel("运动距离: --", this);
    m_labelVelDistance->setStyleSheet(
        "QLabel { color: #F1C40F; font-weight: bold; padding: 4px; "
        "font-size: 10pt; font-family: monospace; }");
    m_labelVelDistance->setMinimumWidth(180);

    velLayout->addWidget(lblVelSpeed);
    velLayout->addWidget(m_spinVelSpeed);
    velLayout->addWidget(m_btnVelPos);
    velLayout->addWidget(m_btnVelNeg);
    velLayout->addWidget(m_btnVelStop);
    velLayout->addWidget(m_labelVelDistance);
    velLayout->addStretch();

    rightLayout->addWidget(m_velocityGroup);

    // ========== 位置模式控制面板 ==========
    m_positionGroup = new QGroupBox("位置模式（输入目标位置，点击运行，到达后自动停止）", this);
    QHBoxLayout* posLayout = new QHBoxLayout(m_positionGroup);
    posLayout->setSpacing(6);

    QLabel* lblTarget = new QLabel("目标位置:", this);
    lblTarget->setStyleSheet("color: #ECF0F1; font-weight: bold;");
    m_spinTargetPos = new QDoubleSpinBox(this);
    m_spinTargetPos->setRange(-99999.0, 99999.0);
    m_spinTargetPos->setDecimals(2);
    m_spinTargetPos->setSingleStep(1.0);
    m_spinTargetPos->setValue(0.0);
    m_spinTargetPos->setFixedWidth(110);
    m_spinTargetPos->setSuffix(" deg");
    m_spinTargetPos->setStyleSheet(
        "QDoubleSpinBox { background-color: #2C3E50; color: #ECF0F1; "
        "border: 1px solid #7F8C8D; border-radius: 3px; padding: 3px; "
        "font-size: 10pt; font-family: monospace; }");

    QLabel* lblPosSpeed = new QLabel("速度:", this);
    lblPosSpeed->setStyleSheet("color: #ECF0F1; font-weight: bold;");
    m_spinPosSpeed = new QDoubleSpinBox(this);
    m_spinPosSpeed->setRange(0.1, 99999.0);
    m_spinPosSpeed->setDecimals(1);
    m_spinPosSpeed->setSingleStep(5.0);
    m_spinPosSpeed->setValue(1.0);
    m_spinPosSpeed->setFixedWidth(90);
    m_spinPosSpeed->setSuffix(" deg/s");
    m_spinPosSpeed->setStyleSheet(
        "QDoubleSpinBox { background-color: #2C3E50; color: #ECF0F1; "
        "border: 1px solid #7F8C8D; border-radius: 3px; padding: 3px; "
        "font-size: 10pt; font-family: monospace; }");

    m_btnPosRun = new QPushButton("运行", this);
    m_btnPosRun->setFixedHeight(36);
    m_btnPosRun->setMinimumWidth(70);
    m_btnPosRun->setStyleSheet(
        "QPushButton { background-color: #2980B9; color: white; "
        "border-radius: 5px; padding: 6px; font-weight: bold; font-size: 11pt; }"
        "QPushButton:hover { background-color: #1F6F9F; }");
    connect(m_btnPosRun, &QPushButton::clicked, this, &MainWindow::onPositionModeRun);

    m_labelPosTarget = new QLabel("目标: --", this);
    m_labelPosTarget->setStyleSheet(
        "QLabel { color: #3498DB; font-weight: bold; padding: 4px; "
        "font-size: 10pt; font-family: monospace; }");
    m_labelPosTarget->setMinimumWidth(130);

    m_labelPosRemain = new QLabel("剩余: --", this);
    m_labelPosRemain->setStyleSheet(
        "QLabel { color: #E67E22; font-weight: bold; padding: 4px; "
        "font-size: 10pt; font-family: monospace; }");
    m_labelPosRemain->setMinimumWidth(130);

    posLayout->addWidget(lblTarget);
    posLayout->addWidget(m_spinTargetPos);
    posLayout->addWidget(lblPosSpeed);
    posLayout->addWidget(m_spinPosSpeed);
    posLayout->addWidget(m_btnPosRun);
    posLayout->addWidget(m_labelPosTarget);
    posLayout->addWidget(m_labelPosRemain);
    posLayout->addStretch();

    rightLayout->addWidget(m_positionGroup);

    // ========== 命令状态 ==========
    QHBoxLayout* cmdStatusLayout = new QHBoxLayout();

    m_labelCmdStatus = new QLabel("命令状态: 空闲", this);
    m_labelCmdStatus->setStyleSheet(
        "QLabel { color: #7F8C8D; font-weight: bold; padding: 4px; "
        "font-size: 10pt; }");

    cmdStatusLayout->addWidget(m_labelCmdStatus);
    cmdStatusLayout->addStretch();

    rightLayout->addLayout(cmdStatusLayout);

    // ========== 动作序列队列面板 ==========
    m_seqGroup = new QGroupBox("动作序列队列（填好左侧目标位置 → 存入队列 → 循环执行）", this);
    QVBoxLayout* seqMainLayout = new QVBoxLayout(m_seqGroup);
    seqMainLayout->setSpacing(4);

    // 队列操作按钮行
    QHBoxLayout* seqBtnLayout = new QHBoxLayout();
    seqBtnLayout->setSpacing(6);

    m_btnSeqAdd = new QPushButton("＋ 存入队列", this);
    m_btnSeqAdd->setFixedHeight(30);
    m_btnSeqAdd->setStyleSheet(
        "QPushButton { background-color: #2980B9; color: white; "
        "border-radius: 4px; padding: 4px 8px; font-weight: bold; font-size: 9pt; }"
        "QPushButton:hover { background-color: #1F6F9F; }");
    connect(m_btnSeqAdd, &QPushButton::clicked, this, &MainWindow::onSeqAddGroup);

    m_btnSeqDelete = new QPushButton("✕ 删除选中", this);
    m_btnSeqDelete->setFixedHeight(30);
    m_btnSeqDelete->setStyleSheet(
        "QPushButton { background-color: #7F8C8D; color: white; "
        "border-radius: 4px; padding: 4px 8px; font-weight: bold; font-size: 9pt; }"
        "QPushButton:hover { background-color: #6C7A89; }");
    connect(m_btnSeqDelete, &QPushButton::clicked, this, &MainWindow::onSeqDeleteGroup);

    m_btnSeqClear = new QPushButton("清空队列", this);
    m_btnSeqClear->setFixedHeight(30);
    m_btnSeqClear->setStyleSheet(
        "QPushButton { background-color: #E74C3C; color: white; "
        "border-radius: 4px; padding: 4px 8px; font-weight: bold; font-size: 9pt; }"
        "QPushButton:hover { background-color: #C0392B; }");
    connect(m_btnSeqClear, &QPushButton::clicked, this, &MainWindow::onSeqClear);

    seqBtnLayout->addWidget(m_btnSeqAdd);
    seqBtnLayout->addWidget(m_btnSeqDelete);
    seqBtnLayout->addWidget(m_btnSeqClear);
    seqBtnLayout->addStretch();
    seqMainLayout->addLayout(seqBtnLayout);

    // 队列列表
    m_seqListWidget = new QListWidget(this);
    m_seqListWidget->setFixedHeight(100);
    m_seqListWidget->setStyleSheet(
        "QListWidget { background-color: #1B2631; color: #ECF0F1; "
        "border: 1px solid #7F8C8D; border-radius: 3px; font-size: 8pt; font-family: monospace; }"
        "QListWidget::item:selected { background-color: #2980B9; }");
    seqMainLayout->addWidget(m_seqListWidget);

    // 循环勾选 + 启停按钮
    QHBoxLayout* seqCtrlLayout = new QHBoxLayout();
    seqCtrlLayout->setSpacing(6);

    m_seqLoopingCheck = new QCheckBox("循环执行", this);
    m_seqLoopingCheck->setStyleSheet("QCheckBox { color: #ECF0F1; font-weight: bold; font-size: 9pt; }");
    m_seqLoopingCheck->setChecked(true);
    connect(m_seqLoopingCheck, &QCheckBox::toggled, this, &MainWindow::onSeqLoopingToggled);

    m_btnSeqStart = new QPushButton("▶ 开始队列", this);
    m_btnSeqStart->setFixedHeight(30);
    m_btnSeqStart->setMinimumWidth(100);
    m_btnSeqStart->setStyleSheet(
        "QPushButton { background-color: #27AE60; color: white; "
        "border-radius: 4px; padding: 4px 8px; font-weight: bold; font-size: 9pt; }"
        "QPushButton:hover { background-color: #229954; }");
    connect(m_btnSeqStart, &QPushButton::clicked, this, &MainWindow::onSeqStart);

    m_btnSeqStop = new QPushButton("■ 停止", this);
    m_btnSeqStop->setFixedHeight(30);
    m_btnSeqStop->setMinimumWidth(80);
    m_btnSeqStop->setStyleSheet(
        "QPushButton { background-color: #E67E22; color: white; "
        "border-radius: 4px; padding: 4px 8px; font-weight: bold; font-size: 9pt; }"
        "QPushButton:hover { background-color: #D35400; }");
    connect(m_btnSeqStop, &QPushButton::clicked, this, &MainWindow::onSeqStop);

    seqCtrlLayout->addWidget(m_seqLoopingCheck);
    seqCtrlLayout->addStretch();
    seqCtrlLayout->addWidget(m_btnSeqStart);
    seqCtrlLayout->addWidget(m_btnSeqStop);
    seqMainLayout->addLayout(seqCtrlLayout);

    // 序列状态
    m_labelSeqStatus = new QLabel("队列为空", this);
    m_labelSeqStatus->setStyleSheet(
        "QLabel { color: #7F8C8D; font-weight: bold; padding: 2px; "
        "font-size: 9pt; }");
    seqMainLayout->addWidget(m_labelSeqStatus);

    rightLayout->addWidget(m_seqGroup);

    // ========== 三个图表，用 QTabWidget 切换 ==========
    QTabWidget* tabWidget = new QTabWidget(this);

    m_chartPos = new ChartWidget("位置", "deg", "mm", 1000, this);
    m_chartVel = new ChartWidget("速度", "deg/s", "mm/s", 1000, this);
    m_chartTor = new ChartWidget("力矩", "Nm", "N", 1000, this);

    tabWidget->addTab(m_chartPos, "位置");
    tabWidget->addTab(m_chartVel, "速度");
    tabWidget->addTab(m_chartTor, "力矩");

    rightLayout->addWidget(tabWidget, 1);

    mainLayout->addWidget(rightPanel, 1);

    // 默认选中轴1
    m_axisButtons[0]->setChecked(true);
    m_chartPos->setAxis(0, MOTOR_TYPE_ROTARY);
    m_chartVel->setAxis(0, MOTOR_TYPE_ROTARY);
    m_chartTor->setAxis(0, MOTOR_TYPE_ROTARY);

    m_spinTargetPos->setSuffix(" deg");
    m_spinPosSpeed->setSuffix(" deg/s");
    m_spinVelSpeed->setSuffix(" deg/s");

    // 初始化多轴面板的单位后缀
    for (int i = 0; i < 10; i++)
    {
        int axisType = (i == 1 || i == 5 || i == 7 || i == 9) ? MOTOR_TYPE_LINEAR : MOTOR_TYPE_ROTARY;
        m_multiTargetPos[i]->setSuffix(QString(" %1").arg(unitForPos(axisType)));
    }

    // ========== UDP 遥测面板 ==========
    // 创建 UdpSender 对象
    m_udpSender = new UdpSender(this);
    m_udpSender->setTarget("172.20.15.30", 12345);

    m_udpGroup = new QGroupBox("UDP 遥测 — 右髋/右膝/右踝角度发送", this);
    QHBoxLayout* udpLayout = new QHBoxLayout(m_udpGroup);
    udpLayout->setSpacing(6);

    QLabel* udpHostLabel = new QLabel("目标IP:", this);
    udpHostLabel->setStyleSheet("color: #ECF0F1; font-weight: bold;");
    m_udpHostInput = new QLineEdit("172.20.15.30", this);
    m_udpHostInput->setFixedWidth(170);
    m_udpHostInput->setStyleSheet(
        "QLineEdit { background-color: #2C3E50; color: #ECF0F1; "
        "border: 1px solid #7F8C8D; border-radius: 3px; padding: 3px; "
        "font-size: 10pt; font-family: monospace; }");

    QLabel* udpPortLabel = new QLabel("端口:", this);
    udpPortLabel->setStyleSheet("color: #ECF0F1; font-weight: bold;");
    m_udpPortInput = new QLineEdit("12345", this);
    m_udpPortInput->setFixedWidth(70);
    m_udpPortInput->setStyleSheet(
        "QLineEdit { background-color: #2C3E50; color: #ECF0F1; "
        "border: 1px solid #7F8C8D; border-radius: 3px; padding: 3px; "
        "font-size: 10pt; font-family: monospace; }");

    m_btnUdpStart = new QPushButton("▶ 开始发送", this);
    m_btnUdpStart->setFixedHeight(30);
    m_btnUdpStart->setStyleSheet(
        "QPushButton { background-color: #27AE60; color: white; "
        "border-radius: 4px; padding: 4px 8px; font-weight: bold; font-size: 9pt; }"
        "QPushButton:hover { background-color: #229954; }");

    m_btnUdpStop = new QPushButton("■ 停止", this);
    m_btnUdpStop->setFixedHeight(30);
    m_btnUdpStop->setStyleSheet(
        "QPushButton { background-color: #E74C3C; color: white; "
        "border-radius: 4px; padding: 4px 8px; font-weight: bold; font-size: 9pt; }"
        "QPushButton:hover { background-color: #C0392B; }");

    m_labelUdpStatus = new QLabel("已就绪，点击\"开始发送\"", this);
    m_labelUdpStatus->setStyleSheet(
        "QLabel { color: #7F8C8D; font-weight: bold; font-size: 9pt; }");

    udpLayout->addWidget(udpHostLabel);
    udpLayout->addWidget(m_udpHostInput);
    udpLayout->addWidget(udpPortLabel);
    udpLayout->addWidget(m_udpPortInput);
    udpLayout->addWidget(m_btnUdpStart);
    udpLayout->addWidget(m_btnUdpStop);
    udpLayout->addWidget(m_labelUdpStatus);
    udpLayout->addStretch();

    rightLayout->addWidget(m_udpGroup);

    // 连接 UDP 启停信号
    connect(m_btnUdpStart, &QPushButton::clicked, this, [this]() {
        QString host = m_udpHostInput->text().trimmed();
        quint16 port = (quint16)m_udpPortInput->text().toUShort();
        m_udpSender->setTarget(host, port);
        m_udpSender->start(50);
        m_labelUdpStatus->setText(
            QString("正在发送 → %1:%2").arg(host).arg(port));
        m_labelUdpStatus->setStyleSheet(
            "QLabel { color: #27AE60; font-weight: bold; font-size: 9pt; }");
        m_btnUdpStart->setEnabled(false);
        m_btnUdpStop->setEnabled(true);
    });

    connect(m_btnUdpStop, &QPushButton::clicked, this, [this]() {
        m_udpSender->stop();
        m_labelUdpStatus->setText("已停止发送");
        m_labelUdpStatus->setStyleSheet(
            "QLabel { color: #E74C3C; font-weight: bold; font-size: 9pt; }");
        m_btnUdpStart->setEnabled(true);
        m_btnUdpStop->setEnabled(false);
    });

    // 初始状态: 停止按钮不可用
    m_btnUdpStop->setEnabled(false);

    // ========== IMU 数据接收显示 ==========
    QGroupBox* imuGroup = new QGroupBox("IMU 增量数据 (UDP 接收)", this);
    QHBoxLayout* imuLayout = new QHBoxLayout(imuGroup);
    imuLayout->setSpacing(10);

    QLabel* imuLabel10 = new QLabel("IMU 0x10:", this);
    imuLabel10->setStyleSheet("color: #ECF0F1; font-weight: bold;");
    m_labelIMU_d10 = new QLabel("--", this);
    m_labelIMU_d10->setStyleSheet("font-family: monospace; font-size: 10pt; color: #3498DB; font-weight: bold; min-width: 80px;");

    QLabel* imuLabel20 = new QLabel("IMU 0x20:", this);
    imuLabel20->setStyleSheet("color: #ECF0F1; font-weight: bold;");
    m_labelIMU_d20 = new QLabel("--", this);
    m_labelIMU_d20->setStyleSheet("font-family: monospace; font-size: 10pt; color: #2ECC71; font-weight: bold; min-width: 80px;");

    QLabel* imuLabel30 = new QLabel("IMU 0x30:", this);
    imuLabel30->setStyleSheet("color: #ECF0F1; font-weight: bold;");
    m_labelIMU_d30 = new QLabel("--", this);
    m_labelIMU_d30->setStyleSheet("font-family: monospace; font-size: 10pt; color: #E74C3C; font-weight: bold; min-width: 80px;");

    QLabel* imuLabel40 = new QLabel("IMU 0x40:", this);
    imuLabel40->setStyleSheet("color: #ECF0F1; font-weight: bold;");
    m_labelIMU_d40 = new QLabel("--", this);
    m_labelIMU_d40->setStyleSheet("font-family: monospace; font-size: 10pt; color: #F1C40F; font-weight: bold; min-width: 80px;");

    imuLayout->addWidget(imuLabel10);
    imuLayout->addWidget(m_labelIMU_d10);
    imuLayout->addWidget(imuLabel20);
    imuLayout->addWidget(m_labelIMU_d20);
    imuLayout->addWidget(imuLabel30);
    imuLayout->addWidget(m_labelIMU_d30);
    imuLayout->addWidget(imuLabel40);
    imuLayout->addWidget(m_labelIMU_d40);

    // 响应模式按钮
    m_btnResponseMode = new QPushButton("响应模式", this);
    m_btnResponseMode->setFixedHeight(30);
    m_btnResponseMode->setStyleSheet(
        "QPushButton { background-color: #8E44AD; color: white; "
        "border-radius: 4px; padding: 4px 10px; font-weight: bold; font-size: 9pt; }"
        "QPushButton:hover { background-color: #7D3C98; }"
        "QPushButton:checked { background-color: #E67E22; border: 2px solid white; }");
    m_btnResponseMode->setCheckable(true);
    imuLayout->addWidget(m_btnResponseMode);
    imuLayout->addStretch();

    rightLayout->addWidget(imuGroup);

    // ========== 连接响应模式开关 ==========
    connect(m_btnResponseMode, &QPushButton::toggled, this, [this](bool checked) {
        m_responseMode = checked;
        if (checked) {
            m_btnResponseMode->setText("响应模式 ON");
            m_btnResponseMode->setStyleSheet(
                "QPushButton { background-color: #E67E22; color: white; "
                "border-radius: 4px; padding: 4px 10px; font-weight: bold; font-size: 9pt; border: 2px solid white; }"
                "QPushButton:hover { background-color: #D35400; }");
            // 记录当前角度作为参考零点
            m_hipRefPos  = m_cachedPos[IDX_RIGHT_HIP];
            m_kneeRefPos = m_cachedPos[IDX_RIGHT_KNEE];
            // 向 UDP 对端发送 start 信号
            m_udpSender->sendRaw(QByteArray("start"));
            qDebug() << "[响应模式] 已开启，发送 start 信号"
                     << "右髋参考:" << m_hipRefPos << "右膝参考:" << m_kneeRefPos;
        } else {
            m_btnResponseMode->setText("响应模式");
            m_btnResponseMode->setStyleSheet(
                "QPushButton { background-color: #8E44AD; color: white; "
                "border-radius: 4px; padding: 4px 10px; font-weight: bold; font-size: 9pt; }"
                "QPushButton:hover { background-color: #7D3C98; }");
            // 向 UDP 对端发送 stop 信号
            m_udpSender->sendRaw(QByteArray("stop"));
            qDebug() << "[响应模式] 已关闭，发送 stop 信号";
        }
    });

    // ========== 启动 UDP 接收 ==========
    m_udpReceiver = new UdpReceiver(this);
    connect(m_udpReceiver, &UdpReceiver::imuDataReceived, this,
        [this](double d10, double d20, double d30, double d40) {
            // 更新显示（原始值）
            m_labelIMU_d10->setText(QString::number(d10, 'f', 3));
            m_labelIMU_d20->setText(QString::number(d20, 'f', 3));
            m_labelIMU_d30->setText(QString::number(d30, 'f', 3));
            m_labelIMU_d40->setText(QString::number(d40, 'f', 3));

            // 响应模式：右髋(0x20→idx3)和右膝(0x30→idx2)位置随动
            if (!m_responseMode || !m_shmConnected || !m_shmPtr) return;

            const double RESPONSE_POS_SPEED = 10.0;

            // ===== 5 点滑动窗口均值滤波 =====
            m_bufD20[m_bufIdx] = d20;
            m_bufD30[m_bufIdx] = d30;
            m_bufIdx = (m_bufIdx + 1) % IMU_WINDOW_SIZE;

            double avgD20 = 0.0, avgD30 = 0.0;
            for (int i = 0; i < IMU_WINDOW_SIZE; i++) {
                avgD20 += m_bufD20[i];
                avgD30 += m_bufD30[i];
            }
            avgD20 /= IMU_WINDOW_SIZE;
            avgD30 /= IMU_WINDOW_SIZE;

            // ===== 右髋控制 (d20) 死区 + 最小变化阈值 =====
            if (fabs(avgD20) >= IMU_DEADBAND) {
                double target = m_hipRefPos - avgD20;
                if (target < -40.0) target = -40.0;
                // 只有目标变化超过 IMU_MIN_CHANGE 时才发送，防止频繁启停
                if (fabs(target - m_lastTargetHip) >= IMU_MIN_CHANGE) {
                    sendAxisCommand(IDX_RIGHT_HIP, CMD_MOVE_ABS, target, RESPONSE_POS_SPEED);
                    m_lastTargetHip = target;
                }
            }

            // ===== 右膝控制 (d30) 死区 + 最小变化阈值 =====
            if (fabs(avgD30) >= IMU_DEADBAND) {
                double target = m_kneeRefPos - avgD30;
                if (target > -125.0) target = -125.0;
                if (fabs(target - m_lastTargetKnee) >= IMU_MIN_CHANGE) {
                    sendAxisCommand(IDX_RIGHT_KNEE, CMD_MOVE_ABS, target, RESPONSE_POS_SPEED);
                    m_lastTargetKnee = target;
                }
            }
        });

    bool recvOk = m_udpReceiver->start(12345);
    if (!recvOk) {
        qWarning() << "UDP 接收器启动失败，端口 12345 可能被占用";
    }
}

void MainWindow::initSharedMemory()
{
    m_shmFd = shm_open(MOTOR_SHM_NAME, O_RDWR, 0666);
    if (m_shmFd < 0)
    {
        m_shmFd = shm_open(MOTOR_SHM_NAME, O_RDONLY, 0666);
        if (m_shmFd < 0)
        {
            m_labelStatus->setText("状态: 未连接 (共享内存不存在，请先启动 ServoDemo)");
            m_labelStatus->setStyleSheet("QLabel { color: #E74C3C; font-weight: bold; padding: 6px; }");
            m_shmConnected = false;
            return;
        }
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
        m_labelStatus->setText("状态: 已连接 (只读模式，无法控制)");
        m_labelStatus->setStyleSheet("QLabel { color: #F1C40F; font-weight: bold; padding: 6px; }");
        return;
    }

    m_shmPtr = mmap(NULL, MOTOR_SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, m_shmFd, 0);
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
    m_labelStatus->setText("状态: 已连接 (读写模式，可控制)");
    m_labelStatus->setStyleSheet("QLabel { color: #27AE60; font-weight: bold; padding: 6px; }");
}

void MainWindow::closeSharedMemory()
{
    if (m_shmPtr) { munmap(m_shmPtr, MOTOR_SHM_SIZE); m_shmPtr = nullptr; }
    if (m_shmFd >= 0) { ::close(m_shmFd); m_shmFd = -1; }
}

void MainWindow::sendCommand(int cmdType, double value, double speed)
{
    if (!m_shmConnected || !m_shmPtr)
    {
        m_labelCmdStatus->setText("命令状态: 未连接");
        m_labelCmdStatus->setStyleSheet("QLabel { color: #E74C3C; font-weight: bold; padding: 4px; font-size: 10pt; }");
        return;
    }

    // 直接写入控制字段，避免读取-修改-写回的竞争条件
    // 先读取当前的 cmd_sequence 用于递增
    uint64_t old_seq;
    memcpy(&old_seq, (uint8_t*)m_shmPtr + offsetof(MotorMonitorData, cmd_sequence), sizeof(uint64_t));

    // 准备要写入的数据
    int32_t typeVal = cmdType;
    int32_t axisVal = m_currentAxis;
    double valueVal = value;
    double speedVal = speed;
    double targetPosVal = (cmdType == CMD_MOVE_ABS || cmdType == CMD_MOVE_TO) ? value : 0.0;
    uint64_t newSeq = old_seq + 1;
    int32_t statusVal = 0;
    char resultZero[64] = {0};

    // 写入顺序至关重要！
    // ServoDemo 先读 cmd_type，再读 cmd_sequence。
    // 所以必须先写 cmd_sequence，再用内存屏障，最后写 cmd_type。
    // 这样当 ServoDemo 看到 cmd_type != CMD_NONE 时，cmd_sequence 保证已更新。

    // 第1步：先写 cmd_sequence（以及其他辅助字段）
    memcpy((uint8_t*)m_shmPtr + offsetof(MotorMonitorData, cmd_sequence), &newSeq, sizeof(uint64_t));
    memcpy((uint8_t*)m_shmPtr + offsetof(MotorMonitorData, cmd_axis_idx), &axisVal, sizeof(int32_t));
    memcpy((uint8_t*)m_shmPtr + offsetof(MotorMonitorData, cmd_value), &valueVal, sizeof(double));
    memcpy((uint8_t*)m_shmPtr + offsetof(MotorMonitorData, cmd_speed), &speedVal, sizeof(double));
    memcpy((uint8_t*)m_shmPtr + offsetof(MotorMonitorData, cmd_target_position), &targetPosVal, sizeof(double));
    memcpy((uint8_t*)m_shmPtr + offsetof(MotorMonitorData, cmd_status), &statusVal, sizeof(int32_t));
    memcpy((uint8_t*)m_shmPtr + offsetof(MotorMonitorData, cmd_result), resultZero, 64);

    // 第2步：内存屏障，确保所有上述写入对其他线程可见
    __sync_synchronize();

    // 第3步：最后写 cmd_type，作为"发布"信号
    // 当 ServoDemo 看到 cmd_type != CMD_NONE 时，cmd_sequence 已经是最新的
    memcpy((uint8_t*)m_shmPtr + offsetof(MotorMonitorData, cmd_type), &typeVal, sizeof(int32_t));

    // 再次内存屏障，确保 cmd_type 写入也可见
    __sync_synchronize();

    m_labelCmdStatus->setText(QString("命令状态: 已发送 (类型=%1, 轴=%2)")
        .arg(cmdType).arg(AXIS_NAMES[m_currentAxis]));
    m_labelCmdStatus->setStyleSheet("QLabel { color: #3498DB; font-weight: bold; padding: 4px; font-size: 10pt; }");
}

void MainWindow::sendAxisCommand(int axisIdx, int cmdType, double value, double speed)
{
    if (!m_shmConnected || !m_shmPtr) return;

    uint64_t old_seq;
    memcpy(&old_seq, (uint8_t*)m_shmPtr + offsetof(MotorMonitorData, cmd_sequence), sizeof(uint64_t));

    int32_t typeVal = cmdType;
    int32_t axisVal = axisIdx;
    double valueVal = value;
    double speedVal = speed;
    uint64_t newSeq = old_seq + 1;
    int32_t statusVal = 0;
    char resultZero[64] = {0};

    // 先写辅助字段
    memcpy((uint8_t*)m_shmPtr + offsetof(MotorMonitorData, cmd_sequence), &newSeq, sizeof(uint64_t));
    memcpy((uint8_t*)m_shmPtr + offsetof(MotorMonitorData, cmd_axis_idx), &axisVal, sizeof(int32_t));
    memcpy((uint8_t*)m_shmPtr + offsetof(MotorMonitorData, cmd_value), &valueVal, sizeof(double));
    memcpy((uint8_t*)m_shmPtr + offsetof(MotorMonitorData, cmd_speed), &speedVal, sizeof(double));
    memcpy((uint8_t*)m_shmPtr + offsetof(MotorMonitorData, cmd_target_position), &valueVal, sizeof(double));
    memcpy((uint8_t*)m_shmPtr + offsetof(MotorMonitorData, cmd_status), &statusVal, sizeof(int32_t));
    memcpy((uint8_t*)m_shmPtr + offsetof(MotorMonitorData, cmd_result), resultZero, 64);

    __sync_synchronize();

    // 最后写 cmd_type 作为发布信号
    memcpy((uint8_t*)m_shmPtr + offsetof(MotorMonitorData, cmd_type), &typeVal, sizeof(int32_t));
    __sync_synchronize();
}

void MainWindow::onAxisButtonClicked(int axisIndex)
{
    if (axisIndex < 0 || axisIndex > 9) return;

    m_currentAxis = axisIndex;

    m_chartPos->setAxis(axisIndex, m_cachedAxisType[axisIndex]);
    m_chartVel->setAxis(axisIndex, m_cachedAxisType[axisIndex]);
    m_chartTor->setAxis(axisIndex, m_cachedAxisType[axisIndex]);

    QString typeStr = (m_cachedAxisType[axisIndex] == MOTOR_TYPE_ROTARY) ? "旋转" : "线性";
    m_labelAxisInfo->setText(
        QString("当前: %1 | 类型: %2")
            .arg(AXIS_NAMES[axisIndex]).arg(typeStr));

    if (m_hasCachedData)
    {
        m_labelPosition->setText(
            QString("位置: %1%2")
                .arg(m_cachedPos[axisIndex], 0, 'f', 2)
                .arg(unitForPos(m_cachedAxisType[axisIndex])));
        m_labelVelocity->setText(
            QString("速度: %1%2")
                .arg(m_cachedVel[axisIndex], 0, 'f', 2)
                .arg(unitForVel(m_cachedAxisType[axisIndex])));
        m_labelTorque->setText(
            QString("力矩: %1%2")
                .arg(m_cachedTor[axisIndex], 0, 'f', 2)
                .arg(unitForTorque(m_cachedAxisType[axisIndex])));
    }

    int axisType = m_cachedAxisType[axisIndex];
    const char* posUnit = unitForPos(axisType);
    const char* speedUnit = unitForVel(axisType);
    m_spinTargetPos->setSuffix(QString(" %1").arg(posUnit));
    m_spinPosSpeed->setSuffix(QString(" %1").arg(speedUnit));
    m_spinVelSpeed->setSuffix(QString(" %1").arg(speedUnit));
}

void MainWindow::onTimerTick()
{
    if (m_paused || !m_shmConnected || !m_shmPtr) return;

    MotorMonitorData data;
    memcpy(&data, m_shmPtr, sizeof(MotorMonitorData));

    if (data.sequence == m_lastSequence) return;
    m_lastSequence = data.sequence;

    m_hasCachedData = true;
    for (int i = 0; i < 10; i++)
    {
        m_cachedPos[i] = data.position_deg[i];
        m_cachedVel[i] = data.velocity_deg_per_s[i];
        m_cachedTor[i] = data.torque_nm[i];
        m_cachedAxisType[i] = data.axis_type[i];
    }

    int idx = m_currentAxis;
    int axisType = data.axis_type[idx];

    m_chartPos->addDataPoint(data.position_deg[idx], axisType);
    m_chartVel->addDataPoint(data.velocity_deg_per_s[idx], axisType);
    m_chartTor->addDataPoint(data.torque_nm[idx], axisType);

    m_chartPos->autoAdjustYAxis();
    m_chartVel->autoAdjustYAxis();
    m_chartTor->autoAdjustYAxis();

    m_labelPosition->setText(
        QString("位置: %1%2")
            .arg(data.position_deg[idx], 0, 'f', 2)
            .arg(unitForPos(axisType)));
    m_labelVelocity->setText(
        QString("速度: %1%2")
            .arg(data.velocity_deg_per_s[idx], 0, 'f', 2)
            .arg(unitForVel(axisType)));
    m_labelTorque->setText(
        QString("力矩: %1%2")
            .arg(data.torque_nm[idx], 0, 'f', 2)
            .arg(unitForTorque(axisType)));

    // 速度模式 - 实时显示运动状态
    if (data.cmd_type == CMD_VELOCITY_MODE && data.cmd_status == 1)
    {
        double vel = data.cmd_value;
        m_labelVelDistance->setText(
            QString("运动距离: 运动中 (速度: %1%2/s)")
                .arg(fabs(vel), 0, 'f', 1).arg(unitForVel(axisType)));
    }
    else
    {
        m_labelVelDistance->setText("运动距离: --");
    }

    // 位置模式 - 实时显示目标和剩余距离
    if ((data.cmd_type == CMD_MOVE_TO || data.cmd_type == CMD_MOVE_ABS)
        && data.cmd_status == 1)
    {
        double target = data.cmd_target_position;
        double remain = target - data.position_deg[idx];
        m_labelPosTarget->setText(
            QString("目标: %1%2").arg(target, 0, 'f', 2).arg(unitForPos(axisType)));
        m_labelPosRemain->setText(
            QString("剩余: %1%2").arg(remain, 0, 'f', 2).arg(unitForPos(axisType)));
    }
    else if ((data.cmd_type == CMD_MOVE_TO || data.cmd_type == CMD_MOVE_ABS)
             && data.cmd_status == 2)
    {
        m_labelPosTarget->setText("目标: 已到达");
        m_labelPosRemain->setText("剩余: 0");
    }
    else
    {
        m_labelPosTarget->setText("目标: --");
        m_labelPosRemain->setText("剩余: --");
    }

    // 命令执行状态
    if (data.cmd_status == 1)
    {
        if (data.cmd_type == CMD_VELOCITY_MODE)
        {
            m_labelCmdStatus->setText("命令状态: 速度模式运行中...");
            m_labelCmdStatus->setStyleSheet("QLabel { color: #F1C40F; font-weight: bold; padding: 4px; font-size: 10pt; }");
        }
        else
        {
            m_labelCmdStatus->setText("命令状态: 执行中...");
            m_labelCmdStatus->setStyleSheet("QLabel { color: #F1C40F; font-weight: bold; padding: 4px; font-size: 10pt; }");
        }
    }
    else if (data.cmd_status == 2)
    {
        m_labelCmdStatus->setText(QString("命令状态: 完成 (%1)").arg(data.cmd_result));
        m_labelCmdStatus->setStyleSheet("QLabel { color: #27AE60; font-weight: bold; padding: 4px; font-size: 10pt; }");
    }
    else if (data.cmd_status == -1)
    {
        m_labelCmdStatus->setText(QString("命令状态: 失败 (%1)").arg(data.cmd_result));
        m_labelCmdStatus->setStyleSheet("QLabel { color: #E74C3C; font-weight: bold; padding: 4px; font-size: 10pt; }");
    }
    else
    {
        m_labelCmdStatus->setText("命令状态: 空闲");
        m_labelCmdStatus->setStyleSheet("QLabel { color: #7F8C8D; font-weight: bold; padding: 4px; font-size: 10pt; }");
    }

    // 多轴运动状态更新
    if (data.multi_cmd_status == 1)
    {
        m_labelMultiStatus->setText("多轴运动中...");
        m_labelMultiStatus->setStyleSheet("QLabel { color: #F1C40F; font-weight: bold; padding: 4px; font-size: 10pt; }");
        m_btnMultiRun->setEnabled(false);
        m_btnMultiRun->setText("⏳ 运动中...");

        // 更新每个轴的状态
        int arrivedCount = 0;
        int activeCount = 0;
        for (int i = 0; i < 10; i++)
        {
            int axisStatus = data.multi_axis_status[i];
            if (data.multi_active[i])
            {
                activeCount++;
                if (axisStatus == 2)
                {
                    m_multiAxisStatusLabel[i]->setText("✓ 已到达");
                    m_multiAxisStatusLabel[i]->setStyleSheet("QLabel { color: #27AE60; font-weight: bold; font-size: 9pt; }");
                    arrivedCount++;
                }
                else if (axisStatus == 1)
                {
                    m_multiAxisStatusLabel[i]->setText("▶ 运动中");
                    m_multiAxisStatusLabel[i]->setStyleSheet("QLabel { color: #F1C40F; font-weight: bold; font-size: 9pt; }");
                }
                else if (axisStatus == -1)
                {
                    m_multiAxisStatusLabel[i]->setText("✗ 失败");
                    m_multiAxisStatusLabel[i]->setStyleSheet("QLabel { color: #E74C3C; font-weight: bold; font-size: 9pt; }");
                }
                else
                {
                    m_multiAxisStatusLabel[i]->setText("⏳ 等待");
                    m_multiAxisStatusLabel[i]->setStyleSheet("QLabel { color: #3498DB; font-size: 9pt; }");
                }
            }
            else
            {
                m_multiAxisStatusLabel[i]->setText("--");
                m_multiAxisStatusLabel[i]->setStyleSheet("QLabel { color: #7F8C8D; font-size: 9pt; }");
            }
        }

        // 如果所有 active 的轴都已到达，标记完成
        if (activeCount > 0 && arrivedCount >= activeCount)
        {
            m_labelMultiStatus->setText("多轴运动完成");
            m_labelMultiStatus->setStyleSheet("QLabel { color: #27AE60; font-weight: bold; padding: 4px; font-size: 10pt; }");
            m_btnMultiRun->setEnabled(true);
            m_btnMultiRun->setText("🚀 多轴运行");
        }
    }
    else if (data.multi_cmd_status == 2)
    {
        m_labelMultiStatus->setText(QString("完成: %1").arg(data.multi_cmd_result));
        m_labelMultiStatus->setStyleSheet("QLabel { color: #27AE60; font-weight: bold; padding: 4px; font-size: 10pt; }");
        m_btnMultiRun->setEnabled(true);
        m_btnMultiRun->setText("🚀 多轴运行");
    }
    else if (data.multi_cmd_status == -1)
    {
        m_labelMultiStatus->setText(QString("失败: %1").arg(data.multi_cmd_result));
        m_labelMultiStatus->setStyleSheet("QLabel { color: #E74C3C; font-weight: bold; padding: 4px; font-size: 10pt; }");
        m_btnMultiRun->setEnabled(true);
        m_btnMultiRun->setText("🚀 多轴运行");
    }
    else
    {
        // 空闲状态，重置所有轴状态标签
        for (int i = 0; i < 10; i++)
        {
            m_multiAxisStatusLabel[i]->setText("--");
            m_multiAxisStatusLabel[i]->setStyleSheet("QLabel { color: #7F8C8D; font-size: 9pt; }");
        }
    }

    // 序列队列状态更新
    m_cachedSeqStatus = data.seq_status;
    m_cachedSeqCurrentGroup = data.seq_current_group;
    m_cachedSeqGroupCount = data.seq_group_count;

    if (data.seq_status == 1)
    {
        m_labelSeqStatus->setText(QString("运行中: 组%1/%2 (%3)")
            .arg(data.seq_current_group + 1)
            .arg(data.seq_group_count)
            .arg(data.seq_looping ? "循环" : "单次"));
        m_labelSeqStatus->setStyleSheet("QLabel { color: #F1C40F; font-weight: bold; padding: 2px; font-size: 9pt; }");
        m_btnSeqStart->setEnabled(false);
        m_btnSeqAdd->setEnabled(false);
        m_btnSeqDelete->setEnabled(false);
        m_btnSeqClear->setEnabled(false);

        // 高亮当前执行的组
        for (int r = 0; r < m_seqListWidget->count(); r++)
        {
            QListWidgetItem* item = m_seqListWidget->item(r);
            if (r == data.seq_current_group)
                item->setForeground(QColor("#F1C40F"));
            else
                item->setForeground(QColor("#ECF0F1"));
        }

        // 更新左侧多轴状态标签显示序列各轴进度
        for (int i = 0; i < 10; i++)
        {
            int axisStatus = data.seq_axis_status[i];
            if (data.seq_active[data.seq_current_group][i])
            {
                if (axisStatus == 2)
                {
                    m_multiAxisStatusLabel[i]->setText("✓ 到达");
                    m_multiAxisStatusLabel[i]->setStyleSheet("QLabel { color: #27AE60; font-weight: bold; font-size: 8pt; }");
                }
                else if (axisStatus == 1)
                {
                    m_multiAxisStatusLabel[i]->setText("▶ 运动");
                    m_multiAxisStatusLabel[i]->setStyleSheet("QLabel { color: #F1C40F; font-weight: bold; font-size: 8pt; }");
                }
                else if (axisStatus == 3)
                {
                    m_multiAxisStatusLabel[i]->setText("🛑 限位");
                    m_multiAxisStatusLabel[i]->setStyleSheet("QLabel { color: #E74C3C; font-weight: bold; font-size: 8pt; }");
                }
                else
                {
                    m_multiAxisStatusLabel[i]->setText("⏳ 等待");
                    m_multiAxisStatusLabel[i]->setStyleSheet("QLabel { color: #3498DB; font-size: 8pt; }");
                }
            }
            else if (data.multi_cmd_status == 0)
            {
                m_multiAxisStatusLabel[i]->setText("--");
                m_multiAxisStatusLabel[i]->setStyleSheet("QLabel { color: #7F8C8D; font-size: 8pt; }");
            }
        }
    }
    else if (data.seq_status == 0 && data.seq_group_count > 0)
    {
        m_labelSeqStatus->setText(QString("%1 组就绪").arg(data.seq_group_count));
        m_labelSeqStatus->setStyleSheet("QLabel { color: #27AE60; font-weight: bold; padding: 2px; font-size: 9pt; }");
        m_btnSeqStart->setEnabled(true);
        m_btnSeqAdd->setEnabled(true);
        m_btnSeqDelete->setEnabled(true);
        m_btnSeqClear->setEnabled(true);
        // 恢复列表项颜色
        for (int r = 0; r < m_seqListWidget->count(); r++)
            m_seqListWidget->item(r)->setForeground(QColor("#ECF0F1"));
    }
    else if (data.seq_status == 0 && data.seq_group_count == 0)
    {
        // 保持默认"队列为空"
    }

    statusBar()->showMessage(
        QString("序列号: %1 | 时间戳: %2 us | 控制周期: %3 us | 当前: %4")
            .arg(data.sequence).arg(data.timestamp_us)
            .arg(data.control_cycle_us).arg(AXIS_NAMES[idx]));

    // ========== UDP 遥测：发送全部 6 个旋转关节实时角度 ==========
    if (m_udpSender && m_udpSender->isRunning())
    {
        m_udpSender->sendAngles(
            data.position_deg[IDX_RIGHT_HIP],
            data.position_deg[IDX_RIGHT_KNEE],
            data.position_deg[IDX_RIGHT_ANKLE],
            data.position_deg[IDX_LEFT_HIP],
            data.position_deg[IDX_LEFT_KNEE],
            data.position_deg[IDX_LEFT_ANKLE]);
    }
}

void MainWindow::onClearData()
{
    m_chartPos->clearData();
    m_chartVel->clearData();
    m_chartTor->clearData();
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
    m_chartPos->autoAdjustYAxis();
    m_chartVel->autoAdjustYAxis();
    m_chartTor->autoAdjustYAxis();
}

// ========== 速度模式控制 ==========

void MainWindow::onVelocityPositive()
{
    double speed = m_spinVelSpeed->value();
    sendCommand(CMD_VELOCITY_MODE, speed, speed);
}

void MainWindow::onVelocityNegative()
{
    double speed = m_spinVelSpeed->value();
    sendCommand(CMD_VELOCITY_MODE, -speed, speed);
}

void MainWindow::onVelocityStop()
{
    sendCommand(CMD_VELOCITY_STOP);
}

// ========== 位置模式控制 ==========

void MainWindow::onPositionModeRun()
{
    double target = m_spinTargetPos->value();
    double speed = m_spinPosSpeed->value();
    sendCommand(CMD_MOVE_ABS, target, speed);
}

void MainWindow::onPreset1()
{
    m_multiTargetPos[2]->setValue(-155.0);
    m_multiTargetPos[3]->setValue(-25.0);
    m_multiTargetPos[4]->setValue(205.0);
    m_multiTargetPos[6]->setValue(78.0);
    m_multiActive[2]->setChecked(true);
    m_multiActive[3]->setChecked(true);
    m_multiActive[4]->setChecked(true);
    m_multiActive[6]->setChecked(true);
    for (int i = 0; i < 10; i++)
        if (i != 2 && i != 3 && i != 4 && i != 6)
            m_multiActive[i]->setChecked(false);
    m_labelMultiStatus->setText("预设1: 右膝-155° 右髋-25° 左髋205° 左膝78°");
    m_labelMultiStatus->setStyleSheet("QLabel { color: #8E44AD; font-weight: bold; padding: 2px; font-size: 9pt; }");
}

void MainWindow::onPreset2()
{
    m_multiTargetPos[2]->setValue(-118.0);
    m_multiTargetPos[3]->setValue(-45.0);
    m_multiTargetPos[4]->setValue(187.0);
    m_multiTargetPos[6]->setValue(115.0);
    m_multiActive[2]->setChecked(true);
    m_multiActive[3]->setChecked(true);
    m_multiActive[4]->setChecked(true);
    m_multiActive[6]->setChecked(true);
    for (int i = 0; i < 10; i++)
        if (i != 2 && i != 3 && i != 4 && i != 6)
            m_multiActive[i]->setChecked(false);
    m_labelMultiStatus->setText("预设2: 右膝-118° 右髋-45° 左髋187° 左膝115°");
    m_labelMultiStatus->setStyleSheet("QLabel { color: #8E44AD; font-weight: bold; padding: 2px; font-size: 9pt; }");
}

void MainWindow::onPreset3()
{
    m_multiTargetPos[2]->setValue(-155.0);
    m_multiTargetPos[3]->setValue(-25.0);
    m_multiTargetPos[4]->setValue(-152.0);
    m_multiTargetPos[6]->setValue(78.0);
    m_multiActive[2]->setChecked(true);
    m_multiActive[3]->setChecked(true);
    m_multiActive[4]->setChecked(true);
    m_multiActive[6]->setChecked(true);
    for (int i = 0; i < 10; i++)
        if (i != 2 && i != 3 && i != 4 && i != 6)
            m_multiActive[i]->setChecked(false);
    m_labelMultiStatus->setText("预设3: 右膝-155° 右髋-25° 左髋-152° 左膝78°");
    m_labelMultiStatus->setStyleSheet("QLabel { color: #8E44AD; font-weight: bold; padding: 2px; font-size: 9pt; }");
}

void MainWindow::onPreset4()
{
    m_multiTargetPos[2]->setValue(-118.0);
    m_multiTargetPos[3]->setValue(-45.0);
    m_multiTargetPos[4]->setValue(-172.0);
    m_multiTargetPos[6]->setValue(115.0);
    m_multiActive[2]->setChecked(true);
    m_multiActive[3]->setChecked(true);
    m_multiActive[4]->setChecked(true);
    m_multiActive[6]->setChecked(true);
    for (int i = 0; i < 10; i++)
        if (i != 2 && i != 3 && i != 4 && i != 6)
            m_multiActive[i]->setChecked(false);
    m_labelMultiStatus->setText("预设4: 右膝-118° 右髋-45° 左髋-172° 左膝115°");
    m_labelMultiStatus->setStyleSheet("QLabel { color: #8E44AD; font-weight: bold; padding: 2px; font-size: 9pt; }");
}

// ========== 使能/失能 ==========

void MainWindow::onEnable()
{
    sendCommand(CMD_ENABLE);
}

void MainWindow::onDisable()
{
    sendCommand(CMD_DISABLE);
}

// ========== 多轴位置控制 ==========

void MainWindow::sendMultiAxisCommand()
{
    if (!m_shmConnected || !m_shmPtr)
    {
        m_labelMultiStatus->setText("状态: 未连接");
        m_labelMultiStatus->setStyleSheet("QLabel { color: #E74C3C; font-weight: bold; padding: 4px; font-size: 10pt; }");
        return;
    }

    // 检查是否有至少一个轴被勾选
    bool anyActive = false;
    for (int i = 0; i < 10; i++)
    {
        if (m_multiActive[i]->isChecked())
        {
            anyActive = true;
            break;
        }
    }
    if (!anyActive)
    {
        m_labelMultiStatus->setText("请至少勾选一个轴");
        m_labelMultiStatus->setStyleSheet("QLabel { color: #E67E22; font-weight: bold; padding: 4px; font-size: 10pt; }");
        return;
    }

    // 读取当前 multi_cmd_sequence 用于递增
    uint64_t old_seq;
    memcpy(&old_seq, (uint8_t*)m_shmPtr + offsetof(MotorMonitorData, multi_cmd_sequence), sizeof(uint64_t));
    uint64_t newSeq = old_seq + 1;

    // 先写 multi_cmd_sequence
    memcpy((uint8_t*)m_shmPtr + offsetof(MotorMonitorData, multi_cmd_sequence), &newSeq, sizeof(uint64_t));

    // 使用统一速度（从位置模式的速度输入框获取）
    double uniformSpeed = m_spinPosSpeed->value();

    // 写入每个轴的目标位置、速度、启用状态
    for (int i = 0; i < 10; i++)
    {
        int32_t active = m_multiActive[i]->isChecked() ? 1 : 0;
        double target = m_multiTargetPos[i]->value();
        double speed = uniformSpeed;

        memcpy((uint8_t*)m_shmPtr + offsetof(MotorMonitorData, multi_active) + i * sizeof(int32_t),
               &active, sizeof(int32_t));
        memcpy((uint8_t*)m_shmPtr + offsetof(MotorMonitorData, multi_target_pos) + i * sizeof(double),
               &target, sizeof(double));
        memcpy((uint8_t*)m_shmPtr + offsetof(MotorMonitorData, multi_speed) + i * sizeof(double),
               &speed, sizeof(double));

        // 重置轴状态
        int32_t axisStatus = 0;
        memcpy((uint8_t*)m_shmPtr + offsetof(MotorMonitorData, multi_axis_status) + i * sizeof(int32_t),
               &axisStatus, sizeof(int32_t));
    }

    // 内存屏障
    __sync_synchronize();

    // 最后写 multi_cmd_status = 1 作为"发布"信号
    int32_t statusVal = 1;
    memcpy((uint8_t*)m_shmPtr + offsetof(MotorMonitorData, multi_cmd_status), &statusVal, sizeof(int32_t));
    __sync_synchronize();

    m_labelMultiStatus->setText("多轴运动已启动");
    m_labelMultiStatus->setStyleSheet("QLabel { color: #27AE60; font-weight: bold; padding: 4px; font-size: 10pt; }");
    m_btnMultiRun->setEnabled(false);
    m_btnMultiRun->setText("⏳ 运动中...");
}

void MainWindow::onMultiAxisRun()
{
    sendMultiAxisCommand();
}

// ========== 动作序列队列 ==========

void MainWindow::onSeqAddGroup()
{
    if (!m_shmConnected || !m_shmPtr) return;

    bool anyActive = false;
    for (int i = 0; i < 10; i++)
    {
        if (m_multiActive[i]->isChecked()) { anyActive = true; break; }
    }
    if (!anyActive)
    {
        m_labelSeqStatus->setText("请至少勾选一个轴");
        m_labelSeqStatus->setStyleSheet("QLabel { color: #E67E22; font-weight: bold; padding: 2px; font-size: 9pt; }");
        return;
    }

    uint32_t currentCount;
    memcpy(&currentCount, (uint8_t*)m_shmPtr + offsetof(MotorMonitorData, seq_group_count), sizeof(int32_t));
    if ((int)currentCount >= MAX_SEQUENCE_GROUPS)
    {
        m_labelSeqStatus->setText(QString("队列已满（最多 %1 组）").arg(MAX_SEQUENCE_GROUPS));
        m_labelSeqStatus->setStyleSheet("QLabel { color: #E74C3C; font-weight: bold; padding: 2px; font-size: 9pt; }");
        return;
    }

    int groupIdx = (int)currentCount;
    double uniformSpeed = m_spinVelSpeed->value();

    for (int i = 0; i < 10; i++)
    {
        int32_t active = m_multiActive[i]->isChecked() ? 1 : 0;
        double target = m_multiTargetPos[i]->value();
        double speed = uniformSpeed;

        memcpy((uint8_t*)m_shmPtr + offsetof(MotorMonitorData, seq_active)
               + groupIdx * MOTOR_NUM_AXES * sizeof(int32_t) + i * sizeof(int32_t),
               &active, sizeof(int32_t));
        memcpy((uint8_t*)m_shmPtr + offsetof(MotorMonitorData, seq_target)
               + groupIdx * MOTOR_NUM_AXES * sizeof(double) + i * sizeof(double),
               &target, sizeof(double));
        memcpy((uint8_t*)m_shmPtr + offsetof(MotorMonitorData, seq_speed)
               + groupIdx * MOTOR_NUM_AXES * sizeof(double) + i * sizeof(double),
               &speed, sizeof(double));
    }

    int32_t newCount = currentCount + 1;
    memcpy((uint8_t*)m_shmPtr + offsetof(MotorMonitorData, seq_group_count), &newCount, sizeof(int32_t));
    __sync_synchronize();

    uint64_t oldSeq;
    memcpy(&oldSeq, (uint8_t*)m_shmPtr + offsetof(MotorMonitorData, seq_cmd_sequence), sizeof(uint64_t));
    uint64_t newSeq = oldSeq + 1;
    memcpy((uint8_t*)m_shmPtr + offsetof(MotorMonitorData, seq_cmd_sequence), &newSeq, sizeof(uint64_t));
    __sync_synchronize();

    m_cachedSeqGroupCount = newCount;

    QString desc = QString("组%1: ").arg(newCount);
    bool first = true;
    for (int i = 0; i < 10; i++)
    {
        if (m_multiActive[i]->isChecked())
        {
            if (!first) desc += ", ";
            first = false;
            int axisType = (i == 1 || i == 5 || i == 7 || i == 9) ? MOTOR_TYPE_LINEAR : MOTOR_TYPE_ROTARY;
            desc += QString("%1→%2%3")
                .arg(AXIS_NAMES[i].mid(0, AXIS_NAMES[i].indexOf('(')))
                .arg(m_multiTargetPos[i]->value(), 0, 'f', 1)
                .arg(unitForPos(axisType));
        }
    }
    m_seqListWidget->addItem(desc);
    m_seqListWidget->scrollToBottom();

    m_labelSeqStatus->setText(QString("已添加 %1/%2 组").arg(newCount).arg(MAX_SEQUENCE_GROUPS));
    m_labelSeqStatus->setStyleSheet("QLabel { color: #27AE60; font-weight: bold; padding: 2px; font-size: 9pt; }");
}

void MainWindow::onSeqClear()
{
    if (!m_shmConnected || !m_shmPtr) return;

    int32_t zero = 0;
    memcpy((uint8_t*)m_shmPtr + offsetof(MotorMonitorData, seq_group_count), &zero, sizeof(int32_t));
    memset((uint8_t*)m_shmPtr + offsetof(MotorMonitorData, seq_active), 0,
           MAX_SEQUENCE_GROUPS * MOTOR_NUM_AXES * sizeof(int32_t));
    __sync_synchronize();

    uint64_t oldSeq;
    memcpy(&oldSeq, (uint8_t*)m_shmPtr + offsetof(MotorMonitorData, seq_cmd_sequence), sizeof(uint64_t));
    uint64_t newSeq = oldSeq + 1;
    memcpy((uint8_t*)m_shmPtr + offsetof(MotorMonitorData, seq_cmd_sequence), &newSeq, sizeof(uint64_t));
    __sync_synchronize();

    m_seqListWidget->clear();
    m_cachedSeqGroupCount = 0;
    m_labelSeqStatus->setText("队列为空");
    m_labelSeqStatus->setStyleSheet("QLabel { color: #7F8C8D; font-weight: bold; padding: 2px; font-size: 9pt; }");
}

void MainWindow::onSeqDeleteGroup()
{
    int row = m_seqListWidget->currentRow();
    if (row < 0 || !m_shmConnected || !m_shmPtr) return;

    int32_t count;
    memcpy(&count, (uint8_t*)m_shmPtr + offsetof(MotorMonitorData, seq_group_count), sizeof(int32_t));
    if (row >= (int)count) return;

    for (int g = row; g < (int)count - 1; g++)
    {
        memcpy((uint8_t*)m_shmPtr + offsetof(MotorMonitorData, seq_active)
               + g * MOTOR_NUM_AXES * sizeof(int32_t),
               (uint8_t*)m_shmPtr + offsetof(MotorMonitorData, seq_active)
               + (g + 1) * MOTOR_NUM_AXES * sizeof(int32_t),
               MOTOR_NUM_AXES * sizeof(int32_t));
        memcpy((uint8_t*)m_shmPtr + offsetof(MotorMonitorData, seq_target)
               + g * MOTOR_NUM_AXES * sizeof(double),
               (uint8_t*)m_shmPtr + offsetof(MotorMonitorData, seq_target)
               + (g + 1) * MOTOR_NUM_AXES * sizeof(double),
               MOTOR_NUM_AXES * sizeof(double));
        memcpy((uint8_t*)m_shmPtr + offsetof(MotorMonitorData, seq_speed)
               + g * MOTOR_NUM_AXES * sizeof(double),
               (uint8_t*)m_shmPtr + offsetof(MotorMonitorData, seq_speed)
               + (g + 1) * MOTOR_NUM_AXES * sizeof(double),
               MOTOR_NUM_AXES * sizeof(double));
    }

    int32_t newCount = count - 1;
    memcpy((uint8_t*)m_shmPtr + offsetof(MotorMonitorData, seq_group_count), &newCount, sizeof(int32_t));
    __sync_synchronize();

    uint64_t oldSeq;
    memcpy(&oldSeq, (uint8_t*)m_shmPtr + offsetof(MotorMonitorData, seq_cmd_sequence), sizeof(uint64_t));
    uint64_t newSeq = oldSeq + 1;
    memcpy((uint8_t*)m_shmPtr + offsetof(MotorMonitorData, seq_cmd_sequence), &newSeq, sizeof(uint64_t));
    __sync_synchronize();

    delete m_seqListWidget->takeItem(row);
    m_cachedSeqGroupCount = newCount;

    for (int i = 0; i < m_seqListWidget->count(); i++)
    {
        QListWidgetItem* item = m_seqListWidget->item(i);
        QString text = item->text();
        int idx = text.indexOf(':');
        if (idx > 1) text = QString("组%1").arg(i + 1) + text.mid(idx);
        item->setText(text);
    }

    m_labelSeqStatus->setText(newCount > 0 ? QString("%1 组").arg(newCount) : "队列为空");
    m_labelSeqStatus->setStyleSheet(QString("QLabel { color: %1; font-weight: bold; padding: 2px; font-size: 9pt; }")
        .arg(newCount > 0 ? "#27AE60" : "#7F8C8D"));
}

void MainWindow::onSeqStart()
{
    if (!m_shmConnected || !m_shmPtr) return;

    int32_t count;
    memcpy(&count, (uint8_t*)m_shmPtr + offsetof(MotorMonitorData, seq_group_count), sizeof(int32_t));
    if (count <= 0)
    {
        m_labelSeqStatus->setText("队列为空，请先添加动作组");
        m_labelSeqStatus->setStyleSheet("QLabel { color: #E74C3C; font-weight: bold; padding: 2px; font-size: 9pt; }");
        return;
    }

    int32_t looping = m_seqLoopingCheck->isChecked() ? 1 : 0;
    memcpy((uint8_t*)m_shmPtr + offsetof(MotorMonitorData, seq_looping), &looping, sizeof(int32_t));

    int32_t zero = 0;
    memcpy((uint8_t*)m_shmPtr + offsetof(MotorMonitorData, seq_current_group), &zero, sizeof(int32_t));
    memset((uint8_t*)m_shmPtr + offsetof(MotorMonitorData, seq_axis_status), 0, MOTOR_NUM_AXES * sizeof(int32_t));
    memset((uint8_t*)m_shmPtr + offsetof(MotorMonitorData, seq_result), 0, 256);

    int32_t statusRunning = 1;
    memcpy((uint8_t*)m_shmPtr + offsetof(MotorMonitorData, seq_status), &statusRunning, sizeof(int32_t));
    __sync_synchronize();

    uint64_t oldSeq;
    memcpy(&oldSeq, (uint8_t*)m_shmPtr + offsetof(MotorMonitorData, seq_cmd_sequence), sizeof(uint64_t));
    uint64_t newSeq = oldSeq + 1;
    memcpy((uint8_t*)m_shmPtr + offsetof(MotorMonitorData, seq_cmd_sequence), &newSeq, sizeof(uint64_t));
    __sync_synchronize();
}

void MainWindow::onSeqStop()
{
    if (!m_shmConnected || !m_shmPtr) return;

    int32_t statusIdle = 0;
    memcpy((uint8_t*)m_shmPtr + offsetof(MotorMonitorData, seq_status), &statusIdle, sizeof(int32_t));
    __sync_synchronize();

    uint64_t oldSeq;
    memcpy(&oldSeq, (uint8_t*)m_shmPtr + offsetof(MotorMonitorData, seq_cmd_sequence), sizeof(uint64_t));
    uint64_t newSeq = oldSeq + 1;
    memcpy((uint8_t*)m_shmPtr + offsetof(MotorMonitorData, seq_cmd_sequence), &newSeq, sizeof(uint64_t));
    __sync_synchronize();

    m_labelSeqStatus->setText("队列已停止");
    m_labelSeqStatus->setStyleSheet("QLabel { color: #E67E22; font-weight: bold; padding: 2px; font-size: 9pt; }");
    m_btnSeqStart->setEnabled(true);
    m_btnSeqAdd->setEnabled(true);
    m_btnSeqDelete->setEnabled(true);
    m_btnSeqClear->setEnabled(true);
}

void MainWindow::onSeqLoopingToggled(bool checked)
{
    if (!m_shmConnected || !m_shmPtr) return;

    int32_t looping = checked ? 1 : 0;
    memcpy((uint8_t*)m_shmPtr + offsetof(MotorMonitorData, seq_looping), &looping, sizeof(int32_t));
    __sync_synchronize();

    uint64_t oldSeq;
    memcpy(&oldSeq, (uint8_t*)m_shmPtr + offsetof(MotorMonitorData, seq_cmd_sequence), sizeof(uint64_t));
    uint64_t newSeq = oldSeq + 1;
    memcpy((uint8_t*)m_shmPtr + offsetof(MotorMonitorData, seq_cmd_sequence), &newSeq, sizeof(uint64_t));
    __sync_synchronize();
}