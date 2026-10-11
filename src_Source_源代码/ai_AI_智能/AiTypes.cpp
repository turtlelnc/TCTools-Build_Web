// ============================================================================
//  AiTypes.cpp -- value types shared by the TI-UseAgent subsystem
//  AiTypes.cpp —— TI-UseAgent 子系统共享的值类型
//
//  Everything in this file is pure (no I/O, no network, no credentials), which
//  keeps the AI layer unit-testable and keeps secrets out of the value layer.
//  本文件中的一切都是纯逻辑（无 I/O、无网络、无凭据），这使 AI 层可被单元测试，
//  也让机密不会进入值类型层。
// ============================================================================
#include "AiTypes.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QRegularExpression>
#include <QStringList>

namespace tcbw {
namespace {

// ---------------------------------------------------------------------------
//  Capability <-> stable string
//  能力标记与稳定字符串互转
// ---------------------------------------------------------------------------
struct CapPair
{
    ModelCapability flag;
    const char *key;
};

const CapPair kCapabilityPairs[] = {
    {CapChat,       "chat"},
    {CapStreaming,  "streaming"},
    {CapTools,      "tools"},
    {CapVision,     "vision"},
    {CapReasoning,  "reasoning"},
    {CapEmbeddings, "embeddings"},
    {CapImage,      "image"},
};

QString capabilityKey(ModelCapability flag)
{
    for (const CapPair &pair : kCapabilityPairs) {
        if (pair.flag == flag)
            return QString::fromLatin1(pair.key);
    }
    return QStringLiteral("none");
}

ModelCapabilities capabilitiesFromJson(const QJsonValue &value)
{
    ModelCapabilities caps = CapNone;

    const auto applyKey = [&caps](const QString &raw) {
        const QString key = raw.trimmed().toLower();
        if (key.isEmpty() || key == QLatin1String("none"))
            return;
        for (const CapPair &pair : kCapabilityPairs) {
            if (key == QLatin1String(pair.key))
                caps |= pair.flag;
        }
        //  Accept a few aliases used by older project files and by MCP clients.
        //  兼容旧工程文件与部分 MCP 客户端使用的别名。
        if (key == QLatin1String("function-calling") || key == QLatin1String("tool-calling"))
            caps |= CapTools;
        else if (key == QLatin1String("thinking") || key == QLatin1String("reason"))
            caps |= CapReasoning;
        else if (key == QLatin1String("image-generation"))
            caps |= CapImage;
        else if (key == QLatin1String("embedding"))
            caps |= CapEmbeddings;
    };

    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        for (const QJsonValue &entry : array)
            applyKey(entry.toString());
    } else if (value.isString()) {
        //  Tolerate "chat|tools" and "chat,tools" as well as a single flag.
        //  同时兼容 "chat|tools"、"chat,tools" 以及单个标记。
        const QString text = value.toString();
        for (const QString &part : text.split(QRegularExpression(QStringLiteral("[|,]")),
                                              Qt::SkipEmptyParts)) {
            applyKey(part);
        }
    }

    return caps;
}

QJsonArray capabilitiesToJson(ModelCapabilities caps)
{
    QJsonArray array;
    for (const CapPair &pair : kCapabilityPairs) {
        if (caps.testFlag(pair.flag))
            array.append(QString::fromLatin1(pair.key));
    }
    return array;
}

//  Reads the first present key of `names` from `obj`.
//  从 `obj` 中读取 `names` 里第一个存在的键。
QJsonValue firstOf(const QJsonObject &obj, std::initializer_list<const char *> names)
{
    for (const char *name : names) {
        const QString key = QString::fromLatin1(name);
        const QJsonValue value = obj.value(key);
        if (!value.isUndefined() && !value.isNull())
            return value;
    }
    return QJsonValue(QJsonValue::Undefined);
}

double doubleOr(const QJsonObject &obj, std::initializer_list<const char *> names, double fallback)
{
    const QJsonValue value = firstOf(obj, names);
    if (value.isDouble())
        return value.toDouble();
    if (value.isString()) {
        bool ok = false;
        const double parsed = value.toString().toDouble(&ok);
        if (ok)
            return parsed;
    }
    return fallback;
}

int intOr(const QJsonObject &obj, std::initializer_list<const char *> names, int fallback)
{
    const QJsonValue value = firstOf(obj, names);
    if (value.isDouble())
        return value.toInt();
    if (value.isString()) {
        bool ok = false;
        const int parsed = value.toString().toInt(&ok);
        if (ok)
            return parsed;
    }
    return fallback;
}

QString stringOr(const QJsonObject &obj, std::initializer_list<const char *> names,
                 const QString &fallback = QString())
{
    const QJsonValue value = firstOf(obj, names);
    return value.isString() ? value.toString() : fallback;
}

bool boolOr(const QJsonObject &obj, std::initializer_list<const char *> names, bool fallback)
{
    const QJsonValue value = firstOf(obj, names);
    if (value.isBool())
        return value.toBool();
    if (value.isDouble())
        return value.toDouble() != 0.0;
    if (value.isString()) {
        const QString text = value.toString().trimmed().toLower();
        if (text == QLatin1String("true") || text == QLatin1String("1") || text == QLatin1String("yes"))
            return true;
        if (text == QLatin1String("false") || text == QLatin1String("0") || text == QLatin1String("no"))
            return false;
    }
    return fallback;
}

QStringList stringListOr(const QJsonObject &obj, std::initializer_list<const char *> names)
{
    const QJsonValue value = firstOf(obj, names);
    QStringList out;
    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        for (const QJsonValue &entry : array) {
            if (entry.isString())
                out.append(entry.toString());
        }
    } else if (value.isString()) {
        const QString text = value.toString();
        if (!text.isEmpty())
            out.append(text);
    }
    return out;
}

