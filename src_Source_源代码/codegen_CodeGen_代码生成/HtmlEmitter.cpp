// ============================================================================
//  HtmlEmitter.cpp -- renders the page tree as HTML
//  HtmlEmitter.cpp —— 将页面树渲染为 HTML
//  Part of TCTools-Build_Web (TC-BuildWeb)
// ============================================================================
#include "HtmlEmitter.h"

#include "CssEmitter.h"
#include "DocumentNode.h"
#include "EmitOptions.h"
#include "Page.h"

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

//  Attributes whose value is a URL and must therefore pass the URL allow-list.
//  取值是 URL、因而必须通过 URL 白名单的属性。
const QStringList &urlAttributes()
{
    static const QStringList s = {
        QStringLiteral("href"), QStringLiteral("src"), QStringLiteral("action"),
        QStringLiteral("poster"), QStringLiteral("formaction"), QStringLiteral("srcset"),
        QStringLiteral("data"), QStringLiteral("cite"), QStringLiteral("background"),
        QStringLiteral("xlink:href")
    };
    return s;
}

//  Attributes rendered as a bare name when present.
//  存在时仅以裸名字形式输出的属性。
const QStringList &booleanAttributes()
{
    static const QStringList s = {
        QStringLiteral("disabled"), QStringLiteral("required"), QStringLiteral("checked"),
        QStringLiteral("readonly"), QStringLiteral("multiple"), QStringLiteral("autofocus"),
        QStringLiteral("controls"), QStringLiteral("loop"), QStringLiteral("muted"),
        QStringLiteral("hidden"), QStringLiteral("open")
    };
    return s;
}

//  Tags whose textual content is not HTML and must be neutralised differently.
//  其文本内容不是 HTML、需要以不同方式中和的标签。
bool isRawTextTag(const QString &tag)
{
    const QString t = tag.trimmed().toLower();
    return t == QLatin1String("script") || t == QLatin1String("style");
}

//  Tags whose whitespace is significant and must not be re-indented.
//  空白有意义、不能重新缩进的标签。
bool isPreformattedTag(const QString &tag)
{
    const QString t = tag.trimmed().toLower();
    return t == QLatin1String("pre") || t == QLatin1String("textarea");
}

//  Splits a `srcset`-style value and verifies every candidate URL.
//  The whole attribute is dropped when any candidate fails.
//  拆分 `srcset` 风格的值并校验每个候选 URL。任一候选不通过即丢弃整个属性。
bool srcsetIsSafe(const QString &value)
{
    const QStringList candidates = value.split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (candidates.isEmpty())
        return false;
    for (const QString &candidate : candidates) {
        const QString url = candidate.trimmed().section(QLatin1Char(' '), 0, 0);
        if (url.isEmpty() || !isSafeUrl(url))
            return false;
    }
    return true;
}

//  Renders one `name="value"` pair, choosing the bare form for boolean
//  attributes.
//  渲染单个 `name="value"` 对，对布尔属性选择裸名字形式。
QString renderAttribute(const QString &name, const QString &value)
{
    if (booleanAttributes().contains(name))
        return QLatin1Char(' ') + name;
    return QLatin1Char(' ') + name + QStringLiteral("=\"") + escapeHtml(value, true)
           + QLatin1Char('"');
}

} // namespace

// ---------------------------------------------------------------------------
//  Construction
//  构造
// ---------------------------------------------------------------------------
HtmlEmitter::HtmlEmitter(const EmitOptions &options)
    : m_options(options)
{
}

// ---------------------------------------------------------------------------
//  Tag helpers
//  标签辅助函数
// ---------------------------------------------------------------------------
QString HtmlEmitter::voidTag(const QString &tag, const QMap<QString, QString> &attributes)
{
    QString out = QLatin1Char('<') + tag;
    for (auto it = attributes.constBegin(); it != attributes.constEnd(); ++it) {
        if (!isSafeAttributeName(it.key()))
            continue;
        out += renderAttribute(it.key(), it.value());
    }
    //  XHTML-style self closing keeps every XML-based downstream tool happy.
    //  采用 XHTML 风格自闭合，方便所有基于 XML 的下游工具。
    out += QStringLiteral(" />");
    return out;
}

