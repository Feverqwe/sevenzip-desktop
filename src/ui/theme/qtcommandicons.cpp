// Lucide geometry adapted to QPainterPath from icons/{plus,folder-output,check,
// copy,move-right,trash,info,arrow-up,folder-plus,rotate-cw,scissors,
// clipboard-paste,text-cursor-input,panel-left,arrow-left,pencil,archive,folder,
// monitor,network,hard-drive}.svg at
// https://github.com/lucide-icons/lucide/tree/1ef60af944de207e0d5176905a0759ba12e6ee8d
// Copyright (c) 2026 Lucide Icons and Contributors; Feather-derived icons:
// Copyright (c) 2013-present Cole Bemis. ISC/MIT: third_party/lucide/LICENSE.
#include "ui/theme/qtcommandicons.h"
#include <QApplication>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>

namespace {
QPainterPath geometry(QtCommandIcon command) {
    QPainterPath p;
    const auto line = [&p](qreal x, qreal y, qreal toX, qreal toY) {
        p.moveTo(x, y);
        p.lineTo(toX, toY);
    };
    switch (command) {
    case QtCommandIcon::Operations:
        // Project-owned clock glyph for returning to independent operations.
        p.addEllipse(QRectF(3, 3, 18, 18));
        line(12, 7, 12, 12);
        line(12, 12, 16, 14);
        break;
    case QtCommandIcon::Computer:
        p.addRoundedRect(QRectF(2, 3, 20, 14), 2, 2);
        line(8, 21, 16, 21);
        line(12, 17, 12, 21);
        break;
    case QtCommandIcon::Network:
        p.addRoundedRect(QRectF(16, 16, 6, 6), 1, 1);
        p.addRoundedRect(QRectF(2, 16, 6, 6), 1, 1);
        p.addRoundedRect(QRectF(9, 2, 6, 6), 1, 1);
        p.moveTo(5, 16);
        p.lineTo(5, 13);
        p.arcTo(5, 12, 2, 2, 180, -90);
        p.lineTo(18, 12);
        p.arcTo(17, 12, 2, 2, 90, -90);
        p.lineTo(19, 16);
        line(12, 12, 12, 8);
        break;
    case QtCommandIcon::Volumes:
        line(10, 16, 10.01, 16);
        line(6, 16, 6.01, 16);
        line(21.946, 12.013, 2.054, 12.013);
        p.moveTo(2.212, 11.577);
        p.cubicTo(2.072, 11.855, 2, 12.162, 2, 12.473);
        p.lineTo(2, 18);
        p.arcTo(2, 16, 4, 4, 180, 90);
        p.lineTo(20, 20);
        p.arcTo(18, 16, 4, 4, 270, 90);
        p.lineTo(22, 12.473);
        p.cubicTo(22, 12.162, 21.928, 11.855, 21.788, 11.577);
        p.lineTo(18.55, 5.11);
        p.cubicTo(18.21, 4.43, 17.52, 4, 16.76, 4);
        p.lineTo(7.24, 4);
        p.cubicTo(6.48, 4, 5.79, 4.43, 5.45, 5.11);
        p.closeSubpath();
        break;
    case QtCommandIcon::Archive:
        p.addRoundedRect(QRectF(2, 3, 20, 5), 1, 1);
        p.moveTo(4, 8);
        p.lineTo(4, 19);
        p.arcTo(4, 17, 4, 4, 180, 90);
        p.lineTo(18, 21);
        p.arcTo(16, 17, 4, 4, 270, 90);
        p.lineTo(20, 8);
        line(10, 12, 14, 12);
        break;
    case QtCommandIcon::Add:
        line(5, 12, 19, 12);
        line(12, 5, 12, 19);
        break;
    case QtCommandIcon::Extract:
        p.moveTo(2, 7.5);
        p.lineTo(2, 5);
        p.arcTo(2, 3, 4, 4, 180, -90);
        p.lineTo(7.9, 3);
        p.cubicTo(8.58, 3, 9.21, 3.34, 9.59, 3.9);
        p.lineTo(10.4, 5.1);
        p.cubicTo(10.78, 5.66, 11.39, 6, 12.07, 6);
        p.lineTo(20, 6);
        p.arcTo(18, 6, 4, 4, 90, -90);
        p.lineTo(22, 18);
        p.arcTo(18, 16, 4, 4, 0, -90);
        p.lineTo(4, 20);
        p.cubicTo(3.07, 20, 2.26, 19.38, 2, 18.5);
        line(2, 13, 12, 13);
        p.moveTo(5, 10);
        p.lineTo(2, 13);
        p.lineTo(5, 16);
        break;
    case QtCommandIcon::Test:
        p.moveTo(20, 6);
        p.lineTo(9, 17);
        p.lineTo(4, 12);
        break;
    case QtCommandIcon::Copy:
        p.addRoundedRect(QRectF(8, 8, 14, 14), 2, 2);
        p.moveTo(4, 16);
        p.cubicTo(2.9, 16, 2, 15.1, 2, 14);
        p.lineTo(2, 4);
        p.cubicTo(2, 2.9, 2.9, 2, 4, 2);
        p.lineTo(14, 2);
        p.cubicTo(15.1, 2, 16, 2.9, 16, 4);
        break;
    case QtCommandIcon::Cut:
        p.addEllipse(QPointF(6, 6), 3, 3);
        line(8.12, 8.12, 12, 12);
        line(20, 4, 8.12, 15.88);
        p.addEllipse(QPointF(6, 18), 3, 3);
        line(14.8, 14.8, 20, 20);
        break;
    case QtCommandIcon::Paste:
        line(11, 14, 21, 14);
        p.moveTo(16, 4);
        p.lineTo(18, 4);
        p.arcTo(16, 4, 4, 4, 90, -90);
        p.lineTo(20, 7.344);
        p.moveTo(17, 18);
        p.lineTo(21, 14);
        p.lineTo(17, 10);
        p.moveTo(8, 4);
        p.lineTo(6, 4);
        p.arcTo(4, 4, 4, 4, 90, 90);
        p.lineTo(4, 20);
        p.arcTo(4, 18, 4, 4, 180, 90);
        p.lineTo(18, 22);
        p.arcTo(16, 18, 4, 4, 270, 64);
        p.addRoundedRect(QRectF(8, 2, 8, 4), 1, 1);
        break;
    case QtCommandIcon::Rename:
        p.moveTo(12, 20);
        p.lineTo(11, 20);
        p.arcTo(9, 16, 4, 4, 270, -90);
        p.arcTo(5, 16, 4, 4, 0, -90);
        p.lineTo(6, 20);
        p.moveTo(13, 8);
        p.lineTo(20, 8);
        p.arcTo(18, 8, 4, 4, 90, -90);
        p.lineTo(22, 14);
        p.arcTo(18, 12, 4, 4, 0, -90);
        p.lineTo(13, 16);
        p.moveTo(5, 16);
        p.lineTo(4, 16);
        p.arcTo(2, 12, 4, 4, 270, -90);
        p.lineTo(2, 10);
        p.arcTo(2, 8, 4, 4, 180, -90);
        p.lineTo(5, 8);
        p.moveTo(6, 4);
        p.lineTo(7, 4);
        p.arcTo(5, 4, 4, 4, 90, -90);
        p.arcTo(9, 4, 4, 4, 180, -90);
        p.lineTo(12, 4);
        line(9, 6, 9, 18);
        break;
    case QtCommandIcon::Sidebar:
        p.addRoundedRect(QRectF(3, 3, 18, 18), 2, 2);
        line(9, 3, 9, 21);
        break;
    case QtCommandIcon::Back:
        p.moveTo(12, 19);
        p.lineTo(5, 12);
        p.lineTo(12, 5);
        line(19, 12, 5, 12);
        break;
    case QtCommandIcon::Forward:
        return QTransform(-1, 0, 0, 1, 24, 0).map(geometry(QtCommandIcon::Back));
    case QtCommandIcon::EditLocation:
        p.moveTo(21.174, 6.812);
        p.arcTo(QRectF(16.362, 1.999, 5.638, 5.638), -45, 180);
        p.lineTo(3.842, 16.174);
        p.cubicTo(3.615, 16.401, 3.444, 16.686, 3.342, 17.004);
        p.lineTo(2.021, 21.356);
        p.cubicTo(1.9, 21.755, 2.246, 22.099, 2.644, 21.978);
        p.lineTo(6.997, 20.658);
        p.cubicTo(7.314, 20.562, 7.599, 20.391, 7.827, 20.161);
        p.closeSubpath();
        line(15, 5, 19, 9);
        break;
    case QtCommandIcon::Move:
        p.moveTo(18, 8);
        p.lineTo(22, 12);
        p.lineTo(18, 16);
        line(2, 12, 22, 12);
        break;
    case QtCommandIcon::Delete:
        line(10, 11, 10, 17);
        line(14, 11, 14, 17);
        p.moveTo(19, 6);
        p.lineTo(19, 20);
        p.arcTo(15, 18, 4, 4, 0, -90);
        p.lineTo(7, 22);
        p.arcTo(5, 18, 4, 4, 270, -90);
        p.lineTo(5, 6);
        line(3, 6, 21, 6);
        p.moveTo(8, 6);
        p.lineTo(8, 4);
        p.arcTo(8, 2, 4, 4, 180, -90);
        p.lineTo(14, 2);
        p.arcTo(12, 2, 4, 4, 90, -90);
        p.lineTo(16, 6);
        break;
    case QtCommandIcon::Info:
        p.addEllipse(QPointF(12, 12), 10, 10);
        line(12, 16, 12, 12);
        line(12, 8, 12.01, 8);
        break;
    case QtCommandIcon::Parent:
        p.moveTo(5, 12);
        p.lineTo(12, 5);
        p.lineTo(19, 12);
        line(12, 19, 12, 5);
        break;
    case QtCommandIcon::Refresh:
        p.moveTo(21, 12);
        p.arcTo(3, 3, 18, 18, 0, -270);
        p.cubicTo(14.52, 3, 16.93, 4, 18.74, 5.74);
        p.lineTo(21, 8);
        p.moveTo(21, 3);
        p.lineTo(21, 8);
        p.lineTo(16, 8);
        break;
    case QtCommandIcon::NewFolder:
    case QtCommandIcon::Folder:
        if (command == QtCommandIcon::NewFolder) {
            line(12, 10, 12, 16);
            line(9, 13, 15, 13);
        }
        p.moveTo(20, 20);
        p.arcTo(18, 16, 4, 4, 270, 90);
        p.lineTo(22, 8);
        p.arcTo(18, 6, 4, 4, 0, 90);
        p.lineTo(12.1, 6);
        p.cubicTo(11.42, 6, 10.79, 5.66, 10.41, 5.1);
        p.lineTo(9.6, 3.9);
        p.cubicTo(9.22, 3.34, 8.61, 3, 7.93, 3);
        p.lineTo(4, 3);
        p.arcTo(2, 3, 4, 4, 90, 90);
        p.lineTo(2, 18);
        p.arcTo(2, 16, 4, 4, 180, 90);
        p.closeSubpath();
        break;
    }
    return p;
}

class CommandIconEngine final : public QIconEngine {
    QtCommandIcon command_;

  public:
    explicit CommandIconEngine(QtCommandIcon command) : command_(command) {}
    QIconEngine *clone() const override { return new CommandIconEngine(command_); }
    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State) override {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const qreal extent = qMin(rect.width(), rect.height());
        painter->translate(QRectF(rect).center() - QPointF(extent / 2, extent / 2));
        painter->scale(extent / 24, extent / 24);
        const auto group = mode == QIcon::Disabled ? QPalette::Disabled : QPalette::Active;
        const auto role =
            mode == QIcon::Selected ? QPalette::HighlightedText : QPalette::ButtonText;
        painter->setPen(QPen(QApplication::palette().color(group, role), 2, Qt::SolidLine,
                             Qt::RoundCap, Qt::RoundJoin));
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(geometry(command_));
        painter->restore();
    }
    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override {
        QPixmap image(size);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        paint(&painter, QRect(QPoint(), size), mode, state);
        return image;
    }
    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state,
                         qreal scale) override {
        auto result = pixmap(size * scale, mode, state);
        result.setDevicePixelRatio(scale);
        return result;
    }
};
} // namespace

QIcon QtCommandSymbol(QtCommandIcon command) { return QIcon(new CommandIconEngine(command)); }
