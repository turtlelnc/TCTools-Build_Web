// ============================================================================
//  Types.cpp -- shared value types and enumerations
//  Types.cpp —— 共享值类型与枚举
// ============================================================================
#include "Types.h"

#include <QRegularExpression>
#include <QRegularExpressionMatchIterator>

namespace tcbw {

namespace {

template <typename E>
E fromStringImpl(const QString &s, const QVector<QPair<E, const char *>> &table, E fallback)
{
    const QString needle = s.trimmed().toLower();
    for (const auto &pair : table) {
        if (needle == QString::fromLatin1(pair.second).toLower())
            return pair.first;
    }
    return fallback;
}

} // namespace

// ---------------------------------------------------------------------------
//  Enum -> string
//  枚举 -> 字符串
// ---------------------------------------------------------------------------
QString toString(NodeKind v)
{
    switch (v) {
    case NodeKind::Root:      return QStringLiteral("root");
    case NodeKind::Element:   return QStringLiteral("element");
    case NodeKind::Component: return QStringLiteral("component");
    case NodeKind::Text:      return QStringLiteral("text");
    case NodeKind::Slot:      return QStringLiteral("slot");
    }
    return QStringLiteral("element");
}

QString toString(ElementCategory v)
{
    switch (v) {
    case ElementCategory::Layout:     return QStringLiteral("layout");
    case ElementCategory::Text:       return QStringLiteral("text");
    case ElementCategory::Media:      return QStringLiteral("media");
    case ElementCategory::Form:       return QStringLiteral("form");
    case ElementCategory::Data:       return QStringLiteral("data");
    case ElementCategory::Navigation: return QStringLiteral("navigation");
    case ElementCategory::Advanced:   return QStringLiteral("advanced");
    case ElementCategory::Component:  return QStringLiteral("component");
    case ElementCategory::Custom:     return QStringLiteral("custom");
    }
    return QStringLiteral("layout");
}

QString toString(LayoutMode v)
{
    return v == LayoutMode::Free ? QStringLiteral("free") : QStringLiteral("flow");
}

QString toString(Breakpoint v)
{
    switch (v) {
    case Breakpoint::Desktop: return QStringLiteral("desktop");
    case Breakpoint::Tablet:  return QStringLiteral("tablet");
    case Breakpoint::Mobile:  return QStringLiteral("mobile");
    }
    return QStringLiteral("desktop");
}

QString toString(ExportMode v)
{
    switch (v) {
    case ExportMode::SingleHtml:      return QStringLiteral("single-html");
    case ExportMode::HtmlWithAssets:  return QStringLiteral("html-with-assets");
    case ExportMode::ScriptOnly:      return QStringLiteral("script-only");
    case ExportMode::HeadlessBundle:  return QStringLiteral("headless-bundle");
    }
    return QStringLiteral("single-html");
}

QString toString(StyleUnit v)
{
    switch (v) {
    case StyleUnit::Auto:    return QStringLiteral("auto");
    case StyleUnit::Px:      return QStringLiteral("px");
    case StyleUnit::Percent: return QStringLiteral("%");
    case StyleUnit::Em:      return QStringLiteral("em");
    case StyleUnit::Rem:     return QStringLiteral("rem");
    case StyleUnit::Vw:      return QStringLiteral("vw");
    case StyleUnit::Vh:      return QStringLiteral("vh");
    case StyleUnit::Vmin:    return QStringLiteral("vmin");
    case StyleUnit::Vmax:    return QStringLiteral("vmax");
    case StyleUnit::Fr:      return QStringLiteral("fr");
    case StyleUnit::None:    return QStringLiteral("");
    }
    return QStringLiteral("px");
}

QString toString(AuthMode v)
{
    switch (v) {
    case AuthMode::ApiKey:      return QStringLiteral("api-key");
    case AuthMode::OAuthPkce:   return QStringLiteral("oauth-pkce");
    case AuthMode::OAuthDevice: return QStringLiteral("oauth-device");
    case AuthMode::Bearer:      return QStringLiteral("bearer");
    case AuthMode::AwsSigV4:    return QStringLiteral("aws-sigv4");
    case AuthMode::None:        return QStringLiteral("none");
    }
    return QStringLiteral("api-key");
}

QString toString(ApiFlavor v)
{
    switch (v) {
    case ApiFlavor::OpenAiCompatible: return QStringLiteral("openai-compatible");
    case ApiFlavor::Anthropic:        return QStringLiteral("anthropic");
    case ApiFlavor::GoogleGemini:     return QStringLiteral("google-gemini");
    case ApiFlavor::Ollama:           return QStringLiteral("ollama");
    case ApiFlavor::AwsBedrock:       return QStringLiteral("aws-bedrock");
    case ApiFlavor::Custom:           return QStringLiteral("custom");
    }
    return QStringLiteral("openai-compatible");
}

QString toString(McpTransport v)
{
    switch (v) {
    case McpTransport::Stdio:   return QStringLiteral("stdio");
    case McpTransport::HttpSse: return QStringLiteral("http-sse");
    case McpTransport::HttpJson:return QStringLiteral("http-json");
    }
    return QStringLiteral("stdio");
}

QString toString(ChatRole v)
{
    switch (v) {
    case ChatRole::System:    return QStringLiteral("system");
    case ChatRole::User:      return QStringLiteral("user");
    case ChatRole::Assistant: return QStringLiteral("assistant");
    case ChatRole::Tool:      return QStringLiteral("tool");
    }
    return QStringLiteral("user");
}

QString toString(ThemeMode v)
{
    switch (v) {
    case ThemeMode::Light: return QStringLiteral("light");
    case ThemeMode::Dark:  return QStringLiteral("dark");
    case ThemeMode::Auto:  return QStringLiteral("auto");
    }
    return QStringLiteral("auto");
}

// ---------------------------------------------------------------------------
//  String -> enum
//  字符串 -> 枚举
// ---------------------------------------------------------------------------
NodeKind nodeKindFromString(const QString &s, NodeKind fallback)
{
    return fromStringImpl<NodeKind>(s, {
        {NodeKind::Root, "root"}, {NodeKind::Element, "element"},
        {NodeKind::Component, "component"}, {NodeKind::Text, "text"},
        {NodeKind::Slot, "slot"}}, fallback);
}

ElementCategory elementCategoryFromString(const QString &s, ElementCategory fallback)
{
    return fromStringImpl<ElementCategory>(s, {
        {ElementCategory::Layout, "layout"}, {ElementCategory::Text, "text"},
        {ElementCategory::Media, "media"}, {ElementCategory::Form, "form"},
        {ElementCategory::Data, "data"}, {ElementCategory::Navigation, "navigation"},
        {ElementCategory::Advanced, "advanced"}, {ElementCategory::Component, "component"},
        {ElementCategory::Custom, "custom"}}, fallback);
}

LayoutMode layoutModeFromString(const QString &s, LayoutMode fallback)
{
    return fromStringImpl<LayoutMode>(s, {
        {LayoutMode::Flow, "flow"}, {LayoutMode::Free, "free"}}, fallback);
}

Breakpoint breakpointFromString(const QString &s, Breakpoint fallback)
{
    return fromStringImpl<Breakpoint>(s, {
        {Breakpoint::Desktop, "desktop"}, {Breakpoint::Tablet, "tablet"},
        {Breakpoint::Mobile, "mobile"}}, fallback);
}

ExportMode exportModeFromString(const QString &s, ExportMode fallback)
{
    return fromStringImpl<ExportMode>(s, {
        {ExportMode::SingleHtml, "single-html"},
        {ExportMode::HtmlWithAssets, "html-with-assets"},
        {ExportMode::ScriptOnly, "script-only"},
        {ExportMode::HeadlessBundle, "headless-bundle"}}, fallback);
}

StyleUnit styleUnitFromString(const QString &s, StyleUnit fallback)
{
    return fromStringImpl<StyleUnit>(s, {
        {StyleUnit::Auto, "auto"}, {StyleUnit::Px, "px"}, {StyleUnit::Percent, "%"},
        {StyleUnit::Em, "em"}, {StyleUnit::Rem, "rem"}, {StyleUnit::Vw, "vw"},
        {StyleUnit::Vh, "vh"}, {StyleUnit::Vmin, "vmin"}, {StyleUnit::Vmax, "vmax"},
        {StyleUnit::Fr, "fr"}}, fallback);
}

AuthMode authModeFromString(const QString &s, AuthMode fallback)
{
    return fromStringImpl<AuthMode>(s, {
        {AuthMode::ApiKey, "api-key"}, {AuthMode::OAuthPkce, "oauth-pkce"},
        {AuthMode::OAuthDevice, "oauth-device"}, {AuthMode::Bearer, "bearer"},
        {AuthMode::AwsSigV4, "aws-sigv4"}, {AuthMode::None, "none"}}, fallback);
}

ApiFlavor apiFlavorFromString(const QString &s, ApiFlavor fallback)
{
    return fromStringImpl<ApiFlavor>(s, {
        {ApiFlavor::OpenAiCompatible, "openai-compatible"},
        {ApiFlavor::Anthropic, "anthropic"},
        {ApiFlavor::GoogleGemini, "google-gemini"},
        {ApiFlavor::Ollama, "ollama"},
        {ApiFlavor::AwsBedrock, "aws-bedrock"},
        {ApiFlavor::Custom, "custom"}}, fallback);
}

McpTransport mcpTransportFromString(const QString &s, McpTransport fallback)
{
    return fromStringImpl<McpTransport>(s, {
        {McpTransport::Stdio, "stdio"}, {McpTransport::HttpSse, "http-sse"},
        {McpTransport::HttpJson, "http-json"}}, fallback);
}

ChatRole chatRoleFromString(const QString &s, ChatRole fallback)
{
    return fromStringImpl<ChatRole>(s, {
        {ChatRole::System, "system"}, {ChatRole::User, "user"},
        {ChatRole::Assistant, "assistant"}, {ChatRole::Tool, "tool"}}, fallback);
}

ThemeMode themeModeFromString(const QString &s, ThemeMode fallback)
{
    return fromStringImpl<ThemeMode>(s, {
        {ThemeMode::Light, "light"}, {ThemeMode::Dark, "dark"},
        {ThemeMode::Auto, "auto"}}, fallback);
}

// ---------------------------------------------------------------------------
//  Localised display names  (English first, then Chinese)
//  本地化显示名称（先英文，后中文）
// ---------------------------------------------------------------------------
QString displayName(ElementCategory v, bool chinese)
{
    switch (v) {
    case ElementCategory::Layout:
        return chinese ? QStringLiteral("布局") : QStringLiteral("Layout");
    case ElementCategory::Text:
        return chinese ? QStringLiteral("文本") : QStringLiteral("Text");
    case ElementCategory::Media:
        return chinese ? QStringLiteral("媒体") : QStringLiteral("Media");
    case ElementCategory::Form:
        return chinese ? QStringLiteral("表单") : QStringLiteral("Form");
    case ElementCategory::Data:
        return chinese ? QStringLiteral("数据") : QStringLiteral("Data");
    case ElementCategory::Navigation:
        return chinese ? QStringLiteral("导航") : QStringLiteral("Navigation");
    case ElementCategory::Advanced:
        return chinese ? QStringLiteral("高级") : QStringLiteral("Advanced");
    case ElementCategory::Component:
        return chinese ? QStringLiteral("组件") : QStringLiteral("Components");
    case ElementCategory::Custom:
        return chinese ? QStringLiteral("自定义") : QStringLiteral("Custom");
    }
    return chinese ? QStringLiteral("其他") : QStringLiteral("Other");
}

QString displayName(Breakpoint v, bool chinese)
{
    switch (v) {
    case Breakpoint::Desktop:
        return chinese ? QStringLiteral("桌面") : QStringLiteral("Desktop");
    case Breakpoint::Tablet:
        return chinese ? QStringLiteral("平板") : QStringLiteral("Tablet");
    case Breakpoint::Mobile:
        return chinese ? QStringLiteral("手机") : QStringLiteral("Mobile");
    }
    return chinese ? QStringLiteral("桌面") : QStringLiteral("Desktop");
}

QString displayName(ExportMode v, bool chinese)
{
    switch (v) {
    case ExportMode::SingleHtml:
        return chinese ? QStringLiteral("单个 HTML 文件") : QStringLiteral("Single .html file");
    case ExportMode::HtmlWithAssets:
        return chinese ? QStringLiteral("HTML + CSS + JS") : QStringLiteral("HTML + CSS + JS");
    case ExportMode::ScriptOnly:
        return chinese ? QStringLiteral("仅 .js 脚本") : QStringLiteral("Script (.js) only");
    case ExportMode::HeadlessBundle:
        return chinese ? QStringLiteral("完整静态站点包") : QStringLiteral("Full static bundle");
    }
    return chinese ? QStringLiteral("单个 HTML 文件") : QStringLiteral("Single .html file");
}

QString displayName(AuthMode v, bool chinese)
{
    switch (v) {
    case AuthMode::ApiKey:
        return chinese ? QStringLiteral("API Key") : QStringLiteral("API Key");
    case AuthMode::OAuthPkce:
        return chinese ? QStringLiteral("OAuth 2.0（PKCE）") : QStringLiteral("OAuth 2.0 (PKCE)");
    case AuthMode::OAuthDevice:
        return chinese ? QStringLiteral("OAuth 2.0（设备码）") : QStringLiteral("OAuth 2.0 (Device code)");
    case AuthMode::Bearer:
        return chinese ? QStringLiteral("Bearer 令牌") : QStringLiteral("Bearer token");
    case AuthMode::AwsSigV4:
        return chinese ? QStringLiteral("AWS 签名 V4") : QStringLiteral("AWS Signature V4");
    case AuthMode::None:
        return chinese ? QStringLiteral("无需授权") : QStringLiteral("No authentication");
    }
    return chinese ? QStringLiteral("API Key") : QStringLiteral("API Key");
}

// ---------------------------------------------------------------------------
//  CSS helpers
//  CSS 辅助函数
// ---------------------------------------------------------------------------
QString styleUnitToCss(StyleUnit unit)
{
    return unit == StyleUnit::Auto ? QStringLiteral("auto") : toString(unit);
}

bool isColorLike(const QString &value)
{
    static const QRegularExpression re(
        QStringLiteral("^(#[0-9a-fA-F]{3,8}|rgba?\\([^)]*\\)|hsla?\\([^)]*\\)|"
                       "transparent|currentColor|[a-zA-Z]{3,24})$"));
    return re.match(value.trimmed()).hasMatch();
}

QString normaliseColor(const QString &value)
{
    QString v = value.trimmed();
    if (v.isEmpty())
        return v;
    // Expand #abc / #abcd into the canonical six/eight digit form.
    // 将 #abc / #abcd 展开为规范的六/八位形式。
    if (v.startsWith(QLatin1Char('#'))) {
        const QString hex = v.mid(1);
        if (hex.size() == 3 || hex.size() == 4) {
            QString out = QStringLiteral("#");
            for (const QChar c : hex)
                out += QString(c) + QString(c);
            return out;
        }
    }
    return v;
}

// ---------------------------------------------------------------------------
//  Security helpers
//  安全辅助函数
// ---------------------------------------------------------------------------
bool looksLikeSecret(const QString &value)
{
    const QString v = value.trimmed();
    if (v.size() < 12)
        return false;

    static const QRegularExpression prefixed(
        QStringLiteral("^(sk-|sk_|ghp_|gho_|ghu_|ghs_|github_pat_|glpat-|xoxb-|xoxp-|"
                       "AIza[0-9A-Za-z_-]{10,}|AKIA[0-9A-Z]{16}|hf_|r8_|LTAI|"
                       "Bearer\\s+\\S{16,})"));
    if (prefixed.match(v).hasMatch())
        return true;

    // Long high-entropy tokens with no spaces are treated as secrets.
    // 无空格的长随机串一律视为机密。
    if (v.contains(QLatin1Char(' ')) || v.contains(QLatin1Char('\n')))
        return false;
    static const QRegularExpression alnum(QStringLiteral("^[A-Za-z0-9_\\-\\.=+/]{32,}$"));
    return alnum.match(v).hasMatch();
}

QString redactSecrets(const QString &text)
{
    QString out = text;

    // Known credential prefixes: keep the first 4 characters, mask the rest.
    // 已知凭据前缀：保留前 4 个字符，其余遮蔽。
    static const QRegularExpression known(
        QStringLiteral("\\b(sk-[A-Za-z0-9_\\-]{6,}|sk_[A-Za-z0-9_\\-]{6,}|"
                       "ghp_[A-Za-z0-9]{10,}|gho_[A-Za-z0-9]{10,}|"
                       "github_pat_[A-Za-z0-9_]{10,}|glpat-[A-Za-z0-9_\\-]{10,}|"
                       "xox[bpas]-[A-Za-z0-9\\-]{10,}|hf_[A-Za-z0-9]{10,}|"
                       "AIza[A-Za-z0-9_\\-]{10,}|AKIA[0-9A-Z]{16})"));
    QRegularExpressionMatchIterator it = known.globalMatch(out);
    QVector<QPair<int, int>> spans;
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        spans.append({m.capturedStart(1), m.capturedLength(1)});
    }
    // Replace back-to-front so earlier offsets stay valid.
    // 从后向前替换，保证较早的偏移量仍然有效。
    for (int i = spans.size() - 1; i >= 0; --i) {
        const int start = spans.at(i).first;
        const int len = spans.at(i).second;
        const QString original = out.mid(start, len);
        out.replace(start, len,
                    original.left(4) + QStringLiteral("****REDACTED****"));
    }

