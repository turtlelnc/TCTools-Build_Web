// ============================================================================
//  BlockRegistry.cpp -- the block catalogue of TC-BuildWeb
//  BlockRegistry.cpp —— TC-BuildWeb 的积木目录
//
//  Every programming capability the toolbox offers lives here as data: one
//  BlockDef per block type. Adding a capability means adding one row below and
//  (optionally) one code-generation branch in BlockCompiler.cpp.
//  工具箱提供的每一种编程能力都以数据形式存放在此：每种积木一条 BlockDef。
//  新增能力只需在下方增加一行，并（可选）在 BlockCompiler.cpp 中增加一个分支。
// ============================================================================
#include "BlockRegistry.h"

#include "IdGenerator.h"

#include <QRegularExpression>

#include <algorithm>
#include <utility>

namespace tcbw {

namespace {

// ---------------------------------------------------------------------------
//  Static value-kind estimation for a block type.
//  积木类型的静态值种类估计。
//
//  The definition format stores no explicit return type, so the kind is derived
//  from the shape and the type prefix. `Any` means "compatible with
//  everything", which keeps the checker quiet where being clever would only
//  produce false alarms.
//  定义格式没有显式的返回类型，因此由形状与类型前缀推导。
//  `Any` 表示"与任何类型兼容"，避免类型检查器产生误报。
// ---------------------------------------------------------------------------
ValueKind valueKindOf(const QString &type, BlockShape shape)
{
    if (shape == BlockShape::Boolean)
        return ValueKind::Boolean;
    if (shape == BlockShape::Round)
        return ValueKind::Number;
    if (type == QLatin1String("text.length") || type == QLatin1String("data.length")
        || type == QLatin1String("variables.listLength"))
        return ValueKind::Number;
    if (type == QLatin1String("variables.listGet"))
        return ValueKind::Any;
    if (type.startsWith(QLatin1String("math.")))
        return ValueKind::Number;
    if (type.startsWith(QLatin1String("text.")))
        return ValueKind::String;
    if (type.startsWith(QLatin1String("logic.")))
        return ValueKind::Boolean;
    if (type.startsWith(QLatin1String("timing.")))
        return ValueKind::String;
    return ValueKind::Any;
}

//  True when a socket declared as `wanted` may hold a block producing
//  `produced`.
//  当声明为 `wanted` 的槽位可以放入产出 `produced` 的积木时返回 true。
bool kindCompatible(ValueKind wanted, ValueKind produced)
{
    if (wanted == ValueKind::Any || produced == ValueKind::Any)
        return true;
    if (wanted == produced)
        return true;
    //  Anything can be stringified, and colours are typed as text.
    //  任何值都可以转成字符串，颜色也以文本形式输入。
    if (produced == ValueKind::String || wanted == ValueKind::String)
        return true;
    if (wanted == ValueKind::Colour)
        return true;
    return false;
}

QString kindLabel(ValueKind kind, bool chinese)
{
    switch (kind) {
    case ValueKind::Any:      return chinese ? QStringLiteral("任意") : QStringLiteral("any");
    case ValueKind::Number:   return chinese ? QStringLiteral("数值") : QStringLiteral("number");
    case ValueKind::String:   return chinese ? QStringLiteral("文本") : QStringLiteral("text");
    case ValueKind::Boolean:  return chinese ? QStringLiteral("布尔") : QStringLiteral("boolean");
    case ValueKind::Colour:   return chinese ? QStringLiteral("颜色") : QStringLiteral("colour");
    case ValueKind::Element:  return chinese ? QStringLiteral("元素") : QStringLiteral("element");
    case ValueKind::List:     return chinese ? QStringLiteral("列表") : QStringLiteral("list");
    case ValueKind::Object:   return chinese ? QStringLiteral("对象") : QStringLiteral("object");
    case ValueKind::Function: return chinese ? QStringLiteral("函数") : QStringLiteral("function");
    }
    return chinese ? QStringLiteral("任意") : QStringLiteral("any");
}

//  Turns "set the text of %1 to %2" into "set the text of … to …" for the
//  auto-generated tooltip.
//  把 "set the text of %1 to %2" 变成 "set the text of … to …"，用于自动提示。
QString ellipsisPlaceholders(const QString &label)
{
    static const QRegularExpression re(QStringLiteral("%[0-9]+"));
    QString out = label;
    out.replace(re, QStringLiteral("…"));
    return out;
}

} // namespace

// ===========================================================================
//  Construction
//  构造
// ===========================================================================
BlockRegistry::BlockRegistry()
{
    registerCategories();
    registerBlocks();
}

const BlockRegistry &BlockRegistry::instance()
{
    //  Function-local static: initialised exactly once, thread-safe in C++11+.
    //  函数内静态变量：只初始化一次，C++11 起线程安全。
    static const BlockRegistry registry;
    return registry;
}

// ===========================================================================
//  Definition builders
//  定义构造辅助
// ===========================================================================
BlockFieldDef BlockRegistry::textField(const QString &name, const QString &labelEn,
                                       const QString &labelZh, const QString &def)
{
    BlockFieldDef field;
    field.name = name;
    field.labelEn = labelEn;
    field.labelZh = labelZh;
    field.kind = ValueKind::String;
    field.defaultValue = def;
    return field;
}

BlockFieldDef BlockRegistry::numberField(const QString &name, const QString &labelEn,
                                         const QString &labelZh, double def,
                                         double min, double max)
{
    BlockFieldDef field;
    field.name = name;
    field.labelEn = labelEn;
    field.labelZh = labelZh;
    field.kind = ValueKind::Number;
    field.defaultValue = QString::number(def, 'g', 12);
    field.minValue = min;
    field.maxValue = max;
    field.hasRange = !(min == 0.0 && max == 0.0);
    return field;
}

BlockFieldDef BlockRegistry::dropdownField(const QString &name, const QString &labelEn,
                                           const QString &labelZh, const QStringList &options,
                                           const QStringList &optionsZh, const QString &def)
{
    BlockFieldDef field;
    field.name = name;
    field.labelEn = labelEn;
    field.labelZh = labelZh;
    field.kind = ValueKind::String;
    field.options = options;
    field.optionLabelsZh = optionsZh;
    field.defaultValue = !def.isEmpty() ? def
                                        : (options.isEmpty() ? QString() : options.first());
    return field;
}

BlockFieldDef BlockRegistry::colourField(const QString &name, const QString &labelEn,
                                         const QString &labelZh, const QString &def)
{
    BlockFieldDef field;
    field.name = name;
    field.labelEn = labelEn;
    field.labelZh = labelZh;
    field.kind = ValueKind::Colour;
    field.defaultValue = def;
    field.prefix = QStringLiteral("#");
    return field;
}

BlockInputDef BlockRegistry::input(const QString &name, const QString &labelEn,
                                   const QString &labelZh, ValueKind kind,
                                   const QString &shadowType, const QString &shadowValue)
{
    BlockInputDef socket;
    socket.name = name;
    socket.labelEn = labelEn;
    socket.labelZh = labelZh;
    socket.kind = kind;
    socket.shadowType = shadowType;
    socket.shadowValue = shadowValue;
    socket.acceptsBlocks = true;
    return socket;
}

BlockStatementDef BlockRegistry::branch(const QString &name, const QString &labelEn,
                                        const QString &labelZh)
{
    BlockStatementDef slot;
    slot.name = name;
    slot.labelEn = labelEn;
    slot.labelZh = labelZh;
    slot.minBlocks = 0;
    slot.maxBlocks = -1;
    return slot;
}

void BlockRegistry::add(BlockDef def)
{
    if (def.type.trimmed().isEmpty())
        return;

    //  First definition wins: a duplicate would silently shadow a real block.
    //  先注册者优先：重复定义会静默覆盖真正的积木。
    if (m_byType.contains(def.type))
        return;

    if (def.category.isEmpty())
        def.category = def.type.section(QLatin1Char('.'), 0, 0);

    if (def.colour.isEmpty())
        def.colour = colourForCategory(def.category);

    const BlockCategory *cat = category(def.category);

    if (def.tooltipEn.isEmpty()) {
        def.tooltipEn = ellipsisPlaceholders(def.labelEn);
        if (cat)
            def.tooltipEn += QStringLiteral(" — ") + cat->labelEn
                           + QStringLiteral(". ") + cat->descriptionEn;
    }
    if (def.tooltipZh.isEmpty()) {
        def.tooltipZh = ellipsisPlaceholders(def.labelZh);
        if (cat)
            def.tooltipZh += QStringLiteral(" —— ") + cat->labelZh
                           + QStringLiteral("。") + cat->descriptionZh;
    }

    //  The AI and MCP surfaces depend on runtime configuration and are flagged
    //  as experimental so the editor can warn before exporting.
    //  AI 与 MCP 依赖运行时配置，标记为实验性，便于编辑器在导出前提示。
    if (def.category == QLatin1String("ai") || def.category == QLatin1String("mcp"))
        def.experimental = true;

    m_byType.insert(def.type, m_defs.size());
    m_defs.append(def);
}

// ===========================================================================
//  Categories
//  分类
// ===========================================================================
void BlockRegistry::registerCategories()
{
    const auto addCategory = [this](const char *key, const char *labelEn, const char *labelZh,
                                    const char *colour, const char *icon,
                                    const char *descEn, const char *descZh,
                                    bool advanced, int order) {
        BlockCategory cat;
        cat.key = QString::fromLatin1(key);
        cat.labelEn = QString::fromLatin1(labelEn);
        cat.labelZh = QString::fromUtf8(labelZh);
        cat.colour = QString::fromLatin1(colour);
        cat.icon = QString::fromLatin1(icon);
        cat.descriptionEn = QString::fromLatin1(descEn);
        cat.descriptionZh = QString::fromUtf8(descZh);
        cat.advanced = advanced;
        cat.order = order;
        m_categoryByKey.insert(cat.key, m_categories.size());
        m_categories.append(cat);
    };

    //  Apple system palette colours, one per category.
    //  使用 Apple 系统调色板，每个分类一种颜色。
    addCategory("events", "Events", "事件", "#FF9500", "bolt",
                "Start a script when something happens on the page.",
                "当页面上发生某件事时启动脚本。", false, 0);
    addCategory("ui", "Interface", "界面", "#0A84FF", "cursorarrow.rays",
                "Show, hide and restyle elements on the page.",
                "显示、隐藏与修改页面元素样式。", false, 1);
    addCategory("text", "Text", "文本", "#5856D6", "textformat",
                "Build, split and inspect strings.",
                "拼接、拆分与检查字符串。", false, 2);
    addCategory("control", "Control", "控制", "#FFB340", "arrow.triangle.branch",
                "Conditions, loops and jumps.",
                "条件、循环与跳转。", false, 3);
    addCategory("logic", "Logic", "逻辑", "#32ADE6", "switch.2",
                "Compare values and combine conditions.",
                "比较数值并组合条件。", false, 4);
    addCategory("math", "Math", "数学", "#34C759", "function",
                "Numbers, arithmetic and trigonometry.",
                "数值、算术与三角函数。", false, 5);
    addCategory("variables", "Variables", "变量", "#FF375F", "cube",
                "Named values and lists shared by every script.",
                "由所有脚本共享的命名值与列表。", false, 6);
    addCategory("storage", "Storage", "存储", "#00C7BE", "externaldrive",
                "Local storage, session storage and cookies.",
                "本地存储、会话存储与 Cookie。", false, 7);
    addCategory("http", "HTTP", "网络请求", "#5AC8FA", "network",
                "Talk to web services with fetch.",
                "使用 fetch 与网络服务通信。", false, 8);
    addCategory("backend", "Backend", "后端", "#AF52DE", "server.rack",
                "Call the pre-linked backend, authenticate and subscribe.",
                "调用预链接后端、登录鉴权与订阅。", true, 9);
    addCategory("data", "Data", "数据", "#FFD60A", "tablecells",
                "Parse JSON and transform lists.",
                "解析 JSON 并转换列表。", true, 10);
    addCategory("ai", "AI", "人工智能", "#FF2D55", "sparkles",
                "Ask a configured model to write, translate or classify.",
                "调用已配置的模型进行写作、翻译或分类。", true, 11);
    addCategory("mcp", "MCP Tools", "MCP 工具", "#6E6E73", "puzzlepiece.extension",
                "Call tools and read resources from MCP servers.",
                "调用 MCP 服务器的工具并读取资源。", true, 12);
    addCategory("timing", "Timing", "定时", "#A2845E", "clock",
                "Delay, repeat over time and format dates.",
                "延时、定时重复与日期格式化。", false, 13);
    addCategory("console", "Console", "控制台", "#8E8E93", "terminal",
                "Write diagnostics to the browser console.",
                "向浏览器控制台输出诊断信息。", false, 14);
}

// ===========================================================================
//  Queries
//  查询
// ===========================================================================
const BlockCategory *BlockRegistry::category(const QString &key) const
{
    auto it = m_categoryByKey.constFind(key);
    if (it == m_categoryByKey.constEnd())
        return nullptr;
    const int index = it.value();
    if (index < 0 || index >= m_categories.size())
        return nullptr;
    return &m_categories.at(index);
}

QVector<const BlockCategory *> BlockRegistry::visibleCategories(bool showAdvanced) const
{
    QVector<const BlockCategory *> out;
    for (const BlockCategory &cat : m_categories) {
        if (cat.advanced && !showAdvanced)
            continue;
        out.append(&cat);
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const BlockCategory *a, const BlockCategory *b) {
                         return a->order < b->order;
                     });
    return out;
}