QMap<QString, QString> stringMapOr(const QJsonObject &obj, std::initializer_list<const char *> names)
{
    QMap<QString, QString> out;
    const QJsonValue value = firstOf(obj, names);
    if (value.isObject()) {
        const QJsonObject nested = value.toObject();
        for (auto it = nested.constBegin(); it != nested.constEnd(); ++it) {
            if (it.value().isString())
                out.insert(it.key(), it.value().toString());
        }
    }
    return out;
}

QJsonObject toJsonStringMap(const QMap<QString, QString> &map)
{
    QJsonObject out;
    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
        out.insert(it.key(), it.value());
    return out;
}

QJsonArray stringListToJson(const QStringList &list)
{
    QJsonArray array;
    for (const QString &entry : list)
        array.append(entry);
    return array;
}

//  AuthMode / ApiFlavor string helpers reuse the frozen core converters, but we
//  need a tolerant parser for hand-edited configuration files.
//  AuthMode / ApiFlavor 的字符串转换复用核心模块中已冻结的转换函数，但手工编辑的
//  配置文件需要更宽容的解析。
AuthMode authModeFromLoose(const QString &raw, AuthMode fallback)
{
    const QString key = raw.trimmed().toLower();
    if (key.isEmpty())
        return fallback;
    if (key == QLatin1String("apikey") || key == QLatin1String("api_key") || key == QLatin1String("key"))
        return AuthMode::ApiKey;
    if (key == QLatin1String("oauth") || key == QLatin1String("oauth2") || key == QLatin1String("pkce"))
        return AuthMode::OAuthPkce;
    if (key == QLatin1String("device") || key == QLatin1String("device-code"))
        return AuthMode::OAuthDevice;
    if (key == QLatin1String("sigv4") || key == QLatin1String("aws"))
        return AuthMode::AwsSigV4;
    return authModeFromString(key, fallback);
}

ApiFlavor apiFlavorFromLoose(const QString &raw, ApiFlavor fallback)
{
    const QString key = raw.trimmed().toLower();
    if (key.isEmpty())
        return fallback;
    if (key == QLatin1String("openai") || key == QLatin1String("open-ai"))
        return ApiFlavor::OpenAiCompatible;
    if (key == QLatin1String("gemini") || key == QLatin1String("google"))
        return ApiFlavor::GoogleGemini;
    if (key == QLatin1String("bedrock"))
        return ApiFlavor::AwsBedrock;
    return apiFlavorFromString(key, fallback);
}

//  Trims trailing slashes so callers can concatenate paths safely.
//  去掉末尾斜杠，便于调用方安全拼接路径。
QString trimmedUrl(const QString &url)
{
    QString out = url.trimmed();
    while (out.endsWith(QLatin1Char('/')))
        out.chop(1);
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
//  ModelInfo
//  模型信息
// ---------------------------------------------------------------------------
QJsonObject ModelInfo::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), id);
    obj.insert(QStringLiteral("label"), label.isEmpty() ? id : label);
    obj.insert(QStringLiteral("capabilities"), capabilitiesToJson(caps));
    obj.insert(QStringLiteral("contextWindow"), contextWindow);
    obj.insert(QStringLiteral("maxOutputTokens"), maxOutputTokens);
    obj.insert(QStringLiteral("inputPricePerMillion"), inputPricePerMillion);
    obj.insert(QStringLiteral("outputPricePerMillion"), outputPricePerMillion);
    obj.insert(QStringLiteral("deprecated"), deprecated);
    return obj;
}

ModelInfo ModelInfo::fromJson(const QJsonObject &obj)
{
    ModelInfo info;
    info.id = stringOr(obj, {"id", "model", "modelId", "name"});
    info.label = stringOr(obj, {"label", "displayName"}, info.id);
    info.caps = capabilitiesFromJson(firstOf(obj, {"capabilities", "caps", "features"}));
    info.contextWindow = intOr(obj, {"contextWindow", "context_window", "context", "contextLength"}, 0);
    info.maxOutputTokens = intOr(obj, {"maxOutputTokens", "max_output_tokens", "maxTokens"}, 0);
    info.inputPricePerMillion =
        doubleOr(obj, {"inputPricePerMillion", "input_price_per_million", "inputPrice"}, -1.0);
    info.outputPricePerMillion =
        doubleOr(obj, {"outputPricePerMillion", "output_price_per_million", "outputPrice"}, -1.0);
    info.deprecated = boolOr(obj, {"deprecated", "retired"}, false);

    //  A model with no explicit capability list is at least a chat model.
    //  未显式声明能力的模型至少是对话模型。
    if (info.caps == CapNone)
        info.caps = CapChat | CapStreaming;
    return info;
}