QString HtmlEmitter::openTag(const QString &tag, const QMap<QString, QString> &attributes,
                             const QString &classAttribute, const QString &styleAttribute)
{
    QString out = QLatin1Char('<') + tag;

    for (auto it = attributes.constBegin(); it != attributes.constEnd(); ++it) {
        const QString name = it.key();
        if (!isSafeAttributeName(name))
            continue;
        //  `class` and `style` are supplied by the caller so that the generated
        //  class always wins over a stale hand-written one.
        //  `class` 与 `style` 由调用方提供，使生成的类名总能覆盖过时的手写值。
        if (name.compare(QLatin1String("class"), Qt::CaseInsensitive) == 0
            || name.compare(QLatin1String("style"), Qt::CaseInsensitive) == 0)
            continue;
        out += renderAttribute(name, it.value());
    }

    if (!classAttribute.isEmpty())
        out += QStringLiteral(" class=\"") + escapeHtml(classAttribute, true) + QLatin1Char('"');
    if (!styleAttribute.isEmpty())
        out += QStringLiteral(" style=\"") + escapeHtml(styleAttribute, true) + QLatin1Char('"');

    out += QLatin1Char('>');
    return out;
}

// ---------------------------------------------------------------------------
//  Inline styles
//  内联样式
// ---------------------------------------------------------------------------
QString HtmlEmitter::styleAttributeFor(const DocumentNode &node, Breakpoint bp,
                                       const EmitOptions &options)
{
    //  A stylesheet is used whenever the mode is not a self-contained single
    //  document, or when the caller explicitly asked for an external sheet.
    //  当模式不是自包含单文件，或调用方明确要求外部样式表时，就使用样式表。
    const bool externalSheet = (options.mode != ExportMode::SingleHtml) || !options.inlineCss;
    if (externalSheet)
        return QString();

    QMap<QString, QString> decls = node.style.resolved(bp);

    if (bp == Breakpoint::Desktop && node.layoutMode == LayoutMode::Free) {
        if (!decls.contains(QStringLiteral("position")))
            decls.insert(QStringLiteral("position"), QStringLiteral("absolute"));
        const QStringList required = {QStringLiteral("left"), QStringLiteral("top"),
                                      QStringLiteral("width"), QStringLiteral("height")};
        const QStringList values = {
            QString::number(node.freeRect.x(), 'f', 2) + QStringLiteral("px"),
            QString::number(node.freeRect.y(), 'f', 2) + QStringLiteral("px"),
            QString::number(node.freeRect.width(), 'f', 2) + QStringLiteral("px"),
            QString::number(node.freeRect.height(), 'f', 2) + QStringLiteral("px")
        };
        for (int k = 0; k < required.size(); ++k) {
            if (decls.contains(required.at(k)))
                continue;
            //  Mirror the semantics of the stylesheet emitter: when the author set
            //  one of the four, keep the other three too.
            //  与样式表生成器语义一致：作者设置了其中一项时，其余三项也一并补充。
            decls.insert(required.at(k), values.at(k));
        }
    }

    if (decls.isEmpty())
        return QString();

    QString out;
    for (auto it = decls.constBegin(); it != decls.constEnd(); ++it) {
        if (it.key().trimmed().isEmpty())
            continue;
        if (!out.isEmpty())
            out += QLatin1Char(' ');
        out += it.key().trimmed();
        out += QStringLiteral(": ");
        out += it.value().trimmed();
        out += QLatin1Char(';');
    }
    return out;
}

QString HtmlEmitter::emitInlineStyle(const DocumentNode &node) const
{
    //  Inline styles can only carry one breakpoint, so the desktop values are
    //  used: they are the base of the cascade and the responsive overrides live
    //  in the stylesheet. `styleAttributeFor` is public for callers that need a
    //  different breakpoint.
    //  内联样式只能承载一个断点，因此使用桌面端取值：它是层叠的基础，
    //  响应式覆盖则留在样式表中。需要其他断点的调用方可直接使用公开的
    //  `styleAttributeFor`。
    return styleAttributeFor(node, Breakpoint::Desktop, m_options);
}

