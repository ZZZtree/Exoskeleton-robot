#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QLabel>
#include <QPushButton>
#include <QStatusBar>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QGridLayout>
#include <QButtonGroup>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QListWidget>
#include <QLineEdit>
#include "chartwidget.h"
#include "udp_sender.h"
#include "udp_receiver.h"

/**
 * @brief 主窗口 - 十个电机切换监控与控制界面
 *
 * 10个按钮切换电机，每个电机有3个独立图表分别显示位置、速度、力矩。
 * 控制模式：
 *   速度模式：点击正/负方向按钮持续运动，实时显示当前位置和距离
 *   位置模式：输入目标位置，点击运行，到达目标位置后自动停止
 *   序列模式：多组动作存入队列，按顺序循环执行
 *   UDP遥测：向右髋/右膝/右踝实时角度发送至 172.20.15.30
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private slots:
    void onTimerTick();
    void onAxisButtonClicked(int axisIndex);
    void onClearData();
    void onTogglePause();
    void onAutoAdjust();

    // 速度模式：持续运动
    void onVelocityPositive();   // 正方向运动
    void onVelocityNegative();   // 负方向运动
    void onVelocityStop();       // 停止速度模式

    // 位置模式：输入目标位置，到达后自动停止
    void onPositionModeRun();    // 运行到目标位置

    // 多轴位置控制
    void onMultiAxisRun();       // 多轴一键运行

    // 使能/失能
    void onEnable();
    void onDisable();

    // 预设姿势按钮
    void onPreset1();            // 预设1: 右膝-155,右髋-25,左髋205,左膝78
    void onPreset2();            // 预设2: 右膝-118,右髋-45,左髋187,左膝115
    void onPreset3();            // 预设3: 右膝-155,右髋-25,左髋-152,左膝78
    void onPreset4();            // 预设4: 右膝-118,右髋-45,左髋-172,左膝115

    // 动作序列队列
    void onSeqAddGroup();        // 将当前多轴设定存入队列
    void onSeqClear();           // 清空队列
    void onSeqDeleteGroup();     // 删除选中组
    void onSeqStart();           // 开始执行队列
    void onSeqStop();            // 停止执行队列
    void onSeqLoopingToggled(bool checked); // 循环开关切换

private:
    void setupUI();
    void initSharedMemory();
    void closeSharedMemory();

    // 发送单轴控制命令到共享内存（使用当前 UI 选择的轴）
    void sendCommand(int cmdType, double value = 0.0, double speed = 0.0);

    // 发送指定轴的控制命令（用于响应模式等自动控制）
    void sendAxisCommand(int axisIdx, int cmdType, double value, double speed);

    // 发送多轴控制命令
    void sendMultiAxisCommand();

    // 共享内存
    int m_shmFd;
    void* m_shmPtr;
    uint64_t m_lastSequence;

    // 数据缓存
    double m_cachedPos[10];
    double m_cachedVel[10];
    double m_cachedTor[10];
    int m_cachedAxisType[10];
    bool m_hasCachedData;

    // UI - 图表
    ChartWidget* m_chartPos;
    ChartWidget* m_chartVel;
    ChartWidget* m_chartTor;
    QButtonGroup* m_axisButtonGroup;
    QPushButton* m_axisButtons[10];

    // UI - 控制栏
    QPushButton* m_btnPause;
    QPushButton* m_btnClear;
    QPushButton* m_btnAutoAdjust;

    // UI - 速度模式控制面板
    QGroupBox* m_velocityGroup;
    QPushButton* m_btnVelPos;     // 正方向按钮
    QPushButton* m_btnVelNeg;     // 负方向按钮
    QPushButton* m_btnVelStop;    // 停止按钮
    QDoubleSpinBox* m_spinVelSpeed;  // 速度模式速度
    QLabel* m_labelVelDistance;   // 实时显示运动距离

    // UI - 位置模式控制面板（单轴）
    QGroupBox* m_positionGroup;
    QDoubleSpinBox* m_spinTargetPos;  // 目标位置
    QDoubleSpinBox* m_spinPosSpeed;   // 位置模式速度
    QPushButton* m_btnPosRun;         // 运行按钮
    QLabel* m_labelPosTarget;         // 显示目标位置
    QLabel* m_labelPosRemain;         // 显示剩余距离

    // UI - 多轴位置控制面板
    QGroupBox* m_multiAxisGroup;
    QDoubleSpinBox* m_multiTargetPos[10];  // 每个轴的目标位置
    QDoubleSpinBox* m_multiSpeed[10];      // 每个轴的速度
    QCheckBox* m_multiActive[10];          // 每个轴的启用勾选框
    QLabel* m_multiAxisStatusLabel[10];    // 每个轴的状态显示
    QPushButton* m_btnMultiRun;            // 多轴运行按钮
    QLabel* m_labelMultiStatus;            // 多轴状态显示

    // UI - 使能/失能
    QPushButton* m_btnEnable;
    QPushButton* m_btnDisable;

    // UI - 命令状态
    QLabel* m_labelCmdStatus;

    // UI - 信息显示
    QLabel* m_labelStatus;
    QLabel* m_labelAxisInfo;
    QLabel* m_labelPosition;
    QLabel* m_labelVelocity;
    QLabel* m_labelTorque;

    // UI - 预设姿势按钮
    QPushButton* m_btnPreset[4];

    // UI - 动作序列队列
    QGroupBox* m_seqGroup;
    QListWidget* m_seqListWidget;
    QCheckBox* m_seqLoopingCheck;
    QPushButton* m_btnSeqAdd;
    QPushButton* m_btnSeqClear;
    QPushButton* m_btnSeqDelete;
    QPushButton* m_btnSeqStart;
    QPushButton* m_btnSeqStop;
    QLabel* m_labelSeqStatus;

    QTimer* m_timer;
    int m_intervalMs;

    int m_currentAxis;
    bool m_paused;
    bool m_shmConnected;

    // 序列队列状态缓存
    int m_cachedSeqGroupCount;
    int m_cachedSeqCurrentGroup;
    int m_cachedSeqStatus;

    // ========== UDP 遥测 ==========
    UdpSender* m_udpSender;
    UdpReceiver* m_udpReceiver;

    // UI - UDP 遥测面板
    QGroupBox* m_udpGroup;
    QLineEdit* m_udpHostInput;
    QLineEdit* m_udpPortInput;
    QPushButton* m_btnUdpStart;
    QPushButton* m_btnUdpStop;
    QLabel* m_labelUdpStatus;

    // UI - IMU 数据接收显示
    QLabel* m_labelIMU_d10;
    QLabel* m_labelIMU_d20;
    QLabel* m_labelIMU_d30;
    QLabel* m_labelIMU_d40;
    QPushButton* m_btnResponseMode;
    bool m_responseMode;
    double m_hipRefPos;   // 右髋响应模式参考位置（开启时记录）
    double m_kneeRefPos;  // 右膝响应模式参考位置（开启时记录）
    // IMU 滑动窗口滤波（5 点均值）
    double m_bufD20[5];        // 右髋采样缓冲区
    double m_bufD30[5];        // 右膝采样缓冲区
    int    m_bufIdx;           // 当前写入位置
    static constexpr int    IMU_WINDOW_SIZE = 5;    // 滑动窗口大小
    static constexpr double IMU_DEADBAND    = 0.5;  // 死区阈值（度），小于此值忽略
    static constexpr double IMU_MIN_CHANGE   = 0.5; // 最小目标变化阈值，避免频繁启停抖动
    // 上次发送的目标位置（用于抖动抑制）
    double m_lastTargetHip;
    double m_lastTargetKnee;

    // 六个旋转关节的轴索引常量
    static const int IDX_RIGHT_ANKLE = 0;  // 轴1
    static const int IDX_RIGHT_KNEE  = 2;  // 轴3
    static const int IDX_RIGHT_HIP   = 3;  // 轴4
    static const int IDX_LEFT_HIP    = 4;  // 轴5
    static const int IDX_LEFT_KNEE   = 6;  // 轴7
    static const int IDX_LEFT_ANKLE  = 8;  // 轴9
};


#endif // MAINWINDOW_H