// ---------------------------------------------------------------------------
//  ToolCall / ToolResult
//  工具调用 / 工具结果
// ---------------------------------------------------------------------------
QJsonObject ToolCall::toJson() const
{
    //  Normalised superset: the OpenAI shape is canonical, `name`/`input` are
    //  mirrored so an Anthropic-shaped reader can consume the same object.
    //  规范化超集：以 OpenAI 形态为准，同时镜像 `name`/`input`，
    //  使 Anthropic 形态的读取方也能消费同一对象。
    QJsonObject function;
    function.insert(QStringLiteral("name"), name);
    function.insert(QStringLiteral("arguments"),
                    QString::fromUtf8(QJsonDocument(arguments).toJson(QJsonDocument::Compact)));

    QJsonObject obj;
    obj.insert(QStringLiteral("id"), id);
    obj.insert(QStringLiteral("type"), QStringLiteral("function"));
    obj.insert(QStringLiteral("function"), function);
    obj.insert(QStringLiteral("name"), name);
    obj.insert(QStringLiteral("input"), arguments);
    return obj;
}

ToolCall ToolCall::fromJson(const QJsonObject &obj)
{
    ToolCall call;
    call.id = stringOr(obj, {"id", "toolCallId", "tool_call_id", "callId"});

    // --- OpenAI shape: {"function": {"name": ..., "arguments": "<json>"}} ----
    // --- OpenAI 形态：{"function": {"name": ..., "arguments": "<json>"}} -----
    const QJsonObject function = obj.value(QStringLiteral("function")).toObject();
    if (!function.isEmpty()) {
        call.name = stringOr(function, {"name"}, call.name);
        const QJsonValue arguments = function.value(QStringLiteral("arguments"));
        if (arguments.isString()) {
            //  `arguments` is a JSON *string* in the OpenAI wire format; some
            //  gateways emit an empty string for a call with no parameters.
            //  在 OpenAI 协议中 `arguments` 是 JSON *字符串*；某些网关在无参数
            //  调用时会发出空字符串。
            const QString raw = arguments.toString().trimmed();
            if (raw.isEmpty()) {
                call.arguments = QJsonObject();
            } else {
                const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8());
                if (doc.isObject())
                    call.arguments = doc.object();
            }
        } else if (arguments.isObject()) {
            //  Tolerated deviation: some proxies send a real object.
            //  可容忍的偏差：部分代理直接发送真实对象。
            call.arguments = arguments.toObject();
        }
    }

    // --- Anthropic / MCP shape: {"name": ..., "input": {...}} ---------------
    // --- Anthropic / MCP 形态：{"name": ..., "input": {...}} ----------------
    if (call.name.isEmpty())
        call.name = stringOr(obj, {"name", "tool", "toolName"});

    if (call.arguments.isEmpty()) {
        const QJsonValue input = firstOf(obj, {"input", "arguments", "args", "parameters"});
        if (input.isObject())
            call.arguments = input.toObject();
        else if (input.isString()) {
            const QJsonDocument doc = QJsonDocument::fromJson(input.toString().toUtf8());
            if (doc.isObject())
                call.arguments = doc.object();
        }
    }

    //  Some transcript formats keep the arguments under the call id.
    //  某些会话记录格式把参数放在调用 id 之下。
    if (call.id.isEmpty() && call.name.isEmpty() && !obj.isEmpty())
        call.id = stringOr(obj, {"tool_use_id"});
    return call;
}

QJsonObject ToolResult::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("toolCallId"), toolCallId);
    obj.insert(QStringLiteral("tool_call_id"), toolCallId);
    obj.insert(QStringLiteral("content"), content);
    obj.insert(QStringLiteral("isError"), isError);
    obj.insert(QStringLiteral("is_error"), isError);
    return obj;
}

// ---------------------------------------------------------------------------
//  ToolDescriptor
//  工具描述符
// ---------------------------------------------------------------------------
QJsonObject ToolDescriptor::toOpenAiTool() const
{
    QJsonObject function;
    function.insert(QStringLiteral("name"), name);
    function.insert(QStringLiteral("description"),
                    descriptionEn.isEmpty() ? titleEn : descriptionEn);

    //  A JSON Schema of type "object" is mandatory; synthesise an empty one so
    //  a tool without parameters is still a valid declaration.
    //  必须提供 type 为 "object" 的 JSON Schema；为无参数工具合成空 schema，
    //  保证声明始终合法。
    QJsonObject schema = inputSchema;
    if (schema.isEmpty() || !schema.contains(QStringLiteral("type"))) {
        schema.insert(QStringLiteral("type"), QStringLiteral("object"));
        if (!schema.contains(QStringLiteral("properties")))
            schema.insert(QStringLiteral("properties"), QJsonObject());
    }

    function.insert(QStringLiteral("parameters"), schema);

    QJsonObject tool;
    tool.insert(QStringLiteral("type"), QStringLiteral("function"));
    tool.insert(QStringLiteral("function"), function);
    return tool;
}

