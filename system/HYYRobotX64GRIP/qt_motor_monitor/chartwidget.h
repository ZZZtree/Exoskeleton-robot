#ifndef CHARTWIDGET_H
#define CHARTWIDGET_H

#include <QWidget>
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <QtCharts/QDateTimeAxis>
#include <QtCharts/QSplineSeries>
#include <QVBoxLayout>
#include <QLabel>

QT_CHARTS_USE_NAMESPACE

/**
 * @brief 电机曲线图表组件
 *
 * 显示单个物理量（位置/速度/加速度）的四条曲线（四个电机）
 */
class ChartWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ChartWidget(const QString& title,
                         const QString& yAxisTitle,
                         const QString& yAxisUnitLeft,
                         const QString& yAxisUnitRight,
                         int maxDataPoints = 1000,
                         QWidget* parent = nullptr);

    ~ChartWidget();

    /**
     * @brief 添加数据点
     * @param axisIndex 电机索引 (0, 1, 2, 3)
     * @param value 数值
     * @param axisType 电机类型（0=旋转左轴，1=线性右轴）
     */
    void addDataPoint(int axisIndex, double value, int axisType);

    /**
     * @brief 清除所有数据
     */
    void clearData();

    /**
     * @brief 设置左Y轴范围（旋转电机）
     */
    void setYAxisRange(double min, double max);

    /**
     * @brief 设置右Y轴范围（线性电机）
     */
    void setYAxisRangeRight(double min, double max);

    /**
     * @brief 自动调整Y轴范围
     */
    void autoAdjustYAxis();

private:
    void setupChart(const QString& title, const QString& yAxisTitle,
                    const QString& yAxisUnitLeft, const QString& yAxisUnitRight);

    QChart* m_chart;
    QChartView* m_chartView;
    QLineSeries* m_series[4];  // 四个电机
    QValueAxis* m_axisX;
    QValueAxis* m_axisY;   // 左Y轴（旋转电机：deg/deg/s/deg/s²）
    QValueAxis* m_axisY2;  // 右Y轴（线性电机：mm/mm/s/mm/s²）

    int m_maxDataPoints;
    int m_pointCount;

    // 电机颜色
    static const QColor COLORS[4];
    static const QString AXIS_NAMES[4];
};

#endif // CHARTWIDGET_H
