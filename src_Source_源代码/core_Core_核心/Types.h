// ============================================================================
//  Types.h -- shared value types and enumerations
//  Types.h —— 共享值类型与枚举
//  Part of TCTools-Build_Web (TC-BuildWeb)
// ============================================================================
#pragma once

#include <QColor>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace tcbw {

// ---------------------------------------------------------------------------
//  Node / element kinds
//  节点 / 元素种类
// ---------------------------------------------------------------------------
enum class NodeKind {
    Root = 0,     // page root container / 页面根容器
    Element,      // plain HTML element / 普通 HTML 元素
    Component,    // reusable component instance / 可复用组件实例
    Text,         // raw text node / 纯文本节点
    Slot          // placeholder in a component template / 组件模板占位符
};

// ---------------------------------------------------------------------------
//  Element categories used by the palette
//  素材面板使用的元素分类
// ---------------------------------------------------------------------------
enum class ElementCategory {
    Layout = 0,
    Text,
    Media,
    Form,
    Data,
    Navigation,
    Advanced,
    Component,
    Custom
};

// ---------------------------------------------------------------------------
//  Layout positioning mode
//  布局定位模式
// ---------------------------------------------------------------------------
enum class LayoutMode {
    Flow = 0,   // normal document flow + flex/grid / 常规文档流 + flex/grid
    Free        // absolute positioning on a canvas / 画布绝对定位
};

// ---------------------------------------------------------------------------
//  Responsive breakpoints
//  响应式断点
// ---------------------------------------------------------------------------
enum class Breakpoint {
    Desktop = 0,
    Tablet,
    Mobile
};

// ---------------------------------------------------------------------------
//  Export mode -- the user chooses .html or .js
//  导出模式 —— 用户选择 .html 或 .js
// ---------------------------------------------------------------------------
enum class ExportMode {
    SingleHtml = 0,   // one self-contained .html file / 单个自包含 .html 文件
    HtmlWithAssets,   // .html + .css + .js / .html + .css + .js
    ScriptOnly,       // .js only (programmatic builder) / 仅 .js（代码构建器）
    HeadlessBundle    // .html + .js + .css + assets + manifest / 完整静态站点包
};

// ---------------------------------------------------------------------------
//  Units understood by the style engine
//  样式引擎支持的单位
// ---------------------------------------------------------------------------
enum class StyleUnit {
    Auto = 0,
    Px,
    Percent,
    Em,
    Rem,
    Vw,
    Vh,
    Vmin,
    Vmax,
    Fr,
    None
};

// ---------------------------------------------------------------------------
//  AI authorisation modes
//  AI 授权方式
// ---------------------------------------------------------------------------
enum class AuthMode {
    ApiKey = 0,     // static API key / 静态 API Key
    OAuthPkce,      // OAuth 2.0 authorization code + PKCE / 授权码 + PKCE
    OAuthDevice,    // OAuth 2.0 device authorization grant / 设备码授权
    Bearer,         // pre-issued bearer token / 预签发令牌
    AwsSigV4,       // AWS Signature V4 (Bedrock) / AWS 签名 V4
    None            // local / self-hosted, no auth / 本地部署，无需授权
};

// ---------------------------------------------------------------------------
//  Wire formats an AI vendor may speak
//  AI 厂商可能使用的接口协议
// ---------------------------------------------------------------------------
enum class ApiFlavor {
    OpenAiCompatible = 0,  // POST /chat/completions
    Anthropic,             // POST /v1/messages
    GoogleGemini,          // POST /v1beta/models/{m}:generateContent
    Ollama,                // POST /api/chat
    AwsBedrock,
    Custom
};

// ---------------------------------------------------------------------------
//  Model capability flags
//  模型能力标记
// ---------------------------------------------------------------------------
enum ModelCapability {
    CapNone        = 0x00,
    CapChat        = 0x01,
    CapStreaming   = 0x02,
    CapTools       = 0x04,   // function/tool calling / 函数/工具调用
    CapVision      = 0x08,
    CapReasoning   = 0x10,
    CapEmbeddings  = 0x20,
    CapImage       = 0x40
};
Q_DECLARE_FLAGS(ModelCapabilities, ModelCapability)
Q_DECLARE_OPERATORS_FOR_FLAGS(ModelCapabilities)