const BlockDef *BlockRegistry::find(const QString &type) const
{
    auto it = m_byType.constFind(type);
    if (it == m_byType.constEnd())
        return nullptr;
    const int index = it.value();
    if (index < 0 || index >= m_defs.size())
        return nullptr;
    return &m_defs.at(index);
}

QVector<const BlockDef *> BlockRegistry::byCategory(const QString &categoryKey) const
{
    QVector<const BlockDef *> out;
    for (const BlockDef &def : m_defs) {
        if (def.category == categoryKey)
            out.append(&def);
    }
    return out;
}

QVector<const BlockDef *> BlockRegistry::search(const QString &needle) const
{
    QVector<const BlockDef *> out;
    const QString term = needle.trimmed().toLower();
    if (term.isEmpty())
        return out;

    for (const BlockDef &def : m_defs) {
        bool hit = def.type.toLower().contains(term)
                || def.labelEn.toLower().contains(term)
                || def.labelZh.contains(needle.trimmed())
                || def.category.toLower().contains(term);
        if (!hit) {
            for (const QString &keyword : def.keywords) {
                if (keyword.toLower().contains(term)) {
                    hit = true;
                    break;
                }
            }
        }
        if (hit)
            out.append(&def);
    }
    return out;
}

QVector<const BlockDef *> BlockRegistry::hatBlocks() const
{
    QVector<const BlockDef *> out;
    for (const BlockDef &def : m_defs) {
        if (def.shape == BlockShape::Hat)
            out.append(&def);
    }
    return out;
}

QVector<const BlockDef *> BlockRegistry::shadowBlocks() const
{
    //  The blocks createShadow() can return, plus every numeric slot block.
    //  createShadow() 可能返回的积木，以及所有数值槽积木。
    QVector<const BlockDef *> out;
    for (const BlockDef &def : m_defs) {
        const bool isShadow = def.shape == BlockShape::Round
                           || def.type == QLatin1String("text.value")
                           || def.type == QLatin1String("logic.true")
                           || def.type == QLatin1String("logic.false");
        if (isShadow)
            out.append(&def);
    }
    return out;
}

QString BlockRegistry::colourForCategory(const QString &key) const
{
    const BlockCategory *cat = category(key);
    if (cat && !cat->colour.isEmpty())
        return cat->colour;
    //  Neutral grey / 中性灰
    return QStringLiteral("#8E8E93");
}

// ===========================================================================
//  Instantiation
//  实例化
// ===========================================================================
std::unique_ptr<Block> BlockRegistry::createBlock(const QString &type) const
{
    const BlockDef *def = find(type);
    //  Returns nullptr for an unknown type so callers can surface the problem
    //  (validate() reports it as fatal) instead of rendering a broken block.
    //  未知类型返回 nullptr，让调用方显式处理（validate() 会报为致命错误），
    //  而不是渲染一个坏掉的积木。
    if (!def)
        return nullptr;

    auto block = std::make_unique<Block>(def->type);
    for (const BlockFieldDef &field : def->fields) {
        if (field.name.isEmpty())
            continue;
        block->fieldValues.insert(field.name, field.defaultValue);
    }
    //  Inputs stay empty on purpose: the shadow is created on demand by
    //  createShadow(), and the compiler falls back to `shadowValue` while the
    //  socket has no connected block.
    //  故意不填充输入槽：影子块由 createShadow() 按需创建；
    //  槽位没有连接积木时，编译器回退到 `shadowValue`。
    return block;
}