QJsonObject ToolDescriptor::toAnthropicTool() const
{
    QJsonObject schema = inputSchema;
    if (schema.isEmpty() || !schema.contains(QStringLiteral("type"))) {
        schema.insert(QStringLiteral("type"), QStringLiteral("object"));
        if (!schema.contains(QStringLiteral("properties")))
            schema.insert(QStringLiteral("properties"), QJsonObject());
    }

    QJsonObject tool;
    tool.insert(QStringLiteral("name"), name);
    tool.insert(QStringLiteral("description"),
                descriptionEn.isEmpty() ? titleEn : descriptionEn);
    tool.insert(QStringLiteral("input_schema"), schema);
    return tool;
}

// ---------------------------------------------------------------------------
//  ChatMessage
//  对话消息
// ---------------------------------------------------------------------------
QJsonObject ChatMessage::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("role"), toString(role));
    obj.insert(QStringLiteral("content"), content);

    if (!name.isEmpty())
        obj.insert(QStringLiteral("name"), name);
    if (!toolCallId.isEmpty()) {
        obj.insert(QStringLiteral("toolCallId"), toolCallId);
        obj.insert(QStringLiteral("tool_call_id"), toolCallId);
    }
    if (!toolCalls.isEmpty()) {
        QJsonArray calls;
        for (const ToolCall &call : toolCalls)
            calls.append(call.toJson());
        obj.insert(QStringLiteral("toolCalls"), calls);
    }
    if (!reasoning.isEmpty())
        obj.insert(QStringLiteral("reasoning"), reasoning);
    if (!imageParts.isEmpty())
        obj.insert(QStringLiteral("imageParts"), imageParts);
    if (timestamp.isValid())
        obj.insert(QStringLiteral("timestamp"), timestamp.toString(Qt::ISODateWithMs));
    obj.insert(QStringLiteral("streaming"), streaming);
    return obj;
}

ChatMessage ChatMessage::fromJson(const QJsonObject &obj)
{
    ChatMessage message;
    message.role = chatRoleFromString(stringOr(obj, {"role"}, QStringLiteral("user")),
                                      ChatRole::User);
    message.content = stringOr(obj, {"content", "text"});
    message.name = stringOr(obj, {"name", "toolName"});
    message.toolCallId = stringOr(obj, {"toolCallId", "tool_call_id"});
    message.reasoning = stringOr(obj, {"reasoning", "reasoningContent", "thinking"});

    const QJsonValue calls = firstOf(obj, {"toolCalls", "tool_calls"});
    if (calls.isArray()) {
        const QJsonArray array = calls.toArray();
        for (const QJsonValue &entry : array) {
            if (entry.isObject())
                message.toolCalls.append(ToolCall::fromJson(entry.toObject()));
        }
    }

    const QJsonValue images = firstOf(obj, {"imageParts", "images", "attachments"});
    if (images.isArray())
        message.imageParts = images.toArray();

    const QString stamp = stringOr(obj, {"timestamp", "time"});
    if (!stamp.isEmpty()) {
        message.timestamp = QDateTime::fromString(stamp, Qt::ISODateWithMs);
        if (!message.timestamp.isValid())
            message.timestamp = QDateTime::fromString(stamp, Qt::ISODate);
    }
    if (!message.timestamp.isValid())
        message.timestamp = QDateTime::currentDateTime();

    message.streaming = boolOr(obj, {"streaming"}, false);

    //  A tool message without an explicit name still needs one for the UI.
    //  没有显式名称的工具消息仍需一个名称供界面显示。
    if (message.role == ChatRole::Tool && message.name.isEmpty())
        message.name = QStringLiteral("tool");
    return message;
}

ChatMessage ChatMessage::system(const QString &text)
{
    ChatMessage message;
    message.role = ChatRole::System;
    message.content = text;
    return message;
}

ChatMessage ChatMessage::user(const QString &text)
{
    ChatMessage message;
    message.role = ChatRole::User;
    message.content = text;
    return message;
}

ChatMessage ChatMessage::assistant(const QString &text)
{
    ChatMessage message;
    message.role = ChatRole::Assistant;
    message.content = text;
    return message;
}

ChatMessage ChatMessage::tool(const QString &toolCallId, const QString &name,
                              const QString &content, bool isError)
{
    ChatMessage message;
    message.role = ChatRole::Tool;
    message.toolCallId = toolCallId;
    message.name = name;
    message.content = content;
    //  Providers have no error channel for tool results, so an error is a
    //  prefixed payload the model can read and react to.
    //  各厂商的工具结果没有错误通道，因此错误以带前缀的载荷形式表达，
    //  便于模型理解并作出反应。
    if (isError && !content.startsWith(QLatin1String("ERROR:")))
        message.content = QStringLiteral("ERROR: ") + content;
    message.timestamp = QDateTime::currentDateTime();
    return message;
}