// ---------------------------------------------------------------------------
//  MCP transport
//  MCP 传输方式
// ---------------------------------------------------------------------------
enum class McpTransport {
    Stdio = 0,   // child process, newline-delimited JSON-RPC / 子进程，换行分隔 JSON-RPC
    HttpSse,     // HTTP + Server-Sent Events / HTTP + SSE
    HttpJson     // plain streamable HTTP JSON / 普通流式 HTTP JSON
};

// ---------------------------------------------------------------------------
//  Chat message roles
//  对话消息角色
// ---------------------------------------------------------------------------
enum class ChatRole {
    System = 0,
    User,
    Assistant,
    Tool
};

// ---------------------------------------------------------------------------
//  Theme mode
//  主题模式
// ---------------------------------------------------------------------------
enum class ThemeMode {
    Light = 0,
    Dark,
    Auto
};

// ---------------------------------------------------------------------------
//  Free helper functions
//  自由辅助函数
// ---------------------------------------------------------------------------

// Enum <-> stable string conversion (used by the JSON project format).
// 枚举与稳定字符串互转（用于 JSON 工程格式）。
QString toString(NodeKind v);
QString toString(ElementCategory v);
QString toString(LayoutMode v);
QString toString(Breakpoint v);
QString toString(ExportMode v);
QString toString(StyleUnit v);
QString toString(AuthMode v);
QString toString(ApiFlavor v);
QString toString(McpTransport v);
QString toString(ChatRole v);
QString toString(ThemeMode v);

NodeKind nodeKindFromString(const QString &s, NodeKind fallback = NodeKind::Element);
ElementCategory elementCategoryFromString(const QString &s, ElementCategory fallback = ElementCategory::Layout);
LayoutMode layoutModeFromString(const QString &s, LayoutMode fallback = LayoutMode::Flow);
Breakpoint breakpointFromString(const QString &s, Breakpoint fallback = Breakpoint::Desktop);
ExportMode exportModeFromString(const QString &s, ExportMode fallback = ExportMode::SingleHtml);
StyleUnit styleUnitFromString(const QString &s, StyleUnit fallback = StyleUnit::Px);
AuthMode authModeFromString(const QString &s, AuthMode fallback = AuthMode::ApiKey);
ApiFlavor apiFlavorFromString(const QString &s, ApiFlavor fallback = ApiFlavor::OpenAiCompatible);
McpTransport mcpTransportFromString(const QString &s, McpTransport fallback = McpTransport::Stdio);
ChatRole chatRoleFromString(const QString &s, ChatRole fallback = ChatRole::User);
ThemeMode themeModeFromString(const QString &s, ThemeMode fallback = ThemeMode::Auto);

// Human readable, localised labels (English / Chinese).
// 人类可读的本地化标签（英文 / 中文）。
QString displayName(ElementCategory v, bool chinese);
QString displayName(Breakpoint v, bool chinese);
QString displayName(ExportMode v, bool chinese);
QString displayName(AuthMode v, bool chinese);

// CSS helpers / CSS 辅助函数
QString styleUnitToCss(StyleUnit unit);
bool isColorLike(const QString &value);
QString normaliseColor(const QString &value);

// Security helpers / 安全辅助函数
//  Returns true when the string looks like a secret that must never be logged.
//  当字符串看起来像必须禁止写日志的机密时返回 true。
bool looksLikeSecret(const QString &value);
//  Redacts secrets found inside free text (used for logs and AI transcripts).
//  遮蔽自由文本中的机密（用于日志与 AI 会话记录）。
QString redactSecrets(const QString &text);

// Version information / 版本信息
QString versionString();      // "0.1.0-rc1" / 版本号
QString versionChannel();     // "rc1"
QString buildTimestamp();

} // namespace tcbw
