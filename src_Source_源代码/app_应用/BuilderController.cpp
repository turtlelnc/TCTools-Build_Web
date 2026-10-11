// ============================================================================
//  BuilderController.cpp -- visual editing behind the canvas
//  BuilderController.cpp —— 画布背后的可视化编辑
//
//  Part of TCTools-Build_Web (TC-BuildWeb).
//  属于 TCTools-Build_Web（TC-BuildWeb）。
// ============================================================================
#include "BuilderController.h"

#include "AppController.h"
#include "AppSettings.h"
#include "IdGenerator.h"
#include "StyleSheet.h"
#include "Types.h"

#include <QStringList>
#include <QVariant>

#include <algorithm>

namespace tcbw {

namespace {

// ---------------------------------------------------------------------------
//  Hard limits
//  A corrupt document, a runaway AI loop or a hostile clipboard payload must
//  never be able to hang or crash the user interface.
//  硬性上限
//  损坏的文档、失控的 AI 循环或恶意的剪贴板内容，绝不能挂起或崩溃用户界面。
// ---------------------------------------------------------------------------
constexpr int kMaxNodeDepth = 64;          // recursive serialisation depth / 递归序列化深度
constexpr int kMaxPageNodes = 5000;        // elements per page / 每页元素数
constexpr int kMaxClipboardNodes = 2000;   // clipboard subtree size / 剪贴板子树大小
constexpr int kMaxLayerRows = 20000;       // rows in the layers panel / 图层面板行数

// ---------------------------------------------------------------------------
//  Dynamic-property keys (the public header is frozen).
//  动态属性键（公开头文件已冻结）。
// ---------------------------------------------------------------------------
constexpr char kClipboardSourceProperty[] = "_tcbwClipboardSourceId";
constexpr char kBoundedDocumentProperty[] = "_tcbwBoundDocument";

// ---------------------------------------------------------------------------
//  Canvas sizes. They mirror the breakpoint minimums declared in StyleSheet.h
//  so the artboard always matches the media query the generator emits.
//  画布尺寸。它们与 StyleSheet.h 中声明的断点最小值保持一致，
//  使画板始终与生成器输出的媒体查询相符。
// ---------------------------------------------------------------------------
constexpr int kDesktopWidth = 1440;
constexpr int kDesktopHeight = 900;
constexpr int kTabletWidth = 834;
constexpr int kTabletHeight = 1112;
constexpr int kMobileWidth = 390;
constexpr int kMobileHeight = 844;

// ---------------------------------------------------------------------------
//  Returns the number of nodes in a subtree, including its root.
//  返回子树中的节点数量（包含其根节点）。
// ---------------------------------------------------------------------------
int subtreeCount(const DocumentNode *root)
{
    if (!root)
        return 0;
    int count = 0;
    QVector<const DocumentNode *> pending;
    pending.append(root);
    while (!pending.isEmpty()) {
        const DocumentNode *current = pending.takeLast();
        ++count;
        for (int i = current->childCount() - 1; i >= 0; --i) {
            if (const DocumentNode *child = current->childAt(i))
                pending.append(child);
        }
    }
    return count;
}

// ---------------------------------------------------------------------------
//  A palette / element-picker entry.
//  素材面板 / 元素选择器的条目。
// ---------------------------------------------------------------------------
QVariantMap elementItemMap(const ElementDef &def)
{
    QVariantMap item;
    item.insert(QStringLiteral("tag"), def.tag);
    item.insert(QStringLiteral("labelEn"), def.labelEn);
    item.insert(QStringLiteral("labelZh"), def.labelZh);
    item.insert(QStringLiteral("groupEn"), def.groupEn);
    item.insert(QStringLiteral("groupZh"), def.groupZh);
    item.insert(QStringLiteral("descriptionEn"), def.descriptionEn);
    item.insert(QStringLiteral("descriptionZh"), def.descriptionZh);
    item.insert(QStringLiteral("icon"), def.icon);
    item.insert(QStringLiteral("isVoid"), def.isVoid);
    item.insert(QStringLiteral("allowsChildren"), def.allowsChildren);
    item.insert(QStringLiteral("order"), def.order);
    return item;
}

// ---------------------------------------------------------------------------
//  Every catalogue element, ordered by category and then by the catalogue's own
//  display order, so the palette, the category filter and the search results all
//  agree on one sequence.
//  全部目录元素，先按分类排序再按目录自身的显示顺序排序，使素材面板、分类筛选与
//  搜索结果共用同一顺序。
// ---------------------------------------------------------------------------
QVector<const ElementDef *> sortedElementDefs()
{
    QVector<const ElementDef *> defs;
    const ElementCatalog &catalog = ElementCatalog::instance();
    defs.reserve(catalog.elements().size());
    for (const ElementDef &def : catalog.elements())
        defs.append(&def);

    std::stable_sort(defs.begin(), defs.end(),
                     [](const ElementDef *a, const ElementDef *b) {
                         if (a->category != b->category)
                             return static_cast<int>(a->category) < static_cast<int>(b->category);
                         if (a->order != b->order)
                             return a->order < b->order;
                         return a->tag < b->tag;
                     });
    return defs;
}

bool matchesNeedle(const ElementDef &def, const QString &needle)
{
    return def.tag.contains(needle, Qt::CaseInsensitive)
           || def.labelEn.contains(needle, Qt::CaseInsensitive)
           || def.labelZh.contains(needle, Qt::CaseInsensitive)
           || def.groupEn.contains(needle, Qt::CaseInsensitive)
           || def.groupZh.contains(needle, Qt::CaseInsensitive)
           || def.descriptionEn.contains(needle, Qt::CaseInsensitive)
           || def.descriptionZh.contains(needle, Qt::CaseInsensitive);
}

// ---------------------------------------------------------------------------
//  Icon key for a tag, falling back to a generic key the UI understands.
//  标签对应的图标键，找不到时回退到界面已知的通用键。
// ---------------------------------------------------------------------------
QString iconForTag(const QString &tag)
{
    if (const ElementDef *def = ElementCatalog::instance().find(tag))
        return def->icon;
    return QStringLiteral("element");
}

QString noOpenPageMessage()
{
    return QStringLiteral("No page is open to edit. / 没有可编辑的页面。");
}

// ---------------------------------------------------------------------------
//  One CSS declaration inside a style preset.
//  样式预设中的一条 CSS 声明。
// ---------------------------------------------------------------------------
struct Declaration
{
    QString property;
    QString value;
};

// ---------------------------------------------------------------------------
//  One curated style preset.
//  一个精心设计的样式预设。
// ---------------------------------------------------------------------------
struct StylePreset
{
    QString id;
    QString labelEn;
    QString labelZh;
    QString groupEn;
    QString groupZh;
    QVector<Declaration> declarations;
};

// ---------------------------------------------------------------------------
//  The preset catalogue: 18 coherent looks an author can apply in one click.
//  预设目录：18 套协调的外观，作者一键即可应用。
// ---------------------------------------------------------------------------
const QVector<StylePreset> &stylePresetTable()
{
    static const QVector<StylePreset> table = {
        // --- typography / 排版 ---------------------------------------------
        {QStringLiteral("heading-xl"), QStringLiteral("Display heading"),
         QStringLiteral("超大标题"), QStringLiteral("Typography"), QStringLiteral("排版"),
         {{QStringLiteral("font-size"), QStringLiteral("3rem")},
          {QStringLiteral("font-weight"), QStringLiteral("700")},
          {QStringLiteral("line-height"), QStringLiteral("1.1")},
          {QStringLiteral("letter-spacing"), QStringLiteral("-0.02em")},
          {QStringLiteral("margin"), QStringLiteral("0 0 0.5em 0")}}},
        {QStringLiteral("heading-lg"), QStringLiteral("Section heading"),
         QStringLiteral("区块标题"), QStringLiteral("Typography"), QStringLiteral("排版"),
         {{QStringLiteral("font-size"), QStringLiteral("2rem")},
          {QStringLiteral("font-weight"), QStringLiteral("600")},
          {QStringLiteral("line-height"), QStringLiteral("1.2")},
          {QStringLiteral("letter-spacing"), QStringLiteral("-0.01em")},
          {QStringLiteral("margin"), QStringLiteral("0 0 0.5em 0")}}},
        {QStringLiteral("body-text"), QStringLiteral("Body text"), QStringLiteral("正文"),
         QStringLiteral("Typography"), QStringLiteral("排版"),
         {{QStringLiteral("font-size"), QStringLiteral("1rem")},
          {QStringLiteral("line-height"), QStringLiteral("1.6")},
          {QStringLiteral("margin"), QStringLiteral("0 0 1em 0")}}},
        {QStringLiteral("muted-text"), QStringLiteral("Muted text"), QStringLiteral("弱化文字"),
         QStringLiteral("Typography"), QStringLiteral("排版"),
         {{QStringLiteral("font-size"), QStringLiteral("0.9rem")},
          {QStringLiteral("line-height"), QStringLiteral("1.5")},
          {QStringLiteral("color"), QStringLiteral("#6b7280")},
          {QStringLiteral("opacity"), QStringLiteral("0.85")}}},
        {QStringLiteral("link"), QStringLiteral("Text link"), QStringLiteral("文字链接"),
         QStringLiteral("Typography"), QStringLiteral("排版"),
         {{QStringLiteral("color"), QStringLiteral("#0a6cff")},
          {QStringLiteral("font-weight"), QStringLiteral("500")},
          {QStringLiteral("text-decoration"), QStringLiteral("underline")},
          {QStringLiteral("text-underline-offset"), QStringLiteral("0.15em")},
          {QStringLiteral("cursor"), QStringLiteral("pointer")}}},

        // --- buttons and pills / 按钮与胶囊 ---------------------------------
        {QStringLiteral("button-primary"), QStringLiteral("Primary button"),
         QStringLiteral("主要按钮"), QStringLiteral("Buttons"), QStringLiteral("按钮"),
         {{QStringLiteral("display"), QStringLiteral("inline-flex")},
          {QStringLiteral("align-items"), QStringLiteral("center")},
          {QStringLiteral("justify-content"), QStringLiteral("center")},
          {QStringLiteral("gap"), QStringLiteral("0.5rem")},
          {QStringLiteral("padding"), QStringLiteral("0.65rem 1.3rem")},
          {QStringLiteral("border"), QStringLiteral("1px solid transparent")},
          {QStringLiteral("border-radius"), QStringLiteral("999px")},
          {QStringLiteral("background-color"), QStringLiteral("#0a6cff")},
          {QStringLiteral("color"), QStringLiteral("#ffffff")},
          {QStringLiteral("font-weight"), QStringLiteral("600")},
          {QStringLiteral("text-decoration"), QStringLiteral("none")},
          {QStringLiteral("cursor"), QStringLiteral("pointer")}}},
        {QStringLiteral("button-secondary"), QStringLiteral("Secondary button"),
         QStringLiteral("次要按钮"), QStringLiteral("Buttons"), QStringLiteral("按钮"),
         {{QStringLiteral("display"), QStringLiteral("inline-flex")},
          {QStringLiteral("align-items"), QStringLiteral("center")},
          {QStringLiteral("justify-content"), QStringLiteral("center")},
          {QStringLiteral("gap"), QStringLiteral("0.5rem")},
          {QStringLiteral("padding"), QStringLiteral("0.65rem 1.3rem")},
          {QStringLiteral("border"), QStringLiteral("1px solid currentColor")},
          {QStringLiteral("border-radius"), QStringLiteral("999px")},
          {QStringLiteral("background-color"), QStringLiteral("transparent")},
          {QStringLiteral("color"), QStringLiteral("#0a6cff")},
          {QStringLiteral("font-weight"), QStringLiteral("600")},
          {QStringLiteral("text-decoration"), QStringLiteral("none")},
          {QStringLiteral("cursor"), QStringLiteral("pointer")}}},
        {QStringLiteral("pill"), QStringLiteral("Pill badge"), QStringLiteral("胶囊标签"),
         QStringLiteral("Buttons"), QStringLiteral("按钮"),
         {{QStringLiteral("display"), QStringLiteral("inline-flex")},
          {QStringLiteral("align-items"), QStringLiteral("center")},
          {QStringLiteral("padding"), QStringLiteral("0.25rem 0.75rem")},
          {QStringLiteral("border-radius"), QStringLiteral("999px")},
          {QStringLiteral("background-color"), QStringLiteral("rgba(10, 108, 255, 0.12)")},
          {QStringLiteral("color"), QStringLiteral("#0a6cff")},
          {QStringLiteral("font-size"), QStringLiteral("0.8rem")},
          {QStringLiteral("font-weight"), QStringLiteral("600")},
          {QStringLiteral("line-height"), QStringLiteral("1.4")}}},

        // --- surfaces / 表面 -----------------------------------------------
        {QStringLiteral("card"), QStringLiteral("Card"), QStringLiteral("卡片"),
         QStringLiteral("Surfaces"), QStringLiteral("表面"),
         {{QStringLiteral("background-color"), QStringLiteral("#ffffff")},
          {QStringLiteral("border"), QStringLiteral("1px solid rgba(15, 23, 42, 0.08)")},
          {QStringLiteral("border-radius"), QStringLiteral("16px")},
          {QStringLiteral("padding"), QStringLiteral("1.5rem")},
          {QStringLiteral("box-shadow"),
           QStringLiteral("0 1px 2px rgba(15, 23, 42, 0.06), 0 10px 30px rgba(15, 23, 42, 0.08)")}}},
        {QStringLiteral("glass-card"), QStringLiteral("Glass card"), QStringLiteral("玻璃卡片"),
         QStringLiteral("Surfaces"), QStringLiteral("表面"),
         {{QStringLiteral("background-color"), QStringLiteral("rgba(255, 255, 255, 0.6)")},
          {QStringLiteral("border"), QStringLiteral("1px solid rgba(255, 255, 255, 0.45)")},
          {QStringLiteral("border-radius"), QStringLiteral("20px")},
          {QStringLiteral("padding"), QStringLiteral("1.5rem")},
          {QStringLiteral("box-shadow"), QStringLiteral("0 8px 32px rgba(15, 23, 42, 0.12)")},
          {QStringLiteral("backdrop-filter"), QStringLiteral("blur(14px) saturate(160%)")},
          {QStringLiteral("-webkit-backdrop-filter"),
           QStringLiteral("blur(14px) saturate(160%)")}}},
        {QStringLiteral("shadow-soft"), QStringLiteral("Soft shadow"), QStringLiteral("柔和阴影"),
         QStringLiteral("Surfaces"), QStringLiteral("表面"),
         {{QStringLiteral("box-shadow"),
           QStringLiteral("0 1px 2px rgba(15, 23, 42, 0.06), 0 8px 24px rgba(15, 23, 42, 0.08)")}}},
        {QStringLiteral("shadow-strong"), QStringLiteral("Strong shadow"),
         QStringLiteral("强烈阴影"), QStringLiteral("Surfaces"), QStringLiteral("表面"),
         {{QStringLiteral("box-shadow"),
           QStringLiteral("0 2px 4px rgba(15, 23, 42, 0.10), 0 18px 48px rgba(15, 23, 42, 0.18)")}}},
        {QStringLiteral("rounded-lg"), QStringLiteral("Large radius"), QStringLiteral("大圆角"),
         QStringLiteral("Surfaces"), QStringLiteral("表面"),
         {{QStringLiteral("border-radius"), QStringLiteral("16px")}}},
        {QStringLiteral("rounded-full"), QStringLiteral("Full radius"), QStringLiteral("全圆角"),
         QStringLiteral("Surfaces"), QStringLiteral("表面"),
         {{QStringLiteral("border-radius"), QStringLiteral("9999px")}}},

        // --- layout / 布局 --------------------------------------------------
        {QStringLiteral("full-bleed"), QStringLiteral("Full bleed"), QStringLiteral("满幅"),
         QStringLiteral("Layout"), QStringLiteral("布局"),
         {{QStringLiteral("width"), QStringLiteral("100vw")},
          {QStringLiteral("max-width"), QStringLiteral("none")},
          {QStringLiteral("margin-left"), QStringLiteral("calc(50% - 50vw)")},
          {QStringLiteral("margin-right"), QStringLiteral("calc(50% - 50vw)")}}},
        {QStringLiteral("centered-column"), QStringLiteral("Centered column"),
         QStringLiteral("居中单列"), QStringLiteral("Layout"), QStringLiteral("布局"),
         {{QStringLiteral("display"), QStringLiteral("flex")},
          {QStringLiteral("flex-direction"), QStringLiteral("column")},
          {QStringLiteral("align-items"), QStringLiteral("center")},
          {QStringLiteral("justify-content"), QStringLiteral("center")},
          {QStringLiteral("gap"), QStringLiteral("1.5rem")},
          {QStringLiteral("max-width"), QStringLiteral("72rem")},
          {QStringLiteral("margin-left"), QStringLiteral("auto")},
          {QStringLiteral("margin-right"), QStringLiteral("auto")},
          {QStringLiteral("padding"), QStringLiteral("4rem 1.5rem")}}},
        {QStringLiteral("grid-3"), QStringLiteral("Three column grid"),
         QStringLiteral("三列网格"), QStringLiteral("Layout"), QStringLiteral("布局"),
         {{QStringLiteral("display"), QStringLiteral("grid")},
          {QStringLiteral("grid-template-columns"),
           QStringLiteral("repeat(auto-fit, minmax(240px, 1fr))")},
          {QStringLiteral("gap"), QStringLiteral("1.5rem")},
          {QStringLiteral("align-items"), QStringLiteral("start")}}},
        {QStringLiteral("flex-row-center"), QStringLiteral("Centered row"),
         QStringLiteral("居中横排"), QStringLiteral("Layout"), QStringLiteral("布局"),
         {{QStringLiteral("display"), QStringLiteral("flex")},
          {QStringLiteral("flex-direction"), QStringLiteral("row")},
          {QStringLiteral("flex-wrap"), QStringLiteral("wrap")},
          {QStringLiteral("align-items"), QStringLiteral("center")},
          {QStringLiteral("justify-content"), QStringLiteral("center")},
          {QStringLiteral("gap"), QStringLiteral("1rem")}}},
    };
    return table;
}

const StylePreset *findStylePreset(const QString &presetId)
{
    const QString wanted = presetId.trimmed();
    if (wanted.isEmpty())
        return nullptr;
    const QVector<StylePreset> &table = stylePresetTable();
    for (const StylePreset &preset : table) {
        if (preset.id == wanted)
            return &preset;
    }
    return nullptr;
}

} // namespace

// ---------------------------------------------------------------------------
//  Construction
//  构造
// ---------------------------------------------------------------------------
BuilderController::BuilderController(AppController *app, QObject *parent)
    : QObject(parent)
{
    //  The grid preferences live in AppSettings and are shared with the canvas
    //  overlay; the controller mirrors them so QML can bind to plain properties.
    //  网格偏好保存在 AppSettings 中并与画布叠加层共享；控制器镜像它们，
    //  使 QML 可以绑定到普通属性。
    const AppSettings &settings = AppSettings::instance();
    m_gridVisible = settings.showGrid();
    m_gridSize = qMax(1, settings.gridSize());
    m_snapping = settings.snapToGrid();

    setAppController(app);
}

BuilderController::~BuilderController() = default;

void BuilderController::setAppController(AppController *app)
{
    if (m_app == app)
        return;
    if (m_app)
        m_app->disconnect(this);

    m_app = app;
    if (!m_app)
        return;

    //  The document object is replaced whenever the user creates or opens a
    //  project, so the document signals are (re)bound on every projectChanged().
    //  用户新建或打开工程时文档对象会被替换，因此每次 projectChanged() 都会
    //  （重新）绑定文档信号。
    //  Returns true when the document object actually changed, so that a plain
    //  rename (which also emits projectChanged()) does not disturb the selection.
    //  当文档对象确实发生变化时返回 true，使单纯的改名（同样会发出
    //  projectChanged()）不会打扰当前选择。
    auto bindDocument = [this]() -> bool {
        ProjectDocument *document = m_app ? m_app->document() : nullptr;
        QObject *previous = property(kBoundedDocumentProperty).value<QObject *>();
        if (previous == document)
            return false;
        if (previous)
            previous->disconnect(this);
        setProperty(kBoundedDocumentProperty,
                    QVariant::fromValue(static_cast<QObject *>(document)));
        if (!document)
            return true;
        connect(document, &ProjectDocument::structureChanged, this,
                &BuilderController::onStructureChanged);
        connect(document, &ProjectDocument::nodeChanged, this, &BuilderController::onNodeChanged);
        connect(document, &ProjectDocument::pageChanged, this, &BuilderController::onPageChanged);
        return true;
    };

    bindDocument();

    connect(m_app, &AppController::projectChanged, this, [this, bindDocument]() {
        if (!bindDocument())
            return;
        //  A new document cannot contain the previously selected node. The
        //  clipboard survives on purpose: copying an element in one project and
        //  pasting it into another is a legitimate workflow, and a stale source
        //  id simply disables the cycle guard (there is no cycle to guard
        //  against once the source is gone).
        //  新文档不可能包含之前选中的节点。剪贴板刻意保留：在一个工程中复制元素、
        //  再粘贴到另一个工程是合理的工作流；而过期的源 id 只会让循环防护失效
        //  （源已不存在，也就没有环需要防护）。
        m_selectedNodeId.clear();
        emit layerTreeChanged();
        emit selectionChanged();
        emit requestRepaint();
        emit canvasSettingsChanged();
    });

    //  The canvas size follows the shell-wide breakpoint.
    //  画布尺寸随整个外壳的断点变化。
    connect(m_app, &AppController::breakpointChanged, this, [this](const QString &) {
        emit canvasSettingsChanged();
        emit requestRepaint();
    });
}

// ---------------------------------------------------------------------------
//  Small lookups
//  小型查询
// ---------------------------------------------------------------------------
Page *BuilderController::currentPage() const
{
    if (!m_app)
        return nullptr;
    ProjectDocument *document = m_app->document();
    if (!document)
        return nullptr;
    if (Page *page = document->pageById(document->activePageId))
        return page;
    if (Page *page = document->homePage())
        return page;
    return document->pageAt(0);
}

CommandStack *BuilderController::stack() const
{
    return m_app ? m_app->commands() : nullptr;
}

DocumentNode *BuilderController::node(const QString &nodeId) const
{
    Page *page = currentPage();
    if (!page || !page->root || nodeId.isEmpty())
        return nullptr;
    return page->root->findById(nodeId);
}

QString BuilderController::labelEn(const QString &key) const
{
    if (key.isEmpty())
        return QString();
    if (const ElementDef *def = ElementCatalog::instance().find(key))
        return def->labelEn.isEmpty() ? def->tag : def->labelEn;
    return key;
}

QString BuilderController::labelZh(const QString &key) const
{
    if (key.isEmpty())
        return QString();
    if (const ElementDef *def = ElementCatalog::instance().find(key))
        return def->labelZh.isEmpty() ? def->tag : def->labelZh;
    return key;
}

// ---------------------------------------------------------------------------
//  Selection
//  选择
// ---------------------------------------------------------------------------
QVariantMap BuilderController::selectedNode() const
{
    //  The selection is cleared by the structure/node handlers; a getter cannot
    //  mutate it, so a stale id simply yields an empty map here.
    //  选择由结构 / 节点变更处理函数清除；取值函数无法修改它，
    //  因此过期的 id 在此只会返回空映射。
    DocumentNode *current = node(m_selectedNodeId);
    if (!current)
        return QVariantMap();
    return nodeToVariant(current, false);
}

QVariantList BuilderController::selectedPath() const
{
    QVariantList path;
    DocumentNode *current = node(m_selectedNodeId);
    if (!current)
        return path;
    Page *page = currentPage();
    const DocumentNode *root = page ? page->root.get() : nullptr;

    for (DocumentNode *cursor = current; cursor != nullptr; cursor = cursor->parent()) {
        QVariantMap entry;
        entry.insert(QStringLiteral("id"), cursor->id);
        entry.insert(QStringLiteral("tag"), cursor->tag);
        entry.insert(QStringLiteral("name"), cursor->name);
        entry.insert(QStringLiteral("kind"), toString(cursor->kind));
        entry.insert(QStringLiteral("labelEn"),
                     cursor->name.isEmpty() ? labelEn(cursor->tag) : cursor->name);
        entry.insert(QStringLiteral("labelZh"),
                     cursor->name.isEmpty() ? labelZh(cursor->tag) : cursor->name);
        entry.insert(QStringLiteral("childCount"), cursor->childCount());
        entry.insert(QStringLiteral("hasChildren"), cursor->childCount() > 0);
        entry.insert(QStringLiteral("isRoot"), root != nullptr && cursor == root);
        path.prepend(entry);
    }
    return path;
}

QVariantList BuilderController::breadcrumbs() const
{
    QVariantList crumbs;
    DocumentNode *current = node(m_selectedNodeId);
    if (!current)
        return crumbs;
    Page *page = currentPage();
    const DocumentNode *root = page ? page->root.get() : nullptr;

    for (DocumentNode *cursor = current; cursor != nullptr; cursor = cursor->parent()) {
        QVariantMap crumb;
        crumb.insert(QStringLiteral("id"), cursor->id);
        crumb.insert(QStringLiteral("tag"), cursor->tag);
        crumb.insert(QStringLiteral("name"), cursor->name);
        crumb.insert(QStringLiteral("labelEn"),
                     cursor->name.isEmpty() ? labelEn(cursor->tag) : cursor->name);
        crumb.insert(QStringLiteral("labelZh"),
                     cursor->name.isEmpty() ? labelZh(cursor->tag) : cursor->name);
        crumb.insert(QStringLiteral("isRoot"), root != nullptr && cursor == root);
        crumbs.prepend(crumb);
    }
    return crumbs;
}

void BuilderController::selectNode(const QString &nodeId)
{
    if (nodeId == m_selectedNodeId)
        return;
    //  An id that does not resolve is ignored instead of stored: a stale id from
    //  QML must never leave the inspector pointing at nothing.
    //  无法解析的 id 会被忽略而不是保存：来自 QML 的过期 id 绝不能使检查器指向空。
    if (!nodeId.isEmpty() && !node(nodeId))
        return;
    m_selectedNodeId = nodeId;
    emit selectionChanged();
}

void BuilderController::clearSelection()
{
    if (m_selectedNodeId.isEmpty())
        return;
    m_selectedNodeId.clear();
    emit selectionChanged();
}

void BuilderController::selectParent()
{
    DocumentNode *current = node(m_selectedNodeId);
    if (!current)
        return;
    if (DocumentNode *parent = current->parent())
        selectNode(parent->id);
}

void BuilderController::selectNextSibling()
{
    DocumentNode *current = node(m_selectedNodeId);
    if (!current)
        return;
    DocumentNode *parent = current->parent();
    if (!parent)
        return;
    const int index = current->indexInParent();
    if (index < 0 || index + 1 >= parent->childCount())
        return;
    if (DocumentNode *sibling = parent->childAt(index + 1))
        selectNode(sibling->id);
}

void BuilderController::selectPreviousSibling()
{
    DocumentNode *current = node(m_selectedNodeId);
    if (!current)
        return;
    DocumentNode *parent = current->parent();
    if (!parent)
        return;
    const int index = current->indexInParent();
    if (index <= 0)
        return;
    if (DocumentNode *sibling = parent->childAt(index - 1))
        selectNode(sibling->id);
}

// ---------------------------------------------------------------------------
//  Trees
//  树
// ---------------------------------------------------------------------------
QVariantMap BuilderController::nodeToVariant(const DocumentNode *n, bool deep) const
{
    QVariantMap map;
    if (!n)
        return map;

    //  The recursion is capped so that a corrupt or hostile document can never
    //  overflow the stack of the user interface thread.
    //  递归深度受到限制，使损坏或恶意的文档永远不会溢出界面线程的栈。
    auto build = [this, deep](const DocumentNode *current, int depth,
                              const auto &self) -> QVariantMap {
        QVariantMap out;
        if (!current)
            return out;

        out.insert(QStringLiteral("id"), current->id);
        out.insert(QStringLiteral("tag"), current->tag);
        out.insert(QStringLiteral("kind"), toString(current->kind));
        out.insert(QStringLiteral("name"), current->name);
        out.insert(QStringLiteral("labelEn"),
                   current->name.isEmpty() ? labelEn(current->tag) : current->name);
        out.insert(QStringLiteral("labelZh"),
                   current->name.isEmpty() ? labelZh(current->tag) : current->name);
        out.insert(QStringLiteral("text"), current->text);
        out.insert(QStringLiteral("classes"), current->classes);
        out.insert(QStringLiteral("layoutMode"), toString(current->layoutMode));
        out.insert(QStringLiteral("locked"), current->locked);
        out.insert(QStringLiteral("hidden"), current->hidden);
        out.insert(QStringLiteral("expanded"), current->expanded);
        out.insert(QStringLiteral("childCount"), current->childCount());
        out.insert(QStringLiteral("hasChildren"), current->childCount() > 0);

        QVariantMap attributes;
        const QMap<QString, QString> effective = current->effectiveAttributes();
        for (auto it = effective.constBegin(); it != effective.constEnd(); ++it)
            attributes.insert(it.key(), it.value());
        out.insert(QStringLiteral("attributes"), attributes);

        //  The inspector shows what the page really renders, which is the
        //  resolved declaration map for the breakpoint being previewed.
        //  检查器展示页面真正渲染的结果，即当前预览断点下解析后的声明表。
        const Breakpoint breakpoint = m_app ? m_app->activeBreakpoint() : Breakpoint::Desktop;
        QVariantMap style;
        const QMap<QString, QString> resolved = current->style.resolved(breakpoint);
        for (auto it = resolved.constBegin(); it != resolved.constEnd(); ++it)
            style.insert(it.key(), it.value());
        out.insert(QStringLiteral("style"), style);

        QVariantMap freeRect;
        freeRect.insert(QStringLiteral("x"), current->freeRect.x());
        freeRect.insert(QStringLiteral("y"), current->freeRect.y());
        freeRect.insert(QStringLiteral("w"), current->freeRect.width());
        freeRect.insert(QStringLiteral("h"), current->freeRect.height());
        out.insert(QStringLiteral("freeRect"), freeRect);

        out.insert(QStringLiteral("data"), current->data.toVariantMap());
        out.insert(QStringLiteral("rotation"), current->rotation);
        out.insert(QStringLiteral("isVoid"), current->isVoidElement());

        if (deep && depth < kMaxNodeDepth) {
            QVariantList children;
            children.reserve(current->childCount());
            for (const std::unique_ptr<DocumentNode> &child : current->children())
                children.append(self(child.get(), depth + 1, self));
            out.insert(QStringLiteral("children"), children);
        }
        return out;
    };

    map = build(n, 0, build);
    return map;
}

QVariantList BuilderController::layerTree() const
{
    QVariantList rows;
    Page *page = currentPage();
    if (!page || !page->root)
        return rows;

    //  An explicit stack keeps the traversal safe for arbitrarily deep trees;
    //  children are pushed in reverse so the rows come out in document order.
    //  显式栈使遍历对任意深度的树都安全；子节点逆序压栈，使输出行符合文档顺序。
    struct Frame
    {
        const DocumentNode *node;
        int depth;
    };
    QVector<Frame> pending;
    pending.append({page->root.get(), 0});

    while (!pending.isEmpty() && rows.size() < kMaxLayerRows) {
        const Frame frame = pending.takeLast();
        const DocumentNode *current = frame.node;
        if (!current)
            continue;

        QVariantMap row;
        row.insert(QStringLiteral("id"), current->id);
        row.insert(QStringLiteral("tag"), current->tag);
        row.insert(QStringLiteral("name"), current->name);
        row.insert(QStringLiteral("labelEn"),
                   current->name.isEmpty() ? labelEn(current->tag) : current->name);
        row.insert(QStringLiteral("labelZh"),
                   current->name.isEmpty() ? labelZh(current->tag) : current->name);
        row.insert(QStringLiteral("depth"), frame.depth);
        row.insert(QStringLiteral("childCount"), current->childCount());
        row.insert(QStringLiteral("hasChildren"), current->childCount() > 0);
        row.insert(QStringLiteral("expanded"), current->expanded);
        row.insert(QStringLiteral("hidden"), current->hidden);
        row.insert(QStringLiteral("locked"), current->locked);
        row.insert(QStringLiteral("selected"), current->id == m_selectedNodeId);
        row.insert(QStringLiteral("isRoot"), current == page->root.get());
        row.insert(QStringLiteral("icon"), iconForTag(current->tag));
        rows.append(row);

        if (frame.depth >= kMaxNodeDepth)
            continue;
        for (int i = current->childCount() - 1; i >= 0; --i) {
            if (const DocumentNode *child = current->childAt(i))
                pending.append({child, frame.depth + 1});
        }
    }
    return rows;
}

QVariantList BuilderController::outlineNodes() const
{
    QVariantList rows;
    Page *page = currentPage();
    if (!page || !page->root)
        return rows;

    //  The page root itself is not drawn: the canvas already represents it as
    //  the white page. Traversal begins at its children, in document order.
    //  页面根节点本身不会被绘制：画布已用白色页面代表它。遍历从其子节点开始，
    //  按文档顺序。
    struct Frame
    {
        const DocumentNode *node;
        int depth;
    };
    QVector<Frame> pending;
    for (int i = page->root->childCount() - 1; i >= 0; --i) {
        if (const DocumentNode *child = page->root->childAt(i))
            pending.append({child, 1});
    }

    const ElementCatalog &catalog = ElementCatalog::instance();

    while (!pending.isEmpty() && rows.size() < kMaxLayerRows) {
        const Frame frame = pending.takeLast();
        const DocumentNode *current = frame.node;
        if (!current)
            continue;

        const ElementDef *def = catalog.find(current->tag);

        QVariantMap row;
        row.insert(QStringLiteral("id"), current->id);
        row.insert(QStringLiteral("tag"), current->tag);
        row.insert(QStringLiteral("kind"), toString(current->kind));
        row.insert(QStringLiteral("name"), current->name);
        row.insert(QStringLiteral("labelEn"),
                   current->name.isEmpty() ? labelEn(current->tag) : current->name);
        row.insert(QStringLiteral("labelZh"),
                   current->name.isEmpty() ? labelZh(current->tag) : current->name);
        row.insert(QStringLiteral("descriptionEn"), def ? def->descriptionEn : QString());
        row.insert(QStringLiteral("descriptionZh"), def ? def->descriptionZh : QString());
        row.insert(QStringLiteral("icon"), iconForTag(current->tag));
        row.insert(QStringLiteral("text"), current->text);
        row.insert(QStringLiteral("depth"), frame.depth);
        row.insert(QStringLiteral("childCount"), current->childCount());
        row.insert(QStringLiteral("hasChildren"), current->childCount() > 0);
        //  A drop is only legal where the catalogue says children are allowed,
        //  which is stricter than "is not a void element".
        //  只有在目录声明允许子元素的位置才允许放置，这比"不是空元素"更严格。
        row.insert(QStringLiteral("allowsChildren"),
                   def ? def->allowsChildren : !current->isVoidElement());
        row.insert(QStringLiteral("isTextual"), current->isTextual());
        row.insert(QStringLiteral("layoutMode"), toString(current->layoutMode));
        row.insert(QStringLiteral("hidden"), current->hidden);
        row.insert(QStringLiteral("locked"), current->locked);
        row.insert(QStringLiteral("selected"), current->id == m_selectedNodeId);
        rows.append(row);

        if (frame.depth >= kMaxNodeDepth)
            continue;
        for (int i = current->childCount() - 1; i >= 0; --i) {
            if (const DocumentNode *child = current->childAt(i))
                pending.append({child, frame.depth + 1});
        }
    }
    return rows;
}

QVariantList BuilderController::palette() const
{
    QVariantList groups;
    const QVector<const ElementDef *> defs = sortedElementDefs();

    //  The catalogue is static data that cannot change at run time, so
    //  paletteChanged() is never emitted after start-up.
    //  目录是不可在运行期变化的静态数据，因此启动之后永远不会发出 paletteChanged()。
    for (ElementCategory category : ElementCatalog::categories()) {
        QVariantList items;
        for (const ElementDef *def : defs) {
            if (def->category == category)
                items.append(elementItemMap(*def));
        }
        if (items.isEmpty())
            continue;

        QVariantMap group;
        group.insert(QStringLiteral("category"), toString(category));
        group.insert(QStringLiteral("labelEn"), displayName(category, false));
        group.insert(QStringLiteral("labelZh"), displayName(category, true));
        group.insert(QStringLiteral("order"), groups.size());
        group.insert(QStringLiteral("items"), items);
        groups.append(group);
    }
    return groups;
}

QVariantList BuilderController::composites() const
{
    QVariantList list;
    const ElementCatalog &catalog = ElementCatalog::instance();

    QVector<const CompositeDef *> defs;
    defs.reserve(catalog.composites().size());
    for (const CompositeDef &def : catalog.composites())
        defs.append(&def);
    std::stable_sort(defs.begin(), defs.end(),
                     [](const CompositeDef *a, const CompositeDef *b) {
                         if (a->order != b->order)
                             return a->order < b->order;
                         return a->id < b->id;
                     });

    list.reserve(defs.size());
    for (const CompositeDef *def : defs) {
        QVariantMap item;
        item.insert(QStringLiteral("id"), def->id);
        item.insert(QStringLiteral("labelEn"), def->labelEn);
        item.insert(QStringLiteral("labelZh"), def->labelZh);
        item.insert(QStringLiteral("groupEn"), def->groupEn);
        item.insert(QStringLiteral("groupZh"), def->groupZh);
        item.insert(QStringLiteral("descriptionEn"), def->descriptionEn);
        item.insert(QStringLiteral("descriptionZh"), def->descriptionZh);
        item.insert(QStringLiteral("icon"), def->icon);
        item.insert(QStringLiteral("order"), def->order);
        list.append(item);
    }
    return list;
}

QVariantList BuilderController::availableElements() const
{
    const QVector<const ElementDef *> defs = sortedElementDefs();
    QVariantList list;
    list.reserve(defs.size());
    for (const ElementDef *def : defs)
        list.append(elementItemMap(*def));
    return list;
}

QVariantList BuilderController::elementsInCategory(const QString &category) const
{
    const QString wanted = category.trimmed();
    const QVector<const ElementDef *> defs = sortedElementDefs();
    QVariantList list;
    for (const ElementDef *def : defs) {
        if (wanted.isEmpty()
            || toString(def->category).compare(wanted, Qt::CaseInsensitive) == 0) {
            list.append(elementItemMap(*def));
        }
    }
    return list;
}

QVariantList BuilderController::searchElements(const QString &needle) const
{
    const QString trimmed = needle.trimmed();
    //  An empty query shows everything: that is what an empty search box means.
    //  空查询返回全部内容：这正是空搜索框应有的语义。
    if (trimmed.isEmpty())
        return availableElements();

    const QVector<const ElementDef *> defs = sortedElementDefs();
    QVariantList list;
    for (const ElementDef *def : defs) {
        if (matchesNeedle(*def, trimmed))
            list.append(elementItemMap(*def));
    }
    return list;
}

// ---------------------------------------------------------------------------
//  Insertion
//  插入
// ---------------------------------------------------------------------------
QString BuilderController::insertElement(const QString &tag, const QString &parentId, int index)
{
    Page *page = currentPage();
    CommandStack *commands = stack();
    if (!page || !page->root || !commands || !m_app) {
        if (m_app)
            m_app->reportError(noOpenPageMessage());
        return QString();
    }

    //  A runaway AI loop must not be able to grow a page until the user
    //  interface stops responding.
    //  失控的 AI 循环绝不能把页面撑大到用户界面失去响应。
    if (page->nodeCount() > kMaxPageNodes) {
        m_app->reportError(
            QStringLiteral("This page already holds more than %1 elements; add no more. / "
                           "本页面元素已超过 %1 个，无法继续添加。")
                .arg(kMaxPageNodes));
        return QString();
    }

    const QString wantedParent = parentId.trimmed();
    DocumentNode *parent = wantedParent.isEmpty() ? page->root.get()
                                                  : page->root->findById(wantedParent);
    if (!parent) {
        m_app->reportError(
            QStringLiteral("The target element no longer exists. / 目标元素已不存在。"));
        return QString();
    }
    if (parent->isVoidElement()) {
        m_app->reportError(QStringLiteral("<%1> cannot contain child elements. / "
                                          "<%1> 不能包含子元素。")
                               .arg(parent->tag));
        return QString();
    }

    std::unique_ptr<DocumentNode> created =
        ElementCatalog::instance().create(tag.trimmed(), m_app->chinese());
    if (!created) {
        m_app->reportError(QStringLiteral("Unknown element \"%1\". / 未知元素 \"%1\"。").arg(tag));
        return QString();
    }
    if (created->id.isEmpty())
        created->id = IdGenerator::elementId();
    const QString fallbackId = created->id;

    const QString addedId = commands->addNode(page->id, parent->id, index, std::move(created),
                                              QStringLiteral("Add element"),
                                              QStringLiteral("添加元素"));
    const QString newId = addedId.isEmpty() ? fallbackId : addedId;
    m_selectedNodeId = newId;
    afterMutation(true);
    return newId;
}

QString BuilderController::insertComposite(const QString &compositeId, const QString &parentId,
                                           int index)
{
    Page *page = currentPage();
    CommandStack *commands = stack();
    if (!page || !page->root || !commands || !m_app) {
        if (m_app)
            m_app->reportError(noOpenPageMessage());
        return QString();
    }
    if (page->nodeCount() > kMaxPageNodes) {
        m_app->reportError(
            QStringLiteral("This page already holds more than %1 elements; add no more. / "
                           "本页面元素已超过 %1 个，无法继续添加。")
                .arg(kMaxPageNodes));
        return QString();
    }

    const QString wantedParent = parentId.trimmed();
    DocumentNode *parent = wantedParent.isEmpty() ? page->root.get()
                                                  : page->root->findById(wantedParent);
    if (!parent) {
        m_app->reportError(
            QStringLiteral("The target element no longer exists. / 目标元素已不存在。"));
        return QString();
    }
    if (parent->isVoidElement()) {
        m_app->reportError(QStringLiteral("<%1> cannot contain child elements. / "
                                          "<%1> 不能包含子元素。")
                               .arg(parent->tag));
        return QString();
    }

    const QString wanted = compositeId.trimmed();
    if (!ElementCatalog::instance().findComposite(wanted)) {
        m_app->reportError(QStringLiteral("Unknown component \"%1\". / 未知组件 \"%1\"。")
                               .arg(compositeId));
        return QString();
    }

    std::unique_ptr<DocumentNode> created =
        ElementCatalog::instance().createComposite(wanted, m_app->chinese());
    if (!created) {
        m_app->reportError(QStringLiteral("The component \"%1\" could not be built. / "
                                          "无法构建组件 \"%1\"。")
                               .arg(wanted));
        return QString();
    }
    if (created->id.isEmpty())
        created->id = IdGenerator::elementId();
    const QString fallbackId = created->id;

    const QString addedId = commands->addNode(page->id, parent->id, index, std::move(created),
                                              QStringLiteral("Add component"),
                                              QStringLiteral("添加组件"));
    const QString newId = addedId.isEmpty() ? fallbackId : addedId;
    m_selectedNodeId = newId;
    afterMutation(true);
    return newId;
}

QString BuilderController::duplicateSelected()
{
    Page *page = currentPage();
    CommandStack *commands = stack();
    if (!page || !page->root || !commands || !m_app) {
        if (m_app)
            m_app->reportError(noOpenPageMessage());
        return QString();
    }
    DocumentNode *current = node(m_selectedNodeId);
    if (!current || current == page->root.get() || !current->parent())
        return QString();
    if (page->nodeCount() > kMaxPageNodes) {
        m_app->reportError(
            QStringLiteral("This page already holds more than %1 elements; duplicate no more. / "
                           "本页面元素已超过 %1 个，无法继续复制。")
                .arg(kMaxPageNodes));
        return QString();
    }

    //  The copy gets brand new ids so that the document never contains two nodes
    //  with the same identity.
    //  副本使用全新的 id，使文档中永远不会出现两个身份相同的节点。
    std::unique_ptr<DocumentNode> copy = current->clone(false);
    if (!copy)
        return QString();
    if (copy->id.isEmpty())
        copy->id = IdGenerator::elementId();
    const QString copyId = copy->id;

    commands->addNode(page->id, current->parent()->id, current->indexInParent() + 1,
                      std::move(copy), QStringLiteral("Duplicate element"),
                      QStringLiteral("复制元素"));
    m_selectedNodeId = copyId;
    afterMutation(true);
    return copyId;
}

bool BuilderController::deleteSelected()
{
    if (m_selectedNodeId.isEmpty())
        return false;
    return deleteNode(m_selectedNodeId);
}

bool BuilderController::deleteNode(const QString &nodeId)
{
    Page *page = currentPage();
    CommandStack *commands = stack();
    if (!page || !page->root || !commands || !m_app) {
        if (m_app)
            m_app->reportError(noOpenPageMessage());
        return false;
    }
    DocumentNode *current = page->root->findById(nodeId);
    if (!current) {
        m_app->reportError(
            QStringLiteral("The element no longer exists. / 该元素已不存在。"));
        return false;
    }
    if (current == page->root.get()) {
        m_app->reportError(QStringLiteral("The page root cannot be deleted. / "
                                          "页面根节点无法删除。"));
        return false;
    }

    if (!commands->removeNode(page->id, current->id, QStringLiteral("Delete element"),
                              QStringLiteral("删除元素")))
        return false;

    //  afterMutation() clears the selection when the selected node is gone.
    //  当被选中的节点消失时，afterMutation() 会清除选择。
    afterMutation(true);
    return true;
}

bool BuilderController::moveNode(const QString &nodeId, const QString &newParentId, int index)
{
    Page *page = currentPage();
    CommandStack *commands = stack();
    ProjectDocument *document = m_app ? m_app->document() : nullptr;
    if (!page || !page->root || !commands || !document || !m_app) {
        if (m_app)
            m_app->reportError(noOpenPageMessage());
        return false;
    }

    DocumentNode *current = page->root->findById(nodeId);
    if (!current || current == page->root.get()) {
        m_app->reportError(QStringLiteral("The element cannot be moved. / 该元素无法移动。"));
        return false;
    }

    const QString wantedParent = newParentId.trimmed();
    DocumentNode *target = wantedParent.isEmpty() ? page->root.get()
                                                  : page->root->findById(wantedParent);
    if (!target) {
        m_app->reportError(
            QStringLiteral("The target element no longer exists. / 目标元素已不存在。"));
        return false;
    }
    if (target == current || current->isAncestorOf(target)) {
        m_app->reportError(
            QStringLiteral("An element cannot be moved inside itself. / "
                           "元素不能移动到自身内部。"));
        return false;
    }
    if (target->isVoidElement()) {
        m_app->reportError(QStringLiteral("<%1> cannot contain child elements. / "
                                          "<%1> 不能包含子元素。")
                               .arg(target->tag));
        return false;
    }

    commands->pushIfEffective(
        new ReparentNodeCommand(document, page->id, current->id, target->id, index));
    afterMutation(true);
    return true;
}

bool BuilderController::reorderSelected(int delta)
{
    Page *page = currentPage();
    CommandStack *commands = stack();
    ProjectDocument *document = m_app ? m_app->document() : nullptr;
    if (!page || !page->root || !commands || !document || !m_app)
        return false;

    DocumentNode *current = node(m_selectedNodeId);
    if (!current || current == page->root.get())
        return false;
    DocumentNode *parent = current->parent();
    if (!parent)
        return false;

    const int index = current->indexInParent();
    const int target = index + (delta < 0 ? -1 : 1);
    //  At either end of the sibling list there is nothing left to swap with.
    //  在兄弟列表的两端已经没有可交换的对象。
    if (target < 0 || target >= parent->childCount())
        return false;

    commands->pushIfEffective(new MoveNodeCommand(document, page->id, current->id, target));
    afterMutation(true);
    return true;
}

// ---------------------------------------------------------------------------
//  Node properties
//  节点属性
// ---------------------------------------------------------------------------
bool BuilderController::setNodeText(const QString &nodeId, const QString &text)
{
    Page *page = currentPage();
    CommandStack *commands = stack();
    if (!page || !commands || !m_app) {
        if (m_app)
            m_app->reportError(noOpenPageMessage());
        return false;
    }
    if (!node(nodeId)) {
        m_app->reportError(QStringLiteral("The element no longer exists. / 该元素已不存在。"));
        return false;
    }

    const bool changed = commands->setText(page->id, nodeId, text,
                                           QStringLiteral("Edit text"),
                                           QStringLiteral("编辑文本"));
    if (changed)
        afterMutation(false);
    return changed;
}

bool BuilderController::setNodeAttribute(const QString &nodeId, const QString &key,
                                         const QString &value)
{
    Page *page = currentPage();
    CommandStack *commands = stack();
    if (!page || !commands || !m_app) {
        if (m_app)
            m_app->reportError(noOpenPageMessage());
        return false;
    }
    if (!node(nodeId)) {
        m_app->reportError(QStringLiteral("The element no longer exists. / 该元素已不存在。"));
        return false;
    }
    if (key.trimmed().isEmpty()) {
        m_app->reportError(QStringLiteral("An attribute needs a name. / 属性必须有名称。"));
        return false;
    }

    //  An empty value stores an empty attribute rather than removing it; use the
    //  attribute list in the inspector (or the AI tools) to remove one.
    //  空值会存储一个空属性而不是删除它；删除属性请使用检查器的属性列表
    //  （或 AI 工具）。
    const bool changed = commands->setAttribute(page->id, nodeId, key, value,
                                                QStringLiteral("Set attribute"),
                                                QStringLiteral("设置属性"));
    if (changed)
        afterMutation(false);
    return changed;
}

bool BuilderController::setNodeName(const QString &nodeId, const QString &name)
{
    Page *page = currentPage();
    CommandStack *commands = stack();
    ProjectDocument *document = m_app ? m_app->document() : nullptr;
    if (!page || !commands || !document || !m_app) {
        if (m_app)
            m_app->reportError(noOpenPageMessage());
        return false;
    }
    if (!node(nodeId)) {
        m_app->reportError(QStringLiteral("The element no longer exists. / 该元素已不存在。"));
        return false;
    }

    commands->pushIfEffective(new RenameNodeCommand(document, page->id, nodeId, name));
    afterMutation(false);
    return true;
}

bool BuilderController::setNodeStyle(const QString &nodeId, const QString &property,
                                     const QString &value, const QString &breakpoint)
{
    Page *page = currentPage();
    CommandStack *commands = stack();
    if (!page || !commands || !m_app) {
        if (m_app)
            m_app->reportError(noOpenPageMessage());
        return false;
    }
    if (!node(nodeId)) {
        m_app->reportError(QStringLiteral("The element no longer exists. / 该元素已不存在。"));
        return false;
    }
    if (property.trimmed().isEmpty()) {
        m_app->reportError(QStringLiteral("A style declaration needs a property name. / "
                                          "样式声明必须有属性名。"));
        return false;
    }

    const Breakpoint wanted = breakpoint.trimmed().isEmpty()
                                  ? m_app->activeBreakpoint()
                                  : breakpointFromString(breakpoint, m_app->activeBreakpoint());

    //  An empty value removes the declaration: StyleSheet::set() erases the
    //  property when the value is empty, which is exactly the "reset" gesture the
    //  inspector needs.
    //  空值会删除该声明：当值为空时 StyleSheet::set() 会移除该属性，
    //  这正是检查器所需的"重置"操作。
    const bool changed = commands->setStyle(page->id, nodeId, wanted, property, value,
                                            QStringLiteral("Set style"),
                                            QStringLiteral("设置样式"));
    if (changed)
        afterMutation(false);
    return changed;
}

bool BuilderController::setNodeLayoutMode(const QString &nodeId, const QString &mode)
{
    Page *page = currentPage();
    CommandStack *commands = stack();
    DocumentNode *current = node(nodeId);
    if (!page || !commands || !current || !m_app) {
        if (m_app && !current)
            m_app->reportError(QStringLiteral("The element no longer exists. / "
                                              "该元素已不存在。"));
        return false;
    }

    const LayoutMode wanted = layoutModeFromString(mode.trimmed(), current->layoutMode);
    if (wanted == current->layoutMode)
        return true;

    //  Commands.h defines no layout-mode command, so the change is made directly
    //  inside a macro: the macro groups the user's drag in the undo history and
    //  the document is marked dirty by hand. The trade-off is that this single
    //  change is not itself undoable.
    //  Commands.h 未定义布局模式命令，因此在宏内直接修改：该宏使用户的拖拽在撤销
    //  历史中保持成组，文档的脏标记则手动设置。代价是这一次修改本身无法撤销。
    commands->beginMacro(QStringLiteral("Change layout mode"), QStringLiteral("更改布局模式"));
    current->layoutMode = wanted;
    if (ProjectDocument *document = m_app->document())
        document->setModified(true);
    commands->endMacro();

    afterMutation(false);
    return true;
}

bool BuilderController::setNodeFreeRect(const QString &nodeId, double x, double y, double w, double h)
{
    Page *page = currentPage();
    CommandStack *commands = stack();
    DocumentNode *current = node(nodeId);
    if (!page || !commands || !current || !m_app) {
        if (m_app && !current)
            m_app->reportError(QStringLiteral("The element no longer exists. / "
                                              "该元素已不存在。"));
        return false;
    }

    //  Direct mutation inside a macro -- see setNodeLayoutMode() for why.
    //  在宏内直接修改 —— 原因见 setNodeLayoutMode()。
    commands->beginMacro(QStringLiteral("Move or resize element"),
                         QStringLiteral("移动或调整元素"));
    current->freeRect = QRectF(x, y, qMax(0.0, w), qMax(0.0, h));
    if (ProjectDocument *document = m_app->document())
        document->setModified(true);
    commands->endMacro();

    afterMutation(false);
    return true;
}

bool BuilderController::setNodeFlag(const QString &nodeId, const QString &flag, bool value)
{
    Page *page = currentPage();
    CommandStack *commands = stack();
    DocumentNode *current = node(nodeId);
    if (!page || !commands || !current || !m_app) {
        if (m_app && !current)
            m_app->reportError(QStringLiteral("The element no longer exists. / "
                                              "该元素已不存在。"));
        return false;
    }

    const QString key = flag.trimmed().toLower();
    bool *target = nullptr;
    if (key == QLatin1String("locked"))
        target = &current->locked;
    else if (key == QLatin1String("hidden"))
        target = &current->hidden;
    else if (key == QLatin1String("expanded"))
        target = &current->expanded;
    else {
        m_app->reportError(QStringLiteral("Unknown element flag \"%1\". / "
                                          "未知的元素标记 \"%1\"。")
                               .arg(flag));
        return false;
    }
    if (*target == value)
        return true;

    //  "hidden" and "expanded" (and, like them, "locked") have no dedicated
    //  command in Commands.h, so they are mutated directly inside a macro --
    //  documented trade-off: the change is grouped for the next undo step but is
    //  not itself undoable.
    //  "hidden" 与 "expanded"（以及同类的 "locked"）在 Commands.h 中没有专用命令，
    //  因此在宏内直接修改 —— 这是一项有据可查的取舍：该修改会与下一步撤销归为一组，
    //  但其自身无法撤销。
    commands->beginMacro(QStringLiteral("Change element flag"),
                         QStringLiteral("更改元素标记"));
    *target = value;
    if (ProjectDocument *document = m_app->document())
        document->setModified(true);
    commands->endMacro();

    afterMutation(false);
    return true;
}

bool BuilderController::setNodeClasses(const QString &nodeId, const QStringList &classes)
{
    Page *page = currentPage();
    CommandStack *commands = stack();
    DocumentNode *current = node(nodeId);
    if (!page || !commands || !current || !m_app) {
        if (m_app && !current)
            m_app->reportError(QStringLiteral("The element no longer exists. / "
                                              "该元素已不存在。"));
        return false;
    }
    if (current->classes == classes)
        return true;

    //  Direct mutation inside a macro -- see setNodeLayoutMode() for why.
    //  在宏内直接修改 —— 原因见 setNodeLayoutMode()。
    commands->beginMacro(QStringLiteral("Change classes"), QStringLiteral("更改类名"));
    current->classes = classes;
    if (ProjectDocument *document = m_app->document())
        document->setModified(true);
    commands->endMacro();

    afterMutation(false);
    return true;
}

bool BuilderController::wrapSelected(const QString &tag)
{
    Page *page = currentPage();
    CommandStack *commands = stack();
    ProjectDocument *document = m_app ? m_app->document() : nullptr;
    if (!page || !page->root || !commands || !document || !m_app) {
        if (m_app)
            m_app->reportError(noOpenPageMessage());
        return false;
    }

    DocumentNode *current = node(m_selectedNodeId);
    if (!current || current == page->root.get() || !current->parent()) {
        m_app->reportError(QStringLiteral("Select an element to wrap first. / "
                                          "请先选择一个要包裹的元素。"));
        return false;
    }
    if (page->nodeCount() > kMaxPageNodes) {
        m_app->reportError(
            QStringLiteral("This page already holds more than %1 elements; add no more. / "
                           "本页面元素已超过 %1 个，无法继续添加。")
                .arg(kMaxPageNodes));
        return false;
    }

    std::unique_ptr<DocumentNode> wrapper =
        ElementCatalog::instance().create(tag.trimmed(), m_app->chinese());
    if (!wrapper) {
        m_app->reportError(QStringLiteral("Unknown element \"%1\". / 未知元素 \"%1\"。").arg(tag));
        return false;
    }
    if (wrapper->isVoidElement()) {
        m_app->reportError(QStringLiteral("<%1> cannot wrap other elements. / "
                                          "<%1> 无法包裹其他元素。")
                               .arg(wrapper->tag));
        return false;
    }
    if (wrapper->id.isEmpty())
        wrapper->id = IdGenerator::elementId();
    const QString wrapperId = wrapper->id;

    DocumentNode *parent = current->parent();
    const QString parentId = parent->id;
    const int index = current->indexInParent();

    //  One macro, two commands: the wrapper is inserted where the element was,
    //  and the element is reparented into the wrapper.
    //  一个宏、两个命令：包裹元素插入到原元素所在位置，原元素被重新挂到包裹元素内部。
    commands->beginMacro(QStringLiteral("Wrap element"), QStringLiteral("包裹元素"));
    commands->pushIfEffective(new AddNodeCommand(document, page->id, parentId, index,
                                                 std::move(wrapper)));
    commands->pushIfEffective(
        new ReparentNodeCommand(document, page->id, current->id, wrapperId, -1));
    commands->endMacro();

    m_selectedNodeId = wrapperId;
    afterMutation(true);
    return true;
}

// ---------------------------------------------------------------------------
//  Queries
//  查询
// ---------------------------------------------------------------------------
QVariantMap BuilderController::nodeById(const QString &nodeId) const
{
    return nodeToVariant(node(nodeId), false);
}

QVariantMap BuilderController::nodeStyle(const QString &nodeId, const QString &breakpoint) const
{
    QVariantMap map;
    DocumentNode *current = node(nodeId);
    if (!current)
        return map;

    const Breakpoint active = m_app ? m_app->activeBreakpoint() : Breakpoint::Desktop;
    const Breakpoint wanted =
        breakpoint.trimmed().isEmpty() ? active : breakpointFromString(breakpoint, active);

    const QMap<QString, QString> resolved = current->style.resolved(wanted);
    for (auto it = resolved.constBegin(); it != resolved.constEnd(); ++it)
        map.insert(it.key(), it.value());
    return map;
}

QVariantMap BuilderController::nodeGeometry(const QString &nodeId) const
{
    QVariantMap result;
    result.insert(QStringLiteral("x"), 0.0);
    result.insert(QStringLiteral("y"), 0.0);
    result.insert(QStringLiteral("width"), 0.0);
    result.insert(QStringLiteral("height"), 0.0);
    result.insert(QStringLiteral("found"), false);

    Page *page = currentPage();
    if (!page || !page->root)
        return result;
    DocumentNode *current = page->root->findById(nodeId);
    if (!current)
        return result;

    //  Offsets accumulate only through free-layout ancestors: a flow container
    //  positions its children, so its own rectangle must not be added.
    //  只有自由布局的祖先才会累加偏移：常规文档流容器会自行排布子元素，
    //  因此不能累加它自身的矩形。
    double x = 0.0;
    double y = 0.0;
    bool reachedRoot = false;
    DocumentNode *cursor = current;
    for (int guard = 0; cursor != nullptr && guard <= kMaxNodeDepth; ++guard) {
        if (cursor == page->root.get()) {
            reachedRoot = true;
            break;
        }
        if (cursor->layoutMode == LayoutMode::Free) {
            x += cursor->freeRect.x();
            y += cursor->freeRect.y();
        }
        cursor = cursor->parent();
    }
    //  A broken parent chain means the geometry cannot be trusted; reporting
    //  "not found" is better than drawing an overlay at a wrong place.
    //  父链断裂意味着几何不可信；报告"未找到"好过在错误位置绘制叠加层。
    if (!reachedRoot)
        return result;

    result.insert(QStringLiteral("x"), x);
    result.insert(QStringLiteral("y"), y);
    result.insert(QStringLiteral("width"), current->freeRect.width());
    result.insert(QStringLiteral("height"), current->freeRect.height());
    result.insert(QStringLiteral("found"), true);
    return result;
}

// ---------------------------------------------------------------------------
//  Clipboard
//  剪贴板
// ---------------------------------------------------------------------------
bool BuilderController::copySelected()
{
    DocumentNode *current = node(m_selectedNodeId);
    if (!current || !m_app)
        return false;

    if (subtreeCount(current) > kMaxClipboardNodes) {
        m_app->reportError(
            QStringLiteral("This element holds more than %1 nodes; it is too large to copy. / "
                           "该元素包含超过 %1 个节点，过大无法复制。")
                .arg(kMaxClipboardNodes));
        return false;
    }

    //  The clone gets fresh ids so that pasting can never create a duplicate
    //  identity; the source id is remembered only to keep the cycle guard honest.
    //  克隆体使用全新的 id，使粘贴永远不会产生重复身份；记录源 id 只是为了实现
    //  可靠的循环防护。
    m_clipboard = current->clone(false);
    if (!m_clipboard)
        return false;
    setProperty(kClipboardSourceProperty, current->id);
    emit clipboardChanged();
    return true;
}

bool BuilderController::cutSelected()
{
    if (!copySelected())
        return false;
    return deleteSelected();
}

bool BuilderController::pasteInto(const QString &parentId)
{
    Page *page = currentPage();
    CommandStack *commands = stack();
    if (!page || !page->root || !commands || !m_app) {
        if (m_app)
            m_app->reportError(noOpenPageMessage());
        return false;
    }
    if (!m_clipboard) {
        m_app->reportError(QStringLiteral("The clipboard is empty. / 剪贴板为空。"));
        return false;
    }
    if (subtreeCount(m_clipboard.get()) > kMaxClipboardNodes) {
        m_app->reportError(
            QStringLiteral("This clipboard entry holds more than %1 nodes; it is too large to "
                           "paste. / 该剪贴板内容包含超过 %1 个节点，过大无法粘贴。")
                .arg(kMaxClipboardNodes));
        return false;
    }
    if (page->nodeCount() > kMaxPageNodes) {
        m_app->reportError(
            QStringLiteral("This page already holds more than %1 elements; paste no more. / "
                           "本页面元素已超过 %1 个，无法继续粘贴。")
                .arg(kMaxPageNodes));
        return false;
    }

    const QString wantedParent = parentId.trimmed();
    DocumentNode *target = wantedParent.isEmpty() ? page->root.get()
                                                  : page->root->findById(wantedParent);
    if (!target) {
        m_app->reportError(
            QStringLiteral("The target element no longer exists. / 目标元素已不存在。"));
        return false;
    }
    if (target->isVoidElement()) {
        m_app->reportError(QStringLiteral("<%1> cannot contain child elements. / "
                                          "<%1> 不能包含子元素。")
                               .arg(target->tag));
        return false;
    }

    //  Cycle guard: pasting a subtree back into itself would detach the tree.
    //  循环防护：把子树粘贴回它自己内部会让树断开。
    const QString sourceId = property(kClipboardSourceProperty).toString();
    if (!sourceId.isEmpty()) {
        DocumentNode *source = page->root->findById(sourceId);
        if (target == source || (source && source->isAncestorOf(target))) {
            m_app->reportError(
                QStringLiteral("An element cannot be pasted inside itself. / "
                               "元素不能粘贴到自身内部。"));
            return false;
        }
    }

    std::unique_ptr<DocumentNode> copy = m_clipboard->clone(false);
    if (!copy)
        return false;
    if (copy->id.isEmpty())
        copy->id = IdGenerator::elementId();
    const QString copyId = copy->id;

    commands->addNode(page->id, target->id, -1, std::move(copy),
                      QStringLiteral("Paste element"), QStringLiteral("粘贴元素"));
    m_selectedNodeId = copyId;
    afterMutation(true);
    return true;
}

void BuilderController::beginMacro(const QString &labelEn, const QString &labelZh)
{
    CommandStack *commands = stack();
    if (!commands)
        return;
    if (labelEn.trimmed().isEmpty() && labelZh.trimmed().isEmpty())
        return;

    //  Nesting is guarded so an unbalanced QML call can never leave the undo
    //  stack in macro mode, which would silently merge unrelated edits.
    //  嵌套受到保护，使不配对的 QML 调用永远不会让撤销栈停留在宏模式，
    //  否则会把无关的编辑悄悄合并在一起。
    commands->beginMacro(labelEn.trimmed().isEmpty() ? labelZh : labelEn,
                         labelZh.trimmed().isEmpty() ? labelEn : labelZh);
    ++m_macroDepth;
}

void BuilderController::endMacro()
{
    CommandStack *commands = stack();
    if (m_macroDepth <= 0) {
        //  An unmatched endMacro() is ignored instead of closing somebody else's
        //  macro.
        //  未配对的 endMacro() 会被忽略，而不是关闭别人的宏。
        m_macroDepth = 0;
        return;
    }
    --m_macroDepth;
    if (commands)
        commands->endMacro();
}

// ---------------------------------------------------------------------------
//  Style presets
//  样式预设
// ---------------------------------------------------------------------------
QVariantList BuilderController::stylePresets() const
{
    QVariantList list;
    const QVector<StylePreset> &table = stylePresetTable();
    list.reserve(table.size());
    for (const StylePreset &preset : table) {
        QVariantList declarations;
        declarations.reserve(preset.declarations.size());
        for (const Declaration &declaration : preset.declarations) {
            QVariantMap entry;
            entry.insert(QStringLiteral("property"), declaration.property);
            entry.insert(QStringLiteral("value"), declaration.value);
            declarations.append(entry);
        }

        QVariantMap item;
        item.insert(QStringLiteral("id"), preset.id);
        item.insert(QStringLiteral("labelEn"), preset.labelEn);
        item.insert(QStringLiteral("labelZh"), preset.labelZh);
        item.insert(QStringLiteral("groupEn"), preset.groupEn);
        item.insert(QStringLiteral("groupZh"), preset.groupZh);
        item.insert(QStringLiteral("declarations"), declarations);
        list.append(item);
    }
    return list;
}

bool BuilderController::applyStylePreset(const QString &presetId)
{
    Page *page = currentPage();
    CommandStack *commands = stack();
    if (!page || !commands || !m_app) {
        if (m_app)
            m_app->reportError(noOpenPageMessage());
        return false;
    }
    if (m_selectedNodeId.isEmpty() || !node(m_selectedNodeId)) {
        m_app->reportError(QStringLiteral("Select an element before applying a style preset. / "
                                          "应用样式预设前请先选择元素。"));
        return false;
    }

    const StylePreset *preset = findStylePreset(presetId);
    if (!preset) {
        m_app->reportError(QStringLiteral("Unknown style preset \"%1\". / "
                                          "未知的样式预设 \"%1\"。")
                               .arg(presetId));
        return false;
    }

    //  The declarations land at the breakpoint currently being previewed, so
    //  what the user sees on the canvas is exactly what a preset produces.
    //  声明写入当前预览的断点，因此用户在画布上看到的就是预设产生的效果。
    const Breakpoint breakpoint = m_app->activeBreakpoint();
    commands->beginMacro(QStringLiteral("Apply style preset"),
                         QStringLiteral("应用样式预设"));
    for (const Declaration &declaration : preset->declarations) {
        commands->setStyle(page->id, m_selectedNodeId, breakpoint, declaration.property,
                           declaration.value, QStringLiteral("Set style"),
                           QStringLiteral("设置样式"));
    }
    commands->endMacro();

    afterMutation(false);
    return true;
}

// ---------------------------------------------------------------------------
//  Canvas
//  画布
// ---------------------------------------------------------------------------
QString BuilderController::layoutMode() const
{
    DocumentNode *current = node(m_selectedNodeId);
    return current ? toString(current->layoutMode) : QString();
}

void BuilderController::setGridVisible(bool value)
{
    if (m_gridVisible == value)
        return;
    m_gridVisible = value;
    AppSettings::instance().setShowGrid(value);
    emit canvasSettingsChanged();
}

void BuilderController::setGridSize(int value)
{
    const int wanted = qBound(1, value, 512);
    if (m_gridSize == wanted)
        return;
    m_gridSize = wanted;
    AppSettings::instance().setGridSize(wanted);
    emit canvasSettingsChanged();
}

void BuilderController::setSnapping(bool value)
{
    if (m_snapping == value)
        return;
    m_snapping = value;
    AppSettings::instance().setSnapToGrid(value);
    emit canvasSettingsChanged();
}

int BuilderController::canvasWidth() const
{
    const Breakpoint active = m_app ? m_app->activeBreakpoint() : Breakpoint::Desktop;
    switch (active) {
    case Breakpoint::Tablet:
        return kTabletWidth;
    case Breakpoint::Mobile:
        return kMobileWidth;
    case Breakpoint::Desktop:
        break;
    }
    return kDesktopWidth;
}

int BuilderController::canvasHeight() const
{
    const Breakpoint active = m_app ? m_app->activeBreakpoint() : Breakpoint::Desktop;
    switch (active) {
    case Breakpoint::Tablet:
        return kTabletHeight;
    case Breakpoint::Mobile:
        return kMobileHeight;
    case Breakpoint::Desktop:
        break;
    }
    return kDesktopHeight;
}

void BuilderController::setBreakpoint(const QString &key)
{
    //  Routed through AppController so that the canvas, the tab bar and the
    //  responsive inspector all move together.
    //  经由 AppController 转发，使画布、标签栏与响应式检查器同步变化。
    if (m_app)
        m_app->setBreakpoint(key);
}

// ---------------------------------------------------------------------------
//  Change propagation
//  变更传播
// ---------------------------------------------------------------------------
void BuilderController::afterMutation(bool structural)
{
    const bool selectionLost = !m_selectedNodeId.isEmpty() && node(m_selectedNodeId) == nullptr;
    if (selectionLost)
        m_selectedNodeId.clear();

    emit layerTreeChanged();
    emit requestRepaint();

    //  The selection signal is also the refresh signal for the inspector, so it
    //  is emitted whenever the selection survives the change.
    //  选择信号同时是检查器的刷新信号，因此只要选择在修改后仍然存在就发出它。
    if (selectionLost || structural || !m_selectedNodeId.isEmpty())
        emit selectionChanged();
}

void BuilderController::onStructureChanged()
{
    afterMutation(true);
}

void BuilderController::onNodeChanged(const QString &nodeId)
{
    Q_UNUSED(nodeId)
    afterMutation(false);
}

void BuilderController::onPageChanged(const QString &pageId)
{
    Q_UNUSED(pageId)
    afterMutation(true);
}

} // namespace tcbw