// ---------------------------------------------------------------------------
//  VendorInfo
//  厂商信息
// ---------------------------------------------------------------------------
const ModelInfo *VendorInfo::model(const QString &id) const
{
    if (id.isEmpty())
        return nullptr;

    //  Exact match first, then a case-insensitive pass: model ids are case
    //  sensitive on the wire but users type them casually in the UI.
    //  先精确匹配，再忽略大小写匹配：模型标识在接口上区分大小写，
    //  但用户在界面中往往随手输入。
    for (const ModelInfo &entry : models) {
        if (entry.id == id)
            return &entry;
    }
    for (const ModelInfo &entry : models) {
        if (entry.id.compare(id, Qt::CaseInsensitive) == 0)
            return &entry;
    }
    return nullptr;
}

QJsonObject VendorInfo::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), id);
    obj.insert(QStringLiteral("labelEn"), labelEn);
    obj.insert(QStringLiteral("labelZh"), labelZh);
    obj.insert(QStringLiteral("region"), region);
    obj.insert(QStringLiteral("homepageUrl"), homepageUrl);
    obj.insert(QStringLiteral("docsUrl"), docsUrl);
    obj.insert(QStringLiteral("keysUrl"), keysUrl);
    obj.insert(QStringLiteral("baseUrl"), baseUrl);
    obj.insert(QStringLiteral("chatPath"), chatPath);
    obj.insert(QStringLiteral("flavor"), toString(flavor));

    QJsonArray modes;
    for (AuthMode mode : authModes)
        modes.append(toString(mode));
    obj.insert(QStringLiteral("authModes"), modes);
    obj.insert(QStringLiteral("preferredAuth"), preferredAuth);

    obj.insert(QStringLiteral("defaultModel"), defaultModel);
    QJsonArray modelArray;
    for (const ModelInfo &entry : models)
        modelArray.append(entry.toJson());
    obj.insert(QStringLiteral("models"), modelArray);

    obj.insert(QStringLiteral("oauthAuthorizeUrl"), oauthAuthorizeUrl);
    obj.insert(QStringLiteral("oauthTokenUrl"), oauthTokenUrl);
    obj.insert(QStringLiteral("oauthDeviceCodeUrl"), oauthDeviceCodeUrl);
    obj.insert(QStringLiteral("oauthRedirectUri"), oauthRedirectUri);
    obj.insert(QStringLiteral("oauthScopes"), oauthScopes);
    obj.insert(QStringLiteral("oauthClientId"), oauthClientId);
    obj.insert(QStringLiteral("oauthRequiresUserClientId"), oauthRequiresUserClientId);
    obj.insert(QStringLiteral("oauthUsesPkce"), oauthUsesPkce);

    obj.insert(QStringLiteral("keyPrefixHint"), keyPrefixHint);
    obj.insert(QStringLiteral("keyMinLength"), keyMinLength);
    QJsonArray headers;
    for (const QString &header : keyHeaderNames)
        headers.append(header);
    obj.insert(QStringLiteral("keyHeaderNames"), headers);

    obj.insert(QStringLiteral("supportsTools"), supportsTools);
    obj.insert(QStringLiteral("supportsStreaming"), supportsStreaming);
    obj.insert(QStringLiteral("supportsVision"), supportsVision);
    obj.insert(QStringLiteral("supportsReasoning"), supportsReasoning);
    obj.insert(QStringLiteral("isLocal"), isLocal);

    obj.insert(QStringLiteral("notesEn"), stringListToJson(notesEn));
    obj.insert(QStringLiteral("notesZh"), stringListToJson(notesZh));
    return obj;
}

// ---------------------------------------------------------------------------
//  ProviderConfig
//  厂商配置
// ---------------------------------------------------------------------------
QJsonObject ProviderConfig::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("vendorId"), vendorId);
    obj.insert(QStringLiteral("authMode"), authMode);
    obj.insert(QStringLiteral("baseUrlOverride"), baseUrlOverride);
    obj.insert(QStringLiteral("model"), model);
    obj.insert(QStringLiteral("organisation"), organisation);
    obj.insert(QStringLiteral("project"), project);
    obj.insert(QStringLiteral("azureDeployment"), azureDeployment);
    obj.insert(QStringLiteral("azureApiVersion"), azureApiVersion);
    obj.insert(QStringLiteral("awsRegion"), awsRegion);
    obj.insert(QStringLiteral("systemPromptOverride"), systemPromptOverride);
    obj.insert(QStringLiteral("temperature"), temperature);
    obj.insert(QStringLiteral("maxTokens"), maxTokens);
    obj.insert(QStringLiteral("streaming"), streaming);
    obj.insert(QStringLiteral("toolsEnabled"), toolsEnabled);
    obj.insert(QStringLiteral("autoApproveSafeTools"), autoApproveSafeTools);
    obj.insert(QStringLiteral("requestTimeoutMs"), requestTimeoutMs);
    return obj;
}

