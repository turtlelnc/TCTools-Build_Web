// ============================================================================
//  tst_codegen.cpp -- HTML/CSS/JS emission, escaping and safe writing
//  tst_codegen.cpp —— HTML/CSS/JS 生成、转义与安全写入
// ============================================================================
#include "CodeGenerator.h"
#include "CssEmitter.h"
#include "DocumentNode.h"
#include "EmitOptions.h"
#include "ElementCatalog.h"
#include "HtmlEmitter.h"
#include "JsEmitter.h"
#include "Page.h"
#include "ProjectDocument.h"

#include <QTest>
#include <QTemporaryDir>

using namespace tcbw;

namespace {

//  Builds a page with a single node whose text is hostile, so the escaping
//  rules are exercised on every emission path.
//  构建一个页面，其唯一节点的文本具有攻击性，从而在每条生成路径上都检验转义规则。
std::unique_ptr<Page> pageWithHostileText()
{
    auto page = Page::makeDefault(QStringLiteral("index"), false);
    auto section = DocumentNode::makeElement(QStringLiteral("section"));
    section->id = QStringLiteral("hostile-sec");
    section->setAttribute(QStringLiteral("title"), QStringLiteral("\" onmouseover=\"alert(1)"));

    auto heading = DocumentNode::makeElement(QStringLiteral("h1"));
    heading->id = QStringLiteral("hostile-h1");
    heading->text = QStringLiteral("<script>alert('xss')</script> & \"quoted\"");
    section->appendChild(std::move(heading));

    auto anchor = DocumentNode::makeElement(QStringLiteral("a"));
    anchor->id = QStringLiteral("hostile-a");
    anchor->setAttribute(QStringLiteral("href"), QStringLiteral("javascript:alert(1)"));
    anchor->text = QStringLiteral("click");
    section->appendChild(std::move(anchor));

    page->root->appendChild(std::move(section));
    return page;
}

} // namespace

class TestCodegen : public QObject
{
    Q_OBJECT

private slots:
    // --- escaping ----------------------------------------------------------
    void htmlEscapingCoversBothContexts()
    {
        QCOMPARE(escapeHtml(QStringLiteral("<a & b>")), QStringLiteral("&lt;a &amp; b&gt;"));
        //  Attribute context additionally escapes the quote characters.
        //  属性上下文还会转义引号字符。
        const QString attr = escapeHtml(QStringLiteral("x\" onload=\"y"), true);
        QVERIFY(!attr.contains(QStringLiteral("\" onload")));
        QVERIFY(!attr.contains(QLatin1Char('"')));

        //  Existing entities must not be double-escaped.
        //  已有实体不得被二次转义。
        QCOMPARE(escapeHtml(QStringLiteral("&amp;")), QStringLiteral("&amp;"));
        QCOMPARE(escapeHtml(QStringLiteral("&lt;")), QStringLiteral("&lt;"));
    }

    void urlSafetyBlocksScriptSchemes()
    {
        QVERIFY(isSafeUrl(QStringLiteral("https://example.com/a")));
        QVERIFY(isSafeUrl(QStringLiteral("/relative/path")));
        QVERIFY(isSafeUrl(QStringLiteral("#anchor")));
        QVERIFY(isSafeUrl(QStringLiteral("mailto:a@b.c")));
        QVERIFY(isSafeUrl(QStringLiteral("data:image/png;base64,AAAA")));

        QVERIFY(!isSafeUrl(QStringLiteral("javascript:alert(1)")));
        QVERIFY(!isSafeUrl(QStringLiteral("JaVaScRiPt:alert(1)")));
        //  Leading control characters and whitespace must not defeat the check.
        //  前导控制字符与空白不得绕过检查。
        QVERIFY(!isSafeUrl(QStringLiteral("  \tjava\nscript:alert(1)")));
        QVERIFY(!isSafeUrl(QStringLiteral("data:text/html,<script>alert(1)</script>")));
        QVERIFY(!isSafeUrl(QStringLiteral("vbscript:msgbox(1)")));
    }

    void attributeAndTagNamesAreRestricted()
    {
        QVERIFY(isSafeAttributeName(QStringLiteral("data-tcbw-id")));
        QVERIFY(isSafeAttributeName(QStringLiteral("aria-label")));
        QVERIFY(!isSafeAttributeName(QStringLiteral("onload")));
        QVERIFY(!isSafeAttributeName(QStringLiteral("x\"><script>")));

        QVERIFY(isSafeTagName(QStringLiteral("div")));
        QVERIFY(isSafeTagName(QStringLiteral("my-element")));
        QVERIFY(!isSafeTagName(QStringLiteral("div><script")));
        QVERIFY(!isSafeTagName(QStringLiteral("1div")));
    }

