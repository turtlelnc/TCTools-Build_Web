// ============================================================================
//  CodeGenerator.cpp -- the single entry point used by the UI and the AI tools
//  CodeGenerator.cpp —— 界面层与 AI 工具层使用的唯一入口
//  Part of TCTools-Build_Web (TC-BuildWeb)
// ============================================================================
#include "CodeGenerator.h"

#include "BlockCompiler.h"
#include "BlockDef.h"
#include "CssEmitter.h"
#include "DocumentNode.h"
#include "EmitOptions.h"
#include "HtmlEmitter.h"
#include "JsEmitter.h"
#include "Page.h"
#include "ProjectDocument.h"
#include "Types.h"

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QSaveFile>
#include <QSet>
#include <QString>
#include <QStringList>

namespace tcbw {

namespace {

constexpr qint64 kMaxGeneratedFileBytes = 64LL * 1024LL * 1024LL;

//  Bilingual refusal messages, so the same text can be reused everywhere.
//  双语拒绝消息，便于在各处复用相同文本。
QString refusal(const QString &english, const QString &chinese)
{
    return QStringLiteral("Refused: ") + english + QStringLiteral(" / 已拒绝：") + chinese;
}

//  `true` when a path component list contains `..`, which would let a caller
//  climb out of the export directory.
//  当路径分段中包含 `..` 时返回 `true`，因为它会让调用方爬出导出目录。
bool hasParentTraversal(const QString &relativePath)
{
    const QString normalised = QDir::fromNativeSeparators(relativePath);
    const QStringList parts = normalised.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        if (part == QLatin1String(".."))
            return true;
    }
    return false;
}

//  `true` for absolute paths, drive letters and UNC prefixes on every platform,
//  so a POSIX-hosted export cannot be tricked by a Windows-shaped path either.
//  在任意平台上识别绝对路径、盘符与 UNC 前缀，使运行于 POSIX 的导出也无法被
//  Windows 形式的路径欺骗。
bool looksAbsolute(const QString &path)
{
    if (path.startsWith(QLatin1Char('/')) || path.startsWith(QLatin1Char('\\')))
        return true;
    //  Drive letter, e.g. `C:` or `c:/`.
    //  盘符，例如 `C:` 或 `c:/`。
    if (path.size() >= 2 && path.at(1) == QLatin1Char(':')
        && path.at(0).isLetter())
        return true;
    return false;
}

//  `true` for a file name that is only whitespace or contains characters that
//  confuse the platform's file system APIs.
//  当文件名仅含空白或包含会干扰平台文件系统 API 的字符时返回 `true`。
bool hasForbiddenNameCharacters(const QString &name)
{
    for (const QChar c : name) {
        const ushort u = c.unicode();
        if (u == 0x0000 || u < 0x20)
            return true;
        if (u == '<' || u == '>' || u == ':' || u == '"' || u == '|' || u == '?'
            || u == '*')
            return true;
    }
    return false;
}

//  Builds a filesystem-safe stem from a page name.
//  由页面名构建文件系统安全的文件名主干。
QString safeStem(const Page &page)
{
    QString stem = slugify(page.name.trimmed().isEmpty() ? page.title : page.name);
    if (stem.isEmpty())
        stem = QStringLiteral("page");
    return stem;
}

//  `true` when the page should become `index.html` for its extension.
//  当页面应对应 `index.html` 时返回 `true`。
bool isHomeLike(const Page &page)
{
    if (page.isHome)
        return true;
    const QString name = page.name.trimmed().toLower();
    if (name == QLatin1String("index") || name == QLatin1String("home")
        || name == QLatin1String("首页"))
        return true;
    return false;
}

//  A file name for `page` with the requested extension, honouring `page.fileName`
//  when it already carries that extension.
//  为页面生成指定扩展名的文件名；当 `page.fileName` 已带该扩展名时直接沿用。
QString pageFileBase(const Page &page, const QString &extension)
{
    const QString fileName = page.fileName.trimmed();
    if (!fileName.isEmpty()) {
        const QString suffix = QLatin1Char('.') + extension;
        if (fileName.endsWith(suffix, Qt::CaseInsensitive))
            return fileName;
        const QString base = fileName.section(QLatin1Char('.'), 0, 0).trimmed();
        if (!base.isEmpty() && !hasForbiddenNameCharacters(base))
            return base + suffix;
    }
    return safeStem(page) + QLatin1Char('.') + extension;
}

//  A page-derived module name, kept a legal JavaScript identifier.
//  由页面推导的模块名，保证是合法的 JavaScript 标识符。
QString moduleNameFor(const Page &page, const QString &fallback)
{
    const QString source = page.name.trimmed().isEmpty() ? page.title : page.name;
    const QString identifier = BlockCompiler::sanitiseIdentifier(source, fallback);
    return identifier.trimmed().isEmpty() ? fallback : identifier;
}

//  Renames the marker comments written by the HTML emitter so every multi-page
//  output carries a useful `data-page` hook (the shared stylesheet is scoped on
//  exactly that attribute).
//  将 HTML 生成器写出的标记注释改写为有用的 `data-page` 钩子
//  （共享样式表正是基于该属性做作用域限定）。
QString withPageAttribute(const QString &html, const Page &page, const EmitOptions &options)
{
    const int width = (options.indentWidth > 0 && options.indentWidth <= 8) ? options.indentWidth
                                                                           : 2;
    const QString pad(width, QLatin1Char(' '));

    QString out;
    out.reserve(html.size() + 64);

    int cursor = 0;
    while (true) {
        const int marker = html.indexOf(QStringLiteral("tcbw:page-data"), cursor);
        if (marker < 0) {
            out += html.mid(cursor);
            break;
        }
        //  Copy everything up to the start of the marker comment.
        //  复制到标记注释开始处之前的所有内容。
        const int commentStart = html.lastIndexOf(QStringLiteral("<!--"), marker);
        if (commentStart < 0 || commentStart < cursor)
            break;
        out += html.mid(cursor, commentStart - cursor);

        const int commentEnd = html.indexOf(QStringLiteral("-->"), marker);
        const int resume = (commentEnd < 0) ? html.size() : commentEnd + 3;
        const int newline = html.indexOf(QLatin1Char('\n'), resume);
        const int afterLine = (newline < 0) ? html.size() : newline + 1;

        out += pad + QStringLiteral("<script id=\"tcbw-page-data\" type=\"application/json\">")
               + html.mid(resume, afterLine - resume);
        out += QStringLiteral("</script>") + QLatin1Char('\n');
        cursor = afterLine;    }

    //  Inject the page hook into the <body> tag.
    //  将页面钩子注入 <body> 标签。
    const int bodyStart = out.indexOf(QStringLiteral("<body"));
    if (bodyStart >= 0) {
        const int bodyEnd = out.indexOf(QLatin1Char('>'), bodyStart);
        if (bodyEnd > bodyStart) {
            const QString bodyTag = out.mid(bodyStart, bodyEnd - bodyStart);
            if (!bodyTag.contains(QLatin1String("data-page"))) {
                out.insert(bodyEnd, QStringLiteral(" data-page=\"")
                                        + escapeHtml(page.id, true) + QLatin1Char('"'));
            }
        }
    }
    return out;
}

//  Creates the directories leading to `directory` one component at a time and
//  refuses to follow a symbolic link while doing so.
//  逐级创建通往 `directory` 的目录，并在此过程中拒绝跟随符号链接。
bool makeDirectoriesSafely(const QString &path, QStringList *errors, const QString &rootDisplay)
{
    const QString cleaned = QDir::cleanPath(path);
    if (cleaned.isEmpty())
        return true;

    QStringList parts = QDir::fromNativeSeparators(cleaned).split(QLatin1Char('/'));
    QString current = parts.isEmpty() ? cleaned : parts.takeFirst();
    if (current.isEmpty()) {
        //  POSIX absolute path: the first component is the root.
        //  POSIX 绝对路径：第一个分段就是根。
        current = QStringLiteral("/");
    }

    for (const QString &part : parts) {
        if (part.isEmpty() || part == QLatin1String("."))
            continue;
        current = current.endsWith(QLatin1Char('/')) ? current + part
                                                     : current + QLatin1Char('/') + part;
        QFileInfo info(current);
        if (info.exists()) {
            if (info.isSymLink()) {
                if (errors) {
                    errors->append(refusal(
                        QStringLiteral("a path component is a symbolic link: %1 (under %2)")
                            .arg(part, rootDisplay),
                        QStringLiteral("路径分段是指向别处的符号链接：%1（位于 %2 下）")
                            .arg(part, rootDisplay)));
                }
                return false;
            }
            if (!info.isDir()) {
                if (errors) {
                    errors->append(refusal(
                        QStringLiteral("%1 exists and is not a directory").arg(current),
                        QStringLiteral("%1 已存在且不是目录").arg(current)));
                }
                return false;
            }
            continue;
        }
        if (!QDir().mkpath(current)) {
            if (errors) {
                errors->append(refusal(QStringLiteral("cannot create directory %1").arg(current),
                                       QStringLiteral("无法创建目录 %1").arg(current)));
            }
            return false;
        }
    }
    return true;
}

//  Detects the browser APIs the generated JavaScript depends on.
//  检测生成的 JavaScript 所依赖的浏览器接口。
QStringList detectBrowserApis(const QString &code)
{
    static const QVector<QPair<QString, QString>> table = {
        {QStringLiteral("fetch("), QStringLiteral("fetch")},
        {QStringLiteral("AbortController"), QStringLiteral("AbortController")},
        {QStringLiteral("localStorage"), QStringLiteral("localStorage")},
        {QStringLiteral("sessionStorage"), QStringLiteral("sessionStorage")},
        {QStringLiteral("document.cookie"), QStringLiteral("Cookie")},
        {QStringLiteral("requestAnimationFrame"), QStringLiteral("requestAnimationFrame")},
        {QStringLiteral("IntersectionObserver"), QStringLiteral("IntersectionObserver")},
        {QStringLiteral("navigator.clipboard"), QStringLiteral("Clipboard")},
        {QStringLiteral("URLSearchParams"), QStringLiteral("URLSearchParams")},
        {QStringLiteral("document.querySelector"), QStringLiteral("DOM")},
        {QStringLiteral("classList"), QStringLiteral("classList")},
        {QStringLiteral("JSON.parse"), QStringLiteral("JSON")},
        {QStringLiteral("setTimeout"), QStringLiteral("setTimeout")},
        {QStringLiteral("Promise"), QStringLiteral("Promise")},
        {QStringLiteral("history.pushState"), QStringLiteral("History")},
        {QStringLiteral("window.matchMedia"), QStringLiteral("matchMedia")}
    };

    QStringList found;
    QSet<QString> seen;
    for (const auto &entry : table) {
        if (!code.contains(entry.first))
            continue;
        if (seen.contains(entry.second))
            continue;
        seen.insert(entry.second);
        found.append(entry.second);
    }
    found.sort();
    return found;
}

//  `1.2 KB` style human readable byte count.
//  `1.2 KB` 风格的人类可读字节数。
QString humanBytes(qint64 bytes)
{
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    const double kb = static_cast<double>(bytes) / 1024.0;
    if (kb < 1024.0)
        return QStringLiteral("%1 KB").arg(kb, 0, 'f', 1);
    return QStringLiteral("%1 MB").arg(kb / 1024.0, 0, 'f', 1);
}

//  Scopes every selector of a single page's stylesheet to that page, so several
//  pages can share one file without leaking rules into each other.
//  将单页样式表的所有选择器限定到该页，使多个页面可以共用一个文件而不互相泄漏规则。
QString scopeCssToPage(const QString &css, const QString &pageId)
{
    if (css.isEmpty())
        return css;

    const QString prefix = QStringLiteral("[data-page=\"") + pageId + QStringLiteral("\"] ");

    QString out;
    out.reserve(css.size() + 64);

    //  Depth of the innermost at-rule block. Selectors inside `@media` are
    //  prefixed; at-rule preludes are not.
    //  最内层 at-rule 块的深度。`@media` 内部的选择器需要加前缀，at-rule 的前奏不加。
    int atDepth = 0;
    QString selector;
    bool haveSelector = false;

    for (const QChar c : css) {
        if (c == QLatin1Char('{')) {
            const QString head = selector.trimmed();
            const bool isAtRule = head.startsWith(QLatin1Char('@'));
            if (!isAtRule) {
                if (haveSelector && !head.isEmpty() && !head.startsWith(QLatin1String("[data-page")))
                    out += prefix + head;
                else
                    out += head;
            } else {
                out += head;
                ++atDepth;
            }
            out += QLatin1Char('{');
            selector.clear();
            haveSelector = false;
            continue;
        }
        if (c == QLatin1Char('}')) {
            if (haveSelector && !selector.trimmed().isEmpty())
                out += selector;
            if (atDepth > 0)
                --atDepth;
            out += QLatin1Char('}');
            selector.clear();
            haveSelector = false;
            continue;
        }
        if (c == QLatin1Char(';') && !haveSelector) {
            out += c;                       // declaration or at-rule statement / 声明或 at-rule 语句
            continue;
        }
        selector += c;
        haveSelector = true;
    }
    if (!selector.isEmpty())
        out += selector;

    return out;
}

} // namespace

