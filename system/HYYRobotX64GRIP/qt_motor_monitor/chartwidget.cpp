#include "chartwidget.h"
#include "MotorMonitorData.h"
#include <QtMath>

const QColor ChartWidget::COLORS[4] = {
    QColor("#E74C3C"),  // 红色 - 轴1
    QColor("#2ECC71"),  // 绿色 - 轴2
    QColor("#3498DB"),  // 蓝色 - 轴3
    QColor("#9B59B6")   // 紫色 - 轴4
};

const QString ChartWidget::AXIS_NAMES[4] = {
    "轴1", "轴2", "轴3", "轴4"
};

ChartWidget::ChartWidget(const QString& title,
                         const QString& yAxisTitle,
                         const QString& yAxisUnitLeft,
                         const QString& yAxisUnitRight,
                         int maxDataPoints,
                         QWidget* parent)
    : QWidget(parent)
    , m_chart(nullptr)
    , m_chartView(nullptr)
    , m_axisX(nullptr)
    , m_axisY(nullptr)
    , m_axisY2(nullptr)
    , m_maxDataPoints(maxDataPoints)
    , m_pointCount(0)
{
    setupChart(title, yAxisTitle, yAxisUnitLeft, yAxisUnitRight);
}

ChartWidget::~ChartWidget()
{
}

void ChartWidget::setupChart(const QString& title,
                             const QString& yAxisTitle,
                             const QString& yAxisUnitLeft,
                             const QString& yAxisUnitRight)
{
    // 创建图表
    m_chart = new QChart();
    m_chart->setTitle(title);
    m_chart->setAnimationOptions(QChart::NoAnimation);
    m_chart->legend()->setVisible(true);
    m_chart->legend()->setAlignment(Qt::AlignBottom);
    m_chart->setMargins(QMargins(0, 0, 0, 0));
    m_chart->setBackgroundRoundness(0);

    // 创建四个电机的曲线
    for (int i = 0; i < 4; i++)
    {
        m_series[i] = new QLineSeries();
        m_series[i]->setName(AXIS_NAMES[i]);
        m_series[i]->setColor(COLORS[i]);
        m_series[i]->setPen(QPen(COLORS[i], 1.5));
        m_chart->addSeries(m_series[i]);
    }

    // 创建X轴（采样点索引）
    m_axisX = new QValueAxis();
    m_axisX->setTitleText("采样点");
    m_axisX->setLabelFormat("%d");
    m_axisX->setRange(0, m_maxDataPoints);
    m_axisX->setGridLineVisible(true);
    m_axisX->setTickCount(10);
    m_chart->addAxis(m_axisX, Qt::AlignBottom);

    // ========== 左Y轴（旋转电机） ==========
    m_axisY = new QValueAxis();
    m_axisY->setTitleText(yAxisTitle + " (" + yAxisUnitLeft + ")");
    m_axisY->setLabelFormat("%.1f");
    m_axisY->setGridLineVisible(true);
    m_axisY->setTickCount(8);
    m_axisY->setLinePenColor(QColor("#E74C3C"));  // 红色调
    m_axisY->setLabelsColor(QColor("#E74C3C"));
    m_chart->addAxis(m_axisY, Qt::AlignLeft);

    // 旋转电机初始范围
    if (yAxisUnitLeft == "deg")
        m_axisY->setRange(-180, 180);     // 位置：±180°
    else if (yAxisUnitLeft == "deg/s")
        m_axisY->setRange(-20, 20);       // 速度：±20 deg/s
    else if (yAxisUnitLeft == "Nm")
        m_axisY->setRange(-200.0, 200.0); // 力矩：±200 Nm（轴1输出端额定194.9Nm，轴2/3输出端额定68.2Nm）
    else
        m_axisY->setRange(-50, 50);       // 加速度：±50 deg/s²（保留）

    // 所有轴关联到X轴
    for (int i = 0; i < 4; i++)
    {
        m_series[i]->attachAxis(m_axisX);
    }

    // 轴1~3（旋转电机）关联到左Y轴
    for (int i = 0; i < 3; i++)
    {
        m_series[i]->attachAxis(m_axisY);
    }

    // ========== 右Y轴（线性电机） ==========
    // 如果右轴单位为空，则不创建右Y轴（例如力矩图表中轴4数据已禁用）
    if (!yAxisUnitRight.isEmpty())
    {
        m_axisY2 = new QValueAxis();
        m_axisY2->setTitleText(yAxisTitle + " (" + yAxisUnitRight + ")");
        m_axisY2->setLabelFormat("%.1f");
        m_axisY2->setGridLineVisible(false);
        m_axisY2->setTickCount(8);
        m_axisY2->setLinePenColor(QColor("#9B59B6"));  // 紫色调
        m_axisY2->setLabelsColor(QColor("#9B59B6"));
        m_chart->addAxis(m_axisY2, Qt::AlignRight);

        // 线性电机初始范围（最大行程72mm，最大速度300mm/s）
        if (yAxisUnitRight == "mm")
            m_axisY2->setRange(-40, 40);      // 位置：±40mm（留有余量）
        else if (yAxisUnitRight == "mm/s")
            m_axisY2->setRange(-350, 350);    // 速度：±350 mm/s（最大300mm/s，留有余量）
        else if (yAxisUnitRight == "N")
            m_axisY2->setRange(-6000, 6000);  // 力矩/力：±6000 N（线性电机最大推力6000N）
        else
            m_axisY2->setRange(-500, 500);    // 加速度：±500 mm/s²（保留）

        // 轴4（线性电机）关联到右Y轴
        m_series[3]->attachAxis(m_axisY2);
    }

    // ========== 零轴参考线（y=0） ==========
    // 在左Y轴上添加一条 y=0 的水平参考线，便于观察数值是否偏离零点
    QLineSeries* zeroLine = new QLineSeries();
    zeroLine->setName("零轴");
    zeroLine->setPen(QPen(QColor("#888888"), 1, Qt::DashLine));
    zeroLine->append(0, 0);
    zeroLine->append(m_maxDataPoints, 0);
    m_chart->addSeries(zeroLine);
    zeroLine->attachAxis(m_axisX);
    zeroLine->attachAxis(m_axisY);

    // 创建图表视图
    m_chartView = new QChartView(m_chart);
    m_chartView->setRenderHint(QPainter::Antialiasing);
    m_chartView->setRubberBand(QChartView::RectangleRubberBand);

    // 布局
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_chartView);
    setLayout(layout);
}

