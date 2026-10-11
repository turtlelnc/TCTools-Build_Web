// ============================================================================
//  Page.cpp -- one page of a project
//  Page.cpp —— 工程中的一个页面
// ============================================================================
#include "Page.h"

#include "IdGenerator.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSet>

namespace tcbw {

namespace {

//  Maps an arbitrary label onto the character set allowed in a CSS class name.
//  把任意标签映射为 CSS 类名允许的字符集。
QString slugify(const QString &input)
{
    static const QRegularExpression nonWord(QStringLiteral("[^a-z0-9]+"));
    QString slug = input.toLower().trimmed();
    slug.replace(nonWord, QStringLiteral("-"));
    slug.remove(QRegularExpression(QStringLiteral("^-+|-+$")));
    if (slug.isEmpty())
        slug = QStringLiteral("element");
    //  Keep class names short enough for readable generated CSS.
    //  保持类名足够短，使生成的 CSS 便于阅读。
    if (slug.size() > 24)
        slug = slug.left(24);
    slug.remove(QRegularExpression(QStringLiteral("-+$")));
    return slug;
}

//  The last three hex characters of an element id, used as a stable suffix.
//  元素 id 末三位十六进制字符，用作稳定后缀。
QString shortSuffix(const QString &id)
{
    QString tail;
    for (int i = id.size() - 1; i >= 0 && tail.size() < 3; --i) {
        const QChar c = id.at(i);
        if (c.isLetterOrNumber())
            tail.prepend(c);
        else if (!tail.isEmpty())
            break;
    }
    if (tail.isEmpty())
        tail = QStringLiteral("000");
    return tail.toLower();
}

} // namespace

// ---------------------------------------------------------------------------
//  Construction
//  构造
// ---------------------------------------------------------------------------
Page::Page()
    : id(IdGenerator::pageId())
    , root(DocumentNode::makeRoot())
{
}

Page::Page(const QString &title_)
    : id(IdGenerator::pageId())
    , name(title_)
    , fileName(title_.isEmpty() ? QStringLiteral("page.html")
                                : title_ + QStringLiteral(".html"))
    , title(title_)
    , root(DocumentNode::makeRoot())
{
}

Page::~Page() = default;

std::unique_ptr<Page> Page::clone(bool keepIds) const
{
    auto copy = std::make_unique<Page>();
    copy->id = keepIds ? id : IdGenerator::pageId();
    copy->name = name;
    copy->fileName = fileName;
    copy->title = title;
    copy->isHome = isHome;
    copy->description = description;
    copy->keywords = keywords;
    copy->author = author;
    copy->language = language;
    copy->charset = charset;
    copy->viewport = viewport;
    copy->faviconUrl = faviconUrl;
    copy->headLinks = headLinks;
    copy->headScripts = headScripts;
    copy->customHeadHtml = customHeadHtml;
    copy->customBodyEndHtml = customBodyEndHtml;
    copy->cssVariables = cssVariables;
    copy->stylesheetOrder = stylesheetOrder;
    copy->m_classCounter = m_classCounter;

    //  The cloned tree always owns fresh nodes, so ids are regenerated unless
    //  the caller explicitly asked for a byte-identical copy.
    //  克隆出的树总是拥有全新的节点，因此除非调用方明确要求完全一致的副本，
    //  否则会重新生成 id。
    copy->root = root ? root->clone(keepIds) : DocumentNode::makeRoot();
    if (!copy->root)
        copy->root = DocumentNode::makeRoot();
    return copy;
}

// ---------------------------------------------------------------------------
//  Helpers
//  辅助函数
// ---------------------------------------------------------------------------

DocumentNode *Page::findNode(const QString &nodeId) const
{
    if (!root || nodeId.isEmpty())
        return nullptr;
    return root->findById(nodeId);
}

QStringList Page::allNodeIds() const
{
    QStringList ids;
    if (!root)
        return ids;
    root->forEach([&ids](const DocumentNode *node) { ids.append(node->id); });
    return ids;
}

int Page::nodeCount() const
{
    if (!root)
        return 0;
    int count = 0;
    root->forEach([&count](const DocumentNode *) { ++count; });
    return count;
}

QString Page::ensureElementClass(DocumentNode *node)
{
    if (!node)
        return QString();

    //  The node already carries a generated class: keep it so repeated calls
    //  and re-opened documents stay byte-stable.
    //  节点已经带有生成的类名：保持不变，使重复调用与重新打开的文档保持稳定。
    for (const QString &existing : node->classes) {
        if (existing.startsWith(QLatin1String("tcbw-")))
            return existing;
    }

    const QSet<QString> taken = [this] {
        QSet<QString> set;
        if (root) {
            root->forEach([&set](const DocumentNode *n) {
                for (const QString &cls : n->classes)
                    set.insert(cls);
            });
        }
        return set;
    }();

    //  Prefer the layer name, fall back to the tag, then to a generic stem.
    //  优先使用图层名，其次标签名，最后使用通用词干。
    QString stem = slugify(node->name);
    if (stem == QLatin1String("element"))
        stem = slugify(node->tag);
    const QString base = QStringLiteral("tcbw-") + stem + QLatin1Char('-')
                         + shortSuffix(node->id);

    QString candidate = base;
    int attempt = 0;
    while (taken.contains(candidate)) {
        ++attempt;
        candidate = base + QLatin1Char('-') + QString::number(++m_classCounter);
        //  Defensive: never loop forever even if the counter is pathological.
        //  防御性处理：即使计数器异常也不会死循环。
        if (attempt > 10000)
            break;
    }

    node->classes.append(candidate);
    return candidate;
}

// ---------------------------------------------------------------------------
//  Serialisation
//  序列化
// ---------------------------------------------------------------------------
QJsonObject Page::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), id);
    obj.insert(QStringLiteral("name"), name);
    obj.insert(QStringLiteral("fileName"), fileName);
    obj.insert(QStringLiteral("title"), title);
    obj.insert(QStringLiteral("isHome"), isHome);
    obj.insert(QStringLiteral("description"), description);
    obj.insert(QStringLiteral("keywords"), QJsonArray::fromStringList(keywords));
    obj.insert(QStringLiteral("author"), author);
    obj.insert(QStringLiteral("language"), language);
    obj.insert(QStringLiteral("charset"), charset);
    obj.insert(QStringLiteral("viewport"), viewport);
    obj.insert(QStringLiteral("faviconUrl"), faviconUrl);
    obj.insert(QStringLiteral("headLinks"), QJsonArray::fromStringList(headLinks));
    obj.insert(QStringLiteral("headScripts"), QJsonArray::fromStringList(headScripts));
    obj.insert(QStringLiteral("customHeadHtml"), customHeadHtml);
    obj.insert(QStringLiteral("customBodyEndHtml"), customBodyEndHtml);

    if (!cssVariables.isEmpty()) {
        QJsonObject vars;
        for (auto it = cssVariables.constBegin(); it != cssVariables.constEnd(); ++it)
            vars.insert(it.key(), it.value());
        obj.insert(QStringLiteral("cssVariables"), vars);
    }

    obj.insert(QStringLiteral("stylesheetOrder"),
               QJsonArray::fromStringList(QStringList(stylesheetOrder.begin(),
                                                      stylesheetOrder.end())));

    if (root)
        obj.insert(QStringLiteral("root"), root->toJson());
    else
        obj.insert(QStringLiteral("root"), DocumentNode::makeRoot()->toJson());

    return obj;
}