QString HtmlEmitter::elementClass(const DocumentNode &node) const
{
    QStringList ordered;
    QSet<QString> seen;

    for (const QString &cls : node.classes) {
        const QString trimmed = cls.trimmed();
        if (trimmed.isEmpty() || seen.contains(trimmed))
            continue;
        seen.insert(trimmed);
        ordered.append(trimmed);
    }

    //  The generated class is kept identical to `CssEmitter::selectorFor` so the
    //  markup and the stylesheet can never drift apart.
    //  生成的类名与 `CssEmitter::selectorFor` 完全一致，使标记与样式表不会脱节。
    const QString generated = CssEmitter::selectorFor(node).mid(1);
    if (!generated.isEmpty() && !seen.contains(generated)) {
        seen.insert(generated);
        ordered.append(generated);
    }

    return ordered.join(QLatin1Char(' '));
}

QString HtmlEmitter::emitAttributes(const DocumentNode &node) const
{
    QString out;
    const QMap<QString, QString> attrs = node.effectiveAttributes();

    for (auto it = attrs.constBegin(); it != attrs.constEnd(); ++it) {
        const QString name = it.key().trimmed().toLower();
        if (name.isEmpty())
            continue;

        //  Event handlers and any other unvetted name never reach the document.
        //  事件处理器以及其他未经审查的名字绝不会进入文档。
        if (!isSafeAttributeName(name))
            continue;
        if (name == QLatin1String("class") || name == QLatin1String("style"))
            continue;

        const QString value = it.value();

        if (urlAttributes().contains(name)) {
            if (name == QLatin1String("srcset")) {
                if (!srcsetIsSafe(value))
                    continue;
            } else if (!isSafeUrl(value)) {
                continue;
            }
        }

        out += renderAttribute(name, value);
    }
    return out;
}

// ---------------------------------------------------------------------------
//  Nodes
//  节点
// ---------------------------------------------------------------------------
QString HtmlEmitter::emitBody(const Page &page) const
{
    if (!page.root)
        return QString();

    QString out;
    for (const auto &child : page.root->children()) {
        if (child->hidden)
            continue;
        out += emitNode(*child, 0);
    }
    return out;
}

QString HtmlEmitter::emitChildren(const DocumentNode &node, int depth) const
{
    QString out;
    for (const auto &child : node.children()) {
        if (child->hidden)
            continue;
        out += emitNode(*child, depth);
    }
    return out;
}

