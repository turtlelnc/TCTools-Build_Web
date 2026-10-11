// ============================================================================
//  StyleSheet.h -- CSS declaration storage and responsive resolution
//  StyleSheet.h —— CSS 声明存储与响应式解析
// ============================================================================
#pragma once

#include "Types.h"

#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>

namespace tcbw {

// ---------------------------------------------------------------------------
//  StyleSheet
//  A single, ordered set of CSS declarations. Order is preserved because CSS
//  shorthand/longhand precedence is order sensitive.
//  单个有序的 CSS 声明集合。顺序会被保留，因为 CSS 简写与长写属性的优先级
//  与书写顺序有关。
// ---------------------------------------------------------------------------
class StyleSheet
{
public:
    StyleSheet() = default;

    void set(const QString &property, const QString &cssValue);
    void setImportant(const QString &property, const QString &cssValue);
    QString get(const QString &property) const;
    bool has(const QString &property) const;
    void remove(const QString &property);
    void clear();

    int size() const { return static_cast<int>(m_decls.size()); }
    bool isEmpty() const { return m_decls.isEmpty(); }

    const QMap<QString, QString> &declarations() const { return m_decls; }

    void merge(const StyleSheet &other);
    void mergeMissing(const StyleSheet &other);
    bool equals(const StyleSheet &other) const;

    QString toCss(bool pretty = false, int indent = 0) const;
    static StyleSheet fromCss(const QString &css);
    static StyleSheet fromJson(const QJsonObject &obj);
    QJsonObject toJson() const;

    //  Convenience typed setters / 便捷的类型化设置函数
    void setLength(const QString &property, double number, StyleUnit unit);
    void setColor(const QString &property, const QString &color);
    void setUrl(const QString &property, const QString &url);

    //  Well-known shorthand helpers / 常用简写辅助函数
    void setSpacing(double top, double right, double bottom, double left, StyleUnit unit);
    void setBoxShadow(double x, double y, double blur, double spread,
                      const QString &color, bool inset = false);

private:
    QMap<QString, QString> m_decls;
};

// ---------------------------------------------------------------------------
//  ResponsiveStyle
//  Desktop declarations plus optional tablet/mobile overrides.
//  桌面端声明，外加可选的平板/手机覆盖声明。
// ---------------------------------------------------------------------------
class ResponsiveStyle
{
public:
    ResponsiveStyle() = default;

    StyleSheet &base() { return m_base; }
    const StyleSheet &base() const { return m_base; }

    StyleSheet &overrideFor(Breakpoint bp);
    const StyleSheet &overrideFor(Breakpoint bp) const;
    bool hasOverride(Breakpoint bp) const;
    void clearOverride(Breakpoint bp);
    bool hasAnyOverride() const;

    //  Effective value for a property at a breakpoint (override wins).
    //  指定断点下属性的有效值（覆盖值优先）。
    QString resolve(Breakpoint bp, const QString &property) const;

    //  Effective, fully merged declaration map for a breakpoint.
    //  指定断点下完全合并后的有效声明表。
    QMap<QString, QString> resolved(Breakpoint bp) const;

    //  CSS text for the desktop rules plus the media-query overrides.
    //  桌面规则加上媒体查询覆盖后的 CSS 文本。
    QString toCss(const QString &selector, bool pretty = false) const;

    bool isEmpty() const;

    QJsonObject toJson() const;
    static ResponsiveStyle fromJson(const QJsonObject &obj);

private:
    StyleSheet m_base;
    QMap<int, StyleSheet> m_overrides; // keyed by Breakpoint
};

//  Media query text for a breakpoint.
//  断点对应的媒体查询文本。
QString mediaQueryFor(Breakpoint bp);
int breakpointMinWidth(Breakpoint bp);
int breakpointMinHeight(Breakpoint bp);

} // namespace tcbw
