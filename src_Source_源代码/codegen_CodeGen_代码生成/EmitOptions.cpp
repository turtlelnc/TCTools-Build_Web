// ============================================================================
//  EmitOptions.cpp -- shared options for every code emitter
//  EmitOptions.cpp —— 所有代码生成器共享的选项
//  Part of TCTools-Build_Web (TC-BuildWeb)
// ============================================================================
#include "EmitOptions.h"

#include <QChar>
#include <QDir>
#include <QHash>
#include <QLatin1String>
#include <QRegularExpression>
#include <QStringList>

namespace tcbw {

namespace {

//  Default mount point used when no page is known.
//  未知页面时使用的默认挂载点。
QLatin1String kDefaultMountPoint("[data-tcbw-root]");

//  Media types that may legitimately appear inside a `data:` URL.
//  `data:` URL 中允许出现的媒体类型。
bool isAllowedDataMediaType(const QString &schemeRest)
{
    if (schemeRest.isEmpty())
        return false;

    //  A data URL looks like `<media-type>[;param...][,<payload>]`.
    //  data URL 形如 `<media-type>[;参数……][,<负载>]`。
    const int comma = schemeRest.indexOf(QLatin1Char(','));
    if (comma < 0)
        return false;                       // no payload -> not a usable URL / 无负载，不可用

    QString descriptor = schemeRest.left(comma);
    const int semi = descriptor.indexOf(QLatin1Char(';'));
    if (semi >= 0)
        descriptor = descriptor.left(semi);
    descriptor = descriptor.trimmed().toLower();

    //  A missing media type means the implicit `text/plain`.
    //  缺少媒体类型意味着隐式的 `text/plain`。
    if (descriptor.isEmpty())
        return false;

    if (descriptor.startsWith(QLatin1String("image/")))
        return true;

    //  Fonts are allowed too: they cannot script the embedding document.
    //  字体同样允许：它无法在嵌入文档中执行脚本。
    static const QStringList fontTypes = {
        QStringLiteral("font/"),
        QStringLiteral("application/font-woff"),
        QStringLiteral("application/font-woff2"),
        QStringLiteral("application/x-font-woff"),
        QStringLiteral("application/x-font-ttf"),
        QStringLiteral("application/x-font-opentype"),
        QStringLiteral("application/vnd.ms-fontobject")
    };
    for (const QString &prefix : fontTypes) {
        if (descriptor.startsWith(prefix))
            return true;
    }
    return false;
}

} // namespace

// ---------------------------------------------------------------------------
//  EmitOptions
//  生成选项
// ---------------------------------------------------------------------------

//  Derives the block-compiler options from the emitter flags. The mount point is
//  the data attribute `document.js` puts on the page root; the module name is
//  derived from the export mode so a plain `.js` export still has a sensible
//  global, while page-level emitters override it with the real page name.
//  由生成器标志推导积木编译器选项。挂载点是 `document.js` 写在页面根节点上的
//  data 属性；模块名由导出模式推导，使纯 `.js` 导出仍有合理的全局名，而页面级
//  生成器会用真实的页面名覆盖它。
CompileOptions EmitOptions::compileOptions() const
{
    CompileOptions out = compile;

    out.pretty = pretty;
    out.indentWidth = (indentWidth > 0 && indentWidth <= 8) ? indentWidth : 2;
    out.targetElementSelector = QString(kDefaultMountPoint);

    if (out.moduleName.trimmed().isEmpty()) {
        switch (mode) {
        case ExportMode::SingleHtml:
            out.moduleName = QStringLiteral("TCBuildWebPage");
            break;
        case ExportMode::HtmlWithAssets:
            out.moduleName = QStringLiteral("TCBuildWebSite");
            break;
        case ExportMode::ScriptOnly:
            out.moduleName = QStringLiteral("TCBuildWebScript");
            break;
        case ExportMode::HeadlessBundle:
            out.moduleName = QStringLiteral("TCBuildWebBundle");
            break;
        }
    }

    return out;
}

// ---------------------------------------------------------------------------
//  EmitResult
//  生成结果
// ---------------------------------------------------------------------------
const EmittedFile *EmitResult::file(const QString &relativePath) const
{
    const QString wanted = QDir::fromNativeSeparators(relativePath);
    for (const EmittedFile &f : files) {
        if (QDir::fromNativeSeparators(f.relativePath) == wanted)
            return &f;
    }
    //  Second pass: compare case-insensitively on case-insensitive platforms and
    //  ignore a leading "./" on every platform.
    //  第二遍：在大小写不敏感的平台忽略大小写，并在所有平台忽略开头的 "./"。
    const QString stripped = wanted.startsWith(QLatin1String("./")) ? wanted.mid(2) : wanted;
    for (const EmittedFile &f : files) {
        QString candidate = QDir::fromNativeSeparators(f.relativePath);
        if (candidate.startsWith(QLatin1String("./")))
            candidate = candidate.mid(2);
        if (candidate.compare(stripped, Qt::CaseInsensitive) == 0)
            return &f;
    }
    return nullptr;
}

QString EmitResult::fileContents(const QString &relativePath) const
{
    const EmittedFile *f = file(relativePath);
    if (!f)
        return QString();
    return f->binary ? QString::fromUtf8(f->bytes) : f->contents;
}

qint64 EmitResult::totalBytes() const
{
    qint64 total = 0;
    for (const EmittedFile &f : files)
        total += f.size();
    return total;
}

// ---------------------------------------------------------------------------
//  Shared text helpers
//  共享文本辅助函数
// ---------------------------------------------------------------------------

//  Prefixes every non-empty line with `spaces` spaces. Blank lines stay blank so
//  the output does not accumulate trailing whitespace.
//  为每个非空行加上 `spaces` 个空格前缀。空行保持为空，避免产生行尾空白。
QString indentBlock(const QString &text, int spaces)
{
    if (text.isEmpty() || spaces <= 0)
        return text;

    const QString pad(spaces, QLatin1Char(' '));
    QStringList lines = text.split(QLatin1Char('\n'));

    //  split() adds a trailing empty entry for text ending in '\n'; keep it so the
    //  terminating newline is preserved.
    //  对以 '\n' 结尾的文本，split() 会追加一个空条目；保留它以维持结尾换行。
    for (QString &line : lines) {
        if (!line.trimmed().isEmpty())
            line.prepend(pad);
    }
    return lines.join(QLatin1Char('\n'));
}

//  Escapes text for HTML. `&`, `<`, `>` and the two quote characters are always
//  escaped; in attribute context the backtick and the equals sign are escaped as
//  well because legacy parsers accept them without quotes.
//  Existing entities are left alone, so `&amp;` never becomes `&amp;amp;`.
//  为 HTML 转义文本。`&`、`<`、`>` 与两种引号始终转义；在属性上下文中还会转义
//  反引号与等号，因为旧式解析器接受它们不加引号。已有实体会被保留，
//  因此 `&amp;` 不会变成 `&amp;amp;`。
QString escapeHtml(const QString &text, bool attributeContext)
{
    QString out;
    out.reserve(text.size() + 16);

    const int n = text.size();
    for (int i = 0; i < n; ++i) {
        const QChar c = text.at(i);
        const ushort u = c.unicode();

        switch (u) {
        case '&': {
            //  Preserve an already encoded entity: `&name;`, `&#123;`, `&#x1F;`.
            //  保留已编码的实体：`&name;`、`&#123;`、`&#x1F;`。
            static const QRegularExpression entity(
                QStringLiteral("^&(?:[a-zA-Z][a-zA-Z0-9]{1,31}|#[0-9]{1,7}|#[xX][0-9a-fA-F]{1,6});"));
            const QRegularExpressionMatch m = entity.match(text.mid(i));
            if (m.hasMatch()) {
                out += m.captured(0);
                i += m.captured(0).size() - 1;
                break;
            }
            out += QLatin1String("&amp;");
            break;
        }
        case '<':
            out += QLatin1String("&lt;");
            break;
        case '>':
            out += QLatin1String("&gt;");
            break;
        case '"':
            out += QLatin1String("&quot;");
            break;
        case '\'':
            out += QLatin1String("&#39;");
            break;
        case '`':
            if (attributeContext)
                out += QLatin1String("&#96;");
            else
                out += c;
            break;
        case '=':
            if (attributeContext)
                out += QLatin1String("&#61;");
            else
                out += c;
            break;
        default:
            out += c;
            break;
        }
    }
    return out;
}

//  Makes arbitrary text safe to embed inside a `<script>` or `<style>` element.
//  The HTML parser terminates those elements on the literal sequences below, so
//  they must never survive verbatim. U+2028/U+2029 are escaped too: they are
//  line terminators for JavaScript but not for HTML.
//  使任意文本可以安全嵌入 `<script>` 或 `<style>` 元素。HTML 解析器遇到下列
//  字面量序列即结束该元素，因此它们绝不能原样保留。U+2028/U+2029 也要转义：
//  它们对 JavaScript 是行终止符，对 HTML 却不是。
QString escapeJsForHtml(const QString &text)
{
    QString out;
    out.reserve(text.size() + 16);

    const int n = text.size();
    for (int i = 0; i < n; ++i) {
        const QChar c = text.at(i);

        if (c == QLatin1Char('<')) {
            const QString tail = text.mid(i);
            if (tail.startsWith(QLatin1String("</script"), Qt::CaseInsensitive)
                || tail.startsWith(QLatin1String("<!--"))) {
                //  Backslash escape keeps the JavaScript meaning identical while
                //  hiding the sequence from the HTML tokeniser.
                //  反斜杠转义在保持 JavaScript 语义不变的同时，
                //  让该序列对 HTML 分词器不可见。
                out += QLatin1String("<\\/");
                i += 1;                           // consumed "</" or "<!" / 已消费 "</" 或 "<!"
                continue;
            }
            out += c;
            continue;
        }

        if (c.unicode() == 0x2028) {
            out += QLatin1String("\\u2028");
            continue;
        }
        if (c.unicode() == 0x2029) {
            out += QLatin1String("\\u2029");
            continue;
        }
        out += c;
    }
    return out;
}

//  Produces a valid CSS identifier: letters, digits, `-`, `_` and non-ASCII
//  characters survive; everything else becomes `-`. A leading digit (or a leading
//  `-` followed by a digit) is escaped so the result is a legal ident.
//  生成合法的 CSS 标识符：字母、数字、`-`、`_` 与非 ASCII 字符保留，其余变为 `-`。
//  前导数字（或后接数字的前导 `-`）会被转义，使结果成为合法标识符。
QString escapeCssIdentifier(const QString &text)
{
    QString out;
    out.reserve(text.size() + 4);

    const QString trimmed = text.trimmed();
    for (const QChar c : trimmed) {
        const ushort u = c.unicode();
        const bool asciiAlpha = (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z');
        const bool digit = (u >= '0' && u <= '9');
        if (asciiAlpha || digit || u == '-' || u == '_' || u >= 0x80)
            out += c;
        else
            out += QLatin1Char('-');
    }

    if (out.isEmpty())
        out = QStringLiteral("tcbw");

    //  Reject reserved empty-ish idents so the caller always gets something usable.
    //  避免产生近乎空的标识符，保证调用方总能拿到可用结果。
    if (out == QLatin1String("-") || out == QLatin1String("--"))
        out = QStringLiteral("tcbw");

    auto isDigitChar = [](QChar c) {
        const ushort u = c.unicode();
        return u >= '0' && u <= '9';
    };

    if (isDigitChar(out.at(0)) || (out.startsWith(QLatin1Char('-')) && out.size() > 1
                                   && isDigitChar(out.at(1)))) {
        QString escaped;
        escaped.reserve(out.size() + 6);
        for (const QChar c : out) {
            if (isDigitChar(c))
                escaped += QStringLiteral("\\3%1 ").arg(static_cast<uint>(c.unicode()), 0, 16);
            else
                escaped += c;
        }
        out = escaped;
    }
    return out;
}

//  Lowercases, folds non-ASCII to ASCII where possible and replaces every run of
//  non-alphanumeric characters with a single `-`. The result is trimmed of
//  leading/trailing dashes and truncated to 48 characters.
//  转为小写，尽可能将非 ASCII 折叠为 ASCII，并把每一段非字母数字字符替换为单个
//  `-`。结果会去掉首尾短横线，并截断到 48 个字符。
QString slugify(const QString &text)
{
    //  Common Latin-1 letters that QString::normalized() cannot fold on its own.
    //  QString::normalized() 无法自行折叠的常见 Latin-1 字母。
    static const QHash<QChar, QString> folds = {
        {QChar(0x00DF), QStringLiteral("ss")}, {QChar(0x00E6), QStringLiteral("ae")},
        {QChar(0x00C6), QStringLiteral("AE")}, {QChar(0x00F8), QStringLiteral("o")},
        {QChar(0x00D8), QStringLiteral("O")},  {QChar(0x00F0), QStringLiteral("d")},
        {QChar(0x00FE), QStringLiteral("th")}, {QChar(0x0153), QStringLiteral("oe")},
        {QChar(0x0152), QStringLiteral("OE")}
    };

    QString folded;
    folded.reserve(text.size());
    for (const QChar c : text.normalized(QString::NormalizationForm_KD)) {
        const QChar lower = c.toLower();
        if (lower.unicode() < 0x80) {
            folded += lower;
            continue;
        }
        const auto it = folds.constFind(lower);
        if (it != folds.constEnd()) {
            folded += it.value();
            continue;
        }
        folded += QLatin1Char(' ');
    }

    QString out;
    out.reserve(folded.size());
    bool pendingDash = false;
    for (const QChar c : folded) {
        const ushort u = c.unicode();
        const bool alnum = (u >= 'a' && u <= 'z') || (u >= '0' && u <= '9');
        if (alnum) {
            if (pendingDash && !out.isEmpty())
                out += QLatin1Char('-');
            pendingDash = false;
            out += c;
        } else {
            pendingDash = true;
        }
    }

    //  `out` never ends with a dash because a dash is only added before a
    //  character, so no trailing trim is needed.
    //  短横线只在字符之前追加，因此 `out` 从不以短横线结尾，无需再做尾部裁剪。
    if (out.isEmpty())
        out = QStringLiteral("tcbw");

    if (out.size() > 48) {
        out.truncate(48);
        while (out.endsWith(QLatin1Char('-')))
            out.chop(1);
    }
    return out;
}

//  Decides whether a URL/URI value is safe to place in the generated document.
//  Leading control characters and whitespace are stripped first, and the check is
//  case-insensitive, so `java\tscript:alert(1)` and `JaVaScRiPt:` are both
//  rejected. Relative paths, fragments and `blob:` URLs are allowed.
//  判断 URL/URI 值是否可以安全放入生成的文档。首先去除开头的控制字符与空白，
//  并忽略大小写，因此 `java\tscript:alert(1)` 与 `JaVaScRiPt:` 都会被拒绝。
//  相对路径、片段与 `blob:` URL 允许通过。
bool isSafeUrl(const QString &url)
{
    //  Only the *leading* run is removed: browsers ignore leading controls and
    //  whitespace inside a URL, but an inner control character is meaningful.
    //  只移除开头的连续片段：浏览器会忽略 URL 开头的控制字符与空白，
    //  但中间的控制字符是有意义的。
    int begin = 0;
    while (begin < url.size()) {
        const QChar c = url.at(begin);
        const ushort u = c.unicode();
        if (u < 0x20 || u == 0x7F || c.isSpace()) {
            ++begin;
            continue;
        }
        break;
    }
    const QString s = url.mid(begin);

    if (s.isEmpty())
        return true;                          // empty attribute value / 空属性值

    const int colon = s.indexOf(QLatin1Char(':'));
    const int slash = s.indexOf(QLatin1Char('/'));
    const int hash = s.indexOf(QLatin1Char('#'));
    const int question = s.indexOf(QLatin1Char('?'));

    if (colon < 0)
        return true;                          // no scheme -> relative reference / 无协议，相对引用

    //  A colon that appears after a path separator is not a scheme separator.
    //  出现在路径分隔符之后的冒号不是协议分隔符。
    const bool schemeLike = (slash < 0 || colon < slash)
                            && (hash < 0 || colon < hash)
                            && (question < 0 || colon < question);
    if (!schemeLike)
        return true;

    const QString scheme = s.left(colon).toLower();
    const QString rest = s.mid(colon + 1);

    static const QStringList allowed = {
        QStringLiteral("http"), QStringLiteral("https"), QStringLiteral("mailto"),
        QStringLiteral("tel"), QStringLiteral("blob")
    };
    if (allowed.contains(scheme))
        return true;

    if (scheme == QLatin1String("data"))
        return isAllowedDataMediaType(rest);

    //  javascript:, vbscript:, file:, and every unknown scheme are refused.
    //  拒绝 javascript:、vbscript:、file: 以及所有未知协议。
    return false;
}

//  Attribute names that may be written to the document without any encoding.
//  Names beginning with `on` are event handlers and are refused, with the single
//  exception of this product's own `data-tcbw-*` hooks.
//  可以不经编码写入文档的属性名。以 `on` 开头的是事件处理器，一律拒绝，
//  仅本产品自有的 `data-tcbw-*` 钩子是例外。
bool isSafeAttributeName(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return false;

    static const QRegularExpression pattern(QStringLiteral("^[a-zA-Z][a-zA-Z0-9-]*$"));
    if (!pattern.match(trimmed).hasMatch())
        return false;

    const QString lower = trimmed.toLower();
    if (lower.startsWith(QLatin1String("data-tcbw-")))
        return true;
    if (lower.startsWith(QLatin1String("on")))
        return false;
    return true;
}

//  Tag names are restricted to the same conservative production.
//  标签名限定为同样保守的产生式。
bool isSafeTagName(const QString &tag)
{
    const QString trimmed = tag.trimmed();
    if (trimmed.isEmpty())
        return false;

    static const QRegularExpression pattern(QStringLiteral("^[a-zA-Z][a-zA-Z0-9-]*$"));
    return pattern.match(trimmed).hasMatch();
}

} // namespace tcbw