    // Explicit key=value / "key": "value" pairs for secret-looking names.
    // 针对形如 key=value / "key": "value" 的机密字段。
    static const QRegularExpression kv(
        QStringLiteral("((?:api[_\\-]?key|apikey|access[_\\-]?token|refresh[_\\-]?token|"
                       "client[_\\-]?secret|password|passwd|secret|authorization|"
                       "auth[_\\-]?token|private[_\\-]?key)\\s*[:=]\\s*[\"']?)([^\"'\\s,;]{6,})"),
        QRegularExpression::CaseInsensitiveOption);
    out.replace(kv, QStringLiteral("\\1****REDACTED****"));

    // Bearer tokens / Bearer 令牌
    static const QRegularExpression bearer(
        QStringLiteral("(Bearer\\s+)([A-Za-z0-9_\\-\\.=+/]{8,})"),
        QRegularExpression::CaseInsensitiveOption);
    out.replace(bearer, QStringLiteral("\\1****REDACTED****"));

    return out;
}

// ---------------------------------------------------------------------------
//  Version information
//  版本信息
// ---------------------------------------------------------------------------
QString versionString()
{
#ifdef TCBW_VERSION_FULL
    return QStringLiteral(TCBW_VERSION_FULL);
#else
    return QStringLiteral("0.1.0-rc1");
#endif
}

QString versionChannel()
{
#ifdef TCBW_VERSION_CHANNEL
    return QStringLiteral(TCBW_VERSION_CHANNEL);
#else
    return QStringLiteral("rc1");
#endif
}

QString buildTimestamp()
{
    return QStringLiteral(__DATE__ " " __TIME__);
}

} // namespace tcbw