std::unique_ptr<Page> Page::fromJson(const QJsonObject &obj, QString *error)
{
    if (obj.isEmpty()) {
        if (error)
            *error = QStringLiteral("Empty page object / 页面对象为空");
        return nullptr;
    }

    //  A page without a root has no canvas to draw on: that is a hard error.
    //  没有根节点的页面无法绘制画布：这是硬错误。
    if (!obj.contains(QStringLiteral("root"))
        || !obj.value(QStringLiteral("root")).isObject()) {
        if (error)
            *error = QStringLiteral("Page is missing its root node / 页面缺少根节点");
        return nullptr;
    }

    auto page = std::make_unique<Page>();
    page->id = obj.value(QStringLiteral("id")).toString();
    if (page->id.isEmpty())
        page->id = IdGenerator::pageId();

    page->name = obj.value(QStringLiteral("name")).toString();
    page->fileName = obj.value(QStringLiteral("fileName")).toString();
    page->title = obj.value(QStringLiteral("title")).toString();
    page->isHome = obj.value(QStringLiteral("isHome")).toBool(false);
    page->description = obj.value(QStringLiteral("description")).toString();
    page->author = obj.value(QStringLiteral("author")).toString();
    page->language = obj.value(QStringLiteral("language")).toString(QStringLiteral("en"));
    page->charset = obj.value(QStringLiteral("charset")).toString(QStringLiteral("utf-8"));
    page->viewport = obj.value(QStringLiteral("viewport"))
                         .toString(QStringLiteral("width=device-width, initial-scale=1"));
    page->faviconUrl = obj.value(QStringLiteral("faviconUrl")).toString();
    page->customHeadHtml = obj.value(QStringLiteral("customHeadHtml")).toString();
    page->customBodyEndHtml = obj.value(QStringLiteral("customBodyEndHtml")).toString();

    const QJsonArray keywords = obj.value(QStringLiteral("keywords")).toArray();
    for (const QJsonValue &v : keywords) {
        if (v.isString())
            page->keywords.append(v.toString());
        else if (v.isArray())
            continue;
    }
    for (const QJsonValue &v : obj.value(QStringLiteral("headLinks")).toArray()) {
        if (v.isString())
            page->headLinks.append(v.toString());
    }
    for (const QJsonValue &v : obj.value(QStringLiteral("headScripts")).toArray()) {
        if (v.isString())
            page->headScripts.append(v.toString());
    }

    const QJsonObject vars = obj.value(QStringLiteral("cssVariables")).toObject();
    for (auto it = vars.constBegin(); it != vars.constEnd(); ++it)
        page->cssVariables.insert(it.key(), it.value().toString());

    const QJsonArray order = obj.value(QStringLiteral("stylesheetOrder")).toArray();
    for (const QJsonValue &v : order) {
        if (v.isString())
            page->stylesheetOrder.append(v.toString());
    }

    QString nodeError;
    auto rootNode = DocumentNode::fromJson(obj.value(QStringLiteral("root")).toObject(),
                                           &nodeError);
    if (!rootNode) {
        if (error)
            *error = nodeError.isEmpty()
                         ? QStringLiteral("Could not read the page tree / 无法读取页面树")
                         : nodeError;
        return nullptr;
    }

    //  The root of a page is always <body>; anything else means the file is not
    //  a page document and must not be silently accepted.
    //  页面根节点始终是 <body>；其他标签说明该文件不是页面文档，不能静默接受。
    if (rootNode->tag != QLatin1String("body")) {
        if (error) {
            *error = QStringLiteral("Page root must be <body>, found <%1> / "
                                    "页面根节点必须是 <body>，实际为 <%1>")
                         .arg(rootNode->tag);
        }
        return nullptr;
    }
    rootNode->kind = NodeKind::Root;
    rootNode->ensureIds();
    page->root = std::move(rootNode);

    if (page->fileName.isEmpty())
        page->fileName = (page->name.isEmpty() ? QStringLiteral("index") : page->name)
                         + QStringLiteral(".html");
    if (page->name.isEmpty())
        page->name = QFileInfo(page->fileName).completeBaseName();

    return page;
}