QString HtmlEmitter::emitNode(const DocumentNode &node, int depth) const
{
    const bool pretty = m_options.pretty;
    const int width = (m_options.indentWidth > 0 && m_options.indentWidth <= 8)
                          ? m_options.indentWidth
                          : 2;
    const QString pad = pretty ? QString(width * depth, QLatin1Char(' ')) : QString();
    const QString newline = pretty ? QStringLiteral("\n") : QString();

    if (node.hidden)
        return QString();

    //  --- text nodes --------------------------------------------------------
    //  —— 文本节点 ——
    if (node.kind == NodeKind::Text) {
        if (node.text.isEmpty())
            return QString();
        if (node.textIsHtml) {
            //  The author explicitly asked for markup; it is emitted verbatim, but
            //  a tag/closing-tag pair can never be forged from node text alone.
            //  作者明确要求使用标记；将原样输出，但仅凭节点文本无法伪造出标签对。
            return pad + node.text + newline;
        }
        return pad + escapeHtml(node.text, false) + newline;
    }

    if (node.kind == NodeKind::Slot)
        return QString();                       // slots are resolved before export / 导出前槽位已解析

    //  --- element nodes -----------------------------------------------------
    //  —— 元素节点 ——
    const QString tag = node.tag.trimmed().toLower();
    if (!isSafeTagName(tag)) {
        if (m_options.includeSourceComments)
            return pad + QStringLiteral("<!-- TCBW: unsafe tag '")
                   + escapeHtml(node.tag, false) + QStringLiteral("' skipped / 不安全的标签已跳过 -->")
                   + newline;
        return QString();
    }

    const QString classAttribute = elementClass(node);
    const QString styleAttribute = emitInlineStyle(node);
    const QString attrs = emitAttributes(node);

    //  Rebuild the attribute map for the static helpers: `openTag` filters again,
    //  which is cheap and keeps `voidTag` honest for callers outside this file.
    //  为静态辅助函数重建属性表：`openTag` 会再次过滤，代价很低，
    //  同时让 `voidTag` 对本文件之外的调用方也保持严谨。
    QMap<QString, QString> attrMap = node.effectiveAttributes();

    if (isVoidTag(tag) || node.isVoidElement())
        return pad + voidTag(tag, attrMap) + newline;

    //  Raw-text elements: content must not be parsed as markup.
    //  原始文本元素：内容不得被当作标记解析。
    if (isRawTextTag(tag)) {
        QString content = node.text;
        if (node.textIsHtml)
            content = node.text;                // already authored JS/CSS / 已是作者编写的 JS/CSS
        content = escapeJsForHtml(content);
        if (content.isEmpty())
            return pad + openTag(tag, attrMap, classAttribute, styleAttribute) + QLatin1Char('<')
                   + QLatin1Char('/') + tag + QLatin1Char('>') + newline;
        return pad + openTag(tag, attrMap, classAttribute, styleAttribute) + content
               + QStringLiteral("</") + tag + QLatin1Char('>') + newline;
    }

    //  Preformatted content keeps its own whitespace and indentation.
    //  预格式化内容保留自身的空白与缩进。
    if (isPreformattedTag(tag)) {
        QString content = node.textIsHtml ? node.text : escapeHtml(node.text, false);
        if (content.isEmpty()) {
            const QString inner = emitChildren(node, depth + 1);
            content = inner;
        }
        if (content.isEmpty())
            return pad + openTag(tag, attrMap, classAttribute, styleAttribute) + QStringLiteral("</")
                   + tag + QLatin1Char('>') + newline;
        return pad + openTag(tag, attrMap, classAttribute, styleAttribute) + content
               + QStringLiteral("</") + tag + QLatin1Char('>') + newline;
    }

    //  Ordinary element: emit a self-closing tag when there is nothing inside.
    //  普通元素：内部无内容时输出自闭合标签。
    const QString inner = emitChildren(node, depth + 1);
    QString ownText;
    if (!node.text.isEmpty()) {
        ownText = node.textIsHtml ? node.text : escapeHtml(node.text, false);
        if (pretty)
            ownText = pad + QString(width, QLatin1Char(' ')) + ownText + newline;
    }

    if (inner.isEmpty() && ownText.isEmpty())
        return pad + openTag(tag, attrMap, classAttribute, styleAttribute) + QStringLiteral("</")
               + tag + QLatin1Char('>') + newline;

    return pad + openTag(tag, attrMap, classAttribute, styleAttribute) + newline + ownText + inner
           + pad + QStringLiteral("</") + tag + QLatin1Char('>') + newline;
}

// ---------------------------------------------------------------------------
//  Head / body fragments
//  头部 / 主体片段
// ---------------------------------------------------------------------------
namespace {

//  `true` when the document is emitted as a set of files instead of one blob.
//  当文档以多文件而非单文件形式生成时返回 `true`。
[[maybe_unused]] bool usesExternalAssets(ExportMode mode)
{
    return mode == ExportMode::HtmlWithAssets || mode == ExportMode::HeadlessBundle;
}

} // namespace