// ---------------------------------------------------------------------------
//  Construction
//  构造
// ---------------------------------------------------------------------------
CodeGenerator::CodeGenerator(const EmitOptions &options)
    : m_options(options)
{
}

void CodeGenerator::setOptions(const EmitOptions &options)
{
    m_options = options;
}

// ---------------------------------------------------------------------------
//  Builders
//  构建器
// ---------------------------------------------------------------------------
QString CodeGenerator::buildDocument(const Page &page, const QVector<BlockScript *> &program,
                                     const ProjectDocument *project) const
{
    Q_UNUSED(project)

    //  The effective options for this page: the mode decides whether the CSS and
    //  the JS are inlined or referenced, and the per-page file names are stamped
    //  in so the HTML emitter can link the right script.
    //  该页面的有效选项：模式决定 CSS 与 JS 是内联还是引用，并写入本页的文件名，
    //  使 HTML 生成器能链接正确的脚本。
    EmitOptions options = m_options;
    options.inlineCss = (options.mode == ExportMode::SingleHtml);
    options.inlineJs = (options.mode == ExportMode::SingleHtml);

    const QString scriptFileName = pageFileBase(page, QStringLiteral("js"));
    const QString styleFileName = pageFileBase(page, QStringLiteral("css"));
    if (options.mode == ExportMode::ScriptOnly)
        options.jsFileName = scriptFileName;
    if (options.mode == ExportMode::HtmlWithAssets || options.mode == ExportMode::HeadlessBundle)
        options.cssFileName = styleFileName;

    options.compile = m_options.compileOptions();
    options.compile.moduleName = moduleNameFor(page, QStringLiteral("TCBuildWebPage"));
    options.compile.targetElementSelector = QStringLiteral("[data-tcbw-root]");

    HtmlEmitter html(options);
    QString out = (options.mode == ExportMode::ScriptOnly) ? html.emitScriptHost(page)
                                                           : html.emitDocument(page);

    //  The pure HTML emitter cannot render the program, so it leaves a marker and
    //  the script is spliced in here. In the asset modes the document links
    //  `<page>.js` instead, so the marker simply disappears.
    //  纯函数式 HTML 生成器无法渲染程序，因此它留下一个标记，脚本在此处拼接进去。
    //  在资源模式下文档改为链接 `<page>.js`，标记直接消失。
    const int marker = out.indexOf(QStringLiteral("<!-- tcbw:page-script -->"));
    if (marker >= 0) {
        const int lineEnd = out.indexOf(QLatin1Char('\n'), marker);
        const int eraseTo = (lineEnd < 0) ? out.size() : lineEnd + 1;
        out.remove(marker, eraseTo - marker);

        if (options.inlineJs) {
            const QString script = buildScript(page, program, project);
            const int bodyClose = out.lastIndexOf(QStringLiteral("</body>"));
            const QString scriptTag = QStringLiteral("  <script>") + escapeJsForHtml(script)
                                      + QStringLiteral("</script>\n");
            if (bodyClose >= 0)
                out.insert(bodyClose, scriptTag);
            else
                out += scriptTag;
        }
    }

    return out;
}