ProviderConfig ProviderConfig::fromJson(const QJsonObject &obj)
{
    //  Defaults come from the struct itself so a partial JSON object always
    //  produces a usable configuration.
    //  默认值取自结构体本身，因此即使 JSON 不完整也能得到可用配置。
    const ProviderConfig defaults;
    ProviderConfig config;

    config.vendorId = stringOr(obj, {"vendorId", "vendor", "id"}, defaults.vendorId);
    config.authMode = stringOr(obj, {"authMode", "auth", "auth_mode"}, defaults.authMode);
    if (config.authMode.isEmpty())
        config.authMode = defaults.authMode;
    //  Normalise informal spellings ("oauth", "key") to the canonical ids used
    //  by Types.h so comparisons elsewhere stay simple.
    //  把口语化写法（"oauth"、"key"）规范为 Types.h 使用的标准标识，
    //  便于其它位置直接比较。
    config.authMode = toString(authModeFromLoose(config.authMode, AuthMode::ApiKey));

    config.baseUrlOverride = stringOr(obj, {"baseUrlOverride", "baseUrl", "base_url"},
                                      defaults.baseUrlOverride);
    config.model = stringOr(obj, {"model", "modelId"}, defaults.model);
    config.organisation = stringOr(obj, {"organisation", "organization", "org"},
                                   defaults.organisation);
    config.project = stringOr(obj, {"project", "projectId"}, defaults.project);
    config.azureDeployment = stringOr(obj, {"azureDeployment", "deployment"},
                                      defaults.azureDeployment);
    config.azureApiVersion = stringOr(obj, {"azureApiVersion", "apiVersion"},
                                      defaults.azureApiVersion);
    config.awsRegion = stringOr(obj, {"awsRegion", "region"}, defaults.awsRegion);
    config.systemPromptOverride = stringOr(obj, {"systemPromptOverride", "systemPrompt"},
                                           defaults.systemPromptOverride);
    config.temperature = doubleOr(obj, {"temperature"}, defaults.temperature);
    config.maxTokens = intOr(obj, {"maxTokens", "max_tokens"}, defaults.maxTokens);
    config.streaming = boolOr(obj, {"streaming", "stream"}, defaults.streaming);
    config.toolsEnabled = boolOr(obj, {"toolsEnabled", "tools"}, defaults.toolsEnabled);
    config.autoApproveSafeTools = boolOr(obj, {"autoApproveSafeTools"},
                                         defaults.autoApproveSafeTools);
    config.requestTimeoutMs = intOr(obj, {"requestTimeoutMs", "timeoutMs"},
                                    defaults.requestTimeoutMs);

    //  Guard rails: a nonsensical temperature or timeout would make every
    //  request fail with a confusing server error.
    //  防护：不合理的温度或超时会让每次请求都以难以理解的服务器错误失败。
    if (config.temperature < 0.0 || config.temperature > 2.0)
        config.temperature = defaults.temperature;
    if (config.maxTokens <= 0)
        config.maxTokens = defaults.maxTokens;
    if (config.requestTimeoutMs < 1000)
        config.requestTimeoutMs = defaults.requestTimeoutMs;
    return config;
}

QString ProviderConfig::effectiveBaseUrl(const VendorInfo &vendor) const
{
    QString url = trimmedUrl(baseUrlOverride.isEmpty() ? vendor.baseUrl : baseUrlOverride);

    //  Presets for managed clouds carry placeholders the user must fill in;
    //  resolving the ones we know about here keeps the call sites simple.
    //  托管云厂商的预设包含需要用户填写的占位符；在此解析已知占位符，
    //  可让调用方保持简单。
    if (!awsRegion.isEmpty())
        url.replace(QLatin1String("{region}"), awsRegion);
    //  {deployment} lives in the chat path of managed vendors; {resource} is
    //  deliberately left untouched because it identifies the Azure resource
    //  itself, which is not the same thing as a deployment name. A user with an
    //  Azure resource sets baseUrlOverride once and keeps {deployment} in the
    //  path.
    //  {deployment} 出现在托管厂商的对话路径中；{resource} 刻意不做替换，
    //  因为它标识的是 Azure 资源本身，与部署名称并不是一回事。Azure 用户只需
    //  设置一次 baseUrlOverride，并让 {deployment} 留在路径中即可。
    if (!azureDeployment.isEmpty())
        url.replace(QLatin1String("{deployment}"), azureDeployment);
    if (!project.isEmpty())
        url.replace(QLatin1String("{project}"), project);
    return url;
}