void ChartWidget::addDataPoint(int axisIndex, double value, int axisType)
{
    if (axisIndex < 0 || axisIndex > 3) return;

    // 添加数据点，横轴为采样点索引
    m_series[axisIndex]->append(m_pointCount, value);

    // 如果数据点超过最大数量，移除旧数据
    if (m_series[axisIndex]->count() > m_maxDataPoints)
    {
        m_series[axisIndex]->remove(0);
    }

    // 更新X轴范围（滚动效果，显示最近 m_maxDataPoints 个点）
    if (m_pointCount > m_maxDataPoints)
    {
        m_axisX->setRange(m_pointCount - m_maxDataPoints, m_pointCount);
    }

    m_pointCount++;
}

void ChartWidget::clearData()
{
    for (int i = 0; i < 4; i++)
    {
        m_series[i]->clear();
    }
    m_pointCount = 0;
    m_axisX->setRange(0, (double)m_maxDataPoints);
}

void ChartWidget::setYAxisRange(double min, double max)
{
    m_axisY->setRange(min, max);
}

void ChartWidget::setYAxisRangeRight(double min, double max)
{
    if (m_axisY2)
        m_axisY2->setRange(min, max);
}

void ChartWidget::autoAdjustYAxis()
{
    // 左Y轴：轴0~2（旋转电机），范围始终关于0对称
    double maxAbs = 0.0;
    for (int i = 0; i < 3; i++)
    {
        const auto& points = m_series[i]->points();
        for (const QPointF& pt : points)
        {
            double absVal = qAbs(pt.y());
            if (absVal > maxAbs) maxAbs = absVal;
        }
    }
    if (maxAbs > 0.0)
    {
        double margin = maxAbs * 0.1;
        if (margin < 0.1) margin = 0.1;
        double range = maxAbs + margin;
        m_axisY->setRange(-range, range);
    }

    // 右Y轴：轴3（线性电机），仅在右Y轴存在时调整，范围始终关于0对称
    if (m_axisY2)
    {
        maxAbs = 0.0;
        const auto& points = m_series[3]->points();
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
            m_axisY2->setRange(-range, range);
        }
    }
}