QString CodeGenerator::buildStylesheet(const Page &page, const ProjectDocument *project) const
{
    Q_UNUSED(project)

    EmitOptions options = m_options;
    options.inlineCss = true;               // the caller decides where it lands / 落地位置由调用方决定
    options.minifyCss = false;              // minified once, after concatenation / 拼接后统一压缩

    CssEmitter css(options);
    return css.emitPage(page);
}

QString CodeGenerator::buildScript(const Page &page, const QVector<BlockScript *> &program,
                                   const ProjectDocument *project) const
{
    Q_UNUSED(project)

    EmitOptions options = m_options;
    options.inlineJs = true;
    options.minifyJs = false;

    options.compile = m_options.compileOptions();
    options.compile.moduleName = moduleNameFor(page, QStringLiteral("TCBuildWebPage"));
    options.compile.targetElementSelector = QStringLiteral("[data-tcbw-root]");

    JsEmitter js(options);
    //  ScriptOnly mode inlines the page tree and the DOM builder; the other
    //  modes ship static markup and only need the runtime plus interactions.
    //  仅脚本模式内联页面树与 DOM 构建器；其他模式输出静态标记，只需要运行时与交互。
    QString body = js.emitPage(page, program);

    if (options.mode == ExportMode::SingleHtml)
        return body;                        // inlined into the document / 内联进文档

    //  One IIFE per page keeps the page modules from colliding.
    //  每个页面一个 IIFE，避免各页面模块互相冲突。
    return JsEmitter::wrapIife(body, options.compile.moduleName, options.compile.useStrict, false);
}

