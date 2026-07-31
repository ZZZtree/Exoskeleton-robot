#ifndef UDP_RECEIVER_H
#define UDP_RECEIVER_H

#include <QObject>
#include <QUdpSocket>
#include <QHostAddress>

/**
 * @brief UDP IMU 增量数据接收器
 *
 * 绑定本地端口，接收对端 IMU 发送的 d10,d20,d30,d40 格式数据。
 * 数据格式：4 个浮点数，逗号分隔，表示 4 路 IMU 的 X 轴增量。
 *
 * 例: -1.234,5.678,0.012,-7.890
 */
class UdpReceiver : public QObject
{
    Q_OBJECT

public:
    explicit UdpReceiver(QObject* parent = nullptr);
    ~UdpReceiver();

    /**
     * @brief 绑定端口并开始监听
     * @param port 本地端口
     * @return true 成功，false 失败
     */
    bool start(quint16 port);

    /**
     * @brief 停止监听
     */
    void stop();

    /**
     * @brief 是否正在监听
     */
    bool isRunning() const { return m_running; }

signals:
    /**
     * @brief 收到 4 路 IMU 增量数据
     * @param d10 IMU 0x10 的 ΔX
     * @param d20 IMU 0x20 的 ΔX
     * @param d30 IMU 0x30 的 ΔX
     * @param d40 IMU 0x40 的 ΔX
     */
    void imuDataReceived(double d10, double d20, double d30, double d40);

    /**
     * @brief 接收错误
     */
    void error(const QString& message);

private slots:
    void onDatagramReceived();

private:
    QUdpSocket* m_socket;
    bool m_running;
};

#endif // UDP_RECEIVER_H