// ---------------------------------------------------------------------------
//  McpServerConfig
//  MCP 服务器配置
// ---------------------------------------------------------------------------
QJsonObject McpServerConfig::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), id);
    obj.insert(QStringLiteral("name"), name);
    obj.insert(QStringLiteral("transport"), toString(transport));
    obj.insert(QStringLiteral("enabled"), enabled);
    obj.insert(QStringLiteral("trusted"), trusted);

    obj.insert(QStringLiteral("command"), command);
    obj.insert(QStringLiteral("arguments"), stringListToJson(arguments));
    obj.insert(QStringLiteral("workingDirectory"), workingDirectory);
    //  Environment variables of an MCP server routinely contain API keys, so
    //  only the *names* are persisted. The values live in SecretStore.
    //  MCP 服务器的环境变量常常包含 API Key，因此只持久化变量*名*，
    //  变量值保存在 SecretStore 中。
    obj.insert(QStringLiteral("environmentKeys"), stringListToJson(environment.keys()));

    obj.insert(QStringLiteral("url"), url);
    //  A bearer token must never be written to a project or settings file.
    //  bearerToken is intentionally not serialised: the flag below tells the
    //  reader that a token exists in SecretStore under "<id>/bearer".
    //  令牌绝不能写入工程或设置文件。此处刻意不序列化 bearerToken，
    //  下面的标志表示 SecretStore 的 "<id>/bearer" 下存在令牌。
    obj.insert(QStringLiteral("hasBearerToken"), !bearerToken.isEmpty());
    QJsonObject headerObj;
    for (auto it = headers.constBegin(); it != headers.constEnd(); ++it)
        headerObj.insert(it.key(), it.value());
    obj.insert(QStringLiteral("headers"), headerObj);

    obj.insert(QStringLiteral("allowedTools"), stringListToJson(allowedTools));
    obj.insert(QStringLiteral("blockedTools"), stringListToJson(blockedTools));
    return obj;
}

McpServerConfig McpServerConfig::fromJson(const QJsonObject &obj)
{
    const McpServerConfig defaults;
    McpServerConfig config;

    config.id = stringOr(obj, {"id"}, defaults.id);
    config.name = stringOr(obj, {"name"}, defaults.name);
    config.transport = mcpTransportFromString(stringOr(obj, {"transport"}, QStringLiteral("stdio")),
                                              McpTransport::Stdio);
    config.enabled = boolOr(obj, {"enabled"}, defaults.enabled);
    //  Trust is an explicit user decision and is never imported from a file.
    //  信任是用户的明确决定，绝不从文件导入。
    config.trusted = false;

    config.command = stringOr(obj, {"command"}, defaults.command);
    config.arguments = stringListOr(obj, {"arguments", "args"});
    config.workingDirectory = stringOr(obj, {"workingDirectory", "cwd"}, defaults.workingDirectory);
    config.environment = stringMapOr(obj, {"environment", "env"});

    config.url = stringOr(obj, {"url", "endpoint"}, defaults.url);
    //  An imported token is accepted (so a user can migrate a config by hand),
    //  but toJson() never writes one back out.
    //  允许导入令牌（便于用户手工迁移配置），但 toJson() 绝不写回令牌。
    config.bearerToken = stringOr(obj, {"bearerToken", "token"}, defaults.bearerToken);
    config.headers = stringMapOr(obj, {"headers"});

    config.allowedTools = stringListOr(obj, {"allowedTools"});
    config.blockedTools = stringListOr(obj, {"blockedTools"});
    return config;
}