// ---------------------------------------------------------------------------
//  Page selection helpers
//  页面选择辅助函数
// ---------------------------------------------------------------------------
namespace {

//  Resolves the page a convenience wrapper should report on: the explicit id
//  first, then the active page, then the home page.
//  解析便捷封装应报告的页面：先显式 id，再当前页面，最后首页。
const Page *resolvePage(const ProjectDocument &project, const QString &pageId)
{
    if (!pageId.trimmed().isEmpty()) {
        if (const Page *page = project.pageById(pageId.trimmed()))
            return page;
    }
    if (!project.activePageId.trimmed().isEmpty()) {
        if (const Page *page = project.pageById(project.activePageId.trimmed()))
            return page;
    }
    return project.homePage();
}

} // namespace

QString CodeGenerator::suggestedFileName(const Page &page) const
{
    switch (m_options.mode) {
    case ExportMode::SingleHtml:
    case ExportMode::HtmlWithAssets:
    case ExportMode::HeadlessBundle:
        return pageFileBase(page, QStringLiteral("html"));
    case ExportMode::ScriptOnly:
        return pageFileBase(page, QStringLiteral("js"));
    }
    return pageFileBase(page, QStringLiteral("html"));
}

// ---------------------------------------------------------------------------
//  Single page generation
//  单页生成
// ---------------------------------------------------------------------------
EmitResult CodeGenerator::generatePage(const Page &page, const QVector<BlockScript *> &program,
                                       const ProjectDocument *project) const
{
    EmitResult result;

    const QString html = buildDocument(page, program, project);
    const QString css = buildStylesheet(page, project);
    const QString js = buildScript(page, program, project);

    //  Every mode gets the same logical payload; `generatePage` reports the raw
    //  fragments and `generateProject` decides how they are packaged.
    //  所有模式都得到相同的逻辑内容；`generatePage` 报告原始片段，
    //  `generateProject` 决定如何打包。
    EmittedFile documentFile;
    documentFile.relativePath = suggestedFileName(page);
    documentFile.contents = html;
    documentFile.mimeType = QStringLiteral("text/html; charset=utf-8");
    result.files.append(documentFile);

    EmittedFile styleFile;
    styleFile.relativePath = pageFileBase(page, QStringLiteral("css"));
    styleFile.contents = css;
    styleFile.mimeType = QStringLiteral("text/css; charset=utf-8");
    result.files.append(styleFile);

    EmittedFile scriptFile;
    scriptFile.relativePath = pageFileBase(page, QStringLiteral("js"));
    scriptFile.contents = js;
    scriptFile.mimeType = QStringLiteral("text/javascript; charset=utf-8");
    result.files.append(scriptFile);

    //  Compiler diagnostics are duplicated here so the UI can show them without
    //  parsing the generated comment block.
    //  编译器诊断在此重复一份，使界面无需解析生成的注释块即可展示。
    if (!program.isEmpty()) {
        BlockCompiler compiler(m_options.compileOptions());
        const CompileResult compiled = compiler.compileProgram(program);
        for (const QString &error : compiled.errors)
            result.errors.append(error);
        for (const QString &warning : compiled.warnings)
            result.warnings.append(warning);
    }

    //  Tree-level validation: the export should tell the user about a page that
    //  cannot be represented faithfully.
    //  树级校验：当页面无法被忠实表示时，导出应当告知用户。
    if (!page.root) {
        result.errors.append(QStringLiteral("Page '%1' has no root node / 页面 '%1' 没有根节点")
                                 .arg(page.name));
    } else {
        QStringList problems;
        if (!page.root->validate(&problems)) {
            for (const QString &problem : problems)
                result.warnings.append(problem);
        }
    }

    result.requiredBrowserApis = detectBrowserApis(js);
    result.ok = result.errors.isEmpty();
    return result;
}

