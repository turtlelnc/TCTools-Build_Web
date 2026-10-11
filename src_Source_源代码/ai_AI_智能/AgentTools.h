// ============================================================================
//  AgentTools.h -- the capabilities the AI is allowed to use on the platform
//  AgentTools.h —— AI 在本平台上被允许使用的能力
//
//  This is the security-critical seam of the product. The AI never touches the
//  document directly: every change goes through a declared tool with a JSON
//  Schema, an explicit mutation flag and a confirmation policy.
//  这是产品中与安全密切相关的接缝。AI 从不直接操作文档：每次变更都必须经过
//  声明的工具，这些工具带有 JSON Schema、明确的变更标记以及确认策略。
// ============================================================================
#pragma once

#include "AiTypes.h"
#include "McpClient.h"

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>

#include <functional>
#include <memory>

namespace tcbw {

class ProjectDocument;
class BlockScript;

// ---------------------------------------------------------------------------
//  ApprovalPolicy
//  审批策略
// ---------------------------------------------------------------------------
enum class ApprovalPolicy {
    Never = 0,        // refuse every mutating tool / 拒绝所有变更类工具
    SafeOnly,         // allow non-destructive mutations automatically
    AskEveryChange,   // require confirmation for every mutation
    AskDestructive    // allow additive changes, ask before deletes
};

// ---------------------------------------------------------------------------
//  ToolContext
//  Everything a tool handler may need.
//  工具处理函数可能需要的一切。
// ---------------------------------------------------------------------------
struct ToolContext
{
    ProjectDocument *project = nullptr;
    QString activePageId;
    QString activeNodeId;
    QString workspaceDirectory;
    bool chinese = false;
};

// ---------------------------------------------------------------------------
//  AgentToolRegistry
//  工具注册表
// ---------------------------------------------------------------------------
class AgentToolRegistry : public QObject
{
    Q_OBJECT

public:
    using Handler = std::function<ToolResult(const QJsonObject &arguments,
                                             ToolContext &context,
                                             QString *error)>;
    using ApprovalCallback = std::function<bool(const ToolCall &call, const ToolDescriptor &desc)>;

    explicit AgentToolRegistry(QObject *parent = nullptr);
    ~AgentToolRegistry() override;

    // --- registration ------------------------------------------------------
    void registerTool(const ToolDescriptor &descriptor, Handler handler);

    //  Registers every built-in project/AI/codegen tool. Handlers operate on
    //  the ToolContext, so this is pure plumbing and fully unit-testable.
    //  注册全部内置的工程/AI/代码生成工具。处理函数基于 ToolContext 工作，
    //  因此这里只是纯粹的接线，可完整单元测试。
    void registerBuiltinTools();

    //  Attaches an MCP client; its tools become available under the
    //  `mcp_<server>_<tool>` namespace.
    //  挂接 MCP 客户端；其工具将以 `mcp_<server>_<tool>` 命名空间提供。
    void attachMcpClient(McpClient *client);

    // --- discovery ---------------------------------------------------------
    QVector<ToolDescriptor> descriptors() const;
    QVector<ToolDescriptor> descriptorsForModel() const;   // English only / 仅英文
    const ToolDescriptor *find(const QString &name) const;
    int toolCount() const;

    // --- invocation --------------------------------------------------------
    ToolResult invoke(const ToolCall &call, ToolContext &context);
    bool isMutating(const QString &toolName) const;
    bool isDestructive(const QString &toolName) const;

    // --- policy ------------------------------------------------------------
    void setApprovalPolicy(ApprovalPolicy policy) { m_policy = policy; }
    ApprovalPolicy approvalPolicy() const { return m_policy; }
    void setApprovalCallback(ApprovalCallback callback) { m_approval = std::move(callback); }
    void setAllowedTools(const QStringList &names);   // empty == all
    void setBlockedTools(const QStringList &names);

    //  True when the tool may run under the current policy without asking.
    //  在当前策略下该工具是否可以不询问直接运行。
    bool isAutoApproved(const QString &toolName) const;

    QStringList invocationLog() const { return m_log; }
    void clearLog();

signals:
    void toolInvoked(const QString &name, bool success);
    void toolDenied(const QString &name, const QString &reason);

private:
    struct Entry
    {
        ToolDescriptor descriptor;
        Handler handler;
    };
    ToolResult invokeMcpTool(const ToolCall &call, const QString &serverId,
                             const QString &toolName);
    void registerDefaults();
    void logLine(const QString &line);

    QVector<Entry> m_entries;
    QHash<QString, int> m_index;
    QHash<QString, McpClient *> m_mcpClients;
    ApprovalPolicy m_policy = ApprovalPolicy::AskDestructive;
    ApprovalCallback m_approval;
    QStringList m_allowed;
    QStringList m_blocked;
    QStringList m_log;
};

// ---------------------------------------------------------------------------
//  Built-in tool JSON Schemas
//  Exposed separately so that tests and the documentation generator can read
//  them without instantiating a registry.
//  单独暴露，使测试与文档生成器无需实例化注册表即可读取。
// ---------------------------------------------------------------------------
namespace agenttemplates {

QJsonObject schemaObject(const QJsonObject &properties,
                         const QStringList &required = {});
QJsonObject propString(const QString &description, const QString &enumValue = QString());
QJsonObject propNumber(const QString &description, double min = 0, double max = 0);
QJsonObject propBoolean(const QString &description);
QJsonObject propEnum(const QString &description, const QStringList &values);
QJsonObject propArray(const QString &description, const QJsonObject &items);
QJsonObject propObject(const QString &description);

} // namespace agenttemplates

} // namespace tcbw