// ---------------------------------------------------------------------------
//  makeDefault -- the page a brand new project starts from
//  makeDefault —— 新建工程时的初始页面
// ---------------------------------------------------------------------------
std::unique_ptr<Page> Page::makeDefault(const QString &name, bool chineseDefaults)
{
    auto page = std::make_unique<Page>(name.isEmpty() ? QStringLiteral("index") : name);
    page->isHome = true;
    page->title = chineseDefaults ? QStringLiteral("欢迎使用 TCTools-Build_Web")
                                  : QStringLiteral("Welcome to TCTools-Build_Web");
    page->description = chineseDefaults
        ? QStringLiteral("使用 TCTools-Build_Web 可视化构建的页面。")
        : QStringLiteral("A page built visually with TCTools-Build_Web.");
    page->language = chineseDefaults ? QStringLiteral("zh") : QStringLiteral("en");
    page->cssVariables.insert(QStringLiteral("--tcbw-accent"),
                              QStringLiteral("#2563eb"));
    page->cssVariables.insert(QStringLiteral("--tcbw-surface"),
                              QStringLiteral("#ffffff"));

    if (!page->root)
        page->root = DocumentNode::makeRoot();
    page->root->style.base().set(QStringLiteral("font-family"),
                                 QStringLiteral("system-ui, -apple-system, 'Segoe UI', "
                                                "'Microsoft YaHei', sans-serif"));
    page->root->style.base().set(QStringLiteral("color"), QStringLiteral("#0f172a"));
    page->root->style.base().set(QStringLiteral("background"), QStringLiteral("#ffffff"));
    page->root->ensureIds();

    // --- <header> with an <h1> and a <p> ------------------------------------
    // --- 包含 <h1> 与 <p> 的 <header> ---------------------------------------
    auto header = DocumentNode::makeElement(QStringLiteral("header"));
    header->name = chineseDefaults ? QStringLiteral("页头") : QStringLiteral("Header");
    header->style.base().set(QStringLiteral("display"), QStringLiteral("flex"));
    header->style.base().set(QStringLiteral("flex-direction"), QStringLiteral("column"));
    header->style.base().set(QStringLiteral("gap"), QStringLiteral("10px"));
    header->style.base().set(QStringLiteral("padding"), QStringLiteral("64px 28px 48px"));
    header->style.base().set(QStringLiteral("background"),
                             QStringLiteral("linear-gradient(180deg, #eff6ff 0%, #ffffff 100%)"));
    header->classes.append(QStringLiteral("tcbw-header"));

    auto heading = DocumentNode::makeElement(QStringLiteral("h1"));
    heading->text = chineseDefaults ? QStringLiteral("欢迎使用 TCTools-Build_Web")
                                    : QStringLiteral("Welcome to TCTools-Build_Web");
    heading->style.base().set(QStringLiteral("font-size"), QStringLiteral("2.5rem"));
    heading->style.base().set(QStringLiteral("font-weight"), QStringLiteral("800"));
    heading->style.base().set(QStringLiteral("line-height"), QStringLiteral("1.2"));
    heading->style.base().set(QStringLiteral("margin"), QStringLiteral("0"));
    header->appendChild(std::move(heading));

    auto intro = DocumentNode::makeElement(QStringLiteral("p"));
    intro->text = chineseDefaults
        ? QStringLiteral("从左侧素材面板拖入元素，用积木连接交互，然后导出为 HTML。")
        : QStringLiteral("Drag elements from the palette, wire up behaviour with blocks, "
                         "then export to HTML.");
    intro->style.base().set(QStringLiteral("font-size"), QStringLiteral("1.125rem"));
    intro->style.base().set(QStringLiteral("line-height"), QStringLiteral("1.7"));
    intro->style.base().set(QStringLiteral("max-width"), QStringLiteral("64ch"));
    intro->style.base().set(QStringLiteral("opacity"), QStringLiteral("0.82"));
    intro->style.base().set(QStringLiteral("margin"), QStringLiteral("0"));
    header->appendChild(std::move(intro));
    DocumentNode *headerNode = page->root->appendChild(std::move(header));

    // --- <main> section -----------------------------------------------------
    // --- <main> 区块 --------------------------------------------------------
    auto main = DocumentNode::makeElement(QStringLiteral("main"));
    main->name = chineseDefaults ? QStringLiteral("主内容") : QStringLiteral("Main");
    main->style.base().set(QStringLiteral("display"), QStringLiteral("block"));
    main->style.base().set(QStringLiteral("padding"), QStringLiteral("40px 28px"));
    main->classes.append(QStringLiteral("tcbw-main"));

    auto section = DocumentNode::makeElement(QStringLiteral("section"));
    section->name = chineseDefaults ? QStringLiteral("介绍区块") : QStringLiteral("Intro section");
    section->style.base().set(QStringLiteral("display"), QStringLiteral("flex"));
    section->style.base().set(QStringLiteral("flex-direction"), QStringLiteral("column"));
    section->style.base().set(QStringLiteral("gap"), QStringLiteral("14px"));
    section->style.base().set(QStringLiteral("max-width"), QStringLiteral("72ch"));

    auto sectionHeading = DocumentNode::makeElement(QStringLiteral("h2"));
    sectionHeading->text = chineseDefaults ? QStringLiteral("开始构建你的第一个页面")
                                           : QStringLiteral("Start building your first page");
    sectionHeading->style.base().set(QStringLiteral("font-size"), QStringLiteral("1.75rem"));
    sectionHeading->style.base().set(QStringLiteral("font-weight"), QStringLiteral("700"));
    sectionHeading->style.base().set(QStringLiteral("margin"), QStringLiteral("0"));
    section->appendChild(std::move(sectionHeading));

    auto sectionBody = DocumentNode::makeElement(QStringLiteral("p"));
    sectionBody->text = chineseDefaults
        ? QStringLiteral("这个页面是自动生成的示例。可以直接编辑文字、调整样式，"
                         "或者从素材面板添加更多元素。")
        : QStringLiteral("This page was generated for you. Edit the copy, tweak the styles, "
                         "or add more elements from the palette.");
    sectionBody->style.base().set(QStringLiteral("line-height"), QStringLiteral("1.75"));
    sectionBody->style.base().set(QStringLiteral("margin"), QStringLiteral("0"));
    section->appendChild(std::move(sectionBody));

    auto button = DocumentNode::makeElement(QStringLiteral("button"));
    button->text = chineseDefaults ? QStringLiteral("立即开始") : QStringLiteral("Get started");
    button->setAttribute(QStringLiteral("type"), QStringLiteral("button"));
    button->style.base().set(QStringLiteral("align-self"), QStringLiteral("flex-start"));
    button->style.base().set(QStringLiteral("padding"), QStringLiteral("11px 22px"));
    button->style.base().set(QStringLiteral("border"), QStringLiteral("0"));
    button->style.base().set(QStringLiteral("border-radius"), QStringLiteral("10px"));
    button->style.base().set(QStringLiteral("background"), QStringLiteral("#2563eb"));
    button->style.base().set(QStringLiteral("color"), QStringLiteral("#ffffff"));
    button->style.base().set(QStringLiteral("font-size"), QStringLiteral("1rem"));
    button->style.base().set(QStringLiteral("font-weight"), QStringLiteral("600"));
    button->style.base().set(QStringLiteral("cursor"), QStringLiteral("pointer"));
    button->data.insert(QStringLiteral("role"), QStringLiteral("primary-action"));
    section->appendChild(std::move(button));

    main->appendChild(std::move(section));
    DocumentNode *mainNode = page->root->appendChild(std::move(main));

    //  Give the structural nodes deterministic, readable classes.
    //  为结构节点生成确定、可读的类名。
    if (headerNode)
        page->ensureElementClass(headerNode);
    if (mainNode)
        page->ensureElementClass(mainNode);
    page->ensureElementClass(page->root.get());

    return page;
}

} // namespace tcbw