// ---------------------------------------------------------------------------
//  Whole project generation
//  整个工程生成
// ---------------------------------------------------------------------------
EmitResult CodeGenerator::generateProject(const ProjectDocument &project) const
{
    EmitResult result;

    const int pageCount = project.pageCount();
    if (pageCount == 0) {
        result.errors.append(QStringLiteral("The project has no pages / 工程中没有任何页面"));
        result.ok = false;
        return result;
    }

    //  -- shared helpers ------------------------------------------------------
    //  —— 共享辅助函数 ——
    auto addFile = [&result](const QString &path, const QString &contents, const QString &mime) {
        EmittedFile file;
        file.relativePath = path;
        file.contents = contents;
        file.mimeType = mime;
        result.files.append(file);
    };

    auto pageProgram = [&project](const Page &page) -> QVector<BlockScript *> {
        QVector<BlockScript *> scoped;
        const QVector<BlockScript *> &all = project.program();
        for (BlockScript *script : all) {
            if (!script)
                continue;
            if (script->pageTarget.trimmed().isEmpty()
                || script->pageTarget == page.id)
                scoped.append(script);
        }
        return scoped;
    };

    //  -- per-page assets -----------------------------------------------------
    //  —— 逐页资源 ——
    //  Every fragment is computed exactly once so the different export modes can
    //  simply pick the ones they need.
    //  每个片段只计算一次，各导出模式只需挑选所需的部分。
    struct PageAssets
    {
        Page *page = nullptr;
        QString id;
        QVector<BlockScript *> scopedProgram;
        QString css;                // unscoped, unminified page stylesheet
        QString scopedCss;          // scoped to `[data-page="<id>"]`
        QString script;             // wrapped (or inline) page script
        QString documentHtml;       // self-contained document / 自包含文档
        QString assetsHtml;         // document referencing external files / 引用外部文件
        QString hostHtml;           // ScriptOnly host page / 仅脚本模式的宿主页
    };
    QVector<PageAssets> assets;
    assets.reserve(pageCount);

    EmitOptions cssOptions = m_options;
    cssOptions.inlineCss = true;
    cssOptions.minifyCss = false;

    EmitOptions jsOptions = m_options;
    jsOptions.minifyJs = false;

    //  Every per-page document gets its own generated file names.
    //  每个页面的文档都会得到自己生成的文件名。
    for (int index = 0; index < pageCount; ++index) {
        Page *page = project.pageAt(index);
        if (!page)
            continue;

        PageAssets entry;
        entry.page = page;
        entry.id = page->id;
        entry.scopedProgram = pageProgram(*page);
        entry.css = buildStylesheet(*page, &project);
        entry.scopedCss = scopeCssToPage(entry.css, page->id);
        entry.script = buildScript(*page, entry.scopedProgram, &project);

        //  Mode-specific documents, all built from the same page tree.
        //  各模式专属文档，全部由同一棵页面树构建。
        {
            EmitOptions options = m_options;
            options.inlineCss = true;
            options.inlineJs = false;
            options.compile = m_options.compileOptions();
            options.compile.moduleName = moduleNameFor(*page, QStringLiteral("TCBuildWebPage"));
            options.compile.targetElementSelector = QStringLiteral("[data-tcbw-root]");
            HtmlEmitter emitter(options);
            entry.documentHtml = withPageAttribute(emitter.emitDocument(*page), *page, options);
        }
        {
            EmitOptions options = m_options;
            options.inlineCss = false;
            options.inlineJs = false;
            options.cssFileName = pageFileBase(*page, QStringLiteral("css"));
            options.jsFileName = pageFileBase(*page, QStringLiteral("js"));
            HtmlEmitter emitter(options);
            entry.assetsHtml = withPageAttribute(emitter.emitDocumentWithAssets(*page), *page,
                                                 options);
        }
        {
            EmitOptions options = m_options;
            options.inlineCss = true;
            options.inlineJs = false;
            options.jsFileName = pageFileBase(*page, QStringLiteral("js"));
            HtmlEmitter emitter(options);
            entry.hostHtml = withPageAttribute(emitter.emitScriptHost(*page), *page, options);
        }

        //  Block-compiler diagnostics are surfaced through the result so the UI
        //  can show them without parsing the generated comment block.
        //  积木编译器的诊断通过结果上报，使界面无需解析生成的注释块即可展示。
        if (!entry.scopedProgram.isEmpty()) {
            BlockCompiler compiler(jsOptions.compileOptions());
            const CompileResult compiled = compiler.compileProgram(entry.scopedProgram);
            for (const QString &error : compiled.errors)
                result.errors.append(QStringLiteral("[%1] %2").arg(page->name, error));
            for (const QString &warning : compiled.warnings)
                result.warnings.append(QStringLiteral("[%1] %2").arg(page->name, warning));
        }
        result.requiredBrowserApis += detectBrowserApis(entry.script);

        assets.append(entry);
    }

    result.requiredBrowserApis.sort();
    result.requiredBrowserApis.removeDuplicates();

    //  Shared stylesheet: the reset, the shared tokens and the accessibility
    //  helpers are emitted once, then every page's rules follow, each one scoped
    //  by its `[data-page="<id>"]` attribute.
    //  共享样式表：reset、共享令牌与无障碍辅助类只输出一次，随后是各页面的规则，
    //  每条都以 `[data-page="<id>"]` 属性做作用域限定。
    QString sharedCss = QStringLiteral("/* TCBW shared stylesheet / TCBW 共享样式表 */\n");
    {
        CssEmitter tokens(cssOptions);
        if (cssOptions.includeResetCss)
            sharedCss += tokens.emitReset();
        //  `emitTokens` also carries the page's own custom properties; the shared
        //  file merges every page because custom properties are inherited, not
        //  scoped, and a later page may legitimately reuse a name.
        //  `emitTokens` 也会带上页面自定义属性；共享文件合并所有页面，因为自定义
        //  属性是继承而非作用域限定的，后面的页面可能合法地复用同名令牌。
        for (const PageAssets &entry : assets)
            sharedCss += tokens.emitTokens(*entry.page);
        if (cssOptions.includeAccessibilityHelpers)
            sharedCss += tokens.emitAccessibilityHelpers();
    }
    for (const PageAssets &entry : assets)
        sharedCss += entry.scopedCss;
    if (cssOptions.minifyCss)
        sharedCss = CssEmitter::minify(sharedCss);

    //  -- packaging per export mode -------------------------------------------
    //  —— 按导出模式打包 ——
    switch (m_options.mode) {
    case ExportMode::SingleHtml: {
        //  One self-contained `.html` per page, CSS and JS inlined; the home page
        //  is always reachable as `index.html`.
        //  每页一个自包含 `.html`，CSS 与 JS 内联；首页始终可以通过
        //  `index.html` 访问。
        QSet<QString> used;
        for (int index = 0; index < assets.size(); ++index) {
            const PageAssets &entry = assets.at(index);
            QString path = pageFileBase(*entry.page, QStringLiteral("html"));
            if (isHomeLike(*entry.page))
                path = QStringLiteral("index.html");
            if (used.contains(path))
                path = safeStem(*entry.page) + QStringLiteral("-") + QString::number(index)
                       + QStringLiteral(".html");
            used.insert(path);

            //  `buildDocument` already spliced the executable script and turned
            //  the data-island marker into a real JSON script element.
            //  `buildDocument` 已经拼接了可执行脚本，并把数据岛标记转换为真正的
            //  JSON 脚本元素。
            addFile(path, entry.documentHtml, QStringLiteral("text/html; charset=utf-8"));
        }
        break;
    }

    case ExportMode::HtmlWithAssets:
    case ExportMode::HeadlessBundle: {
        //  Decision (documented): every page gets its own `<page>.html` and its
        //  own `<page>.css`, and the bundle additionally carries a shared
        //  `styles.css` (reset + tokens + every page scoped) plus a shared
        //  `app.js`. Pages link their own stylesheet; the shared files exist for
        //  tooling and for pages that prefer a single request.
        //  决策（已记录）：每个页面各有 `<page>.html` 与 `<page>.css`，
        //  打包结果还额外带有共享的 `styles.css`（reset + 令牌 + 全部页面的
        //  作用域规则）以及共享的 `app.js`。页面链接各自的样式表；共享文件
        //  用于工具链以及希望只发一次请求的页面。
        QSet<QString> used;
        for (int index = 0; index < assets.size(); ++index) {
            const PageAssets &entry = assets.at(index);

            QString htmlPath = pageFileBase(*entry.page, QStringLiteral("html"));
            if (isHomeLike(*entry.page))
                htmlPath = QStringLiteral("index.html");
            if (used.contains(htmlPath))
                htmlPath = safeStem(*entry.page) + QStringLiteral("-") + QString::number(index)
                           + QStringLiteral(".html");
            used.insert(htmlPath);

            addFile(htmlPath, entry.assetsHtml, QStringLiteral("text/html; charset=utf-8"));
            addFile(pageFileBase(*entry.page, QStringLiteral("css")), entry.css,
                    QStringLiteral("text/css; charset=utf-8"));
        }

        addFile(m_options.cssFileName, sharedCss, QStringLiteral("text/css; charset=utf-8"));

        //  Shared script: one runtime, then every page module.
        //  共享脚本：一份运行时，随后是各页面模块。
        JsEmitter runtimeEmitter(jsOptions);
        QString appJs = QStringLiteral("/* TCBW shared runtime and page modules. */\n");
        appJs += QStringLiteral("/* TCBW 共享运行时与页面模块。 */\n");
        appJs += runtimeEmitter.emitRuntime();
        appJs += QLatin1Char('\n');
        for (const PageAssets &entry : assets) {
            appJs += entry.script;
            if (!entry.script.endsWith(QLatin1Char('\n')))
                appJs += QLatin1Char('\n');
        }
        if (m_options.minifyJs)
            appJs = JsEmitter::minify(appJs);
        addFile(m_options.jsFileName, appJs, QStringLiteral("text/javascript; charset=utf-8"));

        if (m_options.mode == ExportMode::HeadlessBundle) {
            //  -- bundle extras -------------------------------------------------
            //  —— 打包附加文件 ——
            const QString assetsDir = m_options.assetsDir.trimmed().isEmpty()
                                          ? QStringLiteral("assets")
                                          : m_options.assetsDir.trimmed();
            addFile(assetsDir + QStringLiteral("/.gitkeep"), QString(),
                    QStringLiteral("text/plain"));

            QJsonObject manifest;
            manifest.insert(QStringLiteral("name"),
                            project.name.trimmed().isEmpty() ? QStringLiteral("Untitled Project")
                                                             : project.name.trimmed());
            manifest.insert(QStringLiteral("version"),
                            project.version.trimmed().isEmpty() ? versionString()
                                                                : project.version.trimmed());
            manifest.insert(QStringLiteral("description"), project.description);
            manifest.insert(QStringLiteral("generatedAt"),
                            QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
            manifest.insert(QStringLiteral("generator"),
                            QStringLiteral("TCTools-Build_Web ") + versionString());

            QJsonArray pages;
            for (int index = 0; index < pageCount; ++index) {
                Page *page = project.pageAt(index);
                if (!page)
                    continue;
                QJsonObject entry;
                entry.insert(QStringLiteral("id"), page->id);
                entry.insert(QStringLiteral("name"), page->name);
                entry.insert(QStringLiteral("title"), page->title);
                entry.insert(QStringLiteral("file"), pageFileBase(*page, QStringLiteral("html")));
                entry.insert(QStringLiteral("stylesheet"),
                             pageFileBase(*page, QStringLiteral("css")));
                entry.insert(QStringLiteral("home"), isHomeLike(*page));
                pages.append(entry);
            }
            manifest.insert(QStringLiteral("pages"), pages);
            manifest.insert(QStringLiteral("sharedStylesheet"), m_options.cssFileName);
            manifest.insert(QStringLiteral("sharedScript"), m_options.jsFileName);

            addFile(QStringLiteral("manifest.json"),
                    QString::fromUtf8(QJsonDocument(manifest).toJson(QJsonDocument::Indented)),
                    QStringLiteral("application/json; charset=utf-8"));

            QString readme;
            readme += QStringLiteral("TCTools-Build_Web export\n");
            readme += QStringLiteral("TCTools-Build_Web 导出结果\n");
            readme += QStringLiteral("=========================\n\n");
            readme += QStringLiteral("How to open / 如何打开\n");
            readme += QStringLiteral("----------------------\n");
            readme += QStringLiteral(
                "1. Open `index.html` in any modern browser (Chrome, Edge, Firefox, "
                "Safari).\n");
            readme += QStringLiteral(
                "   用任意现代浏览器（Chrome、Edge、Firefox、Safari）打开 "
                "`index.html`。\n");
            readme += QStringLiteral(
                "2. The site is fully static: no server, no build step, no network "
                "access is required.\n");
            readme += QStringLiteral(
                "   站点完全静态：不需要服务器、不需要构建步骤、不需要联网。\n");
            readme += QStringLiteral(
                "3. If the browser blocks local scripts, serve the folder instead, "
                "for example:\n");
            readme += QStringLiteral(
                "   若浏览器阻止本地脚本，请改为以本地服务器方式打开，例如：\n");
            readme += QStringLiteral(
                "       python -m http.server 8000\n");
            readme += QStringLiteral(
                "   then browse to http://localhost:8000/ .\n");
            readme += QStringLiteral(
                "   然后访问 http://localhost:8000/ 。\n\n");
            readme += QStringLiteral("Files / 文件\n");
            readme += QStringLiteral("------------\n");
            readme += QStringLiteral("- `index.html` -- home page / 首页\n");
            readme += QStringLiteral("- `%1` -- shared stylesheet / 共享样式表\n")
                          .arg(m_options.cssFileName);
            readme += QStringLiteral("- `%1` -- shared runtime and page modules / "
                                     "共享运行时与页面模块\n").arg(m_options.jsFileName);
            readme += QStringLiteral("- `%1/` -- place images and other assets here / "
                                     "请将图片与其他资源放在此处\n").arg(assetsDir);
            readme += QStringLiteral("- `manifest.json` -- machine readable index / "
                                     "机器可读的索引\n");
            readme += QStringLiteral("\nGenerated by TCTools-Build_Web %1 on %2\n")
                          .arg(versionString(),
                               QDateTime::currentDateTime().toString(Qt::ISODate));
            readme += QStringLiteral("由 TCTools-Build_Web %1 于 %2 生成\n")
                          .arg(versionString(),
                               QDateTime::currentDateTime().toString(Qt::ISODate));
            addFile(QStringLiteral("README.txt"), readme,
                    QStringLiteral("text/plain; charset=utf-8"));

            addFile(QStringLiteral("robots.txt"),
                    QStringLiteral("# TCTools-Build_Web export / TCTools-Build_Web 导出\n"
                                   "User-agent: *\nAllow: /\n"),
                    QStringLiteral("text/plain; charset=utf-8"));
        }
        break;
    }

    case ExportMode::ScriptOnly: {
        //  `<page>.js` plus a minimal `<page>.html` host; the script builds the
        //  DOM itself.
        //  `<page>.js` 加上极简的 `<page>.html` 宿主；由脚本自行构建 DOM。
        QSet<QString> usedHosts;
        for (int index = 0; index < assets.size(); ++index) {
            const PageAssets &entry = assets.at(index);

            addFile(pageFileBase(*entry.page, QStringLiteral("js")), entry.script,
                    QStringLiteral("text/javascript; charset=utf-8"));

            QString hostPath = pageFileBase(*entry.page, QStringLiteral("html"));
            if (isHomeLike(*entry.page))
                hostPath = QStringLiteral("index.html");
            if (usedHosts.contains(hostPath))
                hostPath = safeStem(*entry.page) + QStringLiteral("-") + QString::number(index)
                           + QStringLiteral(".html");
            usedHosts.insert(hostPath);

            //  The host only references the script; the DOM is built at runtime by
            //  the exported bundle.
            //  宿主只引用脚本；DOM 由导出的代码在运行时构建。
            addFile(hostPath, entry.hostHtml, QStringLiteral("text/html; charset=utf-8"));
        }
        break;
    }
    }

    //  -- site index ----------------------------------------------------------
    //  —— 目录页 ——
    const bool hasHome = [&project, pageCount]() {
        for (int index = 0; index < pageCount; ++index) {
            if (Page *page = project.pageAt(index)) {
                if (isHomeLike(*page))
                    return true;
            }
        }
        return false;
    }();

    if (!hasHome && pageCount > 1) {
        QVector<Page *> pages;
        for (int index = 0; index < pageCount; ++index) {
            if (Page *page = project.pageAt(index))
                pages.append(page);
        }
        HtmlEmitter html(m_options);
        addFile(QStringLiteral("_tcbw_index.html"), html.emitSiteIndex(pages),
                QStringLiteral("text/html; charset=utf-8"));
    }

    result.ok = result.errors.isEmpty();
    return result;
}

// ---------------------------------------------------------------------------
//  Convenience wrappers
//  便捷封装
// ---------------------------------------------------------------------------
QString CodeGenerator::generateHtml(const ProjectDocument &project, const QString &pageId) const
{
    const Page *page = resolvePage(project, pageId);
    if (!page)
        return QString();
    const QVector<BlockScript *> scoped = project.program();
    return buildDocument(*page, scoped, &project);
}

QString CodeGenerator::generateJavaScript(const ProjectDocument &project,
                                         const QString &pageId) const
{
    const Page *page = resolvePage(project, pageId);
    if (!page)
        return QString();
    return buildScript(*page, project.program(), &project);
}

QString CodeGenerator::generateCss(const ProjectDocument &project, const QString &pageId) const
{
    const Page *page = resolvePage(project, pageId);
    if (!page)
        return QString();
    return buildStylesheet(*page, &project);
}

// ---------------------------------------------------------------------------
//  Writing
//  写入
// ---------------------------------------------------------------------------
bool CodeGenerator::writeToDirectory(const EmitResult &result, const QString &directory,
                                     QStringList *errors)
{
    bool allOk = true;

    auto refuse = [&](const QString &english, const QString &chinese) {
        if (errors)
            errors->append(refusal(english, chinese));
        allOk = false;
    };

    const QString dirTrimmed = directory.trimmed();
    if (dirTrimmed.isEmpty()) {
        refuse(QStringLiteral("the export directory is empty"),
               QStringLiteral("导出目录为空"));
        return false;
    }
    if (dirTrimmed.contains(QChar(0))) {
        refuse(QStringLiteral("the export directory contains a NUL byte"),
               QStringLiteral("导出目录包含 NUL 字节"));
        return false;
    }

    //  Never resolve the directory through a symlink: the canonical path decides
    //  what "inside" means for every file below.
    //  绝不通过符号链接解析该目录：规范路径决定了下属文件“位于内部”的含义。
    QFileInfo dirInfo(dirTrimmed);
    if (dirInfo.exists() && dirInfo.isSymLink()) {
        refuse(QStringLiteral("the export directory itself is a symbolic link: %1")
                   .arg(dirTrimmed),
               QStringLiteral("导出目录本身是指向别处的符号链接：%1").arg(dirTrimmed));
        return false;
    }

    if (!QDir().mkpath(dirTrimmed)) {
        refuse(QStringLiteral("cannot create the export directory %1").arg(dirTrimmed),
               QStringLiteral("无法创建导出目录 %1").arg(dirTrimmed));
        return false;
    }
    dirInfo.refresh();
    if (!dirInfo.isDir() || !dirInfo.isWritable()) {
        refuse(QStringLiteral("the export directory is not a writable directory: %1")
                   .arg(dirTrimmed),
               QStringLiteral("导出目录不是可写目录：%1").arg(dirTrimmed));
        return false;
    }
    const QString canonicalDir = dirInfo.canonicalFilePath();
    if (canonicalDir.isEmpty()) {
        refuse(QStringLiteral("cannot resolve the export directory %1").arg(dirTrimmed),
               QStringLiteral("无法解析导出目录 %1").arg(dirTrimmed));
        return false;
    }

    for (const EmittedFile &file : result.files) {
        const QString rawRelative = file.relativePath.trimmed();

        //  -- path validation -------------------------------------------------
        //  —— 路径校验 ——
        if (rawRelative.isEmpty()) {
            refuse(QStringLiteral("an output file has an empty relative path"),
                   QStringLiteral("某个输出文件的相对路径为空"));
            continue;
        }
        if (rawRelative.contains(QChar(0))) {
            refuse(QStringLiteral("output path contains a NUL byte: %1").arg(rawRelative),
                   QStringLiteral("输出路径包含 NUL 字节：%1").arg(rawRelative));
            continue;
        }
        if (rawRelative.contains(QLatin1Char('\\'))) {
            refuse(QStringLiteral("output path contains a backslash: %1").arg(rawRelative),
                   QStringLiteral("输出路径包含反斜杠：%1").arg(rawRelative));
            continue;
        }
        if (looksAbsolute(rawRelative)) {
            refuse(QStringLiteral("output path is absolute: %1").arg(rawRelative),
                   QStringLiteral("输出路径是绝对路径：%1").arg(rawRelative));
            continue;
        }
        if (hasParentTraversal(rawRelative)) {
            refuse(QStringLiteral("output path escapes the export directory with '..': %1")
                       .arg(rawRelative),
                   QStringLiteral("输出路径通过 '..' 逃逸出导出目录：%1").arg(rawRelative));
            continue;
        }

        QString relative = QDir::cleanPath(rawRelative);
        while (relative.startsWith(QLatin1String("./")))
            relative = relative.mid(2);
        if (relative.isEmpty() || relative == QLatin1String(".")
            || relative.startsWith(QLatin1String("../"))) {
            refuse(QStringLiteral("output path resolves outside the export directory: %1")
                       .arg(rawRelative),
                   QStringLiteral("输出路径解析后位于导出目录之外：%1").arg(rawRelative));
            continue;
        }

        const QString targetPath = QDir(canonicalDir).filePath(relative);
        const QString targetParent = QFileInfo(targetPath).absolutePath();

        if (!makeDirectoriesSafely(targetParent, errors, dirTrimmed)) {
            allOk = false;
            continue;
        }

        //  Re-verify with the canonical form: the parent must still live inside
        //  the export directory after every symlink has been resolved.
        //  用规范形式再次校验：解析所有符号链接后，父目录仍必须位于导出目录内。
        const QString canonicalParent = QFileInfo(targetParent).canonicalFilePath();
        if (canonicalParent.isEmpty()
            || (canonicalParent != canonicalDir
                && !canonicalParent.startsWith(canonicalDir + QLatin1Char('/')))) {
            refuse(QStringLiteral("output path resolves outside the export directory: %1")
                       .arg(relative),
                   QStringLiteral("输出路径解析后位于导出目录之外：%1").arg(relative));
            continue;
        }

        //  -- destination validation ------------------------------------------
        //  —— 目标校验 ——
        const QFileInfo targetInfo(targetPath);
        if (targetInfo.exists() && targetInfo.isSymLink()) {
            refuse(QStringLiteral("destination is a symbolic link: %1").arg(relative),
                   QStringLiteral("目标是符号链接：%1").arg(relative));
            continue;
        }
        if (targetInfo.exists() && targetInfo.isDir()) {
            refuse(QStringLiteral("destination is a directory: %1").arg(relative),
                   QStringLiteral("目标是目录：%1").arg(relative));
            continue;
        }

        //  -- atomic write -----------------------------------------------------
        //  —— 原子写入 ——
        QSaveFile output(targetPath);
        output.setDirectWriteFallback(false);
        if (!output.open(QIODevice::WriteOnly)) {
            refuse(QStringLiteral("cannot open %1 for writing: %2")
                       .arg(relative, output.errorString()),
                   QStringLiteral("无法打开 %1 进行写入：%2")
                       .arg(relative, output.errorString()));
            continue;
        }

        const QByteArray payload = file.binary ? file.bytes : file.contents.toUtf8();
        if (payload.size() > kMaxGeneratedFileBytes) {
            output.cancelWriting();
            refuse(QStringLiteral("refusing to write %1 because it exceeds %2 bytes")
                       .arg(relative, QString::number(kMaxGeneratedFileBytes)),
                   QStringLiteral("拒绝写入 %1，因为它超过 %2 字节")
                       .arg(relative, QString::number(kMaxGeneratedFileBytes)));
            continue;
        }

        const qint64 written = output.write(payload);
        if (written != payload.size()) {
            output.cancelWriting();
            refuse(QStringLiteral("short write for %1 (%2 of %3 bytes)")
                       .arg(relative, QString::number(written), QString::number(payload.size())),
                   QStringLiteral("%1 写入不完整（%2 / %3 字节）")
                       .arg(relative, QString::number(written), QString::number(payload.size())));
            continue;
        }

        if (!output.commit()) {
            refuse(QStringLiteral("cannot finalise %1: %2").arg(relative, output.errorString()),
                   QStringLiteral("无法完成 %1 的写入：%2")
                       .arg(relative, output.errorString()));
            continue;
        }
    }

    return allOk;
}

// ---------------------------------------------------------------------------
//  Summary
//  摘要
// ---------------------------------------------------------------------------
QString CodeGenerator::summarise(const EmitResult &result, bool chinese)
{
    if (result.files.isEmpty()) {
        return chinese ? QStringLiteral("未生成任何文件")
                       : QStringLiteral("Nothing was generated");
    }

    QStringList names;
    for (int index = 0; index < result.files.size() && index < 4; ++index)
        names.append(result.files.at(index).relativePath);
    QString listed = names.join(QStringLiteral(", "));
    if (result.files.size() > 4)
        listed += QStringLiteral(", …");

    const int count = result.files.size();
    const QString size = humanBytes(result.totalBytes());

    if (chinese) {
        QString text = QStringLiteral("已生成 %1 个文件（%2）：%3")
                           .arg(count)
                           .arg(size, listed);
        if (!result.errors.isEmpty())
            text += QStringLiteral("；%1 个错误").arg(result.errors.size());
        if (!result.warnings.isEmpty())
            text += QStringLiteral("；%1 个警告").arg(result.warnings.size());
        return text;
    }

    QString text = QStringLiteral("Generated %1 files (%2): %3").arg(count).arg(size, listed);
    if (!result.errors.isEmpty())
        text += QStringLiteral("; %1 error(s)").arg(result.errors.size());
    if (!result.warnings.isEmpty())
        text += QStringLiteral("; %1 warning(s)").arg(result.warnings.size());
    return text;
}

} // namespace tcbw
