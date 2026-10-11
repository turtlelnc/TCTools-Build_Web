// ============================================================================
//  StyleSheet.cpp -- CSS declaration storage and responsive resolution
//  StyleSheet.cpp —— CSS 声明存储与响应式解析
// ============================================================================
#include "StyleSheet.h"
#include "IdGenerator.h"

#include <QJsonValue>
#include <QRegularExpression>
#include <QStringList>

namespace tcbw {

namespace {

//  Properties whose values are numeric lengths and therefore benefit from unit
//  normalisation.
//  数值型长度属性，需要对单位做规范化。
const QSet<QString> &lengthProperties()
{
    static const QSet<QString> s = {
        QStringLiteral("width"), QStringLiteral("height"),
        QStringLiteral("min-width"), QStringLiteral("max-width"),
        QStringLiteral("min-height"), QStringLiteral("max-height"),
        QStringLiteral("top"), QStringLiteral("right"),
        QStringLiteral("bottom"), QStringLiteral("left"),
        QStringLiteral("margin-top"), QStringLiteral("margin-right"),
        QStringLiteral("margin-bottom"), QStringLiteral("margin-left"),
        QStringLiteral("padding-top"), QStringLiteral("padding-right"),
        QStringLiteral("padding-bottom"), QStringLiteral("padding-left"),
        QStringLiteral("gap"), QStringLiteral("row-gap"), QStringLiteral("column-gap"),
        QStringLiteral("border-radius"), QStringLiteral("border-width"),
        QStringLiteral("font-size"), QStringLiteral("line-height"),
        QStringLiteral("letter-spacing"), QStringLiteral("flex-basis"),
        QStringLiteral("outline-width"), QStringLiteral("outline-offset")
    };
    return s;
}

QString formatNumber(double v)
{
    //  Emit at most four decimals and strip trailing zeros so that the produced
    //  CSS stays readable and diff-stable.
    //  最多输出四位小数并去掉末尾的零，使生成的 CSS 保持可读且便于 diff。
    QString s = QString::number(v, 'f', 4);
    while (s.contains(QLatin1Char('.')) && (s.endsWith(QLatin1Char('0'))))
        s.chop(1);
    if (s.endsWith(QLatin1Char('.')))
        s.chop(1);
    return s;
}

QString sanitisePropertyName(const QString &name)
{
    QString n = name.trimmed().toLower();
    n.replace(QRegularExpression(QStringLiteral("[^a-z0-9\\-]")), QString());
    return n;
}

//  Guards against CSS injection through user-controlled values.
//  防止通过用户可控的值进行 CSS 注入。
bool isSafeDeclarationValue(const QString &value)
{
    if (value.contains(QLatin1Char(';')))
        return false;
    if (value.contains(QLatin1Char('}')))
        return false;
    if (value.contains(QLatin1Char('{')))
        return false;
    if (value.contains(QLatin1String("</"), Qt::CaseInsensitive))
        return false;
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
//  StyleSheet
// ---------------------------------------------------------------------------
void StyleSheet::set(const QString &property, const QString &cssValue)
{
    const QString prop = sanitisePropertyName(property);
    if (prop.isEmpty())
        return;

    const QString value = cssValue.trimmed();
    if (value.isEmpty()) {
        m_decls.remove(prop);
        return;
    }
    if (!isSafeDeclarationValue(value))
        return;

    m_decls.insert(prop, value);
}

void StyleSheet::setImportant(const QString &property, const QString &cssValue)
{
    QString v = cssValue.trimmed();
    if (v.endsWith(QLatin1String("!important"), Qt::CaseInsensitive))
        v.chop(10);
    set(property, v.trimmed() + QStringLiteral(" !important"));
}

QString StyleSheet::get(const QString &property) const
{
    return m_decls.value(sanitisePropertyName(property));
}

bool StyleSheet::has(const QString &property) const
{
    return m_decls.contains(sanitisePropertyName(property));
}

void StyleSheet::remove(const QString &property)
{
    m_decls.remove(sanitisePropertyName(property));
}

void StyleSheet::clear()
{
    m_decls.clear();
}

void StyleSheet::merge(const StyleSheet &other)
{
    for (auto it = other.m_decls.constBegin(); it != other.m_decls.constEnd(); ++it)
        m_decls.insert(it.key(), it.value());
}

void StyleSheet::mergeMissing(const StyleSheet &other)
{
    for (auto it = other.m_decls.constBegin(); it != other.m_decls.constEnd(); ++it) {
        if (!m_decls.contains(it.key()))
            m_decls.insert(it.key(), it.value());
    }
}

bool StyleSheet::equals(const StyleSheet &other) const
{
    return m_decls == other.m_decls;
}

QString StyleSheet::toCss(bool pretty, int indent) const
{
    if (m_decls.isEmpty())
        return QString();

    const QString pad = pretty ? QString(indent, QLatin1Char(' ')) : QString();
    QString out;
    for (auto it = m_decls.constBegin(); it != m_decls.constEnd(); ++it) {
        if (pretty) {
            out += pad;
            out += it.key();
            out += QStringLiteral(": ");
            out += it.value();
            out += QStringLiteral(";\n");
        } else {
            out += it.key();
            out += QLatin1Char(':');
            out += it.value();
            out += QLatin1Char(';');
        }
    }
    return out;
}

StyleSheet StyleSheet::fromCss(const QString &css)
{
    StyleSheet sheet;
    const QStringList blocks = css.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString &block : blocks) {
        //  Strip CSS comments before parsing.
        //  解析前先去掉 CSS 注释。
        static const QRegularExpression comment(QStringLiteral("/\\*.*?\\*/"),
                                                QRegularExpression::DotMatchesEverythingOption);
        QString cleaned = block;
        cleaned.remove(comment);

        const int colon = cleaned.indexOf(QLatin1Char(':'));
        if (colon <= 0)
            continue;
        sheet.set(cleaned.left(colon), cleaned.mid(colon + 1));
    }
    return sheet;
}

StyleSheet StyleSheet::fromJson(const QJsonObject &obj)
{
    StyleSheet sheet;
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        if (it.value().isString())
            sheet.set(it.key(), it.value().toString());
        else if (it.value().isDouble())
            sheet.set(it.key(), formatNumber(it.value().toDouble()));
    }
    return sheet;
}

QJsonObject StyleSheet::toJson() const
{
    QJsonObject obj;
    for (auto it = m_decls.constBegin(); it != m_decls.constEnd(); ++it)
        obj.insert(it.key(), it.value());
    return obj;
}

void StyleSheet::setLength(const QString &property, double number, StyleUnit unit)
{
    if (unit == StyleUnit::Auto) {
        set(property, QStringLiteral("auto"));
        return;
    }
    set(property, formatNumber(number) + styleUnitToCss(unit));
}

void StyleSheet::setColor(const QString &property, const QString &color)
{
    set(property, normaliseColor(color));
}

void StyleSheet::setUrl(const QString &property, const QString &url)
{
    QString u = url.trimmed();
    //  Remove characters that could terminate the url() token early.
    //  去掉可能提前终止 url() 记号的字符。
    u.remove(QLatin1Char(')'));
    u.remove(QLatin1Char('('));
    u.remove(QLatin1Char('"'));
    u.remove(QLatin1Char('\''));
    u.remove(QLatin1Char('\n'));
    set(property, QStringLiteral("url(\"") + u + QStringLiteral("\")"));
}

void StyleSheet::setSpacing(double top, double right, double bottom, double left,
                            StyleUnit unit)
{
    const QString u = styleUnitToCss(unit);
    set(QStringLiteral("padding-top"), formatNumber(top) + u);
    set(QStringLiteral("padding-right"), formatNumber(right) + u);
    set(QStringLiteral("padding-bottom"), formatNumber(bottom) + u);
    set(QStringLiteral("padding-left"), formatNumber(left) + u);
}

void StyleSheet::setBoxShadow(double x, double y, double blur, double spread,
                              const QString &color, bool inset)
{
    QString v;
    if (inset)
        v += QStringLiteral("inset ");
    v += formatNumber(x) + QStringLiteral("px ") + formatNumber(y) + QStringLiteral("px ")
         + formatNumber(blur) + QStringLiteral("px ") + formatNumber(spread)
         + QStringLiteral("px ") + normaliseColor(color);
    set(QStringLiteral("box-shadow"), v);
}

// ---------------------------------------------------------------------------
//  ResponsiveStyle
// ---------------------------------------------------------------------------
StyleSheet &ResponsiveStyle::overrideFor(Breakpoint bp)
{
    return m_overrides[static_cast<int>(bp)];
}

const StyleSheet &ResponsiveStyle::overrideFor(Breakpoint bp) const
{
    static const StyleSheet empty;
    const auto it = m_overrides.constFind(static_cast<int>(bp));
    return it == m_overrides.constEnd() ? empty : it.value();
}

bool ResponsiveStyle::hasOverride(Breakpoint bp) const
{
    const auto it = m_overrides.constFind(static_cast<int>(bp));
    return it != m_overrides.constEnd() && !it.value().isEmpty();
}

void ResponsiveStyle::clearOverride(Breakpoint bp)
{
    m_overrides.remove(static_cast<int>(bp));
}

bool ResponsiveStyle::hasAnyOverride() const
{
    for (auto it = m_overrides.constBegin(); it != m_overrides.constEnd(); ++it) {
        if (!it.value().isEmpty())
            return true;
    }
    return false;
}

QString ResponsiveStyle::resolve(Breakpoint bp, const QString &property) const
{
    const QMap<QString, QString> merged = resolved(bp);
    return merged.value(property.toLower().trimmed());
}

QMap<QString, QString> ResponsiveStyle::resolved(Breakpoint bp) const
{
    QMap<QString, QString> out = m_base.declarations();
    if (bp == Breakpoint::Desktop)
        return out;

    //  Tablet inherits desktop, mobile inherits tablet.
    //  平板继承桌面，手机继承平板。
    if (bp == Breakpoint::Mobile) {
        const auto tab = m_overrides.constFind(static_cast<int>(Breakpoint::Tablet));
        if (tab != m_overrides.constEnd()) {
            for (auto it = tab.value().declarations().constBegin();
                 it != tab.value().declarations().constEnd(); ++it)
                out.insert(it.key(), it.value());
        }
    }
    const auto own = m_overrides.constFind(static_cast<int>(bp));
    if (own != m_overrides.constEnd()) {
        for (auto it = own.value().declarations().constBegin();
             it != own.value().declarations().constEnd(); ++it)
            out.insert(it.key(), it.value());
    }
    return out;
}

QString ResponsiveStyle::toCss(const QString &selector, bool pretty) const
{
    QString out;

    if (!m_base.isEmpty()) {
        if (pretty)
            out += selector + QStringLiteral(" {\n") + m_base.toCss(true, 2)
                   + QStringLiteral("}\n");
        else
            out += selector + QLatin1Char('{') + m_base.toCss(false) + QStringLiteral("}\n");
    }

    //  Emit media queries from narrowest to widest so that the cascade matches
    //  the desktop-first override rules.
    //  按由窄到宽的顺序输出媒体查询，使层叠顺序与桌面优先的覆盖规则一致。
    const QVector<Breakpoint> order = {Breakpoint::Mobile, Breakpoint::Tablet};
    for (Breakpoint bp : order) {
        const auto it = m_overrides.constFind(static_cast<int>(bp));
        if (it == m_overrides.constEnd() || it.value().isEmpty())
            continue;
        if (pretty)
            out += QStringLiteral("@media ") + mediaQueryFor(bp) + QStringLiteral(" {\n  ")
                   + selector + QStringLiteral(" {\n") + it.value().toCss(true, 4)
                   + QStringLiteral("  }\n}\n");
        else
            out += QStringLiteral("@media ") + mediaQueryFor(bp) + QLatin1Char('{') + selector
                   + QLatin1Char('{') + it.value().toCss(false) + QStringLiteral("}}\n");
    }
    return out;
}

bool ResponsiveStyle::isEmpty() const
{
    return m_base.isEmpty() && !hasAnyOverride();
}

QJsonObject ResponsiveStyle::toJson() const
{
    QJsonObject obj;
    if (!m_base.isEmpty())
        obj.insert(QStringLiteral("base"), m_base.toJson());

    QJsonObject overrides;
    for (auto it = m_overrides.constBegin(); it != m_overrides.constEnd(); ++it) {
        if (it.value().isEmpty())
            continue;
        overrides.insert(toString(static_cast<Breakpoint>(it.key())), it.value().toJson());
    }
    if (!overrides.isEmpty())
        obj.insert(QStringLiteral("overrides"), overrides);
    return obj;
}

ResponsiveStyle ResponsiveStyle::fromJson(const QJsonObject &obj)
{
    ResponsiveStyle style;
    if (obj.contains(QStringLiteral("base")))
        style.m_base = StyleSheet::fromJson(obj.value(QStringLiteral("base")).toObject());

    const QJsonObject overrides = obj.value(QStringLiteral("overrides")).toObject();
    for (auto it = overrides.constBegin(); it != overrides.constEnd(); ++it) {
        const Breakpoint bp = breakpointFromString(it.key(), Breakpoint::Desktop);
        style.m_overrides.insert(static_cast<int>(bp),
                                 StyleSheet::fromJson(it.value().toObject()));
    }
    return style;
}

// ---------------------------------------------------------------------------
//  Breakpoint helpers
//  断点辅助函数
// ---------------------------------------------------------------------------
QString mediaQueryFor(Breakpoint bp)
{
    switch (bp) {
    case Breakpoint::Desktop:
        return QStringLiteral("all");
    case Breakpoint::Tablet:
        return QStringLiteral("(max-width: %1px)").arg(breakpointMinWidth(Breakpoint::Tablet) - 1);
    case Breakpoint::Mobile:
        return QStringLiteral("(max-width: %1px)").arg(breakpointMinWidth(Breakpoint::Mobile) - 1);
    }
    return QStringLiteral("all");
}

int breakpointMinWidth(Breakpoint bp)
{
    switch (bp) {
    case Breakpoint::Desktop: return 1024;
    case Breakpoint::Tablet:  return 768;
    case Breakpoint::Mobile:  return 0;
    }
    return 0;
}

int breakpointMinHeight(Breakpoint /*bp*/)
{
    return 0;
}

} // namespace tcbw
