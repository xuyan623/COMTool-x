#pragma once

// The former toolbar's vector geometry is reused by the QML interface. Native
// SVG rendering keeps theme colors and disabled states independent of fonts.
#include <QQuickImageProvider>
namespace cx {
class IconProvider final : public QQuickImageProvider {
public:
    IconProvider();
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};
}