//  Shared <head> builder. `dataScript` carries the exported page tree written by
//  the JS emitter; it is empty unless the caller asked for it.
//  共享的 <head> 构建函数。`dataScript` 携带由 JS 生成器写出的页面树；
//  除非调用方需要，否则为空。
static QString buildHead(const Page &page, const EmitOptions &options,
                         const QString &extraStyleContent, const QString &dataScript)
{
    const bool pretty = options.pretty;
    const int width = (options.indentWidth > 0 && options.indentWidth <= 8) ? options.indentWidth
                                                                           : 2;
    const QString pad(width, QLatin1Char(' '));
    const QString newline = pretty ? QStringLiteral("\n") : QString();

    const auto indentLine = [&](const QString &text) {
        return pretty ? pad + text + newline : text;
    };

    QString out = indentLine(QStringLiteral("<head>")) ;

    //  Charset first: it must appear within the first 1024 bytes.
    //  字符集放在最前：它必须出现在前 1024 字节内。
    const QString charset = page.charset.trimmed().isEmpty() ? QStringLiteral("utf-8")
                                                             : page.charset.trimmed();
    out += pretty ? pad + pad + QStringLiteral("<meta charset=\"") + escapeHtml(charset, true)
                        + QStringLiteral("\" />") + newline
                  : QStringLiteral("<meta charset=\"") + escapeHtml(charset, true)
                        + QStringLiteral("\" />");

    const QString viewport = page.viewport.trimmed().isEmpty()
                                 ? QStringLiteral("width=device-width, initial-scale=1")
                                 : page.viewport.trimmed();
    const QString viewportTag = QStringLiteral("<meta name=\"viewport\" content=\"")
                                + escapeHtml(viewport, true) + QStringLiteral("\" />");
    out += pretty ? pad + pad + viewportTag + newline : viewportTag;

    const QString title = page.title.trimmed().isEmpty()
                              ? (page.name.trimmed().isEmpty() ? QStringLiteral("Untitled")
                                                               : page.name.trimmed())
                              : page.title.trimmed();
    const QString titleTag = QStringLiteral("<title>") + escapeHtml(title, false)
                             + QStringLiteral("</title>");
    out += pretty ? pad + pad + titleTag + newline : titleTag;

    auto appendMeta = [&](const QString &name, const QString &content) {
        if (content.trimmed().isEmpty())
            return;
        const QString tag = QStringLiteral("<meta name=\"") + escapeHtml(name, true)
                            + QStringLiteral("\" content=\"") + escapeHtml(content, true)
                            + QStringLiteral("\" />");
        out += pretty ? pad + pad + tag + newline : tag;
    };

    appendMeta(QStringLiteral("description"), page.description);
    if (!page.keywords.isEmpty())
        appendMeta(QStringLiteral("keywords"), page.keywords.join(QStringLiteral(", ")));
    appendMeta(QStringLiteral("author"),
               page.author.trimmed().isEmpty() ? options.author : page.author);
    appendMeta(QStringLiteral("generator"),
               QStringLiteral("TCTools-Build_Web ") + versionString());
    appendMeta(QStringLiteral("tcbw-page"), page.id);

    //  Favicon: only when the URL passed the allow-list.
    //  站点图标：仅当 URL 通过白名单时输出。
    if (!page.faviconUrl.trimmed().isEmpty() && isSafeUrl(page.faviconUrl)) {
        const QString link = QStringLiteral("<link rel=\"icon\" href=\"")
                             + escapeHtml(page.faviconUrl.trimmed(), true) + QStringLiteral("\" />");
        out += pretty ? pad + pad + link + newline : link;
    }

    for (const QString &href : page.headLinks) {
        if (!isSafeUrl(href))
            continue;
        const QString link = QStringLiteral("<link rel=\"stylesheet\" href=\"")
                             + escapeHtml(href.trimmed(), true) + QStringLiteral("\" />");
        out += pretty ? pad + pad + link + newline : link;
    }

    for (const QString &src : page.headScripts) {
        if (!isSafeUrl(src))
            continue;
        const QString script = QStringLiteral("<script src=\"") + escapeHtml(src.trimmed(), true)
                               + QStringLiteral("\" defer></script>");
        out += pretty ? pad + pad + script + newline : script;
    }

    for (const QString &fragment : options.extraHeadHtml) {
        if (fragment.trimmed().isEmpty())
            continue;
        //  Fragments come from the trusted backend pre-linking subsystem; they are
        //  injected verbatim, which is exactly why they are not user text.
        //  片段来自受信任的后端预链接子系统；它们被原样注入，
        //  正因如此它们不是用户文本。
        out += pretty ? indentBlock(fragment, width * 2) + newline : fragment;
    }

    if (!page.customHeadHtml.trimmed().isEmpty())
        out += pretty ? indentBlock(page.customHeadHtml, width * 2) + newline
                      : page.customHeadHtml;

    if (!extraStyleContent.isEmpty()) {
        const QString styleOpen = QStringLiteral("<style>");
        const QString styleBody = pretty ? indentBlock(extraStyleContent, width * 2)
                                         : extraStyleContent;
        const QString styleClose = QStringLiteral("</style>");
        if (pretty)
            out += pad + pad + styleOpen + newline + styleBody + newline + pad + pad + styleClose
                   + newline;
        else
            out += styleOpen + styleBody + styleClose;
    }

    if (!dataScript.isEmpty()) {
        const QString scriptTag = QStringLiteral("<script>") + escapeJsForHtml(dataScript)
                                  + QStringLiteral("</script>");
        out += pretty ? pad + pad + scriptTag + newline : scriptTag;
    }

    out += indentLine(QStringLiteral("</head>"));
    return out;
}

