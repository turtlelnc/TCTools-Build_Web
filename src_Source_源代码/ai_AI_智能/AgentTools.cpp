// ============================================================================
//  AgentTools.cpp -- the capabilities the AI is allowed to use on the platform
//  AgentTools.cpp —— AI 在本平台上被允许使用的能力
//
//  DESIGN / 设计要点
//    * Every mutation is a declared tool with a JSON Schema, so the model can
//      never invent an operation and the UI can explain exactly what will change.
//    * Handlers are defensive: a bad argument produces a bilingual error result
//      (isError = true) instead of an exception or a partially applied change.
//    * Nothing is written outside `ToolContext::workspaceDirectory`.
//    * 每次变更都是带 JSON Schema 的声明式工具，因此模型无法凭空创造操作，
//      界面也能准确说明将要发生的变化。
//    * 处理函数是防御式的：错误参数产生双语的错误结果（isError = true），
//      而不是抛出异常或造成半途而废的修改。
//    * 绝不写入 `ToolContext::workspaceDirectory` 之外的位置。
// ============================================================================

#include "AgentTools.h"

#include "BlockCompiler.h"
#include "BlockDef.h"
#include "BlockRegistry.h"
#include "CodeGenerator.h"
#include "DocumentNode.h"
#include "ElementCatalog.h"
#include "EmitOptions.h"
#include "Endpoint.h"
#include "IdGenerator.h"
#include "Page.h"
#include "ProjectDocument.h"
#include "StyleSheet.h"
#include "Types.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QStringList>
#include <QVector>

#include <algorithm>
#include <memory>

namespace tcbw {
namespace {

// ---------------------------------------------------------------------------
//  Constants
//  常量
// ---------------------------------------------------------------------------

//  Maximum entries kept in the invocation log.
//  调用日志保留的最大条目数。
constexpr int kMaxLogEntries = 500;

//  Truncation limits that keep tool results cheap for the model.
//  让工具结果对模型保持轻量的截断上限。
constexpr int kTextPreviewChars = 40;
constexpr int kMaxOutlineNodes = 200;
constexpr int kMaxSearchResults = 50;
constexpr int kMaxCodeChars = 20000;

// ---------------------------------------------------------------------------
//  Local helpers
//  本地辅助函数
// ---------------------------------------------------------------------------

//  Compact UTF-8 JSON, used for every tool result.
//  紧凑 UTF-8 JSON，用于所有工具结果。
QString compact(const QJsonObject &object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

QString compact(const QJsonArray &array)
{
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

//  Builds a bilingual message (English line, then Chinese).
//  构建双语消息（英文一行，随后中文）。
QString bilingual(const QString &en, const QString &zh)
{
    return en + QStringLiteral("\n") + zh;
}

//  A failed result; the message is bilingual and never throws.
//  失败结果；消息为双语，且绝不抛出异常。
ToolResult errorResult(const QString &en, const QString &zh)
{
    ToolResult result;
    result.isError = true;
    result.content = bilingual(en, zh);
    return result;
}

//  A successful result carrying a compact JSON payload.
//  携带紧凑 JSON 载荷的成功结果。
ToolResult okResult(const QJsonObject &payload)
{
    ToolResult result;
    result.isError = false;
    result.content = compact(payload);
    return result;
}

//  First 40 characters of a node's text, newlines flattened.
//  节点文本的前 40 个字符，换行符被压平。
QString textPreview(const QString &text)
{
    QString flat = text;
    flat.replace(QLatin1Char('\n'), QLatin1Char(' '));
    flat.replace(QLatin1Char('\r'), QLatin1Char(' '));
    flat = flat.simplified();
    if (flat.size() > kTextPreviewChars)
        flat = flat.left(kTextPreviewChars) + QStringLiteral("...");
    return flat;
}

//  Converts a string map to JSON.
//  将字符串映射转换为 JSON。
QJsonObject mapToJson(const QMap<QString, QString> &map)
{
    QJsonObject object;
    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
        object.insert(it.key(), it.value());
    return object;
}

// ---------------------------------------------------------------------------
//  Context resolution
//  上下文解析
// ---------------------------------------------------------------------------

//  Resolves a page from an explicit id/name, else the active page.
//  由显式 id/名称解析页面，否则使用活动页面。
Page *resolvePage(ToolContext &context, const QString &explicitId, QString *errorEn,
                  QString *errorZh)
{
    const auto fail = [errorEn, errorZh](const QString &en, const QString &zh) -> Page * {
        if (errorEn)
            *errorEn = en;
        if (errorZh)
            *errorZh = zh;
        return nullptr;
    };

    if (!context.project)
        return fail(QStringLiteral("No project is open, so no page can be resolved."),
                    QStringLiteral("当前没有打开工程，无法解析页面。"));

    QString key = explicitId.trimmed();
    if (key.isEmpty())
        key = context.activePageId.trimmed();
    if (key.isEmpty())
        key = context.project->activePageId.trimmed();

    if (key.isEmpty()) {
        if (context.project->pageCount() == 1)
            return context.project->pageAt(0);
        return fail(QStringLiteral("No page id was given and no page is active. Call list_pages "
                                   "and pass an explicit pageId."),
                    QStringLiteral("未提供页面 id 且当前没有活动页面。请调用 list_pages 并显式"
                                   "传入 pageId。"));
    }

    Page *page = context.project->pageById(key);
    if (!page)
        page = context.project->pageByName(key);
    if (!page)
        return fail(QStringLiteral("No page has the id or name \"%1\". Call list_pages for the "
                                   "current ids.").arg(key),
                    QStringLiteral("没有 id 或名称为 \"%1\" 的页面。请调用 list_pages 获取"
                                   "当前 id。").arg(key));
    return page;
}

//  Resolves a node inside a page, falling back to the active node.
//  在页面内解析节点，回退到活动节点。
DocumentNode *resolveNode(Page *page, ToolContext &context, const QString &explicitId,
                          QString *errorEn, QString *errorZh)
{
    const auto fail = [errorEn, errorZh](const QString &en, const QString &zh) -> DocumentNode * {
        if (errorEn)
            *errorEn = en;
        if (errorZh)
            *errorZh = zh;
        return nullptr;
    };

    if (!page || !page->root)
        return fail(QStringLiteral("The page has no element tree."),
                    QStringLiteral("该页面没有元素树。"));

    QString id = explicitId.trimmed();
    if (id.isEmpty())
        id = context.activeNodeId.trimmed();

    if (id.isEmpty())
        return fail(QStringLiteral("No element id was given and no element is selected. Call "
                                   "get_page_outline to find the id."),
                    QStringLiteral("未提供元素 id 且当前没有选中的元素。请调用 get_page_outline "
                                   "查找 id。"));

    DocumentNode *node = page->findNode(id);
    if (!node)
        return fail(QStringLiteral("No element with id \"%1\" exists on page \"%2\".")
                        .arg(id, page->name),
                    QStringLiteral("页面 \"%2\" 上不存在 id 为 \"%1\" 的元素。")
                        .arg(id, page->name));
    return node;
}

// ---------------------------------------------------------------------------
//  Serialisation helpers
//  序列化辅助
// ---------------------------------------------------------------------------

//  Full detail object for one node.
//  单个节点的完整详情对象。
QJsonObject nodeDetail(const DocumentNode *node)
{
    QJsonObject object;
    if (!node)
        return object;

    object.insert(QStringLiteral("id"), node->id);
    object.insert(QStringLiteral("tag"), node->tag);
    object.insert(QStringLiteral("kind"), toString(node->kind));
    if (!node->name.isEmpty())
        object.insert(QStringLiteral("name"), node->name);
    if (!node->componentId.isEmpty())
        object.insert(QStringLiteral("componentId"), node->componentId);
    object.insert(QStringLiteral("depth"), node->depth());
    if (!node->isLeaf())
        object.insert(QStringLiteral("childCount"), node->childCount());
    if (!node->text.isEmpty())
        object.insert(QStringLiteral("text"), node->text);
    object.insert(QStringLiteral("textIsHtml"), node->textIsHtml);

    const QMap<QString, QString> attributes = node->effectiveAttributes();
    if (!attributes.isEmpty())
        object.insert(QStringLiteral("attributes"), mapToJson(attributes));
    if (!node->classes.isEmpty()) {
        QJsonArray classes;
        for (const QString &value : node->classes)
            classes.append(value);
        object.insert(QStringLiteral("classes"), classes);
    }

    const QMap<QString, QString> styles = node->style.resolved(Breakpoint::Desktop);
    if (!styles.isEmpty())
        object.insert(QStringLiteral("styles"), mapToJson(styles));
    if (node->style.hasOverride(Breakpoint::Tablet))
        object.insert(QStringLiteral("stylesTablet"),
                      mapToJson(node->style.resolved(Breakpoint::Tablet)));
    if (node->style.hasOverride(Breakpoint::Mobile))
        object.insert(QStringLiteral("stylesMobile"),
                      mapToJson(node->style.resolved(Breakpoint::Mobile)));
    if (node->hidden)
        object.insert(QStringLiteral("hidden"), true);
    if (node->locked)
        object.insert(QStringLiteral("locked"), true);

    DocumentNode *parent = node->parent();
    if (parent)
        object.insert(QStringLiteral("parentId"), parent->id);
    return object;
}

//  Cheap outline entry: id, tag, text preview, depth and child count.
//  轻量纲要条目：id、标签、文本预览、深度与子节点数。
QJsonObject nodeSummary(const DocumentNode *node, int depth)
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), node->id);
    object.insert(QStringLiteral("tag"), node->tag);
    if (!node->text.isEmpty())
        object.insert(QStringLiteral("text"), textPreview(node->text));
    object.insert(QStringLiteral("depth"), depth);
    if (!node->isLeaf())
        object.insert(QStringLiteral("children"), node->childCount());
    if (node->hidden)
        object.insert(QStringLiteral("hidden"), true);
    return object;
}

//  Flattens a subtree into outline entries, bounded by `limit`.
//  将子树展平为纲要条目，数量受 `limit` 限制。
void collectOutline(const DocumentNode *node, int depth, int maxDepth, int limit, QJsonArray *out)
{
    if (!node || !out || out->size() >= limit)
        return;
    out->append(nodeSummary(node, depth));
    if (depth >= maxDepth)
        return;
    for (const std::unique_ptr<DocumentNode> &child : node->children()) {
        if (out->size() >= limit)
            return;
        collectOutline(child.get(), depth + 1, maxDepth, limit, out);
    }
}

//  Resolves the emit mode from its wire string.
//  由接口字符串解析导出模式。
ExportMode exportModeFromTool(const QString &value)
{
    const QString key = value.trimmed().toLower();
    if (key == QStringLiteral("html-with-assets"))
        return ExportMode::HtmlWithAssets;
    if (key == QStringLiteral("script-only"))
        return ExportMode::ScriptOnly;
    if (key == QStringLiteral("headless-bundle"))
        return ExportMode::HeadlessBundle;
    return ExportMode::SingleHtml;
}

//  Maps a breakpoint name onto the enum, defaulting to desktop.
//  将断点名称映射为枚举，默认为桌面端。
Breakpoint breakpointFromTool(const QString &value)
{
    const QString key = value.trimmed().toLower();
    if (key == QStringLiteral("tablet"))
        return Breakpoint::Tablet;
    if (key == QStringLiteral("mobile"))
        return Breakpoint::Mobile;
    return Breakpoint::Desktop;
}

//  Locates the block with `blockId` anywhere inside a program.
//  在程序内任意位置查找 id 为 `blockId` 的积木。
Block *findBlockInProgram(const QVector<BlockScript *> &program, const QString &blockId)
{
    Block *found = nullptr;
    for (BlockScript *script : program) {
        if (!script)
            continue;
        if (script->hat && script->hat->id == blockId)
            return script->hat.get();
        script->forEach([&found, &blockId](Block *block) {
            if (!found && block && block->id == blockId)
                found = block;
        });
        if (found)
            return found;
    }
    return nullptr;
}

//  Resolves the script that owns `blockId` (or the script itself by id).
//  解析拥有 `blockId` 的脚本（或按 id 直接匹配脚本）。
BlockScript *findScriptFor(const QVector<BlockScript *> &program, const QString &blockId,
                           bool *isHat)
{
    for (BlockScript *script : program) {
        if (!script)
            continue;
        if (script->id == blockId) {
            if (isHat)
                *isHat = true;
            return script;
        }
        if (script->hat && script->hat->id == blockId) {
            if (isHat)
                *isHat = true;
            return script;
        }
        for (Block *block : script->body) {
            if (!block)
                continue;
            bool matched = (block->id == blockId);
            if (!matched) {
                block->forEach([&matched, &blockId](Block *nested) {
                    if (!matched && nested && nested->id == blockId)
                        matched = true;
                });
            }
            if (matched) {
                if (isHat)
                    *isHat = false;
                return script;
            }
        }
    }
    return nullptr;
}

//  Endpoints live in `ProjectDocument::backendConfig` as an "endpoints" array.
//  端点保存在 `ProjectDocument::backendConfig` 的 "endpoints" 数组中。
QJsonArray endpointsOf(const ProjectDocument *project)
{
    if (!project)
        return QJsonArray();
    return project->backendConfig.value(QStringLiteral("endpoints")).toArray();
}

void storeEndpoints(ProjectDocument *project, const QJsonArray &endpoints)
{
    if (!project)
        return;
    QJsonObject config = project->backendConfig;
    config.insert(QStringLiteral("endpoints"), endpoints);
    project->backendConfig = config;
}

} // namespace

// ===========================================================================
//  agenttemplates -- the JSON Schema builders
//  agenttemplates —— JSON Schema 构建辅助
// ===========================================================================
namespace agenttemplates {

QJsonObject schemaObject(const QJsonObject &properties, const QStringList &required)
{
    QJsonObject schema;
    schema.insert(QStringLiteral("type"), QStringLiteral("object"));
    schema.insert(QStringLiteral("properties"), properties);
    if (!required.isEmpty()) {
        QJsonArray requiredArray;
        for (const QString &name : required)
            requiredArray.append(name);
        schema.insert(QStringLiteral("required"), requiredArray);
    }
    //  Additional properties are refused so a model typo fails loudly instead of
    //  being silently ignored.
    //  拒绝额外属性，使模型的拼写错误显式失败而不是被静默忽略。
    schema.insert(QStringLiteral("additionalProperties"), false);
    return schema;
}

QJsonObject propString(const QString &description, const QString &enumValue)
{
    QJsonObject property;
    property.insert(QStringLiteral("type"), QStringLiteral("string"));
    property.insert(QStringLiteral("description"), description);
    if (!enumValue.isEmpty())
        property.insert(QStringLiteral("enum"), QJsonArray{ enumValue });
    return property;
}

QJsonObject propNumber(const QString &description, double min, double max)
{
    QJsonObject property;
    property.insert(QStringLiteral("type"), QStringLiteral("number"));
    property.insert(QStringLiteral("description"), description);
    if (min != 0.0 || max != 0.0) {
        property.insert(QStringLiteral("minimum"), min);
        property.insert(QStringLiteral("maximum"), max);
    }
    return property;
}

QJsonObject propBoolean(const QString &description)
{
    QJsonObject property;
    property.insert(QStringLiteral("type"), QStringLiteral("boolean"));
    property.insert(QStringLiteral("description"), description);
    return property;
}

QJsonObject propEnum(const QString &description, const QStringList &values)
{
    QJsonObject property;
    property.insert(QStringLiteral("type"), QStringLiteral("string"));
    property.insert(QStringLiteral("description"), description);
    QJsonArray array;
    for (const QString &value : values)
        array.append(value);
    property.insert(QStringLiteral("enum"), array);
    return property;
}

QJsonObject propArray(const QString &description, const QJsonObject &items)
{
    QJsonObject property;
    property.insert(QStringLiteral("type"), QStringLiteral("array"));
    property.insert(QStringLiteral("description"), description);
    property.insert(QStringLiteral("items"), items);
    return property;
}

QJsonObject propObject(const QString &description)
{
    QJsonObject property;
    property.insert(QStringLiteral("type"), QStringLiteral("object"));
    property.insert(QStringLiteral("description"), description);
    return property;
}

} // namespace agenttemplates

// ===========================================================================
//  AgentToolRegistry -- lifecycle
//  AgentToolRegistry —— 生命周期
// ===========================================================================

AgentToolRegistry::AgentToolRegistry(QObject *parent)
    : QObject(parent)
{
    registerDefaults();
}

AgentToolRegistry::~AgentToolRegistry() = default;

void AgentToolRegistry::registerDefaults()
{
    registerBuiltinTools();
}

// ---------------------------------------------------------------------------
//  registerTool
//  注册单个工具
// ---------------------------------------------------------------------------
void AgentToolRegistry::registerTool(const ToolDescriptor &descriptor, Handler handler)
{
    if (descriptor.name.trimmed().isEmpty())
        return;

    Entry entry;
    entry.descriptor = descriptor;
    entry.handler = std::move(handler);

    const auto existing = m_index.constFind(descriptor.name);
    if (existing != m_index.constEnd()) {
        //  Re-registration replaces the handler; the name stays unique.
        //  重复注册会替换处理函数；名称保持唯一。
        m_entries[existing.value()] = entry;
        return;
    }

    m_index.insert(descriptor.name, static_cast<int>(m_entries.size()));
    m_entries.append(entry);
}

