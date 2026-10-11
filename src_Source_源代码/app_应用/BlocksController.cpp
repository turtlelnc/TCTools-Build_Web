// ============================================================================
//  BlocksController.cpp -- the Scratch-style block editor behind QML
//  BlocksController.cpp —— QML 背后的类 Scratch 积木编辑器
//
//  Part of TCTools-Build_Web (TC-BuildWeb).
//  属于 TCTools-Build_Web（TC-BuildWeb）。
// ============================================================================
#include "BlocksController.h"

#include "AppController.h"
#include "AppSettings.h"
#include "Commands.h"
#include "IdGenerator.h"
#include "Page.h"
#include "Types.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QSet>
#include <QTimer>
#include <QVariant>

#include <algorithm>

namespace tcbw {

namespace {

// ---------------------------------------------------------------------------
//  Hard limits
//  A corrupt project, a runaway AI loop or a hostile drop must never be able
//  to freeze the editor or blow up the generated program.
//  硬性上限
//  损坏的工程、失控的 AI 循环或恶意的拖放，绝不能冻结编辑器或撑爆生成的程序。
// ---------------------------------------------------------------------------
constexpr int kMaxScriptBlocks = 400;      // blocks per script / 每个脚本的积木数
constexpr int kMaxProgramBlocks = 2000;    // blocks per program / 每个程序的积木数
constexpr int kMaxRecursionDepth = 64;     // nesting depth of the QVariant tree / 变体树嵌套深度
constexpr int kMaxDryRunSteps = 25;        // blocks described by dryRun() / dryRun() 描述的积木数
constexpr int kMaxDiagnostics = 200;       // diagnostics shown in the UI / 界面显示的诊断数

// ---------------------------------------------------------------------------
//  Dynamic-property keys (the public header is frozen, so bookkeeping that is
//  an implementation detail lives in dynamic properties).
//  动态属性键（公开头文件已冻结，因此属于实现细节的记录保存在动态属性中）。
// ---------------------------------------------------------------------------
constexpr char kBoundDocumentProperty[] = "_tcbwBlocksBoundDocument";
constexpr char kRecompilePendingProperty[] = "_tcbwRecompilePending";

// ---------------------------------------------------------------------------
//  Localised helpers
//  本地化辅助函数
// ---------------------------------------------------------------------------
QString noProjectMessage()
{
    return QStringLiteral("No project is open. / 当前没有打开的工程。");
}

QString bilingual(const QString &en, const QString &zh)
{
    return en + QStringLiteral(" / ") + zh;
}

const BlockRegistry &blocks()
{
    return BlockRegistry::instance();
}

QString colourOf(const BlockDef &def)
{
    if (!def.colour.isEmpty())
        return def.colour;
    return blocks().colourForCategory(def.category);
}

QString labelEnOf(const BlockDef &def)
{
    return def.labelEn.isEmpty() ? def.type : def.labelEn;
}

QString labelZhOf(const BlockDef &def)
{
    return def.labelZh.isEmpty() ? labelEnOf(def) : def.labelZh;
}

// ---------------------------------------------------------------------------
//  The shape name the editor renders with.
//  BlockItem.qml switches on the PascalCase spellings of BlockShape
//  ("Reporter", "Boolean", "Round", "CShape", "EShape", "Stack", "Hat", "Cap"),
//  which are deliberately different from the stable lower-case keys
//  toString(BlockShape) writes into the project file. Emitting the file keys
//  here would silently disable the reporter styling and hide every nested
//  statement branch.
//  编辑器渲染所用的形状名称。
//  BlockItem.qml 依据 BlockShape 的帕斯卡命名（"Reporter"、"Boolean"、"Round"、
//  "CShape"、"EShape"、"Stack"、"Hat"、"Cap"）分支，它们刻意不同于
//  toString(BlockShape) 写入工程文件的稳定小写键。在此输出文件键会悄悄地关闭
//  返回值样式并隐藏所有嵌套语句分支。
// ---------------------------------------------------------------------------
QString shapeName(BlockShape shape)
{
    switch (shape) {
    case BlockShape::Hat:      return QStringLiteral("Hat");
    case BlockShape::Stack:    return QStringLiteral("Stack");
    case BlockShape::CShape:   return QStringLiteral("CShape");
    case BlockShape::EShape:   return QStringLiteral("EShape");
    case BlockShape::Cap:      return QStringLiteral("Cap");
    case BlockShape::Reporter: return QStringLiteral("Reporter");
    case BlockShape::Boolean:  return QStringLiteral("Boolean");
    case BlockShape::Round:    return QStringLiteral("Round");
    }
    return QStringLiteral("Stack");
}

QString languageOf(const AppController *app)
{
    if (app)
        return app->language();
    return AppSettings::instance().language();
}

bool isChinese(const AppController *app)
{
    const QString language = languageOf(app);
    return language.startsWith(QLatin1String("zh"));
}

// ---------------------------------------------------------------------------
//  Socket compatibility.
//  BlockDef declares the shape of a block but no declared return kind, so the
//  checker works from the shape: a boolean socket only accepts a hexagon, a
//  numeric socket accepts a hexagon/oval, and every other kind accepts any
//  value-returning shape. `Any` accepts everything.
//  槽位兼容性。
//  BlockDef 声明了积木的形状，但没有声明返回类型，因此检查器依据形状判断：
//  布尔槽只接受六边形，数值槽接受六边形/椭圆形，其余种类接受任何有返回值的
//  形状。`Any` 接受一切。
// ---------------------------------------------------------------------------
bool kindAccepts(ValueKind socket, BlockShape shape)
{
    if (!shapeReturnsValue(shape))
        return false;
    switch (socket) {
    case ValueKind::Any:
        return true;
    case ValueKind::Boolean:
        return shape == BlockShape::Boolean;
    case ValueKind::Number:
        return shape == BlockShape::Round || shape == BlockShape::Reporter;
    case ValueKind::String:
    case ValueKind::Colour:
    case ValueKind::Element:
    case ValueKind::List:
    case ValueKind::Object:
    case ValueKind::Function:
    default:
        return shape == BlockShape::Reporter || shape == BlockShape::Round
            || shape == BlockShape::Boolean;
    }
}

int countBlocks(const BlockScript &script)
{
    int count = 0;
    script.forEach([&count](const Block *) { ++count; });
    return count;
}

int countBlocks(const ProjectDocument &document)
{
    int count = 0;
    for (const BlockScript *script : document.program()) {
        if (script)
            count += countBlocks(*script);
    }
    return count;
}

QString scriptLabel(const BlockScript &script, bool chinese)
{
    if (!script.name.trimmed().isEmpty())
        return script.name.trimmed();
    if (const BlockDef *def = blocks().find(script.eventType))
        return chinese ? labelZhOf(*def) : labelEnOf(*def);
    return script.eventType;
}

// ---------------------------------------------------------------------------
//  The script that owns a block, or null when the block no longer exists.
//  拥有某个积木的脚本；当积木已不存在时返回空。
// ---------------------------------------------------------------------------
BlockScript *ownerScript(ProjectDocument *document, const QString &blockId)
{
    if (!document || blockId.isEmpty())
        return nullptr;
    for (BlockScript *script : document->program()) {
        if (!script)
            continue;
        bool found = false;
        script->forEach([&found, &blockId](const Block *candidate) {
            if (!found && candidate && candidate->id == blockId)
                found = true;
        });
        if (found)
            return script;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
//  Propagates the script's enabled flag through a serialised block tree, so a
//  disabled script renders every one of its blocks dimmed.
//  把脚本的启用标志传播到序列化后的积木树，使已禁用的脚本中每个积木都显示为
//  变暗状态。
// ---------------------------------------------------------------------------
void patchEnabled(QVariantMap &block, bool enabled)
{
    block.insert(QStringLiteral("enabled"), enabled);

    QVariantList statements = block.value(QStringLiteral("statements")).toList();
    for (int i = 0; i < statements.size(); ++i) {
        QVariantMap branch = statements.at(i).toMap();
        QVariantList children = branch.value(QStringLiteral("blocks")).toList();
        for (int j = 0; j < children.size(); ++j) {
            QVariantMap child = children.at(j).toMap();
            patchEnabled(child, enabled);
            children[j] = child;
        }
        branch.insert(QStringLiteral("blocks"), children);
        statements[i] = branch;
    }
    block.insert(QStringLiteral("statements"), statements);

    QVariantList inputs = block.value(QStringLiteral("inputs")).toList();
    for (int i = 0; i < inputs.size(); ++i) {
        QVariantMap socket = inputs.at(i).toMap();
        QVariantList children = socket.value(QStringLiteral("blocks")).toList();
        for (int j = 0; j < children.size(); ++j) {
            QVariantMap child = children.at(j).toMap();
            patchEnabled(child, enabled);
            children[j] = child;
        }
        socket.insert(QStringLiteral("blocks"), children);
        inputs[i] = socket;
    }
    block.insert(QStringLiteral("inputs"), inputs);
}

// ---------------------------------------------------------------------------
//  The first field value that looks like an element id, used by dryRun() to
//  resolve a block against the current page.
//  第一个看起来像元素 id 的字段值，dryRun() 用它把积木解析到当前页面。
// ---------------------------------------------------------------------------
QString firstElementCandidate(const Block &block)
{
    for (auto it = block.fieldValues.constBegin(); it != block.fieldValues.constEnd(); ++it) {
        const QString value = it.value().trimmed();
        if (!value.isEmpty() && IdGenerator::isValid(value))
            return value;
    }
    return QString();
}

// ---------------------------------------------------------------------------
//  The observable effect a block would have, plus its localisation.
//  积木将会产生的可观察效果及其本地化文本。
// ---------------------------------------------------------------------------
QStringList effectFor(const BlockDef *def, const Block &block, const Page *page)
{
    QStringList out;

    if (!def) {
        out << QStringLiteral("This block has no definition and will not run.")
            << QStringLiteral("该积木没有定义，不会运行。");
        return out;
    }

    const QString target = firstElementCandidate(block);
    if (!target.isEmpty()) {
        const DocumentNode *node = page ? page->findNode(target) : nullptr;
        if (node) {
            out << QStringLiteral("Applies to <%1> (#%2).").arg(node->tag, target)
                << QStringLiteral("作用于 <%1>（#%2）。").arg(node->tag, target);
        } else {
            out << QStringLiteral("Element #%1 is not on this page, so nothing happens.")
                       .arg(target)
                << QStringLiteral("元素 #%1 不在本页面上，因此什么也不会发生。").arg(target);
        }
        return out;
    }

    if (def->shape == BlockShape::Hat) {
        out << QStringLiteral("Starts this script when the event fires.")
            << QStringLiteral("事件触发时启动本脚本。");
        return out;
    }

    //  Categories that leave the page (network / storage / AI).
    //  离开页面的分类（网络 / 存储 / AI）。
    static const QStringList outOfPage = {QStringLiteral("http"), QStringLiteral("fetch"),
                                          QStringLiteral("ai"),   QStringLiteral("data"),
                                          QStringLiteral("storage"), QStringLiteral("cloud")};
    if (outOfPage.contains(def->category)) {
        out << QStringLiteral("Performs work outside the page; open the preview to observe it.")
            << QStringLiteral("在页面之外执行工作；请打开预览观察结果。");
        return out;
    }

    if (def->shape == BlockShape::CShape || def->shape == BlockShape::EShape) {
        out << QStringLiteral("Runs the nested blocks when its condition holds.")
            << QStringLiteral("条件成立时运行嵌套的积木。");
        return out;
    }

    out << QStringLiteral("Runs \"%1\".").arg(labelEnOf(*def))
        << QStringLiteral("运行“%1”。").arg(labelZhOf(*def));
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
//  Construction
//  构造
// ---------------------------------------------------------------------------
BlocksController::BlocksController(AppController *app, QObject *parent)
    : QObject(parent)
{
    setAppController(app);
}

BlocksController::~BlocksController() = default;

void BlocksController::setAppController(AppController *app)
{
    if (m_app == app)
        return;
    if (m_app)
        m_app->disconnect(this);

    m_app = app;
    if (!m_app)
        return;

    //  The document object is replaced on every new/open, so the document
    //  signals are (re)bound whenever projectChanged() arrives.
    //  用户每次新建 / 打开工程都会替换文档对象，因此每当 projectChanged() 到达时
    //  都（重新）绑定文档信号。
    auto bindDocument = [this]() -> bool {
        ProjectDocument *document = m_app ? m_app->document() : nullptr;
        QObject *previous = property(kBoundDocumentProperty).value<QObject *>();
        if (previous == document)
            return false;
        if (previous)
            previous->disconnect(this);
        setProperty(kBoundDocumentProperty,
                    QVariant::fromValue(static_cast<QObject *>(document)));
        if (!document)
            return true;
        connect(document, &ProjectDocument::structureChanged, this,
                &BlocksController::onStructureChanged);
        connect(document, &ProjectDocument::pageChanged, this,
                [this](const QString &) { onStructureChanged(); });
        return true;
    };

    bindDocument();

    connect(m_app, &AppController::projectChanged, this, [this, bindDocument]() {
        //  Selections are dropped by onStructureChanged() when they no longer
        //  resolve; a fresh document never contains the old ids.
        //  当选择不再有效时由 onStructureChanged() 清除；新文档绝不会包含旧 id。
        bindDocument();
        onStructureChanged();
    });
    connect(m_app, &AppController::currentPageChanged, this, [this](const QString &) {
        //  Scripts may be bound to a single page, so the list is refreshed.
        //  脚本可能绑定到单个页面，因此刷新列表。
        emit scriptsChanged();
    });
    connect(m_app, &AppController::projectStructureChanged, this,
            &BlocksController::onStructureChanged);

    onStructureChanged();
}

// ---------------------------------------------------------------------------
//  Small lookups
//  小型查询
// ---------------------------------------------------------------------------
BlockScript *BlocksController::script(const QString &scriptId) const
{
    if (!m_app || !m_app->document() || scriptId.isEmpty())
        return nullptr;
    return m_app->document()->scriptById(scriptId);
}

Block *BlocksController::block(const QString &blockId) const
{
    BlockScript *owner = ownerScript(m_app ? m_app->document() : nullptr, blockId);
    if (!owner)
        return nullptr;
    Block *found = nullptr;
    owner->forEach([&found, &blockId](Block *candidate) {
        if (!found && candidate && candidate->id == blockId)
            found = candidate;
    });
    return found;
}

// ---------------------------------------------------------------------------
//  Toolbox
//  工具箱
// ---------------------------------------------------------------------------
QVariantList BlocksController::categories() const
{
    QVariantList list;
    for (const BlockCategory *category : blocks().visibleCategories(m_showAdvanced)) {
        if (!category)
            continue;
        QVariantMap entry;
        entry.insert(QStringLiteral("key"), category->key);
        entry.insert(QStringLiteral("labelEn"), category->labelEn);
        entry.insert(QStringLiteral("labelZh"),
                     category->labelZh.isEmpty() ? category->labelEn : category->labelZh);
        entry.insert(QStringLiteral("colour"), category->colour);
        entry.insert(QStringLiteral("icon"), category->icon);
        entry.insert(QStringLiteral("descriptionEn"), category->descriptionEn);
        entry.insert(QStringLiteral("descriptionZh"),
                     category->descriptionZh.isEmpty() ? category->descriptionEn
                                                       : category->descriptionZh);
        entry.insert(QStringLiteral("advanced"), category->advanced);
        entry.insert(QStringLiteral("order"), category->order);
        entry.insert(QStringLiteral("blockCount"),
                     static_cast<int>(blocks().byCategory(category->key).size()));
        list.append(entry);
    }
    return list;
}

QVariantList BlocksController::toolbox() const
{
    QVariantList list;
    for (const BlockCategory *category : blocks().visibleCategories(m_showAdvanced)) {
        if (!category)
            continue;
        QVariantMap entry;
        entry.insert(QStringLiteral("category"), category->key);
        entry.insert(QStringLiteral("key"), category->key);
        entry.insert(QStringLiteral("labelEn"), category->labelEn);
        entry.insert(QStringLiteral("labelZh"),
                     category->labelZh.isEmpty() ? category->labelEn : category->labelZh);
        entry.insert(QStringLiteral("colour"), category->colour);
        entry.insert(QStringLiteral("icon"), category->icon);

        QVariantList items;
        for (const BlockDef *def : blocks().byCategory(category->key)) {
            if (def)
                items.append(describeBlockDef(*def));
        }
        entry.insert(QStringLiteral("blocks"), items);
        list.append(entry);
    }
    return list;
}

QVariantList BlocksController::blocksInCategory(const QString &categoryKey) const
{
    QVariantList list;
    for (const BlockDef *def : blocks().byCategory(categoryKey.trimmed())) {
        if (def)
            list.append(describeBlockDef(*def));
    }
    return list;
}

QVariantList BlocksController::searchBlocks(const QString &needle) const
{
    QVariantList list;
    if (needle.trimmed().isEmpty())
        return list;
    for (const BlockDef *def : blocks().search(needle)) {
        if (def)
            list.append(describeBlockDef(*def));
    }
    return list;
}

QVariantMap BlocksController::blockDefinition(const QString &type) const
{
    if (const BlockDef *def = blocks().find(type.trimmed()))
        return describeBlockDef(*def);
    return QVariantMap();
}

// ---------------------------------------------------------------------------
//  Definition serialisation
//  定义的序列化
// ---------------------------------------------------------------------------
QVariantMap BlocksController::describeBlockDef(const BlockDef &def) const
{
    QVariantMap map;
    map.insert(QStringLiteral("type"), def.type);
    map.insert(QStringLiteral("category"), def.category);
    map.insert(QStringLiteral("labelEn"), labelEnOf(def));
    map.insert(QStringLiteral("labelZh"), labelZhOf(def));
    map.insert(QStringLiteral("tooltipEn"), def.tooltipEn);
    map.insert(QStringLiteral("tooltipZh"),
               def.tooltipZh.isEmpty() ? def.tooltipEn : def.tooltipZh);
    //  BlockItem.qml switches on these exact shape names.
    //  BlockItem.qml 依据这些确切的形状名称分支。
    map.insert(QStringLiteral("shape"), shapeName(def.shape));
    map.insert(QStringLiteral("colour"), colourOf(def));
    map.insert(QStringLiteral("returnsValue"), def.returnsValue());
    map.insert(QStringLiteral("hasStatements"), def.hasNestedStatements());
    map.insert(QStringLiteral("terminal"), def.terminal);
    map.insert(QStringLiteral("experimental"), def.experimental);
    map.insert(QStringLiteral("keywords"), def.keywords);

    QVariantList fields;
    fields.reserve(def.fields.size());
    for (const BlockFieldDef &field : def.fields) {
        QVariantMap entry;
        entry.insert(QStringLiteral("name"), field.name);
        entry.insert(QStringLiteral("labelEn"), field.labelEn);
        entry.insert(QStringLiteral("labelZh"), field.labelZh.isEmpty() ? field.labelEn
                                                                       : field.labelZh);
        entry.insert(QStringLiteral("kind"), toString(field.kind));
        entry.insert(QStringLiteral("defaultValue"), field.defaultValue);
        entry.insert(QStringLiteral("value"), field.defaultValue);
        entry.insert(QStringLiteral("options"), field.options);
        entry.insert(QStringLiteral("optionLabelsZh"), field.optionLabelsZh);
        entry.insert(QStringLiteral("min"), field.minValue);
        entry.insert(QStringLiteral("max"), field.maxValue);
        entry.insert(QStringLiteral("hasRange"), field.hasRange);
        entry.insert(QStringLiteral("prefix"), field.prefix);
        fields.append(entry);
    }
    map.insert(QStringLiteral("fields"), fields);

    QVariantList inputs;
    inputs.reserve(def.inputs.size());
    for (const BlockInputDef &input : def.inputs) {
        QVariantMap entry;
        entry.insert(QStringLiteral("name"), input.name);
        entry.insert(QStringLiteral("labelEn"), input.labelEn);
        entry.insert(QStringLiteral("labelZh"), input.labelZh.isEmpty() ? input.labelEn
                                                                       : input.labelZh);
        entry.insert(QStringLiteral("kind"), toString(input.kind));
        entry.insert(QStringLiteral("shadowType"), input.shadowType);
        entry.insert(QStringLiteral("shadowValue"), input.shadowValue);
        entry.insert(QStringLiteral("acceptsBlocks"), input.acceptsBlocks);
        inputs.append(entry);
    }
    map.insert(QStringLiteral("inputs"), inputs);

    QVariantList statements;
    statements.reserve(def.statements.size());
    for (const BlockStatementDef &statement : def.statements) {
        QVariantMap entry;
        entry.insert(QStringLiteral("name"), statement.name);
        entry.insert(QStringLiteral("labelEn"), statement.labelEn);
        entry.insert(QStringLiteral("labelZh"),
                     statement.labelZh.isEmpty() ? statement.labelEn : statement.labelZh);
        entry.insert(QStringLiteral("minBlocks"), statement.minBlocks);
        entry.insert(QStringLiteral("maxBlocks"), statement.maxBlocks);
        statements.append(entry);
    }
    map.insert(QStringLiteral("statements"), statements);
    return map;
}

// ---------------------------------------------------------------------------
//  Instance serialisation
//  实例的序列化
// ---------------------------------------------------------------------------
QVariantMap BlocksController::blockToVariant(const Block &b, int depth) const
{
    const BlockDef *def = blocks().find(b.type);

    QVariantMap map;
    map.insert(QStringLiteral("id"), b.id);
    map.insert(QStringLiteral("type"), b.type);
    map.insert(QStringLiteral("labelEn"), def ? labelEnOf(*def) : b.type);
    map.insert(QStringLiteral("labelZh"), def ? labelZhOf(*def) : b.type);
    map.insert(QStringLiteral("colour"),
               def ? colourOf(*def) : QStringLiteral("#8E8E93"));
    map.insert(QStringLiteral("shape"), def ? shapeName(def->shape) : QStringLiteral("Stack"));
    //  A block has no enabled flag of its own; the owning script's flag is
    //  propagated by selectedScriptBlocks().
    //  积木自身没有启用标志；所属脚本的标志由 selectedScriptBlocks() 传播。
    map.insert(QStringLiteral("enabled"), true);
    map.insert(QStringLiteral("depth"), depth);
    map.insert(QStringLiteral("collapsed"), b.uiCollapsed);
    map.insert(QStringLiteral("selected"), b.id == m_selectedBlockId);
    map.insert(QStringLiteral("x"), b.uiX);
    map.insert(QStringLiteral("y"), b.uiY);
    map.insert(QStringLiteral("errorEn"), QString());
    map.insert(QStringLiteral("errorZh"), QString());

    // --- fields ------------------------------------------------------------
    QVariantList fields;
    if (def) {
        fields.reserve(def->fields.size());
        for (const BlockFieldDef &field : def->fields) {
            QVariantMap entry;
            entry.insert(QStringLiteral("name"), field.name);
            entry.insert(QStringLiteral("labelEn"), field.labelEn);
            entry.insert(QStringLiteral("labelZh"),
                         field.labelZh.isEmpty() ? field.labelEn : field.labelZh);
            entry.insert(QStringLiteral("kind"), toString(field.kind));
            entry.insert(QStringLiteral("defaultValue"), field.defaultValue);
            //  BlockItem.qml reads `value` first and falls back to `defaultValue`.
            //  BlockItem.qml 优先读取 `value`，缺失时回退到 `defaultValue`。
            entry.insert(QStringLiteral("value"),
                         b.fieldValues.contains(field.name) ? b.fieldValues.value(field.name)
                                                            : field.defaultValue);
            entry.insert(QStringLiteral("options"), field.options);
            entry.insert(QStringLiteral("optionLabelsZh"), field.optionLabelsZh);
            entry.insert(QStringLiteral("min"), field.minValue);
            entry.insert(QStringLiteral("max"), field.maxValue);
            entry.insert(QStringLiteral("hasRange"), field.hasRange);
            entry.insert(QStringLiteral("prefix"), field.prefix);
            fields.append(entry);
        }
    } else {
        //  A block from a newer or corrupt file: show its raw values so the user
        //  can still see and repair them.
        //  来自更新版本或损坏文件的积木：显示其原始值，使用户仍能看到并修复。
        for (auto it = b.fieldValues.constBegin(); it != b.fieldValues.constEnd(); ++it) {
            QVariantMap entry;
            entry.insert(QStringLiteral("name"), it.key());
            entry.insert(QStringLiteral("labelEn"), it.key());
            entry.insert(QStringLiteral("labelZh"), it.key());
            entry.insert(QStringLiteral("kind"), toString(ValueKind::String));
            entry.insert(QStringLiteral("defaultValue"), QString());
            entry.insert(QStringLiteral("value"), it.value());
            entry.insert(QStringLiteral("options"), QStringList());
            entry.insert(QStringLiteral("optionLabelsZh"), QStringList());
            entry.insert(QStringLiteral("min"), 0.0);
            entry.insert(QStringLiteral("max"), 0.0);
            entry.insert(QStringLiteral("hasRange"), false);
            entry.insert(QStringLiteral("prefix"), QString());
            fields.append(entry);
        }
    }
    map.insert(QStringLiteral("fields"), fields);

    //  Recursion is capped: a hand-edited or hostile file may nest blocks far
    //  deeper than any real script.
    //  递归有上限：手工编辑或恶意文件可能嵌套得比任何真实脚本都深。
    const bool recurse = depth < kMaxRecursionDepth;

    QVariantList inputs;
    if (def) {
        inputs.reserve(def->inputs.size());
        for (const BlockInputDef &input : def->inputs) {
            QVariantMap entry;
            entry.insert(QStringLiteral("name"), input.name);
            entry.insert(QStringLiteral("labelEn"), input.labelEn);
            entry.insert(QStringLiteral("labelZh"),
                         input.labelZh.isEmpty() ? input.labelEn : input.labelZh);
            entry.insert(QStringLiteral("kind"), toString(input.kind));
            entry.insert(QStringLiteral("shadowType"), input.shadowType);
            entry.insert(QStringLiteral("shadowValue"), input.shadowValue);
            QVariantList children;
            if (recurse) {
                const QVector<Block *> connected = b.inputBlocks(input.name);
                for (const Block *child : connected) {
                    if (child)
                        children.append(blockToVariant(*child, depth + 1));
                }
            }
            entry.insert(QStringLiteral("blocks"), children);
            inputs.append(entry);
        }
    }
    map.insert(QStringLiteral("inputs"), inputs);

    QVariantList statements;
    if (def) {
        statements.reserve(def->statements.size());
        for (const BlockStatementDef &statement : def->statements) {
            QVariantMap entry;
            entry.insert(QStringLiteral("name"), statement.name);
            entry.insert(QStringLiteral("labelEn"), statement.labelEn);
            entry.insert(QStringLiteral("labelZh"),
                         statement.labelZh.isEmpty() ? statement.labelEn : statement.labelZh);
            entry.insert(QStringLiteral("minBlocks"), statement.minBlocks);
            entry.insert(QStringLiteral("maxBlocks"), statement.maxBlocks);
            QVariantList children;
            if (recurse) {
                const QVector<Block *> nested = b.statementBlocks(statement.name);
                for (const Block *child : nested) {
                    if (child)
                        children.append(blockToVariant(*child, depth + 1));
                }
            }
            entry.insert(QStringLiteral("blocks"), children);
            statements.append(entry);
        }
    }
    map.insert(QStringLiteral("statements"), statements);
    return map;
}

// ---------------------------------------------------------------------------
//  Scripts
//  脚本
// ---------------------------------------------------------------------------
QVariantList BlocksController::scripts() const
{
    QVariantList list;
    ProjectDocument *document = m_app ? m_app->document() : nullptr;
    if (!document)
        return list;

    const bool chinese = isChinese(m_app);
    for (const BlockScript *script : document->program()) {
        if (!script)
            continue;
        const BlockDef *hatDef = blocks().find(script->eventType);
        const int blocksInScript = countBlocks(*script);
        const QString label = hatDef ? (chinese ? labelZhOf(*hatDef) : labelEnOf(*hatDef))
                                     : script->eventType;

        QVariantMap entry;
        entry.insert(QStringLiteral("id"), script->id);
        entry.insert(QStringLiteral("name"),
                     script->name.trimmed().isEmpty() ? label : script->name.trimmed());
        entry.insert(QStringLiteral("eventType"), script->eventType);
        entry.insert(QStringLiteral("colour"),
                     hatDef ? colourOf(*hatDef) : QStringLiteral("#8E8E93"));
        entry.insert(QStringLiteral("enabled"), script->enabled);
        entry.insert(QStringLiteral("x"), script->uiX);
        entry.insert(QStringLiteral("y"), script->uiY);
        entry.insert(QStringLiteral("blockCount"), blocksInScript);
        entry.insert(QStringLiteral("pageTarget"), script->pageTarget);
        entry.insert(QStringLiteral("selected"), script->id == m_selectedScriptId);
        entry.insert(QStringLiteral("previewEn"),
                     QStringLiteral("%1  ·  %2 blocks")
                         .arg(hatDef ? labelEnOf(*hatDef) : script->eventType)
                         .arg(blocksInScript));
        entry.insert(QStringLiteral("previewZh"),
                     QStringLiteral("%1  ·  %2 个积木")
                         .arg(hatDef ? labelZhOf(*hatDef) : script->eventType)
                         .arg(blocksInScript));
        list.append(entry);
    }
    return list;
}

QVariantMap BlocksController::selectedBlock() const
{
    Block *current = block(m_selectedBlockId);
    if (!current)
        return QVariantMap();
    return blockToVariant(*current, 0);
}

QVariantList BlocksController::selectedScriptBlocks() const
{
    QVariantList list;
    BlockScript *current = script(m_selectedScriptId);
    if (!current)
        return list;

    list.reserve(current->body.size());
    for (const Block *child : current->body) {
        if (!child)
            continue;
        QVariantMap entry = blockToVariant(*child, 0);
        patchEnabled(entry, current->enabled);
        list.append(entry);
    }
    return list;
}

QVariantMap BlocksController::blockById(const QString &blockId) const
{
    Block *found = block(blockId);
    if (!found)
        return QVariantMap();
    return blockToVariant(*found, 0);
}

// ---------------------------------------------------------------------------
//  Selection
//  选择
// ---------------------------------------------------------------------------
void BlocksController::selectScript(const QString &scriptId)
{
    const QString wanted = scriptId.trimmed();
    if (!wanted.isEmpty() && !script(wanted))
        return;
    if (m_selectedScriptId == wanted && m_selectedBlockId.isEmpty())
        return;
    m_selectedScriptId = wanted;
    m_selectedBlockId.clear();
    emit selectionChanged();
    emit scriptsChanged();
}

void BlocksController::selectBlock(const QString &blockId)
{
    const QString wanted = blockId.trimmed();
    if (wanted.isEmpty()) {
        if (m_selectedBlockId.isEmpty())
            return;
        m_selectedBlockId.clear();
        emit selectionChanged();
        return;
    }
    BlockScript *owner = ownerScript(m_app ? m_app->document() : nullptr, wanted);
    if (!owner)
        return;
    m_selectedBlockId = wanted;
    m_selectedScriptId = owner->id;
    emit selectionChanged();
    emit scriptsChanged();
}

// ---------------------------------------------------------------------------
//  Script CRUD
//  脚本增删改
// ---------------------------------------------------------------------------
QString BlocksController::addScript(const QString &eventType)
{
    ProjectDocument *document = m_app ? m_app->document() : nullptr;
    CommandStack *commands = m_app ? m_app->commands() : nullptr;
    if (!document || !commands) {
        if (m_app)
            m_app->reportError(noProjectMessage());
        return QString();
    }

    QString wanted = eventType.trimmed();
    const BlockDef *hatDef = wanted.isEmpty() ? nullptr : blocks().find(wanted);
    if (!hatDef || hatDef->shape != BlockShape::Hat) {
        //  A script must start with a hat block; anything else falls back to
        //  "when the page starts".
        //  脚本必须以帽子块开始；其他任何类型都回退到“当页面启动时”。
        if (!wanted.isEmpty() && m_app) {
            m_app->reportError(
                bilingual(QStringLiteral("Unknown event type \"%1\"; using events.start.")
                              .arg(wanted),
                          QStringLiteral("未知的事件类型 \"%1\"；改用 events.start。")
                              .arg(wanted)));
        }
        wanted = QStringLiteral("events.start");
        hatDef = blocks().find(wanted);
    }
    if (!hatDef) {
        if (m_app) {
            m_app->reportError(bilingual(
                QStringLiteral("The block catalogue does not contain \"events.start\"."),
                QStringLiteral("积木目录中不包含 \"events.start\"。")));
        }
        return QString();
    }

    auto created = std::make_unique<BlockScript>(wanted);
    created->name = hatDef->labelEn;
    //  A new script listens on the page the user is editing.
    //  新脚本监听用户正在编辑的页面。
    created->pageTarget = m_app ? m_app->currentPageId() : QString();
    const QString newId = created->id;

    commands->pushIfEffective(
        new AddScriptCommand(document, std::move(created), -1));

    m_selectedScriptId = newId;
    m_selectedBlockId.clear();
    emit selectionChanged();
    onStructureChanged();
    return newId;
}

bool BlocksController::removeScript(const QString &scriptId)
{
    ProjectDocument *document = m_app ? m_app->document() : nullptr;
    CommandStack *commands = m_app ? m_app->commands() : nullptr;
    if (!document || !commands) {
        if (m_app)
            m_app->reportError(noProjectMessage());
        return false;
    }
    if (!document->scriptById(scriptId)) {
        if (m_app) {
            m_app->reportError(bilingual(
                QStringLiteral("The script no longer exists."),
                QStringLiteral("该脚本已不存在。")));
        }
        return false;
    }

    commands->pushIfEffective(new RemoveScriptCommand(document, scriptId));

    if (m_selectedScriptId == scriptId) {
        m_selectedScriptId.clear();
        m_selectedBlockId.clear();
        emit selectionChanged();
    }
    onStructureChanged();
    return true;
}

//  There is no command for these two edits in Commands.h, so they change only
//  editor presentation state, are written straight into the document and are
//  intentionally not undoable.
//  Commands.h 中没有这两个编辑对应的命令，因此它们只改变编辑器的呈现状态，
//  直接写入文档，并且刻意不可撤销。
bool BlocksController::renameScript(const QString &scriptId, const QString &name)
{
    BlockScript *target = script(scriptId);
    if (!target) {
        if (m_app) {
            m_app->reportError(bilingual(
                QStringLiteral("The script no longer exists."),
                QStringLiteral("该脚本已不存在。")));
        }
        return false;
    }

    QString wanted = name.trimmed();
    if (wanted.isEmpty()) {
        if (const BlockDef *def = blocks().find(target->eventType))
            wanted = labelEnOf(*def);
        else
            wanted = target->eventType;
    }
    if (target->name == wanted)
        return true;

    target->name = wanted;
    if (ProjectDocument *document = m_app->document())
        document->touch();
    emit scriptsChanged();
    //  The script name feeds the generated function name, so the code changes.
    //  脚本名会参与生成函数名，因此代码需要重新生成。
    scheduleRecompile();
    return true;
}

//  No command exists for enabling/disabling a script: it is a non-destructive
//  presentation flag, so it is applied directly and is not undoable.
//  没有命令用于启用 / 停用脚本：它是非破坏性的呈现标志，因此直接应用且不可撤销。
bool BlocksController::setScriptEnabled(const QString &scriptId, bool enabled)
{
    BlockScript *target = script(scriptId);
    if (!target) {
        if (m_app) {
            m_app->reportError(bilingual(
                QStringLiteral("The script no longer exists."),
                QStringLiteral("该脚本已不存在。")));
        }
        return false;
    }
    if (target->enabled == enabled)
        return true;

    target->enabled = enabled;
    if (ProjectDocument *document = m_app->document())
        document->touch();
    emit scriptsChanged();
    scheduleRecompile();
    return true;
}

//  Script position is pure canvas state and has no command in Commands.h, so it
//  is applied directly and is intentionally not undoable.
//  脚本位置纯粹是画布状态，Commands.h 中没有对应命令，因此直接应用且刻意不可撤销。
bool BlocksController::setScriptPosition(const QString &scriptId, double x, double y)
{
    BlockScript *target = script(scriptId);
    if (!target)
        return false;
    if (qFuzzyCompare(target->uiX + 1.0, x + 1.0) && qFuzzyCompare(target->uiY + 1.0, y + 1.0))
        return true;

    target->uiX = x;
    target->uiY = y;
    if (ProjectDocument *document = m_app->document())
        document->touch();
    emit scriptsChanged();
    return true;
}

// ---------------------------------------------------------------------------
//  Block CRUD
//  积木增删改
// ---------------------------------------------------------------------------
QString BlocksController::insertBlock(const QString &type, const QString &scriptId,
                                      const QString &parentBlockId, const QString &socketName,
                                      bool isStatement, int index)
{
    ProjectDocument *document = m_app ? m_app->document() : nullptr;
    CommandStack *commands = m_app ? m_app->commands() : nullptr;
    if (!document || !commands) {
        if (m_app)
            m_app->reportError(noProjectMessage());
        return QString();
    }

    const QString wantedType = type.trimmed();
    const BlockDef *def = blocks().find(wantedType);
    if (!def) {
        m_app->reportError(bilingual(QStringLiteral("Unknown block type \"%1\".").arg(wantedType),
                                     QStringLiteral("未知的积木类型 \"%1\"。").arg(wantedType)));
        return QString();
    }

    // --- resolve the target script -----------------------------------------
    BlockScript *target = nullptr;
    const QString wantedScript = scriptId.trimmed();
    if (!wantedScript.isEmpty()) {
        target = document->scriptById(wantedScript);
        if (!target) {
            m_app->reportError(bilingual(
                QStringLiteral("The target script no longer exists."),
                QStringLiteral("目标脚本已不存在。")));
            return QString();
        }
    } else {
        target = script(m_selectedScriptId);
        if (!target && !document->program().isEmpty())
            target = document->program().first();
    }
    if (!target) {
        //  Nothing to insert into yet: create the default script first, exactly
        //  as the QML layer does before dropping a block.
        //  尚无插入目标：先创建默认脚本，与 QML 层在放置积木前的做法一致。
        const QString created = addScript(QStringLiteral("events.start"));
        target = document->scriptById(created);
        if (!target) {
            m_app->reportError(bilingual(
                QStringLiteral("No script could be created for the new block."),
                QStringLiteral("无法为新积木创建脚本。")));
            return QString();
        }
    }

    // --- budget ------------------------------------------------------------
    if (countBlocks(*target) >= kMaxScriptBlocks) {
        m_app->reportError(
            bilingual(QStringLiteral("This script already holds %1 blocks; add no more.")
                          .arg(kMaxScriptBlocks),
                      QStringLiteral("本脚本的积木已达 %1 个，无法继续添加。")
                          .arg(kMaxScriptBlocks)));
        return QString();
    }
    if (countBlocks(*document) >= kMaxProgramBlocks) {
        m_app->reportError(
            bilingual(QStringLiteral("This program already holds %1 blocks; add no more.")
                          .arg(kMaxProgramBlocks),
                      QStringLiteral("本程序的积木已达 %1 个，无法继续添加。")
                          .arg(kMaxProgramBlocks)));
        return QString();
    }

    // --- socket sanity -----------------------------------------------------
    const QString wantedParent = parentBlockId.trimmed();
    const QString wantedSocket = socketName.trimmed();
    if (!isStatement && wantedParent.isEmpty()) {
        //  A value block must live inside an input socket: appending it to the
        //  script body would create a tree the compiler cannot read.
        //  返回值积木必须位于输入槽中：把它追加到脚本主体会形成编译器无法读取的树。
        m_app->reportError(bilingual(
            QStringLiteral("A value block needs an input socket and cannot be added to the body "
                           "of a script."),
            QStringLiteral("返回值积木需要输入槽，不能添加到脚本主体中。")));
        return QString();
    }
    if (!wantedParent.isEmpty()) {
        const Block *parent = block(wantedParent);
        if (!parent) {
            m_app->reportError(bilingual(
                QStringLiteral("The target block no longer exists."),
                QStringLiteral("目标积木已不存在。")));
            return QString();
        }
        if (const BlockDef *parentDef = blocks().find(parent->type)) {
            bool socketExists = false;
            if (isStatement) {
                for (const BlockStatementDef &statement : parentDef->statements) {
                    if (statement.name == wantedSocket) {
                        socketExists = true;
                        break;
                    }
                }
            } else {
                for (const BlockInputDef &input : parentDef->inputs) {
                    if (input.name == wantedSocket) {
                        socketExists = true;
                        break;
                    }
                }
            }
            if (!socketExists) {
                m_app->reportError(
                    bilingual(QStringLiteral("\"%1\" has no socket named \"%2\".")
                                  .arg(parentDef->type, wantedSocket),
                              QStringLiteral("\"%1\" 没有名为 \"%2\" 的槽位。")
                                  .arg(parentDef->type, wantedSocket)));
                return QString();
            }
        }
    }

    std::unique_ptr<Block> created = blocks().createBlock(wantedType);
    if (!created) {
        m_app->reportError(bilingual(QStringLiteral("The block \"%1\" could not be created.")
                                         .arg(wantedType),
                                     QStringLiteral("无法创建积木 \"%1\"。").arg(wantedType)));
        return QString();
    }

    //  The id is generated here and preserved by AddBlockCommand, so the caller
    //  can select the new block immediately.
    //  id 在此生成并由 AddBlockCommand 保留，因此调用方可以立即选中新积木。
    const QString newId = created->id;

    commands->pushIfEffective(new AddBlockCommand(document, target->id, wantedParent,
                                                  wantedSocket, isStatement, index,
                                                  std::move(created)));

    m_selectedScriptId = target->id;
    m_selectedBlockId = newId;
    emit selectionChanged();
    onStructureChanged();
    return newId;
}

bool BlocksController::removeBlock(const QString &blockId)
{
    ProjectDocument *document = m_app ? m_app->document() : nullptr;
    CommandStack *commands = m_app ? m_app->commands() : nullptr;
    if (!document || !commands) {
        if (m_app)
            m_app->reportError(noProjectMessage());
        return false;
    }

    BlockScript *owner = ownerScript(document, blockId);
    if (!owner) {
        if (m_app) {
            m_app->reportError(bilingual(QStringLiteral("The block no longer exists."),
                                         QStringLiteral("该积木已不存在。")));
        }
        return false;
    }
    if (owner->hat && owner->hat->id == blockId) {
        if (m_app) {
            m_app->reportError(bilingual(
                QStringLiteral("The hat block cannot be removed on its own; delete the script."),
                QStringLiteral("帽子块不能单独删除；请删除整个脚本。")));
        }
        return false;
    }

    commands->pushIfEffective(new RemoveBlockCommand(document, owner->id, blockId));

    if (m_selectedBlockId == blockId) {
        m_selectedBlockId.clear();
        emit selectionChanged();
    }
    onStructureChanged();
    return true;
}

//  Commands.h declares no move/paste command for blocks, so the tree is
//  restructured directly. The edit is still marked through document->touch();
//  it is deliberately not undoable because the frozen header offers no command.
//  Commands.h 没有为积木声明移动 / 粘贴命令，因此直接重构树。修改仍通过
//  document->touch() 标记；由于冻结的头文件没有提供命令，它刻意不可撤销。
bool BlocksController::moveBlock(const QString &blockId, const QString &newParentBlockId,
                                 const QString &socketName, bool isStatement, int index)
{
    ProjectDocument *document = m_app ? m_app->document() : nullptr;
    if (!document) {
        if (m_app)
            m_app->reportError(noProjectMessage());
        return false;
    }

    Block *moving = block(blockId);
    BlockScript *owner = ownerScript(document, blockId);
    if (!moving || !owner) {
        if (m_app) {
            m_app->reportError(bilingual(QStringLiteral("The block no longer exists."),
                                         QStringLiteral("该积木已不存在。")));
        }
        return false;
    }
    if (owner->hat && owner->hat->id == blockId) {
        if (m_app) {
            m_app->reportError(bilingual(QStringLiteral("The hat block cannot be moved."),
                                         QStringLiteral("帽子块无法移动。")));
        }
        return false;
    }

    const QString wantedParent = newParentBlockId.trimmed();
    const QString wantedSocket = socketName.trimmed();
    Block *newParent = nullptr;
    if (!wantedParent.isEmpty()) {
        newParent = block(wantedParent);
        if (!newParent) {
            if (m_app) {
                m_app->reportError(bilingual(QStringLiteral("The target block no longer exists."),
                                             QStringLiteral("目标积木已不存在。")));
            }
            return false;
        }
        //  Refuse a cycle: a block may never be moved inside itself.
        //  拒绝成环：积木绝不能移动到自身内部。
        for (const Block *ancestor = newParent; ancestor; ancestor = ancestor->parent) {
            if (ancestor == moving) {
                if (m_app) {
                    m_app->reportError(bilingual(
                        QStringLiteral("A block cannot be moved inside itself."),
                        QStringLiteral("积木不能移动到自身内部。")));
                }
                return false;
            }
        }
        if (const BlockDef *def = blocks().find(newParent->type)) {
            bool socketExists = false;
            if (isStatement) {
                for (const BlockStatementDef &statement : def->statements) {
                    if (statement.name == wantedSocket) {
                        socketExists = true;
                        break;
                    }
                }
            } else {
                for (const BlockInputDef &input : def->inputs) {
                    if (input.name == wantedSocket) {
                        socketExists = true;
                        break;
                    }
                }
            }
            if (!socketExists) {
                if (m_app) {
                    m_app->reportError(
                        bilingual(QStringLiteral("\"%1\" has no socket named \"%2\".")
                                      .arg(def->type, wantedSocket),
                                  QStringLiteral("\"%1\" 没有名为 \"%2\" 的槽位。")
                                      .arg(def->type, wantedSocket)));
                }
                return false;
            }
        }
    }

    // --- detach from the current container ---------------------------------
    std::unique_ptr<Block> owned;
    if (!moving->parent) {
        owned = owner->takeBlock(moving);
    } else {
        Block *parent = moving->parent;
        if (moving->parentIsStatement)
            parent->statements[moving->parentInput].removeOne(moving);
        else
            parent->inputs[moving->parentInput].removeOne(moving);
        moving->parent = nullptr;
        moving->parentInput.clear();
        moving->parentIsStatement = false;
        owned.reset(moving);
    }
    if (!owned) {
        if (m_app) {
            m_app->reportError(bilingual(
                QStringLiteral("The block is not attached to this script any more."),
                QStringLiteral("该积木已不再挂接于本脚本。")));
        }
        return false;
    }

    // --- attach to the new container ---------------------------------------
    if (!newParent) {
        owner->addBlock(std::move(owned), index);
    } else if (isStatement) {
        Block *raw = owned.release();
        QVector<Block *> &target = newParent->statements[wantedSocket];
        if (index < 0 || index > target.size())
            index = target.size();
        target.insert(index, raw);
        raw->parent = newParent;
        raw->parentInput = wantedSocket;
        raw->parentIsStatement = true;
    } else {
        Block *raw = owned.release();
        QVector<Block *> &target = newParent->inputs[wantedSocket];
        if (index < 0 || index > target.size())
            index = target.size();
        target.insert(index, raw);
        raw->parent = newParent;
        raw->parentInput = wantedSocket;
        raw->parentIsStatement = false;
    }

    document->touch();
    onStructureChanged();
    return true;
}

bool BlocksController::setBlockField(const QString &blockId, const QString &fieldName,
                                     const QString &value)
{
    ProjectDocument *document = m_app ? m_app->document() : nullptr;
    CommandStack *commands = m_app ? m_app->commands() : nullptr;
    if (!document || !commands) {
        if (m_app)
            m_app->reportError(noProjectMessage());
        return false;
    }

    BlockScript *owner = ownerScript(document, blockId);
    if (!owner) {
        if (m_app) {
            m_app->reportError(bilingual(QStringLiteral("The block no longer exists."),
                                         QStringLiteral("该积木已不存在。")));
        }
        return false;
    }
    if (fieldName.trimmed().isEmpty())
        return false;

    Block *target = block(blockId);
    if (target && target->fieldValues.value(fieldName) == value)
        return true;

    commands->pushIfEffective(
        new SetBlockFieldCommand(document, owner->id, blockId, fieldName, value));

    onStructureChanged();
    return true;
}

//  Block position is stored in the document so the editor reopens exactly as it
//  was left, but it is presentation state only: Commands.h has no command for
//  it, so it is applied directly and is intentionally not undoable.
//  积木位置保存在文档中，使编辑器恢复上次离开时的样子，但它只是呈现状态：
//  Commands.h 没有对应命令，因此直接应用且刻意不可撤销。
bool BlocksController::setBlockPosition(const QString &blockId, double x, double y)
{
    Block *target = block(blockId);
    if (!target)
        return false;
    if (qFuzzyCompare(target->uiX + 1.0, x + 1.0)
        && qFuzzyCompare(target->uiY + 1.0, y + 1.0)) {
        return true;
    }

    target->uiX = x;
    target->uiY = y;
    if (ProjectDocument *document = m_app ? m_app->document() : nullptr)
        document->touch();
    emit scriptsChanged();
    return true;
}

// ---------------------------------------------------------------------------
//  Connectability
//  可连接性判断
// ---------------------------------------------------------------------------
bool BlocksController::canConnect(const QString &blockType, const QString &parentBlockId,
                                  const QString &socketName, bool isStatement) const
{
    const BlockDef *child = blocks().find(blockType.trimmed());
    if (!child)
        return false;

    if (isStatement) {
        //  Only stack-shaped blocks hang below another block.
        //  只有堆叠形状的积木可以挂在另一块下方。
        switch (child->shape) {
        case BlockShape::Stack:
        case BlockShape::CShape:
        case BlockShape::EShape:
        case BlockShape::Cap:
            break;
        case BlockShape::Hat:
            //  A hat starts a script, so it may only sit at the top of a body.
            //  帽子块用于启动脚本，因此只能位于主体的顶部。
            if (!parentBlockId.trimmed().isEmpty())
                return false;
            break;
        default:
            return false;
        }
    } else if (!child->returnsValue()) {
        return false;
    }

    const QString parentId = parentBlockId.trimmed();
    if (parentId.isEmpty())
        return true;   // top of the script body / 脚本主体顶部

    const Block *parent = block(parentId);
    if (!parent)
        return false;
    const BlockDef *parentDef = blocks().find(parent->type);
    if (!parentDef)
        return false;

    const QString socket = socketName.trimmed();
    if (isStatement) {
        for (const BlockStatementDef &statement : parentDef->statements) {
            if (statement.name == socket)
                return true;
        }
        return false;
    }

    for (const BlockInputDef &input : parentDef->inputs) {
        if (input.name != socket)
            continue;
        if (!input.acceptsBlocks)
            return false;
        return kindAccepts(input.kind, child->shape);
    }
    return false;
}

QVariantList BlocksController::compatibleBlocks(const QString &inputName,
                                                const QString &valueKind) const
{
    //  The registry declares no return kind per block, and the target socket is
    //  not required to resolve the set, so the socket name only documents the
    //  caller's intent.
    //  注册表未声明每个积木的返回类型，且该集合也不依赖目标槽位，因此槽位名仅
    //  用于说明调用方意图。
    Q_UNUSED(inputName)

    const ValueKind wanted = valueKindFromString(valueKind, ValueKind::Any);
    QVariantList list;
    for (const BlockDef &def : blocks().definitions()) {
        if (!def.returnsValue())
            continue;
        if (!kindAccepts(wanted, def.shape))
            continue;
        list.append(describeBlockDef(def));
    }
    return list;
}

// ---------------------------------------------------------------------------
//  Compilation
//  编译
// ---------------------------------------------------------------------------
void BlocksController::recompile()
{
    ProjectDocument *document = m_app ? m_app->document() : nullptr;
    m_errors.clear();
    m_warnings.clear();

    if (!document) {
        if (!m_generatedCode.isEmpty()) {
            m_generatedCode.clear();
            emit codeChanged();
        }
        return;
    }

    //  The editor compiles pretty, 2-space, strict code wrapped in an IIFE:
    //  exactly what the exported .js contains.
    //  编辑器生成美化、两空格缩进、严格模式并包裹在 IIFE 中的代码：与导出的 .js
    //  完全一致。
    CompileOptions options;
    options.pretty = true;
    options.indentWidth = 2;
    options.useStrict = true;
    options.wrapInIife = true;
    BlockCompiler compiler(options);

    //  Scripts are compiled one by one so every diagnostic can name its script.
    //  逐个脚本编译，使每条诊断都能指明所属脚本。
    for (const BlockScript *script : document->program()) {
        if (!script)
            continue;
        const CompileResult result = compiler.compileScript(*script);
        const QString prefix = scriptLabel(*script, isChinese(m_app));
        for (const QString &error : result.errors) {
            if (m_errors.size() >= kMaxDiagnostics)
                break;
            m_errors.append(QStringLiteral("%1: %2").arg(prefix, error));
        }
        for (const QString &warning : result.warnings) {
            if (m_warnings.size() >= kMaxDiagnostics)
                break;
            m_warnings.append(QStringLiteral("%1: %2").arg(prefix, warning));
        }
    }

    const CompileResult program = compiler.compileProgram(document->program());
    m_generatedCode = program.code;
    if (m_errors.isEmpty()) {
        for (const QString &error : program.errors) {
            if (m_errors.size() >= kMaxDiagnostics)
                break;
            m_errors.append(error);
        }
    }
    for (const QString &warning : program.warnings) {
        if (m_warnings.size() >= kMaxDiagnostics)
            break;
        if (!m_warnings.contains(warning))
            m_warnings.append(warning);
    }

    if (m_errors.size() >= kMaxDiagnostics) {
        m_errors.append(bilingual(QStringLiteral("Further errors were suppressed."),
                                  QStringLiteral("更多错误已被省略。")));
    }
    if (m_warnings.size() >= kMaxDiagnostics) {
        m_warnings.append(bilingual(QStringLiteral("Further warnings were suppressed."),
                                    QStringLiteral("更多警告已被省略。")));
    }

    emit codeChanged();
}

void BlocksController::scheduleRecompile()
{
    //  Every edit schedules a rebuild; coalescing them keeps a burst of drag
    //  operations from compiling the program dozens of times.
    //  每次编辑都会安排一次重建；合并它们可避免一串拖拽操作导致程序被编译数十次。
    if (property(kRecompilePendingProperty).toBool())
        return;
    setProperty(kRecompilePendingProperty, true);

    QTimer::singleShot(120, this, [this]() {
        setProperty(kRecompilePendingProperty, false);
        if (!m_app || !m_app->document()) {
            //  Safe with no document: the editor simply shows no code.
            //  无文档时同样安全：编辑器只是不显示任何代码。
            if (!m_generatedCode.isEmpty() || !m_errors.isEmpty() || !m_warnings.isEmpty()) {
                m_generatedCode.clear();
                m_errors.clear();
                m_warnings.clear();
                emit codeChanged();
            }
            return;
        }
        recompile();
    });
}

QString BlocksController::codeForScript(const QString &scriptId) const
{
    const BlockScript *target = script(scriptId);
    if (!target)
        return QString();

    CompileOptions options;
    options.pretty = true;
    options.indentWidth = 2;
    options.useStrict = true;
    options.wrapInIife = true;
    BlockCompiler compiler(options);
    return compiler.compileScript(*target).code;
}

QString BlocksController::exportProgramJavaScript() const
{
    ProjectDocument *document = m_app ? m_app->document() : nullptr;
    if (!document)
        return QString();

    CompileOptions options;
    options.pretty = true;
    options.indentWidth = 2;
    options.useStrict = true;
    options.wrapInIife = true;
    BlockCompiler compiler(options);
    return compiler.compileProgram(document->program()).code;
}

bool BlocksController::copyCodeToClipboard() const
{
    if (m_generatedCode.isEmpty())
        return false;
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return false;
    clipboard->setText(m_generatedCode);
    return true;
}

// ---------------------------------------------------------------------------
//  Validation and preview
//  校验与预览
// ---------------------------------------------------------------------------
QVariantList BlocksController::validateScript(const QString &scriptId) const
{
    QVariantList list;
    const BlockScript *target = script(scriptId);
    if (!target)
        return list;

    // --- structural diagnostics from the registry ---------------------------
    target->forEach([this, &list](const Block *current) {
        if (!current)
            return;
        for (const BlockRegistry::Diagnostic &diagnostic : blocks().validate(*current)) {
            QVariantMap entry;
            entry.insert(QStringLiteral("blockId"),
                         diagnostic.blockId.isEmpty() ? current->id : diagnostic.blockId);
            entry.insert(QStringLiteral("severity"),
                         diagnostic.fatal ? QStringLiteral("error") : QStringLiteral("warning"));
            entry.insert(QStringLiteral("messageEn"), diagnostic.messageEn);
            entry.insert(QStringLiteral("messageZh"),
                         diagnostic.messageZh.isEmpty() ? diagnostic.messageEn
                                                        : diagnostic.messageZh);
            entry.insert(QStringLiteral("fatal"), diagnostic.fatal);
            list.append(entry);
        }
    });

    // --- compiler diagnostics ----------------------------------------------
    CompileOptions options;
    options.pretty = true;
    options.indentWidth = 2;
    options.useStrict = true;
    options.wrapInIife = true;
    BlockCompiler compiler(options);
    const CompileResult result = compiler.compileScript(*target);
    for (const QString &error : result.errors) {
        QVariantMap entry;
        entry.insert(QStringLiteral("blockId"), QString());
        entry.insert(QStringLiteral("severity"), QStringLiteral("error"));
        entry.insert(QStringLiteral("messageEn"), error);
        entry.insert(QStringLiteral("messageZh"), error);
        entry.insert(QStringLiteral("fatal"), true);
        list.append(entry);
    }
    for (const QString &warning : result.warnings) {
        QVariantMap entry;
        entry.insert(QStringLiteral("blockId"), QString());
        entry.insert(QStringLiteral("severity"), QStringLiteral("warning"));
        entry.insert(QStringLiteral("messageEn"), warning);
        entry.insert(QStringLiteral("messageZh"), warning);
        entry.insert(QStringLiteral("fatal"), false);
        list.append(entry);
    }
    return list;
}

//  dryRun() is a preview aid, not a simulator: it walks the first blocks of a
//  script and reports what each one would touch. Nothing is executed.
//  dryRun() 是预览辅助，并非模拟器：它遍历脚本最前面的积木并报告每块会触及
//  什么。不会执行任何内容。
QVariantList BlocksController::dryRun(const QString &scriptId) const
{
    QVariantList list;
    const BlockScript *target = script(scriptId);
    if (!target)
        return list;

    const Page *page = nullptr;
    if (m_app) {
        //  The active page (falling back to the home page) is where the block's
        //  element references are resolved.
        //  在活动页面（回退到首页）上解析积木引用的元素。
        page = m_app->currentPage();
    }

    int index = 0;
    for (const Block *current : target->body) {
        if (!current)
            continue;
        if (index >= kMaxDryRunSteps)
            break;
        const BlockDef *def = blocks().find(current->type);
        const QStringList effect = effectFor(def, *current, page);

        QVariantMap entry;
        entry.insert(QStringLiteral("index"), index);
        entry.insert(QStringLiteral("blockType"), current->type);
        entry.insert(QStringLiteral("labelEn"), def ? labelEnOf(*def) : current->type);
        entry.insert(QStringLiteral("labelZh"), def ? labelZhOf(*def) : current->type);
        entry.insert(QStringLiteral("effectEn"), effect.value(0));
        entry.insert(QStringLiteral("effectZh"), effect.value(1, effect.value(0)));
        list.append(entry);
        ++index;
    }
    return list;
}

// ---------------------------------------------------------------------------
//  Change propagation
//  变更传播
// ---------------------------------------------------------------------------
void BlocksController::onStructureChanged()
{
    ProjectDocument *document = m_app ? m_app->document() : nullptr;

    //  Selections that no longer resolve are dropped, so the inspector never
    //  renders a block that has just been deleted.
    //  不再有效的选择会被清除，检查器因此绝不会渲染刚被删除的积木。
    if (!document) {
        if (!m_selectedScriptId.isEmpty() || !m_selectedBlockId.isEmpty()) {
            m_selectedScriptId.clear();
            m_selectedBlockId.clear();
            emit selectionChanged();
        }
    } else {
        const bool scriptLost = !m_selectedScriptId.isEmpty()
                             && !document->scriptById(m_selectedScriptId);
        const bool blockLost = !m_selectedBlockId.isEmpty() && !block(m_selectedBlockId);
        if (scriptLost)
            m_selectedScriptId.clear();
        if (blockLost)
            m_selectedBlockId.clear();
        if (scriptLost || blockLost)
            emit selectionChanged();
    }

    emit scriptsChanged();
    scheduleRecompile();
}

void BlocksController::setShowAdvanced(bool value)
{
    if (m_showAdvanced == value)
        return;
    m_showAdvanced = value;
    emit toolboxChanged();
}

} // namespace tcbw