//  Shared <body> tail builder: the inline data island, the extra script URLs,
//  the caller-supplied fragments and the <noscript> fallback.
//  共享的 <body> 尾部构建函数：内联数据岛、额外脚本 URL、调用方片段与
//  <noscript> 回退。
static QString buildBodyTail(const Page &page, const EmitOptions &options,
                             const QString &dataScript)
{
    const bool pretty = options.pretty;
    const int width = (options.indentWidth > 0 && options.indentWidth <= 8) ? options.indentWidth
                                                                           : 2;
    const QString pad(width, QLatin1Char(' '));
    const QString newline = pretty ? QStringLiteral("\n") : QString();

    QString out;

    if (!dataScript.isEmpty()) {
        const QString tag = QStringLiteral("<script>") + escapeJsForHtml(dataScript)
                            + QStringLiteral("</script>");
        out += pretty ? pad + tag + newline : tag;
    }

    for (const QString &url : options.extraScriptUrls) {
        if (!isSafeUrl(url))
            continue;
        const QString tag = QStringLiteral("<script src=\"") + escapeHtml(url.trimmed(), true)
                            + QStringLiteral("\"></script>");
        out += pretty ? pad + tag + newline : tag;
    }

    for (const QString &fragment : options.extraBodyEndHtml) {
        if (fragment.trimmed().isEmpty())
            continue;
        out += pretty ? indentBlock(fragment, width) + newline : fragment;
    }

    if (!page.customBodyEndHtml.trimmed().isEmpty())
        out += pretty ? indentBlock(page.customBodyEndHtml, width) + newline
                      : page.customBodyEndHtml;

    if (options.includeNoScriptFallback) {
        const QString message = QStringLiteral("This page needs JavaScript to run its programs. "
                                              "Please enable JavaScript and reload.")
                                + QStringLiteral(" / 本页面的程序需要 JavaScript 才能运行，"
                                                 "请启用 JavaScript 后重新加载。");
        const QString tag = QStringLiteral("<noscript><p class=\"tcbw-noscript\">")
                            + escapeHtml(message, false) + QStringLiteral("</p></noscript>");
        out += pretty ? pad + tag + newline : tag;
    }

    return out;
}

// ---------------------------------------------------------------------------
//  Documents
//  文档
// ---------------------------------------------------------------------------
QString HtmlEmitter::emitDocument(const Page &page) const
{
    const QString newline = QStringLiteral("\n");

    //  A single-file export always carries its CSS inline.
    //  单文件导出始终内联其 CSS。
    EmitOptions selfOptions = m_options;
    selfOptions.inlineCss = true;
    selfOptions.inlineJs = true;

    CssEmitter css(selfOptions);
    const QString styleContent = css.emitPage(page);

    const QString lang = page.language.trimmed().isEmpty() ? m_options.language
                                                           : page.language.trimmed();
    QString out;
    out += QStringLiteral("<!DOCTYPE html>") + newline;
    out += QStringLiteral("<html lang=\"") + escapeHtml(lang, true) + QStringLiteral("\">")
           + newline;
    out += buildHead(page, selfOptions, styleContent, QString());
    out += QStringLiteral("<body data-tcbw-page=\"") + escapeHtml(page.id, true)
           + QStringLiteral("\" data-tcbw-root=\"true\">") + newline;
    out += emitBody(page);
    out += buildBodyTail(page, selfOptions, QString());
    //  The executable script of the page is inserted by `CodeGenerator` at this
    //  marker: the HTML emitter is pure and never receives the compiled program.
    //  页面的可执行脚本由 `CodeGenerator` 在该标记处插入：
    //  HTML 生成器是纯函数，从不接收编译后的程序。
    out += QStringLiteral("<!-- tcbw:page-script -->") + newline;
    out += QStringLiteral("</body>") + newline;
    out += QStringLiteral("</html>") + newline;
    return out;
}

