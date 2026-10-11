// ============================================================================
//  ProjectDocument.cpp -- the whole project: pages, scripts, settings
//  ProjectDocument.cpp —— 整个工程：页面、脚本、设置
// ============================================================================
#include "ProjectDocument.h"

#include "BlockDef.h"
#include "IdGenerator.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QSet>

#include <algorithm>
#include <vector>

namespace tcbw {

namespace {

// ---------------------------------------------------------------------------
//  Ownership registries
//  所有权登记表
//
//  The frozen header declares `QVector<std::unique_ptr<Page>> m_pages` and
//  `QVector<BlockScript *> m_program`. A Qt container cannot *store* a move-only
//  type: every implicit detach (and QHash detaches on any mutation, even of an
//  unrelated key) reallocates through a copying operation, which does not
//  compile for std::unique_ptr. The document therefore keeps the real owners in
//  these address-keyed registries of heap-allocated std::vectors -- the registry
//  value is a plain pointer, so Qt only ever copies pointers -- and `program()`
//  remains the raw-pointer view the header promises.
//  冻结的头文件声明了 `QVector<std::unique_ptr<Page>> m_pages` 与
//  `QVector<BlockScript *> m_program`。Qt 容器无法*存放*仅可移动类型：任何隐式
//  detach（QHash 在任意键发生修改时都会 detach）都会通过拷贝操作重新分配内存，
//  而 std::unique_ptr 不允许拷贝。因此文档把真正的持有者放在下面这些以对象地址为
//  键、值为堆上 std::vector 的登记表中——登记表的值只是裸指针，Qt 只会拷贝指针——
//  而 `program()` 仍然保持头文件承诺的裸指针视图。
// ---------------------------------------------------------------------------
QHash<const ProjectDocument *, std::vector<std::unique_ptr<Page>> *> &pageRegistry()
{
    static QHash<const ProjectDocument *, std::vector<std::unique_ptr<Page>> *> registry;
    return registry;
}

QHash<const ProjectDocument *, std::vector<std::unique_ptr<BlockScript>> *> &scriptRegistry()
{
    static QHash<const ProjectDocument *, std::vector<std::unique_ptr<BlockScript>> *> registry;
    return registry;
}

//  Lazily creates (never copies) the owning vector of a document's pages.
//  惰性创建（绝不拷贝）文档页面的持有向量。
std::vector<std::unique_ptr<Page>> *pageStore(const ProjectDocument *document)
{
    if (!document)
        return nullptr;
    QHash<const ProjectDocument *, std::vector<std::unique_ptr<Page>> *> &registry =
        pageRegistry();
    auto it = registry.find(document);
    if (it != registry.end())
        return it.value();
    auto *store = new std::vector<std::unique_ptr<Page>>();
    registry.insert(document, store);
    return store;
}

std::vector<std::unique_ptr<BlockScript>> *scriptStore(const ProjectDocument *document)
{
    if (!document)
        return nullptr;
    QHash<const ProjectDocument *, std::vector<std::unique_ptr<BlockScript>> *> &registry =
        scriptRegistry();
    auto it = registry.find(document);
    if (it != registry.end())
        return it.value();
    auto *store = new std::vector<std::unique_ptr<BlockScript>>();
    registry.insert(document, store);
    return store;
}

const std::vector<std::unique_ptr<Page>> &ownedPages(const ProjectDocument *document)
{
    static const std::vector<std::unique_ptr<Page>> empty;
    const auto it = pageRegistry().constFind(document);
    if (it == pageRegistry().constEnd() || !it.value())
        return empty;
    return *it.value();
}

//  The header's `pageCount()` is `static_cast<int>(m_pages.size())` and `m_pages`
//  is a vector of raw Page pointers, so the frozen Qt member is refreshed from
//  the registry after every mutation. It is a non-owning view: the registry owns
//  the pages and keeps them alive.
//  头文件的 `pageCount()` 是 `static_cast<int>(m_pages.size())`，而 `m_pages` 是
//  Page 裸指针的数组，因此在每次改动之后都依据登记表刷新这个冻结的 Qt 成员。它是
//  非持有视图：页面由登记表持有并保活。
QVector<Page *> sizedPageView(const ProjectDocument *document)
{
    QVector<Page *> view;
    const auto it = pageRegistry().constFind(document);
    if (it == pageRegistry().constEnd() || !it.value())
        return view;
    view.reserve(static_cast<int>(it.value()->size()));
    for (const auto &page : *it.value())
        view.append(page.get());
    return view;
}

const std::vector<std::unique_ptr<BlockScript>> &ownedScripts(const ProjectDocument *document)
{
    static const std::vector<std::unique_ptr<BlockScript>> empty;
    const auto it = scriptRegistry().constFind(document);
    if (it == scriptRegistry().constEnd() || !it.value())
        return empty;
    return *it.value();
}

//  Rebuilds the public raw-pointer view of the scripts from the owning vector.
//  依据持有向量重建公开的脚本裸指针视图。
void syncProgramView(ProjectDocument *document)
{
    if (!document)
        return;
    QVector<BlockScript *> &view = document->program();
    view.clear();
    view.reserve(static_cast<int>(ownedScripts(document).size()));
    for (const auto &owned : ownedScripts(document))
        view.append(owned.get());
}

//  Puts an owned page into the registry at `index`, returning the raw pointer.
//  将受持有的页面放入登记表的 `index` 处，并返回裸指针。
Page *insertOwnedPage(ProjectDocument *document, std::unique_ptr<Page> page, int index)
{
    std::vector<std::unique_ptr<Page>> *owned = pageStore(document);
    if (!owned || !page)
        return nullptr;
    Page *raw = page.get();
    if (index < 0 || index > static_cast<int>(owned->size()))
        index = static_cast<int>(owned->size());
    owned->insert(owned->begin() + index, std::move(page));
    return raw;
}

//  Detaches the page at `index` from the registry, returning ownership.
//  从登记表中取出 `index` 处的页面并返回其所有权。
std::unique_ptr<Page> takeOwnedPage(ProjectDocument *document, int index)
{
    std::vector<std::unique_ptr<Page>> *owned = pageStore(document);
    if (!owned || index < 0 || index >= static_cast<int>(owned->size()))
        return nullptr;
    std::unique_ptr<Page> page = std::move((*owned)[static_cast<size_t>(index)]);
    owned->erase(owned->begin() + index);
    return page;
}

} // namespace

// ---------------------------------------------------------------------------
//  Construction / destruction
//  构造与析构
// ---------------------------------------------------------------------------
ProjectDocument::ProjectDocument(QObject *parent)
    : QObject(parent)
    , id(IdGenerator::projectId())
{
}

ProjectDocument::~ProjectDocument()
{
    //  Pages and scripts live in the registries, so release both stores before
    //  the document address (their key) disappears.
    //  页面与脚本存放在登记表中，因此在文档地址（登记表的键）失效前释放两个存储。
    clearProgram();
    if (std::vector<std::unique_ptr<Page>> *store = pageStore(this)) {
        store->clear();
        delete store;
    }
    pageRegistry().remove(this);
    m_pages = sizedPageView(this);
}

// ---------------------------------------------------------------------------
//  Pages
//  页面
// ---------------------------------------------------------------------------
Page *ProjectDocument::pageAt(int index) const
{
    const std::vector<std::unique_ptr<Page>> &owned = ownedPages(this);
    if (index < 0 || index >= static_cast<int>(owned.size()))
        return nullptr;
    return owned[static_cast<size_t>(index)].get();
}

Page *ProjectDocument::pageById(const QString &pageId) const
{
    if (pageId.isEmpty())
        return nullptr;
    for (const auto &page : ownedPages(this)) {
        if (page && page->id == pageId)
            return page.get();
    }
    return nullptr;
}

Page *ProjectDocument::pageByName(const QString &name) const
{
    for (const auto &page : ownedPages(this)) {
        if (page && page->name == name)
            return page.get();
    }
    return nullptr;
}

int ProjectDocument::indexOfPage(const Page *page) const
{
    if (!page)
        return -1;
    const std::vector<std::unique_ptr<Page>> &owned = ownedPages(this);
    for (int i = 0; i < static_cast<int>(owned.size()); ++i) {
        if (owned[static_cast<size_t>(i)].get() == page)
            return i;
    }
    return -1;
}

Page *ProjectDocument::homePage() const
{
    const std::vector<std::unique_ptr<Page>> &owned = ownedPages(this);
    for (const auto &page : owned) {
        if (page && page->isHome)
            return page.get();
    }
    return owned.empty() ? nullptr : owned.front().get();
}

Page *ProjectDocument::addPage(std::unique_ptr<Page> page, int index)
{
    if (!page)
        return nullptr;

    //  First page in an empty document becomes the home page.
    //  空文档中的第一个页面自动成为首页。
    if (ownedPages(this).empty())
        page->isHome = true;

    Page *raw = insertOwnedPage(this, std::move(page), index);
    if (!raw)
        return nullptr;

    if (activePageId.isEmpty())
        activePageId = raw->id;

    m_pages = sizedPageView(this);
    emit structureChanged();
    return raw;
}

Page *ProjectDocument::addPage(const QString &name, bool chineseDefaults)
{
    QString pageName = name.trimmed();
    if (pageName.isEmpty()) {
        //  "page-2", "page-3", ... skipping any name already in use.
        //  "page-2"、"page-3"……跳过已被占用的名称。
        int counter = m_pageCounter + 1;
        do {
            pageName = QStringLiteral("page-%1").arg(counter);
            ++counter;
        } while (pageByName(pageName) != nullptr);
        m_pageCounter = counter - 1;
    }

    auto page = Page::makeDefault(pageName, chineseDefaults);
    if (!page)
        return nullptr;
    page->name = pageName;
    page->fileName = pageName + QStringLiteral(".html");
    page->isHome = ownedPages(this).empty();
    return addPage(std::move(page), -1);
}

std::unique_ptr<Page> ProjectDocument::takePage(int index)
{
    if (index < 0 || index >= static_cast<int>(ownedPages(this).size()))
        return nullptr;

    std::unique_ptr<Page> page = takeOwnedPage(this, index);

    if (page && activePageId == page->id) {
        activePageId = ownedPages(this).empty() ? QString()
                                                : ownedPages(this).front()->id;
    }
    if (page && page->isHome && !ownedPages(this).empty())
        ownedPages(this).front()->isHome = true;

    m_pages = sizedPageView(this);
    emit structureChanged();
    return page;
}

bool ProjectDocument::removePage(int index)
{
    if (index < 0 || index >= static_cast<int>(ownedPages(this).size()))
        return false;

    //  A page owns the scripts targeted at it, so drop them before destroying
    //  it (otherwise the program would reference pages that no longer exist).
    //  页面拥有指向它的脚本，因此销毁前先释放相关脚本，避免程序引用已不存在的页面。
    const Page *doomed = ownedPages(this)[static_cast<size_t>(index)].get();
    const QString removeId = doomed ? doomed->id : QString();
    if (std::vector<std::unique_ptr<BlockScript>> *store = scriptStore(this)) {
        std::vector<std::unique_ptr<BlockScript>> moved = std::move(*store);
        for (auto &owned : moved) {
            if (owned && !owned->pageTarget.isEmpty() && owned->pageTarget == removeId)
                continue;                 // destroyed here / 在此析构
            store->push_back(std::move(owned));
        }
        syncProgramView(this);
    }

    return takePage(index) != nullptr;
}

bool ProjectDocument::movePage(int from, int to)
{
    const int count = static_cast<int>(ownedPages(this).size());
    if (from < 0 || from >= count || to < 0 || to >= count || from == to)
        return false;

    std::vector<std::unique_ptr<Page>> *owned = pageStore(this);
    if (!owned)
        return false;
    if (from < to)
        std::rotate(owned->begin() + from, owned->begin() + from + 1, owned->begin() + to + 1);
    else
        std::rotate(owned->begin() + to, owned->begin() + from, owned->begin() + from + 1);

    m_pages = sizedPageView(this);
    emit structureChanged();
    return true;
}

// ---------------------------------------------------------------------------
//  Block programs
//  积木程序
// ---------------------------------------------------------------------------
QVector<BlockScript *> &ProjectDocument::program()
{
    //  The registry is the single source of truth; m_program is the cached view
    //  that everything else iterates over.
    //  登记表是唯一权威来源，m_program 是供其他代码遍历的缓存视图。
    return m_program;
}

const QVector<BlockScript *> &ProjectDocument::program() const
{
    static const QVector<BlockScript *> empty;
    if (!scriptRegistry().contains(this))
        return empty;
    return m_program;
}

void ProjectDocument::clearProgram()
{
    if (std::vector<std::unique_ptr<BlockScript>> *store = scriptStore(this)) {
        //  Destroying the owned store deletes every script (and its blocks).
        //  销毁持有存储即析构全部脚本（及其积木）。
        store->clear();
        delete store;
    }
    scriptRegistry().remove(this);
    m_program.clear();
}

BlockScript *ProjectDocument::scriptById(const QString &scriptId) const
{
    if (scriptId.isEmpty())
        return nullptr;
    for (BlockScript *script : m_program) {
        if (script && script->id == scriptId)
            return script;
    }
    return nullptr;
}

void ProjectDocument::addScript(std::unique_ptr<BlockScript> script, int index)
{
    if (!script)
        return;

    std::vector<std::unique_ptr<BlockScript>> *owned = scriptStore(this);
    if (!owned)
        return;
    if (index < 0 || index > static_cast<int>(owned->size()))
        index = static_cast<int>(owned->size());
    owned->insert(owned->begin() + index, std::move(script));
    syncProgramView(this);

    emit structureChanged();
}

// ---------------------------------------------------------------------------
//  Housekeeping
//  维护
// ---------------------------------------------------------------------------
void ProjectDocument::setModified(bool value)
{
    //  Only an actual transition notifies the UI, so Ctrl+S on a clean document
    //  does not flicker the title bar.
    //  只有状态真正变化时才通知界面，因此在干净文档上按 Ctrl+S 不会让标题栏闪烁。
    const bool previous = modified;
    modified = value;
    if (previous != value)
        emit modifiedChanged(value);
}

void ProjectDocument::touch()
{
    //  Always run through setModified so the emission rule lives in one place.
    //  一律经由 setModified，使信号发射规则只存在于一处。
    if (!modified)
        setModified(true);
}

std::unique_ptr<ProjectDocument> ProjectDocument::clone(bool keepIds) const
{
    auto copy = std::make_unique<ProjectDocument>();
    copy->id = keepIds ? id : IdGenerator::projectId();
    copy->name = name;
    copy->description = description;
    copy->author = author;
    copy->version = version;
    copy->defaultLanguage = defaultLanguage;
    copy->themeCss = themeCss;
    copy->sharedCssVariables = sharedCssVariables;
    copy->backendConfig = backendConfig;
    copy->aiConfig = aiConfig;
    copy->assetPaths = assetPaths;
    copy->activeBreakpoint = activeBreakpoint;
    copy->filePath = filePath;
    copy->m_pageCounter = m_pageCounter;

    for (const auto &page : ownedPages(this)) {
        if (!page)
            continue;
        copy->addPage(page->clone(keepIds), -1);
    }

    if (keepIds)
        copy->activePageId = activePageId;
    else if (const Page *source = pageById(activePageId))
        copy->activePageId = copy->pageAt(indexOfPage(source))->id;
    if (copy->activePageId.isEmpty() && copy->pageCount() > 0)
        copy->activePageId = copy->pageAt(0)->id;

    //  Scripts are cloned too so a duplicated project stays self-contained.
    //  脚本同样被克隆，使复制出的工程保持自包含。
    for (const BlockScript *script : m_program) {
        if (!script)
            continue;
        auto scriptCopy = script->clone(keepIds);
        if (!scriptCopy)
            continue;
        if (!keepIds && !script->pageTarget.isEmpty()) {
            const Page *target = pageById(script->pageTarget);
            const int targetIndex = indexOfPage(target);
            Page *clonedTarget = targetIndex >= 0 ? copy->pageAt(targetIndex) : nullptr;
            if (clonedTarget)
                scriptCopy->pageTarget = clonedTarget->id;
        }
        copy->addScript(std::move(scriptCopy), -1);
    }

    copy->setModified(false);
    return copy;
}

void ProjectDocument::clear()
{
    clearProgram();
    if (std::vector<std::unique_ptr<Page>> *store = pageStore(this)) {
        store->clear();
        delete store;
    }
    pageRegistry().remove(this);
    m_pages = sizedPageView(this);
    name = QStringLiteral("Untitled Project");
    description.clear();
    author.clear();
    version = QStringLiteral("0.1.0");
    defaultLanguage = QStringLiteral("en");
    themeCss.clear();
    sharedCssVariables.clear();
    backendConfig = QJsonObject();
    aiConfig = QJsonObject();
    assetPaths.clear();
    activePageId.clear();
    activeBreakpoint = Breakpoint::Desktop;
    filePath.clear();
    m_pageCounter = 0;
    setModified(false);
    emit structureChanged();
}

// ---------------------------------------------------------------------------
//  Serialisation
//  序列化
// ---------------------------------------------------------------------------
QJsonObject ProjectDocument::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("schemaVersion"), kSchemaVersion);
    obj.insert(QStringLiteral("id"), id);
    obj.insert(QStringLiteral("name"), name);
    obj.insert(QStringLiteral("description"), description);
    obj.insert(QStringLiteral("author"), author);
    obj.insert(QStringLiteral("version"), version);
    obj.insert(QStringLiteral("defaultLanguage"), defaultLanguage);
    obj.insert(QStringLiteral("themeCss"), themeCss);

    if (!sharedCssVariables.isEmpty()) {
        QJsonObject vars;
        for (auto it = sharedCssVariables.constBegin(); it != sharedCssVariables.constEnd(); ++it)
            vars.insert(it.key(), it.value());
        obj.insert(QStringLiteral("sharedCssVariables"), vars);
    }

    if (!backendConfig.isEmpty())
        obj.insert(QStringLiteral("backendConfig"), backendConfig);
    if (!aiConfig.isEmpty())
        obj.insert(QStringLiteral("aiConfig"), aiConfig);

    obj.insert(QStringLiteral("assetPaths"), QJsonArray::fromStringList(assetPaths));

    QJsonArray pageArray;
    for (const auto &page : ownedPages(this)) {
        if (page)
            pageArray.append(page->toJson());
    }
    obj.insert(QStringLiteral("pages"), pageArray);

    QJsonArray scriptArray;
    for (const BlockScript *script : m_program) {
        if (script)
            scriptArray.append(script->toJson());
    }
    obj.insert(QStringLiteral("program"), scriptArray);

    obj.insert(QStringLiteral("activePageId"), activePageId);
    obj.insert(QStringLiteral("activeBreakpoint"), toString(activeBreakpoint));

    return obj;
}

