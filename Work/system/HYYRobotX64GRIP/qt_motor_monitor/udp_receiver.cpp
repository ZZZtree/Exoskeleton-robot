#include "udp_receiver.h"
#include <QDebug>
#include <QByteArray>
#include <QList>

UdpReceiver::UdpReceiver(QObject* parent)
    : QObject(parent)
    , m_socket(nullptr)
    , m_running(false)
{
}

UdpReceiver::~UdpReceiver()
{
    stop();
}

bool UdpReceiver::start(quint16 port)
{
    if (m_running) {
        qDebug() << "[UdpReceiver] 已在监听中";
        return true;
    }

    m_socket = new QUdpSocket(this);

    // 绑定到所有网络接口的指定端口，允许多客户端共用端口
    if (!m_socket->bind(QHostAddress::Any, port, QUdpSocket::ShareAddress)) {
        qWarning() << "[UdpReceiver] 绑定端口" << port << "失败:" << m_socket->errorString();
        delete m_socket;
        m_socket = nullptr;
        emit error(QString("绑定端口 %1 失败: %2").arg(port).arg(m_socket->errorString()));
        return false;
    }

    connect(m_socket, &QUdpSocket::readyRead, this, &UdpReceiver::onDatagramReceived);

    m_running = true;
    qDebug() << "[UdpReceiver] 开始监听端口" << port;
    return true;
}

void UdpReceiver::stop()
{
    if (m_socket) {
        m_socket->close();
        delete m_socket;
        m_socket = nullptr;
    }
    m_running = false;
    qDebug() << "[UdpReceiver] 已停止监听";
}

void UdpReceiver::onDatagramReceived()
{
    while (m_socket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize((int)m_socket->pendingDatagramSize());
        qint64 len = m_socket->readDatagram(datagram.data(), datagram.size());

        if (len <= 0) continue;

        QString data = QString::fromUtf8(datagram.left((int)len)).trimmed();
        if (data.isEmpty()) continue;

        // 解析 CSV 行: d10,d20,d30,d40
        QStringList parts = data.split(',');
        if (parts.size() != 4) {
            qDebug() << "[UdpReceiver] 收到的数据格式错误，期望4个值，实际:" << parts.size()
                     << "内容:" << data;
            continue;
        }

        bool ok[4] = {false, false, false, false};
        double d10 = parts[0].toDouble(&ok[0]);
        double d20 = parts[1].toDouble(&ok[1]);
        double d30 = parts[2].toDouble(&ok[2]);
        double d40 = parts[3].toDouble(&ok[3]);

        if (!ok[0] || !ok[1] || !ok[2] || !ok[3]) {
            qDebug() << "[UdpReceiver] 解析浮点数失败:" << data;
            continue;
        }

        emit imuDataReceived(d10, d20, d30, d40);
    }
}