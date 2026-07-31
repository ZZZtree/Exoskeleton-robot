#ifndef CHARTWIDGET_H
#define CHARTWIDGET_H

#include <QWidget>
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <QVBoxLayout>
#include <QLabel>

QT_CHARTS_USE_NAMESPACE

/**
 * @brief 单个物理量的曲线图表组件
 *
 * 显示单个电机的一个物理量（位置/速度/力矩）曲线
 * 支持双Y轴（旋转电机用左轴，线性电机用右轴）
 */
class ChartWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ChartWidget(const QString& title,
                         const QString& unitRotary,
                         const QString& unitLinear,
                         int maxDataPoints = 1000,
                         QWidget* parent = nullptr);

    ~ChartWidget();

    /**
     * @brief 设置当前显示的电机索引
     */
    void setAxis(int axisIndex, int axisType);

    /**
     * @brief 添加数据点
     */
    void addDataPoint(double value, int axisType);

    /**
     * @brief 清除所有数据
     */
    void clearData();

    /**
     * @brief 自动调整Y轴范围
     */
    void autoAdjustYAxis();

    /**
     * @brief 获取当前电机索引
     */
    int currentAxis() const { return m_currentAxis; }

private:
    void setupChart(const QString& title,
                    const QString& unitRotary,
                    const QString& unitLinear);

    QChart* m_chart;
    QChartView* m_chartView;
    QLineSeries* m_series;
    QValueAxis* m_axisX;
    QValueAxis* m_axisY;    // 左Y轴（旋转电机）
    QValueAxis* m_axisY2;   // 右Y轴（线性电机）

    int m_maxDataPoints;
    int m_pointCount;
    int m_currentAxis;
    int m_currentAxisType;

    QString m_unitRotary;
    QString m_unitLinear;

    static const QString AXIS_NAMES[10];
};

#endif // CHARTWIDGET_H