    // --- HTML --------------------------------------------------------------
    void htmlEscapesHostileContent()
    {
        auto page = pageWithHostileText();
        HtmlEmitter emitter(EmitOptions{});

        const QString body = emitter.emitBody(*page);
        QVERIFY(!body.contains(QStringLiteral("<script>alert")));
        QVERIFY(body.contains(QStringLiteral("&lt;script&gt;")));

        //  Dangerous attribute values must be dropped rather than emitted. The
        //  property under test is positional: `javascript:` may legitimately
        //  appear inside the generated JavaScript runtime's scheme blocklist,
        //  but it must never appear as an attribute value.
        //  危险的属性值必须被丢弃，而不是照原样输出。被测性质与位置有关：
        //  `javascript:` 可以合法地出现在生成 JS 运行时的协议黑名单中，但绝不能
        //  作为属性值出现。
        const QString document = emitter.emitDocument(*page);
        QVERIFY(!document.contains(QStringLiteral("href=\"javascript:")));
        QVERIFY(!document.contains(QStringLiteral("href='javascript:")));
        QVERIFY(!document.contains(QStringLiteral("href=javascript:")));
        QVERIFY(!document.contains(QStringLiteral("onmouseover=\"alert")));
        QVERIFY(document.startsWith(QStringLiteral("<!DOCTYPE html>")));
    }

    void htmlVoidElementsHaveNoEndTag()
    {
        auto page = Page::makeDefault(QStringLiteral("index"), false);
        auto image = DocumentNode::makeElement(QStringLiteral("img"));
        image->id = QStringLiteral("el-img");
        image->setAttribute(QStringLiteral("src"), QStringLiteral("/a.png"));
        image->setAttribute(QStringLiteral("alt"), QStringLiteral("a"));
        page->root->appendChild(std::move(image));

        HtmlEmitter emitter(EmitOptions{});
        const QString body = emitter.emitBody(*page);
        QVERIFY(body.contains(QStringLiteral("<img")));
        QVERIFY(!body.contains(QStringLiteral("</img>")));
    }

    // --- CSS ---------------------------------------------------------------
    void cssEmitsResponsiveMediaQueries()
    {
        auto page = Page::makeDefault(QStringLiteral("index"), false);
        auto node = DocumentNode::makeElement(QStringLiteral("div"));
        node->id = QStringLiteral("css-node");
        node->name = QStringLiteral("Hero");
        node->style.base().set(QStringLiteral("padding"), QStringLiteral("32px"));
        node->style.overrideFor(Breakpoint::Mobile).set(QStringLiteral("padding"),
                                                        QStringLiteral("12px"));
        page->root->appendChild(std::move(node));

        CssEmitter emitter(EmitOptions{});
        const QString css = emitter.emitPage(*page);
        QVERIFY(css.contains(QStringLiteral("padding: 32px")));
        QVERIFY(css.contains(QStringLiteral("@media")));
        QVERIFY(css.contains(QStringLiteral("padding: 12px")));
        QVERIFY(css.contains(QStringLiteral(":root")));
    }

    void cssMinifierIsStringSafe()
    {
        const QString source = QStringLiteral(
            "/* c */ .a { content: \"x; y\"; margin: 0   4px ; }");
        const QString minified = CssEmitter::minify(source);
        QVERIFY(!minified.contains(QStringLiteral("/*")));
        //  The string body must survive intact; only the whitespace around the
        //  declaration may be collapsed.
        //  字符串内容必须完整保留；只有声明周围的空白可以被折叠。
        QVERIFY(minified.contains(QStringLiteral("content:\"x; y\""))
                || minified.contains(QStringLiteral("content: \"x; y\"")));
        QVERIFY(minified.contains(QStringLiteral(".a{")));
    }

    // --- JS ----------------------------------------------------------------
    void jsMinifierPreservesStringsAndRegex()
    {
        const QString source = QStringLiteral(
            "// comment\nvar a = \"http://not-a-comment//x\";\n"
            "var b = /\\/\\/regex/;\nvar c = 1; // trailing\n");
        const QString minified = JsEmitter::minify(source);
        QVERIFY(minified.contains(QStringLiteral("http://not-a-comment//x")));
        QVERIFY(minified.contains(QStringLiteral("var c = 1;")) || minified.contains(QStringLiteral("var c=1;")));
        QVERIFY(!minified.contains(QStringLiteral("// comment")));
    }

