#include "chartwidget.h"
#include "MotorMonitorData.h"
#include <QtMath>

const QString ChartWidget::AXIS_NAMES[10] = {
    "轴1(右踝)", "轴2(右小腿)", "轴3(右膝)", "轴4(右髋)", "轴5(左髋)",
    "轴6(左大腿)", "轴7(左膝)", "轴8(左小腿)", "轴9(左踝)", "轴10(右大腿)"
};

ChartWidget::ChartWidget(const QString& title,
                         const QString& unitRotary,
                         const QString& unitLinear,
                         int maxDataPoints,
                         QWidget* parent)
    : QWidget(parent)
    , m_chart(nullptr)
    , m_chartView(nullptr)
    , m_series(nullptr)
    , m_axisX(nullptr)
    , m_axisY(nullptr)
    , m_axisY2(nullptr)
    , m_maxDataPoints(maxDataPoints)
    , m_pointCount(0)
    , m_currentAxis(-1)
    , m_currentAxisType(-1)
    , m_unitRotary(unitRotary)
    , m_unitLinear(unitLinear)
{
    setupChart(title, unitRotary, unitLinear);
}

ChartWidget::~ChartWidget()
{
}

void ChartWidget::setupChart(const QString& title,
                             const QString& unitRotary,
                             const QString& unitLinear)
{
    m_chart = new QChart();
    m_chart->setTitle(title);
    m_chart->setAnimationOptions(QChart::NoAnimation);
    m_chart->legend()->setVisible(false);
    m_chart->setMargins(QMargins(0, 0, 0, 0));
    m_chart->setBackgroundRoundness(0);

    // 单条曲线
    m_series = new QLineSeries();
    m_series->setName(title);
    m_series->setPen(QPen(QColor("#3498DB"), 2.0));
    m_chart->addSeries(m_series);

    // X轴
    m_axisX = new QValueAxis();
    m_axisX->setTitleText("采样点");
    m_axisX->setLabelFormat("%d");
    m_axisX->setRange(0, m_maxDataPoints);
    m_axisX->setGridLineVisible(true);
    m_axisX->setTickCount(10);
    m_chart->addAxis(m_axisX, Qt::AlignBottom);
    m_series->attachAxis(m_axisX);

    // 左Y轴（旋转电机）
    m_axisY = new QValueAxis();
    m_axisY->setTitleText(title + " (" + unitRotary + ")");
    m_axisY->setLabelFormat("%.1f");
    m_axisY->setGridLineVisible(true);
    m_axisY->setTickCount(8);
    m_axisY->setLinePenColor(QColor("#E74C3C"));
    m_axisY->setLabelsColor(QColor("#E74C3C"));

    // 根据物理量设置默认范围
    if (title == "位置")
        m_axisY->setRange(-180, 180);
    else if (title == "速度")
        m_axisY->setRange(-20, 20);
    else if (title == "力矩")
        m_axisY->setRange(-200, 200);
    else
        m_axisY->setRange(-50, 50);

    m_chart->addAxis(m_axisY, Qt::AlignLeft);

    // 右Y轴（线性电机）
    m_axisY2 = new QValueAxis();
    m_axisY2->setTitleText(title + " (" + unitLinear + ")");
    m_axisY2->setLabelFormat("%.1f");
    m_axisY2->setGridLineVisible(false);
    m_axisY2->setTickCount(8);
    m_axisY2->setLinePenColor(QColor("#9B59B6"));
    m_axisY2->setLabelsColor(QColor("#9B59B6"));

    if (title == "位置")
        m_axisY2->setRange(-40, 40);
    else if (title == "速度")
        m_axisY2->setRange(-350, 350);
    else if (title == "力矩")
        m_axisY2->setRange(-6000, 6000);
    else
        m_axisY2->setRange(-500, 500);

    m_chart->addAxis(m_axisY2, Qt::AlignRight);

    // 默认关联到左Y轴（不关联右Y轴，避免 detach 时警告）
    m_series->attachAxis(m_axisY);
    m_axisY2->setLabelsVisible(false);

    // 零轴参考线 - 使用 QChart::addAxis 添加的轴，attach 到两个轴
    QLineSeries* zeroLine = new QLineSeries();
    zeroLine->setPen(QPen(QColor("#888888"), 1, Qt::DashLine));
    zeroLine->append(0, 0);
    zeroLine->append(m_maxDataPoints, 0);
    m_chart->addSeries(zeroLine);
    zeroLine->attachAxis(m_axisX);
    zeroLine->attachAxis(m_axisY);
    zeroLine->attachAxis(m_axisY2);

    // 图表视图
    m_chartView = new QChartView(m_chart);
    m_chartView->setRenderHint(QPainter::Antialiasing);
    m_chartView->setRubberBand(QChartView::RectangleRubberBand);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_chartView);
    setLayout(layout);
}

void ChartWidget::setAxis(int axisIndex, int axisType)
{
    if (axisIndex < 0 || axisIndex > 9) return;

    m_currentAxis = axisIndex;
    m_currentAxisType = axisType;

    // 更新标题
    QString typeStr = (axisType == MOTOR_TYPE_ROTARY) ? m_unitRotary : m_unitLinear;
    m_chart->setTitle(QString("%1 - %2 (%3)")
                          .arg(AXIS_NAMES[axisIndex])
                          .arg(m_series->name())
                          .arg(typeStr));

    // 安全切换Y轴：先 detach 当前可能 attach 的轴，再 attach 正确的轴
    // 使用 tryDetach 方式避免 "Axis not attached to series" 警告
    QList<QAbstractAxis*> attachedAxes = m_series->attachedAxes();
    for (QAbstractAxis* ax : attachedAxes) {
        if (ax != m_axisX) {
            m_series->detachAxis(ax);
        }
    }

    if (axisType == MOTOR_TYPE_ROTARY)
    {
        m_axisY->setLabelsVisible(true);
        m_axisY2->setLabelsVisible(false);
        m_series->attachAxis(m_axisY);
    }
    else
    {
        m_axisY2->setLabelsVisible(true);
        m_axisY->setLabelsVisible(false);
        m_series->attachAxis(m_axisY2);
    }

    clearData();
}

void ChartWidget::addDataPoint(double value, int axisType)
{
    m_series->append(m_pointCount, value);

    if (m_series->count() > m_maxDataPoints)
    {
        m_series->remove(0);
    }

    if (m_pointCount > m_maxDataPoints)
    {
        m_axisX->setRange(m_pointCount - m_maxDataPoints, m_pointCount);
    }

    m_pointCount++;
}

void ChartWidget::clearData()
{
    m_series->clear();
    m_pointCount = 0;
    m_axisX->setRange(0, (double)m_maxDataPoints);
}

void ChartWidget::autoAdjustYAxis()
{
    QValueAxis* targetAxis = nullptr;
    if (m_currentAxisType == MOTOR_TYPE_ROTARY)
        targetAxis = m_axisY;
    else
        targetAxis = m_axisY2;

    if (!targetAxis) return;

    double maxAbs = 0.0;
    const auto& points = m_series->points();
    for (const QPointF& pt : points)
    {
        double absVal = qAbs(pt.y());
        if (absVal > maxAbs) maxAbs = absVal;
    }

    if (maxAbs > 0.0)
    {
        double margin = maxAbs * 0.1;
        if (margin < 0.1) margin = 0.1;
        double range = maxAbs + margin;
        targetAxis->setRange(-range, range);
    }
}
