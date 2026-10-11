// ============================================================================
//  ElementCatalog.cpp -- the palette of HTML elements and ready-made widgets
//  ElementCatalog.cpp —— HTML 元素与预制组件（控件）面板目录
//
//  This file is the single source of truth for "what can be dragged onto the
//  canvas". Every row of the element table below becomes one palette entry; the
//  composite table becomes one ready-made widget. No other translation unit has
//  to change when the palette grows.
//  本文件是"画布上可以拖入什么"的唯一权威来源。下方元素表中的每一行都会成为
//  一个素材条目，组件表则对应一个预制控件。素材增加时无需修改其他编译单元。
// ============================================================================
#include "ElementCatalog.h"

#include "IdGenerator.h"

#include <QHash>
#include <QSet>
#include <QStringList>

#include <algorithm>

namespace tcbw {

namespace {

// ---------------------------------------------------------------------------
//  Small building blocks for the tables below.
//  下方表格使用的小型构造辅助。
// ---------------------------------------------------------------------------

//  Reads better than a QMap brace-list at every call site.
//  比每次调用都写 QMap 花括号列表更易读。
QMap<QString, QString> attrs(std::initializer_list<QPair<QString, QString>> pairs)
{
    QMap<QString, QString> out;
    for (const auto &p : pairs)
        out.insert(p.first, p.second);
    return out;
}

//  Declarations whose value carries "!important" are applied through
//  StyleSheet::setImportant so the generated CSS keeps the priority hint.
//  值中带 "!important" 的声明通过 StyleSheet::setImportant 写入，使生成的 CSS
//  保留优先级提示。
void applyDeclaration(StyleSheet &sheet, const QString &property, const QString &value)
{
    const QString trimmed = value.trimmed();
    static const QString imperative = QStringLiteral("!important");
    if (trimmed.endsWith(imperative, Qt::CaseInsensitive)) {
        QString plain = trimmed.left(trimmed.size() - imperative.size()).trimmed();
        sheet.setImportant(property, plain);
    } else {
        sheet.set(property, trimmed);
    }
}

void applyStyleMap(ResponsiveStyle &style, const QMap<QString, QString> &map)
{
    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
        applyDeclaration(style.base(), it.key(), it.value());
}

//  Creates one element node with an inline declaration list.
//  创建一个带内联声明列表的元素节点。
std::unique_ptr<DocumentNode> el(const QString &tag,
                                 std::initializer_list<QPair<QString, QString>> decls)
{
    auto node = DocumentNode::makeElement(tag);
    for (const auto &d : decls)
        applyDeclaration(node->style.base(), d.first, d.second);
    return node;
}

//  Shorthand for a text node inside a composite.
//  组件内部文本节点的简写。
std::unique_ptr<DocumentNode> txt(const QString &text)
{
    return DocumentNode::makeText(text);
}

//  Names a composite root bilingually (used by the layer panel).
//  为组件根节点写入双语名称（供图层面板使用）。
void setLabel(DocumentNode *node, const QString &en, const QString &zh)
{
    if (!node)
        return;
    node->data.insert(QStringLiteral("labelEn"), en);
    node->data.insert(QStringLiteral("labelZh"), zh);
    if (node->name.isEmpty())
        node->name = en;
}

//  Every composite root is a block-level flex container unless it says
//  otherwise, which keeps the generated markup predictable.
//  除非另有说明，组件根节点都是块级弹性容器，使生成的结构可预测。
std::unique_ptr<DocumentNode> root(const QString &tag,
                                   std::initializer_list<QPair<QString, QString>> decls,
                                   const QString &labelEn, const QString &labelZh)
{
    auto node = el(tag, decls);
    setLabel(node.get(), labelEn, labelZh);
    return node;
}

// ---------------------------------------------------------------------------
//  Tag classifier sets
//  标签分类集合
// ---------------------------------------------------------------------------

const QSet<QString> &voidTagSet()
{
    static const QSet<QString> s = {
        QStringLiteral("area"), QStringLiteral("base"), QStringLiteral("br"),
        QStringLiteral("col"), QStringLiteral("embed"), QStringLiteral("hr"),
        QStringLiteral("img"), QStringLiteral("input"), QStringLiteral("link"),
        QStringLiteral("meta"), QStringLiteral("param"), QStringLiteral("source"),
        QStringLiteral("track"), QStringLiteral("wbr")
    };
    return s;
}

const QSet<QString> &textTagSet()
{
    static const QSet<QString> s = {
        QStringLiteral("a"), QStringLiteral("abbr"), QStringLiteral("address"),
        QStringLiteral("b"), QStringLiteral("bdi"), QStringLiteral("bdo"),
        QStringLiteral("blockquote"), QStringLiteral("button"),
        QStringLiteral("caption"), QStringLiteral("cite"), QStringLiteral("code"),
        QStringLiteral("dd"), QStringLiteral("del"), QStringLiteral("dfn"),
        QStringLiteral("dt"), QStringLiteral("em"), QStringLiteral("figcaption"),
        QStringLiteral("h1"), QStringLiteral("h2"), QStringLiteral("h3"),
        QStringLiteral("h4"), QStringLiteral("h5"), QStringLiteral("h6"),
        QStringLiteral("i"), QStringLiteral("ins"), QStringLiteral("kbd"),
        QStringLiteral("label"), QStringLiteral("legend"), QStringLiteral("li"),
        QStringLiteral("mark"), QStringLiteral("option"), QStringLiteral("output"),
        QStringLiteral("p"), QStringLiteral("pre"), QStringLiteral("q"),
        QStringLiteral("s"), QStringLiteral("samp"), QStringLiteral("small"),
        QStringLiteral("span"), QStringLiteral("strong"), QStringLiteral("sub"),
        QStringLiteral("summary"), QStringLiteral("sup"), QStringLiteral("td"),
        QStringLiteral("th"), QStringLiteral("time"), QStringLiteral("title"),
        QStringLiteral("u"), QStringLiteral("var")
    };
    return s;
}

const QSet<QString> &inlineTagSet()
{
    static const QSet<QString> s = {
        QStringLiteral("a"), QStringLiteral("abbr"), QStringLiteral("b"),
        QStringLiteral("bdi"), QStringLiteral("bdo"), QStringLiteral("br"),
        QStringLiteral("button"), QStringLiteral("cite"), QStringLiteral("code"),
        QStringLiteral("data"), QStringLiteral("del"), QStringLiteral("dfn"),
        QStringLiteral("em"), QStringLiteral("i"), QStringLiteral("img"),
        QStringLiteral("input"), QStringLiteral("ins"), QStringLiteral("kbd"),
        QStringLiteral("label"), QStringLiteral("mark"), QStringLiteral("meter"),
        QStringLiteral("output"), QStringLiteral("progress"), QStringLiteral("q"),
        QStringLiteral("s"), QStringLiteral("samp"), QStringLiteral("select"),
        QStringLiteral("small"), QStringLiteral("span"), QStringLiteral("strong"),
        QStringLiteral("sub"), QStringLiteral("sup"), QStringLiteral("textarea"),
        QStringLiteral("time"), QStringLiteral("u"), QStringLiteral("var"),
        QStringLiteral("wbr")
    };
    return s;
}

const QSet<QString> &headingTagSet()
{
    static const QSet<QString> s = {
        QStringLiteral("h1"), QStringLiteral("h2"), QStringLiteral("h3"),
        QStringLiteral("h4"), QStringLiteral("h5"), QStringLiteral("h6")
    };
    return s;
}

const QSet<QString> &formControlSet()
{
    static const QSet<QString> s = {
        QStringLiteral("button"), QStringLiteral("datalist"), QStringLiteral("fieldset"),
        QStringLiteral("form"), QStringLiteral("input"), QStringLiteral("label"),
        QStringLiteral("legend"), QStringLiteral("meter"), QStringLiteral("optgroup"),
        QStringLiteral("option"), QStringLiteral("output"), QStringLiteral("progress"),
        QStringLiteral("select"), QStringLiteral("textarea")
    };
    return s;
}

const QSet<QString> &mediaTagSet()
{
    static const QSet<QString> s = {
        QStringLiteral("area"), QStringLiteral("audio"), QStringLiteral("canvas"),
        QStringLiteral("embed"), QStringLiteral("iframe"), QStringLiteral("img"),
        QStringLiteral("map"), QStringLiteral("object"), QStringLiteral("picture"),
        QStringLiteral("source"), QStringLiteral("svg"), QStringLiteral("track"),
        QStringLiteral("video")
    };
    return s;
}

const QSet<QString> &headTagSet()
{
    static const QSet<QString> s = {
        QStringLiteral("base"), QStringLiteral("link"), QStringLiteral("meta"),
        QStringLiteral("noscript"), QStringLiteral("script"), QStringLiteral("style"),
        QStringLiteral("template"), QStringLiteral("title")
    };
    return s;
}

} // namespace

// ===========================================================================
//  ElementCatalog -- construction
//  目录构造
// ===========================================================================

const ElementCatalog &ElementCatalog::instance()
{
    //  Function-local static: thread-safe initialisation since C++11.
    //  函数内静态对象：自 C++11 起初始化线程安全。
    static const ElementCatalog catalog;
    return catalog;
}

ElementCatalog::ElementCatalog()
{
    registerElements();
    registerComposites();
}

// ---------------------------------------------------------------------------
//  The element table
//  元素表
// ---------------------------------------------------------------------------
void ElementCatalog::registerElements()
{
    m_elements.clear();
    m_byTag.clear();

    //  Inserts one palette row. Keeps registration readable despite the number
    //  of fields involved.
    //  插入一行素材条目。字段虽多，但让注册代码保持可读。
    auto add = [this](const QString &tag,
                      const QString &labelEn, const QString &labelZh,
                      const QString &groupEn, const QString &groupZh,
                      const QString &descEn, const QString &descZh,
                      const QString &icon, ElementCategory category,
                      const QString &textEn, const QString &textZh,
                      const QMap<QString, QString> &defaultAttributes,
                      const QMap<QString, QString> &defaultStyle,
                      bool allowsChildren, bool isVoid, int order) {
        ElementDef def;
        def.tag = tag;
        def.labelEn = labelEn;
        def.labelZh = labelZh;
        def.groupEn = groupEn;
        def.groupZh = groupZh;
        def.descriptionEn = descEn;
        def.descriptionZh = descZh;
        def.icon = icon;
        def.category = category;
        def.defaultTextEn = textEn;
        def.defaultTextZh = textZh;
        def.defaultAttributes = defaultAttributes;
        def.defaultStyleCss = defaultStyle;
        def.allowsChildren = allowsChildren;
        def.isVoid = isVoid;
        def.order = order;

        //  The first registration of a tag wins, so a palette variant such as
        //  "breadcrumb" never shadows the plain <nav> entry.
        //  同名标签首次注册生效，因此 "breadcrumb" 之类的面板变体不会遮蔽
        //  普通的 <nav> 条目。
        if (m_byTag.contains(tag))
            return;
        m_byTag.insert(tag, static_cast<int>(m_elements.size()));
        m_elements.append(def);
    };

    const QString empty;

    // -----------------------------------------------------------------------
    //  Layout / 布局
    // -----------------------------------------------------------------------
    add(QStringLiteral("div"), QStringLiteral("Container"), QStringLiteral("容器"),
        QStringLiteral("Basic"), QStringLiteral("基础"),
        QStringLiteral("Generic block container for grouping content."),
        QStringLiteral("用于组织内容的通用块级容器。"),
        QStringLiteral("layout-block"), ElementCategory::Layout, empty, empty,
        attrs({}), attrs({{QStringLiteral("display"), QStringLiteral("block")},
                          {QStringLiteral("padding"), QStringLiteral("12px")}}),
        true, false, 100);

    add(QStringLiteral("section"), QStringLiteral("Section"), QStringLiteral("区块"),
        QStringLiteral("Basic"), QStringLiteral("基础"),
        QStringLiteral("Thematic grouping of content, usually with a heading."),
        QStringLiteral("内容的主题分组，通常带有标题。"),
        QStringLiteral("layout-section"), ElementCategory::Layout, empty, empty,
        attrs({}), attrs({{QStringLiteral("display"), QStringLiteral("block")},
                          {QStringLiteral("padding"), QStringLiteral("24px")},
                          {QStringLiteral("width"), QStringLiteral("100%")}}),
        true, false, 110);

    add(QStringLiteral("header"), QStringLiteral("Header"), QStringLiteral("页头"),
        QStringLiteral("Landmarks"), QStringLiteral("地标"),
        QStringLiteral("Introductory content for a page or a section."),
        QStringLiteral("页面或区块的引导性内容。"),
        QStringLiteral("layout-header"), ElementCategory::Layout, empty, empty,
        attrs({}), attrs({{QStringLiteral("display"), QStringLiteral("flex")},
                          {QStringLiteral("align-items"), QStringLiteral("center")},
                          {QStringLiteral("gap"), QStringLiteral("16px")},
                          {QStringLiteral("padding"), QStringLiteral("20px 24px")}}),
        true, false, 120);

    add(QStringLiteral("footer"), QStringLiteral("Footer"), QStringLiteral("页脚"),
        QStringLiteral("Landmarks"), QStringLiteral("地标"),
        QStringLiteral("Closing content: copyright, links, contact details."),
        QStringLiteral("收尾内容：版权、链接、联系方式。"),
        QStringLiteral("layout-footer"), ElementCategory::Layout, empty, empty,
        attrs({}), attrs({{QStringLiteral("display"), QStringLiteral("block")},
                          {QStringLiteral("padding"), QStringLiteral("24px")},
                          {QStringLiteral("font-size"), QStringLiteral("14px")},
                          {QStringLiteral("opacity"), QStringLiteral("0.85")}}),
        true, false, 130);

    add(QStringLiteral("main"), QStringLiteral("Main"), QStringLiteral("主内容"),
        QStringLiteral("Landmarks"), QStringLiteral("地标"),
        QStringLiteral("The dominant content of the document; one per page."),
        QStringLiteral("文档的主体内容，每个页面只应有一个。"),
        QStringLiteral("layout-main"), ElementCategory::Layout, empty, empty,
        attrs({}), attrs({{QStringLiteral("display"), QStringLiteral("block")},
                          {QStringLiteral("padding"), QStringLiteral("24px")},
                          {QStringLiteral("flex"), QStringLiteral("1 1 auto")}}),
        true, false, 140);

    add(QStringLiteral("article"), QStringLiteral("Article"), QStringLiteral("文章"),
        QStringLiteral("Basic"), QStringLiteral("基础"),
        QStringLiteral("Self-contained composition such as a blog post."),
        QStringLiteral("自包含的独立内容，例如一篇博客文章。"),
        QStringLiteral("layout-article"), ElementCategory::Layout, empty, empty,
        attrs({}), attrs({{QStringLiteral("display"), QStringLiteral("block")},
                          {QStringLiteral("padding"), QStringLiteral("20px")},
                          {QStringLiteral("max-width"), QStringLiteral("72ch")}}),
        true, false, 150);

    add(QStringLiteral("aside"), QStringLiteral("Aside"), QStringLiteral("侧边栏"),
        QStringLiteral("Landmarks"), QStringLiteral("地标"),
        QStringLiteral("Tangentially related content such as a sidebar."),
        QStringLiteral("与主体间接相关的内容，例如侧边栏。"),
        QStringLiteral("layout-aside"), ElementCategory::Layout, empty, empty,
        attrs({}), attrs({{QStringLiteral("display"), QStringLiteral("block")},
                          {QStringLiteral("padding"), QStringLiteral("16px")},
                          {QStringLiteral("min-width"), QStringLiteral("220px")}}),
        true, false, 160);

    add(QStringLiteral("figure"), QStringLiteral("Figure"), QStringLiteral("插图组"),
        QStringLiteral("Basic"), QStringLiteral("基础"),
        QStringLiteral("Image, diagram or code sample with an optional caption."),
        QStringLiteral("图片、图示或代码示例，可带说明文字。"),
        QStringLiteral("layout-figure"), ElementCategory::Layout, empty, empty,
        attrs({}), attrs({{QStringLiteral("display"), QStringLiteral("block")},
                          {QStringLiteral("margin"), QStringLiteral("0 0 16px")}}),
        true, false, 170);

    add(QStringLiteral("figcaption"), QStringLiteral("Caption"), QStringLiteral("插图说明"),
        QStringLiteral("Basic"), QStringLiteral("基础"),
        QStringLiteral("Caption describing the parent figure."),
        QStringLiteral("描述所属插图的说明文字。"),
        QStringLiteral("layout-figcaption"), ElementCategory::Layout,
        QStringLiteral("Figure caption"),
        QStringLiteral("插图说明"),
        attrs({}), attrs({{QStringLiteral("font-size"), QStringLiteral("13px")},
                          {QStringLiteral("opacity"), QStringLiteral("0.75")},
                          {QStringLiteral("display"), QStringLiteral("block")}}),
        true, false, 180);

    add(QStringLiteral("hr"), QStringLiteral("Divider"), QStringLiteral("分隔线"),
        QStringLiteral("Basic"), QStringLiteral("基础"),
        QStringLiteral("Thematic break between blocks of content."),
        QStringLiteral("内容块之间的主题分隔线。"),
        QStringLiteral("layout-divider"), ElementCategory::Layout, empty, empty,
        attrs({}), attrs({{QStringLiteral("border"), QStringLiteral("0")},
                          {QStringLiteral("border-top"), QStringLiteral("1px solid #e2e8f0")},
                          {QStringLiteral("margin"), QStringLiteral("24px 0")}}),
        false, true, 190);

    add(QStringLiteral("br"), QStringLiteral("Line break"), QStringLiteral("换行"),
        QStringLiteral("Basic"), QStringLiteral("基础"),
        QStringLiteral("Forces a line break inside the current text flow."),
        QStringLiteral("在当前文本流中强制换行。"),
        QStringLiteral("layout-break"), ElementCategory::Layout, empty, empty,
        attrs({}), attrs({}), false, true, 200);

    add(QStringLiteral("spacer"), QStringLiteral("Spacer"), QStringLiteral("间隔块"),
        QStringLiteral("Basic"), QStringLiteral("基础"),
        QStringLiteral("Empty block used to push content apart."),
        QStringLiteral("用于撑开内容间距的空块。"),
        QStringLiteral("layout-spacer"), ElementCategory::Layout, empty, empty,
        attrs({{QStringLiteral("class"), QStringLiteral("tcbw-spacer")}}),
        attrs({{QStringLiteral("display"), QStringLiteral("block")},
               {QStringLiteral("height"), QStringLiteral("40px")},
               {QStringLiteral("flex"), QStringLiteral("0 0 auto")}}),
        false, false, 210);

    add(QStringLiteral("details"), QStringLiteral("Details"), QStringLiteral("折叠面板"),
        QStringLiteral("Interactive"), QStringLiteral("交互"),
        QStringLiteral("Disclosure widget that expands to reveal more content."),
        QStringLiteral("可展开以显示更多内容的折叠控件。"),
        QStringLiteral("layout-details"), ElementCategory::Layout, empty, empty,
        attrs({}), attrs({{QStringLiteral("display"), QStringLiteral("block")},
                          {QStringLiteral("padding"), QStringLiteral("12px")},
                          {QStringLiteral("border"), QStringLiteral("1px solid #e2e8f0")},
                          {QStringLiteral("border-radius"), QStringLiteral("8px")}}),
        true, false, 220);

    add(QStringLiteral("summary"), QStringLiteral("Summary"), QStringLiteral("折叠标题"),
        QStringLiteral("Interactive"), QStringLiteral("交互"),
        QStringLiteral("Always-visible label of a details element."),
        QStringLiteral("details 元素始终可见的标题。"),
        QStringLiteral("layout-summary"), ElementCategory::Layout,
        QStringLiteral("Toggle section"),
        QStringLiteral("展开或收起"),
        attrs({}), attrs({{QStringLiteral("cursor"), QStringLiteral("pointer")},
                          {QStringLiteral("font-weight"), QStringLiteral("600")}}),
        true, false, 230);

    add(QStringLiteral("dialog"), QStringLiteral("Dialog"), QStringLiteral("对话框"),
        QStringLiteral("Interactive"), QStringLiteral("交互"),
        QStringLiteral("Modal or non-modal dialog window."),
        QStringLiteral("模态或非模态对话框窗口。"),
        QStringLiteral("layout-dialog"), ElementCategory::Layout, empty, empty,
        attrs({}), attrs({{QStringLiteral("padding"), QStringLiteral("20px")},
                          {QStringLiteral("border-radius"), QStringLiteral("12px")},
                          {QStringLiteral("border"), QStringLiteral("1px solid #cbd5e1")},
                          {QStringLiteral("background"), QStringLiteral("#ffffff")}}),
        true, false, 240);

    // -----------------------------------------------------------------------
    //  Text / 文本
    // -----------------------------------------------------------------------
    add(QStringLiteral("h1"), QStringLiteral("Heading 1"), QStringLiteral("一级标题"),
        QStringLiteral("Headings"), QStringLiteral("标题"),
        QStringLiteral("Top-level page heading; one per page."),
        QStringLiteral("页面最高级标题，每页建议只用一个。"),
        QStringLiteral("text-h1"), ElementCategory::Text,
        QStringLiteral("Build web pages by dragging"),
        QStringLiteral("拖拽即可构建网页"),
        attrs({}), attrs({{QStringLiteral("font-size"), QStringLiteral("2.25rem")},
                          {QStringLiteral("font-weight"), QStringLiteral("700")},
                          {QStringLiteral("line-height"), QStringLiteral("1.2")},
                          {QStringLiteral("margin"), QStringLiteral("0 0 12px")}}),
        true, false, 300);

    add(QStringLiteral("h2"), QStringLiteral("Heading 2"), QStringLiteral("二级标题"),
        QStringLiteral("Headings"), QStringLiteral("标题"),
        QStringLiteral("Second-level section heading."),
        QStringLiteral("二级区块标题。"),
        QStringLiteral("text-h2"), ElementCategory::Text,
        QStringLiteral("Everything you need"),
        QStringLiteral("你需要的一切"),
        attrs({}), attrs({{QStringLiteral("font-size"), QStringLiteral("1.75rem")},
                          {QStringLiteral("font-weight"), QStringLiteral("700")},
                          {QStringLiteral("line-height"), QStringLiteral("1.25")},
                          {QStringLiteral("margin"), QStringLiteral("0 0 10px")}}),
        true, false, 310);

    add(QStringLiteral("h3"), QStringLiteral("Heading 3"), QStringLiteral("三级标题"),
        QStringLiteral("Headings"), QStringLiteral("标题"),
        QStringLiteral("Third-level section heading."),
        QStringLiteral("三级区块标题。"),
        QStringLiteral("text-h3"), ElementCategory::Text,
        QStringLiteral("Visual layout")
        , QStringLiteral("可视化布局"),
        attrs({}), attrs({{QStringLiteral("font-size"), QStringLiteral("1.375rem")},
                          {QStringLiteral("font-weight"), QStringLiteral("600")},
                          {QStringLiteral("line-height"), QStringLiteral("1.3")},
                          {QStringLiteral("margin"), QStringLiteral("0 0 8px")}}),
        true, false, 320);

    add(QStringLiteral("h4"), QStringLiteral("Heading 4"), QStringLiteral("四级标题"),
        QStringLiteral("Headings"), QStringLiteral("标题"),
        QStringLiteral("Fourth-level section heading."),
        QStringLiteral("四级区块标题。"),
        QStringLiteral("text-h4"), ElementCategory::Text,
        QStringLiteral("Block programming"),
        QStringLiteral("积木化编程"),
        attrs({}), attrs({{QStringLiteral("font-size"), QStringLiteral("1.125rem")},
                          {QStringLiteral("font-weight"), QStringLiteral("600")},
                          {QStringLiteral("margin"), QStringLiteral("0 0 6px")}}),
        true, false, 330);

    add(QStringLiteral("h5"), QStringLiteral("Heading 5"), QStringLiteral("五级标题"),
        QStringLiteral("Headings"), QStringLiteral("标题"),
        QStringLiteral("Fifth-level section heading."),
        QStringLiteral("五级区块标题。"),
        QStringLiteral("text-h5"), ElementCategory::Text,
        QStringLiteral("Export and publish"),
        QStringLiteral("导出与发布"),
        attrs({}), attrs({{QStringLiteral("font-size"), QStringLiteral("1rem")},
                          {QStringLiteral("font-weight"), QStringLiteral("600")},
                          {QStringLiteral("margin"), QStringLiteral("0 0 6px")}}),
        true, false, 340);

    add(QStringLiteral("h6"), QStringLiteral("Heading 6"), QStringLiteral("六级标题"),
        QStringLiteral("Headings"), QStringLiteral("标题"),
        QStringLiteral("Sixth-level section heading."),
        QStringLiteral("六级区块标题。"),
        QStringLiteral("text-h6"), ElementCategory::Text,
        QStringLiteral("Notes"),
        QStringLiteral("备注"),
        attrs({}), attrs({{QStringLiteral("font-size"), QStringLiteral("0.875rem")},
                          {QStringLiteral("font-weight"), QStringLiteral("600")},
                          {QStringLiteral("text-transform"), QStringLiteral("uppercase")},
                          {QStringLiteral("letter-spacing"), QStringLiteral("0.05em")}}),
        true, false, 350);

    add(QStringLiteral("p"), QStringLiteral("Paragraph"), QStringLiteral("段落"),
        QStringLiteral("Body"), QStringLiteral("正文"),
        QStringLiteral("A run of body copy."),
        QStringLiteral("一段正文文本。"),
        QStringLiteral("text-paragraph"), ElementCategory::Text,
        QStringLiteral("Write your copy here, then connect it to an event with a block script."),
        QStringLiteral("在此撰写文案，然后用积木脚本把它与事件连接起来。"),
        attrs({}), attrs({{QStringLiteral("font-size"), QStringLiteral("1rem")},
                          {QStringLiteral("line-height"), QStringLiteral("1.7")},
                          {QStringLiteral("margin"), QStringLiteral("0 0 12px")}}),
        true, false, 360);

    add(QStringLiteral("span"), QStringLiteral("Span"), QStringLiteral("行内容器"),
        QStringLiteral("Body"), QStringLiteral("正文"),
        QStringLiteral("Inline container for a fragment of text."),
        QStringLiteral("用于包裹文本片段的行内容器。"),
        QStringLiteral("text-span"), ElementCategory::Text,
        QStringLiteral("Highlighted text"),
        QStringLiteral("高亮文本"),
        attrs({}), attrs({}), true, false, 370);

    add(QStringLiteral("a"), QStringLiteral("Link"), QStringLiteral("链接"),
        QStringLiteral("Body"), QStringLiteral("正文"),
        QStringLiteral("Hyperlink to another page or resource."),
        QStringLiteral("指向其他页面或资源的超链接。"),
        QStringLiteral("text-link"), ElementCategory::Text,
        QStringLiteral("Learn more"),
        QStringLiteral("了解更多"),
        attrs({{QStringLiteral("href"), QStringLiteral("#")}}),
        attrs({{QStringLiteral("color"), QStringLiteral("#2563eb")},
               {QStringLiteral("text-decoration"), QStringLiteral("underline")},
               {QStringLiteral("cursor"), QStringLiteral("pointer")}}),
        true, false, 380);

    add(QStringLiteral("strong"), QStringLiteral("Strong"), QStringLiteral("加粗"),
        QStringLiteral("Inline"), QStringLiteral("行内"),
        QStringLiteral("Strong importance; rendered bold."),
        QStringLiteral("表示重要性强，渲染为粗体。"),
        QStringLiteral("text-strong"), ElementCategory::Text,
        QStringLiteral("important"),
        QStringLiteral("重要"),
        attrs({}), attrs({{QStringLiteral("font-weight"), QStringLiteral("700")}}),
        true, false, 390);

    add(QStringLiteral("em"), QStringLiteral("Emphasis"), QStringLiteral("强调"),
        QStringLiteral("Inline"), QStringLiteral("行内"),
        QStringLiteral("Stress emphasis; rendered italic."),
        QStringLiteral("表示语气强调，渲染为斜体。"),
        QStringLiteral("text-em"), ElementCategory::Text,
        QStringLiteral("emphasis"),
        QStringLiteral("强调"),
        attrs({}), attrs({{QStringLiteral("font-style"), QStringLiteral("italic")}}),
        true, false, 400);

    add(QStringLiteral("small"), QStringLiteral("Small print"), QStringLiteral("小字"),
        QStringLiteral("Inline"), QStringLiteral("行内"),
        QStringLiteral("Side comments and fine print."),
        QStringLiteral("旁注与附属细则。"),
        QStringLiteral("text-small"), ElementCategory::Text,
        QStringLiteral("Terms and conditions apply."),
        QStringLiteral("适用条款与条件。"),
        attrs({}), attrs({{QStringLiteral("font-size"), QStringLiteral("0.8125rem")},
                          {QStringLiteral("opacity"), QStringLiteral("0.8")}}),
        true, false, 410);

    add(QStringLiteral("mark"), QStringLiteral("Highlight"), QStringLiteral("标记"),
        QStringLiteral("Inline"), QStringLiteral("行内"),
        QStringLiteral("Text marked for reference, drawn with a highlight."),
        QStringLiteral("用于参考的标记文本，带有高亮背景。"),
        QStringLiteral("text-mark"), ElementCategory::Text,
        QStringLiteral("highlighted"),
        QStringLiteral("高亮"),
        attrs({}), attrs({{QStringLiteral("background"), QStringLiteral("#fef08a")},
                          {QStringLiteral("padding"), QStringLiteral("0 2px")}}),
        true, false, 420);

    add(QStringLiteral("code"), QStringLiteral("Code"), QStringLiteral("代码"),
        QStringLiteral("Inline"), QStringLiteral("行内"),
        QStringLiteral("Inline fragment of computer code."),
        QStringLiteral("行内的计算机代码片段。"),
        QStringLiteral("text-code"), ElementCategory::Text,
        QStringLiteral("npm run build"),
        QStringLiteral("npm run build"),
        attrs({}), attrs({{QStringLiteral("font-family"), QStringLiteral("ui-monospace, SFMono-Regular, Consolas, monospace")},
                          {QStringLiteral("background"), QStringLiteral("#f1f5f9")},
                          {QStringLiteral("padding"), QStringLiteral("2px 6px")},
                          {QStringLiteral("border-radius"), QStringLiteral("4px")}}),
        true, false, 430);

    add(QStringLiteral("pre"), QStringLiteral("Preformatted"), QStringLiteral("预格式"),
        QStringLiteral("Blocks"), QStringLiteral("块级"),
        QStringLiteral("Preformatted block that preserves whitespace."),
        QStringLiteral("保留空白字符的预格式化块。"),
        QStringLiteral("text-pre"), ElementCategory::Text,
        QStringLiteral("tcbw export --mode single-html --out dist/index.html"),
        QStringLiteral("tcbw export --mode single-html --out dist/index.html"),
        attrs({}), attrs({{QStringLiteral("font-family"), QStringLiteral("ui-monospace, SFMono-Regular, Consolas, monospace")},
                          {QStringLiteral("background"), QStringLiteral("#0f172a")},
                          {QStringLiteral("color"), QStringLiteral("#e2e8f0")},
                          {QStringLiteral("padding"), QStringLiteral("16px")},
                          {QStringLiteral("border-radius"), QStringLiteral("10px")},
                          {QStringLiteral("overflow"), QStringLiteral("auto")}}),
        true, false, 440);

    add(QStringLiteral("blockquote"), QStringLiteral("Quote"), QStringLiteral("引用"),
        QStringLiteral("Blocks"), QStringLiteral("块级"),
        QStringLiteral("Extended quotation from another source."),
        QStringLiteral("来自其他来源的长篇引文。"),
        QStringLiteral("text-quote"), ElementCategory::Text,
        QStringLiteral("The best way to predict the future is to build it."),
        QStringLiteral("预测未来最好的方式就是把它构建出来。"),
        attrs({}), attrs({{QStringLiteral("border-left"), QStringLiteral("4px solid #93c5fd")},
                          {QStringLiteral("padding"), QStringLiteral("8px 16px")},
                          {QStringLiteral("margin"), QStringLiteral("0 0 16px")},
                          {QStringLiteral("font-style"), QStringLiteral("italic")}}),
        true, false, 450);

    add(QStringLiteral("cite"), QStringLiteral("Citation"), QStringLiteral("出处"),
        QStringLiteral("Inline"), QStringLiteral("行内"),
        QStringLiteral("Title of the work being quoted."),
        QStringLiteral("被引用作品的标题。"),
        QStringLiteral("text-cite"), ElementCategory::Text,
        QStringLiteral("Design Systems Handbook"),
        QStringLiteral("设计系统手册"),
        attrs({}), attrs({{QStringLiteral("font-style"), QStringLiteral("italic")}}),
        true, false, 460);

    add(QStringLiteral("abbr"), QStringLiteral("Abbreviation"), QStringLiteral("缩写"),
        QStringLiteral("Inline"), QStringLiteral("行内"),
        QStringLiteral("Abbreviation or acronym with an expansion."),
        QStringLiteral("带全称说明的缩写或首字母缩写。"),
        QStringLiteral("text-abbr"), ElementCategory::Text,
        QStringLiteral("HTML"),
        QStringLiteral("HTML"),
        attrs({{QStringLiteral("title"), QStringLiteral("HyperText Markup Language")}}),
        attrs({{QStringLiteral("text-decoration"), QStringLiteral("underline dotted")}}),
        true, false, 470);

    add(QStringLiteral("time"), QStringLiteral("Time"), QStringLiteral("时间"),
        QStringLiteral("Inline"), QStringLiteral("行内"),
        QStringLiteral("Machine-readable date or time."),
        QStringLiteral("机器可读的日期或时间。"),
        QStringLiteral("text-time"), ElementCategory::Text,
        QStringLiteral("11 October 2026"),
        QStringLiteral("2026 年 10 月 11 日"),
        attrs({{QStringLiteral("datetime"), QStringLiteral("2026-10-11")}}),
        attrs({}), true, false, 480);

    add(QStringLiteral("address"), QStringLiteral("Address"), QStringLiteral("联系地址"),
        QStringLiteral("Blocks"), QStringLiteral("块级"),
        QStringLiteral("Contact information for the nearest article."),
        QStringLiteral("最近一篇 article 的联系信息。"),
        QStringLiteral("text-address"), ElementCategory::Text,
        QStringLiteral("Turtle Inc. · support@turtlelnc.example"),
        QStringLiteral("Turtle Inc. · support@turtlelnc.example"),
        attrs({}), attrs({{QStringLiteral("font-style"), QStringLiteral("normal")},
                          {QStringLiteral("font-size"), QStringLiteral("0.875rem")}}),
        true, false, 490);

    add(QStringLiteral("kbd"), QStringLiteral("Keyboard key"), QStringLiteral("按键"),
        QStringLiteral("Inline"), QStringLiteral("行内"),
        QStringLiteral("User input from a keyboard."),
        QStringLiteral("来自键盘的用户输入。"),
        QStringLiteral("text-kbd"), ElementCategory::Text,
        QStringLiteral("Ctrl"),
        QStringLiteral("Ctrl"),
        attrs({}), attrs({{QStringLiteral("font-family"), QStringLiteral("ui-monospace, monospace")},
                          {QStringLiteral("border"), QStringLiteral("1px solid #cbd5e1")},
                          {QStringLiteral("border-radius"), QStringLiteral("4px")},
                          {QStringLiteral("padding"), QStringLiteral("1px 5px")}}),
        true, false, 500);

    add(QStringLiteral("samp"), QStringLiteral("Sample output"), QStringLiteral("示例输出"),
        QStringLiteral("Inline"), QStringLiteral("行内"),
        QStringLiteral("Sample output from a program."),
        QStringLiteral("程序的示例输出。"),
        QStringLiteral("text-samp"), ElementCategory::Text,
        QStringLiteral("Build finished in 2.4s"),
        QStringLiteral("构建完成，用时 2.4 秒"),
        attrs({}), attrs({{QStringLiteral("font-family"), QStringLiteral("ui-monospace, monospace")}}),
        true, false, 510);

    add(QStringLiteral("var"), QStringLiteral("Variable"), QStringLiteral("变量"),
        QStringLiteral("Inline"), QStringLiteral("行内"),
        QStringLiteral("Mathematical or programming variable name."),
        QStringLiteral("数学或编程中的变量名。"),
        QStringLiteral("text-var"), ElementCategory::Text,
        QStringLiteral("gridSize"),
        QStringLiteral("gridSize"),
        attrs({}), attrs({{QStringLiteral("font-style"), QStringLiteral("italic")}}),
        true, false, 520);

    add(QStringLiteral("sub"), QStringLiteral("Subscript"), QStringLiteral("下标"),
        QStringLiteral("Inline"), QStringLiteral("行内"),
        QStringLiteral("Inline text lowered below the baseline."),
        QStringLiteral("相对基线降低的行内文本。"),
        QStringLiteral("text-sub"), ElementCategory::Text,
        QStringLiteral("2"),
        QStringLiteral("2"),
        attrs({}), attrs({{QStringLiteral("vertical-align"), QStringLiteral("sub")},
                          {QStringLiteral("font-size"), QStringLiteral("0.75em")}}),
        true, false, 530);

    add(QStringLiteral("sup"), QStringLiteral("Superscript"), QStringLiteral("上标"),
        QStringLiteral("Inline"), QStringLiteral("行内"),
        QStringLiteral("Inline text raised above the baseline."),
        QStringLiteral("相对基线升高的行内文本。"),
        QStringLiteral("text-sup"), ElementCategory::Text,
        QStringLiteral("2"),
        QStringLiteral("2"),
        attrs({}), attrs({{QStringLiteral("vertical-align"), QStringLiteral("super")},
                          {QStringLiteral("font-size"), QStringLiteral("0.75em")}}),
        true, false, 540);

    add(QStringLiteral("ins"), QStringLiteral("Inserted"), QStringLiteral("插入"),
        QStringLiteral("Inline"), QStringLiteral("行内"),
        QStringLiteral("Text added to the document."),
        QStringLiteral("文档中新增的文本。"),
        QStringLiteral("text-ins"), ElementCategory::Text,
        QStringLiteral("new paragraph"),
        QStringLiteral("新增段落"),
        attrs({}), attrs({{QStringLiteral("text-decoration"), QStringLiteral("underline")},
                          {QStringLiteral("background"), QStringLiteral("#dcfce7")}}),
        true, false, 550);

    add(QStringLiteral("del"), QStringLiteral("Deleted"), QStringLiteral("删除"),
        QStringLiteral("Inline"), QStringLiteral("行内"),
        QStringLiteral("Text removed from the document."),
        QStringLiteral("文档中已删除的文本。"),
        QStringLiteral("text-del"), ElementCategory::Text,
        QStringLiteral("old paragraph"),
        QStringLiteral("旧段落"),
        attrs({}), attrs({{QStringLiteral("text-decoration"), QStringLiteral("line-through")},
                          {QStringLiteral("opacity"), QStringLiteral("0.7")}}),
        true, false, 560);

    // -----------------------------------------------------------------------
    //  Media / 媒体
    // -----------------------------------------------------------------------
    add(QStringLiteral("img"), QStringLiteral("Image"), QStringLiteral("图片"),
        QStringLiteral("Graphics"), QStringLiteral("图形"),
        QStringLiteral("Embeds a raster or vector image."),
        QStringLiteral("嵌入位图或矢量图像。"),
        QStringLiteral("media-image"), ElementCategory::Media, empty, empty,
        attrs({{QStringLiteral("src"), QStringLiteral("assets/placeholder.svg")},
               {QStringLiteral("alt"), QStringLiteral("Placeholder image")}}),
        attrs({{QStringLiteral("display"), QStringLiteral("block")},
               {QStringLiteral("width"), QStringLiteral("100%")},
               {QStringLiteral("height"), QStringLiteral("auto")},
               {QStringLiteral("border-radius"), QStringLiteral("10px")}}),
        false, true, 600);

    add(QStringLiteral("picture"), QStringLiteral("Picture"), QStringLiteral("响应式图片"),
        QStringLiteral("Graphics"), QStringLiteral("图形"),
        QStringLiteral("Container offering alternative image sources."),
        QStringLiteral("提供多套候选图片源的容器。"),
        QStringLiteral("media-picture"), ElementCategory::Media, empty, empty,
        attrs({}), attrs({{QStringLiteral("display"), QStringLiteral("block")}}),
        true, false, 610);

    add(QStringLiteral("source"), QStringLiteral("Source"), QStringLiteral("媒体源"),
        QStringLiteral("Graphics"), QStringLiteral("图形"),
        QStringLiteral("Candidate source for picture, video or audio."),
        QStringLiteral("picture、video 或 audio 的候选源。"),
        QStringLiteral("media-source"), ElementCategory::Media, empty, empty,
        attrs({{QStringLiteral("srcset"), QStringLiteral("assets/placeholder.svg")},
               {QStringLiteral("type"), QStringLiteral("image/svg+xml")}}),
        attrs({}), false, true, 620);

    add(QStringLiteral("video"), QStringLiteral("Video"), QStringLiteral("视频"),
        QStringLiteral("Playback"), QStringLiteral("播放"),
        QStringLiteral("Video player with optional controls."),
        QStringLiteral("可带控制条的视频播放器。"),
        QStringLiteral("media-video"), ElementCategory::Media, empty, empty,
        attrs({{QStringLiteral("src"), QStringLiteral("assets/demo.mp4")},
               {QStringLiteral("controls"), QString()},
               {QStringLiteral("poster"), QStringLiteral("assets/placeholder.svg")}}),
        attrs({{QStringLiteral("display"), QStringLiteral("block")},
               {QStringLiteral("width"), QStringLiteral("100%")},
               {QStringLiteral("border-radius"), QStringLiteral("10px")}}),
        true, false, 630);

    add(QStringLiteral("audio"), QStringLiteral("Audio"), QStringLiteral("音频"),
        QStringLiteral("Playback"), QStringLiteral("播放"),
        QStringLiteral("Audio player with optional controls."),
        QStringLiteral("可带控制条的音频播放器。"),
        QStringLiteral("media-audio"), ElementCategory::Media, empty, empty,
        attrs({{QStringLiteral("src"), QStringLiteral("assets/demo.mp3")},
               {QStringLiteral("controls"), QString()}}),
        attrs({{QStringLiteral("display"), QStringLiteral("block")},
               {QStringLiteral("width"), QStringLiteral("100%")}}),
        true, false, 640);

    add(QStringLiteral("track"), QStringLiteral("Track"), QStringLiteral("字幕轨"),
        QStringLiteral("Playback"), QStringLiteral("播放"),
        QStringLiteral("Timed text track such as subtitles."),
        QStringLiteral("字幕等定时文本轨道。"),
        QStringLiteral("media-track"), ElementCategory::Media, empty, empty,
        attrs({{QStringLiteral("kind"), QStringLiteral("subtitles")},
               {QStringLiteral("src"), QStringLiteral("assets/subtitles.vtt")},
               {QStringLiteral("srclang"), QStringLiteral("en")}}),
        attrs({}), false, true, 650);

    add(QStringLiteral("iframe"), QStringLiteral("Embedded frame"), QStringLiteral("内嵌框架"),
        QStringLiteral("Embedding"), QStringLiteral("嵌入"),
        QStringLiteral("Nested browsing context for external content."),
        QStringLiteral("用于承载外部内容的嵌套浏览上下文。"),
        QStringLiteral("media-iframe"), ElementCategory::Media, empty, empty,
        attrs({{QStringLiteral("src"), QStringLiteral("https://example.com/embed")},
               {QStringLiteral("title"), QStringLiteral("Embedded content")},
               {QStringLiteral("loading"), QStringLiteral("lazy")}}),
        attrs({{QStringLiteral("width"), QStringLiteral("100%")},
               {QStringLiteral("height"), QStringLiteral("360px")},
               {QStringLiteral("border"), QStringLiteral("0")},
               {QStringLiteral("border-radius"), QStringLiteral("10px")}}),
        true, false, 660);

    add(QStringLiteral("svg"), QStringLiteral("SVG"), QStringLiteral("矢量图"),
        QStringLiteral("Graphics"), QStringLiteral("图形"),
        QStringLiteral("Inline Scalable Vector Graphics canvas."),
        QStringLiteral("内联的可缩放矢量图形画布。"),
        QStringLiteral("media-svg"), ElementCategory::Media, empty, empty,
        attrs({{QStringLiteral("viewBox"), QStringLiteral("0 0 24 24")},
               {QStringLiteral("width"), QStringLiteral("48")},
               {QStringLiteral("height"), QStringLiteral("48")}}),
        attrs({{QStringLiteral("display"), QStringLiteral("inline-block")}}),
        true, false, 670);

    add(QStringLiteral("canvas"), QStringLiteral("Canvas"), QStringLiteral("画布"),
        QStringLiteral("Graphics"), QStringLiteral("图形"),
        QStringLiteral("Bitmap drawing surface driven by script."),
        QStringLiteral("由脚本驱动的位图绘制表面。"),
        QStringLiteral("media-canvas"), ElementCategory::Media, empty, empty,
        attrs({{QStringLiteral("width"), QStringLiteral("640")},
               {QStringLiteral("height"), QStringLiteral("360")}}),
        attrs({{QStringLiteral("display"), QStringLiteral("block")},
               {QStringLiteral("border"), QStringLiteral("1px dashed #94a3b8")},
               {QStringLiteral("border-radius"), QStringLiteral("10px")}}),
        true, false, 680);

    add(QStringLiteral("object"), QStringLiteral("Object"), QStringLiteral("对象"),
        QStringLiteral("Embedding"), QStringLiteral("嵌入"),
        QStringLiteral("External resource treated as an image or document."),
        QStringLiteral("作为图像或文档处理的外部资源。"),
        QStringLiteral("media-object"), ElementCategory::Media, empty, empty,
        attrs({{QStringLiteral("data"), QStringLiteral("assets/demo.pdf")},
               {QStringLiteral("type"), QStringLiteral("application/pdf")}}),
        attrs({{QStringLiteral("width"), QStringLiteral("100%")},
               {QStringLiteral("height"), QStringLiteral("420px")}}),
        true, false, 690);

    add(QStringLiteral("embed"), QStringLiteral("Embed"), QStringLiteral("嵌入"),
        QStringLiteral("Embedding"), QStringLiteral("嵌入"),
        QStringLiteral("Embeds external content handled by a plug-in."),
        QStringLiteral("嵌入由插件处理的外部内容。"),
        QStringLiteral("media-embed"), ElementCategory::Media, empty, empty,
        attrs({{QStringLiteral("src"), QStringLiteral("assets/demo.pdf")},
               {QStringLiteral("type"), QStringLiteral("application/pdf")}}),
        attrs({{QStringLiteral("width"), QStringLiteral("100%")},
               {QStringLiteral("height"), QStringLiteral("420px")}}),
        false, true, 700);

    add(QStringLiteral("map"), QStringLiteral("Image map"), QStringLiteral("图像映射"),
        QStringLiteral("Graphics"), QStringLiteral("图形"),
        QStringLiteral("Clickable region map for an image."),
        QStringLiteral("为图片定义可点击区域的映射。"),
        QStringLiteral("media-map"), ElementCategory::Media, empty, empty,
        attrs({{QStringLiteral("name"), QStringLiteral("image-map")}}),
        attrs({}), true, false, 710);

    // -----------------------------------------------------------------------
    //  Form / 表单
    // -----------------------------------------------------------------------
    add(QStringLiteral("form"), QStringLiteral("Form"), QStringLiteral("表单"),
        QStringLiteral("Structure"), QStringLiteral("结构"),
        QStringLiteral("Collects user input and submits it."),
        QStringLiteral("收集用户输入并提交。"),
        QStringLiteral("form-form"), ElementCategory::Form, empty, empty,
        attrs({{QStringLiteral("action"), QStringLiteral("#")},
               {QStringLiteral("method"), QStringLiteral("post")}}),
        attrs({{QStringLiteral("display"), QStringLiteral("flex")},
               {QStringLiteral("flex-direction"), QStringLiteral("column")},
               {QStringLiteral("gap"), QStringLiteral("14px")},
               {QStringLiteral("max-width"), QStringLiteral("520px")}}),
        true, false, 800);

    add(QStringLiteral("label"), QStringLiteral("Label"), QStringLiteral("标签"),
        QStringLiteral("Structure"), QStringLiteral("结构"),
        QStringLiteral("Caption for a form control."),
        QStringLiteral("表单控件的说明标签。"),
        QStringLiteral("form-label"), ElementCategory::Form,
        QStringLiteral("Email address"),
        QStringLiteral("电子邮箱"),
        attrs({{QStringLiteral("for"), QStringLiteral("field-id")}}),
        attrs({{QStringLiteral("font-size"), QStringLiteral("0.875rem")},
               {QStringLiteral("font-weight"), QStringLiteral("600")}}),
        true, false, 810);

    add(QStringLiteral("input"), QStringLiteral("Text input"), QStringLiteral("单行输入"),
        QStringLiteral("Controls"), QStringLiteral("控件"),
        QStringLiteral("Single-line input; the type attribute picks the variant."),
        QStringLiteral("单行输入框，type 属性决定具体类型。"),
        QStringLiteral("form-input"), ElementCategory::Form, empty, empty,
        attrs({{QStringLiteral("type"), QStringLiteral("text")},
               {QStringLiteral("name"), QStringLiteral("field")},
               {QStringLiteral("placeholder"), QStringLiteral("you@example.com")}}),
        attrs({{QStringLiteral("padding"), QStringLiteral("10px 12px")},
               {QStringLiteral("border"), QStringLiteral("1px solid #cbd5e1")},
               {QStringLiteral("border-radius"), QStringLiteral("8px")},
               {QStringLiteral("font-size"), QStringLiteral("1rem")},
               {QStringLiteral("width"), QStringLiteral("100%")}}),
        false, true, 820);

    add(QStringLiteral("textarea"), QStringLiteral("Text area"), QStringLiteral("多行输入"),
        QStringLiteral("Controls"), QStringLiteral("控件"),
        QStringLiteral("Multi-line plain-text input."),
        QStringLiteral("多行纯文本输入框。"),
        QStringLiteral("form-textarea"), ElementCategory::Form, empty, empty,
        attrs({{QStringLiteral("name"), QStringLiteral("message")},
               {QStringLiteral("rows"), QStringLiteral("5")},
               {QStringLiteral("placeholder"), QStringLiteral("Tell us about your project")}}),
        attrs({{QStringLiteral("padding"), QStringLiteral("10px 12px")},
               {QStringLiteral("border"), QStringLiteral("1px solid #cbd5e1")},
               {QStringLiteral("border-radius"), QStringLiteral("8px")},
               {QStringLiteral("font-size"), QStringLiteral("1rem")},
               {QStringLiteral("width"), QStringLiteral("100%")},
               {QStringLiteral("resize"), QStringLiteral("vertical")}}),
        true, false, 830);

    add(QStringLiteral("select"), QStringLiteral("Select"), QStringLiteral("下拉选择"),
        QStringLiteral("Controls"), QStringLiteral("控件"),
        QStringLiteral("Drop-down list of options."),
        QStringLiteral("下拉选项列表。"),
        QStringLiteral("form-select"), ElementCategory::Form, empty, empty,
        attrs({{QStringLiteral("name"), QStringLiteral("choice")}}),
        attrs({{QStringLiteral("padding"), QStringLiteral("10px 12px")},
               {QStringLiteral("border"), QStringLiteral("1px solid #cbd5e1")},
               {QStringLiteral("border-radius"), QStringLiteral("8px")},
               {QStringLiteral("font-size"), QStringLiteral("1rem")}}),
        true, false, 840);

    add(QStringLiteral("option"), QStringLiteral("Option"), QStringLiteral("选项"),
        QStringLiteral("Controls"), QStringLiteral("控件"),
        QStringLiteral("One entry inside a select or datalist."),
        QStringLiteral("select 或 datalist 中的一个条目。"),
        QStringLiteral("form-option"), ElementCategory::Form,
        QStringLiteral("Standard plan"),
        QStringLiteral("标准方案"),
        attrs({{QStringLiteral("value"), QStringLiteral("standard")}}),
        attrs({}), true, false, 850);

    add(QStringLiteral("optgroup"), QStringLiteral("Option group"), QStringLiteral("选项组"),
        QStringLiteral("Controls"), QStringLiteral("控件"),
        QStringLiteral("Groups related options inside a select."),
        QStringLiteral("对 select 中相关选项进行分组。"),
        QStringLiteral("form-optgroup"), ElementCategory::Form, empty, empty,
        attrs({{QStringLiteral("label"), QStringLiteral("Plans")}}),
        attrs({}), true, false, 860);

    add(QStringLiteral("button"), QStringLiteral("Button"), QStringLiteral("按钮"),
        QStringLiteral("Controls"), QStringLiteral("控件"),
        QStringLiteral("Clickable button; a block script can react to the click."),
        QStringLiteral("可点击按钮，积木脚本可以响应点击。"),
        QStringLiteral("form-button"), ElementCategory::Form,
        QStringLiteral("Get started"),
        QStringLiteral("立即开始"),
        attrs({{QStringLiteral("type"), QStringLiteral("button")}}),
        attrs({{QStringLiteral("padding"), QStringLiteral("10px 20px")},
               {QStringLiteral("border"), QStringLiteral("0")},
               {QStringLiteral("border-radius"), QStringLiteral("8px")},
               {QStringLiteral("background"), QStringLiteral("#2563eb")},
               {QStringLiteral("color"), QStringLiteral("#ffffff")},
               {QStringLiteral("font-size"), QStringLiteral("1rem")},
               {QStringLiteral("font-weight"), QStringLiteral("600")},
               {QStringLiteral("cursor"), QStringLiteral("pointer")}}),
        true, false, 870);

    add(QStringLiteral("datalist"), QStringLiteral("Datalist"), QStringLiteral("数据列表"),
        QStringLiteral("Controls"), QStringLiteral("控件"),
        QStringLiteral("Suggested values for an input control."),
        QStringLiteral("为输入控件提供候选值。"),
        QStringLiteral("form-datalist"), ElementCategory::Form, empty, empty,
        attrs({{QStringLiteral("id"), QStringLiteral("suggestions")}}),
        attrs({}), true, false, 880);

    add(QStringLiteral("output"), QStringLiteral("Output"), QStringLiteral("输出"),
        QStringLiteral("Controls"), QStringLiteral("控件"),
        QStringLiteral("Result of a calculation or user action."),
        QStringLiteral("计算或用户操作的结果展示。"),
        QStringLiteral("form-output"), ElementCategory::Form,
        QStringLiteral("42"),
        QStringLiteral("42"),
        attrs({}), attrs({{QStringLiteral("font-weight"), QStringLiteral("600")}}),
        true, false, 890);

    add(QStringLiteral("progress"), QStringLiteral("Progress"), QStringLiteral("进度条"),
        QStringLiteral("Controls"), QStringLiteral("控件"),
        QStringLiteral("Completion progress of a task."),
        QStringLiteral("任务完成进度。"),
        QStringLiteral("form-progress"), ElementCategory::Form, empty, empty,
        attrs({{QStringLiteral("value"), QStringLiteral("60")},
               {QStringLiteral("max"), QStringLiteral("100")}}),
        attrs({{QStringLiteral("width"), QStringLiteral("100%")},
               {QStringLiteral("height"), QStringLiteral("10px")}}),
        true, false, 900);

    add(QStringLiteral("meter"), QStringLiteral("Meter"), QStringLiteral("度量"),
        QStringLiteral("Controls"), QStringLiteral("控件"),
        QStringLiteral("Scalar measurement inside a known range."),
        QStringLiteral("已知范围内的标量测量值。"),
        QStringLiteral("form-meter"), ElementCategory::Form, empty, empty,
        attrs({{QStringLiteral("value"), QStringLiteral("0.72")},
               {QStringLiteral("min"), QStringLiteral("0")},
               {QStringLiteral("max"), QStringLiteral("1")}}),
        attrs({{QStringLiteral("width"), QStringLiteral("180px")},
               {QStringLiteral("height"), QStringLiteral("10px")}}),
        true, false, 910);

    add(QStringLiteral("fieldset"), QStringLiteral("Field set"), QStringLiteral("字段组"),
        QStringLiteral("Structure"), QStringLiteral("结构"),
        QStringLiteral("Groups related form controls with a legend."),
        QStringLiteral("用图例把相关表单控件分组。"),
        QStringLiteral("form-fieldset"), ElementCategory::Form, empty, empty,
        attrs({}), attrs({{QStringLiteral("border"), QStringLiteral("1px solid #e2e8f0")},
                          {QStringLiteral("border-radius"), QStringLiteral("10px")},
                          {QStringLiteral("padding"), QStringLiteral("16px")}}),
        true, false, 920);

    add(QStringLiteral("legend"), QStringLiteral("Legend"), QStringLiteral("图例"),
        QStringLiteral("Structure"), QStringLiteral("结构"),
        QStringLiteral("Caption for the enclosing fieldset."),
        QStringLiteral("所属 fieldset 的标题。"),
        QStringLiteral("form-legend"), ElementCategory::Form,
        QStringLiteral("Billing details"),
        QStringLiteral("账单信息"),
        attrs({}), attrs({{QStringLiteral("font-weight"), QStringLiteral("600")},
                          {QStringLiteral("padding"), QStringLiteral("0 6px")}}),
        true, false, 930);

    // -----------------------------------------------------------------------
    //  Data / 数据
    // -----------------------------------------------------------------------
    add(QStringLiteral("table"), QStringLiteral("Table"), QStringLiteral("表格"),
        QStringLiteral("Tables"), QStringLiteral("表格"),
        QStringLiteral("Two-dimensional tabular data."),
        QStringLiteral("二维表格数据。"),
        QStringLiteral("data-table"), ElementCategory::Data, empty, empty,
        attrs({}), attrs({{QStringLiteral("width"), QStringLiteral("100%")},
                          {QStringLiteral("border-collapse"), QStringLiteral("collapse")},
                          {QStringLiteral("font-size"), QStringLiteral("0.9375rem")}}),
        true, false, 1000);

    add(QStringLiteral("caption"), QStringLiteral("Table caption"), QStringLiteral("表格标题"),
        QStringLiteral("Tables"), QStringLiteral("表格"),
        QStringLiteral("Title of the enclosing table."),
        QStringLiteral("所属表格的标题。"),
        QStringLiteral("data-caption"), ElementCategory::Data,
        QStringLiteral("Monthly usage"),
        QStringLiteral("每月用量"),
        attrs({}), attrs({{QStringLiteral("text-align"), QStringLiteral("left")},
                          {QStringLiteral("font-weight"), QStringLiteral("600")},
                          {QStringLiteral("padding-bottom"), QStringLiteral("8px")}}),
        true, false, 1010);

    add(QStringLiteral("thead"), QStringLiteral("Table head"), QStringLiteral("表头"),
        QStringLiteral("Tables"), QStringLiteral("表格"),
        QStringLiteral("Header row group of a table."),
        QStringLiteral("表格的表头行分组。"),
        QStringLiteral("data-thead"), ElementCategory::Data, empty, empty,
        attrs({}), attrs({}), true, false, 1020);

    add(QStringLiteral("tbody"), QStringLiteral("Table body"), QStringLiteral("表格主体"),
        QStringLiteral("Tables"), QStringLiteral("表格"),
        QStringLiteral("Body row group of a table."),
        QStringLiteral("表格的主体行分组。"),
        QStringLiteral("data-tbody"), ElementCategory::Data, empty, empty,
        attrs({}), attrs({}), true, false, 1030);

    add(QStringLiteral("tfoot"), QStringLiteral("Table foot"), QStringLiteral("表尾"),
        QStringLiteral("Tables"), QStringLiteral("表格"),
        QStringLiteral("Footer row group of a table."),
        QStringLiteral("表格的表尾行分组。"),
        QStringLiteral("data-tfoot"), ElementCategory::Data, empty, empty,
        attrs({}), attrs({}), true, false, 1040);

    add(QStringLiteral("tr"), QStringLiteral("Table row"), QStringLiteral("表格行"),
        QStringLiteral("Tables"), QStringLiteral("表格"),
        QStringLiteral("One row of a table."),
        QStringLiteral("表格中的一行。"),
        QStringLiteral("data-tr"), ElementCategory::Data, empty, empty,
        attrs({}), attrs({{QStringLiteral("border-bottom"), QStringLiteral("1px solid #e2e8f0")}}),
        true, false, 1050);

    add(QStringLiteral("td"), QStringLiteral("Table cell"), QStringLiteral("单元格"),
        QStringLiteral("Tables"), QStringLiteral("表格"),
        QStringLiteral("Standard data cell of a table row."),
        QStringLiteral("表格行中的普通数据单元格。"),
        QStringLiteral("data-td"), ElementCategory::Data,
        QStringLiteral("Cell value"),
        QStringLiteral("单元格内容"),
        attrs({}), attrs({{QStringLiteral("padding"), QStringLiteral("10px 12px")},
                          {QStringLiteral("text-align"), QStringLiteral("left")}}),
        true, false, 1060);

    add(QStringLiteral("th"), QStringLiteral("Header cell"), QStringLiteral("表头单元格"),
        QStringLiteral("Tables"), QStringLiteral("表格"),
        QStringLiteral("Header cell of a table row or column."),
        QStringLiteral("表格行或列的表头单元格。"),
        QStringLiteral("data-th"), ElementCategory::Data,
        QStringLiteral("Column"),
        QStringLiteral("列名"),
        attrs({{QStringLiteral("scope"), QStringLiteral("col")}}),
        attrs({{QStringLiteral("padding"), QStringLiteral("10px 12px")},
               {QStringLiteral("text-align"), QStringLiteral("left")},
               {QStringLiteral("background"), QStringLiteral("#f8fafc")},
               {QStringLiteral("font-weight"), QStringLiteral("600")}}),
        true, false, 1070);

    add(QStringLiteral("ul"), QStringLiteral("Bullet list"), QStringLiteral("无序列表"),
        QStringLiteral("Lists"), QStringLiteral("列表"),
        QStringLiteral("Unordered list of items."),
        QStringLiteral("无序的项目列表。"),
        QStringLiteral("data-ul"), ElementCategory::Data, empty, empty,
        attrs({}), attrs({{QStringLiteral("margin"), QStringLiteral("0 0 12px")},
                          {QStringLiteral("padding-left"), QStringLiteral("22px")},
                          {QStringLiteral("list-style"), QStringLiteral("disc")}}),
        true, false, 1080);

    add(QStringLiteral("ol"), QStringLiteral("Numbered list"), QStringLiteral("有序列表"),
        QStringLiteral("Lists"), QStringLiteral("列表"),
        QStringLiteral("Ordered list of items."),
        QStringLiteral("有序的项目列表。"),
        QStringLiteral("data-ol"), ElementCategory::Data, empty, empty,
        attrs({}), attrs({{QStringLiteral("margin"), QStringLiteral("0 0 12px")},
                          {QStringLiteral("padding-left"), QStringLiteral("22px")},
                          {QStringLiteral("list-style"), QStringLiteral("decimal")}}),
        true, false, 1090);

    add(QStringLiteral("li"), QStringLiteral("List item"), QStringLiteral("列表项"),
        QStringLiteral("Lists"), QStringLiteral("列表"),
        QStringLiteral("One entry of a list or a menu."),
        QStringLiteral("列表或菜单中的一个条目。"),
        QStringLiteral("data-li"), ElementCategory::Data,
        QStringLiteral("List item"),
        QStringLiteral("列表项"),
        attrs({}), attrs({{QStringLiteral("margin-bottom"), QStringLiteral("4px")}}),
        true, false, 1100);

    add(QStringLiteral("dl"), QStringLiteral("Description list"), QStringLiteral("描述列表"),
        QStringLiteral("Lists"), QStringLiteral("列表"),
        QStringLiteral("List of term/description pairs."),
        QStringLiteral("术语与描述成对的列表。"),
        QStringLiteral("data-dl"), ElementCategory::Data, empty, empty,
        attrs({}), attrs({{QStringLiteral("margin"), QStringLiteral("0 0 12px")}}),
        true, false, 1110);

    add(QStringLiteral("dt"), QStringLiteral("Term"), QStringLiteral("术语"),
        QStringLiteral("Lists"), QStringLiteral("列表"),
        QStringLiteral("Term being described in a description list."),
        QStringLiteral("描述列表中作为词条的术语。"),
        QStringLiteral("data-dt"), ElementCategory::Data,
        QStringLiteral("Snap to grid"),
        QStringLiteral("对齐网格"),
        attrs({}), attrs({{QStringLiteral("font-weight"), QStringLiteral("600")}}),
        true, false, 1120);

    add(QStringLiteral("dd"), QStringLiteral("Description"), QStringLiteral("描述"),
        QStringLiteral("Lists"), QStringLiteral("列表"),
        QStringLiteral("Description of the preceding term."),
        QStringLiteral("对前一个术语的描述。"),
        QStringLiteral("data-dd"), ElementCategory::Data,
        QStringLiteral("Aligns dragged elements to the nearest grid intersection."),
        QStringLiteral("把拖拽的元素对齐到最近的网格交叉点。"),
        attrs({}), attrs({{QStringLiteral("margin"), QStringLiteral("0 0 8px 20px")},
                          {QStringLiteral("opacity"), QStringLiteral("0.85")}}),
        true, false, 1130);

    add(QStringLiteral("menu"), QStringLiteral("Menu list"), QStringLiteral("菜单列表"),
        QStringLiteral("Lists"), QStringLiteral("列表"),
        QStringLiteral("Semantic list of commands."),
        QStringLiteral("语义化的命令列表。"),
        QStringLiteral("data-menu"), ElementCategory::Data, empty, empty,
        attrs({}), attrs({{QStringLiteral("list-style"), QStringLiteral("none")},
                          {QStringLiteral("padding"), QStringLiteral("0")},
                          {QStringLiteral("margin"), QStringLiteral("0")}}),
        true, false, 1140);

    add(QStringLiteral("template"), QStringLiteral("Template"), QStringLiteral("模板"),
        QStringLiteral("Lists"), QStringLiteral("列表"),
        QStringLiteral("Inert markup cloned by script at runtime."),
        QStringLiteral("由脚本在运行时克隆的惰性标记。"),
        QStringLiteral("data-template"), ElementCategory::Data, empty, empty,
        attrs({}), attrs({}), true, false, 1150);

    // -----------------------------------------------------------------------
    //  Navigation / 导航
    // -----------------------------------------------------------------------
    add(QStringLiteral("nav"), QStringLiteral("Navigation"), QStringLiteral("导航"),
        QStringLiteral("Navigation"), QStringLiteral("导航"),
        QStringLiteral("Block of major navigation links."),
        QStringLiteral("主要导航链接组成的区块。"),
        QStringLiteral("nav-nav"), ElementCategory::Navigation, empty, empty,
        attrs({}), attrs({{QStringLiteral("display"), QStringLiteral("flex")},
                          {QStringLiteral("gap"), QStringLiteral("18px")},
                          {QStringLiteral("align-items"), QStringLiteral("center")}}),
        true, false, 1200);

    add(QStringLiteral("breadcrumb"), QStringLiteral("Breadcrumb"), QStringLiteral("面包屑"),
        QStringLiteral("Navigation"), QStringLiteral("导航"),
        QStringLiteral("Trail showing the current position in the hierarchy."),
        QStringLiteral("显示当前层级位置的面包屑导航。"),
        QStringLiteral("nav-breadcrumb"), ElementCategory::Navigation, empty, empty,
        attrs({{QStringLiteral("class"), QStringLiteral("tcbw-breadcrumb")},
               {QStringLiteral("aria-label"), QStringLiteral("Breadcrumb")}}),
        attrs({{QStringLiteral("display"), QStringLiteral("flex")},
               {QStringLiteral("gap"), QStringLiteral("8px")},
               {QStringLiteral("font-size"), QStringLiteral("0.875rem")}}),
        true, false, 1210);

    add(QStringLiteral("pagination"), QStringLiteral("Pagination"), QStringLiteral("分页"),
        QStringLiteral("Navigation"), QStringLiteral("导航"),
        QStringLiteral("Page number controls for a long result set."),
        QStringLiteral("用于长结果集的页码控件。"),
        QStringLiteral("nav-pagination"), ElementCategory::Navigation, empty, empty,
        attrs({{QStringLiteral("class"), QStringLiteral("tcbw-pagination")},
               {QStringLiteral("aria-label"), QStringLiteral("Pagination")}}),
        attrs({{QStringLiteral("display"), QStringLiteral("flex")},
               {QStringLiteral("gap"), QStringLiteral("6px")},
               {QStringLiteral("justify-content"), QStringLiteral("center")}}),
        true, false, 1220);

    add(QStringLiteral("menu-bar"), QStringLiteral("Menu bar"), QStringLiteral("菜单栏"),
        QStringLiteral("Navigation"), QStringLiteral("导航"),
        QStringLiteral("Horizontal bar of drop-down commands."),
        QStringLiteral("由下拉命令组成的水平菜单栏。"),
        QStringLiteral("nav-menu-bar"), ElementCategory::Navigation, empty, empty,
        attrs({{QStringLiteral("class"), QStringLiteral("tcbw-menu-bar")}}),
        attrs({{QStringLiteral("display"), QStringLiteral("flex")},
               {QStringLiteral("align-items"), QStringLiteral("center")},
               {QStringLiteral("gap"), QStringLiteral("6px")},
               {QStringLiteral("padding"), QStringLiteral("8px 12px")},
               {QStringLiteral("background"), QStringLiteral("#0f172a")},
               {QStringLiteral("color"), QStringLiteral("#f8fafc")},
               {QStringLiteral("border-radius"), QStringLiteral("10px")}}),
        true, false, 1230);

    // -----------------------------------------------------------------------
    //  Advanced / 高级
    // -----------------------------------------------------------------------
    add(QStringLiteral("script"), QStringLiteral("Script"), QStringLiteral("脚本"),
        QStringLiteral("Document"), QStringLiteral("文档"),
        QStringLiteral("Embeds or references executable JavaScript."),
        QStringLiteral("嵌入或引用可执行的 JavaScript。"),
        QStringLiteral("adv-script"), ElementCategory::Advanced,
        QStringLiteral("console.log('Hello from TCTools-Build_Web');"),
        QStringLiteral("console.log('你好，来自 TCTools-Build_Web');"),
        attrs({{QStringLiteral("defer"), QString()}}),
        attrs({}), true, false, 1300);

    add(QStringLiteral("style"), QStringLiteral("Style block"), QStringLiteral("样式块"),
        QStringLiteral("Document"), QStringLiteral("文档"),
        QStringLiteral("Embedded CSS rules for the document."),
        QStringLiteral("文档内嵌的 CSS 规则。"),
        QStringLiteral("adv-style"), ElementCategory::Advanced,
        QStringLiteral(".hero { display: grid; place-items: center; }"),
        QStringLiteral(".hero { display: grid; place-items: center; }"),
        attrs({{QStringLiteral("media"), QStringLiteral("all")}}),
        attrs({}), true, false, 1310);

    add(QStringLiteral("link"), QStringLiteral("Link tag"), QStringLiteral("链接标签"),
        QStringLiteral("Document"), QStringLiteral("文档"),
        QStringLiteral("External stylesheet, icon or preload hint."),
        QStringLiteral("外部样式表、图标或预加载提示。"),
        QStringLiteral("adv-link"), ElementCategory::Advanced, empty, empty,
        attrs({{QStringLiteral("rel"), QStringLiteral("stylesheet")},
               {QStringLiteral("href"), QStringLiteral("styles/site.css")}}),
        attrs({}), false, true, 1320);

    add(QStringLiteral("meta"), QStringLiteral("Meta tag"), QStringLiteral("元信息"),
        QStringLiteral("Document"), QStringLiteral("文档"),
        QStringLiteral("Document metadata such as description or viewport."),
        QStringLiteral("描述、视口等文档元信息。"),
        QStringLiteral("adv-meta"), ElementCategory::Advanced, empty, empty,
        attrs({{QStringLiteral("name"), QStringLiteral("description")},
               {QStringLiteral("content"), QStringLiteral("Built with TCTools-Build_Web")}}),
        attrs({}), false, true, 1330);

    add(QStringLiteral("base"), QStringLiteral("Base URL"), QStringLiteral("基准地址"),
        QStringLiteral("Document"), QStringLiteral("文档"),
        QStringLiteral("Base URL for every relative link in the document."),
        QStringLiteral("文档中所有相对链接的基准地址。"),
        QStringLiteral("adv-base"), ElementCategory::Advanced, empty, empty,
        attrs({{QStringLiteral("href"), QStringLiteral("/")},
               {QStringLiteral("target"), QStringLiteral("_self")}}),
        attrs({}), false, true, 1340);

    add(QStringLiteral("noscript"), QStringLiteral("No-script"), QStringLiteral("无脚本回退"),
        QStringLiteral("Document"), QStringLiteral("文档"),
        QStringLiteral("Fallback markup shown when scripting is disabled."),
        QStringLiteral("禁用脚本时显示的降级内容。"),
        QStringLiteral("adv-noscript"), ElementCategory::Advanced, empty, empty,
        attrs({}), attrs({{QStringLiteral("display"), QStringLiteral("block")},
                          {QStringLiteral("padding"), QStringLiteral("12px")},
                          {QStringLiteral("background"), QStringLiteral("#fef9c3")}}),
        true, false, 1350);

    add(QStringLiteral("slot"), QStringLiteral("Slot"), QStringLiteral("插槽"),
        QStringLiteral("Web components"), QStringLiteral("Web 组件"),
        QStringLiteral("Placeholder filled by a web component's host."),
        QStringLiteral("由 Web 组件宿主填充的占位插槽。"),
        QStringLiteral("adv-slot"), ElementCategory::Advanced, empty, empty,
        attrs({{QStringLiteral("name"), QStringLiteral("content")}}),
        attrs({}), true, false, 1360);

    //  Deterministic palette order: sort by the explicit `order` field.
    //  面板顺序确定：按显式的 order 字段排序。
    std::stable_sort(m_elements.begin(), m_elements.end(),
                     [](const ElementDef &a, const ElementDef &b) {
                         if (a.order != b.order)
                             return a.order < b.order;
                         return a.tag < b.tag;
                     });

    //  Rebuild the lookup after sorting.
    //  排序后重建查表。
    m_byTag.clear();
    for (int i = 0; i < m_elements.size(); ++i)
        m_byTag.insert(m_elements.at(i).tag, i);
}

// ---------------------------------------------------------------------------
//  The composite (ready-made widget) table
//  组件（预制控件）表
// ---------------------------------------------------------------------------
void ElementCatalog::registerComposites()
{
    m_composites.clear();
    m_compositeById.clear();

    auto add = [this](const QString &id, const QString &labelEn, const QString &labelZh,
                      const QString &groupEn, const QString &groupZh,
                      const QString &descEn, const QString &descZh,
                      const QString &icon, int order) {
        CompositeDef def;
        def.id = id;
        def.labelEn = labelEn;
        def.labelZh = labelZh;
        def.groupEn = groupEn;
        def.groupZh = groupZh;
        def.descriptionEn = descEn;
        def.descriptionZh = descZh;
        def.icon = icon;
        def.order = order;
        m_compositeById.insert(id, static_cast<int>(m_composites.size()));
        m_composites.append(def);
    };

    // --- Headers and navigation / 页头与导航 --------------------------------
    add(QStringLiteral("navbar-simple"), QStringLiteral("Simple navbar"), QStringLiteral("简洁导航栏"),
        QStringLiteral("Headers"), QStringLiteral("页头"),
        QStringLiteral("Brand on the left, four links and a button on the right."),
        QStringLiteral("左侧品牌标识，右侧四个链接与一个按钮。"),
        QStringLiteral("widget-navbar"), 100);

    add(QStringLiteral("navbar-mega"), QStringLiteral("Mega menu navbar"), QStringLiteral("多列导航栏"),
        QStringLiteral("Headers"), QStringLiteral("页头"),
        QStringLiteral("Brand, grouped links and a search field."),
        QStringLiteral("品牌标识、分组链接与搜索框。"),
        QStringLiteral("widget-navbar-mega"), 110);

    add(QStringLiteral("breadcrumb-bar"), QStringLiteral("Breadcrumb bar"), QStringLiteral("面包屑栏"),
        QStringLiteral("Headers"), QStringLiteral("页头"),
        QStringLiteral("Home / Section / Current page trail."),
        QStringLiteral("首页 / 栏目 / 当前页 的路径导航。"),
        QStringLiteral("widget-breadcrumb"), 120);

    // --- Hero and calls to action / 主视觉与行动号召 -------------------------
    add(QStringLiteral("hero-center"), QStringLiteral("Centered hero"), QStringLiteral("居中主视觉"),
        QStringLiteral("Hero"), QStringLiteral("主视觉"),
        QStringLiteral("Headline, subtitle and two buttons, centred."),
        QStringLiteral("居中排列的标题、副标题与两个按钮。"),
        QStringLiteral("widget-hero-center"), 200);

    add(QStringLiteral("hero-split"), QStringLiteral("Split hero"), QStringLiteral("左右分栏主视觉"),
        QStringLiteral("Hero"), QStringLiteral("主视觉"),
        QStringLiteral("Copy on the left, illustration on the right."),
        QStringLiteral("左侧文案，右侧插图。"),
        QStringLiteral("widget-hero-split"), 210);

    add(QStringLiteral("cta-banner"), QStringLiteral("Call-to-action banner"), QStringLiteral("行动号召横幅"),
        QStringLiteral("Hero"), QStringLiteral("主视觉"),
        QStringLiteral("Full-width strip with one prominent button."),
        QStringLiteral("整宽横条，含一个醒目的按钮。"),
        QStringLiteral("widget-cta"), 220);

    add(QStringLiteral("coming-soon"), QStringLiteral("Coming soon"), QStringLiteral("敬请期待"),
        QStringLiteral("Hero"), QStringLiteral("主视觉"),
        QStringLiteral("Countdown-style launch page with an email field."),
        QStringLiteral("类似倒计时的上线预告页，带邮箱输入框。"),
        QStringLiteral("widget-coming-soon"), 230);

    add(QStringLiteral("error-404"), QStringLiteral("404 page"), QStringLiteral("404 页面"),
        QStringLiteral("Hero"), QStringLiteral("主视觉"),
        QStringLiteral("Friendly not-found page with a way back home."),
        QStringLiteral("友好的未找到页面，并提供返回首页的入口。"),
        QStringLiteral("widget-404"), 240);

    // --- Feature and social proof / 特性与社会证明 ---------------------------
    add(QStringLiteral("feature-grid-3"), QStringLiteral("Feature grid (3)"), QStringLiteral("特性网格（3 列）"),
        QStringLiteral("Content"), QStringLiteral("内容"),
        QStringLiteral("Three feature cards with icon, title and copy."),
        QStringLiteral("三张特性卡片，含图标、标题与说明。"),
        QStringLiteral("widget-features"), 300);

    add(QStringLiteral("stats-row"), QStringLiteral("Stats row"), QStringLiteral("数据行"),
        QStringLiteral("Content"), QStringLiteral("内容"),
        QStringLiteral("Four headline numbers with captions."),
        QStringLiteral("四个核心数字及其说明。"),
        QStringLiteral("widget-stats"), 310);

    add(QStringLiteral("testimonial"), QStringLiteral("Testimonial"), QStringLiteral("客户评价"),
        QStringLiteral("Content"), QStringLiteral("内容"),
        QStringLiteral("Customer quote with an avatar and name."),
        QStringLiteral("含头像与姓名的客户评价。"),
        QStringLiteral("widget-testimonial"), 320);

    add(QStringLiteral("team-grid"), QStringLiteral("Team grid"), QStringLiteral("团队网格"),
        QStringLiteral("Content"), QStringLiteral("内容"),
        QStringLiteral("Four team members with roles."),
        QStringLiteral("四位团队成员及其角色。"),
        QStringLiteral("widget-team"), 330);

    add(QStringLiteral("blog-list"), QStringLiteral("Blog list"), QStringLiteral("博客列表"),
        QStringLiteral("Content"), QStringLiteral("内容"),
        QStringLiteral("Three post summaries with dates."),
        QStringLiteral("三篇文章摘要，含发布日期。"),
        QStringLiteral("widget-blog"), 340);

    add(QStringLiteral("article-page"), QStringLiteral("Article page"), QStringLiteral("文章页"),
        QStringLiteral("Content"), QStringLiteral("内容"),
        QStringLiteral("Title, byline, cover image and body copy."),
        QStringLiteral("标题、署名、封面图与正文。"),
        QStringLiteral("widget-article"), 350);

    add(QStringLiteral("faq-accordion"), QStringLiteral("FAQ accordion"), QStringLiteral("常见问题折叠"),
        QStringLiteral("Content"), QStringLiteral("内容"),
        QStringLiteral("Four expandable questions and answers."),
        QStringLiteral("四个可展开的问题与答案。"),
        QStringLiteral("widget-faq"), 360);

    add(QStringLiteral("gallery-grid"), QStringLiteral("Image gallery"), QStringLiteral("图片画廊"),
        QStringLiteral("Content"), QStringLiteral("内容"),
        QStringLiteral("Six-image responsive gallery grid."),
        QStringLiteral("六张图片组成的响应式画廊网格。"),
        QStringLiteral("widget-gallery"), 370);

    // --- Commerce / 商业化 --------------------------------------------------
    add(QStringLiteral("pricing-3"), QStringLiteral("Pricing table (3)"), QStringLiteral("价格表（3 档）"),
        QStringLiteral("Commerce"), QStringLiteral("商业化"),
        QStringLiteral("Three plans, the middle one highlighted."),
        QStringLiteral("三档方案，中间一档突出显示。"),
        QStringLiteral("widget-pricing"), 400);

    // --- Forms / 表单 ------------------------------------------------------
    add(QStringLiteral("contact-form"), QStringLiteral("Contact form"), QStringLiteral("联系表单"),
        QStringLiteral("Forms"), QStringLiteral("表单"),
        QStringLiteral("Name, email, message and a submit button."),
        QStringLiteral("姓名、邮箱、留言与提交按钮。"),
        QStringLiteral("widget-contact"), 500);

    add(QStringLiteral("login-form"), QStringLiteral("Login form"), QStringLiteral("登录表单"),
        QStringLiteral("Forms"), QStringLiteral("表单"),
        QStringLiteral("Email, password and a sign-in button."),
        QStringLiteral("邮箱、密码与登录按钮。"),
        QStringLiteral("widget-login"), 510);

    add(QStringLiteral("signup-form"), QStringLiteral("Sign-up form"), QStringLiteral("注册表单"),
        QStringLiteral("Forms"), QStringLiteral("表单"),
        QStringLiteral("Name, email, password and a create-account button."),
        QStringLiteral("姓名、邮箱、密码与注册按钮。"),
        QStringLiteral("widget-signup"), 520);

    // --- Structure / 结构 ---------------------------------------------------
    add(QStringLiteral("sidebar-layout"), QStringLiteral("Sidebar layout"), QStringLiteral("侧边栏布局"),
        QStringLiteral("Structure"), QStringLiteral("结构"),
        QStringLiteral("Left navigation column beside a content column."),
        QStringLiteral("左侧导航栏与右侧内容栏并排。"),
        QStringLiteral("widget-sidebar"), 600);

    add(QStringLiteral("dashboard-shell"), QStringLiteral("Dashboard shell"), QStringLiteral("仪表盘框架"),
        QStringLiteral("Structure"), QStringLiteral("结构"),
        QStringLiteral("Top bar, side rail and four stat tiles."),
        QStringLiteral("顶栏、侧边栏与四个统计卡片。"),
        QStringLiteral("widget-dashboard"), 610);

    add(QStringLiteral("table-data"), QStringLiteral("Data table"), QStringLiteral("数据表格"),
        QStringLiteral("Structure"), QStringLiteral("结构"),
        QStringLiteral("Sortable-looking table with four sample rows."),
        QStringLiteral("带四条示例数据的表格。"),
        QStringLiteral("widget-table"), 620);

    // --- Footers and overlays / 页脚与浮层 -----------------------------------
    add(QStringLiteral("footer-simple"), QStringLiteral("Simple footer"), QStringLiteral("简洁页脚"),
        QStringLiteral("Footers"), QStringLiteral("页脚"),
        QStringLiteral("Copyright line with three link columns."),
        QStringLiteral("版权信息与三列链接。"),
        QStringLiteral("widget-footer"), 700);

    add(QStringLiteral("cookie-banner"), QStringLiteral("Cookie banner"), QStringLiteral("Cookie 提示条"),
        QStringLiteral("Overlays"), QStringLiteral("浮层"),
        QStringLiteral("Fixed consent bar with accept and decline buttons."),
        QStringLiteral("固定底部的同意条，含接受与拒绝按钮。"),
        QStringLiteral("widget-cookie"), 800);
}

// ===========================================================================
//  Lookup helpers
//  查询辅助
// ===========================================================================

const ElementDef *ElementCatalog::find(const QString &tag) const
{
    const QString key = tag.trimmed().toLower();
    const auto it = m_byTag.constFind(key);
    if (it == m_byTag.constEnd())
        return nullptr;
    const int index = it.value();
    if (index < 0 || index >= m_elements.size())
        return nullptr;
    return &m_elements.at(index);
}

QVector<const ElementDef *> ElementCatalog::byCategory(ElementCategory category) const
{
    QVector<const ElementDef *> out;
    for (const ElementDef &def : m_elements) {
        if (def.category == category)
            out.append(&def);
    }
    return out;
}

QVector<const ElementDef *> ElementCatalog::byGroup(ElementCategory category,
                                                   const QString &groupEn) const
{
    QVector<const ElementDef *> out;
    for (const ElementDef &def : m_elements) {
        if (def.category == category && def.groupEn == groupEn)
            out.append(&def);
    }
    return out;
}

QVector<ElementCategory> ElementCatalog::categories()
{
    //  Palette order follows the enum, which the UI renders top to bottom.
    //  面板顺序与枚举一致，界面自上而下渲染。
    return {ElementCategory::Layout, ElementCategory::Text, ElementCategory::Media,
            ElementCategory::Form, ElementCategory::Data, ElementCategory::Navigation,
            ElementCategory::Advanced, ElementCategory::Component,
            ElementCategory::Custom};
}

QVector<QString> ElementCatalog::groupsIn(ElementCategory category) const
{
    QVector<QString> out;
    for (const ElementDef &def : m_elements) {
        if (def.category != category)
            continue;
        if (!out.contains(def.groupEn))
            out.append(def.groupEn);
    }
    return out;
}

const CompositeDef *ElementCatalog::findComposite(const QString &id) const
{
    const auto it = m_compositeById.constFind(id);
    if (it == m_compositeById.constEnd())
        return nullptr;
    const int index = it.value();
    if (index < 0 || index >= m_composites.size())
        return nullptr;
    return &m_composites.at(index);
}

QVector<const CompositeDef *> ElementCatalog::compositesByGroup(const QString &groupEn) const
{
    QVector<const CompositeDef *> out;
    for (const CompositeDef &def : m_composites) {
        if (def.groupEn == groupEn)
            out.append(&def);
    }
    return out;
}

QVector<QString> ElementCatalog::compositeGroups() const
{
    QVector<QString> out;
    for (const CompositeDef &def : m_composites) {
        if (!out.contains(def.groupEn))
            out.append(def.groupEn);
    }
    return out;
}

// ===========================================================================
//  Node factories
//  节点工厂
// ===========================================================================

std::unique_ptr<DocumentNode> ElementCatalog::create(const QString &tag,
                                                     bool chineseDefaults) const
{
    const ElementDef *def = find(tag);
    if (!def)
        return nullptr;

    auto node = std::make_unique<DocumentNode>(def->tag, NodeKind::Element);

    //  Default copy is chosen by the caller's UI language.
    //  默认文案由调用方的界面语言决定。
    const QString demoText = chineseDefaults ? def->defaultTextZh : def->defaultTextEn;
    if (!demoText.isEmpty())
        node->text = demoText;

    for (auto it = def->defaultAttributes.constBegin();
         it != def->defaultAttributes.constEnd(); ++it) {
        if (it.value().isEmpty())
            node->attributes.insert(it.key(), QString());
        else
            node->setAttribute(it.key(), it.value());
    }

    applyStyleMap(node->style, def->defaultStyleCss);

    //  Bilingual palette labels travel with the node so the layer panel and the
    //  properties inspector can name it before the user renames it.
    //  双语面板名称随节点一起传递，使图层面板与属性检查器在用户重命名前即可显示。
    node->data.insert(QStringLiteral("labelEn"), def->labelEn);
    node->data.insert(QStringLiteral("labelZh"), def->labelZh);
    node->data.insert(QStringLiteral("catalogTag"), def->tag);
    if (def->isVoid)
        node->data.insert(QStringLiteral("void"), true);

    return node;
}

// ---------------------------------------------------------------------------
//  Composite builders
//  组件构建
// ---------------------------------------------------------------------------

std::unique_ptr<DocumentNode> ElementCatalog::createComposite(const QString &id,
                                                              bool chineseDefaults) const
{
    if (!findComposite(id))
        return nullptr;

    const bool zh = chineseDefaults;
    const auto tr = [zh](const QString &en, const QString &zhText) {
        return zh ? zhText : en;
    };

    //  ---------------------------------------------------------------- headers
    if (id == QLatin1String("navbar-simple")) {
        auto bar = root(QStringLiteral("header"),
                        {{QStringLiteral("display"), QStringLiteral("flex")},
                         {QStringLiteral("align-items"), QStringLiteral("center")},
                         {QStringLiteral("justify-content"), QStringLiteral("space-between")},
                         {QStringLiteral("gap"), QStringLiteral("24px")},
                         {QStringLiteral("padding"), QStringLiteral("16px 28px")},
                         {QStringLiteral("background"), QStringLiteral("#ffffff")},
                         {QStringLiteral("border-bottom"), QStringLiteral("1px solid #e2e8f0")}},
                        QStringLiteral("Simple navbar"), QStringLiteral("简洁导航栏"));
        auto brand = el(QStringLiteral("a"),
                        {{QStringLiteral("font-size"), QStringLiteral("1.25rem")},
                         {QStringLiteral("font-weight"), QStringLiteral("700")},
                         {QStringLiteral("color"), QStringLiteral("#0f172a")},
                         {QStringLiteral("text-decoration"), QStringLiteral("none")}});
        brand->setAttribute(QStringLiteral("href"), QStringLiteral("#"));
        brand->text = tr(QStringLiteral("Turtle Studio"), QStringLiteral("海龟工作室"));
        bar->appendChild(std::move(brand));

        auto links = el(QStringLiteral("nav"),
                        {{QStringLiteral("display"), QStringLiteral("flex")},
                         {QStringLiteral("gap"), QStringLiteral("20px")},
                         {QStringLiteral("align-items"), QStringLiteral("center")}});
        const QStringList linkEn = {QStringLiteral("Product"), QStringLiteral("Templates"),
                                    QStringLiteral("Pricing"), QStringLiteral("Docs")};
        const QStringList linkZh = {QStringLiteral("产品"), QStringLiteral("模板"),
                                    QStringLiteral("价格"), QStringLiteral("文档")};
        const QStringList &linkTexts = zh ? linkZh : linkEn;
        for (const QString &text : linkTexts) {
            auto link = el(QStringLiteral("a"),
                           {{QStringLiteral("color"), QStringLiteral("#334155")},
                            {QStringLiteral("text-decoration"), QStringLiteral("none")},
                            {QStringLiteral("font-size"), QStringLiteral("0.9375rem")}});
            link->setAttribute(QStringLiteral("href"), QStringLiteral("#"));
            link->text = text;
            links->appendChild(std::move(link));
        }
        auto cta = el(QStringLiteral("button"),
                      {{QStringLiteral("padding"), QStringLiteral("8px 16px")},
                       {QStringLiteral("border"), QStringLiteral("0")},
                       {QStringLiteral("border-radius"), QStringLiteral("8px")},
                       {QStringLiteral("background"), QStringLiteral("#2563eb")},
                       {QStringLiteral("color"), QStringLiteral("#ffffff")},
                       {QStringLiteral("font-weight"), QStringLiteral("600")},
                       {QStringLiteral("cursor"), QStringLiteral("pointer")}});
        cta->text = tr(QStringLiteral("Start free"), QStringLiteral("免费开始"));
        links->appendChild(std::move(cta));
        bar->appendChild(std::move(links));
        return bar;
    }

    if (id == QLatin1String("navbar-mega")) {
        auto bar = root(QStringLiteral("header"),
                        {{QStringLiteral("display"), QStringLiteral("flex")},
                         {QStringLiteral("align-items"), QStringLiteral("center")},
                         {QStringLiteral("gap"), QStringLiteral("28px")},
                         {QStringLiteral("padding"), QStringLiteral("16px 28px")},
                         {QStringLiteral("background"), QStringLiteral("#0f172a")},
                         {QStringLiteral("color"), QStringLiteral("#f8fafc")}},
                        QStringLiteral("Mega menu navbar"), QStringLiteral("多列导航栏"));
        auto brand = el(QStringLiteral("a"),
                        {{QStringLiteral("font-weight"), QStringLiteral("700")},
                         {QStringLiteral("color"), QStringLiteral("#f8fafc")},
                         {QStringLiteral("text-decoration"), QStringLiteral("none")}});
        brand->setAttribute(QStringLiteral("href"), QStringLiteral("#"));
        brand->text = QStringLiteral("TC-BuildWeb");
        bar->appendChild(std::move(brand));

        auto menu = el(QStringLiteral("nav"),
                       {{QStringLiteral("display"), QStringLiteral("flex")},
                        {QStringLiteral("gap"), QStringLiteral("22px")},
                        {QStringLiteral("flex"), QStringLiteral("1 1 auto")}});
        const QStringList groupEn = {QStringLiteral("Builder"), QStringLiteral("Backend"),
                                     QStringLiteral("AI"), QStringLiteral("Support")};
        const QStringList groupZh = {QStringLiteral("构建器"), QStringLiteral("后端"),
                                     QStringLiteral("智能助手"), QStringLiteral("支持")};
        const QStringList &groups = zh ? groupZh : groupEn;
        for (const QString &text : groups) {
            auto item = el(QStringLiteral("a"),
                           {{QStringLiteral("color"), QStringLiteral("#e2e8f0")},
                            {QStringLiteral("text-decoration"), QStringLiteral("none")}});
            item->setAttribute(QStringLiteral("href"), QStringLiteral("#"));
            item->text = text;
            menu->appendChild(std::move(item));
        }
        bar->appendChild(std::move(menu));

        auto search = el(QStringLiteral("input"),
                         {{QStringLiteral("padding"), QStringLiteral("8px 12px")},
                          {QStringLiteral("border-radius"), QStringLiteral("8px")},
                          {QStringLiteral("border"), QStringLiteral("1px solid #334155")},
                          {QStringLiteral("background"), QStringLiteral("#1e293b")},
                          {QStringLiteral("color"), QStringLiteral("#f8fafc")}});
        search->setAttribute(QStringLiteral("type"), QStringLiteral("search"));
        search->setAttribute(QStringLiteral("placeholder"),
                             tr(QStringLiteral("Search the docs"), QStringLiteral("搜索文档")));
        bar->appendChild(std::move(search));
        return bar;
    }

    if (id == QLatin1String("breadcrumb-bar")) {
        auto bar = root(QStringLiteral("nav"),
                        {{QStringLiteral("display"), QStringLiteral("flex")},
                         {QStringLiteral("align-items"), QStringLiteral("center")},
                         {QStringLiteral("gap"), QStringLiteral("8px")},
                         {QStringLiteral("font-size"), QStringLiteral("0.875rem")},
                         {QStringLiteral("padding"), QStringLiteral("10px 4px")}},
                        QStringLiteral("Breadcrumb bar"), QStringLiteral("面包屑栏"));
        bar->setAttribute(QStringLiteral("aria-label"), QStringLiteral("Breadcrumb"));
        const QStringList partsEn = {QStringLiteral("Home"), QStringLiteral("Templates"),
                                     QStringLiteral("Landing page")};
        const QStringList partsZh = {QStringLiteral("首页"), QStringLiteral("模板"),
                                     QStringLiteral("落地页")};
        const QStringList &parts = zh ? partsZh : partsEn;
        for (int i = 0; i < parts.size(); ++i) {
            const bool last = (i == parts.size() - 1);
            auto crumb = el(QStringLiteral("span"),
                            {{QStringLiteral("color"),
                              last ? QStringLiteral("#0f172a") : QStringLiteral("#2563eb")}});
            crumb->text = parts.at(i);
            bar->appendChild(std::move(crumb));
            if (!last) {
                auto sep = el(QStringLiteral("span"),
                              {{QStringLiteral("opacity"), QStringLiteral("0.5")}});
                sep->text = QStringLiteral("/");
                bar->appendChild(std::move(sep));
            }
        }
        return bar;
    }

    // ------------------------------------------------------------------ heroes
    if (id == QLatin1String("hero-center")) {
        auto hero = root(QStringLiteral("section"),
                         {{QStringLiteral("display"), QStringLiteral("flex")},
                          {QStringLiteral("flex-direction"), QStringLiteral("column")},
                          {QStringLiteral("align-items"), QStringLiteral("center")},
                          {QStringLiteral("text-align"), QStringLiteral("center")},
                          {QStringLiteral("gap"), QStringLiteral("18px")},
                          {QStringLiteral("padding"), QStringLiteral("72px 24px")},
                          {QStringLiteral("background"),
                           QStringLiteral("linear-gradient(180deg, #eff6ff 0%, #ffffff 100%)")}},
                         QStringLiteral("Centered hero"), QStringLiteral("居中主视觉"));
        auto title = el(QStringLiteral("h1"),
                        {{QStringLiteral("font-size"), QStringLiteral("2.75rem")},
                         {QStringLiteral("font-weight"), QStringLiteral("800")},
                         {QStringLiteral("line-height"), QStringLiteral("1.15")},
                         {QStringLiteral("margin"), QStringLiteral("0")}});
        title->text = tr(QStringLiteral("Build production-ready pages without writing code"),
                        QStringLiteral("无需编写代码，即可构建可直接上线的页面"));
        hero->appendChild(std::move(title));

        auto subtitle = el(QStringLiteral("p"),
                           {{QStringLiteral("font-size"), QStringLiteral("1.125rem")},
                            {QStringLiteral("line-height"), QStringLiteral("1.7")},
                            {QStringLiteral("max-width"), QStringLiteral("60ch")},
                            {QStringLiteral("opacity"), QStringLiteral("0.8")},
                            {QStringLiteral("margin"), QStringLiteral("0")}});
        subtitle->text = tr(QStringLiteral("Drag elements onto the canvas, wire up behaviour with blocks, then export clean HTML."),
                           QStringLiteral("把元素拖到画布上，用积木连接行为，然后导出干净的 HTML。"));
        hero->appendChild(std::move(subtitle));

        auto actions = el(QStringLiteral("div"),
                          {{QStringLiteral("display"), QStringLiteral("flex")},
                           {QStringLiteral("gap"), QStringLiteral("12px")},
                           {QStringLiteral("margin-top"), QStringLiteral("8px")}});
        auto primary = el(QStringLiteral("button"),
                          {{QStringLiteral("padding"), QStringLiteral("12px 26px")},
                           {QStringLiteral("border"), QStringLiteral("0")},
                           {QStringLiteral("border-radius"), QStringLiteral("10px")},
                           {QStringLiteral("background"), QStringLiteral("#2563eb")},
                           {QStringLiteral("color"), QStringLiteral("#ffffff")},
                           {QStringLiteral("font-size"), QStringLiteral("1rem")},
                           {QStringLiteral("font-weight"), QStringLiteral("600")},
                           {QStringLiteral("cursor"), QStringLiteral("pointer")}});
        primary->text = tr(QStringLiteral("Create a page"), QStringLiteral("创建页面"));
        actions->appendChild(std::move(primary));
        auto secondary = el(QStringLiteral("button"),
                            {{QStringLiteral("padding"), QStringLiteral("12px 26px")},
                             {QStringLiteral("border"), QStringLiteral("1px solid #cbd5e1")},
                             {QStringLiteral("border-radius"), QStringLiteral("10px")},
                             {QStringLiteral("background"), QStringLiteral("#ffffff")},
                             {QStringLiteral("color"), QStringLiteral("#0f172a")},
                             {QStringLiteral("font-size"), QStringLiteral("1rem")},
                             {QStringLiteral("font-weight"), QStringLiteral("600")},
                             {QStringLiteral("cursor"), QStringLiteral("pointer")}});
        secondary->text = tr(QStringLiteral("Watch the tour"), QStringLiteral("观看演示"));
        actions->appendChild(std::move(secondary));
        hero->appendChild(std::move(actions));
        return hero;
    }

    if (id == QLatin1String("hero-split")) {
        auto hero = root(QStringLiteral("section"),
                         {{QStringLiteral("display"), QStringLiteral("grid")},
                          {QStringLiteral("grid-template-columns"),
                           QStringLiteral("minmax(0, 1fr) minmax(0, 1fr)")},
                          {QStringLiteral("gap"), QStringLiteral("40px")},
                          {QStringLiteral("align-items"), QStringLiteral("center")},
                          {QStringLiteral("padding"), QStringLiteral("64px 28px")}},
                         QStringLiteral("Split hero"), QStringLiteral("左右分栏主视觉"));
        auto copy = el(QStringLiteral("div"),
                       {{QStringLiteral("display"), QStringLiteral("flex")},
                        {QStringLiteral("flex-direction"), QStringLiteral("column")},
                        {QStringLiteral("gap"), QStringLiteral("16px")}});
        auto title = el(QStringLiteral("h1"),
                        {{QStringLiteral("font-size"), QStringLiteral("2.5rem")},
                         {QStringLiteral("font-weight"), QStringLiteral("800")},
                         {QStringLiteral("line-height"), QStringLiteral("1.15")},
                         {QStringLiteral("margin"), QStringLiteral("0")}});
        title->text = tr(QStringLiteral("Design, wire and export in one window"),
                        QStringLiteral("设计、连线与导出，尽在一个窗口"));
        copy->appendChild(std::move(title));

        auto body = el(QStringLiteral("p"),
                       {{QStringLiteral("font-size"), QStringLiteral("1.0625rem")},
                        {QStringLiteral("line-height"), QStringLiteral("1.7")},
                        {QStringLiteral("opacity"), QStringLiteral("0.8")},
                        {QStringLiteral("margin"), QStringLiteral("0")}});
        body->text = tr(QStringLiteral("Visual layout and block programming share the same document, so nothing drifts out of sync."),
                       QStringLiteral("可视化布局与积木编程共用同一份文档，因此永远不会失去同步。"));
        copy->appendChild(std::move(body));

        auto cta = el(QStringLiteral("button"),
                      {{QStringLiteral("padding"), QStringLiteral("12px 24px")},
                       {QStringLiteral("border"), QStringLiteral("0")},
                       {QStringLiteral("border-radius"), QStringLiteral("10px")},
                       {QStringLiteral("background"), QStringLiteral("#0f172a")},
                       {QStringLiteral("color"), QStringLiteral("#ffffff")},
                       {QStringLiteral("font-weight"), QStringLiteral("600")},
                       {QStringLiteral("align-self"), QStringLiteral("flex-start")},
                       {QStringLiteral("cursor"), QStringLiteral("pointer")}});
        cta->text = tr(QStringLiteral("Open the builder"), QStringLiteral("打开构建器"));
        copy->appendChild(std::move(cta));
        hero->appendChild(std::move(copy));

        auto media = el(QStringLiteral("img"),
                        {{QStringLiteral("width"), QStringLiteral("100%")},
                         {QStringLiteral("height"), QStringLiteral("auto")},
                         {QStringLiteral("border-radius"), QStringLiteral("16px")},
                         {QStringLiteral("box-shadow"),
                          QStringLiteral("0 18px 40px rgba(15, 23, 42, 0.16)")}});
        media->setAttribute(QStringLiteral("src"), QStringLiteral("assets/hero-preview.svg"));
        media->setAttribute(QStringLiteral("alt"),
                            tr(QStringLiteral("Screenshot of the builder"),
                              QStringLiteral("构建器界面截图")));
        hero->appendChild(std::move(media));
        return hero;
    }

    if (id == QLatin1String("cta-banner")) {
        auto banner = root(QStringLiteral("section"),
                           {{QStringLiteral("display"), QStringLiteral("flex")},
                            {QStringLiteral("align-items"), QStringLiteral("center")},
                            {QStringLiteral("justify-content"), QStringLiteral("space-between")},
                            {QStringLiteral("gap"), QStringLiteral("24px")},
                            {QStringLiteral("padding"), QStringLiteral("32px 36px")},
                            {QStringLiteral("border-radius"), QStringLiteral("16px")},
                            {QStringLiteral("background"),
                             QStringLiteral("linear-gradient(135deg, #1d4ed8 0%, #0ea5e9 100%)")},
                            {QStringLiteral("color"), QStringLiteral("#ffffff")}},
                           QStringLiteral("Call-to-action banner"),
                           QStringLiteral("行动号召横幅"));
        auto text = el(QStringLiteral("p"),
                       {{QStringLiteral("font-size"), QStringLiteral("1.375rem")},
                        {QStringLiteral("font-weight"), QStringLiteral("700")},
                        {QStringLiteral("margin"), QStringLiteral("0")}});
        text->text = tr(QStringLiteral("Ready to ship your next landing page today?"),
                       QStringLiteral("准备好今天就发布你的下一个落地页了吗？"));
        banner->appendChild(std::move(text));

        auto button = el(QStringLiteral("button"),
                         {{QStringLiteral("padding"), QStringLiteral("12px 28px")},
                          {QStringLiteral("border"), QStringLiteral("0")},
                          {QStringLiteral("border-radius"), QStringLiteral("10px")},
                          {QStringLiteral("background"), QStringLiteral("#ffffff")},
                          {QStringLiteral("color"), QStringLiteral("#1d4ed8")},
                          {QStringLiteral("font-weight"), QStringLiteral("700")},
                          {QStringLiteral("white-space"), QStringLiteral("nowrap")},
                          {QStringLiteral("cursor"), QStringLiteral("pointer")}});
        button->text = tr(QStringLiteral("Get started free"), QStringLiteral("免费开始"));
        banner->appendChild(std::move(button));
        return banner;
    }

    if (id == QLatin1String("coming-soon")) {
        auto page = root(QStringLiteral("section"),
                         {{QStringLiteral("display"), QStringLiteral("flex")},
                          {QStringLiteral("flex-direction"), QStringLiteral("column")},
                          {QStringLiteral("align-items"), QStringLiteral("center")},
                          {QStringLiteral("justify-content"), QStringLiteral("center")},
                          {QStringLiteral("gap"), QStringLiteral("16px")},
                          {QStringLiteral("min-height"), QStringLiteral("70vh")},
                          {QStringLiteral("text-align"), QStringLiteral("center")},
                          {QStringLiteral("padding"), QStringLiteral("48px 24px")}},
                         QStringLiteral("Coming soon"), QStringLiteral("敬请期待"));
        auto title = el(QStringLiteral("h1"),
                        {{QStringLiteral("font-size"), QStringLiteral("2.5rem")},
                         {QStringLiteral("font-weight"), QStringLiteral("800")},
                         {QStringLiteral("margin"), QStringLiteral("0")}});
        title->text = tr(QStringLiteral("Something good is on the way"),
                        QStringLiteral("好东西正在路上"));
        page->appendChild(std::move(title));

        auto copy = el(QStringLiteral("p"),
                       {{QStringLiteral("font-size"), QStringLiteral("1.0625rem")},
                        {QStringLiteral("opacity"), QStringLiteral("0.8")},
                        {QStringLiteral("margin"), QStringLiteral("0")}});
        copy->text = tr(QStringLiteral("Leave your email and we will tell you the moment it launches."),
                       QStringLiteral("留下邮箱，上线第一时间通知你。"));
        page->appendChild(std::move(copy));

        auto form = el(QStringLiteral("form"),
                       {{QStringLiteral("display"), QStringLiteral("flex")},
                        {QStringLiteral("gap"), QStringLiteral("10px")},
                        {QStringLiteral("margin-top"), QStringLiteral("8px")}});
        form->setAttribute(QStringLiteral("action"), QStringLiteral("#"));
        auto email = el(QStringLiteral("input"),
                        {{QStringLiteral("padding"), QStringLiteral("12px 14px")},
                         {QStringLiteral("border"), QStringLiteral("1px solid #cbd5e1")},
                         {QStringLiteral("border-radius"), QStringLiteral("10px")},
                         {QStringLiteral("min-width"), QStringLiteral("280px")}});
        email->setAttribute(QStringLiteral("type"), QStringLiteral("email"));
        email->setAttribute(QStringLiteral("placeholder"),
                            tr(QStringLiteral("you@example.com"), QStringLiteral("you@example.com")));
        form->appendChild(std::move(email));
        auto submit = el(QStringLiteral("button"),
                         {{QStringLiteral("padding"), QStringLiteral("12px 22px")},
                          {QStringLiteral("border"), QStringLiteral("0")},
                          {QStringLiteral("border-radius"), QStringLiteral("10px")},
                          {QStringLiteral("background"), QStringLiteral("#2563eb")},
                          {QStringLiteral("color"), QStringLiteral("#ffffff")},
                          {QStringLiteral("font-weight"), QStringLiteral("600")},
                          {QStringLiteral("cursor"), QStringLiteral("pointer")}});
        submit->text = tr(QStringLiteral("Notify me"), QStringLiteral("通知我"));
        form->appendChild(std::move(submit));
        page->appendChild(std::move(form));
        return page;
    }

    if (id == QLatin1String("error-404")) {
        auto page = root(QStringLiteral("section"),
                         {{QStringLiteral("display"), QStringLiteral("flex")},
                          {QStringLiteral("flex-direction"), QStringLiteral("column")},
                          {QStringLiteral("align-items"), QStringLiteral("center")},
                          {QStringLiteral("justify-content"), QStringLiteral("center")},
                          {QStringLiteral("gap"), QStringLiteral("14px")},
                          {QStringLiteral("min-height"), QStringLiteral("60vh")},
                          {QStringLiteral("text-align"), QStringLiteral("center")},
                          {QStringLiteral("padding"), QStringLiteral("48px 24px")}},
                         QStringLiteral("404 page"), QStringLiteral("404 页面"));
        auto code = el(QStringLiteral("span"),
                       {{QStringLiteral("font-size"), QStringLiteral("5rem")},
                        {QStringLiteral("font-weight"), QStringLiteral("800")},
                        {QStringLiteral("color"), QStringLiteral("#cbd5e1")},
                        {QStringLiteral("line-height"), QStringLiteral("1")}});
        code->text = QStringLiteral("404");
        page->appendChild(std::move(code));

        auto title = el(QStringLiteral("h1"),
                        {{QStringLiteral("font-size"), QStringLiteral("1.75rem")},
                         {QStringLiteral("font-weight"), QStringLiteral("700")},
                         {QStringLiteral("margin"), QStringLiteral("0")}});
        title->text = tr(QStringLiteral("We could not find that page"),
                        QStringLiteral("我们找不到该页面"));
        page->appendChild(std::move(title));

        auto copy = el(QStringLiteral("p"),
                       {{QStringLiteral("opacity"), QStringLiteral("0.8")},
                        {QStringLiteral("margin"), QStringLiteral("0")}});
        copy->text = tr(QStringLiteral("The link may be broken, or the page may have moved."),
                       QStringLiteral("链接可能已失效，或者页面已被移动。"));
        page->appendChild(std::move(copy));

        auto back = el(QStringLiteral("a"),
                       {{QStringLiteral("margin-top"), QStringLiteral("8px")},
                        {QStringLiteral("padding"), QStringLiteral("10px 22px")},
                        {QStringLiteral("border-radius"), QStringLiteral("10px")},
                        {QStringLiteral("background"), QStringLiteral("#2563eb")},
                        {QStringLiteral("color"), QStringLiteral("#ffffff")},
                        {QStringLiteral("text-decoration"), QStringLiteral("none")},
                        {QStringLiteral("font-weight"), QStringLiteral("600")}});
        back->setAttribute(QStringLiteral("href"), QStringLiteral("index.html"));
        back->text = tr(QStringLiteral("Back to home"), QStringLiteral("返回首页"));
        page->appendChild(std::move(back));
        return page;
    }

    // ----------------------------------------------------------------- content
    if (id == QLatin1String("feature-grid-3")) {
        auto grid = root(QStringLiteral("section"),
                         {{QStringLiteral("display"), QStringLiteral("grid")},
                          {QStringLiteral("grid-template-columns"),
                           QStringLiteral("repeat(auto-fit, minmax(240px, 1fr))")},
                          {QStringLiteral("gap"), QStringLiteral("24px")},
                          {QStringLiteral("padding"), QStringLiteral("48px 24px")}},
                         QStringLiteral("Feature grid"), QStringLiteral("特性网格"));
        const QStringList icons = {QStringLiteral("🧩"), QStringLiteral("⚡"),
                                   QStringLiteral("🔒")};
        const QStringList titlesEn = {QStringLiteral("Visual layout"),
                                      QStringLiteral("Block behaviour"),
                                      QStringLiteral("Safe export")};
        const QStringList titlesZh = {QStringLiteral("可视化布局"),
                                      QStringLiteral("积木化行为"),
                                      QStringLiteral("安全导出")};
        const QStringList bodiesEn = {
            QStringLiteral("Snap-to-grid dragging keeps spacing consistent across breakpoints."),
            QStringLiteral("Wire clicks and page loads with Scratch-style blocks, no JavaScript needed."),
            QStringLiteral("Every export is plain HTML, CSS and JS you fully own.")};
        const QStringList bodiesZh = {
            QStringLiteral("对齐网格的拖拽让各断点的间距保持一致。"),
            QStringLiteral("用类 Scratch 积木连接点击与页面加载，无需手写 JavaScript。"),
            QStringLiteral("导出结果就是完全属于你的纯 HTML、CSS 与 JS。")};
        const QStringList &titles = zh ? titlesZh : titlesEn;
        const QStringList &bodies = zh ? bodiesZh : bodiesEn;
        for (int i = 0; i < titles.size(); ++i) {
            auto card = el(QStringLiteral("article"),
                           {{QStringLiteral("display"), QStringLiteral("flex")},
                            {QStringLiteral("flex-direction"), QStringLiteral("column")},
                            {QStringLiteral("gap"), QStringLiteral("10px")},
                            {QStringLiteral("padding"), QStringLiteral("24px")},
                            {QStringLiteral("border"), QStringLiteral("1px solid #e2e8f0")},
                            {QStringLiteral("border-radius"), QStringLiteral("14px")},
                            {QStringLiteral("background"), QStringLiteral("#ffffff")},
                            {QStringLiteral("box-shadow"),
                             QStringLiteral("0 6px 18px rgba(15, 23, 42, 0.06)")}});
            auto icon = el(QStringLiteral("span"),
                           {{QStringLiteral("font-size"), QStringLiteral("1.75rem")}});
            icon->text = icons.at(i);
            card->appendChild(std::move(icon));

            auto heading = el(QStringLiteral("h3"),
                              {{QStringLiteral("font-size"), QStringLiteral("1.125rem")},
                               {QStringLiteral("font-weight"), QStringLiteral("700")},
                               {QStringLiteral("margin"), QStringLiteral("0")}});
            heading->text = titles.at(i);
            card->appendChild(std::move(heading));

            auto copy = el(QStringLiteral("p"),
                           {{QStringLiteral("line-height"), QStringLiteral("1.65")},
                            {QStringLiteral("margin"), QStringLiteral("0")},
                            {QStringLiteral("opacity"), QStringLiteral("0.82")}});
            copy->text = bodies.at(i);
            card->appendChild(std::move(copy));
            grid->appendChild(std::move(card));
        }
        return grid;
    }

    if (id == QLatin1String("stats-row")) {
        auto row = root(QStringLiteral("section"),
                        {{QStringLiteral("display"), QStringLiteral("grid")},
                         {QStringLiteral("grid-template-columns"),
                          QStringLiteral("repeat(auto-fit, minmax(160px, 1fr))")},
                         {QStringLiteral("gap"), QStringLiteral("20px")},
                         {QStringLiteral("padding"), QStringLiteral("36px 24px")},
                         {QStringLiteral("text-align"), QStringLiteral("center")},
                         {QStringLiteral("background"), QStringLiteral("#f8fafc")}},
                        QStringLiteral("Stats row"), QStringLiteral("数据行"));
        const QStringList values = {QStringLiteral("12"), QStringLiteral("98%"),
                                    QStringLiteral("4"), QStringLiteral("0")};
        const QStringList labelsEn = {QStringLiteral("Element types"),
                                      QStringLiteral("Export success"),
                                      QStringLiteral("Export formats"),
                                      QStringLiteral("Telemetry events")};
        const QStringList labelsZh = {QStringLiteral("元素类型"),
                                      QStringLiteral("导出成功率"),
                                      QStringLiteral("导出格式"),
                                      QStringLiteral("遥测事件")};
        const QStringList &labels = zh ? labelsZh : labelsEn;
        for (int i = 0; i < values.size(); ++i) {
            auto block = el(QStringLiteral("div"),
                            {{QStringLiteral("display"), QStringLiteral("flex")},
                             {QStringLiteral("flex-direction"), QStringLiteral("column")},
                             {QStringLiteral("gap"), QStringLiteral("6px")}});
            auto value = el(QStringLiteral("span"),
                            {{QStringLiteral("font-size"), QStringLiteral("2rem")},
                             {QStringLiteral("font-weight"), QStringLiteral("800")},
                             {QStringLiteral("color"), QStringLiteral("#1d4ed8")}});
            value->text = values.at(i);
            block->appendChild(std::move(value));

            auto label = el(QStringLiteral("span"),
                            {{QStringLiteral("font-size"), QStringLiteral("0.875rem")},
                             {QStringLiteral("opacity"), QStringLiteral("0.75")}});
            label->text = labels.at(i);
            block->appendChild(std::move(label));
            row->appendChild(std::move(block));
        }
        return row;
    }

    if (id == QLatin1String("testimonial")) {
        auto card = root(QStringLiteral("figure"),
                         {{QStringLiteral("display"), QStringLiteral("flex")},
                          {QStringLiteral("flex-direction"), QStringLiteral("column")},
                          {QStringLiteral("gap"), QStringLiteral("16px")},
                          {QStringLiteral("padding"), QStringLiteral("28px")},
                          {QStringLiteral("border-radius"), QStringLiteral("16px")},
                          {QStringLiteral("background"), QStringLiteral("#ffffff")},
                          {QStringLiteral("border"), QStringLiteral("1px solid #e2e8f0")},
                          {QStringLiteral("max-width"), QStringLiteral("640px")}},
                         QStringLiteral("Testimonial"), QStringLiteral("客户评价"));
        auto quote = el(QStringLiteral("blockquote"),
                        {{QStringLiteral("font-size"), QStringLiteral("1.125rem")},
                         {QStringLiteral("line-height"), QStringLiteral("1.7")},
                         {QStringLiteral("margin"), QStringLiteral("0")},
                         {QStringLiteral("border-left"), QStringLiteral("3px solid #93c5fd")},
                         {QStringLiteral("padding-left"), QStringLiteral("16px")}});
        quote->text = tr(QStringLiteral("We shipped a marketing site in an afternoon, and the exported HTML was clean enough to hand to our backend team."),
                        QStringLiteral("我们用一下午就上线了营销站点，导出的 HTML 干净到可以直接交给后端团队。"));
        card->appendChild(std::move(quote));

        auto person = el(QStringLiteral("figcaption"),
                         {{QStringLiteral("display"), QStringLiteral("flex")},
                          {QStringLiteral("align-items"), QStringLiteral("center")},
                          {QStringLiteral("gap"), QStringLiteral("12px")}});
        auto avatar = el(QStringLiteral("img"),
                         {{QStringLiteral("width"), QStringLiteral("44px")},
                          {QStringLiteral("height"), QStringLiteral("44px")},
                          {QStringLiteral("border-radius"), QStringLiteral("50%")},
                          {QStringLiteral("object-fit"), QStringLiteral("cover")}});
        avatar->setAttribute(QStringLiteral("src"), QStringLiteral("assets/avatar-1.svg"));
        avatar->setAttribute(QStringLiteral("alt"),
                             tr(QStringLiteral("Portrait of Lin Wei"),
                               QStringLiteral("林薇的肖像")));
        person->appendChild(std::move(avatar));

        auto who = el(QStringLiteral("span"),
                      {{QStringLiteral("font-weight"), QStringLiteral("600")}});
        who->text = tr(QStringLiteral("Lin Wei · Product lead, Turtle Inc."),
                      QStringLiteral("林薇 · 产品负责人，Turtle Inc."));
        person->appendChild(std::move(who));
        card->appendChild(std::move(person));
        return card;
    }

    if (id == QLatin1String("team-grid")) {
        auto grid = root(QStringLiteral("section"),
                         {{QStringLiteral("display"), QStringLiteral("grid")},
                          {QStringLiteral("grid-template-columns"),
                           QStringLiteral("repeat(auto-fit, minmax(180px, 1fr))")},
                          {QStringLiteral("gap"), QStringLiteral("22px")},
                          {QStringLiteral("padding"), QStringLiteral("44px 24px")}},
                         QStringLiteral("Team grid"), QStringLiteral("团队网格"));
        const QStringList names = {QStringLiteral("Lin Wei"), QStringLiteral("Chen Hao"),
                                   QStringLiteral("Zhao Min"), QStringLiteral("Wu Qiao")};
        const QStringList rolesEn = {QStringLiteral("Product lead"),
                                     QStringLiteral("Frontend engineer"),
                                     QStringLiteral("Design systems"),
                                     QStringLiteral("Backend & AI")};
        const QStringList rolesZh = {QStringLiteral("产品负责人"),
                                     QStringLiteral("前端工程师"),
                                     QStringLiteral("设计系统"),
                                     QStringLiteral("后端与 AI")};
        const QStringList &roles = zh ? rolesZh : rolesEn;
        for (int i = 0; i < names.size(); ++i) {
            auto card = el(QStringLiteral("article"),
                           {{QStringLiteral("display"), QStringLiteral("flex")},
                            {QStringLiteral("flex-direction"), QStringLiteral("column")},
                            {QStringLiteral("align-items"), QStringLiteral("center")},
                            {QStringLiteral("gap"), QStringLiteral("10px")},
                            {QStringLiteral("padding"), QStringLiteral("20px")},
                            {QStringLiteral("border-radius"), QStringLiteral("14px")},
                            {QStringLiteral("background"), QStringLiteral("#f8fafc")}});
            auto photo = el(QStringLiteral("img"),
                            {{QStringLiteral("width"), QStringLiteral("84px")},
                             {QStringLiteral("height"), QStringLiteral("84px")},
                             {QStringLiteral("border-radius"), QStringLiteral("50%")},
                             {QStringLiteral("object-fit"), QStringLiteral("cover")}});
            photo->setAttribute(QStringLiteral("src"),
                                QStringLiteral("assets/avatar-%1.svg").arg(i + 1));
            photo->setAttribute(QStringLiteral("alt"), names.at(i));
            card->appendChild(std::move(photo));

            auto name = el(QStringLiteral("span"),
                           {{QStringLiteral("font-weight"), QStringLiteral("700")}});
            name->text = names.at(i);
            card->appendChild(std::move(name));

            auto role = el(QStringLiteral("span"),
                           {{QStringLiteral("font-size"), QStringLiteral("0.875rem")},
                            {QStringLiteral("opacity"), QStringLiteral("0.72")}});
            role->text = roles.at(i);
            card->appendChild(std::move(role));
            grid->appendChild(std::move(card));
        }
        return grid;
    }

    if (id == QLatin1String("blog-list")) {
        auto list = root(QStringLiteral("section"),
                         {{QStringLiteral("display"), QStringLiteral("flex")},
                          {QStringLiteral("flex-direction"), QStringLiteral("column")},
                          {QStringLiteral("gap"), QStringLiteral("28px")},
                          {QStringLiteral("padding"), QStringLiteral("44px 24px")},
                          {QStringLiteral("max-width"), QStringLiteral("820px")}},
                         QStringLiteral("Blog list"), QStringLiteral("博客列表"));
        const QStringList dates = {QStringLiteral("2026-09-18"), QStringLiteral("2026-09-02"),
                                   QStringLiteral("2026-08-21")};
        const QStringList titlesEn = {
            QStringLiteral("Designing a drag-and-drop canvas that stays predictable"),
            QStringLiteral("Generating readable HTML from a visual tree"),
            QStringLiteral("Why we ship zero telemetry")};
        const QStringList titlesZh = {
            QStringLiteral("如何设计一个始终可预期的拖拽画布"),
            QStringLiteral("从可视化树生成可读的 HTML"),
            QStringLiteral("为什么我们不做任何遥测")};
        const QStringList &titles = zh ? titlesZh : titlesEn;
        for (int i = 0; i < titles.size(); ++i) {
            auto post = el(QStringLiteral("article"),
                           {{QStringLiteral("display"), QStringLiteral("flex")},
                            {QStringLiteral("flex-direction"), QStringLiteral("column")},
                            {QStringLiteral("gap"), QStringLiteral("6px")}});
            auto meta = el(QStringLiteral("time"),
                           {{QStringLiteral("font-size"), QStringLiteral("0.8125rem")},
                            {QStringLiteral("opacity"), QStringLiteral("0.7")}});
            meta->setAttribute(QStringLiteral("datetime"), dates.at(i));
            meta->text = dates.at(i);
            post->appendChild(std::move(meta));

            auto heading = el(QStringLiteral("h2"),
                              {{QStringLiteral("font-size"), QStringLiteral("1.375rem")},
                               {QStringLiteral("font-weight"), QStringLiteral("700")},
                               {QStringLiteral("margin"), QStringLiteral("0")}});
            heading->text = titles.at(i);
            post->appendChild(std::move(heading));

            auto more = el(QStringLiteral("a"),
                           {{QStringLiteral("color"), QStringLiteral("#2563eb")},
                            {QStringLiteral("text-decoration"), QStringLiteral("none")},
                            {QStringLiteral("font-weight"), QStringLiteral("600")}});
            more->setAttribute(QStringLiteral("href"), QStringLiteral("#"));
            more->text = tr(QStringLiteral("Read the article"), QStringLiteral("阅读全文"));
            post->appendChild(std::move(more));
            list->appendChild(std::move(post));
        }
        return list;
    }

    if (id == QLatin1String("article-page")) {
        auto page = root(QStringLiteral("article"),
                         {{QStringLiteral("display"), QStringLiteral("flex")},
                          {QStringLiteral("flex-direction"), QStringLiteral("column")},
                          {QStringLiteral("gap"), QStringLiteral("18px")},
                          {QStringLiteral("max-width"), QStringLiteral("760px")},
                          {QStringLiteral("margin"), QStringLiteral("0 auto")},
                          {QStringLiteral("padding"), QStringLiteral("40px 24px")}},
                         QStringLiteral("Article page"), QStringLiteral("文章页"));
        auto title = el(QStringLiteral("h1"),
                        {{QStringLiteral("font-size"), QStringLiteral("2.25rem")},
                         {QStringLiteral("font-weight"), QStringLiteral("800")},
                         {QStringLiteral("line-height"), QStringLiteral("1.2")},
                         {QStringLiteral("margin"), QStringLiteral("0")}});
        title->text = tr(QStringLiteral("A visual builder that exports code you can read"),
                        QStringLiteral("一个能导出可读代码的可视化构建器"));
        page->appendChild(std::move(title));

        auto byline = el(QStringLiteral("span"),
                         {{QStringLiteral("font-size"), QStringLiteral("0.875rem")},
                          {QStringLiteral("opacity"), QStringLiteral("0.7")}});
        byline->text = tr(QStringLiteral("By Lin Wei · 11 October 2026"),
                         QStringLiteral("作者：林薇 · 2026 年 10 月 11 日"));
        page->appendChild(std::move(byline));

        auto cover = el(QStringLiteral("img"),
                        {{QStringLiteral("width"), QStringLiteral("100%")},
                         {QStringLiteral("border-radius"), QStringLiteral("14px")}});
        cover->setAttribute(QStringLiteral("src"), QStringLiteral("assets/cover.svg"));
        cover->setAttribute(QStringLiteral("alt"),
                            tr(QStringLiteral("Article cover"), QStringLiteral("文章封面")));
        page->appendChild(std::move(cover));

        auto para = el(QStringLiteral("p"),
                       {{QStringLiteral("font-size"), QStringLiteral("1.0625rem")},
                        {QStringLiteral("line-height"), QStringLiteral("1.8")},
                        {QStringLiteral("margin"), QStringLiteral("0")}});
        para->text = tr(QStringLiteral("Everything on the canvas maps to a real DOM node, so the generated file looks like something a person would have written by hand."),
                       QStringLiteral("画布上的每一处都对应真实的 DOM 节点，因此生成的文件看起来就像是人手写出来的。"));
        page->appendChild(std::move(para));
        return page;
    }

    if (id == QLatin1String("faq-accordion")) {
        auto list = root(QStringLiteral("section"),
                         {{QStringLiteral("display"), QStringLiteral("flex")},
                          {QStringLiteral("flex-direction"), QStringLiteral("column")},
                          {QStringLiteral("gap"), QStringLiteral("12px")},
                          {QStringLiteral("max-width"), QStringLiteral("720px")},
                          {QStringLiteral("padding"), QStringLiteral("40px 24px")}},
                         QStringLiteral("FAQ accordion"), QStringLiteral("常见问题折叠"));
        const QStringList qEn = {
            QStringLiteral("Do I need to know JavaScript?"),
            QStringLiteral("Can I edit the exported files?"),
            QStringLiteral("Is my project sent to a server?")};
        const QStringList qZh = {
            QStringLiteral("我需要懂 JavaScript 吗？"),
            QStringLiteral("我可以修改导出的文件吗？"),
            QStringLiteral("我的工程会被上传到服务器吗？")};
        const QStringList aEn = {
            QStringLiteral("No. Blocks compile to plain JavaScript for you."),
            QStringLiteral("Yes. The output is ordinary HTML, CSS and JS with no lock-in."),
            QStringLiteral("No. Projects stay on your machine unless you call an AI model.")};
        const QStringList aZh = {
            QStringLiteral("不需要。积木会为你编译成普通的 JavaScript。"),
            QStringLiteral("可以。输出就是普通的 HTML、CSS 与 JS，没有任何绑定。"),
            QStringLiteral("不会。除非你主动调用 AI 模型，工程始终留在本机。")};
        const QStringList &questions = zh ? qZh : qEn;
        const QStringList &answers = zh ? aZh : aEn;
        for (int i = 0; i < questions.size(); ++i) {
            auto item = el(QStringLiteral("details"),
                           {{QStringLiteral("padding"), QStringLiteral("14px 18px")},
                            {QStringLiteral("border"), QStringLiteral("1px solid #e2e8f0")},
                            {QStringLiteral("border-radius"), QStringLiteral("10px")},
                            {QStringLiteral("background"), QStringLiteral("#ffffff")}});
            if (i == 0)
                item->attributes.insert(QStringLiteral("open"), QString());

            auto summary = el(QStringLiteral("summary"),
                              {{QStringLiteral("font-weight"), QStringLiteral("600")},
                               {QStringLiteral("cursor"), QStringLiteral("pointer")}});
            summary->text = questions.at(i);
            item->appendChild(std::move(summary));

            auto answer = el(QStringLiteral("p"),
                             {{QStringLiteral("margin"), QStringLiteral("12px 0 0")},
                              {QStringLiteral("line-height"), QStringLiteral("1.7")},
                              {QStringLiteral("opacity"), QStringLiteral("0.82")}});
            answer->text = answers.at(i);
            item->appendChild(std::move(answer));
            list->appendChild(std::move(item));
        }
        return list;
    }

    if (id == QLatin1String("gallery-grid")) {
        auto grid = root(QStringLiteral("section"),
                         {{QStringLiteral("display"), QStringLiteral("grid")},
                          {QStringLiteral("grid-template-columns"),
                           QStringLiteral("repeat(auto-fit, minmax(200px, 1fr))")},
                          {QStringLiteral("gap"), QStringLiteral("14px")},
                          {QStringLiteral("padding"), QStringLiteral("36px 24px")}},
                         QStringLiteral("Image gallery"), QStringLiteral("图片画廊"));
        for (int i = 1; i <= 6; ++i) {
            auto figure = el(QStringLiteral("figure"),
                             {{QStringLiteral("margin"), QStringLiteral("0")},
                              {QStringLiteral("border-radius"), QStringLiteral("12px")},
                              {QStringLiteral("overflow"), QStringLiteral("hidden")}});
            auto image = el(QStringLiteral("img"),
                            {{QStringLiteral("width"), QStringLiteral("100%")},
                             {QStringLiteral("height"), QStringLiteral("180px")},
                             {QStringLiteral("object-fit"), QStringLiteral("cover")},
                             {QStringLiteral("display"), QStringLiteral("block")}});
            image->setAttribute(QStringLiteral("src"),
                                QStringLiteral("assets/gallery-%1.svg").arg(i));
            image->setAttribute(QStringLiteral("alt"),
                                tr(QStringLiteral("Gallery image %1").arg(i),
                                  QStringLiteral("画廊图片 %1").arg(i)));
            image->setAttribute(QStringLiteral("loading"), QStringLiteral("lazy"));
            figure->appendChild(std::move(image));
            grid->appendChild(std::move(figure));
        }
        return grid;
    }

    // ---------------------------------------------------------------- commerce
    if (id == QLatin1String("pricing-3")) {
        auto grid = root(QStringLiteral("section"),
                         {{QStringLiteral("display"), QStringLiteral("grid")},
                          {QStringLiteral("grid-template-columns"),
                           QStringLiteral("repeat(auto-fit, minmax(240px, 1fr))")},
                          {QStringLiteral("gap"), QStringLiteral("22px")},
                          {QStringLiteral("padding"), QStringLiteral("48px 24px")},
                          {QStringLiteral("align-items"), QStringLiteral("stretch")}},
                         QStringLiteral("Pricing table"), QStringLiteral("价格表"));
        const QStringList plansEn = {QStringLiteral("Starter"), QStringLiteral("Studio"),
                                     QStringLiteral("Team")};
        const QStringList plansZh = {QStringLiteral("入门版"), QStringLiteral("工作室版"),
                                     QStringLiteral("团队版")};
        const QStringList prices = {QStringLiteral("¥0"), QStringLiteral("¥68"),
                                    QStringLiteral("¥188")};
        const QStringList periodsEn = {QStringLiteral("free forever"),
                                       QStringLiteral("per month"),
                                       QStringLiteral("per month")};
        const QStringList periodsZh = {QStringLiteral("永久免费"),
                                       QStringLiteral("每月"),
                                       QStringLiteral("每月")};
        const QStringList &plans = zh ? plansZh : plansEn;
        const QStringList &periods = zh ? periodsZh : periodsEn;
        for (int i = 0; i < plans.size(); ++i) {
            const bool featured = (i == 1);
            auto card = el(QStringLiteral("article"),
                           {{QStringLiteral("display"), QStringLiteral("flex")},
                            {QStringLiteral("flex-direction"), QStringLiteral("column")},
                            {QStringLiteral("gap"), QStringLiteral("14px")},
                            {QStringLiteral("padding"), QStringLiteral("26px")},
                            {QStringLiteral("border-radius"), QStringLiteral("16px")},
                            {QStringLiteral("border"),
                             featured ? QStringLiteral("2px solid #2563eb")
                                      : QStringLiteral("1px solid #e2e8f0")},
                            {QStringLiteral("background"), QStringLiteral("#ffffff")},
                            {QStringLiteral("box-shadow"),
                             featured ? QStringLiteral("0 14px 34px rgba(37, 99, 235, 0.18)")
                                      : QStringLiteral("0 4px 14px rgba(15, 23, 42, 0.05)")}});
            auto plan = el(QStringLiteral("h3"),
                           {{QStringLiteral("font-size"), QStringLiteral("1.0625rem")},
                            {QStringLiteral("font-weight"), QStringLiteral("700")},
                            {QStringLiteral("margin"), QStringLiteral("0")}});
            plan->text = plans.at(i);
            card->appendChild(std::move(plan));

            auto price = el(QStringLiteral("span"),
                            {{QStringLiteral("font-size"), QStringLiteral("2.25rem")},
                             {QStringLiteral("font-weight"), QStringLiteral("800")},
                             {QStringLiteral("color"), QStringLiteral("#0f172a")}});
            price->text = prices.at(i);
            card->appendChild(std::move(price));

            auto period = el(QStringLiteral("span"),
                             {{QStringLiteral("font-size"), QStringLiteral("0.8125rem")},
                              {QStringLiteral("opacity"), QStringLiteral("0.7")}});
            period->text = periods.at(i);
            card->appendChild(std::move(period));

            auto features = el(QStringLiteral("ul"),
                               {{QStringLiteral("list-style"), QStringLiteral("none")},
                                {QStringLiteral("padding"), QStringLiteral("0")},
                                {QStringLiteral("margin"), QStringLiteral("0")},
                                {QStringLiteral("display"), QStringLiteral("flex")},
                                {QStringLiteral("flex-direction"), QStringLiteral("column")},
                                {QStringLiteral("gap"), QStringLiteral("8px")}});
            const QStringList featureLabels = zh
                ? QStringList{QStringLiteral("页面数量不限"), QStringLiteral("HTML 导出"),
                              QStringLiteral("AI 助手")}
                : QStringList{QStringLiteral("Unlimited pages"), QStringLiteral("HTML export"),
                              QStringLiteral("AI assistant")};
            for (const QString &label : featureLabels) {
                auto item = el(QStringLiteral("li"),
                               {{QStringLiteral("font-size"), QStringLiteral("0.9375rem")}});
                item->text = QStringLiteral("✓ ") + label;
                features->appendChild(std::move(item));
            }
            card->appendChild(std::move(features));

            auto choose = el(QStringLiteral("button"),
                             {{QStringLiteral("margin-top"), QStringLiteral("auto")},
                              {QStringLiteral("padding"), QStringLiteral("11px 18px")},
                              {QStringLiteral("border"), QStringLiteral("0")},
                              {QStringLiteral("border-radius"), QStringLiteral("10px")},
                              {QStringLiteral("background"),
                               featured ? QStringLiteral("#2563eb")
                                        : QStringLiteral("#0f172a")},
                              {QStringLiteral("color"), QStringLiteral("#ffffff")},
                              {QStringLiteral("font-weight"), QStringLiteral("600")},
                              {QStringLiteral("cursor"), QStringLiteral("pointer")}});
            choose->text = tr(QStringLiteral("Choose plan"), QStringLiteral("选择方案"));
            card->appendChild(std::move(choose));
            grid->appendChild(std::move(card));
        }
        return grid;
    }

    // ------------------------------------------------------------------- forms
    if (id == QLatin1String("contact-form")) {
        auto card = root(QStringLiteral("form"),
                         {{QStringLiteral("display"), QStringLiteral("flex")},
                          {QStringLiteral("flex-direction"), QStringLiteral("column")},
                          {QStringLiteral("gap"), QStringLiteral("16px")},
                          {QStringLiteral("max-width"), QStringLiteral("560px")},
                          {QStringLiteral("padding"), QStringLiteral("28px")},
                          {QStringLiteral("border-radius"), QStringLiteral("16px")},
                          {QStringLiteral("background"), QStringLiteral("#ffffff")},
                          {QStringLiteral("border"), QStringLiteral("1px solid #e2e8f0")}},
                         QStringLiteral("Contact form"), QStringLiteral("联系表单"));
        card->setAttribute(QStringLiteral("action"), QStringLiteral("#"));
        card->setAttribute(QStringLiteral("method"), QStringLiteral("post"));

        auto heading = el(QStringLiteral("h2"),
                          {{QStringLiteral("font-size"), QStringLiteral("1.5rem")},
                           {QStringLiteral("font-weight"), QStringLiteral("700")},
                           {QStringLiteral("margin"), QStringLiteral("0")}});
        heading->text = tr(QStringLiteral("Talk to us"), QStringLiteral("联系我们"));
        card->appendChild(std::move(heading));

        struct FieldSpec
        {
            const char *tag;
            const char *type;
            const char *name;
            QString label;
            QString placeholder;
            int rows;
        };
        const QVector<FieldSpec> specs = {
            {"input", "text", "name", tr(QStringLiteral("Your name"), QStringLiteral("你的姓名")),
             tr(QStringLiteral("Lin Wei"), QStringLiteral("林薇")), 0},
            {"input", "email", "email", tr(QStringLiteral("Email"), QStringLiteral("电子邮箱")),
             tr(QStringLiteral("you@example.com"), QStringLiteral("you@example.com")), 0},
            {"textarea", nullptr, "message",
             tr(QStringLiteral("How can we help?"), QStringLiteral("我们能帮你做什么？")),
             tr(QStringLiteral("Tell us about your project"),
               QStringLiteral("介绍一下你的项目")), 5}};
        for (const FieldSpec &spec : specs) {
            auto group = el(QStringLiteral("div"),
                            {{QStringLiteral("display"), QStringLiteral("flex")},
                             {QStringLiteral("flex-direction"), QStringLiteral("column")},
                             {QStringLiteral("gap"), QStringLiteral("6px")}});
            auto label = el(QStringLiteral("label"),
                            {{QStringLiteral("font-size"), QStringLiteral("0.875rem")},
                             {QStringLiteral("font-weight"), QStringLiteral("600")}});
            label->text = spec.label;
            label->setAttribute(QStringLiteral("for"), QString::fromLatin1(spec.name));
            group->appendChild(std::move(label));

            auto control = el(QString::fromLatin1(spec.tag),
                              {{QStringLiteral("padding"), QStringLiteral("10px 12px")},
                               {QStringLiteral("border"), QStringLiteral("1px solid #cbd5e1")},
                               {QStringLiteral("border-radius"), QStringLiteral("8px")},
                               {QStringLiteral("font-size"), QStringLiteral("1rem")},
                               {QStringLiteral("width"), QStringLiteral("100%")}});
            control->setAttribute(QStringLiteral("id"), QString::fromLatin1(spec.name));
            control->setAttribute(QStringLiteral("name"), QString::fromLatin1(spec.name));
            if (spec.type)
                control->setAttribute(QStringLiteral("type"), QString::fromLatin1(spec.type));
            control->setAttribute(QStringLiteral("placeholder"), spec.placeholder);
            if (spec.rows > 0)
                control->setAttribute(QStringLiteral("rows"), QString::number(spec.rows));
            group->appendChild(std::move(control));
            card->appendChild(std::move(group));
        }

        auto submit = el(QStringLiteral("button"),
                         {{QStringLiteral("padding"), QStringLiteral("12px 22px")},
                          {QStringLiteral("border"), QStringLiteral("0")},
                          {QStringLiteral("border-radius"), QStringLiteral("10px")},
                          {QStringLiteral("background"), QStringLiteral("#2563eb")},
                          {QStringLiteral("color"), QStringLiteral("#ffffff")},
                          {QStringLiteral("font-weight"), QStringLiteral("600")},
                          {QStringLiteral("align-self"), QStringLiteral("flex-start")},
                          {QStringLiteral("cursor"), QStringLiteral("pointer")}});
        submit->setAttribute(QStringLiteral("type"), QStringLiteral("submit"));
        submit->text = tr(QStringLiteral("Send message"), QStringLiteral("发送消息"));
        card->appendChild(std::move(submit));
        return card;
    }

    if (id == QLatin1String("login-form") || id == QLatin1String("signup-form")) {
        const bool signup = (id == QLatin1String("signup-form"));
        auto card = root(QStringLiteral("form"),
                         {{QStringLiteral("display"), QStringLiteral("flex")},
                          {QStringLiteral("flex-direction"), QStringLiteral("column")},
                          {QStringLiteral("gap"), QStringLiteral("14px")},
                          {QStringLiteral("width"), QStringLiteral("100%")},
                          {QStringLiteral("max-width"), QStringLiteral("400px")},
                          {QStringLiteral("padding"), QStringLiteral("32px")},
                          {QStringLiteral("border-radius"), QStringLiteral("16px")},
                          {QStringLiteral("background"), QStringLiteral("#ffffff")},
                          {QStringLiteral("border"), QStringLiteral("1px solid #e2e8f0")},
                          {QStringLiteral("box-shadow"),
                           QStringLiteral("0 12px 30px rgba(15, 23, 42, 0.08)")}},
                         signup ? QStringLiteral("Sign-up form") : QStringLiteral("Login form"),
                         signup ? QStringLiteral("注册表单") : QStringLiteral("登录表单"));
        card->setAttribute(QStringLiteral("action"), QStringLiteral("#"));
        card->setAttribute(QStringLiteral("method"), QStringLiteral("post"));

        auto heading = el(QStringLiteral("h2"),
                          {{QStringLiteral("font-size"), QStringLiteral("1.5rem")},
                           {QStringLiteral("font-weight"), QStringLiteral("700")},
                           {QStringLiteral("margin"), QStringLiteral("0")}});
        heading->text = signup ? tr(QStringLiteral("Create your account"),
                                   QStringLiteral("创建你的账号"))
                               : tr(QStringLiteral("Welcome back"), QStringLiteral("欢迎回来"));
        card->appendChild(std::move(heading));

        const QVector<QPair<QString, QPair<QString, QString>>> fields = [&] {
            QVector<QPair<QString, QPair<QString, QString>>> out;
            if (signup) {
                out.append({QStringLiteral("text"),
                            {tr(QStringLiteral("Your name"), QStringLiteral("你的姓名")),
                             tr(QStringLiteral("Lin Wei"), QStringLiteral("林薇"))}});
            }
            out.append({QStringLiteral("email"),
                        {tr(QStringLiteral("Email"), QStringLiteral("电子邮箱")),
                         tr(QStringLiteral("you@example.com"), QStringLiteral("you@example.com"))}});
            out.append({QStringLiteral("password"),
                        {tr(QStringLiteral("Password"), QStringLiteral("密码")),
                         QStringLiteral("••••••••")}});
            return out;
        }();

        for (const auto &field : fields) {
            auto group = el(QStringLiteral("div"),
                            {{QStringLiteral("display"), QStringLiteral("flex")},
                             {QStringLiteral("flex-direction"), QStringLiteral("column")},
                             {QStringLiteral("gap"), QStringLiteral("6px")}});
            auto label = el(QStringLiteral("label"),
                            {{QStringLiteral("font-size"), QStringLiteral("0.875rem")},
                             {QStringLiteral("font-weight"), QStringLiteral("600")}});
            label->text = field.second.first;
            group->appendChild(std::move(label));

            auto input = el(QStringLiteral("input"),
                            {{QStringLiteral("padding"), QStringLiteral("10px 12px")},
                             {QStringLiteral("border"), QStringLiteral("1px solid #cbd5e1")},
                             {QStringLiteral("border-radius"), QStringLiteral("8px")},
                             {QStringLiteral("font-size"), QStringLiteral("1rem")}});
            input->setAttribute(QStringLiteral("type"), field.first);
            input->setAttribute(QStringLiteral("name"), field.first);
            input->setAttribute(QStringLiteral("placeholder"), field.second.second);
            group->appendChild(std::move(input));
            card->appendChild(std::move(group));
        }

        auto submit = el(QStringLiteral("button"),
                         {{QStringLiteral("margin-top"), QStringLiteral("4px")},
                          {QStringLiteral("padding"), QStringLiteral("12px 20px")},
                          {QStringLiteral("border"), QStringLiteral("0")},
                          {QStringLiteral("border-radius"), QStringLiteral("10px")},
                          {QStringLiteral("background"), QStringLiteral("#2563eb")},
                          {QStringLiteral("color"), QStringLiteral("#ffffff")},
                          {QStringLiteral("font-size"), QStringLiteral("1rem")},
                          {QStringLiteral("font-weight"), QStringLiteral("600")},
                          {QStringLiteral("cursor"), QStringLiteral("pointer")}});
        submit->setAttribute(QStringLiteral("type"), QStringLiteral("submit"));
        submit->text = signup ? tr(QStringLiteral("Create account"), QStringLiteral("注册账号"))
                              : tr(QStringLiteral("Sign in"), QStringLiteral("登录"));
        card->appendChild(std::move(submit));

        if (!signup) {
            auto hint = el(QStringLiteral("a"),
                           {{QStringLiteral("font-size"), QStringLiteral("0.875rem")},
                            {QStringLiteral("color"), QStringLiteral("#2563eb")},
                            {QStringLiteral("text-decoration"), QStringLiteral("none")},
                            {QStringLiteral("text-align"), QStringLiteral("center")}});
            hint->setAttribute(QStringLiteral("href"), QStringLiteral("#"));
            hint->text = tr(QStringLiteral("Forgot your password?"),
                           QStringLiteral("忘记密码？"));
            card->appendChild(std::move(hint));
        } else {
            auto hint = el(QStringLiteral("p"),
                           {{QStringLiteral("font-size"), QStringLiteral("0.8125rem")},
                            {QStringLiteral("opacity"), QStringLiteral("0.7")},
                            {QStringLiteral("margin"), QStringLiteral("0")}});
            hint->text = tr(QStringLiteral("By continuing you agree to the terms of service."),
                           QStringLiteral("继续即表示你同意服务条款。"));
            card->appendChild(std::move(hint));
        }
        return card;
    }

    // --------------------------------------------------------------- structure
    if (id == QLatin1String("sidebar-layout")) {
        auto shell = root(QStringLiteral("div"),
                          {{QStringLiteral("display"), QStringLiteral("grid")},
                           {QStringLiteral("grid-template-columns"),
                            QStringLiteral("240px minmax(0, 1fr)")},
                           {QStringLiteral("gap"), QStringLiteral("28px")},
                           {QStringLiteral("padding"), QStringLiteral("28px")},
                           {QStringLiteral("align-items"), QStringLiteral("start")}},
                          QStringLiteral("Sidebar layout"), QStringLiteral("侧边栏布局"));
        auto sidebar = el(QStringLiteral("aside"),
                          {{QStringLiteral("display"), QStringLiteral("flex")},
                           {QStringLiteral("flex-direction"), QStringLiteral("column")},
                           {QStringLiteral("gap"), QStringLiteral("10px")},
                           {QStringLiteral("padding"), QStringLiteral("18px")},
                           {QStringLiteral("border-radius"), QStringLiteral("12px")},
                           {QStringLiteral("background"), QStringLiteral("#f8fafc")}});
        auto menu = el(QStringLiteral("ul"),
                       {{QStringLiteral("list-style"), QStringLiteral("none")},
                        {QStringLiteral("padding"), QStringLiteral("0")},
                        {QStringLiteral("margin"), QStringLiteral("0")},
                        {QStringLiteral("display"), QStringLiteral("flex")},
                        {QStringLiteral("flex-direction"), QStringLiteral("column")},
                        {QStringLiteral("gap"), QStringLiteral("8px")}});
        const QStringList itemsEn = {QStringLiteral("Overview"), QStringLiteral("Pages"),
                                     QStringLiteral("Blocks"), QStringLiteral("Settings")};
        const QStringList itemsZh = {QStringLiteral("概览"), QStringLiteral("页面"),
                                     QStringLiteral("积木"), QStringLiteral("设置")};
        const QStringList &items = zh ? itemsZh : itemsEn;
        for (const QString &text : items) {
            auto item = el(QStringLiteral("li"),
                           {{QStringLiteral("padding"), QStringLiteral("8px 10px")},
                            {QStringLiteral("border-radius"), QStringLiteral("8px")},
                            {QStringLiteral("cursor"), QStringLiteral("pointer")}});
            item->text = text;
            menu->appendChild(std::move(item));
        }
        sidebar->appendChild(std::move(menu));
        shell->appendChild(std::move(sidebar));

        auto content = el(QStringLiteral("section"),
                          {{QStringLiteral("display"), QStringLiteral("flex")},
                           {QStringLiteral("flex-direction"), QStringLiteral("column")},
                           {QStringLiteral("gap"), QStringLiteral("12px")},
                           {QStringLiteral("min-height"), QStringLiteral("320px")}});
        auto heading = el(QStringLiteral("h2"),
                          {{QStringLiteral("font-size"), QStringLiteral("1.375rem")},
                           {QStringLiteral("font-weight"), QStringLiteral("700")},
                           {QStringLiteral("margin"), QStringLiteral("0")}});
        heading->text = tr(QStringLiteral("Overview"), QStringLiteral("概览"));
        content->appendChild(std::move(heading));

        auto body = el(QStringLiteral("p"),
                       {{QStringLiteral("line-height"), QStringLiteral("1.75")},
                        {QStringLiteral("margin"), QStringLiteral("0")}});
        body->text = tr(QStringLiteral("Drop modules here to build the detail view for the selected navigation entry."),
                       QStringLiteral("把模块拖到这里，为选中的导航项构建详情视图。"));
        content->appendChild(std::move(body));
        shell->appendChild(std::move(content));
        return shell;
    }

    if (id == QLatin1String("dashboard-shell")) {
        auto shell = root(QStringLiteral("div"),
                          {{QStringLiteral("display"), QStringLiteral("flex")},
                           {QStringLiteral("flex-direction"), QStringLiteral("column")},
                           {QStringLiteral("gap"), QStringLiteral("20px")},
                           {QStringLiteral("padding"), QStringLiteral("24px")},
                           {QStringLiteral("background"), QStringLiteral("#f1f5f9")},
                           {QStringLiteral("min-height"), QStringLiteral("100vh")}},
                          QStringLiteral("Dashboard shell"), QStringLiteral("仪表盘框架"));
        auto topbar = el(QStringLiteral("header"),
                         {{QStringLiteral("display"), QStringLiteral("flex")},
                          {QStringLiteral("align-items"), QStringLiteral("center")},
                          {QStringLiteral("justify-content"), QStringLiteral("space-between")},
                          {QStringLiteral("padding"), QStringLiteral("14px 20px")},
                          {QStringLiteral("border-radius"), QStringLiteral("12px")},
                          {QStringLiteral("background"), QStringLiteral("#0f172a")},
                          {QStringLiteral("color"), QStringLiteral("#f8fafc")}});
        auto brand = el(QStringLiteral("span"),
                        {{QStringLiteral("font-weight"), QStringLiteral("700")}});
        brand->text = QStringLiteral("TC-BuildWeb");
        topbar->appendChild(std::move(brand));
        auto user = el(QStringLiteral("span"),
                       {{QStringLiteral("font-size"), QStringLiteral("0.875rem")},
                        {QStringLiteral("opacity"), QStringLiteral("0.8")}});
        user->text = tr(QStringLiteral("Lin Wei · Studio plan"),
                       QStringLiteral("林薇 · 工作室版"));
        topbar->appendChild(std::move(user));
        shell->appendChild(std::move(topbar));

        auto tiles = el(QStringLiteral("section"),
                        {{QStringLiteral("display"), QStringLiteral("grid")},
                         {QStringLiteral("grid-template-columns"),
                          QStringLiteral("repeat(auto-fit, minmax(180px, 1fr))")},
                         {QStringLiteral("gap"), QStringLiteral("16px")}});
        const QStringList tileLabelsEn = {QStringLiteral("Pages"), QStringLiteral("Elements"),
                                          QStringLiteral("Scripts"), QStringLiteral("Assets")};
        const QStringList tileLabelsZh = {QStringLiteral("页面"), QStringLiteral("元素"),
                                          QStringLiteral("脚本"), QStringLiteral("资源")};
        const QStringList tileValues = {QStringLiteral("6"), QStringLiteral("128"),
                                        QStringLiteral("9"), QStringLiteral("24")};
        const QStringList &tileLabels = zh ? tileLabelsZh : tileLabelsEn;
        for (int i = 0; i < tileValues.size(); ++i) {
            auto tile = el(QStringLiteral("div"),
                           {{QStringLiteral("display"), QStringLiteral("flex")},
                            {QStringLiteral("flex-direction"), QStringLiteral("column")},
                            {QStringLiteral("gap"), QStringLiteral("6px")},
                            {QStringLiteral("padding"), QStringLiteral("18px")},
                            {QStringLiteral("border-radius"), QStringLiteral("12px")},
                            {QStringLiteral("background"), QStringLiteral("#ffffff")},
                            {QStringLiteral("box-shadow"),
                             QStringLiteral("0 2px 10px rgba(15, 23, 42, 0.06)")}});
            auto label = el(QStringLiteral("span"),
                            {{QStringLiteral("font-size"), QStringLiteral("0.8125rem")},
                             {QStringLiteral("opacity"), QStringLiteral("0.7")}});
            label->text = tileLabels.at(i);
            tile->appendChild(std::move(label));

            auto value = el(QStringLiteral("span"),
                            {{QStringLiteral("font-size"), QStringLiteral("1.75rem")},
                             {QStringLiteral("font-weight"), QStringLiteral("800")}});
            value->text = tileValues.at(i);
            tile->appendChild(std::move(value));
            tiles->appendChild(std::move(tile));
        }
        shell->appendChild(std::move(tiles));
        return shell;
    }

    if (id == QLatin1String("table-data")) {
        auto wrapper = root(QStringLiteral("div"),
                            {{QStringLiteral("padding"), QStringLiteral("24px")},
                             {QStringLiteral("border-radius"), QStringLiteral("12px")},
                             {QStringLiteral("background"), QStringLiteral("#ffffff")},
                             {QStringLiteral("border"), QStringLiteral("1px solid #e2e8f0")},
                             {QStringLiteral("overflow"), QStringLiteral("auto")}},
                            QStringLiteral("Data table"), QStringLiteral("数据表格"));
        auto table = el(QStringLiteral("table"),
                        {{QStringLiteral("width"), QStringLiteral("100%")},
                         {QStringLiteral("border-collapse"), QStringLiteral("collapse")},
                         {QStringLiteral("font-size"), QStringLiteral("0.9375rem")}});
        auto caption = el(QStringLiteral("caption"),
                          {{QStringLiteral("text-align"), QStringLiteral("left")},
                           {QStringLiteral("font-weight"), QStringLiteral("600")},
                           {QStringLiteral("padding-bottom"), QStringLiteral("10px")}});
        caption->text = tr(QStringLiteral("Published pages"), QStringLiteral("已发布页面"));
        table->appendChild(std::move(caption));

        const QStringList headersEn = {QStringLiteral("Page"), QStringLiteral("Path"),
                                       QStringLiteral("Status"), QStringLiteral("Updated")};
        const QStringList headersZh = {QStringLiteral("页面"), QStringLiteral("路径"),
                                       QStringLiteral("状态"), QStringLiteral("更新时间")};
        const QStringList &headers = zh ? headersZh : headersEn;
        auto thead = el(QStringLiteral("thead"), {});
        auto headRow = el(QStringLiteral("tr"), {});
        for (const QString &header : headers) {
            auto cell = el(QStringLiteral("th"),
                           {{QStringLiteral("padding"), QStringLiteral("10px 12px")},
                            {QStringLiteral("text-align"), QStringLiteral("left")},
                            {QStringLiteral("background"), QStringLiteral("#f8fafc")},
                            {QStringLiteral("font-weight"), QStringLiteral("600")}});
            cell->setAttribute(QStringLiteral("scope"), QStringLiteral("col"));
            cell->text = header;
            headRow->appendChild(std::move(cell));
        }
        thead->appendChild(std::move(headRow));
        table->appendChild(std::move(thead));

        auto tbody = el(QStringLiteral("tbody"), {});
        const QStringList rows[4] = {
            {QStringLiteral("index"), QStringLiteral("/"), tr(QStringLiteral("Live"), QStringLiteral("已上线")),
             QStringLiteral("2026-10-11")},
            {QStringLiteral("pricing"), QStringLiteral("/pricing"), tr(QStringLiteral("Live"), QStringLiteral("已上线")),
             QStringLiteral("2026-10-08")},
            {QStringLiteral("docs"), QStringLiteral("/docs"), tr(QStringLiteral("Draft"), QStringLiteral("草稿")),
             QStringLiteral("2026-10-02")},
            {QStringLiteral("blog"), QStringLiteral("/blog"), tr(QStringLiteral("Review"), QStringLiteral("待审核")),
             QStringLiteral("2026-09-28")}};
        for (const QStringList &row : rows) {
            auto tr = el(QStringLiteral("tr"),
                         {{QStringLiteral("border-bottom"),
                           QStringLiteral("1px solid #e2e8f0")}});
            for (const QString &value : row) {
                auto cell = el(QStringLiteral("td"),
                               {{QStringLiteral("padding"), QStringLiteral("10px 12px")}});
                cell->text = value;
                tr->appendChild(std::move(cell));
            }
            tbody->appendChild(std::move(tr));
        }
        table->appendChild(std::move(tbody));
        wrapper->appendChild(std::move(table));
        return wrapper;
    }

    // ------------------------------------------------------- footers, overlays
    if (id == QLatin1String("footer-simple")) {
        auto footer = root(QStringLiteral("footer"),
                           {{QStringLiteral("display"), QStringLiteral("grid")},
                            {QStringLiteral("grid-template-columns"),
                             QStringLiteral("repeat(auto-fit, minmax(160px, 1fr))")},
                            {QStringLiteral("gap"), QStringLiteral("24px")},
                            {QStringLiteral("padding"), QStringLiteral("40px 28px")},
                            {QStringLiteral("background"), QStringLiteral("#0f172a")},
                            {QStringLiteral("color"), QStringLiteral("#cbd5e1")},
                            {QStringLiteral("font-size"), QStringLiteral("0.875rem")}},
                           QStringLiteral("Simple footer"), QStringLiteral("简洁页脚"));
        const QStringList groupTitlesEn = {QStringLiteral("Product"),
                                           QStringLiteral("Resources"),
                                           QStringLiteral("Company")};
        const QStringList groupTitlesZh = {QStringLiteral("产品"), QStringLiteral("资源"),
                                           QStringLiteral("公司")};
        const QStringList &groupTitles = zh ? groupTitlesZh : groupTitlesEn;
        for (int i = 0; i < groupTitles.size(); ++i) {
            auto column = el(QStringLiteral("div"),
                             {{QStringLiteral("display"), QStringLiteral("flex")},
                              {QStringLiteral("flex-direction"), QStringLiteral("column")},
                              {QStringLiteral("gap"), QStringLiteral("8px")}});
            auto title = el(QStringLiteral("span"),
                            {{QStringLiteral("font-weight"), QStringLiteral("700")},
                             {QStringLiteral("color"), QStringLiteral("#f8fafc")}});
            title->text = groupTitles.at(i);
            column->appendChild(std::move(title));

            const QStringList links = zh
                ? QStringList{QStringLiteral("功能"), QStringLiteral("价格"), QStringLiteral("更新日志")}
                : QStringList{QStringLiteral("Features"), QStringLiteral("Pricing"),
                              QStringLiteral("Changelog")};
            for (const QString &link : links) {
                auto item = el(QStringLiteral("a"),
                               {{QStringLiteral("color"), QStringLiteral("#94a3b8")},
                                {QStringLiteral("text-decoration"), QStringLiteral("none")}});
                item->setAttribute(QStringLiteral("href"), QStringLiteral("#"));
                item->text = link;
                column->appendChild(std::move(item));
            }
            footer->appendChild(std::move(column));
        }

        auto copyright = el(QStringLiteral("p"),
                            {{QStringLiteral("grid-column"), QStringLiteral("1 / -1")},
                             {QStringLiteral("margin"), QStringLiteral("12px 0 0")},
                             {QStringLiteral("opacity"), QStringLiteral("0.65")},
                             {QStringLiteral("font-size"), QStringLiteral("0.8125rem")}});
        copyright->text = tr(QStringLiteral("© 2026 Turtle Inc. Built with TCTools-Build_Web."),
                            QStringLiteral("© 2026 Turtle Inc. 由 TCTools-Build_Web 构建。"));
        footer->appendChild(std::move(copyright));
        return footer;
    }

    if (id == QLatin1String("cookie-banner")) {
        auto banner = root(QStringLiteral("div"),
                           {{QStringLiteral("position"), QStringLiteral("fixed")},
                            {QStringLiteral("inset"), QStringLiteral("auto 16px 16px 16px")},
                            {QStringLiteral("display"), QStringLiteral("flex")},
                            {QStringLiteral("align-items"), QStringLiteral("center")},
                            {QStringLiteral("justify-content"), QStringLiteral("space-between")},
                            {QStringLiteral("gap"), QStringLiteral("18px")},
                            {QStringLiteral("padding"), QStringLiteral("16px 20px")},
                            {QStringLiteral("border-radius"), QStringLiteral("12px")},
                            {QStringLiteral("background"), QStringLiteral("#0f172a")},
                            {QStringLiteral("color"), QStringLiteral("#e2e8f0")},
                            {QStringLiteral("box-shadow"),
                             QStringLiteral("0 12px 28px rgba(15, 23, 42, 0.28)")},
                            {QStringLiteral("z-index"), QStringLiteral("9999")}},
                           QStringLiteral("Cookie banner"), QStringLiteral("Cookie 提示条"));
        auto message = el(QStringLiteral("p"),
                          {{QStringLiteral("margin"), QStringLiteral("0")},
                           {QStringLiteral("font-size"), QStringLiteral("0.9375rem")},
                           {QStringLiteral("line-height"), QStringLiteral("1.55")}});
        message->text = tr(QStringLiteral("We use only local storage to remember your layout. No analytics, no tracking."),
                          QStringLiteral("我们仅使用本地存储记住你的布局，不做统计分析，也不做追踪。"));
        banner->appendChild(std::move(message));

        auto actions = el(QStringLiteral("div"),
                          {{QStringLiteral("display"), QStringLiteral("flex")},
                           {QStringLiteral("gap"), QStringLiteral("10px")},
                           {QStringLiteral("flex"), QStringLiteral("0 0 auto")}});
        auto accept = el(QStringLiteral("button"),
                         {{QStringLiteral("padding"), QStringLiteral("9px 18px")},
                          {QStringLiteral("border"), QStringLiteral("0")},
                          {QStringLiteral("border-radius"), QStringLiteral("8px")},
                          {QStringLiteral("background"), QStringLiteral("#ffffff")},
                          {QStringLiteral("color"), QStringLiteral("#0f172a")},
                          {QStringLiteral("font-weight"), QStringLiteral("600")},
                          {QStringLiteral("cursor"), QStringLiteral("pointer")}});
        accept->text = tr(QStringLiteral("Got it"), QStringLiteral("知道了"));
        actions->appendChild(std::move(accept));

        auto decline = el(QStringLiteral("button"),
                          {{QStringLiteral("padding"), QStringLiteral("9px 18px")},
                           {QStringLiteral("border"), QStringLiteral("1px solid #334155")},
                           {QStringLiteral("border-radius"), QStringLiteral("8px")},
                           {QStringLiteral("background"), QStringLiteral("transparent")},
                           {QStringLiteral("color"), QStringLiteral("#e2e8f0")},
                           {QStringLiteral("cursor"), QStringLiteral("pointer")}});
        decline->text = tr(QStringLiteral("Decline"), QStringLiteral("拒绝"));
        actions->appendChild(std::move(decline));
        banner->appendChild(std::move(actions));
        return banner;
    }

    //  Unknown composite id (the table above is the contract; this is the belt
    //  and braces fallback for a stale UI).
    //  未知组件 id（上表即契约；此处为界面过旧时的兜底）。
    return nullptr;
}

// ===========================================================================
//  Tag classification
//  标签分类
// ===========================================================================

bool ElementCatalog::isVoidTag(const QString &tag)
{
    return voidTagSet().contains(tag.trimmed().toLower());
}

bool ElementCatalog::isTextTag(const QString &tag)
{
    return textTagSet().contains(tag.trimmed().toLower());
}

bool ElementCatalog::isInlineTag(const QString &tag)
{
    return inlineTagSet().contains(tag.trimmed().toLower());
}

bool ElementCatalog::isHeadingTag(const QString &tag)
{
    return headingTagSet().contains(tag.trimmed().toLower());
}

bool ElementCatalog::isFormControl(const QString &tag)
{
    return formControlSet().contains(tag.trimmed().toLower());
}

bool ElementCatalog::isMediaTag(const QString &tag)
{
    return mediaTagSet().contains(tag.trimmed().toLower());
}

bool ElementCatalog::isHeadTag(const QString &tag)
{
    return headTagSet().contains(tag.trimmed().toLower());
}

} // namespace tcbw