    void iifeWrappingIsCorrect()
    {
        const QString wrapped = JsEmitter::wrapIife(QStringLiteral("return 1;"),
                                                    QStringLiteral("App"), true, false);
        QVERIFY(wrapped.contains(QStringLiteral("use strict")));
        QVERIFY(wrapped.contains(QStringLiteral("return 1;")));

        const QString esm = JsEmitter::wrapIife(QStringLiteral("return 1;"),
                                                QStringLiteral("App"), false, true);
        QVERIFY(esm.contains(QStringLiteral("export")) || esm.contains(QStringLiteral("export default")));
    }

    // --- generator ---------------------------------------------------------
    void generatorProducesEveryExportMode()
    {
        auto project = ProjectDocument::makeStarter(false);

        const QVector<ExportMode> modes = {ExportMode::SingleHtml,
                                           ExportMode::HtmlWithAssets,
                                           ExportMode::ScriptOnly,
                                           ExportMode::HeadlessBundle};
        for (ExportMode mode : modes) {
            EmitOptions options;
            options.mode = mode;
            CodeGenerator generator(options);

            const EmitResult result = generator.generateProject(*project);
            QVERIFY2(result.ok, qPrintable(result.errors.join(QLatin1String("; "))));
            QVERIFY2(!result.files.isEmpty(), qPrintable(toString(mode)));

            //  A document must always be produced, whatever the mode.
            //  无论何种模式，都必须产出一个文档。
            bool hasDocument = false;
            for (const EmittedFile &file : result.files) {
                QVERIFY(!file.relativePath.contains(QStringLiteral("..")));
                if (file.relativePath.endsWith(QStringLiteral(".html")))
                    hasDocument = true;
            }
            QVERIFY2(hasDocument, qPrintable(toString(mode)));
        }
    }

    void generatorHonoursScriptOnlyExport()
    {
        auto project = ProjectDocument::makeStarter(false);
        EmitOptions options;
        options.mode = ExportMode::ScriptOnly;
        CodeGenerator generator(options);

        const EmitResult result = generator.generateProject(*project);
        QVERIFY(result.ok);

        bool hasJs = false;
        for (const EmittedFile &file : result.files) {
            if (file.relativePath.endsWith(QStringLiteral(".js")))
                hasJs = true;
        }
        QVERIFY(hasJs);
    }

    void writeToDirectoryRefusesTraversal()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        EmitResult result;
        result.ok = true;
        result.files.append(EmittedFile{QStringLiteral("../escaped.html"),
                                        QStringLiteral("<html></html>"), {}, {}, false});
        result.files.append(EmittedFile{QStringLiteral("ok/index.html"),
                                        QStringLiteral("<html></html>"), {}, {}, false});

        QStringList errors;
        const bool ok = CodeGenerator::writeToDirectory(result, dir.path(), &errors);

        //  The escape attempt must be refused and reported.
        //  逃逸尝试必须被拒绝并报告。
        QVERIFY(!ok);
        QVERIFY(!errors.isEmpty());
        QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("../escaped.html"))));

        //  The legitimate file must still have been written.
        //  合法文件仍应被写入。
        QVERIFY(QFile::exists(dir.filePath(QStringLiteral("ok/index.html"))));
    }

    void summariseMentionsFilesAndSize()
    {
        EmitResult result;
        result.ok = true;
        result.files.append(EmittedFile{QStringLiteral("index.html"), QString(2048, QLatin1Char('x')),
                                        {}, QStringLiteral("text/html"), false});

        const QString english = CodeGenerator::summarise(result, false);
        QVERIFY(english.contains(QStringLiteral("index.html")));

        const QString chinese = CodeGenerator::summarise(result, true);
        QVERIFY(!chinese.isEmpty());
        QVERIFY(chinese != english);
    }

    void stylesheetOnlyWhenStylesExist()
    {
        auto page = Page::makeDefault(QStringLiteral("index"), false);
        //  makeDefault deliberately styles the page root, so the root's own
        //  declarations are cleared here to isolate the behaviour under test.
        //  makeDefault 会刻意给页面根节点设置样式，因此这里清除根节点自身的声明，
        //  以隔离被测行为。
        page->root->style.base().clear();

        EmitOptions options;
        options.includeResetCss = false;
        options.includeAccessibilityHelpers = false;

        CssEmitter emitter(options);
        const QString tree = emitter.emitTree(*page);
        QVERIFY(tree.trimmed().isEmpty());
    }
};

QTEST_MAIN(TestCodegen)
#include "tst_codegen.moc"