QString HtmlEmitter::emitDocumentWithAssets(const Page &page) const
{
    const QString newline = QStringLiteral("\n");
    const int width = (m_options.indentWidth > 0 && m_options.indentWidth <= 8)
                          ? m_options.indentWidth
                          : 2;
    const QString pad(width, QLatin1Char(' '));

    const QString lang = page.language.trimmed().isEmpty() ? m_options.language
                                                           : page.language.trimmed();

    QString head = buildHead(page, m_options, QString(), QString());
    //  Marker consumed by `CodeGenerator`, which turns it into a real
    //  `type="application/json"` data island.
    //  由 `CodeGenerator` 消费的标记，它会被转换为真正的
    //  `type="application/json"` 数据岛。
    head.insert(head.lastIndexOf(QStringLiteral("</head>")),
                pad + QStringLiteral("<!-- tcbw:page-data -->") + newline);
    //  Insert the stylesheet link just before </head>.
    //  在 </head> 之前插入样式表链接。
    const QString cssHref = m_options.cssFileName.trimmed().isEmpty() ? QStringLiteral("styles.css")
                                                                     : m_options.cssFileName;
    const QString linkTag = pad + QStringLiteral("<link rel=\"stylesheet\" href=\"")
                            + escapeHtml(cssHref, true) + QStringLiteral("\" />") + newline;
    const int headClose = head.lastIndexOf(QStringLiteral("</head>"));
    if (headClose >= 0)
        head.insert(headClose, linkTag);
    else
        head += linkTag;

    const QString jsSrc = m_options.jsFileName.trimmed().isEmpty() ? QStringLiteral("app.js")
                                                                  : m_options.jsFileName;
    const QString scriptTag = pad + QStringLiteral("<script src=\"") + escapeHtml(jsSrc, true)
                              + QStringLiteral("\" defer></script>") + newline;

    QString out;
    out += QStringLiteral("<!DOCTYPE html>") + newline;
    out += QStringLiteral("<html lang=\"") + escapeHtml(lang, true) + QStringLiteral("\">") + newline;
    out += head;
    out += QStringLiteral("<body data-tcbw-page=\"") + escapeHtml(page.id, true)
           + QStringLiteral("\" data-tcbw-root=\"true\">") + newline;
    out += emitBody(page);
    out += buildBodyTail(page, m_options, QString());
    out += scriptTag;
    out += QStringLiteral("</body>") + newline;
    out += QStringLiteral("</html>") + newline;
    return out;
}

QString HtmlEmitter::emitScriptHost(const Page &page) const
{
    const QString newline = QStringLiteral("\n");
    const int width = (m_options.indentWidth > 0 && m_options.indentWidth <= 8)
                          ? m_options.indentWidth
                          : 2;
    const QString pad(width, QLatin1Char(' '));

    const QString lang = page.language.trimmed().isEmpty() ? m_options.language
                                                           : page.language.trimmed();

    const QString title = page.title.trimmed().isEmpty()
                              ? (page.name.trimmed().isEmpty() ? QStringLiteral("App")
                                                               : page.name.trimmed())
                              : page.title.trimmed();

    //  The script file name for this page is derived by the generator; the host
    //  only knows the shared base name when none was supplied.
    //  该页面的脚本文件名由生成器推导；未提供时宿主只使用共享的基名。
    QString jsSrc = m_options.jsFileName.trimmed().isEmpty() ? QStringLiteral("app.js")
                                                            : m_options.jsFileName;

    QString out;
    out += QStringLiteral("<!DOCTYPE html>") + newline;
    out += QStringLiteral("<html lang=\"") + escapeHtml(lang, true) + QStringLiteral("\">") + newline;
    out += QStringLiteral("<head>") + newline;
    out += pad + QStringLiteral("<meta charset=\"utf-8\" />") + newline;
    out += pad + QStringLiteral("<meta name=\"viewport\" content=\"width=device-width, "
                                "initial-scale=1\" />") + newline;
    out += pad + QStringLiteral("<title>") + escapeHtml(title, false) + QStringLiteral("</title>")
           + newline;
    out += pad + QStringLiteral("<meta name=\"generator\" content=\"TCTools-Build_Web ")
           + escapeHtml(versionString(), true) + QStringLiteral("\" />") + newline;
    if (!page.description.trimmed().isEmpty())
        out += pad + QStringLiteral("<meta name=\"description\" content=\"")
               + escapeHtml(page.description, true) + QStringLiteral("\" />") + newline;

    for (const QString &href : page.headLinks) {
        if (!isSafeUrl(href))
            continue;
        out += pad + QStringLiteral("<link rel=\"stylesheet\" href=\"")
               + escapeHtml(href.trimmed(), true) + QStringLiteral("\" />") + newline;
    }
    out += QStringLiteral("</head>") + newline;
    out += QStringLiteral("<body data-tcbw-page=\"") + escapeHtml(page.id, true)
           + QStringLiteral("\">") + newline;
    out += pad + QStringLiteral("<!-- The DOM is built by the exported script. -->") + newline;
    out += pad + QStringLiteral("<!-- DOM 由导出的脚本构建。 -->") + newline;
    out += pad + QStringLiteral("<div class=\"tcbw-visually-hidden\">")
           + QStringLiteral("<a class=\"tcbw-skip-link\" href=\"#tcbw-main\">Skip to content</a>")
           + QStringLiteral("</div>") + newline;
    out += buildBodyTail(page, m_options, QString());
    out += pad + QStringLiteral("<script src=\"") + escapeHtml(jsSrc, true)
           + QStringLiteral("\" defer></script>") + newline;
    out += QStringLiteral("</body>") + newline;
    out += QStringLiteral("</html>") + newline;
    return out;
}

