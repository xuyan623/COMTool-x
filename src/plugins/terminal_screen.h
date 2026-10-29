#pragma once

// Incremental ANSI screen model; presentation and keyboard handling stay in QML.
// Scrollback is bounded and exposes explicit offsets for external inspection.
#include "core/value.h"
#include <QColor>
#include <deque>

namespace cx {

class TerminalScreen {
public:
    TerminalScreen(int columns, int rows, int history);
    void feed(const QString &text);
    void resize(int columns, int rows);
    void clear();
    void scroll(int lines);
    int columns() const { return columns_; }
    int rows() const { return rows_; }
    QVariantMap describe(bool styles) const;
    QVariantMap history(int offset, int limit, bool styles) const;
private:
    struct Cell { QString text = " "; QRgb foreground = qRgb(218,229,239), background = qRgb(17,25,35); bool bold = false, underline = false, inverse = false; };
    using Line = QVector<Cell>;
    Line blankLine() const;
    void newline();
    void command(QChar final);
    void rendition(const QList<int> &parameters);
    QString plain(const Line &line) const;
    QString rich(const Line &line) const;
    QVariantList styles(const Line &line) const;
    int columns_, rows_, historyLimit_, x_ = 0, y_ = 0, savedX_ = 0, savedY_ = 0, pageOffset_ = 0;
    QVector<Line> screen_;
    std::deque<Line> history_;
    Cell pen_;
    QString escape_, title_;
    bool escaped_ = false, osc_ = false;
    quint64 version_ = 0, removed_ = 0;
};

} // namespace cx