std::unique_ptr<ProjectDocument> ProjectDocument::fromJson(const QJsonObject &obj, QString *error)
{
    if (obj.isEmpty()) {
        if (error)
            *error = QStringLiteral("Empty project object / 工程对象为空");
        return nullptr;
    }

    auto document = std::make_unique<ProjectDocument>();
    document->id = obj.value(QStringLiteral("id")).toString();
    if (document->id.isEmpty())
        document->id = IdGenerator::projectId();

    document->name = obj.value(QStringLiteral("name")).toString(QStringLiteral("Untitled Project"));
    document->description = obj.value(QStringLiteral("description")).toString();
    document->author = obj.value(QStringLiteral("author")).toString();
    document->version = obj.value(QStringLiteral("version")).toString(QStringLiteral("0.1.0"));
    document->defaultLanguage = obj.value(QStringLiteral("defaultLanguage"))
                                    .toString(QStringLiteral("en"));
    document->themeCss = obj.value(QStringLiteral("themeCss")).toString();

    const QJsonObject vars = obj.value(QStringLiteral("sharedCssVariables")).toObject();
    for (auto it = vars.constBegin(); it != vars.constEnd(); ++it)
        document->sharedCssVariables.insert(it.key(), it.value().toString());

    document->backendConfig = obj.value(QStringLiteral("backendConfig")).toObject();
    document->aiConfig = obj.value(QStringLiteral("aiConfig")).toObject();

    for (const QJsonValue &v : obj.value(QStringLiteral("assetPaths")).toArray()) {
        if (v.isString())
            document->assetPaths.append(v.toString());
    }

    const QJsonArray pages = obj.value(QStringLiteral("pages")).toArray();
    for (const QJsonValue &v : pages) {
        QString pageError;
        auto page = Page::fromJson(v.toObject(), &pageError);
        if (!page) {
            if (error) {
                *error = pageError.isEmpty()
                             ? QStringLiteral("Could not read a page / 无法读取页面")
                             : pageError;
            }
            return nullptr;
        }
        document->addPage(std::move(page), -1);
    }

    //  A document with no pages has nothing to edit; treat it as corrupt.
    //  没有页面的文档无从编辑，视为损坏。
    if (document->pageCount() == 0) {
        if (error)
            *error = QStringLiteral("Project contains no pages / 工程不包含任何页面");
        return nullptr;
    }

    for (const QJsonValue &v : obj.value(QStringLiteral("program")).toArray()) {
        QString scriptError;
        auto script = BlockScript::fromJson(v.toObject(), &scriptError);
        if (!script) {
            if (error) {
                *error = scriptError.isEmpty()
                             ? QStringLiteral("Could not read a script / 无法读取脚本")
                             : scriptError;
            }
            return nullptr;
        }
        document->addScript(std::move(script), -1);
    }

    document->activePageId = obj.value(QStringLiteral("activePageId")).toString();
    if (!document->pageById(document->activePageId))
        document->activePageId = document->pageAt(0)->id;

    document->activeBreakpoint = breakpointFromString(
        obj.value(QStringLiteral("activeBreakpoint")).toString(), Breakpoint::Desktop);

    document->setModified(false);
    return document;
}

