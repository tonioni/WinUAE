#include <QApplication>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <QPalette>
#include <QToolTip>
#include <algorithm>

#include "sysconfig.h"
#include "sysdeps.h"
#include "od-unix/qt/statusbar.h"

static QFont statusFont()
{
    return QFontDatabase::systemFont(QFontDatabase::GeneralFont);
}
int winUaeQtStatusbarHeight()
{
    return QFontMetrics(statusFont()).height() + 8;
}
void winUaeQtPaintStatusbar(unsigned int *pixels, int width, int height,
    float scale, const statusbar_state &state, const TCHAR *message,
    statusbar_rect *rects, statusbar_item *items)
{
    QImage image(reinterpret_cast<uchar *>(pixels), width, height,
        width * int(sizeof(unsigned int)), QImage::Format_ARGB32);
    const QPalette palette = QApplication::palette();
    image.fill(palette.color(QPalette::Window));
    QPainter painter(&image);
    painter.scale(scale, scale);
    painter.setFont(statusFont());
    const QFontMetrics metrics(painter.font());
    const float logicalWidth = width / scale;
    const float logicalHeight = height / scale;
    float widths[STATUSBAR_ITEMS];
    float total = 0;
    // Stable widths, independent of LED activity and changing counter text.
    const char *samples[STATUSBAR_ITEMS] = {
        "SND: +100%", "CPU: 100%", "FPS: 999.9", "999i", "Power", "Caps",
        "CD", "HD", "N", "00", "00", "00", "00"
    };
    for (int i = 0; i < STATUSBAR_ITEMS; ++i) {
        widths[i] = std::max(statusbar_width(i, state.ppc, state.x86 != 0),
            metrics.horizontalAdvance(QString::fromLatin1(samples[i])) + 12);
        if (statusbar_led(i) == LED_CPU && (state.ppc || state.x86)) {
            QString sample = QStringLiteral("68k: 100%");
            if (state.ppc) sample.prepend(QStringLiteral("PPC: STOP "));
            if (state.x86) sample.prepend(QStringLiteral("x86: STOP "));
            widths[i] = std::max(widths[i], float(metrics.horizontalAdvance(sample) + 12));
        }
        total += widths[i];
    }
    float x = logicalWidth - total;
    if (message && *message && x > 8) {
        painter.setPen(palette.color(QPalette::WindowText));
        const QRectF area(6, 0, x - 12, logicalHeight);
        painter.drawText(area, Qt::AlignVCenter | Qt::AlignLeft,
            metrics.elidedText(QString::fromUtf8(message), Qt::ElideRight, std::max(0, int(area.width()))));
    }
    for (int i = 0; i < STATUSBAR_ITEMS; ++i) {
        int led = statusbar_led(i);
        if (led == LED_DF3 && state.gui.md >= 0 && state.gui.drives[3].drive_disabled)
            led = LED_MD;
        statusbar_format(state, led, statusbar_led_state(state, led), &items[i]);
        rects[i] = {x * scale, 0, widths[i] * scale, float(height)};
        QRectF cell(x, 0, widths[i], logicalHeight);
        painter.setPen(palette.color(QPalette::Mid));
        painter.drawLine(QPointF(x, 4), QPointF(x, logicalHeight - 4));
        QColor ink = palette.color((items[i].flags & STATUSBAR_ON)
            ? QPalette::Active : QPalette::Disabled, QPalette::WindowText);
        if (items[i].flags & STATUSBAR_ON) {
            if (items[i].flags & STATUSBAR_WRITING) ink = QColor(204, 0, 0);
            else if (items[i].flags & STATUSBAR_PLAYING) ink = QColor(0, 160, 0);
        }
        painter.setPen(ink);
        cell.adjust(5, 0, -5, 0);
        painter.save();
        painter.setClipRect(cell);
        painter.drawText(cell, Qt::AlignVCenter |
            ((items[i].flags & STATUSBAR_CENTER) ? Qt::AlignHCenter : Qt::AlignLeft),
            QString::fromUtf8(items[i].text));
        painter.restore();
        x += widths[i];
    }
    painter.setPen(palette.color(QPalette::Mid));
    painter.drawLine(QPointF(0, 0), QPointF(logicalWidth, 0));
}
void winUaeQtStatusbarTooltip(const char *text, int x, int y)
{
    if (text && *text)
        QToolTip::showText(QPoint(x, y), QString::fromUtf8(text));
    else
        QToolTip::hideText();
}