QString HtmlEmitter::emitSiteIndex(const QVector<Page *> &pages) const
{
    if (pages.isEmpty())
        return QString();

    const QString newline = QStringLiteral("\n");
    const int width = (m_options.indentWidth > 0 && m_options.indentWidth <= 8)
                          ? m_options.indentWidth
                          : 2;
    const QString pad(width, QLatin1Char(' '));

    const QString siteTitle = m_options.siteTitle.trimmed().isEmpty()
                                  ? QStringLiteral("Site index")
                                  : m_options.siteTitle.trimmed();

    QString out;
    out += QStringLiteral("<!DOCTYPE html>") + newline;
    out += QStringLiteral("<html lang=\"") + escapeHtml(m_options.language, true)
           + QStringLiteral("\">") + newline;
    out += QStringLiteral("<head>") + newline;
    out += pad + QStringLiteral("<meta charset=\"utf-8\" />") + newline;
    out += pad + QStringLiteral("<meta name=\"viewport\" content=\"width=device-width, "
                                "initial-scale=1\" />") + newline;
    out += pad + QStringLiteral("<title>") + escapeHtml(siteTitle, false)
           + QStringLiteral("</title>") + newline;
    if (!m_options.siteDescription.trimmed().isEmpty())
        out += pad + QStringLiteral("<meta name=\"description\" content=\"")
               + escapeHtml(m_options.siteDescription, true) + QStringLiteral("\" />") + newline;
    out += QStringLiteral("</head>") + newline;

    //  The index carries only the reset plus a handful of tokens so it stays
    //  independent of the generated stylesheet.
    //  目录页只带重置与少量令牌，从而不依赖生成的样式表。
    out += pad + QStringLiteral("<style>") + newline;
    out += indentBlock(QStringLiteral("body{font-family:var(--tcbw-font-sans,system-ui);"
                                      "margin:2rem;color:var(--tcbw-color-text,#1b1f24);"
                                      "background:var(--tcbw-color-bg,#fff)}"
                                      "h1{font-size:1.5rem}ul{line-height:1.9;padding-left:1.2rem}"),
                       width)
           + newline;
    out += pad + QStringLiteral("</style>") + newline;

    out += QStringLiteral("<body>") + newline;
    out += pad + QStringLiteral("<h1>") + escapeHtml(siteTitle, false) + QStringLiteral("</h1>")
           + newline;
    out += pad + QStringLiteral("<ul>") + newline;
    for (const Page *page : pages) {
        if (!page)
            continue;
        QString href = page->fileName.trimmed();
        if (href.isEmpty())
            href = page->name.trimmed();
        if (href.isEmpty())
            href = QStringLiteral("index.html");
        //  The home page is always available as index.html.
        //  首页始终可以通过 index.html 访问。
        if (page->isHome)
            href = QStringLiteral("index.html");

        const QString label = page->title.trimmed().isEmpty() ? page->name.trimmed()
                                                             : page->title.trimmed();
        out += pad + pad + QStringLiteral("<li><a href=\"") + escapeHtml(href, true)
               + QStringLiteral("\">") + escapeHtml(label, false) + QStringLiteral("</a></li>")
               + newline;
    }
    out += pad + QStringLiteral("</ul>") + newline;
    out += QStringLiteral("</body>") + newline;
    out += QStringLiteral("</html>") + newline;
    return out;
}

} // namespace tcbw
