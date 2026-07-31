#include "udp_sender.h"
#include <QByteArray>
#include <QDateTime>
#include <QDebug>
#include <cmath>

UdpSender::UdpSender(QObject* parent)
    : QObject(parent)
    , m_socket(nullptr)
    , m_targetHost(QHostAddress::Any)
    , m_targetPort(0)
    , m_timer(nullptr)
    , m_running(false)
    , m_startTime(0)
{
}

UdpSender::~UdpSender()
{
    stop();
    if (m_socket) {
        m_socket->close();
        delete m_socket;
        m_socket = nullptr;
    }
}

void UdpSender::setTarget(const QString& host, quint16 port)
{
    m_targetHost = QHostAddress(host);
    m_targetPort = port;
    qDebug() << "[UdpSender] 目标地址:" << host << "端口:" << port;
}

void UdpSender::start(int intervalMs)
{
    if (m_running) {
        qDebug() << "[UdpSender] 已经在运行中";
        return;
    }

    // 创建 UDP socket
    if (!m_socket) {
        m_socket = new QUdpSocket(this);
    }

    if (m_targetHost.isNull() || m_targetPort == 0) {
        qWarning() << "[UdpSender] 目标地址未设置，无法启动";
        emit sendFailed("目标地址未设置");
        return;
    }

    m_startTime = QDateTime::currentMSecsSinceEpoch();
    m_running = true;

    qDebug() << "[UdpSender] UDP 发送器已启动，"
             << "目标:" << m_targetHost.toString()
             << "端口:" << m_targetPort
             << "间隔:" << intervalMs << "ms";
}

void UdpSender::stop()
{
    m_running = false;
    if (m_timer) {
        m_timer->stop();
    }
    qDebug() << "[UdpSender] UDP 发送器已停止";
}

void UdpSender::sendAngles(double rHip, double rKnee, double rAnkle,
                            double lHip, double lKnee, double lAnkle)
{
    if (!m_running || !m_socket) return;

    if (m_targetHost.isNull() || m_targetPort == 0) return;

    // 计算相对时间戳（ms）
    qint64 timestamp = QDateTime::currentMSecsSinceEpoch() - m_startTime;

    // 数据格式：timestamp,右髋,右膝,右踝,左髋,左膝,左踝
    QByteArray datagram = QByteArray::number(timestamp) + ","
                        + QByteArray::number(rHip,   'f', 3) + ","
                        + QByteArray::number(rKnee,  'f', 3) + ","
                        + QByteArray::number(rAnkle, 'f', 3) + ","
                        + QByteArray::number(lHip,   'f', 3) + ","
                        + QByteArray::number(lKnee,  'f', 3) + ","
                        + QByteArray::number(lAnkle, 'f', 3);

    qint64 bytesSent = m_socket->writeDatagram(datagram, m_targetHost, m_targetPort);

    if (bytesSent < 0) {
        qWarning() << "[UdpSender] 发送失败:" << m_socket->errorString();
        emit sendFailed(m_socket->errorString());
    } else if (bytesSent != datagram.size()) {
        qWarning() << "[UdpSender] 部分发送:" << bytesSent
                   << "/" << datagram.size();
    }
    // 发送成功不打印日志，避免刷屏
}

void UdpSender::sendRaw(const QByteArray& data)
{
    if (!m_socket || m_targetHost.isNull() || m_targetPort == 0) return;

    qint64 bytesSent = m_socket->writeDatagram(data, m_targetHost, m_targetPort);
    if (bytesSent < 0) {
        qWarning() << "[UdpSender] sendRaw 失败:" << m_socket->errorString();
    }
}