// ---------------------------------------------------------------------------
//  attachMcpClient
//  挂接 MCP 客户端
// ---------------------------------------------------------------------------
void AgentToolRegistry::attachMcpClient(McpClient *client)
{
    if (!client)
        return;

    const QString serverId = client->config().id.trimmed();
    if (serverId.isEmpty())
        return;

    m_mcpClients.insert(serverId, client);

    //  MCP tools arrive asynchronously and may be re-listed when the server
    //  announces a change, so the descriptor table is refreshed on every
    //  `toolsListed` emission.
    //  MCP 工具是异步到达的，且服务器宣布变化时可能重新列举，因此每次
    //  `toolsListed` 信号都会刷新描述符表。
    connect(client, &McpClient::toolsListed, this,
            [this](const QVector<ToolDescriptor> &tools) {
                for (const ToolDescriptor &descriptor : tools) {
                    ToolDescriptor copy = descriptor;
                    copy.category = QStringLiteral("mcp");
                    //  Third-party code: always confirm, and assume network access.
                    //  第三方代码：始终需要确认，并按需要网络访问处理。
                    copy.mutatesProject = true;
                    copy.destructive = true;
                    copy.requiresNetwork = true;
                    if (!copy.titleEn.isEmpty())
                        copy.titleZh = copy.titleEn;

                    //  The handler is a placeholder: dispatch happens through
                    //  invokeMcpTool, which owns the namespace parsing.
                    //  此处处理函数为占位实现：真正的派发由 invokeMcpTool 完成，
                    //  命名空间解析也在那里。
                    registerTool(copy, [](const QJsonObject &, ToolContext &,
                                          QString *error) -> ToolResult {
                        if (error)
                            *error = QString();
                        return ToolResult();
                    });
                }
            });

    if (!client->tools().isEmpty()) {
        for (const ToolDescriptor &descriptor : client->tools()) {
            ToolDescriptor copy = descriptor;
            copy.category = QStringLiteral("mcp");
            copy.mutatesProject = true;
            copy.destructive = true;
            copy.requiresNetwork = true;
            registerTool(copy, [](const QJsonObject &, ToolContext &,
                                  QString *error) -> ToolResult {
                if (error)
                    *error = QString();
                return ToolResult();
            });
        }
    }
}

// ---------------------------------------------------------------------------
//  Discovery
//  发现
// ---------------------------------------------------------------------------
QVector<ToolDescriptor> AgentToolRegistry::descriptors() const
{
    QVector<ToolDescriptor> out;
    out.reserve(m_entries.size());
    for (const Entry &entry : m_entries)
        out.append(entry.descriptor);
    return out;
}

QVector<ToolDescriptor> AgentToolRegistry::descriptorsForModel() const
{
    QVector<ToolDescriptor> out;
    out.reserve(m_entries.size());
    for (const Entry &entry : m_entries) {
        ToolDescriptor copy = entry.descriptor;
        //  Models are prompted in English, so never ship Chinese-only text.
        //  模型提示词使用英文，因此绝不发送仅有中文的文本。
        if (copy.descriptionEn.trimmed().isEmpty())
            copy.descriptionEn = copy.descriptionZh;
        if (copy.titleEn.trimmed().isEmpty())
            copy.titleEn = copy.name;
        if (copy.inputSchema.isEmpty())
            copy.inputSchema = agenttemplates::schemaObject(QJsonObject());
        out.append(copy);
    }
    return out;
}

const ToolDescriptor *AgentToolRegistry::find(const QString &name) const
{
    const auto it = m_index.constFind(name);
    if (it == m_index.constEnd())
        return nullptr;
    const int index = it.value();
    if (index < 0 || index >= m_entries.size())
        return nullptr;
    return &m_entries.at(index).descriptor;
}

int AgentToolRegistry::toolCount() const
{
    return static_cast<int>(m_entries.size());
}

bool AgentToolRegistry::isMutating(const QString &toolName) const
{
    const ToolDescriptor *descriptor = find(toolName);
    return descriptor ? descriptor->mutatesProject : false;
}

bool AgentToolRegistry::isDestructive(const QString &toolName) const
{
    const ToolDescriptor *descriptor = find(toolName);
    return descriptor ? descriptor->destructive : false;
}

// ---------------------------------------------------------------------------
//  Policy
//  审批策略
// ---------------------------------------------------------------------------
void AgentToolRegistry::setAllowedTools(const QStringList &names)
{
    m_allowed = names;
}

void AgentToolRegistry::setBlockedTools(const QStringList &names)
{
    m_blocked = names;
}

bool AgentToolRegistry::isAutoApproved(const QString &toolName) const
{
    const ToolDescriptor *descriptor = find(toolName);
    if (!descriptor)
        return false;
    if (!descriptor->mutatesProject)
        return true;

    switch (m_policy) {
    case ApprovalPolicy::Never:
        return false;
    case ApprovalPolicy::SafeOnly:
        return !descriptor->destructive;
    case ApprovalPolicy::AskEveryChange:
        return false;
    case ApprovalPolicy::AskDestructive:
    default:
        //  Additive edits are cheap to undo; deletions are not.
        //  追加式编辑易于撤销；删除则不然。
        return !descriptor->destructive;
    }
}

// ---------------------------------------------------------------------------
//  Logging
//  日志
// ---------------------------------------------------------------------------
void AgentToolRegistry::logLine(const QString &line)
{
    //  Secrets can reach a tool name through an MCP server id or a model typo,
    //  so every logged line is redacted first.
    //  MCP 服务器 id 或模型拼写错误都可能让机密进入工具名，因此每行日志都先遮蔽。
    m_log.append(redactSecrets(line));
    while (m_log.size() > kMaxLogEntries)
        m_log.removeFirst();
}

void AgentToolRegistry::clearLog()
{
    m_log.clear();
}

// ---------------------------------------------------------------------------
//  invoke
//  调用工具
// ---------------------------------------------------------------------------
ToolResult AgentToolRegistry::invoke(const ToolCall &call, ToolContext &context)
{
    const QString name = call.name.trimmed();

    const auto denied = [this, &name](const QString &reasonEn, const QString &reasonZh) {
        const QString reason = bilingual(reasonEn, reasonZh);
        logLine(QStringLiteral("DENIED %1 -- %2").arg(name, reasonEn));
        emit toolDenied(name, reason);
        ToolResult result;
        result.isError = true;
        result.content = reason;
        return result;
    };

    const auto it = m_index.constFind(name);
    if (it == m_index.constEnd()) {
        const QString message =
            bilingual(QStringLiteral("Unknown tool \"%1\". Call only the tools you were given.")
                          .arg(name),
                      QStringLiteral("未知工具 \"%1\"。请只调用已提供的工具。").arg(name));
        logLine(QStringLiteral("UNKNOWN %1").arg(name));
        emit toolDenied(name, message);
        ToolResult result;
        result.isError = true;
        result.content = message;
        return result;
    }
    const int index = it.value();
    if (index < 0 || index >= m_entries.size())
        return errorResult(QStringLiteral("The tool registry is inconsistent."),
                           QStringLiteral("工具注册表状态不一致。"));
    const Entry &entry = m_entries.at(index);

    // --- MCP tools ---------------------------------------------------------
    // --- MCP 工具 ----------------------------------------------------------
    const QString mcpPrefix = QStringLiteral("mcp_");
    if (entry.descriptor.category == QStringLiteral("mcp") && name.startsWith(mcpPrefix)) {
        McpClient *owner = nullptr;
        QString serverId;
        QString serverToolName;
        for (auto clientIt = m_mcpClients.constBegin(); clientIt != m_mcpClients.constEnd();
             ++clientIt) {
            //  namespacedToolName("srv", "") yields "mcp_srv_"; stripping it
            //  recovers the server's own tool name.
            //  namespacedToolName("srv", "") 得到 "mcp_srv_"；去掉它即可恢复
            //  服务器自身的工具名。
            const QString serverPrefix = McpClient::namespacedToolName(clientIt.key(), QString());
            if (name.startsWith(serverPrefix)) {
                owner = clientIt.value();
                serverId = clientIt.key();
                serverToolName = name.mid(serverPrefix.size());
                break;
            }
        }
        if (!owner || serverToolName.isEmpty())
            return denied(QStringLiteral("No MCP client is attached for the tool \"%1\".").arg(name),
                          QStringLiteral("工具 \"%1\" 没有对应的 MCP 客户端。").arg(name));

        if (!owner->isToolAllowed(serverToolName))
            return denied(QStringLiteral("The MCP server blocks the tool \"%1\".").arg(name),
                          QStringLiteral("该 MCP 服务器禁止使用工具 \"%1\"。").arg(name));

        if (!isAutoApproved(name)) {
            if (!m_approval)
                return denied(
                    QStringLiteral("The MCP tool \"%1\" needs confirmation, but no confirmation "
                                   "handler is available, so it was refused.").arg(name),
                    QStringLiteral("MCP 工具 \"%1\" 需要确认，但没有可用的确认处理函数，因此"
                                   "已被拒绝。").arg(name));
            if (!m_approval(call, entry.descriptor))
                return denied(QStringLiteral("The user declined to run \"%1\".").arg(name),
                              QStringLiteral("用户拒绝运行 \"%1\"。").arg(name));
        }

        ToolResult result = invokeMcpTool(call, serverId, serverToolName);
        logLine(QStringLiteral("%1 %2 -> %3")
                    .arg(result.isError ? QStringLiteral("FAIL") : QStringLiteral("OK"), name,
                         result.isError ? QStringLiteral("error") : QStringLiteral("ok")));
        emit toolInvoked(name, !result.isError);
        return result;
    }

    // --- allow / deny lists ------------------------------------------------
    // --- 允许 / 拒绝列表 ---------------------------------------------------
    if (m_blocked.contains(name))
        return denied(QStringLiteral("The tool \"%1\" is blocked by the user's deny list.")
                          .arg(name),
                      QStringLiteral("工具 \"%1\" 已被用户的拒绝列表阻止。").arg(name));
    if (!m_allowed.isEmpty() && !m_allowed.contains(name))
        return denied(QStringLiteral("The tool \"%1\" is not in the user's allow list.")
                          .arg(name),
                      QStringLiteral("工具 \"%1\" 不在用户的允许列表中。").arg(name));

    // --- approval policy ---------------------------------------------------
    // --- 审批策略 ----------------------------------------------------------
    const bool mutating = entry.descriptor.mutatesProject;

    if (mutating && m_policy == ApprovalPolicy::Never)
        return denied(QStringLiteral("The approval policy forbids every change, so \"%1\" "
                                     "(which modifies the project) was refused.").arg(name),
                      QStringLiteral("审批策略禁止一切变更，因此修改工程的 \"%1\" 已被拒绝。")
                          .arg(name));

    if (mutating && !isAutoApproved(name)) {
        if (!m_approval)
            return denied(
                QStringLiteral("The tool \"%1\" changes the project and needs the user's "
                               "confirmation, but no confirmation handler is connected, so it "
                               "was refused.").arg(name),
                QStringLiteral("工具 \"%1\" 会修改工程并需要用户确认，但没有连接确认处理函数，"
                               "因此已被拒绝。").arg(name));
        if (!m_approval(call, entry.descriptor))
            return denied(QStringLiteral("The user declined to run \"%1\".").arg(name),
                          QStringLiteral("用户拒绝运行 \"%1\"。").arg(name));
    }

    // --- execute -----------------------------------------------------------
    // --- 执行 --------------------------------------------------------------
    ToolResult result;
    QString handlerError;
    if (entry.handler)
        result = entry.handler(call.arguments, context, &handlerError);
    else
        result = errorResult(QStringLiteral("The tool \"%1\" has no handler.").arg(name),
                             QStringLiteral("工具 \"%1\" 没有处理函数。").arg(name));

    if (!handlerError.isEmpty()) {
        result.isError = true;
        if (result.content.isEmpty())
            result.content = handlerError;
    }
    if (result.content.isEmpty())
        result = errorResult(QStringLiteral("The tool \"%1\" produced no result.").arg(name),
                             QStringLiteral("工具 \"%1\" 未产生任何结果。").arg(name));
    if (result.toolCallId.isEmpty())
        result.toolCallId = call.id;

    logLine(QStringLiteral("%1 %2 -> %3")
                .arg(result.isError ? QStringLiteral("FAIL") : QStringLiteral("OK"), name,
                     result.isError ? QStringLiteral("error") : QStringLiteral("ok")));
    emit toolInvoked(name, !result.isError);
    return result;
}

// ---------------------------------------------------------------------------
//  invokeMcpTool
//  转发到 MCP 客户端
// ---------------------------------------------------------------------------
ToolResult AgentToolRegistry::invokeMcpTool(const ToolCall &call, const QString &serverId,
                                            const QString &toolName)
{
    //  The namespaced name is parsed as a defence in depth: the caller already
    //  resolved the server, and this re-derivation must agree with it.
    //  带命名空间的名字会被再次解析，作为纵深防御：调用方已解析出服务器，
    //  此处重新推导的结果必须与其一致。
    McpClient *client = m_mcpClients.value(serverId, nullptr);
    QString stripped = toolName;
    if (client) {
        const QString serverPrefix = McpClient::namespacedToolName(serverId, QString());
        if (toolName.startsWith(serverPrefix))
            stripped = toolName.mid(serverPrefix.size());
    }

    ToolResult result;
    result.toolCallId = call.id;

    if (!client) {
        result.isError = true;
        result.content = bilingual(
            QStringLiteral("The MCP server \"%1\" is not attached to this registry.").arg(serverId),
            QStringLiteral("MCP 服务器 \"%1\" 未挂接到此注册表。").arg(serverId));
        return result;
    }
    if (stripped.isEmpty()) {
        result.isError = true;
        result.content = bilingual(
            QStringLiteral("The MCP tool name \"%1\" carries no server tool name.")
                .arg(toolName),
            QStringLiteral("MCP 工具名 \"%1\" 中不包含服务器工具名。").arg(toolName));
        return result;
    }

    const QString requestId = call.id.isEmpty() ? IdGenerator::randomToken(8) : call.id;

    //  A second allow/deny check here guards direct calls that bypass `invoke`.
    //  此处再次检查允许/拒绝列表，以防绕过 `invoke` 的直接调用。
    if (!client->isToolAllowed(stripped)) {
        result.isError = true;
        result.content = bilingual(
            QStringLiteral("The MCP server \"%1\" blocks the tool \"%2\".")
                .arg(serverId, stripped),
            QStringLiteral("MCP 服务器 \"%1\" 禁止使用工具 \"%2\"。").arg(serverId, stripped));
        return result;
    }

    client->callTool(stripped, call.arguments, requestId);

    //  The JSON-RPC reply is asynchronous and is delivered through
    //  `McpClient::toolResult`; the agent loop consumes that signal directly.
    //  Reporting "dispatched" is honest and avoids blocking the UI thread.
    //  JSON-RPC 回复是异步的，通过 `McpClient::toolResult` 送达；智能体循环直接
    //  消费该信号。返回"已派发"是诚实的，并可避免阻塞界面线程。
    result.isError = false;
    result.content = compact(QJsonObject{
        { QStringLiteral("status"), QStringLiteral("dispatched") },
        { QStringLiteral("server"), serverId },
        { QStringLiteral("tool"), stripped },
        { QStringLiteral("requestId"), requestId },
        { QStringLiteral("note"),
          QStringLiteral("the JSON-RPC reply arrives asynchronously through "
                         "McpClient::toolResult") }
    });
    return result;
}

