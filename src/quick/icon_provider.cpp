#include "icon_provider.h"
#include <QSvgRenderer>
#include <QPainter>
#include <QUrl>
#include <algorithm>

namespace cx {
IconProvider::IconProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

QImage IconProvider::requestImage(const QString &id, QSize *size, const QSize &requested)
{
    static const QHash<QString,QString> paths{
        {"record",R"(<circle cx="12" cy="12" r="7" fill="{c}" stroke="none"/>)"},
        {"save",R"(<path d="M4 3h13l4 4v14H3V3Z"/><path d="M7 3v6h10V3M7 21v-8h10v8"/>)"},
        {"open",R"(<path d="M3 7V4h7l3 3h8v3M3 7h9l2 3h7l-3 10H3Z"/>)"},
        {"rewind",R"(<path d="M5 4v16M19 5l-11 7 11 7Z"/>)"},
        {"export",R"(<path d="M12 15V3m-4 4 4-4 4 4M4 13v8h16v-8"/>)"},
        {"follow",R"(<path d="M3 7h18M3 17h18M7 3v18m10-18v18"/><circle cx="12" cy="12" r="3"/>)"},
        {"spectrum",R"(<path d="M3 21V3m0 18h19M7 21v-6m4 6V6m4 15V10m4 11v-5"/>)"},
        {"previous",R"(<path d="m15 5-7 7 7 7"/>)"},
        {"next",R"(<path d="m9 5 7 7-7 7"/>)"},
        {"paste",R"(<path d="M8 4H5v17h14V4h-3"/><rect x="8" y="2" width="8" height="5" rx="1"/><path d="M8 12h8m-8 4h6"/>)"},
        {"display",R"(<rect x="2" y="3" width="20" height="15" rx="2"/><path d="M8 22h8m-4-4v4M6 8h4m-4 5h12"/>)"},
        {"text-input",R"(<rect x="3" y="5" width="18" height="14" rx="3"/><path d="M7 9h7M7 13h5m5-5v8m-2-8h4m-4 8h4"/>)"},
        {"button",R"(<rect x="3" y="6" width="18" height="13" rx="5"/><path d="M7 15h10M9 3h6"/>)"},
        {"sliders",R"(<path d="M4 7h16M4 17h16"/><circle cx="9" cy="7" r="3"/><circle cx="15" cy="17" r="3"/>)"},
        {"wave",R"(<path d="M2 13c3-12 7-12 10 0s7 12 10 0M3 21h18"/>)"},
        {"clock",R"(<circle cx="12" cy="12" r="9"/><path d="M12 6v6l4 3"/>)"},
        {"connection",R"(<rect x="3" y="6" width="18" height="12" rx="3"/><path d="M7 10h10M7 14h3m4 0h3M8 3v3m8-3v3m-8 12v3m8-3v3"/>)"},
        {"decode",R"(<path d="m7 4-4 8 4 8M17 4l4 8-4 8M10 16l4-8"/>)"},
        {"fork",R"(<path d="M3 12h8m0 0 6-7h4m-10 7 6 7h4"/><circle cx="3" cy="12" r="2"/>)"},
        {"switch",R"(<path d="M3 16h5m8 0h5M8 16l8-9"/><circle cx="8" cy="16" r="2"/><circle cx="16" cy="16" r="2"/>)"},
        {"attitude",R"(<path d="m12 2 3 8 7 4-1 2-7-2v5l3 2v1l-5-1-5 1v-1l3-2v-5l-7 2-1-2 7-4Z"/>)"},
        {"terminal",R"(<rect x="2" y="4" width="20" height="16" rx="2"/><path d="m6 9 3 3-3 3m6 0h5"/>)"},
        {"sidebar-left",R"(<rect x="3" y="4" width="18" height="16" rx="2"/><path d="M9 4v16"/>)"},
        {"sidebar-right",R"(<rect x="3" y="4" width="18" height="16" rx="2"/><path d="M15 4v16"/>)"},
        {"sync-to",R"(<path d="M3 8h17m-5-5 5 5-5 5M21 17H4m5-5-5 5 5 5"/>)"},
        {"up",R"(<path d="m6 14 6-6 6 6"/>)"},
        {"down",R"(<path d="m6 10 6 6 6-6"/>)"},
        {"withdraw",R"(<path d="M4 4h16v5H4Zm3 10h10M12 9v12m-4-4 4 4 4-4"/>)"},
        {"copy",R"(<rect x="8" y="8" width="13" height="13" rx="2"/><path d="M16 8V3H3v13h5"/>)"},
        {"replace",R"(<path d="M3 7h14m-4-4 4 4-4 4M21 17H7m4-4-4 4 4 4"/>)"},
        {"trash",R"(<path d="M4 6h16M9 3h6M6 6l1 15h10l1-15M10 10v7m4-7v7"/>)"},
        {"graph",R"(<path d="M3 4h6v6H3Zm12 10h6v6h-6M9 7h6v10"/>)"},
        {"layout",R"(<rect x="3" y="4" width="18" height="16" rx="1"/><path d="M3 9h18M10 9v11"/>)"},
        {"runtime",R"(<rect x="3" y="4" width="18" height="16" rx="1"/><path d="m10 8 6 4-6 4Z"/>)"},
        {"logs",R"(<path d="M4 5h2m3 0h11M4 10h2m3 0h11M4 15h2m3 0h11M4 20h2m3 0h11"/>)"},
        {"library",R"(<path d="M3 4h7v7H3Zm11 0h7v7h-7M3 15h7v6H3Zm11 0h7v6h-7Z"/>)"},
        {"pin",R"(<path d="m8 3 8 0-1 6 3 3v2H6v-2l3-3ZM12 14v7"/>)"},
        {"star",R"(<path d="m12 3 2.8 5.7 6.2.9-4.5 4.4 1.1 6.2L12 17.3l-5.6 2.9 1.1-6.2L3 9.6l6.2-.9Z"/>)"},
        {"close",R"(<path d="m6 6 12 12M18 6 6 18"/>)"},
        {"ui_update",R"(<path d="M20 10a8 8 0 0 0-13.5-5.5L3 8m0-5v5h5M4 14a8 8 0 0 0 13.5 5.5L21 16m0 5v-5h-5"/>)"},
        {"unlocked",R"(<rect x="5.5" y="10" width="13" height="10" rx="2"/><path d="M8 10V7a4 4 0 0 1 8 0M12 14v2"/>)"},
        {"locked",R"(<rect x="5.5" y="10" width="13" height="10" rx="2"/><path d="M8 10V7a4 4 0 0 1 8 0v3M12 14v2"/>)"},
        {"run",R"(<path d="M8 4.5 19 12 8 19.5Z" fill="{c}" stroke="none"/>)"},
        {"pause",R"(<path d="M8 5V19M16 5V19" stroke-width="3"/>)"},
        {"debug",R"(<path d="M8 9h8v8a4 4 0 0 1-8 0ZM9 8V6a3 3 0 0 1 6 0v2M5 11h3m8 0h3M4 15h4m8 0h4M5 19h3m8 0h3M8 3l2 2m6-2-2 2M12 10v10"/>)"},
        {"batch",R"(<path d="M4 5v14M8 7h5v10H8ZM17 7h3v10h-3"/><path d="m11 3 3 2-3 2M14 5h7"/>)"},
        {"node",R"(<path d="M4 5h5v5H4Zm11 9h5v5h-5M9 7h6v9"/><path d="m12 13 3 3 3-3"/>)"},
        {"reset",R"(<path d="M5 9a7 7 0 1 1 0 6M5 4v5h5"/>)"},
        {"end",R"(<path d="M6 6h12v12H6Z" fill="{c}" stroke="none"/>)"},
        {"row",R"(<rect x="3" y="5" width="18" height="14" rx="1"/><path d="M9 5v14M15 5v14"/>)"},
        {"column",R"(<rect x="3" y="3" width="18" height="18" rx="1"/><path d="M3 9h18M3 15h18"/>)"},
        {"plus",R"(<path d="M12 4v16M4 12h16"/>)"},
        {"fit",R"(<path d="M3 9V3h6M15 3h6v6M21 15v6h-6M9 21H3v-6"/>)"},
        {"rotate",R"(<path d="M5 8a8 8 0 1 1-1 7M5 3v5h5"/>)"},
        {"dock",R"(<rect x="3" y="4" width="18" height="16" rx="1"/><path d="M3 9h18M9 9v11m3-6h6m-3-3-3 3 3 3"/>)"}
    };
    const auto parts = id.split('/'); const auto name = parts.value(0);
    const QColor color(QUrl::fromPercentEncoding(parts.value(1).toUtf8()));
    const auto ink = color.isValid() ? color.name(QColor::HexRgb) : QString("#E6EDF5");
    const auto body = paths.value(name,paths.value("graph"));
    const auto svg = "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='" + ink + "' stroke-width='1.6' stroke-linecap='round' stroke-linejoin='round'>" + QString(body).replace("{c}",ink) + "</svg>";
    const QSize dimensions(std::clamp(requested.width() > 0 ? requested.width() : 48,8,512),std::clamp(requested.height() > 0 ? requested.height() : 48,8,512));
    QImage image(dimensions,QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent);
    QSvgRenderer renderer(svg.toUtf8()); QPainter painter(&image); renderer.render(&painter);
    if (size) *size = dimensions; return image;
}
}