std::unique_ptr<Block> BlockRegistry::createShadow(const BlockInputDef &input) const
{
    const QString wanted = input.shadowType;
    const BlockDef *shadowDef = wanted.isEmpty() ? nullptr : find(wanted);

    //  A declared shadow type is used verbatim when it can carry the value.
    //  当声明的影子类型能够承载该值时，直接使用它。
    const bool usable = shadowDef != nullptr
                     && (input.shadowValue.isEmpty() || shadowDef->fields.isEmpty()
                         || [shadowDef, &input] {
                                for (const BlockFieldDef &f : shadowDef->fields) {
                                    if (f.name == QLatin1String("value")
                                        || f.name == input.name)
                                        return true;
                                }
                                return false;
                            }());

    if (usable) {
        std::unique_ptr<Block> block = createBlock(shadowDef->type);
        if (block && !input.shadowValue.isEmpty()) {
            if (block->fieldValues.contains(QStringLiteral("value")))
                block->setFieldValue(QStringLiteral("value"), input.shadowValue);
            else if (block->fieldValues.contains(input.name))
                block->setFieldValue(input.name, input.shadowValue);
        }
        if (block)
            return block;
    }

    //  Otherwise synthesise the smallest reporter that fits the value kind.
    //  否则合成与值种类匹配的最小返回值积木。
    QString fallbackType;
    switch (input.kind) {
    case ValueKind::Number:
        fallbackType = QStringLiteral("math.number");
        break;
    case ValueKind::Boolean:
        fallbackType = QStringLiteral("logic.true");
        break;
    case ValueKind::Colour:
    case ValueKind::Element:
    case ValueKind::List:
    case ValueKind::Object:
    case ValueKind::Function:
    case ValueKind::String:
    case ValueKind::Any:
    default:
        fallbackType = QStringLiteral("text.value");
        break;
    }

    std::unique_ptr<Block> block = createBlock(fallbackType);
    if (!block)
        block = std::make_unique<Block>(fallbackType);

    if (!input.shadowValue.isEmpty()) {
        if (block->fieldValues.contains(QStringLiteral("value"))
            || block->fieldValues.isEmpty())
            block->setFieldValue(QStringLiteral("value"), input.shadowValue);
    }
    return block;
}

// ===========================================================================
//  Validation
//  校验
// ===========================================================================
QVector<BlockRegistry::Diagnostic> BlockRegistry::validate(const Block &block) const
{
    QVector<Diagnostic> out;
    QSet<const Block *> visited;

    std::function<void(const Block *)> check;
    //  Every descendant is checked as well; the walk is cycle safe.
    //  所有后代同样会被检查；遍历过程可防环。
    const std::function<void(const Block *)> descend = [&](const Block *node) {
        if (!node)
            return;
        for (auto it = node->inputs.constBegin(); it != node->inputs.constEnd(); ++it) {
            for (const Block *child : it.value())
                check(child);
        }
        for (auto it = node->statements.constBegin(); it != node->statements.constEnd(); ++it) {
            for (const Block *child : it.value())
                check(child);
        }
    };

    check = [&](const Block *node) {
        if (!node || visited.contains(node))
            return;
        visited.insert(node);

        const QString blockId = node->id;
        const BlockDef *def = find(node->type);

        if (!def) {
            out.append({ blockId,
                         QStringLiteral("Unknown block type '%1'.").arg(node->type),
                         QStringLiteral("未知的积木类型“%1”。").arg(node->type),
                         true });
            descend(node);
            return;
        }

        // --- nested statement slots ---------------------------------------
        // --- 嵌套语句槽 ---------------------------------------------------
        for (const BlockStatementDef &slot : def->statements) {
            int count = 0;
            for (const Block *child : node->statementBlocks(slot.name)) {
                if (child)
                    ++count;
            }
            if (count < slot.minBlocks) {
                out.append({ blockId,
                             QStringLiteral("Block '%1' needs at least %2 block(s) in '%3'.")
                                 .arg(def->type).arg(slot.minBlocks).arg(slot.name),
                             QStringLiteral("积木“%1”的“%3”槽至少需要 %2 个积木。")
                                 .arg(def->type).arg(slot.minBlocks).arg(slot.name),
                             false });
            }
        }

        //  A value block smuggled into a statement chain is structural damage.
        //  语句链中混入返回值积木属于结构性损坏。
        for (auto it = node->statements.constBegin(); it != node->statements.constEnd(); ++it) {
            for (const Block *child : it.value()) {
                if (!child)
                    continue;
                const BlockDef *childDef = find(child->type);
                if (childDef && childDef->returnsValue()) {
                    out.append({ child->id,
                                 QStringLiteral("Value block '%1' cannot be used as a statement.")
                                     .arg(child->type),
                                 QStringLiteral("返回值积木“%1”不能作为语句使用。")
                                     .arg(child->type),
                                 true });
                }
            }
        }

        // --- input sockets ------------------------------------------------
        // --- 输入槽 -------------------------------------------------------
        for (const BlockInputDef &socket : def->inputs) {
            const QVector<Block *> &wired = node->inputBlocks(socket.name);

            if (!node->hasInput(socket.name)) {
                if (socket.shadowType.isEmpty() && socket.shadowValue.isEmpty()
                    && socket.kind != ValueKind::Any) {
                    out.append({ blockId,
                                 QStringLiteral("Input '%1' of '%2' needs a value.")
                                     .arg(socket.name, def->type),
                                 QStringLiteral("积木“%2”的输入槽“%1”需要一个值。")
                                     .arg(socket.name, def->type),
                                 false });
                }
                continue;
            }

            for (const Block *child : wired) {
                if (!child)
                    continue;
                const BlockDef *childDef = find(child->type);
                if (!childDef) {
                    out.append({ child->id,
                                 QStringLiteral("Unknown block type '%1'.").arg(child->type),
                                 QStringLiteral("未知的积木类型“%1”。").arg(child->type),
                                 true });
                    continue;
                }
                if (!childDef->returnsValue()) {
                    out.append({ child->id,
                                 QStringLiteral("Block '%1' cannot be placed in an input socket.")
                                     .arg(child->type),
                                 QStringLiteral("积木“%1”不能放入输入槽。").arg(child->type),
                                 true });
                    continue;
                }
                const ValueKind produced = valueKindOf(childDef->type, childDef->shape);
                if (!kindCompatible(socket.kind, produced)) {
                    out.append({ child->id,
                                 QStringLiteral("Input '%1' of '%2' expects %3 but '%4' produces %5.")
                                     .arg(socket.name, def->type,
                                          kindLabel(socket.kind, false), childDef->type,
                                          kindLabel(produced, false)),
                                 QStringLiteral("积木“%2”的输入槽“%1”需要%3，而“%4”产出%5。")
                                     .arg(socket.name, def->type,
                                          kindLabel(socket.kind, true), childDef->type,
                                          kindLabel(produced, true)),
                                 false });
                }
            }
        }

        descend(node);
    };

    check(&block);
    return out;
}

