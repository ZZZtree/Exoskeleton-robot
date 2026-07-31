#ifndef UDP_SENDER_H
#define UDP_SENDER_H

#include <QObject>
#include <QUdpSocket>
#include <QHostAddress>
#include <QTimer>

/**
 * @brief UDP 实时角度发送器
 *
 * 定时向右髋(idx3)、右膝(idx2)、右踝(idx0)三个电机角度
 * 通过 UDP 发送到目标地址 172.20.15.30
 *
 * 数据格式：timestamp_ms,right_hip,right_knee,right_ankle
 *   - timestamp_ms: 毫秒级时间戳（程序启动后累计）
 *   - right_hip:    右髋角度（度，轴4 / idx3）
 *   - right_knee:   右膝角度（度，轴3 / idx2）
 *   - right_ankle:  右踝角度（度，轴1 / idx0）
 */
class UdpSender : public QObject
{
    Q_OBJECT

public:
    explicit UdpSender(QObject* parent = nullptr);
    ~UdpSender();

    /**
     * @brief 设置目标地址和端口
     */
    void setTarget(const QString& host, quint16 port);

    /**
     * @brief 启动/停止定时发送
     */
    void start(int intervalMs = 50);
    void stop();

    /**
     * @brief 获取发送状态
     */
    bool isRunning() const { return m_running; }

    /**
     * @brief 发送原始数据到目标地址
     */
    void sendRaw(const QByteArray& data);

public slots:
    /**
     * @brief 发送六个旋转电机的实时角度
     * @param rHip  右髋角度（度）
     * @param rKnee 右膝角度（度）
     * @param rAnkle 右踝角度（度）
     * @param lHip  左髋角度（度）
     * @param lKnee 左膝角度（度）
     * @param lAnkle 左踝角度（度）
     */
    void sendAngles(double rHip, double rKnee, double rAnkle,
                    double lHip, double lKnee, double lAnkle);

signals:
    void sendSucceeded();
    void sendFailed(const QString& error);

private:
    QUdpSocket* m_socket;
    QHostAddress m_targetHost;
    quint16 m_targetPort;
    QTimer* m_timer;
    bool m_running;
    qint64 m_startTime;  // 启动时刻（ms），用于计算相对时间戳
};

#endif // UDP_SENDER_H