// ===========================================================================
//  registerBuiltinTools -- 32 declarative capabilities
//  registerBuiltinTools —— 32 项声明式能力
//
//  Grouping / 分组：
//    project (6)  page (4)  element (13)  style (1)  blocks (5)
//    code (2)     backend (3)  ai (1)     files (1)
//
//  Flags / 标志：
//    deletes                     -> destructive = true, mutatesProject = true
//    any document edit           -> mutatesProject = true
//    nothing here needs network  -> requiresNetwork = false everywhere
// ===========================================================================
void AgentToolRegistry::registerBuiltinTools()
{
    using namespace agenttemplates;

    //  Registers one tool from a compact option string. The option string is a
    //  fixed, internal literal, so a malformed token is a programming error
    //  rather than untrusted input.
    //  通过紧凑的选项字符串注册一个工具。该字符串是固定的内部字面量，因此格式
    //  错误的片段属于编码错误，而非不可信输入。
    const auto add = [this](const QString &name, const QString &category,
                            const QString &options, const QJsonObject &schema,
                            const QString &titleEn, const QString &titleZh,
                            const QString &descriptionEn, const QString &descriptionZh,
                            Handler handler) {
        ToolDescriptor descriptor;
        descriptor.name = name;
        descriptor.titleEn = titleEn;
        descriptor.titleZh = titleZh;
        descriptor.descriptionEn = descriptionEn;
        descriptor.descriptionZh = descriptionZh;
        descriptor.inputSchema = schema;
        descriptor.category = category;
        descriptor.mutatesProject = options.contains(QStringLiteral("mutates"));
        descriptor.destructive = options.contains(QStringLiteral("destructive"));
        descriptor.requiresNetwork = false;
        registerTool(descriptor, std::move(handler));
    };

    // =======================================================================
    //  project
    // =======================================================================

    add(QStringLiteral("list_pages"), QStringLiteral("project"), QString(),
        schemaObject(QJsonObject()),
        QStringLiteral("List pages"), QStringLiteral("列出页面"),
        QStringLiteral("List every page of the open project with its id, name, title, file name "
                       "and element count. Call this first whenever a page id is needed."),
        QStringLiteral("列出当前工程的每个页面及其 id、名称、标题、文件名与元素数量。"
                       "任何需要页面 id 的操作都应先调用它。"),
        [](const QJsonObject &, ToolContext &context, QString *error) -> ToolResult {
            if (!context.project) {
                if (error)
                    *error = bilingual(QStringLiteral("No project is open."),
                                       QStringLiteral("当前没有打开工程。"));
                return ToolResult();
            }
            QJsonArray pages;
            for (int i = 0; i < context.project->pageCount(); ++i) {
                Page *page = context.project->pageAt(i);
                if (!page)
                    continue;
                QJsonObject entry;
                entry.insert(QStringLiteral("id"), page->id);
                entry.insert(QStringLiteral("name"), page->name);
                entry.insert(QStringLiteral("title"), page->title);
                entry.insert(QStringLiteral("fileName"), page->fileName);
                entry.insert(QStringLiteral("isHome"), page->isHome);
                entry.insert(QStringLiteral("elementCount"), page->nodeCount());
                pages.append(entry);
            }
            return okResult(QJsonObject{
                { QStringLiteral("count"), context.project->pageCount() },
                { QStringLiteral("activePageId"),
                  context.project->activePageId.isEmpty() && context.project->pageCount() > 0
                      ? context.project->pageAt(0)->id
                      : context.project->activePageId },
                { QStringLiteral("pages"), pages }
            });
        });

    add(QStringLiteral("get_page_outline"), QStringLiteral("project"), QString(),
        schemaObject(QJsonObject{
                         { QStringLiteral("pageId"),
                           propString(QStringLiteral("Page id from list_pages. Omit to use the "
                                                     "active page.")) },
                         { QStringLiteral("maxDepth"),
                           propNumber(QStringLiteral("Deepest level to include. Default 4."), 1,
                                      12) },
                         { QStringLiteral("limit"),
                           propNumber(QStringLiteral("Maximum entries returned. Default 80, "
                                                     "hard cap 200."), 1, 200) }
                     }),
        QStringLiteral("Get page outline"), QStringLiteral("获取页面纲要"),
        QStringLiteral("Return a compact outline (id, tag, 40-character text preview, depth) of a "
                       "page's element tree. Use it to find element ids before editing."),
        QStringLiteral("返回页面元素树的紧凑纲要（id、标签、40 字符文本预览、深度）。"
                       "编辑前用它来查找元素 id。"),
        [](const QJsonObject &arguments, ToolContext &context, QString *error) -> ToolResult {
            Page *page = resolvePage(context, arguments.value(QStringLiteral("pageId")).toString(),
                                     error, nullptr);
            if (!page)
                return ToolResult();
            const int maxDepth = arguments.contains(QStringLiteral("maxDepth"))
                                     ? std::clamp(arguments.value(QStringLiteral("maxDepth")).toInt(), 1, 12)
                                     : 4;
            const int limit = arguments.contains(QStringLiteral("limit"))
                                  ? std::clamp(arguments.value(QStringLiteral("limit")).toInt(), 1,
                                               kMaxOutlineNodes)
                                  : 80;
            QJsonArray outline;
            if (page->root)
                collectOutline(page->root.get(), 0, maxDepth, limit, &outline);
            return okResult(QJsonObject{
                { QStringLiteral("pageId"), page->id },
                { QStringLiteral("name"), page->name },
                { QStringLiteral("totalNodes"), page->nodeCount() },
                { QStringLiteral("returned"), outline.size() },
                { QStringLiteral("outline"), outline }
            });
        });

    add(QStringLiteral("create_page"), QStringLiteral("project"), QStringLiteral("mutates"),
        schemaObject(QJsonObject{
                         { QStringLiteral("name"),
                           propString(QStringLiteral("Short page name used for the tab label and "
                                                     "the file name, for example \"about\".")) },
                         { QStringLiteral("title"),
                           propString(QStringLiteral("Value for the <title> element. Defaults to "
                                                     "the name.")) },
                         { QStringLiteral("home"),
                           propBoolean(QStringLiteral("Make this page the site home page "
                                                      "(index.html). Default false.")) },
                         { QStringLiteral("language"),
                           propString(QStringLiteral("BCP-47 language code, for example \"en\" or "
                                                     "\"zh-CN\".")) }
                     },
                     { QStringLiteral("name") }),
        QStringLiteral("Create page"), QStringLiteral("新建页面"),
        QStringLiteral("Create a new page with a fresh id. Pass a short snake_case name. Returns "
                       "the new page id."),
        QStringLiteral("创建一个带新 id 的页面。请传入简短的名称。返回新页面 id。"),
        [](const QJsonObject &arguments, ToolContext &context, QString *error) -> ToolResult {
            if (!context.project) {
                if (error)
                    *error = bilingual(QStringLiteral("No project is open."),
                                       QStringLiteral("当前没有打开工程。"));
                return ToolResult();
            }
            QString name = arguments.value(QStringLiteral("name")).toString().trimmed();
            if (name.isEmpty())
                name = QStringLiteral("page");

            std::unique_ptr<Page> fresh = Page::makeDefault(name, context.chinese);
            if (!fresh) {
                if (error)
                    *error = bilingual(QStringLiteral("The page could not be created."),
                                       QStringLiteral("无法创建页面。"));
                return ToolResult();
            }

            //  Guarantee a unique name and file name.
            //  保证名称与文件名唯一。
            QString candidate = name;
            int suffix = 2;
            while (context.project->pageByName(candidate))
                candidate = QStringLiteral("%1-%2").arg(name).arg(suffix++);
            fresh->name = candidate;

            const QString requestedTitle = arguments.value(QStringLiteral("title")).toString();
            fresh->title = requestedTitle.isEmpty() ? candidate : requestedTitle;
            if (arguments.contains(QStringLiteral("language"))
                && !arguments.value(QStringLiteral("language")).toString().trimmed().isEmpty())
                fresh->language = arguments.value(QStringLiteral("language")).toString().trimmed();

            QString fileName = candidate;
            const QString base = fileName;
            int fileSuffix = 2;
            const auto fileTaken = [&context](const QString &value) {
                for (int i = 0; i < context.project->pageCount(); ++i) {
                    Page *page = context.project->pageAt(i);
                    if (page && page->fileName.compare(value, Qt::CaseInsensitive) == 0)
                        return true;
                }
                return false;
            };
            while (fileTaken(fileName + QStringLiteral(".html")))
                fileName = QStringLiteral("%1-%2").arg(base).arg(fileSuffix++);
            fresh->fileName = fileName + QStringLiteral(".html");

            if (arguments.value(QStringLiteral("home")).toBool(false)) {
                for (int i = 0; i < context.project->pageCount(); ++i) {
                    Page *page = context.project->pageAt(i);
                    if (page)
                        page->isHome = false;
                }
                fresh->isHome = true;
            }

            const QString newId = fresh->id;
            Page *added = context.project->addPage(std::move(fresh));
            if (!added) {
                if (error)
                    *error = bilingual(QStringLiteral("The document refused the new page."),
                                       QStringLiteral("文档拒绝了新页面。"));
                return ToolResult();
            }
            context.project->touch();
            return okResult(QJsonObject{
                { QStringLiteral("pageId"), added->id },
                { QStringLiteral("name"), added->name },
                { QStringLiteral("fileName"), added->fileName },
                { QStringLiteral("isHome"), added->isHome },
                { QStringLiteral("created"), !newId.isEmpty() }
            });
        });

    add(QStringLiteral("delete_page"), QStringLiteral("project"),
        QStringLiteral("mutates destructive"),
        schemaObject(QJsonObject{
                         { QStringLiteral("pageId"),
                           propString(QStringLiteral("Id of the page to delete, from "
                                                     "list_pages.")) }
                     },
                     { QStringLiteral("pageId") }),
        QStringLiteral("Delete page"), QStringLiteral("删除页面"),
        QStringLiteral("Permanently delete one page and every element on it. The last remaining "
                       "page cannot be deleted."),
        QStringLiteral("永久删除一个页面及其所有元素。最后一个页面无法删除。"),
        [](const QJsonObject &arguments, ToolContext &context, QString *error) -> ToolResult {
            if (!context.project) {
                if (error)
                    *error = bilingual(QStringLiteral("No project is open."),
                                       QStringLiteral("当前没有打开工程。"));
                return ToolResult();
            }
            const QString pageId = arguments.value(QStringLiteral("pageId")).toString().trimmed();
            if (pageId.isEmpty()) {
                if (error)
                    *error = bilingual(QStringLiteral("\"pageId\" is required."),
                                       QStringLiteral("\"pageId\" 为必填项。"));
                return ToolResult();
            }
            Page *page = context.project->pageById(pageId);
            if (!page) {
                if (error)
                    *error = bilingual(QStringLiteral("No page has the id \"%1\".").arg(pageId),
                                       QStringLiteral("没有 id 为 \"%1\" 的页面。").arg(pageId));
                return ToolResult();
            }
            if (context.project->pageCount() <= 1) {
                if (error)
                    *error = bilingual(
                        QStringLiteral("The only page of a project cannot be deleted."),
                        QStringLiteral("工程中唯一的页面无法删除。"));
                return ToolResult();
            }
            const int index = context.project->indexOfPage(page);
            const QString name = page->name;
            if (!context.project->removePage(index)) {
                if (error)
                    *error = bilingual(QStringLiteral("The page could not be removed."),
                                       QStringLiteral("无法移除该页面。"));
                return ToolResult();
            }
            if (context.project->activePageId == pageId && context.project->pageCount() > 0)
                context.project->activePageId = context.project->pageAt(0)->id;
            if (context.activePageId == pageId)
                context.activePageId = context.project->activePageId;
            context.project->touch();
            return okResult(QJsonObject{
                { QStringLiteral("deleted"), pageId },
                { QStringLiteral("name"), name },
                { QStringLiteral("remaining"), context.project->pageCount() }
            });
        });

    add(QStringLiteral("rename_page"), QStringLiteral("project"), QStringLiteral("mutates"),
        schemaObject(QJsonObject{
                         { QStringLiteral("pageId"),
                           propString(QStringLiteral("Id of the page to rename.")) },
                         { QStringLiteral("name"),
                           propString(QStringLiteral("New short name used for the tab label.")) },
                         { QStringLiteral("title"),
                           propString(QStringLiteral("New <title> text.")) }
                     },
                     { QStringLiteral("pageId") }),
        QStringLiteral("Rename page"), QStringLiteral("重命名页面"),
        QStringLiteral("Change a page's short name, its <title>, or both. At least one of "
                       "\"name\" and \"title\" is required."),
        QStringLiteral("修改页面的短名称、<title> 或两者。\"name\" 与 \"title\" 至少需要"
                       "提供一个。"),
        [](const QJsonObject &arguments, ToolContext &context, QString *error) -> ToolResult {
            Page *page = resolvePage(context, arguments.value(QStringLiteral("pageId")).toString(),
                                     error, nullptr);
            if (!page)
                return ToolResult();
            const QString name = arguments.value(QStringLiteral("name")).toString().trimmed();
            const QString title = arguments.value(QStringLiteral("title")).toString().trimmed();
            if (name.isEmpty() && title.isEmpty()) {
                if (error)
                    *error = bilingual(QStringLiteral("Pass \"name\", \"title\", or both."),
                                       QStringLiteral("请传入 \"name\"、\"title\" 或两者。"));
                return ToolResult();
            }
            const QString previousName = page->name;
            const QString previousTitle = page->title;
            if (!name.isEmpty()) {
                //  A duplicate name would make pageByName ambiguous.
                //  重名会使 pageByName 产生歧义。
                Page *clash = context.project ? context.project->pageByName(name) : nullptr;
                if (clash && clash != page) {
                    if (error)
                        *error = bilingual(QStringLiteral("Another page is already named \"%1\".")
                                               .arg(name),
                                           QStringLiteral("已有另一个页面名为 \"%1\"。").arg(name));
                    return ToolResult();
                }
                page->name = name;
            }
            if (!title.isEmpty())
                page->title = title;
            if (context.project)
                context.project->touch();
            return okResult(QJsonObject{
                { QStringLiteral("pageId"), page->id },
                { QStringLiteral("previousName"), previousName },
                { QStringLiteral("name"), page->name },
                { QStringLiteral("previousTitle"), previousTitle },
                { QStringLiteral("title"), page->title }
            });
        });

    add(QStringLiteral("set_page_meta"), QStringLiteral("project"), QStringLiteral("mutates"),
        schemaObject(QJsonObject{
                         { QStringLiteral("pageId"),
                           propString(QStringLiteral("Page id. Omit for the active page.")) },
                         { QStringLiteral("fileName"),
                           propString(QStringLiteral("Emitted file name, for example "
                                                     "\"about.html\".")) },
                         { QStringLiteral("description"),
                           propString(QStringLiteral("Meta description.")) },
                         { QStringLiteral("language"),
                           propString(QStringLiteral("BCP-47 language code.")) },
                         { QStringLiteral("isHome"),
                           propBoolean(QStringLiteral("Make this the home page; clears the flag "
                                                      "on every other page.")) },
                         { QStringLiteral("keywords"),
                           propArray(QStringLiteral("Meta keywords."),
                                     propString(QStringLiteral("One keyword."))) }
                     }),
        QStringLiteral("Set page metadata"), QStringLiteral("设置页面元数据"),
        QStringLiteral("Update a page's file name, <meta> description, language, keywords or "
                       "home-page flag. Only the fields you pass are changed."),
        QStringLiteral("更新页面的文件名、<meta> 描述、语言、关键词或首页标记。"
                       "仅会修改你传入的字段。"),
        [](const QJsonObject &arguments, ToolContext &context, QString *error) -> ToolResult {
            Page *page = resolvePage(context, arguments.value(QStringLiteral("pageId")).toString(),
                                     error, nullptr);
            if (!page)
                return ToolResult();
            QJsonArray changed;
            const auto mark = [&changed](const QString &field) { changed.append(field); };

            if (arguments.contains(QStringLiteral("fileName"))) {
                QString fileName = arguments.value(QStringLiteral("fileName")).toString().trimmed();
                if (fileName.isEmpty()) {
                    if (error)
                        *error = bilingual(QStringLiteral("\"fileName\" cannot be empty."),
                                           QStringLiteral("\"fileName\" 不能为空。"));
                    return ToolResult();
                }
                //  Refuse path separators: the name must stay a single file.
                //  拒绝路径分隔符：文件名必须保持为单个文件。
                if (fileName.contains(QLatin1Char('/')) || fileName.contains(QLatin1Char('\\'))
                    || fileName.contains(QStringLiteral(".."))) {
                    if (error)
                        *error = bilingual(
                            QStringLiteral("\"fileName\" must be a plain file name without any "
                                           "directory part."),
                            QStringLiteral("\"fileName\" 必须是不含任何目录部分的纯文件名。"));
                    return ToolResult();
                }
                page->fileName = fileName;
                mark(QStringLiteral("fileName"));
            }
            if (arguments.contains(QStringLiteral("description"))) {
                page->description = arguments.value(QStringLiteral("description")).toString();
                mark(QStringLiteral("description"));
            }
            if (arguments.contains(QStringLiteral("language"))) {
                page->language = arguments.value(QStringLiteral("language")).toString().trimmed();
                mark(QStringLiteral("language"));
            }
            if (arguments.contains(QStringLiteral("isHome"))) {
                const bool home = arguments.value(QStringLiteral("isHome")).toBool();
                if (home && context.project) {
                    for (int i = 0; i < context.project->pageCount(); ++i) {
                        Page *other = context.project->pageAt(i);
                        if (other)
                            other->isHome = false;
                    }
                }
                page->isHome = home;
                mark(QStringLiteral("isHome"));
            }
            if (arguments.contains(QStringLiteral("keywords"))) {
                page->keywords.clear();
                for (const QJsonValue &value : arguments.value(QStringLiteral("keywords")).toArray())
                    page->keywords.append(value.toString());
                mark(QStringLiteral("keywords"));
            }
            if (context.project)
                context.project->touch();
            return okResult(QJsonObject{
                { QStringLiteral("pageId"), page->id },
                { QStringLiteral("changed"), changed }
            });
        });

    add(QStringLiteral("set_project_meta"), QStringLiteral("project"), QStringLiteral("mutates"),
        schemaObject(QJsonObject{
                         { QStringLiteral("name"),
                           propString(QStringLiteral("Project name.")) },
                         { QStringLiteral("description"),
                           propString(QStringLiteral("Project description.")) },
                         { QStringLiteral("author"),
                           propString(QStringLiteral("Author name.")) },
                         { QStringLiteral("defaultLanguage"),
                           propString(QStringLiteral("Default BCP-47 language for new pages.")) }
                     }),
        QStringLiteral("Set project metadata"), QStringLiteral("设置工程元数据"),
        QStringLiteral("Update the project name, description, author or default language. Only "
                       "the fields you pass are changed."),
        QStringLiteral("更新工程名称、描述、作者或默认语言。仅会修改你传入的字段。"),
        [](const QJsonObject &arguments, ToolContext &context, QString *error) -> ToolResult {
            if (!context.project) {
                if (error)
                    *error = bilingual(QStringLiteral("No project is open."),
                                       QStringLiteral("当前没有打开工程。"));
                return ToolResult();
            }
            QJsonArray changed;
            if (arguments.contains(QStringLiteral("name"))) {
                context.project->name = arguments.value(QStringLiteral("name")).toString();
                changed.append(QStringLiteral("name"));
            }
            if (arguments.contains(QStringLiteral("description"))) {
                context.project->description =
                    arguments.value(QStringLiteral("description")).toString();
                changed.append(QStringLiteral("description"));
            }
            if (arguments.contains(QStringLiteral("author"))) {
                context.project->author = arguments.value(QStringLiteral("author")).toString();
                changed.append(QStringLiteral("author"));
            }
            if (arguments.contains(QStringLiteral("defaultLanguage"))) {
                context.project->defaultLanguage =
                    arguments.value(QStringLiteral("defaultLanguage")).toString().trimmed();
                changed.append(QStringLiteral("defaultLanguage"));
            }
            if (changed.isEmpty()) {
                if (error)
                    *error = bilingual(QStringLiteral("Pass at least one field to change."),
                                       QStringLiteral("请至少传入一个要修改的字段。"));
                return ToolResult();
            }
            context.project->touch();
            return okResult(QJsonObject{
                { QStringLiteral("projectId"), context.project->id },
                { QStringLiteral("name"), context.project->name },
                { QStringLiteral("changed"), changed }
            });
        });

    add(QStringLiteral("get_project_summary"), QStringLiteral("project"), QString(),
        schemaObject(QJsonObject()),
        QStringLiteral("Summarise project"), QStringLiteral("汇总工程"),
        QStringLiteral("Return a cheap overview of the project: name, page count and total "
                       "element count, the block script count, and the configured endpoint "
                       "count. Use it before planning a larger change."),
        QStringLiteral("返回工程的轻量概览：名称、页面数与元素总数、积木脚本数量、"
                       "已配置的端点数。规划较大改动前可先调用它。"),
        [](const QJsonObject &, ToolContext &context, QString *error) -> ToolResult {
            if (!context.project) {
                if (error)
                    *error = bilingual(QStringLiteral("No project is open."),
                                       QStringLiteral("当前没有打开工程。"));
                return ToolResult();
            }
            int totalNodes = 0;
            QJsonArray pages;
            for (int i = 0; i < context.project->pageCount(); ++i) {
                Page *page = context.project->pageAt(i);
                if (!page)
                    continue;
                totalNodes += page->nodeCount();
                QJsonObject entry;
                entry.insert(QStringLiteral("id"), page->id);
                entry.insert(QStringLiteral("name"), page->name);
                entry.insert(QStringLiteral("elements"), page->nodeCount());
                pages.append(entry);
            }
            return okResult(QJsonObject{
                { QStringLiteral("projectId"), context.project->id },
                { QStringLiteral("name"), context.project->name },
                { QStringLiteral("description"), context.project->description },
                { QStringLiteral("pageCount"), context.project->pageCount() },
                { QStringLiteral("totalElements"), totalNodes },
                { QStringLiteral("scriptCount"),
                  static_cast<int>(context.project->program().size()) },
                { QStringLiteral("endpointCount"), endpointsOf(context.project).size() },
                { QStringLiteral("activePageId"), context.project->activePageId },
                { QStringLiteral("pages"), pages }
            });
        });

    // =======================================================================
    //  element
    // =======================================================================

    add(QStringLiteral("get_element"), QStringLiteral("element"), QString(),
        schemaObject(QJsonObject{
                         { QStringLiteral("elementId"),
                           propString(QStringLiteral("Element id from get_page_outline. Omit to "
                                                     "use the selected element.")) },
                         { QStringLiteral("pageId"),
                           propString(QStringLiteral("Page id. Omit for the active page.")) }
                     }),
        QStringLiteral("Get element"), QStringLiteral("获取元素"),
        QStringLiteral("Return one element in full: tag, attributes, CSS classes, resolved "
                       "styles per breakpoint, text and parent id."),
        QStringLiteral("完整返回一个元素：标签、属性、CSS 类、各断点的解析样式、文本与父 id。"),
        [](const QJsonObject &arguments, ToolContext &context, QString *error) -> ToolResult {
            Page *page = resolvePage(context, arguments.value(QStringLiteral("pageId")).toString(),
                                     error, nullptr);
            if (!page)
                return ToolResult();
            DocumentNode *node =
                resolveNode(page, context, arguments.value(QStringLiteral("elementId")).toString(),
                            error, nullptr);
            if (!node)
                return ToolResult();
            QJsonObject payload = nodeDetail(node);
            payload.insert(QStringLiteral("pageId"), page->id);
            return okResult(payload);
        });

    add(QStringLiteral("find_elements"), QStringLiteral("element"), QString(),
        schemaObject(QJsonObject{
                         { QStringLiteral("pageId"),
                           propString(QStringLiteral("Page id. Omit for the active page.")) },
                         { QStringLiteral("tag"),
                           propString(QStringLiteral("Match this HTML tag, for example "
                                                     "\"button\".")) },
                         { QStringLiteral("text"),
                           propString(QStringLiteral("Case-insensitive substring of the element "
                                                     "text.")) },
                         { QStringLiteral("className"),
                           propString(QStringLiteral("Exact CSS class the element carries.")) },
                         { QStringLiteral("attributeName"),
                           propString(QStringLiteral("Attribute that must be present.")) },
                         { QStringLiteral("attributeValue"),
                           propString(QStringLiteral("Exact value of \"attributeName\".")) },
                         { QStringLiteral("limit"),
                           propNumber(QStringLiteral("Maximum matches. Default 20, cap 50."), 1, 50) }
                     }),
        QStringLiteral("Find elements"), QStringLiteral("查找元素"),
        QStringLiteral("Search a page's element tree by tag, text, CSS class or attribute. All "
                       "supplied filters must match. Returns compact entries with element ids."),
        QStringLiteral("按标签、文本、CSS 类或属性搜索页面的元素树。所有提供的条件都必须"
                       "同时满足。返回带元素 id 的紧凑条目。"),
        [](const QJsonObject &arguments, ToolContext &context, QString *error) -> ToolResult {
            Page *page = resolvePage(context, arguments.value(QStringLiteral("pageId")).toString(),
                                     error, nullptr);
            if (!page || !page->root)
                return ToolResult();

            const QString tag = arguments.value(QStringLiteral("tag")).toString().trimmed().toLower();
            const QString text = arguments.value(QStringLiteral("text")).toString().trimmed();
            const QString className = arguments.value(QStringLiteral("className")).toString().trimmed();
            const QString attributeName =
                arguments.value(QStringLiteral("attributeName")).toString().trimmed();
            const QString attributeValue =
                arguments.value(QStringLiteral("attributeValue")).toString();

            if (tag.isEmpty() && text.isEmpty() && className.isEmpty() && attributeName.isEmpty()) {
                if (error)
                    *error = bilingual(
                        QStringLiteral("Pass at least one filter: tag, text, className or "
                                       "attributeName."),
                        QStringLiteral("请至少传入一个条件：tag、text、className 或 "
                                       "attributeName。"));
                return ToolResult();
            }

            const int limit = arguments.contains(QStringLiteral("limit"))
                                  ? std::clamp(arguments.value(QStringLiteral("limit")).toInt(), 1,
                                               kMaxSearchResults)
                                  : 20;

            QJsonArray matches;
            int total = 0;
            page->root->forEach([&](DocumentNode *node) {
                if (!node)
                    return;
                if (!tag.isEmpty() && node->tag.compare(tag, Qt::CaseInsensitive) != 0)
                    return;
                if (!text.isEmpty()
                    && !node->text.contains(text, Qt::CaseInsensitive))
                    return;
                if (!className.isEmpty() && !node->classes.contains(className))
                    return;
                if (!attributeName.isEmpty() && !node->hasAttribute(attributeName))
                    return;
                if (!attributeValue.isEmpty()
                    && node->attribute(attributeName) != attributeValue)
                    return;
                ++total;
                if (matches.size() < limit)
                    matches.append(nodeSummary(node, node->depth()));
            });

            return okResult(QJsonObject{
                { QStringLiteral("pageId"), page->id },
                { QStringLiteral("totalMatches"), total },
                { QStringLiteral("returned"), matches.size() },
                { QStringLiteral("matches"), matches }
            });
        });

    add(QStringLiteral("add_element"), QStringLiteral("element"), QStringLiteral("mutates"),
        schemaObject(QJsonObject{
                         { QStringLiteral("tag"),
                           propString(QStringLiteral("HTML tag to create, for example \"div\", "
                                                     "\"h2\", \"button\", \"img\".")) },
                         { QStringLiteral("pageId"),
                           propString(QStringLiteral("Page id. Omit for the active page.")) },
                         { QStringLiteral("parentId"),
                           propString(QStringLiteral("Id of the parent element. Omit to append to "
                                                     "<body>.")) },
                         { QStringLiteral("index"),
                           propNumber(QStringLiteral("Insert position among the parent's children. "
                                                     "Omit to append."), 0, 100000) },
                         { QStringLiteral("text"),
                           propString(QStringLiteral("Text content. Ignored for void elements.")) },
                         { QStringLiteral("html"),
                           propBoolean(QStringLiteral("Treat \"text\" as raw HTML. Default "
                                                      "false.")) },
                         { QStringLiteral("attributes"),
                           propObject(QStringLiteral("Attribute name to value map, for example "
                                                     "{\"class\":\"hero\"}.")) },
                         { QStringLiteral("classes"),
                           propArray(QStringLiteral("CSS classes to add."),
                                     propString(QStringLiteral("One class name."))) },
                         { QStringLiteral("styles"),
                           propObject(QStringLiteral("Inline CSS declarations, for example "
                                                     "{\"display\":\"flex\"}.")) }
                     },
                     { QStringLiteral("tag") }),
        QStringLiteral("Add element"), QStringLiteral("添加元素"),
        QStringLiteral("Create an element from a tag, apply optional text, attributes, classes and "
                       "styles, and insert it. Returns the new element id."),
        QStringLiteral("根据标签创建元素，应用可选的文本、属性、类与样式，并插入到页面中。"
                       "返回新元素 id。"),
        [](const QJsonObject &arguments, ToolContext &context, QString *error) -> ToolResult {
            Page *page = resolvePage(context, arguments.value(QStringLiteral("pageId")).toString(),
                                     error, nullptr);
            if (!page || !page->root)
                return ToolResult();

            const QString tag = arguments.value(QStringLiteral("tag")).toString().trimmed().toLower();
            if (tag.isEmpty() || !isSafeTagName(tag)) {
                if (error)
                    *error = bilingual(QStringLiteral("\"%1\" is not a safe HTML tag name.").arg(tag),
                                       QStringLiteral("\"%1\" 不是安全的 HTML 标签名。").arg(tag));
                return ToolResult();
            }

            std::unique_ptr<DocumentNode> created = ElementCatalog::instance().create(tag,
                                                                                      context.chinese);
            if (!created)
                created = DocumentNode::makeElement(tag);
            if (!created) {
                if (error)
                    *error = bilingual(QStringLiteral("The element could not be created."),
                                       QStringLiteral("无法创建该元素。"));
                return ToolResult();
            }

            DocumentNode *parent = page->root.get();
            if (arguments.contains(QStringLiteral("parentId"))
                && !arguments.value(QStringLiteral("parentId")).toString().trimmed().isEmpty()) {
                parent = resolveNode(page, context,
                                     arguments.value(QStringLiteral("parentId")).toString(),
                                     error, nullptr);
                if (!parent)
                    return ToolResult();
            }

            if (arguments.contains(QStringLiteral("text")) && !created->isVoidElement()) {
                created->text = arguments.value(QStringLiteral("text")).toString();
                created->textIsHtml = arguments.value(QStringLiteral("html")).toBool(false);
            }
            if (arguments.contains(QStringLiteral("attributes"))) {
                const QJsonObject values = arguments.value(QStringLiteral("attributes")).toObject();
                for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
                    if (!isSafeAttributeName(it.key()))
                        continue;   // silently skip unsafe names / 静默跳过不安全的名称
                    created->setAttribute(it.key(), it.value().toString());
                }
            }
            if (arguments.contains(QStringLiteral("classes"))) {
                for (const QJsonValue &value : arguments.value(QStringLiteral("classes")).toArray()) {
                    const QString name = value.toString().trimmed();
                    if (!name.isEmpty() && !created->classes.contains(name))
                        created->classes.append(name);
                }
            }
            if (arguments.contains(QStringLiteral("styles"))) {
                const QJsonObject values = arguments.value(QStringLiteral("styles")).toObject();
                for (auto it = values.constBegin(); it != values.constEnd(); ++it)
                    created->style.base().set(it.key(), it.value().toString());
            }

            const int position = arguments.contains(QStringLiteral("index"))
                                     ? arguments.value(QStringLiteral("index")).toInt()
                                     : -1;
            DocumentNode *inserted = parent->insertChild(std::move(created), position);
            if (!inserted) {
                if (error)
                    *error = bilingual(QStringLiteral("The element could not be inserted."),
                                       QStringLiteral("无法插入该元素。"));
                return ToolResult();
            }
            page->ensureElementClass(inserted);
            if (context.project)
                context.project->touch();
            return okResult(QJsonObject{
                { QStringLiteral("elementId"), inserted->id },
                { QStringLiteral("tag"), inserted->tag },
                { QStringLiteral("parentId"), parent->id },
                { QStringLiteral("indexInParent"), inserted->indexInParent() }
            });
        });

    add(QStringLiteral("delete_element"), QStringLiteral("element"),
        QStringLiteral("mutates destructive"),
        schemaObject(QJsonObject{
                         { QStringLiteral("elementId"),
                           propString(QStringLiteral("Id of the element to delete, including all "
                                                     "of its children.")) },
                         { QStringLiteral("pageId"),
                           propString(QStringLiteral("Page id. Omit for the active page.")) }
                     },
                     { QStringLiteral("elementId") }),
        QStringLiteral("Delete element"), QStringLiteral("删除元素"),
        QStringLiteral("Permanently delete an element and everything inside it. The page root "
                       "cannot be deleted."),
        QStringLiteral("永久删除一个元素及其全部内容。页面根节点无法删除。"),
        [](const QJsonObject &arguments, ToolContext &context, QString *error) -> ToolResult {
            Page *page = resolvePage(context, arguments.value(QStringLiteral("pageId")).toString(),
                                     error, nullptr);
            if (!page)
                return ToolResult();
            const QString elementId =
                arguments.value(QStringLiteral("elementId")).toString().trimmed();
            if (elementId.isEmpty()) {
                if (error)
                    *error = bilingual(QStringLiteral("\"elementId\" is required."),
                                       QStringLiteral("\"elementId\" 为必填项。"));
                return ToolResult();
            }
            DocumentNode *node = page->findNode(elementId);
            if (!node) {
                if (error)
                    *error = bilingual(
                        QStringLiteral("No element with id \"%1\" exists on page \"%2\".")
                            .arg(elementId, page->name),
                        QStringLiteral("页面 \"%2\" 上不存在 id 为 \"%1\" 的元素。")
                            .arg(elementId, page->name));
                return ToolResult();
            }
            if (node == page->root.get()) {
                if (error)
                    *error = bilingual(QStringLiteral("The page root cannot be deleted."),
                                       QStringLiteral("页面根节点无法删除。"));
                return ToolResult();
            }
            DocumentNode *parent = node->parent();
            if (!parent) {
                if (error)
                    *error = bilingual(QStringLiteral("The element has no parent."),
                                       QStringLiteral("该元素没有父节点。"));
                return ToolResult();
            }
            const QString tag = node->tag;
            if (!parent->removeChild(node)) {
                if (error)
                    *error = bilingual(QStringLiteral("The element could not be removed."),
                                       QStringLiteral("无法移除该元素。"));
                return ToolResult();
            }
            if (context.activeNodeId == elementId)
                context.activeNodeId.clear();
            if (context.project)
                context.project->touch();
            return okResult(QJsonObject{
                { QStringLiteral("deleted"), elementId },
                { QStringLiteral("tag"), tag },
                { QStringLiteral("parentId"), parent->id }
            });
        });

    add(QStringLiteral("duplicate_element"), QStringLiteral("element"), QStringLiteral("mutates"),
        schemaObject(QJsonObject{
                         { QStringLiteral("elementId"),
                           propString(QStringLiteral("Id of the element to copy.")) },
                         { QStringLiteral("pageId"),
                           propString(QStringLiteral("Page id. Omit for the active page.")) },
                         { QStringLiteral("parentId"),
                           propString(QStringLiteral("Parent for the copy. Omit to keep the "
                                                     "original's parent.")) }
                     },
                     { QStringLiteral("elementId") }),
        QStringLiteral("Duplicate element"), QStringLiteral("复制元素"),
        QStringLiteral("Deep-copy an element and its children with fresh ids, placing the copy "
                       "directly after the original. Returns the new element id."),
        QStringLiteral("深度复制元素及其子节点并生成全新 id，副本紧跟在原元素之后。"
                       "返回新元素 id。"),
        [](const QJsonObject &arguments, ToolContext &context, QString *error) -> ToolResult {
            Page *page = resolvePage(context, arguments.value(QStringLiteral("pageId")).toString(),
                                     error, nullptr);
            if (!page)
                return ToolResult();
            DocumentNode *node =
                resolveNode(page, context, arguments.value(QStringLiteral("elementId")).toString(),
                            error, nullptr);
            if (!node)
                return ToolResult();
            if (node == page->root.get()) {
                if (error)
                    *error = bilingual(QStringLiteral("The page root cannot be duplicated."),
                                       QStringLiteral("页面根节点无法复制。"));
                return ToolResult();
            }

            //  keepIds = false regenerates ids during the clone; the residual
            //  clear below also covers ids copied through attribute maps.
            //  keepIds = false 会在克隆时重新生成 id；下面的清理还可覆盖通过属性表
            //  复制过来的 id。
            std::unique_ptr<DocumentNode> copy = node->clone(false);
            if (!copy) {
                if (error)
                    *error = bilingual(QStringLiteral("The element could not be copied."),
                                       QStringLiteral("无法复制该元素。"));
                return ToolResult();
            }
            copy->id.clear();
            copy->forEach([](DocumentNode *child) { child->id.clear(); });
            copy->ensureIds();

            DocumentNode *parent = node->parent();
            if (arguments.contains(QStringLiteral("parentId"))
                && !arguments.value(QStringLiteral("parentId")).toString().trimmed().isEmpty()) {
                parent = resolveNode(page, context,
                                     arguments.value(QStringLiteral("parentId")).toString(),
                                     error, nullptr);
                if (!parent)
                    return ToolResult();
            }
            if (!parent) {
                if (error)
                    *error = bilingual(QStringLiteral("The element has no parent."),
                                       QStringLiteral("该元素没有父节点。"));
                return ToolResult();
            }

            const int position = (parent == node->parent()) ? node->indexInParent() + 1 : -1;
            DocumentNode *inserted = parent->insertChild(std::move(copy), position);
            if (!inserted) {
                if (error)
                    *error = bilingual(QStringLiteral("The copy could not be inserted."),
                                       QStringLiteral("无法插入副本。"));
                return ToolResult();
            }
            page->ensureElementClass(inserted);
            if (context.project)
                context.project->touch();
            return okResult(QJsonObject{
                { QStringLiteral("elementId"), inserted->id },
                { QStringLiteral("sourceId"), node->id },
                { QStringLiteral("parentId"), parent->id },
                { QStringLiteral("indexInParent"), inserted->indexInParent() }
            });
        });

    add(QStringLiteral("move_element"), QStringLiteral("element"), QStringLiteral("mutates"),
        schemaObject(QJsonObject{
                         { QStringLiteral("elementId"),
                           propString(QStringLiteral("Id of the element to move.")) },
                         { QStringLiteral("parentId"),
                           propString(QStringLiteral("New parent id. Omit to keep the current "
                                                     "parent.")) },
                         { QStringLiteral("index"),
                           propNumber(QStringLiteral("New position among the parent's children. "
                                                     "Omit to append."), 0, 100000) },
                         { QStringLiteral("pageId"),
                           propString(QStringLiteral("Page id. Omit for the active page.")) }
                     },
                     { QStringLiteral("elementId") }),
        QStringLiteral("Move element"), QStringLiteral("移动元素"),
        QStringLiteral("Reparent and/or reorder an element. Moving an element into its own "
                       "descendant is refused."),
        QStringLiteral("改变元素的父节点和/或顺序。拒绝把元素移动到它自己的后代中。"),
        [](const QJsonObject &arguments, ToolContext &context, QString *error) -> ToolResult {
            Page *page = resolvePage(context, arguments.value(QStringLiteral("pageId")).toString(),
                                     error, nullptr);
            if (!page)
                return ToolResult();
            DocumentNode *node =
                resolveNode(page, context, arguments.value(QStringLiteral("elementId")).toString(),
                            error, nullptr);
            if (!node)
                return ToolResult();
            if (node == page->root.get()) {
                if (error)
                    *error = bilingual(QStringLiteral("The page root cannot be moved."),
                                       QStringLiteral("页面根节点无法移动。"));
                return ToolResult();
            }

            DocumentNode *newParent = node->parent();
            if (arguments.contains(QStringLiteral("parentId"))
                && !arguments.value(QStringLiteral("parentId")).toString().trimmed().isEmpty()) {
                newParent = resolveNode(page, context,
                                        arguments.value(QStringLiteral("parentId")).toString(),
                                        error, nullptr);
                if (!newParent)
                    return ToolResult();
            }
            if (!newParent) {
                if (error)
                    *error = bilingual(QStringLiteral("The element has no destination parent."),
                                       QStringLiteral("该元素没有目标父节点。"));
                return ToolResult();
            }
            if (newParent == node || node->isAncestorOf(newParent)) {
                if (error)
                    *error = bilingual(
                        QStringLiteral("An element cannot be moved into itself or one of its own "
                                       "descendants."),
                        QStringLiteral("元素不能被移动到它自身或其任一后代之中。"));
                return ToolResult();
            }

            //  Index is interpreted inside the destination parent, matching the
            //  mental model of "insert at position N".
            //  索引按目标父节点解释，符合"插入到第 N 位"的直观模型。
            int index = arguments.contains(QStringLiteral("index"))
                            ? arguments.value(QStringLiteral("index")).toInt()
                            : -1;
            if (index > newParent->childCount())
                index = newParent->childCount();

            if (!page->root->moveChildTo(node, newParent, index)) {
                if (error)
                    *error = bilingual(QStringLiteral("The move was refused by the document."),
                                       QStringLiteral("文档拒绝了该移动操作。"));
                return ToolResult();
            }
            if (context.project)
                context.project->touch();
            return okResult(QJsonObject{
                { QStringLiteral("elementId"), node->id },
                { QStringLiteral("parentId"), newParent->id },
                { QStringLiteral("indexInParent"), node->indexInParent() }
            });
        });

    add(QStringLiteral("set_element_text"), QStringLiteral("element"), QStringLiteral("mutates"),
        schemaObject(QJsonObject{
                         { QStringLiteral("elementId"),
                           propString(QStringLiteral("Id of the element to change.")) },
                         { QStringLiteral("text"),
                           propString(QStringLiteral("New text content. Pass an empty string to "
                                                     "clear it.")) },
                         { QStringLiteral("html"),
                           propBoolean(QStringLiteral("Treat \"text\" as raw HTML markup. "
                                                      "Default false.")) },
                         { QStringLiteral("pageId"),
                           propString(QStringLiteral("Page id. Omit for the active page.")) }
                     },
                     { QStringLiteral("elementId"), QStringLiteral("text") }),
        QStringLiteral("Set element text"), QStringLiteral("设置元素文本"),
        QStringLiteral("Replace an element's text content. Void elements (img, input, br) are "
                       "refused because they cannot carry text."),
        QStringLiteral("替换元素的文本内容。空元素（img、input、br）不能承载文本，会被拒绝。"),
        [](const QJsonObject &arguments, ToolContext &context, QString *error) -> ToolResult {
            Page *page = resolvePage(context, arguments.value(QStringLiteral("pageId")).toString(),
                                     error, nullptr);
            if (!page)
                return ToolResult();
            DocumentNode *node =
                resolveNode(page, context, arguments.value(QStringLiteral("elementId")).toString(),
                            error, nullptr);
            if (!node)
                return ToolResult();
            if (node->isVoidElement()) {
                if (error)
                    *error = bilingual(
                        QStringLiteral("<%1> is a void element and cannot contain text.")
                            .arg(node->tag),
                        QStringLiteral("<%1> 是空元素，不能包含文本。").arg(node->tag));
                return ToolResult();
            }
            const QString previous = node->text;
            node->text = arguments.value(QStringLiteral("text")).toString();
            node->textIsHtml = arguments.value(QStringLiteral("html")).toBool(false);
            if (context.project)
                context.project->touch();
            return okResult(QJsonObject{
                { QStringLiteral("elementId"), node->id },
                { QStringLiteral("previousText"), textPreview(previous) },
                { QStringLiteral("text"), textPreview(node->text) },
                { QStringLiteral("textIsHtml"), node->textIsHtml }
            });
        });

    add(QStringLiteral("set_element_attribute"), QStringLiteral("element"),
        QStringLiteral("mutates"),
        schemaObject(QJsonObject{
                         { QStringLiteral("elementId"),
                           propString(QStringLiteral("Id of the element to change.")) },
                         { QStringLiteral("name"),
                           propString(QStringLiteral("Attribute name, for example \"href\" or "
                                                     "\"src\".")) },
                         { QStringLiteral("value"),
                           propString(QStringLiteral("Attribute value.")) },
                         { QStringLiteral("remove"),
                           propBoolean(QStringLiteral("Remove the attribute instead of setting "
                                                      "it.")) },
                         { QStringLiteral("pageId"),
                           propString(QStringLiteral("Page id. Omit for the active page.")) }
                     },
                     { QStringLiteral("elementId"), QStringLiteral("name") }),
        QStringLiteral("Set attribute"), QStringLiteral("设置属性"),
        QStringLiteral("Set or remove one HTML attribute on an element, for example href, src, "
                       "alt, placeholder or aria-label."),
        QStringLiteral("在元素上设置或移除一个 HTML 属性，例如 href、src、alt、placeholder "
                       "或 aria-label。"),
        [](const QJsonObject &arguments, ToolContext &context, QString *error) -> ToolResult {
            Page *page = resolvePage(context, arguments.value(QStringLiteral("pageId")).toString(),
                                     error, nullptr);
            if (!page)
                return ToolResult();
            DocumentNode *node =
                resolveNode(page, context, arguments.value(QStringLiteral("elementId")).toString(),
                            error, nullptr);
            if (!node)
                return ToolResult();
            const QString name = arguments.value(QStringLiteral("name")).toString().trimmed();
            if (name.isEmpty() || !isSafeAttributeName(name)) {
                if (error)
                    *error = bilingual(QStringLiteral("\"%1\" is not a safe attribute name.")
                                           .arg(name),
                                       QStringLiteral("\"%1\" 不是安全的属性名。").arg(name));
                return ToolResult();
            }
            const QString previous = node->attribute(name);
            const bool remove = arguments.value(QStringLiteral("remove")).toBool(false);
            if (remove)
                node->removeAttribute(name);
            else
                node->setAttribute(name, arguments.value(QStringLiteral("value")).toString());
            if (context.project)
                context.project->touch();
            return okResult(QJsonObject{
                { QStringLiteral("elementId"), node->id },
                { QStringLiteral("name"), name },
                { QStringLiteral("removed"), remove },
                { QStringLiteral("previousValue"), previous },
                { QStringLiteral("value"), remove ? QString() : node->attribute(name) }
            });
        });

    add(QStringLiteral("set_element_classes"), QStringLiteral("element"), QStringLiteral("mutates"),
        schemaObject(QJsonObject{
                         { QStringLiteral("elementId"),
                           propString(QStringLiteral("Id of the element to change.")) },
                         { QStringLiteral("classes"),
                           propArray(QStringLiteral("CSS classes. With \"mode\":\"set\" this "
                                                    "replaces the whole list."),
                                     propString(QStringLiteral("One class name."))) },
                         { QStringLiteral("mode"),
                           propEnum(QStringLiteral("How to apply the list. Default \"set\"."),
                                    { QStringLiteral("set"), QStringLiteral("add"),
                                      QStringLiteral("remove") }) },
                         { QStringLiteral("pageId"),
                           propString(QStringLiteral("Page id. Omit for the active page.")) }
                     },
                     { QStringLiteral("elementId"), QStringLiteral("classes") }),
        QStringLiteral("Set classes"), QStringLiteral("设置 CSS 类"),
        QStringLiteral("Replace, add or remove an element's CSS classes. Note that the code "
                       "generator also emits a stable per-element class."),
        QStringLiteral("替换、添加或移除元素的 CSS 类。请注意代码生成器还会输出每个元素的"
                       "稳定类名。"),
        [](const QJsonObject &arguments, ToolContext &context, QString *error) -> ToolResult {
            Page *page = resolvePage(context, arguments.value(QStringLiteral("pageId")).toString(),
                                     error, nullptr);
            if (!page)
                return ToolResult();
            DocumentNode *node =
                resolveNode(page, context, arguments.value(QStringLiteral("elementId")).toString(),
                            error, nullptr);
            if (!node)
                return ToolResult();

            const QString mode = arguments.value(QStringLiteral("mode")).toString().trimmed().toLower();
            QStringList incoming;
            for (const QJsonValue &value : arguments.value(QStringLiteral("classes")).toArray()) {
                const QString name = value.toString().trimmed();
                if (!name.isEmpty())
                    incoming.append(name);
            }
            if (mode == QStringLiteral("add")) {
                for (const QString &name : incoming) {
                    if (!node->classes.contains(name))
                        node->classes.append(name);
                }
            } else if (mode == QStringLiteral("remove")) {
                for (const QString &name : incoming)
                    node->classes.removeAll(name);
            } else {
                node->classes = incoming;
            }
            if (context.project)
                context.project->touch();

            QJsonArray classes;
            for (const QString &name : node->classes)
                classes.append(name);
            return okResult(QJsonObject{
                { QStringLiteral("elementId"), node->id },
                { QStringLiteral("mode"), mode.isEmpty() ? QStringLiteral("set") : mode },
                { QStringLiteral("classes"), classes }
            });
        });

    add(QStringLiteral("set_element_style"), QStringLiteral("element"), QStringLiteral("mutates"),
        schemaObject(QJsonObject{
                         { QStringLiteral("elementId"),
                           propString(QStringLiteral("Id of the element to style.")) },
                         { QStringLiteral("property"),
                           propString(QStringLiteral("CSS property in kebab-case, for example "
                                                     "\"background-color\".")) },
                         { QStringLiteral("value"),
                           propString(QStringLiteral("CSS value, for example \"#0f172a\" or "
                                                     "\"1.5rem\". Pass an empty string to remove "
                                                     "the declaration.")) },
                         { QStringLiteral("breakpoint"),
                           propEnum(QStringLiteral("Breakpoint to write to. Default \"desktop\"."),
                                    { QStringLiteral("desktop"), QStringLiteral("tablet"),
                                      QStringLiteral("mobile") }) },
                         { QStringLiteral("pageId"),
                           propString(QStringLiteral("Page id. Omit for the active page.")) }
                     },
                     { QStringLiteral("elementId"), QStringLiteral("property") }),
        QStringLiteral("Set style"), QStringLiteral("设置样式"),
        QStringLiteral("Set or remove one CSS declaration on an element at one breakpoint. The "
                       "desktop value is the base; tablet and mobile are overrides."),
        QStringLiteral("在某个断点为元素设置或移除一条 CSS 声明。桌面端为基础值，"
                       "平板与手机为覆盖值。"),
        [](const QJsonObject &arguments, ToolContext &context, QString *error) -> ToolResult {
            Page *page = resolvePage(context, arguments.value(QStringLiteral("pageId")).toString(),
                                     error, nullptr);
            if (!page)
                return ToolResult();
            DocumentNode *node =
                resolveNode(page, context, arguments.value(QStringLiteral("elementId")).toString(),
                            error, nullptr);
            if (!node)
                return ToolResult();

            const QString property = arguments.value(QStringLiteral("property")).toString().trimmed();
            if (property.isEmpty()) {
                if (error)
                    *error = bilingual(QStringLiteral("\"property\" is required."),
                                       QStringLiteral("\"property\" 为必填项。"));
                return ToolResult();
            }
            //  Only a CSS property name is accepted, never a selector or a brace.
            //  只接受 CSS 属性名，绝不接受选择器或花括号。
            static const QRegularExpression propertyPattern(
                QStringLiteral("^-{0,2}[a-zA-Z][a-zA-Z0-9-]*$"));
            if (!propertyPattern.match(property).hasMatch()) {
                if (error)
                    *error = bilingual(
                        QStringLiteral("\"%1\" is not a plain CSS property name.").arg(property),
                        QStringLiteral("\"%1\" 不是纯 CSS 属性名。").arg(property));
                return ToolResult();
            }

            const Breakpoint breakpoint =
                breakpointFromTool(arguments.value(QStringLiteral("breakpoint")).toString());
            const QString value = arguments.value(QStringLiteral("value")).toString();
            const QString previous = node->style.resolve(breakpoint, property);

            if (value.trimmed().isEmpty())
                node->style.overrideFor(breakpoint).remove(property);
            else
                node->setStyleValue(breakpoint, property, value);

            if (context.project)
                context.project->touch();
            return okResult(QJsonObject{
                { QStringLiteral("elementId"), node->id },
                { QStringLiteral("property"), property },
                { QStringLiteral("breakpoint"), toString(breakpoint) },
                { QStringLiteral("previousValue"), previous },
                { QStringLiteral("value"), value.trimmed().isEmpty()
                                               ? QString()
                                               : node->style.resolve(breakpoint, property) }
            });
        });

    add(QStringLiteral("add_composite_widget"), QStringLiteral("element"),
        QStringLiteral("mutates"),
        schemaObject(QJsonObject{
                         { QStringLiteral("widgetId"),
                           propString(QStringLiteral("Composite id from list_available_elements, "
                                                     "for example \"hero-center\".")) },
                         { QStringLiteral("parentId"),
                           propString(QStringLiteral("Parent element id. Omit to append to "
                                                     "<body>.")) },
                         { QStringLiteral("index"),
                           propNumber(QStringLiteral("Insert position. Omit to append."), 0, 100000) },
                         { QStringLiteral("pageId"),
                           propString(QStringLiteral("Page id. Omit for the active page.")) }
                     },
                     { QStringLiteral("widgetId") }),
        QStringLiteral("Add ready-made widget"), QStringLiteral("添加预制组件"),
        QStringLiteral("Insert a ready-made multi-element widget (navbar, hero, pricing card, "
                       "footer, ...) as a complete subtree. Call list_available_elements for the "
                       "available widget ids."),
        QStringLiteral("插入一个由多元素组成的预制组件（导航栏、主视觉、价格卡片、页脚等）"
                       "的完整子树。可调用 list_available_elements 获取可用的组件 id。"),
        [](const QJsonObject &arguments, ToolContext &context, QString *error) -> ToolResult {
            Page *page = resolvePage(context, arguments.value(QStringLiteral("pageId")).toString(),
                                     error, nullptr);
            if (!page || !page->root)
                return ToolResult();

            const QString widgetId = arguments.value(QStringLiteral("widgetId")).toString().trimmed();
            if (widgetId.isEmpty()) {
                if (error)
                    *error = bilingual(QStringLiteral("\"widgetId\" is required."),
                                       QStringLiteral("\"widgetId\" 为必填项。"));
                return ToolResult();
            }
            if (!ElementCatalog::instance().findComposite(widgetId)) {
                QStringList available;
                for (const CompositeDef &def : ElementCatalog::instance().composites()) {
                    available.append(def.id);
                    if (available.size() >= 30)
                        break;
                }
                if (error)
                    *error = bilingual(
                        QStringLiteral("No ready-made widget has the id \"%1\". Available ids "
                                       "include: %2").arg(widgetId, available.join(QStringLiteral(", "))),
                        QStringLiteral("没有 id 为 \"%1\" 的预制组件。可用 id 包括：%2")
                            .arg(widgetId, available.join(QStringLiteral(", "))));
                return ToolResult();
            }

            std::unique_ptr<DocumentNode> created =
                ElementCatalog::instance().createComposite(widgetId, context.chinese);
            if (!created) {
                if (error)
                    *error = bilingual(QStringLiteral("The widget \"%1\" could not be built.")
                                           .arg(widgetId),
                                       QStringLiteral("无法构建组件 \"%1\"。").arg(widgetId));
                return ToolResult();
            }

            DocumentNode *parent = page->root.get();
            if (arguments.contains(QStringLiteral("parentId"))
                && !arguments.value(QStringLiteral("parentId")).toString().trimmed().isEmpty()) {
                parent = resolveNode(page, context,
                                     arguments.value(QStringLiteral("parentId")).toString(),
                                     error, nullptr);
                if (!parent)
                    return ToolResult();
            }
            const int position = arguments.contains(QStringLiteral("index"))
                                     ? arguments.value(QStringLiteral("index")).toInt()
                                     : -1;
            DocumentNode *inserted = parent->insertChild(std::move(created), position);
            if (!inserted) {
                if (error)
                    *error = bilingual(QStringLiteral("The widget could not be inserted."),
                                       QStringLiteral("无法插入该组件。"));
                return ToolResult();
            }
            page->ensureElementClass(inserted);
            if (context.project)
                context.project->touch();
            return okResult(QJsonObject{
                { QStringLiteral("elementId"), inserted->id },
                { QStringLiteral("widgetId"), widgetId },
                { QStringLiteral("parentId"), parent->id },
                { QStringLiteral("subtreeSize"), inserted->childCount() + 1 }
            });
        });

    add(QStringLiteral("list_available_elements"), QStringLiteral("element"), QString(),
        schemaObject(QJsonObject{
                         { QStringLiteral("category"),
                           propString(QStringLiteral("Optional filter: layout, text, media, form, "
                                                     "data, navigation, advanced.")) },
                         { QStringLiteral("includeComposites"),
                           propBoolean(QStringLiteral("Include ready-made widget ids. Default "
                                                      "true.")) }
                     }),
        QStringLiteral("List element palette"), QStringLiteral("列出素材目录"),
        QStringLiteral("List the HTML tags and ready-made widgets this builder can create. Call "
                       "it before add_element so the tag is known to be valid."),
        QStringLiteral("列出本构建器可创建的 HTML 标签与预制组件。调用 add_element 之前先调用"
                       "它，可确保标签有效。"),
        [](const QJsonObject &arguments, ToolContext &context, QString *error) -> ToolResult {
            Q_UNUSED(error);
            const QString requested = arguments.value(QStringLiteral("category")).toString().trimmed();
            const ElementCategory filter = elementCategoryFromString(requested,
                                                                     ElementCategory::Custom);
            const bool hasFilter = !requested.isEmpty();

            QJsonArray elements;
            for (const ElementDef &def : ElementCatalog::instance().elements()) {
                if (hasFilter && def.category != filter)
                    continue;
                QJsonObject entry;
                entry.insert(QStringLiteral("tag"), def.tag);
                entry.insert(QStringLiteral("label"), context.chinese ? def.labelZh : def.labelEn);
                entry.insert(QStringLiteral("category"), toString(def.category));
                entry.insert(QStringLiteral("allowsChildren"), def.allowsChildren);
                entry.insert(QStringLiteral("isVoid"), def.isVoid);
                elements.append(entry);
            }

            QJsonArray composites;
            if (arguments.value(QStringLiteral("includeComposites")).toBool(true)) {
                for (const CompositeDef &def : ElementCatalog::instance().composites()) {
                    QJsonObject entry;
                    entry.insert(QStringLiteral("id"), def.id);
                    entry.insert(QStringLiteral("label"),
                                 context.chinese ? def.labelZh : def.labelEn);
                    entry.insert(QStringLiteral("group"), context.chinese ? def.groupZh : def.groupEn);
                    composites.append(entry);
                }
            }
            return okResult(QJsonObject{
                { QStringLiteral("elementCount"), elements.size() },
                { QStringLiteral("elements"), elements },
                { QStringLiteral("compositeCount"), composites.size() },
                { QStringLiteral("composites"), composites }
            });
        });

    // =======================================================================
    //  style
    // =======================================================================

    add(QStringLiteral("set_theme_tokens"), QStringLiteral("style"), QStringLiteral("mutates"),
        schemaObject(QJsonObject{
                         { QStringLiteral("pageId"),
                           propString(QStringLiteral("Page id. Omit for the active page.")) },
                         { QStringLiteral("tokens"),
                           propObject(QStringLiteral("CSS custom properties without the leading "
                                                     "--, for example {\"brand\":\"#2563eb\"}.")) },
                         { QStringLiteral("scope"),
                           propEnum(QStringLiteral("Where to apply the tokens. Default \"page\"."),
                                    { QStringLiteral("page"), QStringLiteral("project"),
                                      QStringLiteral("both") }) },
                         { QStringLiteral("remove"),
                           propBoolean(QStringLiteral("Remove the named tokens instead of setting "
                                                      "them. Default false.")) }
                     },
                     { QStringLiteral("tokens") }),
        QStringLiteral("Set theme tokens"), QStringLiteral("设置主题变量"),
        QStringLiteral("Set or remove CSS custom properties (design tokens) on a page or for the "
                       "whole project, for example brand colour, radius or spacing."),
        QStringLiteral("在页面或整个工程范围内设置或移除 CSS 自定义属性（设计变量），"
                       "例如品牌色、圆角或间距。"),
        [](const QJsonObject &arguments, ToolContext &context, QString *error) -> ToolResult {
            if (!arguments.value(QStringLiteral("tokens")).isObject()) {
                if (error)
                    *error = bilingual(
                        QStringLiteral("\"tokens\" must be an object of name to value pairs."),
                        QStringLiteral("\"tokens\" 必须是名称到值的对象。"));
                return ToolResult();
            }
            const QString scope =
                arguments.value(QStringLiteral("scope")).toString().trimmed().toLower();
            const bool projectScope = scope == QStringLiteral("project")
                                      || scope == QStringLiteral("both");
            const bool pageScope = !projectScope || scope == QStringLiteral("both")
                                   || scope.isEmpty();
            const bool remove = arguments.value(QStringLiteral("remove")).toBool(false);

            Page *page = nullptr;
            if (pageScope) {
                page = resolvePage(context, arguments.value(QStringLiteral("pageId")).toString(),
                                   error, nullptr);
                if (!page)
                    return ToolResult();
            }
            if (projectScope && !context.project) {
                if (error)
                    *error = bilingual(QStringLiteral("No project is open."),
                                       QStringLiteral("当前没有打开工程。"));
                return ToolResult();
            }

            const QJsonObject tokens = arguments.value(QStringLiteral("tokens")).toObject();
            QJsonArray applied;
            for (auto it = tokens.constBegin(); it != tokens.constEnd(); ++it) {
                QString key = it.key().trimmed();
                if (key.isEmpty())
                    continue;
                if (key.startsWith(QStringLiteral("--")))
                    key.remove(0, 2);
                //  A token name must not be able to inject CSS syntax.
                //  变量名不得能注入 CSS 语法。
                static const QRegularExpression tokenPattern(
                    QStringLiteral("^[a-zA-Z][a-zA-Z0-9_-]*$"));
                if (!tokenPattern.match(key).hasMatch())
                    continue;
                const QString value = it.value().toString();
                if (pageScope && page) {
                    if (remove)
                        page->cssVariables.remove(key);
                    else
                        page->cssVariables.insert(key, value);
                }
                if (projectScope && context.project) {
                    if (remove)
                        context.project->sharedCssVariables.remove(key);
                    else
                        context.project->sharedCssVariables.insert(key, value);
                }
                applied.append(key);
            }
            if (context.project)
                context.project->touch();
            return okResult(QJsonObject{
                { QStringLiteral("scope"), scope.isEmpty() ? QStringLiteral("page") : scope },
                { QStringLiteral("removed"), remove },
                { QStringLiteral("tokens"), applied },
                { QStringLiteral("pageId"), page ? page->id : QString() }
            });
        });

    // =======================================================================
    //  blocks
    // =======================================================================

    add(QStringLiteral("list_available_blocks"), QStringLiteral("blocks"), QString(),
        schemaObject(QJsonObject{
                         { QStringLiteral("category"),
                           propString(QStringLiteral("Optional BlockRegistry category key, for "
                                                     "example \"control\" or \"http\".")) }
                     }),
        QStringLiteral("List block types"), QStringLiteral("列出积木类型"),
        QStringLiteral("List every block type the Scratch-style editor offers, with its category "
                       "and shape. Call it before add_block so the type id is valid."),
        QStringLiteral("列出类 Scratch 编辑器提供的全部积木类型及其分类与形状。"
                       "调用 add_block 之前先调用它，可确保类型 id 有效。"),
        [](const QJsonObject &arguments, ToolContext &context, QString *error) -> ToolResult {
            Q_UNUSED(error);
            const QString category = arguments.value(QStringLiteral("category")).toString().trimmed();

            QJsonArray categories;
            for (const BlockCategory &entry : BlockRegistry::instance().categories()) {
                QJsonObject object;
                object.insert(QStringLiteral("key"), entry.key);
                object.insert(QStringLiteral("label"),
                              context.chinese ? entry.labelZh : entry.labelEn);
                object.insert(QStringLiteral("advanced"), entry.advanced);
                categories.append(object);
            }

            QJsonArray blocks;
            for (const BlockDef &def : BlockRegistry::instance().definitions()) {
                if (!category.isEmpty() && def.category != category)
                    continue;
                QJsonObject entry;
                entry.insert(QStringLiteral("type"), def.type);
                entry.insert(QStringLiteral("category"), def.category);
                entry.insert(QStringLiteral("label"),
                             context.chinese ? def.labelZh : def.labelEn);
                entry.insert(QStringLiteral("shape"), toString(def.shape));
                entry.insert(QStringLiteral("returnsValue"), def.returnsValue());
                entry.insert(QStringLiteral("hasStatements"), def.hasNestedStatements());
                if (!def.fields.isEmpty()) {
                    QJsonArray fields;
                    for (const BlockFieldDef &field : def.fields) {
                        QJsonObject fieldObject;
                        fieldObject.insert(QStringLiteral("name"), field.name);
                        fieldObject.insert(QStringLiteral("kind"), toString(field.kind));
                        if (!field.options.isEmpty())
                            fieldObject.insert(QStringLiteral("options"),
                                               QJsonArray::fromStringList(field.options));
                        fields.append(fieldObject);
                    }
                    entry.insert(QStringLiteral("fields"), fields);
                }
                blocks.append(entry);
            }
            return okResult(QJsonObject{
                { QStringLiteral("categories"), categories },
                { QStringLiteral("blockCount"), blocks.size() },
                { QStringLiteral("blocks"), blocks }
            });
        });

    add(QStringLiteral("list_block_scripts"), QStringLiteral("blocks"), QString(),
        schemaObject(QJsonObject{
                         { QStringLiteral("pageId"),
                           propString(QStringLiteral("Page id. Omit for the active page.")) }
                     }),
        QStringLiteral("List block scripts"), QStringLiteral("列出积木脚本"),
        QStringLiteral("List the block scripts of a page: script id, name, event type, enabled "
                       "flag, and the ids and types of the statements it contains."),
        QStringLiteral("列出页面的积木脚本：脚本 id、名称、事件类型、启用标记，以及其中语句"
                       "的 id 与类型。"),
        [](const QJsonObject &arguments, ToolContext &context, QString *error) -> ToolResult {
            Page *page = resolvePage(context, arguments.value(QStringLiteral("pageId")).toString(),
                                     error, nullptr);
            if (!page)
                return ToolResult();
            if (!context.project)
                return okResult(QJsonObject{ { QStringLiteral("scripts"), QJsonArray() } });

            QJsonArray scripts;
            for (BlockScript *script : context.project->program()) {
                if (!script)
                    continue;
                if (!script->pageTarget.isEmpty() && script->pageTarget != page->id)
                    continue;
                QJsonObject entry;
                entry.insert(QStringLiteral("scriptId"), script->id);
                entry.insert(QStringLiteral("name"), script->name);
                entry.insert(QStringLiteral("eventType"), script->eventType);
                entry.insert(QStringLiteral("enabled"), script->enabled);
                entry.insert(QStringLiteral("pageTarget"), script->pageTarget);

                QJsonArray body;
                for (Block *block : script->body) {
                    if (!block)
                        continue;
                    QJsonObject blockObject;
                    blockObject.insert(QStringLiteral("blockId"), block->id);
                    blockObject.insert(QStringLiteral("type"), block->type);
                    if (!block->comment.isEmpty())
                        blockObject.insert(QStringLiteral("comment"), textPreview(block->comment));
                    body.append(blockObject);
                }
                entry.insert(QStringLiteral("statementCount"), body.size());
                entry.insert(QStringLiteral("statements"), body);
                scripts.append(entry);
            }
            return okResult(QJsonObject{
                { QStringLiteral("pageId"), page->id },
                { QStringLiteral("count"), scripts.size() },
                { QStringLiteral("scripts"), scripts }
            });
        });

    add(QStringLiteral("add_block"), QStringLiteral("blocks"), QStringLiteral("mutates"),
        schemaObject(QJsonObject{
                         { QStringLiteral("type"),
                           propString(QStringLiteral("Block type id from list_available_blocks, "
                                                     "for example \"control.if\".")) },
                         { QStringLiteral("scriptId"),
                           propString(QStringLiteral("Target script id. Omit to use the page's "
                                                     "first script.")) },
                         { QStringLiteral("afterBlockId"),
                           propString(QStringLiteral("Insert directly after this statement block "
                                                     "id.")) },
                         { QStringLiteral("fields"),
                           propObject(QStringLiteral("Field name to value map, for example "
                                                     "{\"condition\":\"ready\"}.")) },
                         { QStringLiteral("pageId"),
                           propString(QStringLiteral("Page id. Omit for the active page.")) }
                     },
                     { QStringLiteral("type") }),
        QStringLiteral("Add block"), QStringLiteral("添加积木"),
        QStringLiteral("Append a block to the active page's block program. Hat-type blocks are "
                       "refused here because a script already owns one hat."),
        QStringLiteral("向活动页面的积木程序追加一个积木。帽子类积木在此被拒绝，"
                       "因为脚本已经拥有一个帽子块。"),
        [](const QJsonObject &arguments, ToolContext &context, QString *error) -> ToolResult {
            if (!context.project) {
                if (error)
                    *error = bilingual(QStringLiteral("No project is open."),
                                       QStringLiteral("当前没有打开工程。"));
                return ToolResult();
            }
            Page *page = resolvePage(context, arguments.value(QStringLiteral("pageId")).toString(),
                                     error, nullptr);
            if (!page)
                return ToolResult();

            const QString type = arguments.value(QStringLiteral("type")).toString().trimmed();
            if (type.isEmpty()) {
                if (error)
                    *error = bilingual(QStringLiteral("\"type\" is required."),
                                       QStringLiteral("\"type\" 为必填项。"));
                return ToolResult();
            }
            const BlockDef *definition = BlockRegistry::instance().find(type);
            if (!definition) {
                if (error)
                    *error = bilingual(
                        QStringLiteral("No block type \"%1\" exists. Call list_available_blocks.")
                            .arg(type),
                        QStringLiteral("不存在积木类型 \"%1\"。请调用 list_available_blocks。")
                            .arg(type));
                return ToolResult();
            }
            if (definition->shape == BlockShape::Hat) {
                if (error)
                    *error = bilingual(
                        QStringLiteral("\"%1\" is a hat block; a script already owns its hat.")
                            .arg(type),
                        QStringLiteral("\"%1\" 是帽子积木；脚本已经拥有自己的帽子块。").arg(type));
                return ToolResult();
            }

            std::unique_ptr<Block> created = BlockRegistry::instance().createBlock(type);
            if (!created) {
                if (error)
                    *error = bilingual(QStringLiteral("The block \"%1\" could not be created.")
                                           .arg(type),
                                       QStringLiteral("无法创建积木 \"%1\"。").arg(type));
                return ToolResult();
            }
            if (arguments.contains(QStringLiteral("fields"))) {
                const QJsonObject fields = arguments.value(QStringLiteral("fields")).toObject();
                for (auto it = fields.constBegin(); it != fields.constEnd(); ++it)
                    created->setFieldValue(it.key(), it.value().toString());
            }

            //  Resolve the target script and insertion point.
            //  解析目标脚本与插入位置。
            QVector<BlockScript *> &program = context.project->program();
            BlockScript *script = nullptr;
            const QString requestedScript =
                arguments.value(QStringLiteral("scriptId")).toString().trimmed();
            int insertAt = -1;
            if (!requestedScript.isEmpty()) {
                script = context.project->scriptById(requestedScript);
                if (!script) {
                    if (error)
                        *error = bilingual(
                            QStringLiteral("No script has the id \"%1\". Call list_block_scripts.")
                                .arg(requestedScript),
                            QStringLiteral("没有 id 为 \"%1\" 的脚本。请调用 list_block_scripts。")
                                .arg(requestedScript));
                    return ToolResult();
                }
            } else if (!program.isEmpty()) {
                script = program.first();
            }
            if (!script) {
                if (error)
                    *error = bilingual(
                        QStringLiteral("The project has no block script yet. Create one in the "
                                       "blocks editor first."),
                        QStringLiteral("工程尚无积木脚本。请先在积木编辑器中创建脚本。"));
                return ToolResult();
            }

            const QString afterId =
                arguments.value(QStringLiteral("afterBlockId")).toString().trimmed();
            if (!afterId.isEmpty()) {
                for (int i = 0; i < script->body.size(); ++i) {
                    if (script->body.at(i) && script->body.at(i)->id == afterId) {
                        insertAt = i + 1;
                        break;
                    }
                }
                if (insertAt < 0) {
                    if (error)
                        *error = bilingual(
                            QStringLiteral("The statement \"%1\" is not part of script \"%2\".")
                                .arg(afterId, script->id),
                            QStringLiteral("语句 \"%1\" 不属于脚本 \"%2\"。")
                                .arg(afterId, script->id));
                    return ToolResult();
                }
            }

            const QString blockId = created->id;
            script->addBlock(std::move(created), insertAt);
            context.project->touch();
            return okResult(QJsonObject{
                { QStringLiteral("blockId"), blockId },
                { QStringLiteral("type"), type },
                { QStringLiteral("scriptId"), script->id },
                { QStringLiteral("statements"), script->blockCount() }
            });
        });

    add(QStringLiteral("delete_block"), QStringLiteral("blocks"),
        QStringLiteral("mutates destructive"),
        schemaObject(QJsonObject{
                         { QStringLiteral("blockId"),
                           propString(QStringLiteral("Id of the block to delete, including any "
                                                     "blocks nested inside it.")) }
                     },
                     { QStringLiteral("blockId") }),
        QStringLiteral("Delete block"), QStringLiteral("删除积木"),
        QStringLiteral("Permanently delete a block from the page's block program."),
        QStringLiteral("从页面的积木程序中永久删除一个积木。"),
        [](const QJsonObject &arguments, ToolContext &context, QString *error) -> ToolResult {
            if (!context.project) {
                if (error)
                    *error = bilingual(QStringLiteral("No project is open."),
                                       QStringLiteral("当前没有打开工程。"));
                return ToolResult();
            }
            const QString blockId = arguments.value(QStringLiteral("blockId")).toString().trimmed();
            if (blockId.isEmpty()) {
                if (error)
                    *error = bilingual(QStringLiteral("\"blockId\" is required."),
                                       QStringLiteral("\"blockId\" 为必填项。"));
                return ToolResult();
            }
            bool isHat = false;
            BlockScript *script = findScriptFor(context.project->program(), blockId, &isHat);
            if (!script) {
                if (error)
                    *error = bilingual(
                        QStringLiteral("No block or script has the id \"%1\".").arg(blockId),
                        QStringLiteral("没有 id 为 \"%1\" 的积木或脚本。").arg(blockId));
                return ToolResult();
            }

            bool removed = false;
            QString type;
            if (isHat || (script->hat && script->hat->id == blockId)) {
                //  Deleting a hat means deleting the whole script. Only public
                //  APIs are used: the statements are detached first, then the
                //  script is removed from the program and destroyed.
                //  删除帽子块等同于删除整个脚本。这里只使用公开接口：先分离全部
                //  语句，再把脚本从程序列表中移除并销毁。
                for (int i = script->blockCount() - 1; i >= 0; --i)
                    script->takeBlock(i);
                QVector<BlockScript *> &program = context.project->program();
                for (int i = 0; i < program.size(); ++i) {
                    if (program.at(i) != script)
                        continue;
                    type = script->eventType;
                    program.remove(i);
                    delete script;
                    removed = true;
                    break;
                }
            } else {
                for (int i = 0; i < script->body.size(); ++i) {
                    Block *block = script->body.at(i);
                    if (!block)
                        continue;
                    bool match = block->id == blockId;
                    if (!match) {
                        block->forEach([&match, &blockId](Block *nested) {
                            if (!match && nested && nested->id == blockId)
                                match = true;
                        });
                    }
                    if (match) {
                        std::unique_ptr<Block> owned = script->takeBlock(i);
                        if (owned) {
                            type = owned->type;
                            removed = true;
                        }
                        break;
                    }
                }
            }

            if (!removed) {
                if (error)
                    *error = bilingual(QStringLiteral("The block could not be removed."),
                                       QStringLiteral("无法移除该积木。"));
                return ToolResult();
            }
            context.project->touch();
            return okResult(QJsonObject{
                { QStringLiteral("deleted"), blockId },
                { QStringLiteral("type"), type },
                { QStringLiteral("scriptId"), script->id }
            });
        });

    add(QStringLiteral("set_block_field"), QStringLiteral("blocks"), QStringLiteral("mutates"),
        schemaObject(QJsonObject{
                         { QStringLiteral("blockId"),
                           propString(QStringLiteral("Id of the block to change.")) },
                         { QStringLiteral("field"),
                           propString(QStringLiteral("Field name from list_available_blocks.")) },
                         { QStringLiteral("value"),
                           propString(QStringLiteral("New field value as text.")) }
                     },
                     { QStringLiteral("blockId"), QStringLiteral("field"),
                       QStringLiteral("value") }),
        QStringLiteral("Set block field"), QStringLiteral("设置积木字段"),
        QStringLiteral("Change one inline field (dropdown, number or text slot) of an existing "
                       "block."),
        QStringLiteral("修改已有积木的一个内联字段（下拉、数字或文本槽）。"),
        [](const QJsonObject &arguments, ToolContext &context, QString *error) -> ToolResult {
            if (!context.project) {
                if (error)
                    *error = bilingual(QStringLiteral("No project is open."),
                                       QStringLiteral("当前没有打开工程。"));
                return ToolResult();
            }
            const QString blockId = arguments.value(QStringLiteral("blockId")).toString().trimmed();
            const QString field = arguments.value(QStringLiteral("field")).toString().trimmed();
            if (blockId.isEmpty() || field.isEmpty()) {
                if (error)
                    *error = bilingual(QStringLiteral("\"blockId\" and \"field\" are required."),
                                       QStringLiteral("\"blockId\" 与 \"field\" 为必填项。"));
                return ToolResult();
            }

            Block *block = findBlockInProgram(context.project->program(), blockId);
            if (!block) {
                if (error)
                    *error = bilingual(
                        QStringLiteral("No block has the id \"%1\".").arg(blockId),
                        QStringLiteral("没有 id 为 \"%1\" 的积木。").arg(blockId));
                return ToolResult();
            }
            const BlockDef *definition = BlockRegistry::instance().find(block->type);
            if (definition) {
                bool known = false;
                for (const BlockFieldDef &fieldDef : definition->fields) {
                    if (fieldDef.name == field) {
                        known = true;
                        break;
                    }
                }
                if (!known && !definition->inputs.isEmpty()) {
                    for (const BlockInputDef &inputDef : definition->inputs) {
                        if (inputDef.name == field) {
                            known = true;
                            break;
                        }
                    }
                }
                if (!known) {
                    QStringList names;
                    for (const BlockFieldDef &fieldDef : definition->fields)
                        names.append(fieldDef.name);
                    if (error)
                        *error = bilingual(
                            QStringLiteral("The block type \"%1\" has no field \"%2\". Known "
                                           "fields: %3")
                                .arg(block->type, field,
                                     names.isEmpty() ? QStringLiteral("(none)")
                                                     : names.join(QStringLiteral(", "))),
                            QStringLiteral("积木类型 \"%1\" 没有字段 \"%2\"。已知字段：%3")
                                .arg(block->type, field,
                                     names.isEmpty() ? QStringLiteral("（无）")
                                                     : names.join(QStringLiteral("、"))));
                    return ToolResult();
                }
            }

            const QString previous = block->fieldValue(field);
            block->setFieldValue(field, arguments.value(QStringLiteral("value")).toString());
            context.project->touch();
            return okResult(QJsonObject{
                { QStringLiteral("blockId"), blockId },
                { QStringLiteral("type"), block->type },
                { QStringLiteral("field"), field },
                { QStringLiteral("previousValue"), previous },
                { QStringLiteral("value"), block->fieldValue(field) }
            });
        });

    // =======================================================================
    //  code
    // =======================================================================

    add(QStringLiteral("get_generated_code"), QStringLiteral("code"), QString(),
        schemaObject(QJsonObject{
                         { QStringLiteral("pageId"),
                           propString(QStringLiteral("Page id. Omit for the active page.")) },
                         { QStringLiteral("kind"),
                           propEnum(QStringLiteral("Which artefact to return. Default \"html\"."),
                                    { QStringLiteral("html"), QStringLiteral("javascript"),
                                      QStringLiteral("css") }) }
                     }),
        QStringLiteral("Get generated code"), QStringLiteral("获取生成代码"),
        QStringLiteral("Generate and return the preview code for one page without writing any "
                       "file. Use it to check the result of an edit."),
        QStringLiteral("生成并返回某个页面的预览代码，不写入任何文件。用它检查编辑结果。"),
        [](const QJsonObject &arguments, ToolContext &context, QString *error) -> ToolResult {
            Page *page = resolvePage(context, arguments.value(QStringLiteral("pageId")).toString(),
                                     error, nullptr);
            if (!page)
                return ToolResult();
            if (!context.project) {
                if (error)
                    *error = bilingual(QStringLiteral("No project is open."),
                                       QStringLiteral("当前没有打开工程。"));
                return ToolResult();
            }

            EmitOptions options;
            options.siteTitle = context.project->name;
            options.siteDescription = context.project->description;
            options.author = context.project->author;
            options.language = page->language.isEmpty() ? context.project->defaultLanguage
                                                        : page->language;
            CodeGenerator generator(options);

            const QString kind =
                arguments.value(QStringLiteral("kind")).toString().trimmed().toLower();
            QString code;
            if (kind == QStringLiteral("javascript") || kind == QStringLiteral("js"))
                code = generator.generateJavaScript(*context.project, page->id);
            else if (kind == QStringLiteral("css"))
                code = generator.generateCss(*context.project, page->id);
            else
                code = generator.generateHtml(*context.project, page->id);

            const int fullLength = code.size();
            const bool truncated = fullLength > kMaxCodeChars;
            if (truncated)
                code = code.left(kMaxCodeChars);

            return okResult(QJsonObject{
                { QStringLiteral("pageId"), page->id },
                { QStringLiteral("kind"), kind.isEmpty() ? QStringLiteral("html") : kind },
                { QStringLiteral("chars"), fullLength },
                { QStringLiteral("truncated"), truncated },
                { QStringLiteral("code"), code }
            });
        });

    add(QStringLiteral("export_site"), QStringLiteral("code"), QStringLiteral("mutates"),
        schemaObject(QJsonObject{
                         { QStringLiteral("directory"),
                           propString(QStringLiteral("Output folder relative to the workspace "
                                                     "root. Default \"build\".")) },
                         { QStringLiteral("mode"),
                           propEnum(QStringLiteral("Export shape. Default \"single-html\"."),
                                    { QStringLiteral("single-html"),
                                      QStringLiteral("html-with-assets"),
                                      QStringLiteral("script-only"),
                                      QStringLiteral("headless-bundle") }) },
                         { QStringLiteral("pretty"),
                           propBoolean(QStringLiteral("Indent the generated markup. Default "
                                                      "true.")) },
                         { QStringLiteral("minifyCss"),
                           propBoolean(QStringLiteral("Minify the generated CSS. Default "
                                                      "false.")) },
                         { QStringLiteral("minifyJs"),
                           propBoolean(QStringLiteral("Minify the generated JavaScript. Default "
                                                      "false.")) }
                     }),
        QStringLiteral("Export site"), QStringLiteral("导出站点"),
        QStringLiteral("Generate the whole project and write the files into a folder inside the "
                       "workspace. Refuses any path outside the workspace."),
        QStringLiteral("生成整个工程并将文件写入工作区内的文件夹。拒绝工作区之外的任何路径。"),
        [](const QJsonObject &arguments, ToolContext &context, QString *error) -> ToolResult {
            if (!context.project) {
                if (error)
                    *error = bilingual(QStringLiteral("No project is open."),
                                       QStringLiteral("当前没有打开工程。"));
                return ToolResult();
            }
            QString directory =
                arguments.value(QStringLiteral("directory")).toString().trimmed();
            if (directory.isEmpty())
                directory = QStringLiteral("build");

            //  Never write outside the workspace the user granted.
            //  绝不写出用户授权的工作区之外。
            const QDir workspace(context.workspaceDirectory.isEmpty()
                                     ? QDir::currentPath()
                                     : context.workspaceDirectory);
            if (!workspace.exists()) {
                if (error)
                    *error = bilingual(
                        QStringLiteral("The workspace directory \"%1\" does not exist.")
                            .arg(context.workspaceDirectory),
                        QStringLiteral("工作区目录 \"%1\" 不存在。").arg(context.workspaceDirectory));
                return ToolResult();
            }
            const QString cleaned = QDir::cleanPath(workspace.absoluteFilePath(directory));
            const QString workspaceRoot = QDir::cleanPath(workspace.absolutePath());
            if (cleaned != workspaceRoot
                && !cleaned.startsWith(workspaceRoot + QLatin1Char('/'))) {
                if (error)
                    *error = bilingual(
                        QStringLiteral("\"%1\" resolves outside the workspace; refusing to write "
                                       "there.").arg(directory),
                        QStringLiteral("\"%1\" 解析到工作区之外；拒绝写入该位置。").arg(directory));
                return ToolResult();
            }

            EmitOptions options;
            options.mode = exportModeFromTool(arguments.value(QStringLiteral("mode")).toString());
            options.pretty = arguments.value(QStringLiteral("pretty")).toBool(true);
            options.minifyCss = arguments.value(QStringLiteral("minifyCss")).toBool(false);
            options.minifyJs = arguments.value(QStringLiteral("minifyJs")).toBool(false);
            options.siteTitle = context.project->name;
            options.siteDescription = context.project->description;
            options.author = context.project->author;
            options.language = context.project->defaultLanguage;

            CodeGenerator generator(options);
            const EmitResult emitted = generator.generateProject(*context.project);
            if (!emitted.ok) {
                if (error)
                    *error = bilingual(
                        QStringLiteral("Code generation failed: %1")
                            .arg(emitted.errors.isEmpty()
                                     ? QStringLiteral("unknown reason")
                                     : emitted.errors.mid(0, 3).join(QStringLiteral("; "))),
                        QStringLiteral("代码生成失败：%1")
                            .arg(emitted.errors.isEmpty()
                                     ? QStringLiteral("原因未知")
                                     : emitted.errors.mid(0, 3).join(QStringLiteral("；"))));
                return ToolResult();
            }

            QStringList writeErrors;
            const bool written = CodeGenerator::writeToDirectory(emitted, cleaned, &writeErrors);
            QJsonArray files;
            for (const EmittedFile &file : emitted.files) {
                QJsonObject entry;
                entry.insert(QStringLiteral("path"), file.relativePath);
                entry.insert(QStringLiteral("bytes"), file.size());
                files.append(entry);
            }
            if (!written) {
                if (error)
                    *error = bilingual(
                        QStringLiteral("The files could not be written: %1")
                            .arg(writeErrors.mid(0, 3).join(QStringLiteral("; "))),
                        QStringLiteral("无法写入文件：%1")
                            .arg(writeErrors.mid(0, 3).join(QStringLiteral("；"))));
                return ToolResult();
            }
            if (context.project)
                context.project->touch();
            QJsonObject payload{
                { QStringLiteral("directory"), cleaned },
                { QStringLiteral("mode"), toString(options.mode) },
                { QStringLiteral("fileCount"), files.size() },
                { QStringLiteral("totalBytes"), emitted.totalBytes() },
                { QStringLiteral("files"), files }
            };
            if (!emitted.warnings.isEmpty())
                payload.insert(QStringLiteral("warnings"),
                               QJsonArray::fromStringList(emitted.warnings.mid(0, 5)));
            return okResult(payload);
        });

    // =======================================================================
    //  backend
    // =======================================================================

    add(QStringLiteral("list_endpoints"), QStringLiteral("backend"), QString(),
        schemaObject(QJsonObject{
                         { QStringLiteral("includeDisabled"),
                           propBoolean(QStringLiteral("Include disabled endpoints. Default "
                                                      "false.")) }
                     }),
        QStringLiteral("List endpoints"), QStringLiteral("列出端点"),
        QStringLiteral("List the backend endpoints the generated page can call. Secrets are never "
                       "returned; authentication is reported by mode only."),
        QStringLiteral("列出生成的页面可调用的后端端点。绝不返回机密；授权方式仅以模式表示。"),
        [](const QJsonObject &arguments, ToolContext &context, QString *error) -> ToolResult {
            if (!context.project) {
                if (error)
                    *error = bilingual(QStringLiteral("No project is open."),
                                       QStringLiteral("当前没有打开工程。"));
                return ToolResult();
            }
            const bool includeDisabled =
                arguments.value(QStringLiteral("includeDisabled")).toBool(false);
            QJsonArray endpoints;
            for (const QJsonValue &value : endpointsOf(context.project)) {
                const Endpoint endpoint = Endpoint::fromJson(value.toObject());
                if (!includeDisabled && !endpoint.enabled)
                    continue;
                QJsonObject entry;
                entry.insert(QStringLiteral("id"), endpoint.id);
                entry.insert(QStringLiteral("name"), endpoint.name);
                entry.insert(QStringLiteral("method"), endpoint.methodUpper());
                entry.insert(QStringLiteral("url"), endpoint.url);
                entry.insert(QStringLiteral("contentType"), endpoint.contentType);
                entry.insert(QStringLiteral("enabled"), endpoint.enabled);
                entry.insert(QStringLiteral("isGraphQl"), endpoint.isGraphQl);
                entry.insert(QStringLiteral("timeoutMs"), endpoint.timeoutMs);
                //  Report only whether a credential reference exists.
                //  仅报告是否存在凭据引用。
                entry.insert(QStringLiteral("authMode"), toString(endpoint.auth.mode));
                entry.insert(QStringLiteral("hasSecretReference"),
                             !endpoint.auth.secretKey.isEmpty());
                endpoints.append(entry);
            }
            return okResult(QJsonObject{
                { QStringLiteral("count"), endpoints.size() },
                { QStringLiteral("endpoints"), endpoints }
            });
        });

    add(QStringLiteral("add_endpoint"), QStringLiteral("backend"), QStringLiteral("mutates"),
        schemaObject(QJsonObject{
                         { QStringLiteral("name"),
                           propString(QStringLiteral("Short endpoint name, for example "
                                                     "\"create-user\".")) },
                         { QStringLiteral("url"),
                           propString(QStringLiteral("Request URL. May contain {{placeholders}}.")) },
                         { QStringLiteral("method"),
                           propEnum(QStringLiteral("HTTP method. Default \"GET\"."),
                                    { QStringLiteral("GET"), QStringLiteral("POST"),
                                      QStringLiteral("PUT"), QStringLiteral("PATCH"),
                                      QStringLiteral("DELETE") }) },
                         { QStringLiteral("contentType"),
                           propString(QStringLiteral("Request content type. Default "
                                                     "\"application/json\".")) },
                         { QStringLiteral("bodyTemplate"),
                           propString(QStringLiteral("Request body template for POST/PUT/PATCH.")) },
                         { QStringLiteral("description"),
                           propString(QStringLiteral("What the endpoint does.")) },
                         { QStringLiteral("headers"),
                           propObject(QStringLiteral("Extra request headers. Never put a "
                                                     "credential here.")) },
                         { QStringLiteral("timeoutMs"),
                           propNumber(QStringLiteral("Request timeout in milliseconds. Default "
                                                     "30000."), 1000, 600000) }
                     },
                     { QStringLiteral("name"), QStringLiteral("url") }),
        QStringLiteral("Add endpoint"), QStringLiteral("添加端点"),
        QStringLiteral("Add a REST endpoint to the project's pre-linked backend. Credentials must "
                       "be referenced by a secret key, never passed in plain text."),
        QStringLiteral("向工程的预链接后端添加一个 REST 端点。凭据必须通过机密键引用，"
                       "绝不能以明文传入。"),
        [](const QJsonObject &arguments, ToolContext &context, QString *error) -> ToolResult {
            if (!context.project) {
                if (error)
                    *error = bilingual(QStringLiteral("No project is open."),
                                       QStringLiteral("当前没有打开工程。"));
                return ToolResult();
            }
            Endpoint endpoint;
            endpoint.name = arguments.value(QStringLiteral("name")).toString().trimmed();
            endpoint.url = arguments.value(QStringLiteral("url")).toString().trimmed();
            if (endpoint.name.isEmpty() || endpoint.url.isEmpty()) {
                if (error)
                    *error = bilingual(QStringLiteral("\"name\" and \"url\" are required."),
                                       QStringLiteral("\"name\" 与 \"url\" 为必填项。"));
                return ToolResult();
            }
            const QString method = arguments.value(QStringLiteral("method")).toString().trimmed();
            endpoint.method = method.isEmpty() ? QStringLiteral("GET") : method.toUpper();
            const QString contentType =
                arguments.value(QStringLiteral("contentType")).toString().trimmed();
            if (!contentType.isEmpty())
                endpoint.contentType = contentType;
            endpoint.bodyTemplate =
                arguments.value(QStringLiteral("bodyTemplate")).toString();
            endpoint.descriptionEn = arguments.value(QStringLiteral("description")).toString();
            endpoint.descriptionZh = endpoint.descriptionEn;
            endpoint.timeoutMs = arguments.contains(QStringLiteral("timeoutMs"))
                                     ? arguments.value(QStringLiteral("timeoutMs")).toInt()
                                     : 30000;
            endpoint.id = IdGenerator::randomToken(6);
            if (endpoint.id.isEmpty())
                endpoint.id = QStringLiteral("ep-%1").arg(IdGenerator::next(QStringLiteral("ep")));

            //  A credential typed into this tool would end up in the project file
            //  and in the transcript, so header values are validated.
            //  在此工具中输入凭据会使其进入工程文件与会话记录，因此对请求头的值做校验。
            if (arguments.contains(QStringLiteral("headers"))) {
                const QJsonObject headers = arguments.value(QStringLiteral("headers")).toObject();
                for (auto it = headers.constBegin(); it != headers.constEnd(); ++it) {
                    const QString value = it.value().toString();
                    if (looksLikeSecret(value)) {
                        if (error)
                            *error = bilingual(
                                QStringLiteral("The header \"%1\" looks like a credential. Store "
                                               "it in the secret vault and reference it by key "
                                               "instead.").arg(it.key()),
                                QStringLiteral("请求头 \"%1\" 看起来是凭据。请将其存入机密保险库"
                                               "并通过键引用。").arg(it.key()));
                        return ToolResult();
                    }
                    endpoint.headers.insert(it.key(), value);
                }
            }

            QString why;
            if (!endpoint.isValid(&why)) {
                if (error)
                    *error = bilingual(QStringLiteral("The endpoint is not valid: %1").arg(why),
                                       QStringLiteral("端点无效：%1").arg(why));
                return ToolResult();
            }

            QJsonArray list = endpointsOf(context.project);
            list.append(endpoint.toJson());
            storeEndpoints(context.project, list);
            context.project->touch();
            return okResult(QJsonObject{
                { QStringLiteral("endpointId"), endpoint.id },
                { QStringLiteral("name"), endpoint.name },
                { QStringLiteral("method"), endpoint.methodUpper() },
                { QStringLiteral("url"), endpoint.url },
                { QStringLiteral("total"), list.size() }
            });
        });

    add(QStringLiteral("delete_endpoint"), QStringLiteral("backend"),
        QStringLiteral("mutates destructive"),
        schemaObject(QJsonObject{
                         { QStringLiteral("endpointId"),
                           propString(QStringLiteral("Endpoint id from list_endpoints.")) }
                     },
                     { QStringLiteral("endpointId") }),
        QStringLiteral("Delete endpoint"), QStringLiteral("删除端点"),
        QStringLiteral("Remove a backend endpoint from the project. Blocks that referenced it "
                       "will no longer be pre-linked."),
        QStringLiteral("从工程中移除一个后端端点。曾引用它的积木将不再被预链接。"),
        [](const QJsonObject &arguments, ToolContext &context, QString *error) -> ToolResult {
            if (!context.project) {
                if (error)
                    *error = bilingual(QStringLiteral("No project is open."),
                                       QStringLiteral("当前没有打开工程。"));
                return ToolResult();
            }
            const QString id = arguments.value(QStringLiteral("endpointId")).toString().trimmed();
            if (id.isEmpty()) {
                if (error)
                    *error = bilingual(QStringLiteral("\"endpointId\" is required."),
                                       QStringLiteral("\"endpointId\" 为必填项。"));
                return ToolResult();
            }
            const QJsonArray list = endpointsOf(context.project);
            QJsonArray remaining;
            QString removedName;
            for (const QJsonValue &value : list) {
                const Endpoint endpoint = Endpoint::fromJson(value.toObject());
                if (endpoint.id == id) {
                    removedName = endpoint.name;
                    continue;
                }
                remaining.append(value);
            }
            if (removedName.isEmpty() && remaining.size() == list.size()) {
                if (error)
                    *error = bilingual(
                        QStringLiteral("No endpoint has the id \"%1\".").arg(id),
                        QStringLiteral("没有 id 为 \"%1\" 的端点。").arg(id));
                return ToolResult();
            }
            storeEndpoints(context.project, remaining);
            context.project->touch();
            return okResult(QJsonObject{
                { QStringLiteral("deleted"), id },
                { QStringLiteral("name"), removedName },
                { QStringLiteral("remaining"), remaining.size() }
            });
        });

    // =======================================================================
    //  ai / files
    // =======================================================================

    add(QStringLiteral("set_ai_system_prompt"), QStringLiteral("ai"), QStringLiteral("mutates"),
        schemaObject(QJsonObject{
                         { QStringLiteral("prompt"),
                           propString(QStringLiteral("Replacement system prompt for this project's "
                                                     "assistant.")) },
                         { QStringLiteral("append"),
                           propBoolean(QStringLiteral("Append to the current prompt instead of "
                                                      "replacing it. Default false.")) }
                     },
                     { QStringLiteral("prompt") }),
        QStringLiteral("Set assistant prompt"), QStringLiteral("设置助手提示词"),
        QStringLiteral("Store a project-specific system prompt for the TI-UseAgent assistant in "
                       "the project file, so it travels with the project."),
        QStringLiteral("把项目专用的 TI-UseAgent 系统提示词保存到工程文件中，"
                       "使其随工程一起携带。"),
        [](const QJsonObject &arguments, ToolContext &context, QString *error) -> ToolResult {
            if (!context.project) {
                if (error)
                    *error = bilingual(QStringLiteral("No project is open."),
                                       QStringLiteral("当前没有打开工程。"));
                return ToolResult();
            }
            const QString prompt = arguments.value(QStringLiteral("prompt")).toString();
            if (prompt.trimmed().isEmpty()) {
                if (error)
                    *error = bilingual(QStringLiteral("\"prompt\" cannot be empty."),
                                       QStringLiteral("\"prompt\" 不能为空。"));
                return ToolResult();
            }
            const bool append = arguments.value(QStringLiteral("append")).toBool(false);
            QString stored = context.project->aiConfig
                                 .value(QStringLiteral("systemPrompt")).toString();
            stored = append && !stored.isEmpty()
                         ? stored + QStringLiteral("\n\n") + prompt
                         : prompt;

            QJsonObject config = context.project->aiConfig;
            config.insert(QStringLiteral("systemPrompt"), stored);
            context.project->aiConfig = config;
            context.project->touch();
            return okResult(QJsonObject{
                { QStringLiteral("chars"), stored.size() },
                { QStringLiteral("appended"), append }
            });
        });

    add(QStringLiteral("write_workspace_file"), QStringLiteral("files"), QStringLiteral("mutates"),
        schemaObject(QJsonObject{
                         { QStringLiteral("path"),
                           propString(QStringLiteral("File path relative to the workspace root, "
                                                     "for example \"assets/notes.txt\".")) },
                         { QStringLiteral("content"),
                           propString(QStringLiteral("Text to write.")) },
                         { QStringLiteral("append"),
                           propBoolean(QStringLiteral("Append instead of overwrite. Default "
                                                      "false.")) }
                     },
                     { QStringLiteral("path"), QStringLiteral("content") }),
        QStringLiteral("Write workspace file"), QStringLiteral("写入工作区文件"),
        QStringLiteral("Write a small text file inside the workspace, for example a note or an "
                       "asset manifest. Paths outside the workspace are refused."),
        QStringLiteral("在工作区内写入一个小型文本文件，例如说明或资源清单。"
                       "工作区之外的路径会被拒绝。"),
        [](const QJsonObject &arguments, ToolContext &context, QString *error) -> ToolResult {
            const QString relative = arguments.value(QStringLiteral("path")).toString().trimmed();
            if (relative.isEmpty()) {
                if (error)
                    *error = bilingual(QStringLiteral("\"path\" is required."),
                                       QStringLiteral("\"path\" 为必填项。"));
                return ToolResult();
            }
            const QString content = arguments.value(QStringLiteral("content")).toString();
            if (content.size() > 1024 * 1024) {
                if (error)
                    *error = bilingual(
                        QStringLiteral("The content is larger than the 1 MiB limit for this tool."),
                        QStringLiteral("内容超过了本工具 1 MiB 的上限。"));
                return ToolResult();
            }
            if (looksLikeSecret(content)) {
                if (error)
                    *error = bilingual(
                        QStringLiteral("The content looks like a credential and was refused; store "
                                       "secrets in the secret vault instead."),
                        QStringLiteral("内容看起来是凭据，已被拒绝；请把机密存入机密保险库。"));
                return ToolResult();
            }

            const QDir workspace(context.workspaceDirectory.isEmpty() ? QDir::currentPath()
                                                                      : context.workspaceDirectory);
            if (!workspace.exists()) {
                if (error)
                    *error = bilingual(
                        QStringLiteral("The workspace directory \"%1\" does not exist.")
                            .arg(context.workspaceDirectory),
                        QStringLiteral("工作区目录 \"%1\" 不存在。").arg(context.workspaceDirectory));
                return ToolResult();
            }
            const QString cleaned = QDir::cleanPath(workspace.absoluteFilePath(relative));
            const QString workspaceRoot = QDir::cleanPath(workspace.absolutePath());
            if (!cleaned.startsWith(workspaceRoot + QLatin1Char('/'))) {
                if (error)
                    *error = bilingual(
                        QStringLiteral("\"%1\" resolves outside the workspace; refusing to write "
                                       "there.").arg(relative),
                        QStringLiteral("\"%1\" 解析到工作区之外；拒绝写入该位置。").arg(relative));
                return ToolResult();
            }

            const QFileInfo info(cleaned);
            if (!QDir().mkpath(info.absolutePath())) {
                if (error)
                    *error = bilingual(
                        QStringLiteral("The folder \"%1\" could not be created.").arg(info.absolutePath()),
                        QStringLiteral("无法创建文件夹 \"%1\"。").arg(info.absolutePath()));
                return ToolResult();
            }

            QFile file(cleaned);
            QIODevice::OpenMode mode = QIODevice::WriteOnly | QIODevice::Text;
            if (arguments.value(QStringLiteral("append")).toBool(false))
                mode |= QIODevice::Append;
            else
                mode |= QIODevice::Truncate;
            if (!file.open(mode)) {
                if (error)
                    *error = bilingual(
                        QStringLiteral("The file \"%1\" could not be opened for writing.")
                            .arg(relative),
                        QStringLiteral("无法以写入方式打开文件 \"%1\"。").arg(relative));
                return ToolResult();
            }
            const qint64 writtenBytes = file.write(content.toUtf8());
            file.close();
            if (writtenBytes < 0) {
                if (error)
                    *error = bilingual(QStringLiteral("The write to \"%1\" failed.").arg(relative),
                                       QStringLiteral("写入 \"%1\" 失败。").arg(relative));
                return ToolResult();
            }
            if (context.project)
                context.project->touch();
            return okResult(QJsonObject{
                { QStringLiteral("path"), QDir(workspaceRoot).relativeFilePath(cleaned) },
                { QStringLiteral("bytes"), writtenBytes },
                { QStringLiteral("appended"),
                  arguments.value(QStringLiteral("append")).toBool(false) }
            });
        });
}

} // namespace tcbw