// ---------------------------------------------------------------------------
//  makeStarter -- a genuinely usable starting project
//  makeStarter —— 真正可用的初始工程
// ---------------------------------------------------------------------------
namespace {

//  Creates a block with its field values already filled in.
//  创建一个已填好字段值的积木。
std::unique_ptr<Block> makeBlock(const QString &type,
                                 std::initializer_list<QPair<QString, QString>> fields = {})
{
    auto block = std::make_unique<Block>(type);
    for (const auto &field : fields)
        block->setFieldValue(field.first, field.second);
    return block;
}

//  Appends an input chain (reporter feeding a socket).
//  追加一条输入链（返回值积木送入槽位）。
void attachInput(Block *owner, const QString &socket, std::unique_ptr<Block> value)
{
    if (!owner || !value)
        return;
    Block *raw = value.get();
    raw->parent = owner;
    raw->parentInput = socket;
    raw->parentIsStatement = false;
    owner->inputs[socket].append(raw);
    value.release();
}

} // namespace

std::unique_ptr<ProjectDocument> ProjectDocument::makeStarter(bool chineseDefaults)
{
    auto document = std::make_unique<ProjectDocument>();
    document->name = chineseDefaults ? QStringLiteral("我的第一个网站")
                                     : QStringLiteral("My first website");
    document->description = chineseDefaults
        ? QStringLiteral("由 TCTools-Build_Web 创建的起始工程。")
        : QStringLiteral("Starter project created by TCTools-Build_Web.");
    document->defaultLanguage = chineseDefaults ? QStringLiteral("zh") : QStringLiteral("en");

    // --- home page with a hero section and a call to action -----------------
    // --- 含主视觉区块与行动号召按钮的首页 -----------------------------------
    auto page = Page::makeDefault(QStringLiteral("index"), chineseDefaults);
    if (!page)
        page = std::make_unique<Page>(QStringLiteral("index"));
    page->isHome = true;
    page->name = QStringLiteral("index");
    page->fileName = QStringLiteral("index.html");
    page->title = chineseDefaults ? QStringLiteral("首页 · 我的第一个网站")
                                  : QStringLiteral("Home · My first website");

    //  Build the hero directly under <body>: a heading, a paragraph and a
    //  button that the click script below targets.
    auto hero = DocumentNode::makeElement(QStringLiteral("section"));
    hero->name = chineseDefaults ? QStringLiteral("主视觉") : QStringLiteral("Hero");
    hero->classes.append(QStringLiteral("tcbw-hero"));
    hero->style.base().set(QStringLiteral("display"), QStringLiteral("flex"));
    hero->style.base().set(QStringLiteral("flex-direction"), QStringLiteral("column"));
    hero->style.base().set(QStringLiteral("align-items"), QStringLiteral("center"));
    hero->style.base().set(QStringLiteral("text-align"), QStringLiteral("center"));
    hero->style.base().set(QStringLiteral("gap"), QStringLiteral("18px"));
    hero->style.base().set(QStringLiteral("padding"), QStringLiteral("80px 24px"));
    hero->style.base().set(QStringLiteral("background"),
                           QStringLiteral("linear-gradient(180deg, #eff6ff 0%, #ffffff 100%)"));

    auto heroTitle = DocumentNode::makeElement(QStringLiteral("h1"));
    heroTitle->text = chineseDefaults
        ? QStringLiteral("可视化构建，即刻发布")
        : QStringLiteral("Build visually, publish instantly");
    heroTitle->style.base().set(QStringLiteral("font-size"), QStringLiteral("2.75rem"));
    heroTitle->style.base().set(QStringLiteral("font-weight"), QStringLiteral("800"));
    heroTitle->style.base().set(QStringLiteral("line-height"), QStringLiteral("1.15"));
    heroTitle->style.base().set(QStringLiteral("margin"), QStringLiteral("0"));
    hero->appendChild(std::move(heroTitle));

    auto heroCopy = DocumentNode::makeElement(QStringLiteral("p"));
    heroCopy->text = chineseDefaults
        ? QStringLiteral("拖拽、连线、导出。整个过程无需手写一行代码。")
        : QStringLiteral("Drag, wire up behaviour, export. No hand-written code required.");
    heroCopy->style.base().set(QStringLiteral("font-size"), QStringLiteral("1.125rem"));
    heroCopy->style.base().set(QStringLiteral("line-height"), QStringLiteral("1.7"));
    heroCopy->style.base().set(QStringLiteral("max-width"), QStringLiteral("56ch"));
    heroCopy->style.base().set(QStringLiteral("opacity"), QStringLiteral("0.82"));
    heroCopy->style.base().set(QStringLiteral("margin"), QStringLiteral("0"));
    hero->appendChild(std::move(heroCopy));

    auto cta = DocumentNode::makeElement(QStringLiteral("button"));
    cta->text = chineseDefaults ? QStringLiteral("点击我") : QStringLiteral("Click me");
    cta->setAttribute(QStringLiteral("type"), QStringLiteral("button"));
    cta->classes.append(QStringLiteral("tcbw-cta"));
    cta->style.base().set(QStringLiteral("padding"), QStringLiteral("12px 26px"));
    cta->style.base().set(QStringLiteral("border"), QStringLiteral("0"));
    cta->style.base().set(QStringLiteral("border-radius"), QStringLiteral("10px"));
    cta->style.base().set(QStringLiteral("background"), QStringLiteral("#2563eb"));
    cta->style.base().set(QStringLiteral("color"), QStringLiteral("#ffffff"));
    cta->style.base().set(QStringLiteral("font-size"), QStringLiteral("1rem"));
    cta->style.base().set(QStringLiteral("font-weight"), QStringLiteral("600"));
    cta->style.base().set(QStringLiteral("cursor"), QStringLiteral("pointer"));
    hero->appendChild(std::move(cta));

    DocumentNode *heroNode = page->root->appendChild(std::move(hero));
    if (heroNode)
        page->ensureElementClass(heroNode);
    page->ensureElementClass(page->root.get());

    const QString pageId = page->id;
    document->addPage(std::move(page), -1);

    // --- script 1: "when page loads" -> log a message -----------------------
    // --- 脚本 1："当页面加载" -> 输出一条日志 --------------------------------
    {
        auto script = std::make_unique<BlockScript>(QStringLiteral("events.start"));
        script->name = chineseDefaults ? QStringLiteral("页面加载时")
                                       : QStringLiteral("When page loads");
        script->pageTarget = pageId;
        if (script->hat) {
            script->hat->setFieldValue(QStringLiteral("page"), pageId);
            script->hat->uiX = 40.0;
            script->hat->uiY = 40.0;
        }

        auto log = makeBlock(QStringLiteral("console.log"));
        log->setFieldValue(QStringLiteral("message"),
                           chineseDefaults
                               ? QStringLiteral("TCTools-Build_Web 已就绪")
                               : QStringLiteral("TCTools-Build_Web is ready"));
        script->addBlock(std::move(log), -1);
        document->addScript(std::move(script), -1);
    }

    // --- script 2: click handler on the call-to-action button --------------
    // --- 脚本 2：行动号召按钮的点击处理 -------------------------------------
    {
        auto script = std::make_unique<BlockScript>(QStringLiteral("events.click"));
        script->name = chineseDefaults ? QStringLiteral("按钮被点击时")
                                       : QStringLiteral("When button is clicked");
        script->pageTarget = pageId;
        if (script->hat) {
            script->hat->setFieldValue(QStringLiteral("page"), pageId);
            script->hat->setFieldValue(QStringLiteral("selector"),
                                       QStringLiteral(".tcbw-cta"));
            script->hat->uiX = 360.0;
            script->hat->uiY = 40.0;
        }

        //  ui.setText "Thanks for clicking!" on the button that was clicked.
        //  用 ui.setText 把"感谢点击！"写入被点击的按钮。
        auto setText = makeBlock(QStringLiteral("ui.setText"));
        setText->setFieldValue(QStringLiteral("selector"), QStringLiteral(".tcbw-cta"));
        attachInput(setText.get(), QStringLiteral("text"),
                    makeBlock(QStringLiteral("text.value"),
                              {{QStringLiteral("value"),
                                chineseDefaults ? QStringLiteral("感谢点击！")
                                                : QStringLiteral("Thanks for clicking!")}}));

        //  ui.addClass "tcbw-cta--active" so the style change is visible.
        //  用 ui.addClass 添加 "tcbw-cta--active"，让样式变化可见。
        auto addClass = makeBlock(QStringLiteral("ui.addClass"));
        addClass->setFieldValue(QStringLiteral("selector"), QStringLiteral(".tcbw-cta"));
        addClass->setFieldValue(QStringLiteral("className"),
                                QStringLiteral("tcbw-cta--active"));

        //  ui.setStyle background: green.
        //  用 ui.setStyle 把背景色改为绿色。
        auto setStyle = makeBlock(QStringLiteral("ui.setStyle"));
        setStyle->setFieldValue(QStringLiteral("selector"), QStringLiteral(".tcbw-cta"));
        setStyle->setFieldValue(QStringLiteral("property"), QStringLiteral("background"));
        setStyle->setFieldValue(QStringLiteral("value"), QStringLiteral("#16a34a"));

        script->addBlock(std::move(setText), -1);
        script->addBlock(std::move(addClass), -1);
        script->addBlock(std::move(setStyle), -1);
        document->addScript(std::move(script), -1);
    }

    document->activePageId = pageId;
    document->setModified(false);
    return document;
}

} // namespace tcbw
