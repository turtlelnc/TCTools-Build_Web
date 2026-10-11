// ============================================================================
//  AiTypes.h -- value types shared by the TI-UseAgent subsystem
//  AiTypes.h —— TI-UseAgent 子系统共享的值类型
// ============================================================================
#pragma once

#include "Types.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

namespace tcbw {

// ---------------------------------------------------------------------------
//  ModelInfo -- one model offered by a vendor
//  模型信息 —— 厂商提供的某个模型
// ---------------------------------------------------------------------------
struct ModelInfo
{
    QString id;                 // wire identifier / 接口标识符
    QString label;              // UI label / 界面标签
    ModelCapabilities caps = CapChat | CapStreaming;
    int contextWindow = 0;      // tokens, 0 == unknown / 令牌数，0 表示未知
    int maxOutputTokens = 0;
    double inputPricePerMillion = -1.0;   // USD, <0 == unknown
    double outputPricePerMillion = -1.0;
    bool deprecated = false;

    QJsonObject toJson() const;
    static ModelInfo fromJson(const QJsonObject &obj);
};

// ---------------------------------------------------------------------------
//  ToolCall / ToolResult
//  工具调用 / 工具结果
// ---------------------------------------------------------------------------
struct ToolCall
{
    QString id;
    QString name;
    QJsonObject arguments;

    QJsonObject toJson() const;
    static ToolCall fromJson(const QJsonObject &obj);
};

struct ToolResult
{
    QString toolCallId;
    QString content;
    bool isError = false;

    QJsonObject toJson() const;
};

// ---------------------------------------------------------------------------
//  ToolDescriptor -- one capability the AI may invoke
//  工具描述符 —— AI 可以调用的一项能力
// ---------------------------------------------------------------------------
struct ToolDescriptor
{
    QString name;                 // snake_case, stable / 下划线命名，保持稳定
    QString titleEn;
    QString titleZh;
    QString descriptionEn;        // shown to the model / 提供给模型
    QString descriptionZh;        // shown to the user / 提供给用户
    QJsonObject inputSchema;      // JSON Schema object / JSON Schema 对象
    bool mutatesProject = false;  // requires confirmation / 需要用户确认
    bool destructive = false;     // deletes user data / 会删除用户数据
    bool requiresNetwork = false;
    QString category = QStringLiteral("project");

    QJsonObject toOpenAiTool() const;
    QJsonObject toAnthropicTool() const;
};

// ---------------------------------------------------------------------------
//  ChatMessage
//  对话消息
// ---------------------------------------------------------------------------
struct ChatMessage
{
    ChatRole role = ChatRole::User;
    QString content;
    QString name;                 // tool name for role == Tool / 工具名
    QString toolCallId;
    QVector<ToolCall> toolCalls;
    QString reasoning;            // chain-of-thought, when exposed / 思维链
    QJsonArray imageParts;        // vision inputs / 视觉输入
    QDateTime timestamp;
    bool streaming = false;

    QJsonObject toJson() const;
    static ChatMessage fromJson(const QJsonObject &obj);

    static ChatMessage system(const QString &text);
    static ChatMessage user(const QString &text);
    static ChatMessage assistant(const QString &text);
    static ChatMessage tool(const QString &toolCallId, const QString &name,
                            const QString &content, bool isError = false);
};

// ---------------------------------------------------------------------------
//  VendorInfo -- one AI vendor preset
//  厂商信息 —— 一个 AI 厂商预设
// ---------------------------------------------------------------------------
struct VendorInfo
{
    QString id;                   // "openai", "anthropic", "deepseek", "qwen", ...
    QString labelEn;
    QString labelZh;
    QString region;               // "global" | "cn" | "local" / 地区
    QString homepageUrl;
    QString docsUrl;
    QString keysUrl;              // where the user creates an API key / 申请密钥页面
    QString baseUrl;              // default API root / 默认 API 根地址
    QString chatPath;             // path appended to baseUrl
    ApiFlavor flavor = ApiFlavor::OpenAiCompatible;