// ===========================================================================
//  The catalogue
//  积木目录
// ===========================================================================
void BlockRegistry::registerBlocks()
{
    using BS = BlockShape;
    using VK = ValueKind;

    const auto TF = [](const QString &name, const QString &labelEn, const QString &labelZh,
                       const QString &def = QString()) {
        return textField(name, labelEn, labelZh, def);
    };
    const auto NF = [](const QString &name, const QString &labelEn, const QString &labelZh,
                       double def, double min = 0.0, double max = 0.0) {
        return numberField(name, labelEn, labelZh, def, min, max);
    };
    const auto DD = [](const QString &name, const QString &labelEn, const QString &labelZh,
                       const QStringList &options, const QStringList &optionsZh = {},
                       const QString &def = QString()) {
        return dropdownField(name, labelEn, labelZh, options, optionsZh, def);
    };
    const auto CF = [](const QString &name, const QString &labelEn, const QString &labelZh,
                       const QString &def) {
        return colourField(name, labelEn, labelZh, def);
    };
    const auto IN = [](const QString &name, const QString &labelEn, const QString &labelZh,
                       ValueKind kind, const QString &shadowType = QString(),
                       const QString &shadowValue = QString()) {
        return input(name, labelEn, labelZh, kind, shadowType, shadowValue);
    };
    const auto BR = [](const QString &name, const QString &labelEn, const QString &labelZh) {
        return branch(name, labelEn, labelZh);
    };
    const auto REQ = [](const QString &name, const QString &labelEn, const QString &labelZh) {
        BlockStatementDef slot = branch(name, labelEn, labelZh);
        slot.minBlocks = 1;
        return slot;
    };

    //  The compact row builder. `%1`, `%2`, ... in the labels are numbered in
    //  declaration order: fields first, then inputs, then statement branches.
    //  紧凑的行构造器。标签中的 `%1`、`%2` 等按声明顺序编号：
    //  先字段，再输入槽，最后语句分支。
    const auto def = [](const char *type, BlockShape shape, const QString &labelEn,
                        const QString &labelZh, const QStringList &keywords,
                        QVector<BlockFieldDef> fields = {},
                        QVector<BlockInputDef> sockets = {},
                        QVector<BlockStatementDef> branches = {},
                        bool terminal = false) -> BlockDef {
        BlockDef d;
        d.type = QString::fromLatin1(type);
        d.category = d.type.section(QLatin1Char('.'), 0, 0);
        d.shape = shape;
        d.labelEn = labelEn;
        d.labelZh = labelZh;
        d.keywords = keywords;
        d.fields = std::move(fields);
        d.inputs = std::move(sockets);
        d.statements = std::move(branches);
        d.terminal = terminal;
        return d;
    };

    // -----------------------------------------------------------------------
    //  Events -- one hat per supported page event.
    //  事件 —— 每个受支持的页面事件对应一个帽子块。
    // -----------------------------------------------------------------------
    add(def("events.start", BS::Hat,
            "when the page starts", "当页面启动时",
            { "start", "load", "init", "ready", "page", "domcontentloaded" }));

    add(def("events.click", BS::Hat,
            "when %1 is clicked", "当 %1 被点击时",
            { "click", "tap", "press", "mouse", "button" },
            { TF("selector", "target", "目标", "#app") }));

    add(def("events.doubleClick", BS::Hat,
            "when %1 is double clicked", "当 %1 被双击时",
            { "double", "click", "dblclick", "mouse" },
            { TF("selector", "target", "目标", "#app") }));

    add(def("events.submit", BS::Hat,
            "when %1 is submitted", "当 %1 被提交时",
            { "submit", "form", "send", "enter" },
            { TF("selector", "target", "目标", "form") }));

    add(def("events.keyPress", BS::Hat,
            "when key %1 is pressed in %2", "当在 %2 中按下按键 %1 时",
            { "key", "press", "keyboard", "shortcut", "hotkey" },
            { TF("key", "key", "按键", "Enter"),
              TF("selector", "target", "目标", "document") }));

    add(def("events.scroll", BS::Hat,
            "when %1 is scrolled", "当 %1 被滚动时",
            { "scroll", "wheel", "page", "position" },
            { TF("selector", "target", "目标", "window") }));

    add(def("events.resize", BS::Hat,
            "when the window is resized", "当窗口尺寸改变时",
            { "resize", "window", "responsive", "size" }));

    add(def("events.timer", BS::Hat,
            "every %1 milliseconds", "每隔 %1 毫秒",
            { "timer", "interval", "repeat", "every", "tick" },
            { NF("interval", "interval", "间隔", 1000.0, 16.0, 3600000.0) }));

    add(def("events.custom", BS::Hat,
            "when %1 happens", "当自定义事件 %1 发生时",
            { "custom", "event", "emit", "dispatch", "broadcast" },
            { TF("eventName", "event name", "事件名", "myEvent") }));

    // -----------------------------------------------------------------------
    //  Interface -- show, hide and restyle elements.
    //  界面 —— 显示、隐藏与修改元素。
    // -----------------------------------------------------------------------
    add(def("ui.show", BS::Stack,
            "show %1", "显示 %1",
            { "show", "visible", "display", "reveal" },
            { TF("selector", "target", "目标", "#app") }));

    add(def("ui.hide", BS::Stack,
            "hide %1", "隐藏 %1",
            { "hide", "visible", "display", "conceal" },
            { TF("selector", "target", "目标", "#app") }));

    add(def("ui.toggle", BS::Stack,
            "toggle %1", "切换 %1 的显示",
            { "toggle", "switch", "visible", "display" },
            { TF("selector", "target", "目标", "#app") }));

    add(def("ui.setText", BS::Stack,
            "set the text of %1 to %2", "把 %1 的文本设为 %2",
            { "text", "set", "content", "label", "innerText" },
            { TF("selector", "target", "目标", "#app") },
            { IN("value", "text", "文本", VK::String, "text.value", "Hello world") }));

    add(def("ui.setStyle", BS::Stack,
            "set style %1 of %2 to %3", "把 %2 的样式 %1 设为 %3",
            { "style", "css", "property", "design" },
            { TF("property", "property", "样式属性", "color"),
              TF("selector", "target", "目标", "#app") },
            { IN("value", "value", "值", VK::String, "text.value", "red") }));

    add(def("ui.setBackground", BS::Stack,
            "set the background colour of %1 to %2", "把 %1 的背景色设为 %2",
            { "background", "colour", "color", "paint", "fill" },
            { TF("selector", "target", "目标", "#app"),
              CF("colour", "colour", "颜色", "#0A84FF") }));

    add(def("ui.addClass", BS::Stack,
            "add class %1 to %2", "为 %2 添加类 %1",
            { "class", "add", "css", "style" },
            { TF("className", "class", "类名", "active"),
              TF("selector", "target", "目标", "#app") }));

    add(def("ui.removeClass", BS::Stack,
            "remove class %1 from %2", "从 %2 移除类 %1",
            { "class", "remove", "css", "style" },
            { TF("className", "class", "类名", "active"),
              TF("selector", "target", "目标", "#app") }));

    add(def("ui.setAttribute", BS::Stack,
            "set attribute %1 of %2 to %3", "把 %2 的属性 %1 设为 %3",
            { "attribute", "attr", "property", "html" },
            { TF("name", "attribute", "属性", "data-state"),
              TF("selector", "target", "目标", "#app") },
            { IN("value", "value", "值", VK::String, "text.value", "on") }));

    add(def("ui.scrollTo", BS::Stack,
            "scroll %1 to %2", "把 %1 滚动到 %2",
            { "scroll", "jump", "anchor", "position" },
            { TF("selector", "target", "目标", "#app") },
            { IN("position", "position", "位置", VK::String, "text.value", "top") }));

    add(def("ui.focus", BS::Stack,
            "focus %1", "让 %1 获得焦点",
            { "focus", "input", "caret", "select" },
            { TF("selector", "target", "目标", "input") }));

    add(def("ui.animate", BS::Stack,
            "animate %1 with %2 for %3 ms", "用 %2 动画 %1，持续 %3 毫秒",
            { "animate", "animation", "transition", "motion", "effect" },
            { TF("selector", "target", "目标", "#app"),
              DD("animation", "animation", "动画",
                 { "fade", "slide", "scale", "pulse", "spin" },
                 { "淡入", "滑动", "缩放", "脉冲", "旋转" }, "fade") },
            { IN("duration", "duration", "时长", VK::Number, "math.number", "400") }));

    add(def("ui.createElement", BS::Stack,
            "create a %1 element named %2", "创建 %1 元素并命名为 %2",
            { "create", "element", "new", "dom", "node" },
            { DD("tag", "tag", "标签",
                 { "div", "span", "p", "h1", "h2", "button", "img", "a", "input", "ul", "li", "section" },
                 { "容器", "行内", "段落", "一级标题", "二级标题", "按钮", "图片", "链接", "输入框", "列表", "列表项", "区块" },
                 "div"),
              TF("variable", "name", "名称", "newElement") }));

    add(def("ui.appendTo", BS::Stack,
            "append %1 into %2", "把 %1 追加到 %2",
            { "append", "insert", "add", "child", "dom" },
            { TF("source", "element", "元素", "newElement"),
              TF("selector", "target", "目标", "#app") }));

    add(def("ui.remove", BS::Stack,
            "remove %1", "移除 %1",
            { "remove", "delete", "detach", "dom" },
            { TF("selector", "target", "目标", ".temporary") }));

    add(def("ui.setImage", BS::Stack,
            "set the image of %1 to %2", "把 %1 的图片设为 %2",
            { "image", "img", "picture", "src", "photo" },
            { TF("selector", "target", "目标", "img") },
            { IN("source", "image URL", "图片地址", VK::String, "text.value", "image.png") }));

    add(def("ui.setHtml", BS::Stack,
            "set the HTML of %1 to %2", "把 %1 的 HTML 设为 %2",
            { "html", "innerHTML", "markup", "rich" },
            { TF("selector", "target", "目标", "#app") },
            { IN("html", "HTML", "HTML", VK::String, "text.value", "<b>Hello</b>") }));

    // -----------------------------------------------------------------------
    //  Text -- reporters that build and inspect strings.
    //  文本 —— 构造与检查字符串的返回值积木。
    // -----------------------------------------------------------------------
    add(def("text.value", BS::Reporter,
            "text %1", "文本 %1",
            { "text", "string", "literal", "value", "word" },
            { TF("value", "value", "值", "Hello") }));

    add(def("text.join", BS::Reporter,
            "join %1 %2", "连接 %1 %2",
            { "join", "concat", "append", "combine", "merge" },
            {},
            { IN("a", "first", "第一个", VK::String, "text.value", "Hello"),
              IN("b", "second", "第二个", VK::String, "text.value", "world") }));

    add(def("text.length", BS::Reporter,
            "length of %1", "%1 的长度",
            { "length", "size", "count", "characters" },
            {},
            { IN("text", "text", "文本", VK::String, "text.value", QString()) }));

    add(def("text.upper", BS::Reporter,
            "uppercase %1", "把 %1 转为大写",
            { "upper", "uppercase", "capital", "case" },
            {},
            { IN("text", "text", "文本", VK::String, "text.value", QString()) }));

    add(def("text.lower", BS::Reporter,
            "lowercase %1", "把 %1 转为小写",
            { "lower", "lowercase", "case", "small" },
            {},
            { IN("text", "text", "文本", VK::String, "text.value", QString()) }));

    add(def("text.trim", BS::Reporter,
            "trim %1", "去除 %1 首尾空格",
            { "trim", "strip", "whitespace", "clean" },
            {},
            { IN("text", "text", "文本", VK::String, "text.value", QString()) }));

    add(def("text.replace", BS::Reporter,
            "in %1 replace %2 with %3", "把 %1 中的 %2 替换为 %3",
            { "replace", "substitute", "swap", "find" },
            {},
            { IN("text", "text", "文本", VK::String, "text.value", QString()),
              IN("search", "search", "查找", VK::String, "text.value", "a"),
              IN("replacement", "replace with", "替换为", VK::String, "text.value", "b") }));

    add(def("text.split", BS::Reporter,
            "split %1 by %2", "用 %2 分割 %1",
            { "split", "separate", "tokenise", "explode", "array" },
            {},
            { IN("text", "text", "文本", VK::String, "text.value", "a,b,c"),
              IN("separator", "separator", "分隔符", VK::String, "text.value", ",") }));

    add(def("text.contains", BS::Boolean,
            "%1 contains %2", "%1 包含 %2",
            { "contains", "includes", "search", "find", "match" },
            {},
            { IN("text", "text", "文本", VK::String, "text.value", QString()),
              IN("needle", "search text", "查找文本", VK::String, "text.value", QString()) }));

    add(def("text.substring", BS::Reporter,
            "letters %1 to %2 of %3", "%3 的第 %1 到第 %2 个字符",
            { "substring", "slice", "part", "characters", "range" },
            {},
            { IN("from", "from", "起始", VK::Number, "math.number", "1"),
              IN("to", "to", "结束", VK::Number, "math.number", "5"),
              IN("text", "text", "文本", VK::String, "text.value", QString()) }));

    add(def("text.template", BS::Reporter,
            "template %1 with %2", "用 %2 填充模板 %1",
            { "template", "format", "interpolate", "mustache", "placeholder" },
            { TF("template", "template", "模板", "Hello {name}") },
            { IN("values", "values", "变量", VK::Object, QString(), "{}") }));

    // -----------------------------------------------------------------------
    //  Control -- conditions, loops and jumps.
    //  控制 —— 条件、循环与跳转。
    // -----------------------------------------------------------------------
    add(def("control.if", BS::EShape,
            "if %1 then %2", "如果 %1 那么 %2",
            { "if", "condition", "branch", "when", "then" },
            {},
            { IN("condition", "condition", "条件", VK::Boolean, "logic.true", "true") },
            { REQ("then", "then", "那么") }));

    add(def("control.ifElse", BS::EShape,
            "if %1 then %2 else %3", "如果 %1 那么 %2 否则 %3",
            { "if", "else", "condition", "branch", "otherwise" },
            {},
            { IN("condition", "condition", "条件", VK::Boolean, "logic.true", "true") },
            { REQ("then", "then", "那么"), REQ("else", "else", "否则") }));

    add(def("control.repeat", BS::CShape,
            "repeat %1 times %2", "重复 %1 次 %2",
            { "repeat", "loop", "times", "for", "iterate" },
            {},
            { IN("times", "times", "次数", VK::Number, "math.number", "10") },
            { REQ("body", "body", "循环体") }));

    add(def("control.repeatWhile", BS::CShape,
            "repeat while %1 %2", "当 %1 时重复 %2",
            { "while", "repeat", "loop", "until", "condition" },
            {},
            { IN("condition", "condition", "条件", VK::Boolean, "logic.true", "true") },
            { REQ("body", "body", "循环体") }));

    add(def("control.forEach", BS::CShape,
            "for each %1 in %2 %3", "遍历 %2 中的每个 %1 %3",
            { "foreach", "loop", "list", "each", "iterate" },
            { TF("variable", "item", "项目", "item") },
            { IN("list", "list", "列表", VK::List, QString(), "[]") },
            { REQ("body", "body", "循环体") }));

    add(def("control.break", BS::Stack,
            "break out of the loop", "跳出循环",
            { "break", "exit", "loop", "stop" }));

    add(def("control.continue", BS::Stack,
            "continue with the next iteration", "继续下一次循环",
            { "continue", "next", "skip", "loop" }));

    add(def("control.wait", BS::Stack,
            "wait %1 seconds", "等待 %1 秒",
            { "wait", "delay", "sleep", "pause", "timeout" },
            {},
            { IN("seconds", "seconds", "秒", VK::Number, "math.number", "1") }));

    add(def("control.return", BS::Cap,
            "return %1", "返回 %1",
            { "return", "result", "output", "value" },
            {},
            { IN("value", "value", "值", VK::Any, "text.value", QString()) },
            {},
            true));

    add(def("control.stop", BS::Cap,
            "stop this script", "停止本脚本",
            { "stop", "end", "halt", "exit", "finish" },
            {}, {}, {}, true));

    // -----------------------------------------------------------------------
    //  Logic -- comparisons and boolean algebra.
    //  逻辑 —— 比较与布尔运算。
    // -----------------------------------------------------------------------
    add(def("logic.and", BS::Boolean,
            "%1 and %2", "%1 且 %2",
            { "and", "both", "all", "conjunction" },
            {},
            { IN("a", "first", "第一个", VK::Boolean, "logic.true", "true"),
              IN("b", "second", "第二个", VK::Boolean, "logic.true", "true") }));

    add(def("logic.or", BS::Boolean,
            "%1 or %2", "%1 或 %2",
            { "or", "either", "any", "disjunction" },
            {},
            { IN("a", "first", "第一个", VK::Boolean, "logic.true", "true"),
              IN("b", "second", "第二个", VK::Boolean, "logic.true", "true") }));

    add(def("logic.not", BS::Boolean,
            "not %1", "非 %1",
            { "not", "negate", "invert", "opposite" },
            {},
            { IN("value", "value", "值", VK::Boolean, "logic.true", "true") }));

    add(def("logic.equals", BS::Boolean,
            "%1 = %2", "%1 = %2",
            { "equal", "equals", "same", "compare", "==" },
            {},
            { IN("a", "first", "第一个", VK::Any, "text.value", QString()),
              IN("b", "second", "第二个", VK::Any, "text.value", QString()) }));

    add(def("logic.notEquals", BS::Boolean,
            "%1 is not %2", "%1 不等于 %2",
            { "not", "equal", "different", "compare", "!=" },
            {},
            { IN("a", "first", "第一个", VK::Any, "text.value", QString()),
              IN("b", "second", "第二个", VK::Any, "text.value", QString()) }));

    add(def("logic.greater", BS::Boolean,
            "%1 > %2", "%1 > %2",
            { "greater", "more", "bigger", "compare", ">" },
            {},
            { IN("a", "first", "第一个", VK::Number, "math.number", "0"),
              IN("b", "second", "第二个", VK::Number, "math.number", "0") }));

    add(def("logic.less", BS::Boolean,
            "%1 < %2", "%1 < %2",
            { "less", "smaller", "below", "compare", "<" },
            {},
            { IN("a", "first", "第一个", VK::Number, "math.number", "0"),
              IN("b", "second", "第二个", VK::Number, "math.number", "0") }));

    add(def("logic.greaterOrEqual", BS::Boolean,
            "%1 ≥ %2", "%1 ≥ %2",
            { "greater", "equal", "at least", "compare", ">=" },
            {},
            { IN("a", "first", "第一个", VK::Number, "math.number", "0"),
              IN("b", "second", "第二个", VK::Number, "math.number", "0") }));

    add(def("logic.lessOrEqual", BS::Boolean,
            "%1 ≤ %2", "%1 ≤ %2",
            { "less", "equal", "at most", "compare", "<=" },
            {},
            { IN("a", "first", "第一个", VK::Number, "math.number", "0"),
              IN("b", "second", "第二个", VK::Number, "math.number", "0") }));

    add(def("logic.isNull", BS::Boolean,
            "%1 is empty", "%1 为空",
            { "empty", "null", "undefined", "missing", "blank" },
            {},
            { IN("value", "value", "值", VK::Any, "text.value", QString()) }));

    add(def("logic.between", BS::Boolean,
            "%1 is between %2 and %3", "%1 在 %2 与 %3 之间",
            { "between", "range", "within", "inside" },
            {},
            { IN("value", "value", "值", VK::Number, "math.number", "5"),
              IN("min", "minimum", "最小值", VK::Number, "math.number", "0"),
              IN("max", "maximum", "最大值", VK::Number, "math.number", "10") }));

    add(def("logic.true", BS::Boolean,
            "%1", "%1",
            { "true", "yes", "literal", "boolean", "constant" },
            { DD("value", "value", "值", { "true", "false" }, { "真", "假" }, "true") }));

    add(def("logic.false", BS::Boolean,
            "%1", "%1",
            { "false", "no", "literal", "boolean", "constant" },
            { DD("value", "value", "值", { "true", "false" }, { "真", "假" }, "false") }));

    // -----------------------------------------------------------------------
    //  Math -- numbers and arithmetic.
    //  数学 —— 数值与算术。
    // -----------------------------------------------------------------------
    add(def("math.number", BS::Round,
            "%1", "%1",
            { "number", "literal", "value", "constant", "digit" },
            { TF("value", "value", "值", "0") }));

    add(def("math.arithmetic", BS::Reporter,
            "%1 %2 %3", "%1 %2 %3",
            { "add", "subtract", "multiply", "divide", "arithmetic", "operator" },
            { DD("op", "operation", "运算",
                 { "+", "-", "*", "/", "%", "^" },
                 { "加", "减", "乘", "除", "取余", "乘方" }, "+") },
            { IN("a", "first", "第一个", VK::Number, "math.number", "0"),
              IN("b", "second", "第二个", VK::Number, "math.number", "0") }));

    add(def("math.random", BS::Reporter,
            "random number from %1 to %2", "从 %1 到 %2 的随机数",
            { "random", "chance", "dice", "shuffle", "pick" },
            {},
            { IN("from", "from", "最小", VK::Number, "math.number", "1"),
              IN("to", "to", "最大", VK::Number, "math.number", "10") }));

    add(def("math.round", BS::Reporter,
            "round %1", "四舍五入 %1",
            { "round", "nearest", "integer" },
            {},
            { IN("value", "value", "值", VK::Number, "math.number", "0") }));

    add(def("math.floor", BS::Reporter,
            "floor %1", "向下取整 %1",
            { "floor", "down", "integer", "truncate" },
            {},
            { IN("value", "value", "值", VK::Number, "math.number", "0") }));

    add(def("math.ceil", BS::Reporter,
            "ceiling %1", "向上取整 %1",
            { "ceil", "ceiling", "up", "integer" },
            {},
            { IN("value", "value", "值", VK::Number, "math.number", "0") }));

    add(def("math.abs", BS::Reporter,
            "absolute value of %1", "%1 的绝对值",
            { "absolute", "abs", "positive", "magnitude" },
            {},
            { IN("value", "value", "值", VK::Number, "math.number", "0") }));

    add(def("math.min", BS::Reporter,
            "smaller of %1 and %2", "%1 与 %2 中较小者",
            { "minimum", "min", "smaller", "least" },
            {},
            { IN("a", "first", "第一个", VK::Number, "math.number", "0"),
              IN("b", "second", "第二个", VK::Number, "math.number", "0") }));

    add(def("math.max", BS::Reporter,
            "larger of %1 and %2", "%1 与 %2 中较大者",
            { "maximum", "max", "larger", "greatest" },
            {},
            { IN("a", "first", "第一个", VK::Number, "math.number", "0"),
              IN("b", "second", "第二个", VK::Number, "math.number", "0") }));

    add(def("math.sqrt", BS::Reporter,
            "square root of %1", "%1 的平方根",
            { "square", "root", "sqrt" },
            {},
            { IN("value", "value", "值", VK::Number, "math.number", "0") }));

    add(def("math.pow", BS::Reporter,
            "%1 to the power of %2", "%1 的 %2 次方",
            { "power", "exponent", "pow", "squared" },
            {},
            { IN("base", "base", "底数", VK::Number, "math.number", "2"),
              IN("exponent", "exponent", "指数", VK::Number, "math.number", "2") }));

    add(def("math.mod", BS::Reporter,
            "remainder of %1 divided by %2", "%1 除以 %2 的余数",
            { "modulo", "mod", "remainder", "rest" },
            {},
            { IN("a", "dividend", "被除数", VK::Number, "math.number", "0"),
              IN("b", "divisor", "除数", VK::Number, "math.number", "1") }));

    add(def("math.clamp", BS::Reporter,
            "clamp %1 between %2 and %3", "把 %1 限制在 %2 与 %3 之间",
            { "clamp", "limit", "bound", "constrain", "range" },
            {},
            { IN("value", "value", "值", VK::Number, "math.number", "0"),
              IN("min", "minimum", "最小值", VK::Number, "math.number", "0"),
              IN("max", "maximum", "最大值", VK::Number, "math.number", "100") }));

    add(def("math.trig", BS::Reporter,
            "%1 of %2", "%2 的 %1",
            { "sin", "cos", "tan", "log", "trigonometry", "exponential" },
            { DD("fn", "function", "函数",
                 { "sin", "cos", "tan", "asin", "acos", "atan", "ln", "log", "exp" },
                 { "正弦", "余弦", "正切", "反正弦", "反余弦", "反正切",
                   "自然对数", "常用对数", "指数" }, "sin") },
            { IN("value", "value", "值", VK::Number, "math.number", "0") }));

    // -----------------------------------------------------------------------
    //  Variables -- named values and lists shared by every script.
    //  变量 —— 所有脚本共享的命名值与列表。
    // -----------------------------------------------------------------------
    add(def("variables.set", BS::Stack,
            "set %1 to %2", "把 %1 设为 %2",
            { "set", "assign", "variable", "store", "write" },
            { TF("name", "variable", "变量", "score") },
            { IN("value", "value", "值", VK::Any, "text.value", "0") }));

    add(def("variables.change", BS::Stack,
            "change %1 by %2", "把 %1 增加 %2",
            { "change", "increase", "decrease", "increment", "add" },
            { TF("name", "variable", "变量", "score") },
            { IN("value", "amount", "增量", VK::Number, "math.number", "1") }));

    add(def("variables.get", BS::Reporter,
            "value of %1", "%1 的值",
            { "get", "read", "variable", "value", "fetch" },
            { TF("name", "variable", "变量", "score") }));

    add(def("variables.listNew", BS::Stack,
            "create list %1 from %2", "用 %2 创建列表 %1",
            { "list", "create", "array", "new", "collection" },
            { TF("name", "list", "列表", "items") },
            { IN("items", "items", "项目", VK::List, QString(), "[]") }));

    add(def("variables.listAdd", BS::Stack,
            "add %2 to list %1", "把 %2 添加到列表 %1",
            { "list", "add", "append", "push", "item" },
            { TF("name", "list", "列表", "items") },
            { IN("item", "item", "项目", VK::Any, "text.value", QString()) }));

    add(def("variables.listGet", BS::Reporter,
            "item %2 of list %1", "列表 %1 的第 %2 项",
            { "list", "get", "item", "index", "element" },
            { TF("name", "list", "列表", "items") },
            { IN("index", "index", "序号", VK::Number, "math.number", "1") }));

    add(def("variables.listLength", BS::Reporter,
            "length of list %1", "列表 %1 的长度",
            { "list", "length", "size", "count" },
            { TF("name", "list", "列表", "items") }));

    add(def("variables.listRemove", BS::Stack,
            "remove item %2 from list %1", "从列表 %1 中移除第 %2 项",
            { "list", "remove", "delete", "splice", "item" },
            { TF("name", "list", "列表", "items") },
            { IN("index", "index", "序号", VK::Number, "math.number", "1") }));

    // -----------------------------------------------------------------------
    //  Storage -- localStorage, sessionStorage and cookies.
    //  存储 —— localStorage、sessionStorage 与 Cookie。
    // -----------------------------------------------------------------------
    add(def("storage.set", BS::Stack,
            "set local storage %1 to %2", "把本地存储 %1 设为 %2",
            { "storage", "local", "save", "persist", "key" },
            { TF("key", "key", "键", "myKey") },
            { IN("value", "value", "值", VK::Any, "text.value", QString()) }));

    add(def("storage.get", BS::Reporter,
            "local storage %1", "本地存储 %1",
            { "storage", "local", "load", "read", "key" },
            { TF("key", "key", "键", "myKey") }));

    add(def("storage.remove", BS::Stack,
            "remove local storage %1", "删除本地存储 %1",
            { "storage", "local", "remove", "delete", "key" },
            { TF("key", "key", "键", "myKey") }));

    add(def("storage.clear", BS::Stack,
            "clear all local storage", "清空本地存储",
            { "storage", "local", "clear", "reset", "wipe" }));

    add(def("storage.sessionSet", BS::Stack,
            "set session storage %1 to %2", "把会话存储 %1 设为 %2",
            { "storage", "session", "save", "temporary", "key" },
            { TF("key", "key", "键", "myKey") },
            { IN("value", "value", "值", VK::Any, "text.value", QString()) }));

    add(def("storage.sessionGet", BS::Reporter,
            "session storage %1", "会话存储 %1",
            { "storage", "session", "load", "read", "key" },
            { TF("key", "key", "键", "myKey") }));

    add(def("storage.cookieSet", BS::Stack,
            "set cookie %1 to %2 for %3 days", "把 Cookie %1 设为 %2，保留 %3 天",
            { "cookie", "storage", "save", "expire", "browser" },
            { TF("key", "key", "键", "sessionId") },
            { IN("value", "value", "值", VK::Any, "text.value", QString()),
              IN("days", "days", "天数", VK::Number, "math.number", "7") }));

    add(def("storage.cookieGet", BS::Reporter,
            "cookie %1", "Cookie %1",
            { "cookie", "storage", "load", "read", "browser" },
            { TF("key", "key", "键", "sessionId") }));

    // -----------------------------------------------------------------------
    //  HTTP -- fetch based web requests.
    //  网络请求 —— 基于 fetch 的请求。
    // -----------------------------------------------------------------------
    add(def("http.get", BS::Reporter,
            "GET %1", "GET %1",
            { "http", "get", "fetch", "request", "api", "url" },
            {},
            { IN("url", "URL", "地址", VK::String, "text.value", "/api/items") }));

    add(def("http.post", BS::Reporter,
            "POST %1 with %2", "向 %1 发送 POST，请求体 %2",
            { "http", "post", "create", "submit", "api" },
            {},
            { IN("url", "URL", "地址", VK::String, "text.value", "/api/items"),
              IN("body", "body", "请求体", VK::Object, QString(), "{}") }));

    add(def("http.put", BS::Reporter,
            "PUT %1 with %2", "向 %1 发送 PUT，请求体 %2",
            { "http", "put", "update", "replace", "api" },
            {},
            { IN("url", "URL", "地址", VK::String, "text.value", "/api/items/1"),
              IN("body", "body", "请求体", VK::Object, QString(), "{}") }));

    add(def("http.patch", BS::Reporter,
            "PATCH %1 with %2", "向 %1 发送 PATCH，请求体 %2",
            { "http", "patch", "update", "modify", "api" },
            {},
            { IN("url", "URL", "地址", VK::String, "text.value", "/api/items/1"),
              IN("body", "body", "请求体", VK::Object, QString(), "{}") }));

    add(def("http.delete", BS::Reporter,
            "DELETE %1", "DELETE %1",
            { "http", "delete", "remove", "destroy", "api" },
            {},
            { IN("url", "URL", "地址", VK::String, "text.value", "/api/items/1") }));

    add(def("http.request", BS::Reporter,
            "%1 %2 with body %3", "以 %3 为请求体发送 %1 %2",
            { "http", "request", "method", "custom", "api" },
            { DD("method", "method", "方法",
                 { "GET", "POST", "PUT", "PATCH", "DELETE", "HEAD", "OPTIONS" }, {}, "GET") },
            { IN("url", "URL", "地址", VK::String, "text.value", "/api/items"),
              IN("body", "body", "请求体", VK::Object, QString(), "{}") }));

    add(def("http.setHeader", BS::Stack,
            "set request header %1 to %2", "把请求头 %1 设为 %2",
            { "http", "header", "authorisation", "authorization", "token", "content-type" },
            { TF("name", "header", "请求头", "Content-Type") },
            { IN("value", "value", "值", VK::String, "text.value", "application/json") }));

    add(def("http.parseJson", BS::Reporter,
            "parse JSON %1", "解析 JSON %1",
            { "json", "parse", "decode", "http", "object" },
            {},
            { IN("text", "text", "文本", VK::String, "text.value", "{}") }));

    add(def("http.onSuccess", BS::CShape,
            "when request %1 succeeds %2", "当请求 %1 成功时 %2",
            { "http", "then", "success", "response", "callback" },
            {},
            { IN("request", "request", "请求", VK::Any, "http.get", QString()) },
            { BR("body", "success handler", "成功处理") }));

    add(def("http.onError", BS::CShape,
            "when request %1 fails %2", "当请求 %1 失败时 %2",
            { "http", "catch", "error", "failure", "callback" },
            {},
            { IN("request", "request", "请求", VK::Any, "http.get", QString()) },
            { BR("body", "error handler", "失败处理") }));

    // -----------------------------------------------------------------------
    //  Backend -- the pre-linked server side of the project.
    //  后端 —— 工程预链接的服务端。
    // -----------------------------------------------------------------------
    add(def("backend.callEndpoint", BS::Reporter,
            "call endpoint %1 with %2", "调用后端接口 %1，参数 %2",
            { "backend", "endpoint", "api", "call", "server" },
            { TF("endpoint", "endpoint", "接口", "myEndpoint") },
            { IN("variables", "variables", "变量", VK::Object, QString(), "{}") }));

    add(def("backend.callGraphQl", BS::Reporter,
            "GraphQL %1 with variables %2", "GraphQL 查询 %1，变量 %2",
            { "backend", "graphql", "query", "api", "server" },
            {},
            { IN("query", "query", "查询", VK::String, "text.value", "query { ping }"),
              IN("variables", "variables", "变量", VK::Object, QString(), "{}") }));

    add(def("backend.subscribe", BS::CShape,
            "subscribe to %1 %2", "订阅 %1 %2",
            { "backend", "subscribe", "realtime", "websocket", "channel" },
            {},
            { IN("channel", "channel", "频道", VK::String, "text.value", "updates") },
            { REQ("body", "handler", "处理") }));

    add(def("backend.uploadFile", BS::Reporter,
            "upload file %1 to %2", "把文件 %1 上传到 %2",
            { "backend", "upload", "file", "multipart", "asset" },
            {},
            { IN("file", "file", "文件", VK::Any, "text.value", "fileInput"),
              IN("url", "URL", "地址", VK::String, "text.value", "/api/upload") }));

    add(def("backend.authLogin", BS::Reporter,
            "log in with email %1 and password %2", "用邮箱 %1 与密码 %2 登录",
            { "backend", "login", "auth", "signin", "password", "account" },
            {},
            { IN("email", "email", "邮箱", VK::String, "text.value", "user@example.com"),
              IN("password", "password", "密码", VK::String, "text.value", QString()) }));

    add(def("backend.authLogout", BS::Stack,
            "log out of the backend", "退出后端登录",
            { "backend", "logout", "auth", "signout", "session" }));

    add(def("backend.getUser", BS::Reporter,
            "current user", "当前用户",
            { "backend", "user", "profile", "account", "auth" }));

    // -----------------------------------------------------------------------
    //  Data -- JSON and list transformations.
    //  数据 —— JSON 与列表转换。
    // -----------------------------------------------------------------------
    add(def("data.parseJson", BS::Reporter,
            "parse JSON %1", "解析 JSON %1",
            { "json", "parse", "decode", "object", "data" },
            {},
            { IN("text", "text", "文本", VK::String, "text.value", "{}") }));

    add(def("data.stringify", BS::Reporter,
            "to JSON text %1", "把 %1 转为 JSON 文本",
            { "json", "stringify", "encode", "serialise", "data" },
            {},
            { IN("value", "value", "值", VK::Any, "text.value", "{}") }));

    add(def("data.getField", BS::Reporter,
            "field %1 of %2", "%2 的字段 %1",
            { "field", "property", "get", "path", "object" },
            { TF("path", "field path", "字段路径", "data.name") },
            { IN("object", "object", "对象", VK::Object, QString(), "{}") }));

    add(def("data.setField", BS::Stack,
            "set field %1 of %2 to %3", "把 %2 的字段 %1 设为 %3",
            { "field", "property", "set", "path", "object" },
            { TF("path", "field path", "字段路径", "data.name") },
            { IN("object", "object", "对象", VK::Object, QString(), "{}"),
              IN("value", "value", "值", VK::Any, "text.value", QString()) }));

    add(def("data.filterList", BS::Reporter,
            "filter %1 where %2", "筛选 %1，条件 %2",
            { "filter", "where", "select", "list", "query" },
            {},
            { IN("list", "list", "列表", VK::List, QString(), "[]"),
              IN("predicate", "condition", "条件", VK::Boolean, "logic.true", "true") }));

    add(def("data.mapList", BS::Reporter,
            "map %1 using %2", "用 %2 映射 %1",
            { "map", "transform", "convert", "list", "project" },
            {},
            { IN("list", "list", "列表", VK::List, QString(), "[]"),
              IN("expression", "expression", "表达式", VK::Any, "text.value", "item") }));

    add(def("data.sortList", BS::Reporter,
            "sort list %2 in %1 order", "按 %1 顺序排序列表 %2",
            { "sort", "order", "arrange", "list", "rank" },
            { DD("direction", "direction", "顺序",
                 { "ascending", "descending" }, { "升序", "降序" }, "ascending") },
            { IN("list", "list", "列表", VK::List, QString(), "[]") }));

    add(def("data.length", BS::Reporter,
            "length of %1", "%1 的长度",
            { "length", "size", "count", "list", "object" },
            {},
            { IN("list", "list", "列表", VK::List, QString(), "[]") }));

    // -----------------------------------------------------------------------
    //  AI -- the assistant runs in the exported page through a runtime helper.
    //  No API key is ever embedded in generated code.
    //  AI —— 导出的页面通过运行时辅助函数调用助手。生成的代码中绝不内嵌 API Key。
    // -----------------------------------------------------------------------
    add(def("ai.ask", BS::Reporter,
            "ask AI %3 using model %1 at %2", "用模型 %1 向 %2 上的 AI 提问 %3",
            { "ai", "ask", "prompt", "chat", "model", "generate" },
            { TF("model", "model", "模型", QString()),
              TF("target", "target", "目标", "#app") },
            { IN("prompt", "prompt", "提示词", VK::String, "text.value",
                 "Summarise this page in one sentence.") }));

    add(def("ai.askWithContext", BS::Reporter,
            "ask AI %3 with context %4 using model %1 at %2",
            "用模型 %1 向 %2 上的 AI 提问 %3，附加上下文 %4",
            { "ai", "ask", "context", "prompt", "rag", "model" },
            { TF("model", "model", "模型", QString()),
              TF("target", "target", "目标", "#app") },
            { IN("prompt", "prompt", "提示词", VK::String, "text.value", "What does this mean?"),
              IN("context", "context", "上下文", VK::String, "text.value", QString()) }));

    add(def("ai.summarise", BS::Reporter,
            "summarise %2 in %1 style", "用 %1 风格总结 %2",
            { "ai", "summarise", "summary", "shorten", "abstract" },
            { DD("length", "length", "长度",
                 { "short", "medium", "detailed" }, { "简短", "中等", "详细" }, "short") },
            { IN("text", "text", "文本", VK::String, "text.value", QString()) }));

    add(def("ai.translate", BS::Reporter,
            "translate %2 into %1", "把 %2 翻译成 %1",
            { "ai", "translate", "language", "localise", "i18n" },
            { TF("language", "language", "语言", "English") },
            { IN("text", "text", "文本", VK::String, "text.value", QString()) }));

    add(def("ai.generateText", BS::Reporter,
            "generate text from %2 with model %1", "用模型 %1 根据 %2 生成文本",
            { "ai", "generate", "write", "draft", "text", "model" },
            { TF("model", "model", "模型", QString()) },
            { IN("prompt", "prompt", "提示词", VK::String, "text.value",
                 "Write a friendly welcome message.") }));

    add(def("ai.generateImage", BS::Reporter,
            "generate image from %3 with model %1 at %2 size",
            "用模型 %1 以 %2 尺寸根据 %3 生成图片",
            { "ai", "image", "generate", "picture", "draw", "diffusion" },
            { TF("model", "model", "模型", QString()),
              DD("size", "size", "尺寸",
                 { "512x512", "1024x1024", "1024x1792", "1792x1024" }, {}, "1024x1024") },
            { IN("prompt", "prompt", "提示词", VK::String, "text.value",
                 "A calm mountain lake at sunrise.") }));

    add(def("ai.classify", BS::Reporter,
            "classify %2 into %1", "把 %2 归类为 %1",
            { "ai", "classify", "label", "category", "sentiment" },
            { TF("labels", "labels", "标签", "positive, negative, neutral") },
            { IN("text", "text", "文本", VK::String, "text.value", QString()) }));

    add(def("ai.chatMessage", BS::Stack,
            "add a %1 chat message with %2", "添加一条 %1 对话消息，内容为 %2",
            { "ai", "chat", "message", "conversation", "role" },
            { DD("role", "role", "角色",
                 { "system", "user", "assistant" }, { "系统", "用户", "助手" }, "user") },
            { IN("content", "content", "内容", VK::String, "text.value", QString()) }));

    add(def("ai.setSystemPrompt", BS::Stack,
            "set the AI system prompt to %1", "把 AI 系统提示词设为 %1",
            { "ai", "system", "prompt", "instruction", "persona" },
            {},
            { IN("prompt", "prompt", "提示词", VK::String, "text.value",
                 "You are a helpful assistant.") }));

    // -----------------------------------------------------------------------
    //  MCP -- tools and resources exposed by Model Context Protocol servers.
    //  MCP —— 由模型上下文协议服务器提供的工具与资源。
    // -----------------------------------------------------------------------
    add(def("mcp.callTool", BS::Reporter,
            "call MCP tool %2 on %1 with %3", "在 %1 上调用 MCP 工具 %2，参数 %3",
            { "mcp", "tool", "call", "server", "function" },
            { TF("server", "server", "服务器", "local"),
              TF("tool", "tool", "工具", "read_file") },
            { IN("arguments", "arguments", "参数", VK::Object, QString(), "{}") }));

    add(def("mcp.listTools", BS::Reporter,
            "list MCP tools on %1", "列出 %1 上的 MCP 工具",
            { "mcp", "tools", "list", "discover", "server" },
            { TF("server", "server", "服务器", "local") }));

    add(def("mcp.readResource", BS::Reporter,
            "read MCP resource %2 from %1", "从 %1 读取 MCP 资源 %2",
            { "mcp", "resource", "read", "uri", "server" },
            { TF("server", "server", "服务器", "local") },
            { IN("uri", "uri", "资源地址", VK::String, "text.value", "mcp://resource") }));

    // -----------------------------------------------------------------------
    //  Timing -- delays, intervals and dates.
    //  定时 —— 延时、定时重复与日期。
    // -----------------------------------------------------------------------
    add(def("timing.after", BS::CShape,
            "after %1 seconds %2", "%1 秒后 %2",
            { "after", "delay", "later", "timeout", "once" },
            {},
            { IN("seconds", "seconds", "秒", VK::Number, "math.number", "1") },
            { BR("body", "body", "执行") }));

    add(def("timing.every", BS::CShape,
            "every %1 seconds %2", "每隔 %1 秒 %2",
            { "every", "interval", "repeat", "poll", "tick" },
            {},
            { IN("seconds", "seconds", "秒", VK::Number, "math.number", "5") },
            { REQ("body", "body", "执行") }));

    add(def("timing.debounce", BS::Stack,
            "debounce %1 by %2 milliseconds", "把 %1 防抖 %2 毫秒",
            { "debounce", "throttle", "delay", "input", "rate" },
            { TF("eventName", "event", "事件", "input") },
            { IN("milliseconds", "delay", "延迟", VK::Number, "math.number", "300") }));

    add(def("timing.dateNow", BS::Reporter,
            "current date and time", "当前日期时间",
            { "date", "now", "time", "clock", "today" }));

    add(def("timing.formatDate", BS::Reporter,
            "format date %1 as %2", "把日期 %1 格式化为 %2",
            { "date", "format", "time", "display", "string" },
            {},
            { IN("date", "date", "日期", VK::Any, "timing.dateNow", QString()),
              IN("format", "format", "格式", VK::String, "text.value", "YYYY-MM-DD HH:mm") }));

    // -----------------------------------------------------------------------
    //  Console -- diagnostics while building and after export.
    //  控制台 —— 编辑期间与导出后的诊断输出。
    // -----------------------------------------------------------------------
    add(def("console.log", BS::Stack,
            "log %1 to the console", "在控制台输出 %1",
            { "console", "log", "print", "debug", "output" },
            {},
            { IN("message", "message", "内容", VK::Any, "text.value", "Hello") }));

    add(def("console.warn", BS::Stack,
            "warn %1 in the console", "在控制台警告 %1",
            { "console", "warn", "warning", "notice" },
            {},
            { IN("message", "message", "内容", VK::Any, "text.value", "Careful") }));

    add(def("console.error", BS::Stack,
            "show error %1 in the console", "在控制台显示错误 %1",
            { "console", "error", "fail", "problem" },
            {},
            { IN("message", "message", "内容", VK::Any, "text.value", "Something failed") }));

    add(def("console.table", BS::Stack,
            "table %1 in the console", "在控制台以表格显示 %1",
            { "console", "table", "rows", "list", "inspect" },
            {},
            { IN("message", "message", "内容", VK::Any, "text.value", "[]") }));

    add(def("console.time", BS::Stack,
            "start console timer %1", "启动控制台计时器 %1",
            { "console", "time", "timer", "measure", "performance" },
            { TF("label", "label", "标签", "timer") }));

    add(def("console.timeEnd", BS::Stack,
            "stop console timer %1", "停止控制台计时器 %1",
            { "console", "timeEnd", "timer", "measure", "performance" },
            { TF("label", "label", "标签", "timer") }));

    // @@BLOCK_CATALOGUE@@
}

} // namespace tcbw
