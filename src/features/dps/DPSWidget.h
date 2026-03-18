// REVIEW BEFORE BETA: all implementations inline — split to .h/.cpp pair. Also: emoji L97, hardcoded fonts, hardcoded colors.
#pragma once

#include <QWidget>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QMouseEvent>
#include <QPoint>

#include "CombatTracker.h"

/**
 * @brief Transparent overlay widget displaying DPS meter
 */
class DPSWidget : public QWidget
{
    Q_OBJECT
    
public:
    explicit DPSWidget(CombatTracker* tracker, QWidget* parent = nullptr);
    
    // Display options
    void setShowGraph(bool show) { m_showGraph = show; update(); }
    void setShowPeak(bool show) { m_showPeak = show; update(); }
    void setPosition(const QPoint& pos) { move(pos); }
    
protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    
private slots:
    void onStatsUpdated();
    void onCombatStateChanged(bool inCombat);
    
private:
    void drawMeter(QPainter& painter);
    void drawGraph(QPainter& painter, const QRect& rect);
    QString formatDPS(double dps) const;
    QString formatDuration(int seconds) const;
    QColor getDPSColor(double dps) const;
    
    CombatTracker* m_tracker;
    
    bool m_showGraph = true;
    bool m_showPeak = true;
    bool m_dragging = false;
    QPoint m_dragOffset;
    
    // Appearance
    QColor m_bgColor{20, 20, 20, 220};
    QColor m_borderColor{60, 60, 60};
    QColor m_textColor{224, 224, 224};
    QColor m_accentColor{192, 156, 87};  // GW2 gold
};

// Implementation
inline DPSWidget::DPSWidget(CombatTracker* tracker, QWidget* parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool)
    , m_tracker(tracker)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setMouseTracking(true);
    
    setFixedSize(200, m_showGraph ? 120 : 70);
    
    connect(m_tracker, &CombatTracker::statsUpdated, this, &DPSWidget::onStatsUpdated);
    connect(m_tracker, &CombatTracker::combatStateChanged, this, &DPSWidget::onCombatStateChanged);
}

inline void DPSWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    drawMeter(painter);
}

inline void DPSWidget::drawMeter(QPainter& painter)
{
    const CombatStats& stats = m_tracker->stats();
    
    // Background
    QRect bgRect(0, 0, width(), height());
    painter.setBrush(m_bgColor);
    painter.setPen(QPen(m_borderColor, 1));
    painter.drawRoundedRect(bgRect, 8, 8);
    
    int y = 8;
    
    // Title bar
    painter.setPen(m_accentColor);
    painter.setFont(QFont("Segoe UI", 9, QFont::Bold));
    QString title = m_tracker->isInCombat() ? "⚔️ IN COMBAT" : "DPS Meter";
    painter.drawText(8, y + 12, title);
    y += 20;
    
    // Current DPS (large)
    painter.setPen(getDPSColor(stats.currentDPS));
    painter.setFont(QFont("Segoe UI", 20, QFont::Bold));
    painter.drawText(8, y + 25, formatDPS(stats.currentDPS));
    y += 35;
    
    // Duration and Peak
    painter.setPen(m_textColor);
    painter.setFont(QFont("Segoe UI", 9));
    QString info = QString("Duration: %1").arg(formatDuration(stats.duration));
    if (m_showPeak && stats.peakDPS > 0) {
        info += QString("  Peak: %1").arg(formatDPS(stats.peakDPS));
    }
    painter.drawText(8, y + 10, info);
    y += 18;
    
    // DPS Graph
    if (m_showGraph && !stats.dpsHistory.isEmpty()) {
        QRect graphRect(8, y, width() - 16, 35);
        drawGraph(painter, graphRect);
    }
}

inline void DPSWidget::drawGraph(QPainter& painter, const QRect& rect)
{
    const QList<double>& history = m_tracker->stats().dpsHistory;
    if (history.isEmpty()) return;
    
    // Find max for scaling
    double maxDPS = 1;
    for (double d : history) {
        if (d > maxDPS) maxDPS = d;
    }
    
    // Draw background
    painter.setBrush(QColor(30, 30, 30, 150));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(rect, 4, 4);
    
    // Draw graph line
    if (history.size() < 2) return;
    
    QPainterPath path;
    int count = history.size();
    double xStep = static_cast<double>(rect.width()) / (count - 1);
    
    for (int i = 0; i < count; i++) {
        double x = rect.left() + i * xStep;
        double yRatio = history[i] / maxDPS;
        double y = rect.bottom() - (yRatio * rect.height());
        
        if (i == 0) {
            path.moveTo(x, y);
        } else {
            path.lineTo(x, y);
        }
    }
    
    painter.setPen(QPen(m_accentColor, 2));
    painter.drawPath(path);
}

inline QString DPSWidget::formatDPS(double dps) const
{
    if (dps >= 1000000) {
        return QString("%1M").arg(dps / 1000000, 0, 'f', 2);
    } else if (dps >= 1000) {
        return QString("%1K").arg(dps / 1000, 0, 'f', 1);
    } else {
        return QString::number(static_cast<int>(dps));
    }
}

inline QString DPSWidget::formatDuration(int seconds) const
{
    int mins = seconds / 60;
    int secs = seconds % 60;
    return QString("%1:%2").arg(mins).arg(secs, 2, 10, QChar('0'));
}

inline QColor DPSWidget::getDPSColor(double dps) const
{
    // Color gradient based on DPS
    if (dps < 5000) return QColor(150, 150, 150);      // Gray - low
    if (dps < 15000) return QColor(100, 200, 100);     // Green - decent
    if (dps < 25000) return QColor(200, 200, 100);     // Yellow - good
    if (dps < 35000) return QColor(255, 165, 0);       // Orange - great
    return QColor(255, 100, 100);                       // Red - excellent
}

inline void DPSWidget::onStatsUpdated()
{
    update();
}

inline void DPSWidget::onCombatStateChanged(bool inCombat)
{
    Q_UNUSED(inCombat);
    update();
}

inline void DPSWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragOffset = event->pos();
    }
}

inline void DPSWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging) {
        move(mapToParent(event->pos() - m_dragOffset));
    }
}
