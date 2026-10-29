#include "terminal_screen.h"
#include <algorithm>

namespace cx {

static QRgb terminalColor(int index)
{
    static const QRgb palette[]{0xff161d26,0xffcf6067,0xff80bb91,0xffd6bf78,0xff6a9fcd,0xffb595c8,0xff67b7bc,0xffd8e1eb,
        0xff647586,0xffed8c91,0xffa6d6b2,0xffebd995,0xff93c4ed,0xffd3b4e7,0xff8dd5d8,0xfff1f5fa};
    index = std::clamp(index,0,255);
    if (index < 16) return palette[index];
    if (index >= 232) { const auto c = 8 + (index - 232) * 10; return qRgb(c,c,c); }
    index -= 16;
    const auto level = [](int value) { return value == 0 ? 0 : 55 + value * 40; };
    return qRgb(level(index / 36),level(index / 6 % 6),level(index % 6));
}

TerminalScreen::TerminalScreen(int columns, int rows, int history) : columns_(columns), rows_(rows), historyLimit_(history)
{
    resize(columns,rows);
}

TerminalScreen::Line TerminalScreen::blankLine() const { return Line(columns_,Cell{}); }

void TerminalScreen::resize(int columns, int rows)
{
    if (columns < 2 || columns > 500 || rows < 2 || rows > 300) throw Error("终端尺寸超出范围");
    columns_ = columns; rows_ = rows;
    screen_.resize(rows_);
    for (auto &line : screen_) line.resize(columns_);
    x_ = std::clamp(x_,0,columns_ - 1); y_ = std::clamp(y_,0,rows_ - 1); ++version_;
}

void TerminalScreen::clear()
{
    screen_.fill(blankLine()); history_.clear(); pageOffset_ = 0; x_ = y_ = 0;
    escape_.clear(); escaped_ = osc_ = false; pen_ = {}; ++version_;
}

void TerminalScreen::newline()
{
    if (++y_ >= rows_) {
        history_.push_back(screen_.takeFirst()); screen_.append(blankLine()); y_ = rows_ - 1;
        while (history_.size() > static_cast<size_t>(historyLimit_)) { history_.pop_front(); ++removed_; }
    }
}

void TerminalScreen::rendition(const QList<int> &input)
{
    const auto parameters = input.isEmpty() ? QList<int>{0} : input;
    for (qsizetype i = 0; i < parameters.size(); ++i) {
        const auto code = parameters[i];
        if (code == 0) pen_ = {};
        else if (code == 1) pen_.bold = true;
        else if (code == 4) pen_.underline = true;
        else if (code == 7) pen_.inverse = true;
        else if (code == 22) pen_.bold = false;
        else if (code == 24) pen_.underline = false;
        else if (code == 27) pen_.inverse = false;
        else if (code == 39) pen_.foreground = Cell{}.foreground;
        else if (code == 49) pen_.background = Cell{}.background;
        else if (code >= 30 && code <= 37) pen_.foreground = terminalColor(code - 30);
        else if (code >= 40 && code <= 47) pen_.background = terminalColor(code - 40);
        else if (code >= 90 && code <= 97) pen_.foreground = terminalColor(code - 90 + 8);
        else if (code >= 100 && code <= 107) pen_.background = terminalColor(code - 100 + 8);
        else if ((code == 38 || code == 48) && i + 2 < parameters.size()) {
            QRgb color;
            if (parameters[i + 1] == 5) { color = terminalColor(parameters[i + 2]); i += 2; }
            else if (parameters[i + 1] == 2 && i + 4 < parameters.size()) { color = qRgb(std::clamp(parameters[i + 2],0,255),std::clamp(parameters[i + 3],0,255),std::clamp(parameters[i + 4],0,255)); i += 4; }
            else continue;
            if (code == 38) pen_.foreground = color; else pen_.background = color;
        }
    }
}

void TerminalScreen::command(QChar final)
{
    auto body = escape_.mid(1,escape_.size() - 2);
    const bool privateMode = body.startsWith('?'); if (privateMode) body.remove(0,1);
    QList<int> parameters;
    if (!body.isEmpty()) for (const auto &value : body.split(';')) parameters.append(value.isEmpty() ? 0 : value.toInt());
    const auto n = [&](qsizetype index, int fallback = 1) { return parameters.value(index,0) == 0 ? fallback : parameters[index]; };
    const auto erase = [&](int row, int begin, int end) { for (int column = std::max(0,begin); column < std::min(columns_,end); ++column) screen_[row][column] = Cell{}; };
    if (final == 'm') rendition(parameters);
    else if (final == 'A') y_ = std::max(0,y_ - n(0));
    else if (final == 'B') y_ = std::min(rows_ - 1,y_ + n(0));
    else if (final == 'C') x_ = std::min(columns_ - 1,x_ + n(0));
    else if (final == 'D') x_ = std::max(0,x_ - n(0));
    else if (final == 'E') { y_ = std::min(rows_ - 1,y_ + n(0)); x_ = 0; }
    else if (final == 'F') { y_ = std::max(0,y_ - n(0)); x_ = 0; }
    else if (final == 'G' || final == '`') x_ = std::clamp(n(0) - 1,0,columns_ - 1);
    else if (final == 'd') y_ = std::clamp(n(0) - 1,0,rows_ - 1);
    else if (final == 'H' || final == 'f') { y_ = std::clamp(n(0) - 1,0,rows_ - 1); x_ = std::clamp(n(1) - 1,0,columns_ - 1); }
    else if (final == 's') { savedX_ = x_; savedY_ = y_; }
    else if (final == 'u') { x_ = savedX_; y_ = savedY_; }
    else if (final == 'K') {
        const auto mode = parameters.value(0,0);
        erase(y_,mode == 1 || mode == 2 ? 0 : x_, mode == 0 || mode == 2 ? columns_ : x_ + 1);
    } else if (final == 'J') {
        const auto mode = parameters.value(0,0);
        if (mode == 2 || mode == 3) { screen_.fill(blankLine()); if (mode == 3) history_.clear(); }
        else if (mode == 0) { erase(y_,x_,columns_); for (auto row = y_ + 1; row < rows_; ++row) screen_[row] = blankLine(); }
        else { erase(y_,0,x_ + 1); for (int row = 0; row < y_; ++row) screen_[row] = blankLine(); }
    } else if (final == 'P') {
        const auto count = std::min(n(0),columns_ - x_); for (int i = x_; i < columns_ - count; ++i) screen_[y_][i] = screen_[y_][i + count]; erase(y_,columns_ - count,columns_);
    } else if (final == '@') {
        const auto count = std::min(n(0),columns_ - x_); for (int i = columns_ - 1; i >= x_ + count; --i) screen_[y_][i] = screen_[y_][i - count]; erase(y_,x_,x_ + count);
    }
}

void TerminalScreen::feed(const QString &text)
{
    for (qsizetype at = 0; at < text.size(); ++at) {
        const auto character = text[at];
        if (osc_) {
            if (character == QChar(7) || (character == '\\' && escape_.endsWith(QChar(27)))) { title_ = escape_.section(';',1).remove(QChar(27)); osc_ = escaped_ = false; escape_.clear(); }
            else if (escape_.size() < 4096) escape_ += character;
            continue;
        }
        if (escaped_) {
            escape_ += character;
            if (escape_.size() == 1) {
                if (character == ']') { osc_ = true; continue; }
                if (character == '7') { savedX_ = x_; savedY_ = y_; }
                else if (character == '8') { x_ = savedX_; y_ = savedY_; }
                else if (character == 'c') clear();
                if (character != '[') { escaped_ = false; escape_.clear(); }
            } else if (character.unicode() >= 0x40 && character.unicode() <= 0x7e) { command(character); escaped_ = false; escape_.clear(); }
            else if (escape_.size() > 128) { escaped_ = false; escape_.clear(); }
            continue;
        }
        if (character == QChar(27)) { escaped_ = true; escape_.clear(); continue; }
        if (character == '\r') { x_ = 0; continue; }
        if (character == '\n') { newline(); continue; }
        if (character == '\b') { x_ = std::max(0,x_ - 1); continue; }
        if (character == '\t') { x_ = std::min(columns_ - 1,(x_ / 8 + 1) * 8); continue; }
        if (character.unicode() < 32) continue;
        if (character.isMark() && x_ > 0) { screen_[y_][std::min(columns_ - 1,x_ - 1)].text += character; continue; }
        QString glyph(character);
        uint code = character.unicode();
        if (character.isHighSurrogate() && at + 1 < text.size() && text[at + 1].isLowSurrogate()) { code = QChar::surrogateToUcs4(character,text[++at]); glyph += text[at]; }
        const int width = (code >= 0x1100 && (code <= 0x115f || (code >= 0x2e80 && code <= 0xa4cf) || (code >= 0xac00 && code <= 0xd7a3) || (code >= 0xf900 && code <= 0xfaff) || (code >= 0xfe10 && code <= 0xfe6f) || (code >= 0xff00 && code <= 0xff60) || code >= 0x1f300)) ? 2 : 1;
        if (x_ + width > columns_) { x_ = 0; newline(); }
        auto cell = pen_; cell.text = glyph; screen_[y_][x_] = cell;
        if (width == 2) { cell.text.clear(); screen_[y_][x_ + 1] = cell; }
        x_ += width;
    }
    ++version_;
}

QString TerminalScreen::plain(const Line &line) const
{
    QString text; for (const auto &cell : line) text += cell.text;
    while (text.endsWith(' ')) text.chop(1); return text;
}

QString TerminalScreen::rich(const Line &line) const
{
    QString result;
    for (const auto &cell : line) {
        const auto foreground = QColor::fromRgb(cell.inverse ? cell.background : cell.foreground).name();
        const auto background = QColor::fromRgb(cell.inverse ? cell.foreground : cell.background).name();
        result += "<span style=\"color:" + foreground + ";background-color:" + background + (cell.bold ? ";font-weight:bold" : "") + (cell.underline ? ";text-decoration:underline" : "") + "\">" + cell.text.toHtmlEscaped().replace(' ',"&nbsp;") + "</span>";
    }
    return result;
}

QVariantList TerminalScreen::styles(const Line &line) const
{
    QVariantList result;
    for (const auto &cell : line) result.append(QVariantMap{{"text",cell.text},{"foreground",QColor::fromRgb(cell.foreground).name()},{"background",QColor::fromRgb(cell.background).name()},{"bold",cell.bold},{"underline",cell.underline},{"inverse",cell.inverse}});
    return result;
}

void TerminalScreen::scroll(int lines) { pageOffset_ = std::clamp(pageOffset_ + lines,-static_cast<int>(history_.size()),0); ++version_; }

QVariantMap TerminalScreen::describe(bool withStyles) const
{
    QStringList lines, html; QVariantList cells;
    const auto start = static_cast<qsizetype>(history_.size()) + pageOffset_;
    for (int row = 0; row < rows_; ++row) {
        const auto index = start + row;
        const auto &line = index < static_cast<qsizetype>(history_.size()) ? history_[static_cast<size_t>(std::max<qsizetype>(0,index))] : screen_[std::clamp<qsizetype>(index - static_cast<qsizetype>(history_.size()),0,rows_ - 1)];
        lines.append(plain(line));
        if (withStyles) { html.append(rich(line)); cells.append(styles(line)); }
    }
    return {{"kind","terminal"},{"columns",columns_},{"rows",rows_},{"lines",lines},{"html",html.join("<br/>")},{"cells",cells},
        {"cursor",QVariantList{x_,y_}},{"history_lines",static_cast<qulonglong>(history_.size())},{"removed_lines",QVariant::fromValue(removed_)},
        {"page_offset",pageOffset_},{"title",title_},{"version",version_}};
}

QVariantMap TerminalScreen::history(int offset, int limit, bool withStyles) const
{
    QVariantList records;
    const auto total = static_cast<int>(history_.size()) + rows_;
    for (auto i = std::max(0,offset); i < std::min(total,offset + std::clamp(limit,1,1000)); ++i) {
        const auto &line = i < static_cast<int>(history_.size()) ? history_[static_cast<size_t>(i)] : screen_[i - static_cast<int>(history_.size())];
        QVariantMap record{{"text",plain(line)}};
        if (withStyles) record["cells"] = styles(line);
        records.append(record);
    }
    return {{"lines",records},{"total",total},{"removed",QVariant::fromValue(removed_)}};
}

} // namespace cx