// ---------------------------------------------------------------------------
//  AiError
//  AI 错误
// ---------------------------------------------------------------------------
QString toString(AiError::Kind kind)
{
    switch (kind) {
    case AiError::None:      return QStringLiteral("none");
    case AiError::Network:   return QStringLiteral("network");
    case AiError::Auth:      return QStringLiteral("auth");
    case AiError::RateLimit: return QStringLiteral("rate-limit");
    case AiError::BadRequest:return QStringLiteral("bad-request");
    case AiError::Server:    return QStringLiteral("server");
    case AiError::Cancelled: return QStringLiteral("cancelled");
    case AiError::Parse:     return QStringLiteral("parse");
    case AiError::Policy:    return QStringLiteral("policy");
    case AiError::Timeout:   return QStringLiteral("timeout");
    case AiError::Unknown:   return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

QString AiError::text(bool chinese) const
{
    //  Localised message first, then fall back to the kind so the UI never
    //  shows an empty error box.
    //  优先使用本地化消息，其次回退到错误种类，确保界面不会显示空错误框。
    QString primary = chinese ? messageZh : messageEn;
    if (primary.isEmpty())
        primary = chinese ? messageEn : messageZh;
    if (primary.isEmpty()) {
        primary = chinese ? QStringLiteral("AI 请求失败") : QStringLiteral("AI request failed");
        if (kind != None) {
            primary += QStringLiteral(" (")
                       + (chinese ? QStringLiteral("类型：") : QStringLiteral("kind: "))
                       + toString(kind) + QLatin1Char(')');
        }
    }

    QString out = primary;
    if (httpStatus > 0)
        out += QStringLiteral(" [HTTP %1]").arg(httpStatus);
    if (!detail.isEmpty())
        out += QStringLiteral(" — ") + detail;
    return out;
}

// ---------------------------------------------------------------------------
//  Shared helpers
//  共享辅助函数
// ---------------------------------------------------------------------------
QString defaultSystemPrompt(bool chinese)
{
    if (chinese) {
        return QStringLiteral(
            "你是 TI-UseAgent，TCTools-Build_Web（TC-BuildWeb）可视化网页搭建工具内置的助手。\n"
            "\n"
            "你的对话对象：正在制作网页的用户，通常并不是程序员。请用日常语言解释，避免术语，"
            "不要假设对方懂 HTML、CSS 或 JavaScript；必须提到技术细节时，先用一句话说明它的作用。\n"
            "\n"
            "工作方式：\n"
            "1. 优先使用已提供的工具。用户要求修改页面时，请调用对应的工具，而不是用文字描述让用户"
            "自己改代码；只有在确实没有合适工具时才给出文字步骤。\n"
            "2. 绝不编造元素 id、类名、页面名或文件路径。只能使用工具返回结果或工程概要中真实出现过"
            "的标识符；不确定就先查询，不要猜测。\n"
            "3. 任何破坏性操作都必须先征得同意：删除元素或页面、覆盖文件、执行命令、发布上线等。"
            "先用一句话说明会发生什么变化，得到用户确认后再执行；不要自作主张「顺手清理」数据。\n"
            "4. 绝不索取、回显、转述或记录任何凭据（API Key、访问令牌、密码、客户端密钥）。"
            "如果用户消息中出现了凭据，请忽略该内容，并提醒用户在设置界面的凭据输入框中填写——"
            "那里由操作系统的钥匙串或加密存储保管。\n"
            "5. 用用户所使用的语言回答；用户切换语言时你也跟着切换。\n"
            "6. 回答保持简短。说明你做了什么修改、结果如何、以及用户接下来可以做什么，"
            "不要复述整段代码或整个页面。\n"
            "7. 工具失败时，用通俗的话说明失败原因，并给出下一步建议；不要假装修改已经完成。\n"
            "8. 小步修改、逐步验证：修改前先读取当前状态，有依赖关系的编辑一次只做一步，"
            "确认无误后再继续。");
    }

    return QStringLiteral(
        "You are TI-UseAgent, the assistant built into TCTools-Build_Web (TC-BuildWeb), "
        "a visual web page builder.\n"
        "\n"
        "Who you are talking to: someone who is building a web page and is usually not a "
        "programmer. Use plain language, avoid jargon, and never assume they know HTML, CSS "
        "or JavaScript. When a technical detail is unavoidable, explain what it does in one "
        "sentence first.\n"
        "\n"
        "How to work:\n"
        "1. Prefer the tools you have been given. When the user asks for a change to the "
        "page, call the matching tool instead of describing the code they would have to "
        "write by hand; fall back to written steps only when no tool fits.\n"
        "2. Never invent element ids, class names, page names or file paths. Use only "
        "identifiers that actually came back from a tool or from the project outline; when "
        "you are unsure, look it up instead of guessing.\n"
        "3. Ask before anything destructive: deleting elements or pages, overwriting files, "
        "running commands, publishing. Say in one sentence what will change, then wait for a "
        "clear yes. Never delete data on your own initiative to \"tidy up\".\n"
        "4. Never ask for, echo, repeat or store credentials - API keys, access tokens, "
        "passwords or client secrets. If a message contains one, ignore it and tell the user "
        "to enter it in the credential field in Settings, where the operating system "
        "keychain or encrypted store keeps it.\n"
        "5. Reply in the language the user writes in, and follow them if they switch.\n"
        "6. Keep replies short. Say what you changed, what happened, and what the user can do "
        "next; do not repeat whole files or the entire page.\n"
        "7. When a tool fails, explain the failure in plain words and propose the next step. "
        "Never pretend a change succeeded.\n"
        "8. Work in small, verified steps: read the current state before editing, make one "
        "dependent change at a time, and check the result before continuing.");
}

QString redactForPrompt(const QString &text)
{
    //  Single source of truth for redaction lives in the core module so logs,
    //  transcripts and prompts all mask exactly the same patterns.
    //  遮蔽逻辑的唯一实现位于核心模块，保证日志、会话记录与提示词使用完全相同的规则。
    return redactSecrets(text);
}

QJsonObject buildOpenAiToolSchema(const QVector<ToolDescriptor> &tools)
{
    QJsonArray array;
    for (const ToolDescriptor &tool : tools) {
        if (tool.name.isEmpty())
            continue;
        array.append(tool.toOpenAiTool());
    }

    QJsonObject body;
    body.insert(QStringLiteral("tools"), array);
    //  "auto" lets the model decide whether a tool is needed at all, which is
    //  what a non-programmer expects from a chat box.
    //  "auto" 让模型自行判断是否需要调用工具，这符合非程序员对聊天框的预期。
    body.insert(QStringLiteral("tool_choice"), QStringLiteral("auto"));
    return body;
}

QJsonArray buildAnthropicToolSchema(const QVector<ToolDescriptor> &tools)
{
    QJsonArray array;
    for (const ToolDescriptor &tool : tools) {
        if (tool.name.isEmpty())
            continue;
        array.append(tool.toAnthropicTool());
    }
    return array;
}

} // namespace tcbw
