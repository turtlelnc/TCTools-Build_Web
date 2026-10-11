// ============================================================================
//  CssEmitter.cpp -- renders styles as stylesheets
//  CssEmitter.cpp —— 将样式渲染为样式表
//  Part of TCTools-Build_Web (TC-BuildWeb)
// ============================================================================
#include "CssEmitter.h"

#include "DocumentNode.h"
#include "EmitOptions.h"
#include "Page.h"
#include "StyleSheet.h"

#include <QChar>
#include <QLatin1String>
#include <QList>
#include <QMap>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>

namespace tcbw {

namespace {

//  Selector suffix derived from a node id: the last six alphanumeric characters.
//  由节点 id 推导出的选择器后缀：最后的六个字母或数字。
QString shortIdFor(const DocumentNode &node)
{
    QString cleaned;
    cleaned.reserve(node.id.size());
    for (const QChar c : node.id) {
        const ushort u = c.unicode();
        const bool alnum = (u >= '0' && u <= '9') || (u >= 'a' && u <= 'z')
                           || (u >= 'A' && u <= 'Z');
        if (alnum)
            cleaned += c.toLower();
    }
    if (cleaned.isEmpty())
        return QStringLiteral("node");
    if (cleaned.size() <= 6)
        return cleaned;
    return cleaned.right(6);
}

//  Builds `selector { ... }` for one node at one breakpoint, merging the resolved
//  declarations with the free-layout geometry.
//  为某个节点在某个断点构建 `selector { ... }`，把解析后的声明与自由布局几何合并。
QString ruleText(const DocumentNode &node, Breakpoint bp, const QString &selector,
                 const EmitOptions &options)
{
    QMap<QString, QString> decls = node.style.resolved(bp);

    //  Free layout is a desktop-side concept: the canvas rectangle is translated
    //  into `position:absolute` plus left/top/width/height, but only for
    //  properties the author did not already set explicitly.
    //  自由布局属于桌面端概念：画布矩形被转换为 `position:absolute` 以及
    //  left/top/width/height，但仅当作者没有显式设置这些属性时才补充。
    if (bp == Breakpoint::Desktop && node.layoutMode == LayoutMode::Free) {
        if (!decls.contains(QStringLiteral("position")))
            decls.insert(QStringLiteral("position"), QStringLiteral("absolute"));

        const QString left = QString::number(node.freeRect.x(), 'f', 2);
        const QString top = QString::number(node.freeRect.y(), 'f', 2);
        const QString width = QString::number(node.freeRect.width(), 'f', 2);
        const QString height = QString::number(node.freeRect.height(), 'f', 2);

        if (!decls.contains(QStringLiteral("left")))
            decls.insert(QStringLiteral("left"), left + QStringLiteral("px"));
        if (!decls.contains(QStringLiteral("top")))
            decls.insert(QStringLiteral("top"), top + QStringLiteral("px"));
        if (!decls.contains(QStringLiteral("width")))
            decls.insert(QStringLiteral("width"), width + QStringLiteral("px"));
        if (!decls.contains(QStringLiteral("height")))
            decls.insert(QStringLiteral("height"), height + QStringLiteral("px"));
    }

    if (decls.isEmpty())
        return QString();

    const bool pretty = options.pretty;
    const int indent = (options.indentWidth > 0 && options.indentWidth <= 8)
                           ? options.indentWidth
                           : 2;

    QString out;
    if (pretty) {
        out += selector + QStringLiteral(" {\n");
        for (auto it = decls.constBegin(); it != decls.constEnd(); ++it) {
            out += QString(indent, QLatin1Char(' '));
            out += it.key();
            out += QStringLiteral(": ");
            out += it.value();
            out += QStringLiteral(";\n");
        }
        out += QStringLiteral("}\n");
    } else {
        out += selector + QLatin1Char('{');
        for (auto it = decls.constBegin(); it != decls.constEnd(); ++it) {
            out += it.key();
            out += QLatin1Char(':');
            out += it.value();
            out += QLatin1Char(';');
        }
        out += QStringLiteral("}\n");
    }
    return out;
}

//  Full rule set for one node: the desktop rule plus the responsive overrides.
//  单个节点的完整规则集：桌面规则加上响应式覆盖。
QString rulesFor(const DocumentNode &node, const EmitOptions &options)
{
    const QString selector = CssEmitter::selectorFor(node);

    //  Nothing to say about this node at all.
    //  该节点完全没有需要输出的内容。
    if (node.style.isEmpty() && node.layoutMode != LayoutMode::Free)
        return QString();

    QString out = ruleText(node, Breakpoint::Desktop, selector, options);
    if (!options.responsive)
        return out;

    //  Narrowest first so the cascade matches the desktop-first overrides.
    //  先窄后宽，使层叠顺序与桌面优先的覆盖规则一致。
    const QVector<Breakpoint> order = {Breakpoint::Mobile, Breakpoint::Tablet};
    for (Breakpoint bp : order) {
        if (!node.style.hasOverride(bp))
            continue;
        const QString body = ruleText(node, bp, selector, options);
        if (body.isEmpty())
            continue;

        const QString query = QStringLiteral("@media ") + mediaQueryFor(bp);
        if (options.pretty) {
            out += query + QStringLiteral(" {\n");
            out += indentBlock(body, options.indentWidth);
            out += QStringLiteral("}\n");
        } else {
            out += query + QLatin1Char('{') + body.trimmed() + QStringLiteral("}\n");
        }
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
//  Construction
//  构造
// ---------------------------------------------------------------------------
CssEmitter::CssEmitter(const EmitOptions &options)
    : m_options(options)
{
}

// ---------------------------------------------------------------------------
//  Selectors
//  选择器
// ---------------------------------------------------------------------------
QString CssEmitter::selectorFor(const DocumentNode &node)
{
    //  Reuse an existing generated class so the markup and the stylesheet agree.
    //  复用已有的生成类名，使标记与样式表保持一致。
    for (const QString &cls : node.classes) {
        const QString trimmed = cls.trimmed();
        if (trimmed.startsWith(QLatin1String("tcbw-")))
            return QLatin1Char('.') + trimmed;
    }

    const QString source = !node.name.trimmed().isEmpty() ? node.name : node.tag;
    return QStringLiteral(".tcbw-") + slugify(source) + QLatin1Char('-') + shortIdFor(node);
}

// ---------------------------------------------------------------------------
//  Page stylesheet
//  页面样式表
// ---------------------------------------------------------------------------
QString CssEmitter::emitPage(const Page &page) const
{
    QString out;

    if (m_options.includeResetCss)
        out += emitReset();

    out += emitTokens(page);

    if (m_options.includeAccessibilityHelpers)
        out += emitAccessibilityHelpers();

    out += emitTree(page);

    return m_options.minifyCss ? minify(out) : out;
}

QString CssEmitter::emitTree(const Page &page) const
{
    QString out;
    if (!page.root)
        return out;

    //  The page body itself is styled through the markup, every other node
    //  through its generated class.
    //  页面主体通过标记本身设置样式，其他节点通过其生成的类名设置样式。
    const DocumentNode *root = page.root.get();
    if (root->style.isEmpty() && root->layoutMode != LayoutMode::Free) {
        //  nothing for the root / 根节点无需输出
    } else {
        out += ruleText(*root, Breakpoint::Desktop, QStringLiteral("body"), m_options);
        if (m_options.responsive) {
            const QVector<Breakpoint> order = {Breakpoint::Mobile, Breakpoint::Tablet};
            for (Breakpoint bp : order) {
                if (!root->style.hasOverride(bp))
                    continue;
                const QString body = ruleText(*root, bp, QStringLiteral("body"), m_options);
                if (body.isEmpty())
                    continue;
                const QString query = QStringLiteral("@media ") + mediaQueryFor(bp);
                if (m_options.pretty) {
                    out += query + QStringLiteral(" {\n");
                    out += indentBlock(body, m_options.indentWidth);
                    out += QStringLiteral("}\n");
                } else {
                    out += query + QLatin1Char('{') + body.trimmed() + QStringLiteral("}\n");
                }
            }
        }
    }

    for (const auto &child : root->children())
        out += emitNodeRules(*child);

    return out;
}

QString CssEmitter::emitNodeRules(const DocumentNode &node) const
{
    QString out = rulesFor(node, m_options);
    for (const auto &child : node.children())
        out += emitNodeRules(*child);
    return out;
}

// ---------------------------------------------------------------------------
//  Design tokens
//  设计令牌
// ---------------------------------------------------------------------------
QString CssEmitter::emitTokens(const Page &page) const
{
    //  Tokens every exported page relies on; only emitted when the page did not
    //  define its own value.
    //  所有导出页面都会用到的令牌；仅当页面未自行定义时才输出。
    static const QVector<QPair<QString, QString>> shared = {
        {QStringLiteral("--tcbw-radius"), QStringLiteral("10px")},
        {QStringLiteral("--tcbw-gap"), QStringLiteral("16px")},
        {QStringLiteral("--tcbw-font-sans"),
         QStringLiteral("\"Segoe UI\", Roboto, \"Helvetica Neue\", Arial, "
                        "\"Noto Sans SC\", \"Microsoft YaHei\", sans-serif")},
        {QStringLiteral("--tcbw-font-mono"),
         QStringLiteral("ui-monospace, SFMono-Regular, Menlo, Consolas, "
                        "\"Liberation Mono\", monospace")},
        {QStringLiteral("--tcbw-color-text"), QStringLiteral("#1b1f24")},
        {QStringLiteral("--tcbw-color-bg"), QStringLiteral("#ffffff")},
        {QStringLiteral("--tcbw-color-primary"), QStringLiteral("#2563eb")},
        {QStringLiteral("--tcbw-color-muted"), QStringLiteral("#6b7280")},
        {QStringLiteral("--tcbw-shadow-1"), QStringLiteral("0 1px 2px rgba(0, 0, 0, .08)")},
        {QStringLiteral("--tcbw-shadow-2"), QStringLiteral("0 8px 24px rgba(0, 0, 0, .14)")},
        {QStringLiteral("--tcbw-transition"), QStringLiteral("160ms ease-in-out")}
    };

    const bool pretty = m_options.pretty;
    const int indent = (m_options.indentWidth > 0 && m_options.indentWidth <= 8)
                           ? m_options.indentWidth
                           : 2;

    QString out;
    QString body;

    auto appendDeclaration = [&](const QString &name, const QString &value) {
        if (pretty) {
            body += QString(indent, QLatin1Char(' '));
            body += name;
            body += QStringLiteral(": ");
            body += value;
            body += QStringLiteral(";\n");
        } else {
            body += name;
            body += QLatin1Char(':');
            body += value;
            body += QLatin1Char(';');
        }
    };

    for (auto it = page.cssVariables.constBegin(); it != page.cssVariables.constEnd(); ++it) {
        if (it.key().trimmed().isEmpty() || it.value().trimmed().isEmpty())
            continue;
        appendDeclaration(it.key().trimmed(), it.value().trimmed());
    }

    for (const auto &pair : shared) {
        if (page.cssVariables.contains(pair.first))
            continue;
        appendDeclaration(pair.first, pair.second);
    }

    if (body.isEmpty())
        return out;

    if (pretty)
        out += QStringLiteral(":root {\n") + body + QStringLiteral("}\n");
    else
        out += QStringLiteral(":root{") + body + QStringLiteral("}\n");
    return out;
}

// ---------------------------------------------------------------------------
//  Reset
//  重置
// ---------------------------------------------------------------------------
QString CssEmitter::emitReset() const
{
    //  Order matters: the box model, then the document, then replaced content.
    //  顺序很重要：先是盒模型，再是文档，最后是替换内容。
    static const QStringList rules = {
        QStringLiteral("*,*::before,*::after{box-sizing:border-box}"),
        QStringLiteral("html{-webkit-text-size-adjust:100%}"),
        QStringLiteral("body{margin:0}"),
        QStringLiteral("img,video,canvas,svg{display:block;max-width:100%;height:auto}"),
        QStringLiteral("button,input,select,textarea{font:inherit}"),
        QStringLiteral("a{color:inherit}"),
        QStringLiteral(":focus-visible{outline:2px solid currentColor;outline-offset:2px}")
    };

    QString out;
    out += QStringLiteral("/* TCBW reset -- modern, minimal */\n");
    out += QStringLiteral("/* TCBW 重置 —— 现代、精简 */\n");
    for (const QString &rule : rules) {
        if (m_options.pretty) {
            //  Expand `selector{decls}` onto three lines for readability.
            //  为便于阅读，将 `选择器{声明}` 展开为三行。
            const int brace = rule.indexOf(QLatin1Char('{'));
            if (brace > 0) {
                const QString selector = rule.left(brace);
                const QString decls = rule.mid(brace + 1, rule.size() - brace - 2);
                QStringList parts = decls.split(QLatin1Char(';'), Qt::SkipEmptyParts);
                out += selector + QStringLiteral(" {\n");
                for (QString &part : parts) {
                    part = part.trimmed();
                    if (part.isEmpty())
                        continue;
                    out += indentBlock(part + QStringLiteral(";"), m_options.indentWidth)
                           + QStringLiteral("\n");
                }
                out += QStringLiteral("}\n");
            } else {
                out += rule + QStringLiteral("\n");
            }
        } else {
            out += rule + QStringLiteral("\n");
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
//  Accessibility helpers
//  无障碍辅助类
// ---------------------------------------------------------------------------
QString CssEmitter::emitAccessibilityHelpers() const
{
    static const QStringList rules = {
        QStringLiteral(".tcbw-visually-hidden{position:absolute!important;width:1px;height:1px;"
                       "padding:0;margin:-1px;overflow:hidden;clip:rect(0 0 0 0);"
                       "clip-path:inset(50%);white-space:nowrap;border:0}"),
        QStringLiteral(".tcbw-visually-hidden:focus-within,.tcbw-visually-hidden:active{"
                       "position:static!important;width:auto;height:auto;margin:0;overflow:visible;"
                       "clip:auto;clip-path:none;white-space:normal}"),
        QStringLiteral(".tcbw-skip-link{position:absolute;left:-9999px;top:0;z-index:2147483647;"
                       "padding:.5rem 1rem;background:var(--tcbw-color-bg,#fff);"
                       "color:var(--tcbw-color-text,#1b1f24);border-radius:var(--tcbw-radius,10px);"
                       "box-shadow:var(--tcbw-shadow-2,0 8px 24px rgba(0,0,0,.14));"
                       "text-decoration:none}"),
        QStringLiteral(".tcbw-skip-link:focus{left:1rem;top:1rem}"),
        QStringLiteral("[hidden]{display:none!important}")
    };

    QString out;
    out += QStringLiteral("/* TCBW accessibility helpers */\n");
    out += QStringLiteral("/* TCBW 无障碍辅助 */\n");
    for (const QString &rule : rules) {
        if (!m_options.pretty) {
            out += rule + QStringLiteral("\n");
            continue;
        }
        const int brace = rule.indexOf(QLatin1Char('{'));
        if (brace <= 0) {
            out += rule + QStringLiteral("\n");
            continue;
        }
        const QString selector = rule.left(brace);
        const QString decls = rule.mid(brace + 1, rule.size() - brace - 2);
        const QStringList parts = decls.split(QLatin1Char(';'), Qt::SkipEmptyParts);
        out += selector + QStringLiteral(" {\n");
        for (const QString &part : parts)
            out += indentBlock(part.trimmed() + QStringLiteral(";"), m_options.indentWidth)
                   + QStringLiteral("\n");
        out += QStringLiteral("}\n");
    }

    //  Respect the operating-system motion preference.
    //  尊重操作系统的动效偏好设置。
    out += QStringLiteral("@media (prefers-reduced-motion: reduce) {\n");
    if (m_options.pretty) {
        out += QStringLiteral("  *,*::before,*::after {\n");
        out += QStringLiteral("    animation-duration: .001ms !important;\n");
        out += QStringLiteral("    animation-iteration-count: 1 !important;\n");
        out += QStringLiteral("    transition-duration: .001ms !important;\n");
        out += QStringLiteral("    scroll-behavior: auto !important;\n");
        out += QStringLiteral("  }\n");
    } else {
        out += QStringLiteral("*,*::before,*::after{animation-duration:.001ms!important;"
                              "animation-iteration-count:1!important;transition-duration:.001ms"
                              "!important;scroll-behavior:auto!important}\n");
    }
    out += QStringLiteral("}\n");
    return out;
}

// ---------------------------------------------------------------------------
//  Minifier
//  压缩器
// ---------------------------------------------------------------------------
QString CssEmitter::minify(const QString &css)
{
    QString out;
    out.reserve(css.size());

    const int n = css.size();
    int i = 0;
    int pendingSpaces = 0;
    int parenDepth = 0;
    //  `true` while the scanner sits directly inside a declaration block; there a
    //  colon separates a property from its value and the surrounding whitespace is
    //  decorative. A colon at brace depth zero belongs to a selector or to a media
    //  query and must keep its spacing.
    //  当扫描器直接位于声明块内部时为 `true`；此时冒号分隔属性与值，两侧空白可以
    //  删除。花括号深度为零时的冒号属于选择器或媒体查询，必须保留空白。
    bool declarationContext = false;

    //  The last significant character already written, used as the left context
    //  of a whitespace run.
    //  已写出的最后一个有效字符，用作空白段的左侧上下文。
    auto prevSignificant = [&]() -> QChar {
        return out.isEmpty() ? QChar() : out.at(out.size() - 1);
    };

    //  Characters CSS permits whitespace to be removed around, outside any
    //  parenthesis. `(` and `)` are deliberately absent: the contents of
    //  `calc()`, `url()` and `rgba()` must keep their separators.
    //  CSS 允许在其两侧删除空白的字符（不在任何圆括号内）。
    //  此处刻意不含 `(` 与 `)`：`calc()`、`url()` 与 `rgba()` 的内容必须保留分隔符。
    auto dropSpaceAround = [](QChar c) {
        static const QString set = QStringLiteral("{};,>+~");
        return c.isNull() ? false : set.contains(c);
    };

    //  Colon and comma are only expendable inside a declaration block.
    //  冒号与逗号仅可在声明块内部删除空白。
    auto dropSpaceAroundColon = [&declarationContext](QChar c) {
        if (!declarationContext || c.isNull())
            return false;
        return c == QLatin1Char(':') || c == QLatin1Char(',');
    };

    while (i < n) {
        const QChar c = css.at(i);

        //  Comment: skipped wholesale.
        //  注释：整体跳过。
        if (c == QLatin1Char('/') && i + 1 < n && css.at(i + 1) == QLatin1Char('*')) {
            i += 2;
            while (i + 1 < n && !(css.at(i) == QLatin1Char('*')
                                  && css.at(i + 1) == QLatin1Char('/')))
                ++i;
            i = (i + 1 < n) ? i + 2 : n;
            continue;
        }

        //  String literal: copied verbatim, including nested escapes.
        //  字符串字面量：原样复制，包含转义序列。
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            const QChar quote = c;
            out += c;
            ++i;
            while (i < n) {
                const QChar s = css.at(i);
                out += s;
                ++i;
                if (s == QLatin1Char('\\') && i < n) {
                    out += css.at(i);
                    ++i;
                    continue;
                }
                if (s == quote)
                    break;
            }
            continue;
        }

        //  Whitespace run: collapse to at most one space and drop it entirely
        //  around the punctuation listed above.
        //  空白段：最多折叠为一个空格，并在上述标点两侧彻底删除。
        if (c.isSpace()) {
            ++i;
            ++pendingSpaces;
            continue;
        }

        if (pendingSpaces > 0) {
            const QChar prev = prevSignificant();
            //  Inside parentheses a colon may belong to a media-query condition
            //  (`@media (max-width: 767px)`), where the space is required, so
            //  nothing is removed while the parenthesis depth is not zero.
            //  在圆括号内部，冒号可能属于媒体查询条件
            //  （`@media (max-width: 767px)`），其中的空格是必须的，
            //  因此圆括号深度不为零时不删除任何空白。
            const bool drop = (parenDepth == 0)
                              && (dropSpaceAround(prev) || dropSpaceAround(c)
                                  || dropSpaceAroundColon(prev) || dropSpaceAroundColon(c));
            if (!drop && !prev.isNull())
                out += QLatin1Char(' ');
            pendingSpaces = 0;
        }

        if (c == QLatin1Char('(')) {
            ++parenDepth;
        } else if (c == QLatin1Char(')') && parenDepth > 0) {
            --parenDepth;
        } else if (c == QLatin1Char('{')) {
            //  A declaration block starts here, unless the current at-rule block
            //  is itself a declaration block (nested `@supports` is rare and
            //  keeping the spaces is always safe).
            //  声明块从这里开始，除非当前 at-rule 块本身就是声明块
            //  （嵌套 `@supports` 很少见，而保留空白始终是安全的）。
            declarationContext = (parenDepth == 0);
        } else if (c == QLatin1Char('}')) {
            declarationContext = false;
        }

        out += c;
        ++i;
    }

    //  Drop the last `;` before every `}`.
    //  删除每个 `}` 之前的最后一个 `;`。
    QString compact;
    compact.reserve(out.size());
    for (int k = 0; k < out.size(); ++k) {
        const QChar c = out.at(k);
        if (c == QLatin1Char(';')) {
            int look = k + 1;
            while (look < out.size() && out.at(look).isSpace())
                ++look;
            if (look < out.size() && out.at(look) == QLatin1Char('}'))
                continue;                       // drop this semicolon / 丢弃该分号
        }
        compact += c;
    }

    return compact.trimmed();
}

} // namespace tcbw