    QVector<AuthMode> authModes;
    QString preferredAuth = QStringLiteral("api-key");

    QString defaultModel;
    QVector<ModelInfo> models;

    // OAuth endpoints (empty when unsupported) / OAuth 端点（不支持时为空）
    QString oauthAuthorizeUrl;
    QString oauthTokenUrl;
    QString oauthDeviceCodeUrl;
    QString oauthRedirectUri;
    QString oauthScopes;
    QString oauthClientId;        // public client id when the vendor publishes one
    bool oauthRequiresUserClientId = false;
    bool oauthUsesPkce = true;

    // API-key metadata / API Key 元数据
    QString keyPrefixHint;
    int keyMinLength = 16;
    QStringList keyHeaderNames;   // e.g. {"Authorization"}, {"x-api-key"}

    bool supportsTools = true;
    bool supportsStreaming = true;
    bool supportsVision = false;
    bool supportsReasoning = false;
    bool isLocal = false;

    QStringList notesEn;
    QStringList notesZh;

    bool supports(AuthMode mode) const { return authModes.contains(mode); }
    const ModelInfo *model(const QString &id) const;
    QJsonObject toJson() const;
};

// ---------------------------------------------------------------------------
//  ProviderConfig -- the user's saved configuration for one vendor
//  厂商配置 —— 用户为某个厂商保存的配置
// ---------------------------------------------------------------------------
struct ProviderConfig
{
    QString vendorId;
    QString authMode = QStringLiteral("api-key");
    QString baseUrlOverride;
    QString model;
    QString organisation;         // OpenAI organisation / OpenAI 组织
    QString project;              // OpenAI project id
    QString azureDeployment;
    QString azureApiVersion = QStringLiteral("2024-10-21");
    QString awsRegion;
    QString systemPromptOverride;
    double temperature = 0.2;
    int maxTokens = 4096;
    bool streaming = true;
    bool toolsEnabled = true;
    bool autoApproveSafeTools = true;    // never covers destructive tools
    int requestTimeoutMs = 120000;

    QJsonObject toJson() const;
    static ProviderConfig fromJson(const QJsonObject &obj);

    QString effectiveBaseUrl(const VendorInfo &vendor) const;
};

// ---------------------------------------------------------------------------
//  McpServerConfig
//  MCP 服务器配置
// ---------------------------------------------------------------------------
struct McpServerConfig
{
    QString id;
    QString name;
    McpTransport transport = McpTransport::Stdio;
    bool enabled = true;
    bool trusted = false;          // user explicitly trusted it / 用户已明确信任

    // stdio / 标准输入输出方式
    QString command;
    QStringList arguments;
    QString workingDirectory;
    QMap<QString, QString> environment;

    // http / HTTP 方式
    QString url;
    QString bearerToken;
    QMap<QString, QString> headers;

    QStringList allowedTools;      // empty == all / 为空表示全部允许
    QStringList blockedTools;

    QJsonObject toJson() const;
    static McpServerConfig fromJson(const QJsonObject &obj);
};

// ---------------------------------------------------------------------------
//  AiError
//  AI 错误
// ---------------------------------------------------------------------------
struct AiError
{
    enum Kind { None = 0, Network, Auth, RateLimit, BadRequest, Server, Cancelled,
                Parse, Policy, Timeout, Unknown };
    Kind kind = None;
    int httpStatus = 0;
    QString messageEn;
    QString messageZh;
    QString detail;               // already redacted / 已遮蔽机密

    bool isValid() const { return kind != None; }
    QString text(bool chinese) const;
};

// ---------------------------------------------------------------------------
//  Shared helpers
//  共享辅助函数
// ---------------------------------------------------------------------------
QString toString(AiError::Kind kind);
QString defaultSystemPrompt(bool chinese);
QString redactForPrompt(const QString &text);
QJsonObject buildOpenAiToolSchema(const QVector<ToolDescriptor> &tools);
QJsonArray buildAnthropicToolSchema(const QVector<ToolDescriptor> &tools);

} // namespace tcbw
