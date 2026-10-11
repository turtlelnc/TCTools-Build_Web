// ============================================================================
//  AiProvider.cpp -- HTTP client that speaks every supported wire protocol
//  AiProvider.cpp —— 支持全部已接入协议的 HTTP 客户端
//
//  Four wire flavours are implemented here:
//    * OpenAI compatible  (POST /chat/completions, SSE `data:` frames)
//    * Anthropic          (POST /v1/messages, SSE content blocks)
//    * Google Gemini      (POST /v1beta/models/<m>:streamGenerateContent, SSE)
//    * Ollama             (POST /api/chat, newline delimited JSON)
//  此处实现了四种接口协议：
//    * OpenAI 兼容
//    * Anthropic
//    * Google Gemini
//    * Ollama
//
//  SECURITY / 安全说明
//  The request body may contain user project text, so it is never logged in
//  full. Every error detail passes through `redactSecrets` before it reaches
//  the UI, the transcript or the log file.
//  请求正文可能包含用户工程文本，因此绝不完整记录到日志。所有错误详情在进入
//  界面、会话记录或日志文件之前都会经过 `redactSecrets` 遮蔽。
// ============================================================================

#include "AiProvider.h"

#include "AiTypes.h"
#include "AuthManager.h"
#include "IdGenerator.h"
#include "ProviderRegistry.h"
#include "Types.h"

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QList>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>

namespace tcbw {
namespace {

// ---------------------------------------------------------------------------
//  Small local helpers
//  本地小型辅助函数
// ---------------------------------------------------------------------------

//  Removes trailing slashes so URL splicing never doubles a separator.
//  去掉尾部斜杠，避免拼接 URL 时出现重复分隔符。
QString trimmedBase(const QString &value)
{
    QString out = value.trimmed();
    while (out.endsWith(QLatin1Char('/')))
        out.chop(1);
    return out;
}

//  Joins a base URL and a path with exactly one separator.
//  以恰好一个分隔符拼接基础地址与路径。
QString joinUrl(const QString &base, const QString &path)
{
    const QString b = trimmedBase(base);
    QString p = path.trimmed();
    while (p.startsWith(QLatin1Char('/')))
        p.remove(0, 1);
    if (b.isEmpty())
        return p;
    if (p.isEmpty())
        return b;
    return b + QLatin1Char('/') + p;
}

//  True when the vendor is served by Azure OpenAI (deployment based routing).
//  当厂商由 Azure OpenAI 提供（基于部署的路由）时返回 true。
bool isAzureVendor(const VendorInfo &vendor)
{
    const QString id = vendor.id.toLower();
    return id.contains(QStringLiteral("azure"))
           || vendor.baseUrl.contains(QStringLiteral("openai.azure.com"), Qt::CaseInsensitive)
           || vendor.baseUrl.contains(QStringLiteral("cognitiveservices.azure.com"),
                                      Qt::CaseInsensitive);
}

//  O-series reasoning models reject `max_tokens` and need `max_completion_tokens`.
//  Additionally they only accept the default temperature.
//  o 系列推理模型不接受 `max_tokens`，需要 `max_completion_tokens`，
//  并且只接受默认 temperature。
bool isReasoningModel(const QString &model)
{
    const QString m = model.trimmed().toLower();
    if (m.isEmpty())
        return false;
    const QString bare = m.section(QLatin1Char('/'), -1);
    static const QStringList prefixes = {
        QStringLiteral("o1"), QStringLiteral("o3"), QStringLiteral("o4"),
        QStringLiteral("o5")
    };
    for (const QString &p : prefixes) {
        if (bare == p || bare.startsWith(p + QLatin1Char('-')))
            return true;
    }
    return false;
}

//  OpenAI content can be a plain string or an array of typed parts.
//  OpenAI 的 content 可以是纯字符串，也可以是带类型的部件数组。
QJsonValue openAiContentValue(const ChatMessage &message)
{
    if (message.imageParts.isEmpty())
        return QJsonValue(message.content);

    QJsonArray parts;
    if (!message.content.isEmpty()) {
        QJsonObject textPart;
        textPart.insert(QStringLiteral("type"), QStringLiteral("text"));
        textPart.insert(QStringLiteral("text"), message.content);
        parts.append(textPart);
    }
    for (const QJsonValue &value : message.imageParts) {
        if (value.isObject())
            parts.append(value);
    }
    return parts.isEmpty() ? QJsonValue(message.content) : QJsonValue(parts);
}

//  Serialises the transcript for an OpenAI compatible endpoint.
//  为 OpenAI 兼容端点序列化对话消息。
QJsonArray openAiMessages(const QVector<ChatMessage> &messages)
{
    QJsonArray out;
    for (const ChatMessage &message : messages) {
        QJsonObject obj;
        switch (message.role) {
        case ChatRole::System:
            obj.insert(QStringLiteral("role"), QStringLiteral("system"));
            obj.insert(QStringLiteral("content"), message.content);
            break;
        case ChatRole::User:
            obj.insert(QStringLiteral("role"), QStringLiteral("user"));
            obj.insert(QStringLiteral("content"), openAiContentValue(message));
            break;
        case ChatRole::Assistant: {
            obj.insert(QStringLiteral("role"), QStringLiteral("assistant"));
            obj.insert(QStringLiteral("content"),
                       message.content.isEmpty() ? QJsonValue(QJsonValue::Null)
                                                 : QJsonValue(message.content));
            if (!message.toolCalls.isEmpty()) {
                QJsonArray calls;
                for (const ToolCall &call : message.toolCalls) {
                    QJsonObject fn;
                    fn.insert(QStringLiteral("name"), call.name);
                    fn.insert(QStringLiteral("arguments"),
                              QString::fromUtf8(QJsonDocument(call.arguments)
                                                    .toJson(QJsonDocument::Compact)));
                    QJsonObject entry;
                    entry.insert(QStringLiteral("id"), call.id);
                    entry.insert(QStringLiteral("type"), QStringLiteral("function"));
                    entry.insert(QStringLiteral("function"), fn);
                    calls.append(entry);
                }
                obj.insert(QStringLiteral("tool_calls"), calls);
            }
            break;
        }
        case ChatRole::Tool:
            obj.insert(QStringLiteral("role"), QStringLiteral("tool"));
            obj.insert(QStringLiteral("content"), message.content);
            obj.insert(QStringLiteral("tool_call_id"), message.toolCallId);
            break;
        }
        out.append(obj);
    }
    return out;
}

//  Serialises the transcript for Anthropic (system is handled separately).
//  为 Anthropic 序列化对话消息（system 单独处理）。
QJsonArray anthropicMessages(const QVector<ChatMessage> &messages)
{
    QJsonArray out;
    for (const ChatMessage &message : messages) {
        if (message.role == ChatRole::System)
            continue;

        QJsonObject obj;
        switch (message.role) {
        case ChatRole::User:
            obj.insert(QStringLiteral("role"), QStringLiteral("user"));
            obj.insert(QStringLiteral("content"), message.content);
            break;
        case ChatRole::Assistant: {
            QJsonArray blocks;
            if (!message.content.isEmpty()) {
                QJsonObject text;
                text.insert(QStringLiteral("type"), QStringLiteral("text"));
                text.insert(QStringLiteral("text"), message.content);
                blocks.append(text);
            }
            for (const ToolCall &call : message.toolCalls) {
                QJsonObject use;
                use.insert(QStringLiteral("type"), QStringLiteral("tool_use"));
                use.insert(QStringLiteral("id"), call.id);
                use.insert(QStringLiteral("name"), call.name);
                use.insert(QStringLiteral("input"), call.arguments);
                blocks.append(use);
            }
            obj.insert(QStringLiteral("role"), QStringLiteral("assistant"));
            obj.insert(QStringLiteral("content"), blocks);
            break;
        }
        case ChatRole::Tool: {
            QJsonObject block;
            block.insert(QStringLiteral("type"), QStringLiteral("tool_result"));
            block.insert(QStringLiteral("tool_use_id"), message.toolCallId);
            block.insert(QStringLiteral("content"), message.content);
            QJsonArray blocks;
            blocks.append(block);
            obj.insert(QStringLiteral("role"), QStringLiteral("user"));
            obj.insert(QStringLiteral("content"), blocks);
            break;
        }
        default:
            break;
        }
        out.append(obj);
    }
    return out;
}

//  Collects the concatenated system prompt of a transcript.
//  拼接对话中的系统提示词。
QString systemText(const QVector<ChatMessage> &messages)
{
    QStringList parts;
    for (const ChatMessage &message : messages) {
        if (message.role == ChatRole::System && !message.content.isEmpty())
            parts.append(message.content);
    }
    return parts.join(QStringLiteral("\n\n"));
}

//  Serialises the transcript for Gemini `contents`.
//  为 Gemini 的 `contents` 序列化对话消息。
QJsonArray geminiContents(const QVector<ChatMessage> &messages)
{
    QJsonArray out;
    for (const ChatMessage &message : messages) {
        if (message.role == ChatRole::System)
            continue;

        QJsonObject entry;
        entry.insert(QStringLiteral("role"),
                     message.role == ChatRole::Assistant ? QStringLiteral("model")
                                                         : QStringLiteral("user"));
        QJsonArray parts;
        QString text = message.content;
        if (message.role == ChatRole::Tool) {
            QJsonObject functionResponse;
            functionResponse.insert(QStringLiteral("name"),
                                    message.name.isEmpty() ? QStringLiteral("tool")
                                                           : message.name);
            QJsonObject response;
            response.insert(QStringLiteral("result"), text);
            functionResponse.insert(QStringLiteral("response"), response);
            QJsonObject part;
            part.insert(QStringLiteral("functionResponse"), functionResponse);
            parts.append(part);
        } else if (message.role == ChatRole::Assistant) {
            if (!text.isEmpty()) {
                QJsonObject part;
                part.insert(QStringLiteral("text"), text);
                parts.append(part);
            }
            for (const ToolCall &call : message.toolCalls) {
                QJsonObject functionCall;
                functionCall.insert(QStringLiteral("name"), call.name);
                functionCall.insert(QStringLiteral("args"), call.arguments);
                QJsonObject part;
                part.insert(QStringLiteral("functionCall"), functionCall);
                parts.append(part);
            }
        } else {
            QJsonObject part;
            part.insert(QStringLiteral("text"), text);
            parts.append(part);
        }
        if (parts.isEmpty()) {
            QJsonObject part;
            part.insert(QStringLiteral("text"), QString());
            parts.append(part);
        }
        entry.insert(QStringLiteral("parts"), parts);
        out.append(entry);
    }
    return out;
}

//  Maps chat messages straight onto Ollama's wire shape.
//  将对话消息直接映射为 Ollama 的接口结构。
QJsonArray ollamaMessages(const QVector<ChatMessage> &messages)
{
    QJsonArray out;
    for (const ChatMessage &message : messages) {
        QJsonObject obj;
        switch (message.role) {
        case ChatRole::System:
            obj.insert(QStringLiteral("role"), QStringLiteral("system"));
            obj.insert(QStringLiteral("content"), message.content);
            break;
        case ChatRole::User:
            obj.insert(QStringLiteral("role"), QStringLiteral("user"));
            obj.insert(QStringLiteral("content"), openAiContentValue(message));
            break;
        case ChatRole::Assistant: {
            obj.insert(QStringLiteral("role"), QStringLiteral("assistant"));
            obj.insert(QStringLiteral("content"), message.content);
            if (!message.toolCalls.isEmpty()) {
                QJsonArray calls;
                for (const ToolCall &call : message.toolCalls) {
                    QJsonObject fn;
                    fn.insert(QStringLiteral("name"), call.name);
                    fn.insert(QStringLiteral("arguments"), call.arguments);
                    QJsonObject entry;
                    entry.insert(QStringLiteral("function"), fn);
                    calls.append(entry);
                }
                obj.insert(QStringLiteral("tool_calls"), calls);
            }
            break;
        }
        case ChatRole::Tool:
            obj.insert(QStringLiteral("role"), QStringLiteral("tool"));
            obj.insert(QStringLiteral("content"), message.content);
            if (!message.name.isEmpty())
                obj.insert(QStringLiteral("tool_name"), message.name);
            break;
        }
        out.append(obj);
    }
    return out;
}

//  Normalises one line ending style and splits an SSE payload into lines.
//  统一换行风格，并将 SSE 数据块拆分为若干行。
QStringList payloadLines(const QByteArray &payload)
{
    QString text = QString::fromUtf8(payload);
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

//  Strips a leading `data:` (or `event:`) prefix from one SSE line.
//  去除一行 SSE 数据开头的 `data:`（或 `event:`）前缀。
QString stripSseField(const QString &line, const QString &field)
{
    QString trimmed = line.trimmed();
    if (!trimmed.startsWith(field, Qt::CaseInsensitive))
        return QString();
    trimmed.remove(0, field.size());
    if (trimmed.startsWith(QLatin1Char(':')))
        trimmed.remove(0, 1);
    return trimmed.trimmed();
}

//  Parses a JSON object from text, returning an empty object on failure.
//  从文本解析 JSON 对象，失败时返回空对象。
QJsonObject parseObject(const QString &text)
{
    if (text.trimmed().isEmpty())
        return QJsonObject();
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError)
        return QJsonObject();
    return doc.object();
}

//  Converts an OpenAI style arguments string into an object, never throwing.
//  将 OpenAI 风格参数字符串转换为对象，绝不抛出异常。
QJsonObject argumentsFromText(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return QJsonObject();
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(trimmed.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError) {
        //  Partial JSON while streaming is normal: expose the raw fragment so
        //  the caller can keep accumulating.
        //  流式过程中出现不完整 JSON 属正常现象：暴露原始片段供调用方继续累积。
        QJsonObject fallback;
        fallback.insert(QStringLiteral("__raw"), trimmed);
        return fallback;
    }
    if (doc.isObject())
        return doc.object();
    QJsonObject wrapped;
    wrapped.insert(QStringLiteral("value"), doc.toVariant().toJsonValue());
    return wrapped;
}

//  Number of bytes accepted for one error detail.
//  单条错误详情允许的最大字节数。
constexpr int kMaxDetailChars = 1200;

//  Truncates text for display without splitting a UTF-8 sequence.
//  为显示截断文本，且不切断 UTF-8 序列。
QString clampDetail(const QString &text)
{
    if (text.size() <= kMaxDetailChars)
        return text;
    return text.left(kMaxDetailChars) + QStringLiteral("...");
}

} // namespace

// ---------------------------------------------------------------------------
//  AiClient::Pending
//  One tracked network request.
//  一个被跟踪的网络请求。
// ---------------------------------------------------------------------------
struct AiClient::Pending
{
    QString requestId;
    QNetworkReply *reply = nullptr;
    QTimer *timer = nullptr;
    ApiFlavor flavor = ApiFlavor::OpenAiCompatible;
    bool streaming = false;
    bool done = false;            // no signal may be emitted once true / 为真后不得再发信号

    QByteArray buffer;            // partial SSE line buffer / 未完成的 SSE 行缓冲
    QString text;                 // accumulated assistant text / 累积的助手文本
    QString reasoning;
    QVector<ToolCall> toolCalls;  // completed (or completed-so-far) calls / 已完成的调用
    QJsonObject partialTools;     // index -> {id,name,arguments} / 累积中的调用
    bool toolsEmitted = false;
    int inputTokens = 0;
    int outputTokens = 0;
    bool usageEmitted = false;
};

// ---------------------------------------------------------------------------
//  Stream accumulation helpers.
//  They live at file scope because the translation unit already owns the
//  definition of the private `Pending` type; templating them on the pending
//  type keeps the frozen header untouched.
//  流式累积辅助函数。它们位于文件作用域，因为本编译单元持有私有 `Pending`
//  类型的定义；以该类型作模板参数可使冻结的头文件保持不变。
// ---------------------------------------------------------------------------

//  Builds the ordered tool-call list from the per-turn accumulation map.
//  由本轮累积映射构建有序的工具调用列表。
template <typename PendingT>
QVector<ToolCall> pendingToolCalls(PendingT *pending)
{
    QVector<ToolCall> calls;
    QList<int> indexes;
    for (auto it = pending->partialTools.constBegin(); it != pending->partialTools.constEnd(); ++it) {
        bool ok = false;
        const int index = it.key().toInt(&ok);
        if (ok)
            indexes.append(index);
    }
    std::sort(indexes.begin(), indexes.end());

    for (int index : indexes) {
        const QJsonObject entry = pending->partialTools.value(QString::number(index)).toObject();
        ToolCall call;
        call.id = entry.value(QStringLiteral("id")).toString();
        call.name = entry.value(QStringLiteral("name")).toString();
        if (call.id.isEmpty())
            call.id = QStringLiteral("call_%1").arg(index);
        if (call.name.isEmpty())
            continue;
        const QString raw = entry.value(QStringLiteral("arguments")).toString();
        call.arguments = argumentsFromText(raw);
        calls.append(call);
    }
    return calls;
}

//  Applies one parsed stream event to the pending request.
//  将一个已解析的流事件应用到待处理请求。
template <typename PendingT>
void applyStreamEvent(AiClient *client, PendingT *pending,
                      const AiClient::StreamEvent &event)
{
    if (!pending || pending->done)
        return;

    switch (event.type) {
    case AiClient::StreamEvent::Text:
        if (event.text.isEmpty())
            return;
        pending->text += event.text;
        emit client->textDelta(pending->requestId, event.text);
        return;
    case AiClient::StreamEvent::Reasoning:
        if (event.text.isEmpty())
            return;
        pending->reasoning += event.text;
        emit client->reasoningDelta(pending->requestId, event.text);
        return;
    case AiClient::StreamEvent::ToolCallStart:
    case AiClient::StreamEvent::ToolCallDelta: {
        //  OpenAI accumulates per `index`; Anthropic streams a single block at a
        //  time, so index 0 is the correct slot when none is announced.
        //  OpenAI 按 `index` 累积；Anthropic 一次只有一个块，因此未声明索引时
        //  使用 0 号槽位。
        const QString key = QStringLiteral("0");
        QJsonObject entry = pending->partialTools.value(key).toObject();
        if (entry.isEmpty())
            entry.insert(QStringLiteral("arguments"), QString());
        if (!event.toolCallId.isEmpty())
            entry.insert(QStringLiteral("id"), event.toolCallId);
        if (!event.toolName.isEmpty())
            entry.insert(QStringLiteral("name"), event.toolName);
        if (!event.argumentsDelta.isEmpty()) {
            const QString previous = entry.value(QStringLiteral("arguments")).toString();
            entry.insert(QStringLiteral("arguments"), previous + event.argumentsDelta);
        }
        pending->partialTools.insert(key, entry);
        return;
    }
    case AiClient::StreamEvent::ToolCallDone: {
        ToolCall call;
        call.id = event.toolCallId;
        if (call.id.isEmpty()) {
            const QJsonObject entry = pending->partialTools.value(QStringLiteral("0")).toObject();
            call.id = entry.value(QStringLiteral("id")).toString();
        }
        call.name = event.toolName;
        if (call.name.isEmpty()) {
            const QJsonObject entry = pending->partialTools.value(QStringLiteral("0")).toObject();
            call.name = entry.value(QStringLiteral("name")).toString();
        }
        if (call.id.isEmpty())
            call.id = QStringLiteral("call_%1").arg(pending->toolCalls.size());
        call.arguments = argumentsFromText(event.argumentsDelta);

        pending->toolCalls.append(call);
        pending->partialTools.remove(QStringLiteral("0"));

        QVector<ToolCall> ready = pending->toolCalls;
        pending->toolsEmitted = true;
        emit client->toolCallReady(pending->requestId, ready);
        return;
    }
    case AiClient::StreamEvent::Usage:
        pending->inputTokens = event.inputTokens;
        pending->outputTokens = event.outputTokens;
        if (pending->inputTokens > 0 || pending->outputTokens > 0) {
            pending->usageEmitted = true;
            emit client->usageReported(pending->requestId, pending->inputTokens,
                                       pending->outputTokens);
        }
        return;
    case AiClient::StreamEvent::Done: {
        //  Flush any complete-but-unannounced tool call before finishing.
        //  结束前冲掉已完整但尚未通知的工具调用。
        const QVector<ToolCall> calls = pendingToolCalls(pending);
        if (!calls.isEmpty() && !pending->toolsEmitted) {
            pending->toolCalls = calls;
            pending->toolsEmitted = true;
            emit client->toolCallReady(pending->requestId, calls);
        }
        return;
    }
    case AiClient::StreamEvent::Error: {
        pending->done = true;
        AiError error;
        error.kind = AiError::Server;
        error.messageEn = event.errorMessage.isEmpty()
                              ? QStringLiteral("The provider reported a streaming error.")
                              : event.errorMessage;
        error.messageZh = QStringLiteral("厂商报告了流式错误：%1").arg(event.errorMessage);
        error.detail = redactSecrets(event.errorMessage);
        if (pending->timer) {
            pending->timer->stop();
            pending->timer->deleteLater();
            pending->timer = nullptr;
        }
        if (pending->reply) {
            pending->reply->disconnect(client);
            pending->reply->abort();
            pending->reply->deleteLater();
            pending->reply = nullptr;
        }
        emit client->failed(pending->requestId, error);
        return;
    }
    case AiClient::StreamEvent::Ignore:
    default:
        return;
    }
}

// ---------------------------------------------------------------------------
//  AiClient
//  AiClient 构造与析构
// ---------------------------------------------------------------------------
AiClient::AiClient(QNetworkAccessManager *network, AuthManager *auth, QObject *parent)
    : QObject(parent)
    , m_network(network)
    , m_auth(auth)
{
}

AiClient::~AiClient()
{
    cancelAll();
}

void AiClient::setVendor(const VendorInfo &vendor)
{
    m_vendor = vendor;
}

void AiClient::setConfig(const ProviderConfig &config)
{
    m_config = config;
}

// ---------------------------------------------------------------------------
//  chatUrl
//  构建对话请求地址
// ---------------------------------------------------------------------------
QUrl AiClient::chatUrl(const VendorInfo &vendor, const ProviderConfig &config)
{
    const QString base = trimmedBase(config.effectiveBaseUrl(vendor));
    const QString model = config.model.trimmed();

    //  Azure OpenAI routes through a deployment, not a model id.
    //  Azure OpenAI 通过部署路由，而不是模型 id。
    if (isAzureVendor(vendor)) {
        const QString deployment = config.azureDeployment.trimmed().isEmpty()
                                       ? model
                                       : config.azureDeployment.trimmed();
        const QString version = config.azureApiVersion.trimmed().isEmpty()
                                    ? QStringLiteral("2024-10-21")
                                    : config.azureApiVersion.trimmed();
        QString url = joinUrl(base,
                              QStringLiteral("openai/deployments/%1/chat/completions")
                                  .arg(deployment));
        QUrl out(url);
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("api-version"), version);
        out.setQuery(query);
        return out;
    }

    switch (vendor.flavor) {
    case ApiFlavor::Anthropic: {
        QString path = vendor.chatPath.trimmed();
        if (path.isEmpty())
            path = QStringLiteral("/v1/messages");
        return QUrl(joinUrl(base, path));
    }
    case ApiFlavor::GoogleGemini: {
        //  Gemini encodes the model in the path and uses query parameters to
        //  select streaming. A vendor preset normally leaves chatPath empty.
        //  Gemini 在路径中携带模型，并通过查询参数选择流式；厂商预设通常
        //  留空 chatPath。
        const QString encoded = QUrl::toPercentEncoding(model);
        QString path;
        const QString configured = vendor.chatPath.trimmed();
        if (!configured.isEmpty() && configured.contains(QStringLiteral("%1")))
            path = configured;
        else
            path = QStringLiteral("/v1beta/models/%1:streamGenerateContent").arg(encoded);

        if (path.contains(QStringLiteral("%1")))
            path = path.arg(encoded);

        if (!config.streaming) {
            path.replace(QStringLiteral(":streamGenerateContent"),
                         QStringLiteral(":generateContent"));
        }

        QUrl out(joinUrl(base, path));
        if (config.streaming) {
            //  `alt=sse` turns the chunked JSON array into readable SSE frames.
            //  `alt=sse` 将分块 JSON 数组变为可读的 SSE 帧。
            QUrlQuery query(out.query());
            query.addQueryItem(QStringLiteral("alt"), QStringLiteral("sse"));
            out.setQuery(query);
        }
        return out;
    }
    case ApiFlavor::Ollama: {
        QString path = vendor.chatPath.trimmed();
        if (path.isEmpty() || !path.contains(QStringLiteral("/api")))
            path = QStringLiteral("/api/chat");
        return QUrl(joinUrl(base, path));
    }
    case ApiFlavor::AwsBedrock:
    case ApiFlavor::Custom:
    case ApiFlavor::OpenAiCompatible:
    default: {
        QString path = vendor.chatPath.trimmed();
        if (path.isEmpty())
            path = QStringLiteral("/v1/chat/completions");
        return QUrl(joinUrl(base, path));
    }
    }
}

// ---------------------------------------------------------------------------
//  baseHeaders
//  构建基础请求头
// ---------------------------------------------------------------------------
QMap<QString, QString> AiClient::baseHeaders(ApiFlavor flavor)
{
    QMap<QString, QString> headers;
    headers.insert(QStringLiteral("Content-Type"), QStringLiteral("application/json"));
    headers.insert(QStringLiteral("Accept"), QStringLiteral("text/event-stream"));
    headers.insert(QStringLiteral("User-Agent"),
                   QStringLiteral("TCTools-Build_Web/%1").arg(versionString()));

    //  Anthropic requires an explicit API version header.
    //  Anthropic 需要显式的 API 版本头。
    if (flavor == ApiFlavor::Anthropic) {
        if (!headers.contains(QStringLiteral("anthropic-version")))
            headers.insert(QStringLiteral("anthropic-version"), QStringLiteral("2023-06-01"));
    }
    return headers;
}

// ---------------------------------------------------------------------------
//  buildRequestBody
//  构建请求正文
// ---------------------------------------------------------------------------
QJsonObject AiClient::buildRequestBody(ApiFlavor flavor,
                                       const QVector<ChatMessage> &messages,
                                       const QVector<ToolDescriptor> &tools,
                                       const ProviderConfig &config,
                                       const QString &model,
                                       bool stream)
{
    QJsonObject body;
    const QString effectiveModel = model.trimmed();
    const bool toolsEnabled = config.toolsEnabled && !tools.isEmpty();

    switch (flavor) {
    case ApiFlavor::Anthropic: {
        body.insert(QStringLiteral("model"), effectiveModel);
        const QString system = systemText(messages);
        if (!system.isEmpty())
            body.insert(QStringLiteral("system"), system);
        body.insert(QStringLiteral("messages"), anthropicMessages(messages));
        //  max_tokens is mandatory for the Messages API.
        //  Messages API 必须提供 max_tokens。
        body.insert(QStringLiteral("max_tokens"),
                    config.maxTokens > 0 ? config.maxTokens : 4096);
        if (stream)
            body.insert(QStringLiteral("stream"), true);
        body.insert(QStringLiteral("temperature"), config.temperature);
        if (toolsEnabled) {
            body.insert(QStringLiteral("tools"), buildAnthropicToolSchema(tools));
            body.insert(QStringLiteral("tool_choice"),
                        QJsonObject{ { QStringLiteral("type"), QStringLiteral("auto") } });
        }
        break;
    }
    case ApiFlavor::GoogleGemini: {
        body.insert(QStringLiteral("contents"), geminiContents(messages));
        const QString system = systemText(messages);
        if (!system.isEmpty()) {
            QJsonObject instruction;
            QJsonArray parts;
            QJsonObject part;
            part.insert(QStringLiteral("text"), system);
            parts.append(part);
            instruction.insert(QStringLiteral("parts"), parts);
            body.insert(QStringLiteral("systemInstruction"), instruction);
        }
        QJsonObject generation;
        generation.insert(QStringLiteral("temperature"), config.temperature);
        if (config.maxTokens > 0)
            generation.insert(QStringLiteral("maxOutputTokens"), config.maxTokens);
        body.insert(QStringLiteral("generationConfig"), generation);
        if (toolsEnabled) {
            QJsonArray declarations;
            for (const ToolDescriptor &tool : tools) {
                QJsonObject fn;
                fn.insert(QStringLiteral("name"), tool.name);
                fn.insert(QStringLiteral("description"),
                          tool.descriptionEn.isEmpty() ? tool.name : tool.descriptionEn);
                QJsonObject parameters = tool.inputSchema;
                if (parameters.isEmpty()) {
                    //  Gemini rejects a declaration that carries no schema.
                    //  Gemini 会拒绝未携带 schema 的声明。
                    parameters.insert(QStringLiteral("type"), QStringLiteral("object"));
                    parameters.insert(QStringLiteral("properties"), QJsonObject());
                }
                fn.insert(QStringLiteral("parameters"), parameters);
                declarations.append(fn);
            }
            QJsonObject group;
            group.insert(QStringLiteral("functionDeclarations"), declarations);
            QJsonArray toolArray;
            toolArray.append(group);
            body.insert(QStringLiteral("tools"), toolArray);
        }
        break;
    }
    case ApiFlavor::Ollama: {
        body.insert(QStringLiteral("model"), effectiveModel);
        body.insert(QStringLiteral("messages"), ollamaMessages(messages));
        body.insert(QStringLiteral("stream"), stream);
        QJsonObject options;
        options.insert(QStringLiteral("temperature"), config.temperature);
        if (config.maxTokens > 0)
            options.insert(QStringLiteral("num_predict"), config.maxTokens);
        body.insert(QStringLiteral("options"), options);
        if (toolsEnabled) {
            QJsonArray array;
            for (const ToolDescriptor &tool : tools) {
                QJsonObject fn;
                fn.insert(QStringLiteral("name"), tool.name);
                fn.insert(QStringLiteral("description"),
                          tool.descriptionEn.isEmpty() ? tool.name : tool.descriptionEn);
                fn.insert(QStringLiteral("parameters"), tool.inputSchema);
                QJsonObject entry;
                entry.insert(QStringLiteral("type"), QStringLiteral("function"));
                entry.insert(QStringLiteral("function"), fn);
                array.append(entry);
            }
            body.insert(QStringLiteral("tools"), array);
        }
        break;
    }
    case ApiFlavor::AwsBedrock:
    case ApiFlavor::Custom:
    case ApiFlavor::OpenAiCompatible:
    default: {
        body.insert(QStringLiteral("model"), effectiveModel);
        body.insert(QStringLiteral("messages"), openAiMessages(messages));
        body.insert(QStringLiteral("stream"), stream);
        if (stream) {
            QJsonObject streamOptions;
            streamOptions.insert(QStringLiteral("include_usage"), true);
            body.insert(QStringLiteral("stream_options"), streamOptions);
        }

        const bool reasoning = isReasoningModel(effectiveModel);
        if (config.maxTokens > 0) {
            if (reasoning)
                body.insert(QStringLiteral("max_completion_tokens"), config.maxTokens);
            else
                body.insert(QStringLiteral("max_tokens"), config.maxTokens);
        }
        //  Reasoning models only accept the default temperature.
        //  推理模型只接受默认 temperature。
        if (!reasoning)
            body.insert(QStringLiteral("temperature"), config.temperature);

        if (toolsEnabled) {
            body.insert(QStringLiteral("tools"), buildOpenAiToolSchema(tools).value(QStringLiteral("tools")));
            body.insert(QStringLiteral("tool_choice"), QStringLiteral("auto"));
        }
        break;
    }
    }
    return body;
}

// ---------------------------------------------------------------------------
//  parseStreamChunk -- pure, allocation light, never throws
//  解析流式数据块 —— 纯函数、开销小、绝不抛出
// ---------------------------------------------------------------------------
AiClient::StreamEvent AiClient::parseStreamChunk(ApiFlavor flavor, const QByteArray &ssePayload)
{
    StreamEvent event;
    if (ssePayload.isEmpty())
        return event;

    //  Ollama speaks newline delimited JSON, not SSE. Gemini without `alt=sse`
    //  streams a JSON array; both are handled by scanning for objects.
    //  Ollama 使用换行分隔 JSON 而非 SSE。未使用 `alt=sse` 的 Gemini 会流式
    //  返回 JSON 数组；两种情况都通过扫描对象来处理。
    const QStringList lines = payloadLines(ssePayload);

    //  Collect every `data:` payload first: one chunk may hold several frames.
    //  先收集全部 `data:` 载荷：一个数据块可能包含多帧。
    QStringList dataLines;
    for (const QString &line : lines) {
        const QString data = stripSseField(line, QStringLiteral("data:"));
        if (data.isEmpty() && !line.trimmed().startsWith(QStringLiteral("data:"), Qt::CaseInsensitive))
            continue;
        if (data == QStringLiteral("[DONE]")) {
            event.type = StreamEvent::Done;
            return event;
        }
        if (!data.isEmpty())
            dataLines.append(data);
    }

    if (dataLines.isEmpty()) {
        //  Gemini JSON-array streaming and Ollama NDJSON land here.
        //  Gemini 的 JSON 数组流与 Ollama 的 NDJSON 会走到这里。
        for (const QString &line : lines) {
            const QString trimmed = line.trimmed();
            if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char(':')))
                continue;
            if (trimmed.startsWith(QStringLiteral("event:"), Qt::CaseInsensitive))
                continue;
            dataLines.append(trimmed);
        }
    } else if (dataLines.size() == 1 && dataLines.first().startsWith(QLatin1Char('['))) {
        //  Some gateways forward the raw Gemini array; split it into objects.
        //  某些网关会直接转发 Gemini 原始数组；将其拆成对象。
        QString joined = dataLines.first();
        dataLines.clear();
        const QStringList pieces = joined.split(QStringLiteral("},{"));
        for (int i = 0; i < pieces.size(); ++i) {
            QString piece = pieces.at(i);
            if (i > 0)
                piece.prepend(QLatin1Char('{'));
            if (i + 1 < pieces.size())
                piece.append(QLatin1Char('}'));
            dataLines.append(piece);
        }
    }

    for (const QString &data : dataLines) {
        if (data.trimmed().isEmpty())
            continue;

        const QJsonObject obj = parseObject(data);
        if (obj.isEmpty())
            continue;

        //  A JSON-RPC style error frame.
        //  JSON-RPC 风格的错误帧。
        if (obj.contains(QStringLiteral("error"))) {
            const QJsonValue errorValue = obj.value(QStringLiteral("error"));
            QString message;
            if (errorValue.isObject())
                message = errorValue.toObject().value(QStringLiteral("message")).toString();
            else
                message = errorValue.toString();
            if (!message.isEmpty()) {
                event.type = StreamEvent::Error;
                event.errorMessage = redactSecrets(message);
                return event;
            }
        }

        switch (flavor) {
        case ApiFlavor::Anthropic: {
            const QString type = obj.value(QStringLiteral("type")).toString();
            if (type == QStringLiteral("ping") || type == QStringLiteral("message_start")) {
                const QJsonObject message = obj.value(QStringLiteral("message")).toObject();
                const QJsonObject usage = message.value(QStringLiteral("usage")).toObject();
                if (usage.contains(QStringLiteral("input_tokens"))) {
                    event.type = StreamEvent::Usage;
                    event.inputTokens = usage.value(QStringLiteral("input_tokens")).toInt();
                    event.outputTokens = usage.value(QStringLiteral("output_tokens")).toInt(0);
                    return event;
                }
                continue;
            }
            if (type == QStringLiteral("content_block_start")) {
                const QJsonObject block = obj.value(QStringLiteral("content_block")).toObject();
                const QString blockType = block.value(QStringLiteral("type")).toString();
                if (blockType == QStringLiteral("tool_use")) {
                    event.type = StreamEvent::ToolCallStart;
                    event.toolCallId = block.value(QStringLiteral("id")).toString();
                    event.toolName = block.value(QStringLiteral("name")).toString();
                    return event;
                }
                if (blockType == QStringLiteral("text")) {
                    const QString text = block.value(QStringLiteral("text")).toString();
                    if (!text.isEmpty()) {
                        event.type = StreamEvent::Text;
                        event.text = text;
                        return event;
                    }
                }
                continue;
            }
            if (type == QStringLiteral("content_block_delta")) {
                const QJsonObject delta = obj.value(QStringLiteral("delta")).toObject();
                const QString deltaType = delta.value(QStringLiteral("type")).toString();
                if (deltaType == QStringLiteral("text_delta")) {
                    event.type = StreamEvent::Text;
                    event.text = delta.value(QStringLiteral("text")).toString();
                    return event;
                }
                if (deltaType == QStringLiteral("thinking_delta")) {
                    event.type = StreamEvent::Reasoning;
                    event.text = delta.value(QStringLiteral("thinking")).toString();
                    return event;
                }
                if (deltaType == QStringLiteral("input_json_delta")) {
                    event.type = StreamEvent::ToolCallDelta;
                    event.argumentsDelta = delta.value(QStringLiteral("partial_json")).toString();
                    return event;
                }
                continue;
            }
            if (type == QStringLiteral("message_delta")) {
                const QJsonObject usage = obj.value(QStringLiteral("usage")).toObject();
                event.type = StreamEvent::Usage;
                event.outputTokens = usage.value(QStringLiteral("output_tokens")).toInt(0);
                event.inputTokens = usage.value(QStringLiteral("input_tokens")).toInt(0);
                return event;
            }
            if (type == QStringLiteral("message_stop")) {
                event.type = StreamEvent::Done;
                return event;
            }
            continue;
        }
        case ApiFlavor::GoogleGemini: {
            const QJsonArray candidates = obj.value(QStringLiteral("candidates")).toArray();
            if (!candidates.isEmpty()) {
                const QJsonObject candidate = candidates.first().toObject();
                const QJsonObject content = candidate.value(QStringLiteral("content")).toObject();
                const QJsonArray parts = content.value(QStringLiteral("parts")).toArray();
                for (const QJsonValue &partValue : parts) {
                    const QJsonObject part = partValue.toObject();
                    const QString text = part.value(QStringLiteral("text")).toString();
                    if (!text.isEmpty()) {
                        event.type = StreamEvent::Text;
                        event.text = text;
                        return event;
                    }
                    if (part.contains(QStringLiteral("functionCall"))) {
                        const QJsonObject call =
                            part.value(QStringLiteral("functionCall")).toObject();
                        event.type = StreamEvent::ToolCallDone;
                        event.toolName = call.value(QStringLiteral("name")).toString();
                        event.argumentsDelta = QString::fromUtf8(
                            QJsonDocument(call.value(QStringLiteral("args")).toObject())
                                .toJson(QJsonDocument::Compact));
                        return event;
                    }
                }
            }
            const QJsonObject usage = obj.value(QStringLiteral("usageMetadata")).toObject();
            if (usage.contains(QStringLiteral("promptTokenCount"))) {
                event.type = StreamEvent::Usage;
                event.inputTokens = usage.value(QStringLiteral("promptTokenCount")).toInt(0);
                event.outputTokens = usage.value(QStringLiteral("candidatesTokenCount")).toInt(0);
                return event;
            }
            continue;
        }
        case ApiFlavor::Ollama: {
            if (obj.contains(QStringLiteral("message"))) {
                const QJsonObject message = obj.value(QStringLiteral("message")).toObject();
                const QJsonArray calls = message.value(QStringLiteral("tool_calls")).toArray();
                if (!calls.isEmpty()) {
                    const QJsonObject call = calls.first().toObject();
                    const QJsonObject fn = call.value(QStringLiteral("function")).toObject();
                    event.type = StreamEvent::ToolCallDone;
                    event.toolName = fn.value(QStringLiteral("name")).toString();
                    event.toolCallId = call.value(QStringLiteral("id")).toString();
                    event.argumentsDelta = QString::fromUtf8(
                        QJsonDocument(fn.value(QStringLiteral("arguments")).toObject())
                            .toJson(QJsonDocument::Compact));
                    return event;
                }
                const QString thinking = message.value(QStringLiteral("thinking")).toString();
                const QString text = message.value(QStringLiteral("content")).toString();
                if (!text.isEmpty()) {
                    event.type = StreamEvent::Text;
                    event.text = text;
                    return event;
                }
                if (!thinking.isEmpty()) {
                    event.type = StreamEvent::Reasoning;
                    event.text = thinking;
                    return event;
                }
            }
            if (obj.value(QStringLiteral("done")).toBool(false)) {
                event.type = StreamEvent::Usage;
                event.inputTokens = obj.value(QStringLiteral("prompt_eval_count")).toInt(0);
                event.outputTokens = obj.value(QStringLiteral("eval_count")).toInt(0);
                return event;
            }
            continue;
        }
        case ApiFlavor::AwsBedrock:
        case ApiFlavor::Custom:
        case ApiFlavor::OpenAiCompatible:
        default: {
            const QJsonArray choices = obj.value(QStringLiteral("choices")).toArray();
            if (choices.isEmpty()) {
                const QJsonObject usage = obj.value(QStringLiteral("usage")).toObject();
                if (usage.contains(QStringLiteral("prompt_tokens"))) {
                    event.type = StreamEvent::Usage;
                    event.inputTokens = usage.value(QStringLiteral("prompt_tokens")).toInt(0);
                    event.outputTokens = usage.value(QStringLiteral("completion_tokens")).toInt(0);
                    return event;
                }
                continue;
            }
            const QJsonObject choice = choices.first().toObject();
            const QJsonObject delta = choice.value(QStringLiteral("delta")).toObject();

            const QJsonArray calls = delta.value(QStringLiteral("tool_calls")).toArray();
            if (!calls.isEmpty()) {
                const QJsonObject call = calls.first().toObject();
                const QJsonObject fn = call.value(QStringLiteral("function")).toObject();
                const QString name = fn.value(QStringLiteral("name")).toString();
                const QString arguments = fn.value(QStringLiteral("arguments")).toString();
                event.toolCallId = call.value(QStringLiteral("id")).toString();
                event.argumentsDelta = arguments;
                if (!name.isEmpty()) {
                    event.type = StreamEvent::ToolCallStart;
                    event.toolName = name;
                } else {
                    event.type = StreamEvent::ToolCallDelta;
                }
                return event;
            }

            const QString reasoning = delta.value(QStringLiteral("reasoning_content")).toString();
            if (!reasoning.isEmpty()) {
                event.type = StreamEvent::Reasoning;
                event.text = reasoning;
                return event;
            }

            const QString content = delta.value(QStringLiteral("content")).toString();
            if (!content.isEmpty()) {
                event.type = StreamEvent::Text;
                event.text = content;
                return event;
            }

            const QString finish = choice.value(QStringLiteral("finish_reason")).toString();
            if (!finish.isEmpty()) {
                event.type = finish == QStringLiteral("error") ? StreamEvent::Error
                                                                : StreamEvent::Done;
                if (event.type == StreamEvent::Error)
                    event.errorMessage = QStringLiteral("The model reported a stream error.");
                return event;
            }

            const QJsonObject usage = obj.value(QStringLiteral("usage")).toObject();
            if (usage.contains(QStringLiteral("prompt_tokens"))) {
                event.type = StreamEvent::Usage;
                event.inputTokens = usage.value(QStringLiteral("prompt_tokens")).toInt(0);
                event.outputTokens = usage.value(QStringLiteral("completion_tokens")).toInt(0);
                return event;
            }
            continue;
        }
        }
    }

    return event;
}

// ---------------------------------------------------------------------------
//  parseResponse -- non-streaming equivalent of parseStreamChunk
//  解析完整响应 —— parseStreamChunk 的非流式对应实现
// ---------------------------------------------------------------------------
bool AiClient::parseResponse(ApiFlavor flavor, const QJsonObject &body, ChatMessage *out,
                             AiError *error)
{
    const auto setError = [error](AiError::Kind kind, const QString &en, const QString &zh,
                                  const QString &detail) {
        if (!error)
            return;
        error->kind = kind;
        error->messageEn = en;
        error->messageZh = zh;
        error->detail = redactSecrets(clampDetail(detail));
    };

    if (body.isEmpty()) {
        setError(AiError::Parse, QStringLiteral("The reply was empty or not valid JSON."),
                 QStringLiteral("响应为空或不是有效的 JSON。"), QString());
        return false;
    }

    if (body.contains(QStringLiteral("error"))) {
        const QJsonValue value = body.value(QStringLiteral("error"));
        QString detail;
        QString message;
        if (value.isObject()) {
            detail = QString::fromUtf8(
                QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
            message = value.toObject().value(QStringLiteral("message")).toString();
        } else {
            detail = value.toString();
            message = detail;
        }
        setError(AiError::BadRequest,
                 message.isEmpty() ? QStringLiteral("The provider rejected the request.")
                                   : QStringLiteral("The provider rejected the request: %1")
                                         .arg(redactSecrets(message)),
                 QStringLiteral("厂商拒绝了该请求：%1").arg(redactSecrets(message)), detail);
        return false;
    }

    if (out) {
        *out = ChatMessage();
        out->role = ChatRole::Assistant;
        out->timestamp = QDateTime::currentDateTimeUtc();
    }

    switch (flavor) {
    case ApiFlavor::Anthropic: {
        const QJsonArray content = body.value(QStringLiteral("content")).toArray();
        QString text;
        QVector<ToolCall> calls;
        for (const QJsonValue &value : content) {
            const QJsonObject block = value.toObject();
            const QString type = block.value(QStringLiteral("type")).toString();
            if (type == QStringLiteral("text")) {
                text += block.value(QStringLiteral("text")).toString();
            } else if (type == QStringLiteral("thinking")) {
                if (out)
                    out->reasoning += block.value(QStringLiteral("thinking")).toString();
            } else if (type == QStringLiteral("tool_use")) {
                ToolCall call;
                call.id = block.value(QStringLiteral("id")).toString();
                call.name = block.value(QStringLiteral("name")).toString();
                call.arguments = block.value(QStringLiteral("input")).toObject();
                if (call.id.isEmpty())
                    call.id = QStringLiteral("tool_%1").arg(calls.size());
                calls.append(call);
            }
        }
        if (out) {
            out->content = text;
            out->toolCalls = calls;
        }
        break;
    }
    case ApiFlavor::GoogleGemini: {
        const QJsonArray candidates = body.value(QStringLiteral("candidates")).toArray();
        if (candidates.isEmpty()) {
            setError(AiError::Parse,
                     QStringLiteral("The model returned no candidate answer."),
                     QStringLiteral("模型未返回任何候选回答。"),
                     QString::fromUtf8(QJsonDocument(body).toJson(QJsonDocument::Compact)));
            return false;
        }
        const QJsonObject candidate = candidates.first().toObject();
        const QJsonArray parts = candidate.value(QStringLiteral("content"))
                                     .toObject()
                                     .value(QStringLiteral("parts"))
                                     .toArray();
        QString text;
        QVector<ToolCall> calls;
        for (const QJsonValue &value : parts) {
            const QJsonObject part = value.toObject();
            if (part.contains(QStringLiteral("text")))
                text += part.value(QStringLiteral("text")).toString();
            if (part.contains(QStringLiteral("functionCall"))) {
                const QJsonObject call = part.value(QStringLiteral("functionCall")).toObject();
                ToolCall entry;
                entry.name = call.value(QStringLiteral("name")).toString();
                entry.arguments = call.value(QStringLiteral("args")).toObject();
                entry.id = QStringLiteral("call_%1").arg(calls.size());
                calls.append(entry);
            }
        }
        if (out) {
            out->content = text;
            out->toolCalls = calls;
        }
        break;
    }
    case ApiFlavor::Ollama: {
        const QJsonObject message = body.value(QStringLiteral("message")).toObject();
        if (out) {
            out->content = message.value(QStringLiteral("content")).toString();
            out->reasoning = message.value(QStringLiteral("thinking")).toString();
            QVector<ToolCall> calls;
            const QJsonArray array = message.value(QStringLiteral("tool_calls")).toArray();
            for (const QJsonValue &value : array) {
                const QJsonObject fn = value.toObject()
                                           .value(QStringLiteral("function"))
                                           .toObject();
                ToolCall call;
                call.id = value.toObject().value(QStringLiteral("id")).toString();
                call.name = fn.value(QStringLiteral("name")).toString();
                QJsonValue args = fn.value(QStringLiteral("arguments"));
                if (args.isString())
                    call.arguments = argumentsFromText(args.toString());
                else
                    call.arguments = args.toObject();
                if (call.id.isEmpty())
                    call.id = QStringLiteral("call_%1").arg(calls.size());
                calls.append(call);
            }
            out->toolCalls = calls;
        }
        break;
    }
    case ApiFlavor::AwsBedrock:
    case ApiFlavor::Custom:
    case ApiFlavor::OpenAiCompatible:
    default: {
        const QJsonArray choices = body.value(QStringLiteral("choices")).toArray();
        if (choices.isEmpty()) {
            setError(AiError::Parse,
                     QStringLiteral("The model returned no choices."),
                     QStringLiteral("模型未返回任何候选结果。"),
                     QString::fromUtf8(QJsonDocument(body).toJson(QJsonDocument::Compact)));
            return false;
        }
        const QJsonObject choice = choices.first().toObject();
        const QJsonObject message = choice.value(QStringLiteral("message")).toObject();
        const QJsonValue content = message.value(QStringLiteral("content"));
        QString text;
        if (content.isString()) {
            text = content.toString();
        } else if (content.isArray()) {
            for (const QJsonValue &value : content.toArray()) {
                const QJsonObject part = value.toObject();
                if (part.value(QStringLiteral("type")).toString() == QStringLiteral("text"))
                    text += part.value(QStringLiteral("text")).toString();
            }
        }
        QVector<ToolCall> calls;
        const QJsonArray array = message.value(QStringLiteral("tool_calls")).toArray();
        for (const QJsonValue &value : array) {
            const QJsonObject entry = value.toObject();
            const QJsonObject fn = entry.value(QStringLiteral("function")).toObject();
            ToolCall call;
            call.id = entry.value(QStringLiteral("id")).toString();
            call.name = fn.value(QStringLiteral("name")).toString();
            const QJsonValue args = fn.value(QStringLiteral("arguments"));
            if (args.isString())
                call.arguments = argumentsFromText(args.toString());
            else
                call.arguments = args.toObject();
            if (call.id.isEmpty())
                call.id = QStringLiteral("call_%1").arg(calls.size());
            calls.append(call);
        }
        if (out) {
            out->content = text;
            out->toolCalls = calls;
            out->reasoning = message.value(QStringLiteral("reasoning_content")).toString();
        }
        break;
    }
    }

    if (error)
        *error = AiError();
    return true;
}

// ---------------------------------------------------------------------------
//  classifyHttpError
//  将 HTTP 失败归类为带友好提示的 AiError
// ---------------------------------------------------------------------------
AiError AiClient::classifyHttpError(int status, const QByteArray &body, const QString &context)
{
    AiError error;
    error.httpStatus = status;

    const QString raw = QString::fromUtf8(body);
    //  Always redact before exposing vendor text anywhere.
    //  在向任何地方暴露厂商文本之前，始终先遮蔽机密。
    const QString detail = redactSecrets(clampDetail(raw));

    //  Surface the vendor's own message when it exists.
    //  当厂商提供了自身消息时予以呈现。
    QString vendorMessage;
    const QJsonObject object = parseObject(raw);
    if (!object.isEmpty()) {
        const QJsonValue value = object.value(QStringLiteral("error"));
        if (value.isObject()) {
            vendorMessage = value.toObject().value(QStringLiteral("message")).toString();
            if (vendorMessage.isEmpty())
                vendorMessage = value.toObject().value(QStringLiteral("code")).toString();
        } else if (value.isString()) {
            vendorMessage = value.toString();
        }
        if (vendorMessage.isEmpty())
            vendorMessage = object.value(QStringLiteral("message")).toString();
    }
    vendorMessage = redactSecrets(vendorMessage.trimmed());

    const QString where = context.trimmed().isEmpty() ? QStringLiteral("this provider")
                                                      : context.trimmed();
    error.detail = detail;

    switch (status) {
    case 401:
    case 403:
        error.kind = AiError::Auth;
        error.messageEn = QStringLiteral("Authentication failed (%1). Check your API key / "
                                         "OAuth token for %2 and try again.")
                              .arg(status)
                              .arg(where);
        error.messageZh = QStringLiteral("身份验证失败（%1）。请检查 %2 的 API Key / "
                                         "OAuth 令牌后重试。")
                              .arg(status)
                              .arg(where);
        break;
    case 429: {
        error.kind = AiError::RateLimit;
        //  Mention Retry-After when the body happens to carry it; the header
        //  itself is not available to this pure helper.
        //  当正文中恰好带有 Retry-After 时予以提示；本纯函数无法访问响应头。
        const QRegularExpression retry(QStringLiteral("retry[-_ ]?after[^0-9]{0,12}([0-9]+)"),
                                       QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch match = retry.match(raw);
        const QString suffixEn = match.hasMatch()
                                     ? QStringLiteral(" Retry after %1 seconds.").arg(match.captured(1))
                                     : QString();
        const QString suffixZh = match.hasMatch()
                                     ? QStringLiteral(" 请在 %1 秒后重试。").arg(match.captured(1))
                                     : QString();
        error.messageEn = QStringLiteral("Rate limit reached for %1. Wait a moment and retry.")
                              .arg(where) + suffixEn;
        error.messageZh = QStringLiteral("%1 已达到速率限制。请稍候重试。").arg(where) + suffixZh;
        break;
    }
    case 400:
    case 404:
    case 422:
        error.kind = AiError::BadRequest;
        error.messageEn = QStringLiteral("The request was rejected (%1) by %2%3")
                              .arg(status)
                              .arg(where);
        error.messageZh = QStringLiteral("请求被 %2 拒绝（%1）%3")
                              .arg(status)
                              .arg(where);
        if (!vendorMessage.isEmpty()) {
            error.messageEn = QStringLiteral("The request was rejected (%1) by %2: %3")
                                  .arg(status)
                                  .arg(where)
                                  .arg(vendorMessage);
            error.messageZh = QStringLiteral("请求被 %2 拒绝（%1）：%3")
                                  .arg(status)
                                  .arg(where)
                                  .arg(vendorMessage);
        } else {
            error.messageEn += QLatin1Char('.');
            error.messageZh += QStringLiteral("。");
        }
        break;
    case 408:
    case 504:
        error.kind = AiError::Timeout;
        error.messageEn = QStringLiteral("%1 did not answer in time (HTTP %2). "
                                         "Try again or shorten the conversation.")
                              .arg(where)
                              .arg(status);
        error.messageZh = QStringLiteral("%1 未及时响应（HTTP %2）。请重试或缩短对话长度。")
                              .arg(where)
                              .arg(status);
        break;
    default:
        if (status >= 500 && status <= 599) {
            error.kind = AiError::Server;
            error.messageEn = QStringLiteral("%1 reported a server error (HTTP %2). "
                                             "This is usually temporary.")
                                  .arg(where)
                                  .arg(status);
            error.messageZh = QStringLiteral("%1 返回服务器错误（HTTP %2）。通常为暂时性问题。")
                                  .arg(where)
                                  .arg(status);
        } else {
            error.kind = AiError::Unknown;
            error.messageEn = QStringLiteral("Unexpected response (HTTP %1) from %2.")
                                  .arg(status)
                                  .arg(where);
            error.messageZh = QStringLiteral("%2 返回了意外响应（HTTP %1）。")
                                  .arg(status)
                                  .arg(where);
        }
        if (!vendorMessage.isEmpty()) {
            error.messageEn += QLatin1Char(' ') + vendorMessage;
            error.messageZh += QLatin1Char(' ') + vendorMessage;
        }
        break;
    }
    return error;
}

// ---------------------------------------------------------------------------
//  send
//  发送请求
// ---------------------------------------------------------------------------
QString AiClient::send(const QVector<ChatMessage> &messages, const QVector<ToolDescriptor> &tools,
                       const QString &requestId)
{
    AiError error;
    const QString id = requestId.isEmpty() ? IdGenerator::randomToken(8) : requestId;

    QString context = m_vendor.labelEn;
    if (context.isEmpty())
        context = m_vendor.id;
    if (context.isEmpty())
        context = QStringLiteral("the configured provider");

    const auto failNow = [this, &id](const AiError &failure) {
        emit failed(id, failure);
        return QString();
    };

    if (m_vendor.id.isEmpty()) {
        error.kind = AiError::BadRequest;
        error.messageEn = QStringLiteral("No AI provider is selected. Choose one in Settings.");
        error.messageZh = QStringLiteral("尚未选择 AI 厂商。请在设置中选择一个。");
        return failNow(error);
    }
    if (m_config.model.trimmed().isEmpty()) {
        error.kind = AiError::BadRequest;
        error.messageEn = QStringLiteral("No model is selected for %1.").arg(context);
        error.messageZh = QStringLiteral("尚未为 %1 选择模型。").arg(context);
        return failNow(error);
    }
    if (!m_network) {
        error.kind = AiError::Network;
        error.messageEn = QStringLiteral("The network layer is not available.");
        error.messageZh = QStringLiteral("网络层不可用。");
        return failNow(error);
    }
    if (isAzureVendor(m_vendor) && m_config.azureDeployment.trimmed().isEmpty()) {
        error.kind = AiError::BadRequest;
        error.messageEn = QStringLiteral("Azure OpenAI needs a deployment name "
                                         "(ProviderConfig::azureDeployment).");
        error.messageZh = QStringLiteral("Azure OpenAI 需要部署名称"
                                         "（ProviderConfig::azureDeployment）。");
        return failNow(error);
    }

    const QUrl url = chatUrl(m_vendor, m_config);
    if (!url.isValid() || url.host().isEmpty()) {
        error.kind = AiError::BadRequest;
        error.messageEn = QStringLiteral("The API address for %1 is not a valid URL.").arg(context);
        error.messageZh = QStringLiteral("%1 的接口地址不是有效的 URL。").arg(context);
        error.detail = redactSecrets(url.toString());
        return failNow(error);
    }

    const bool stream = m_config.streaming;
    const QJsonObject body = buildRequestBody(m_vendor.flavor, messages, tools, m_config,
                                              m_config.model, stream);
    const QByteArray payload =
        QJsonDocument(body).toJson(QJsonDocument::Compact);

    QNetworkRequest request(url);
    const QMap<QString, QString> headers = baseHeaders(m_vendor.flavor);
    for (auto it = headers.constBegin(); it != headers.constEnd(); ++it)
        request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());

    //  Accept must reflect the actual transport: an SSE request is useless
    //  when the caller asked for a single JSON body.
    //  Accept 必须与实际传输一致：当调用方要求单次 JSON 响应时，SSE 头没有意义。
    request.setRawHeader("Accept",
                         stream ? "text/event-stream" : "application/json");

    if (m_auth) {
        QString authError;
        const QMap<QString, QString> auth = m_auth->authHeaders(m_vendor, m_config, &authError);
        for (auto it = auth.constBegin(); it != auth.constEnd(); ++it)
            request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
        if (!authError.isEmpty()) {
            error.kind = AiError::Auth;
            error.messageEn = QStringLiteral("Could not prepare credentials for %1: %2")
                                  .arg(context)
                                  .arg(redactSecrets(authError));
            error.messageZh = QStringLiteral("无法为 %1 准备凭据：%2")
                                  .arg(context)
                                  .arg(redactSecrets(authError));
            return failNow(error);
        }
    }

    auto *pending = new Pending;
    pending->requestId = id;
    pending->flavor = m_vendor.flavor;
    pending->streaming = stream;

    pending->reply = m_network->post(request, payload);
    if (!pending->reply) {
        delete pending;
        error.kind = AiError::Network;
        error.messageEn = QStringLiteral("Could not start the request to %1.").arg(context);
        error.messageZh = QStringLiteral("无法向 %1 发起请求。").arg(context);
        return failNow(error);
    }

    m_replies.insert(id, pending);

    //  Every request is bounded by the configured timeout.
    //  每个请求都受配置的超时限制。
    pending->timer = new QTimer(this);
    pending->timer->setSingleShot(true);
    pending->timer->setInterval(m_config.requestTimeoutMs > 0 ? m_config.requestTimeoutMs : 120000);
    connect(pending->timer, &QTimer::timeout, this, &AiClient::onTimeout);
    connect(pending->reply, &QNetworkReply::readyRead, this, &AiClient::onReadyRead);
    connect(pending->reply, &QNetworkReply::finished, this, &AiClient::onFinished);
    pending->timer->start();

    emit streamStarted(id);
    return id;
}

// ---------------------------------------------------------------------------
//  cancel / cancelAll
//  取消请求
// ---------------------------------------------------------------------------
void AiClient::cancel(const QString &requestId)
{
    Pending *pending = m_replies.value(requestId, nullptr);
    if (!pending)
        return;

    //  Mark done first so the finished handler stays silent.
    //  先标记为已完成，使 finished 处理函数保持静默。
    pending->done = true;
    m_replies.remove(requestId);
    if (pending->timer) {
        pending->timer->stop();
        pending->timer->deleteLater();
    }
    if (pending->reply) {
        pending->reply->disconnect(this);
        pending->reply->abort();
        pending->reply->deleteLater();
    }
    pending->reply = nullptr;
    delete pending;
}

void AiClient::cancelAll()
{
    const QList<QString> ids = m_replies.keys();
    for (const QString &id : ids)
        cancel(id);
}

// ---------------------------------------------------------------------------
//  pending lookup
//  查找待处理请求
// ---------------------------------------------------------------------------
AiClient::Pending *AiClient::pending(const QString &requestId) const
{
    return m_replies.value(requestId, nullptr);
}

// ---------------------------------------------------------------------------
//  onReadyRead -- incremental SSE parsing, buffering partial lines
//  onReadyRead —— 增量解析 SSE，并缓冲未完成的行
// ---------------------------------------------------------------------------
void AiClient::onReadyRead()
{
    auto *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply)
        return;

    Pending *pending = nullptr;
    for (auto it = m_replies.constBegin(); it != m_replies.constEnd(); ++it) {
        if (it.value() && it.value()->reply == reply) {
            pending = it.value();
            break;
        }
    }
    if (!pending || pending->done)
        return;

    pending->buffer.append(reply->readAll());

    if (!pending->streaming) {
        //  A single JSON body is parsed once the reply finishes.
        //  单个 JSON 响应在请求结束时统一解析。
        return;
    }

    //  A chunk can split a `data:` line, so keep the trailing partial line.
    //  一个数据块可能切断 `data:` 行，因此保留末尾未完成的行。
    int newline = pending->buffer.lastIndexOf('\n');
    if (newline < 0)
        return;

    const QByteArray complete = pending->buffer.left(newline + 1);
    pending->buffer.remove(0, newline + 1);

    const QStringList lines = payloadLines(complete);
    for (const QString &rawLine : lines) {
        const QString data = stripSseField(rawLine, QStringLiteral("data:"));
        if (data.isEmpty())
            continue;
        if (data == QStringLiteral("[DONE]")) {
            pending->buffer.clear();
            return;
        }
        applyStreamEvent(this, pending, parseStreamChunk(pending->flavor, data.toUtf8()));
        if (pending->done)
            return;
    }
}

// ---------------------------------------------------------------------------
//  onFinished
//  onFinished —— 请求结束（成功或失败）
// ---------------------------------------------------------------------------
void AiClient::onFinished()
{
    auto *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply)
        return;

    Pending *pending = nullptr;
    for (auto it = m_replies.constBegin(); it != m_replies.constEnd(); ++it) {
        if (it.value() && it.value()->reply == reply) {
            pending = it.value();
            break;
        }
    }
    //  The request was cancelled (or already completed): stay silent.
    //  请求已被取消（或已完成）：保持静默。
    if (!pending || pending->done) {
        reply->deleteLater();
        return;
    }

    pending->done = true;
    const QString id = pending->requestId;
    const ApiFlavor flavor = pending->flavor;
    const bool streaming = pending->streaming;

    if (pending->timer) {
        pending->timer->stop();
        pending->timer->deleteLater();
        pending->timer = nullptr;
    }
    m_replies.remove(id);

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray tail = reply->readAll();
    const QNetworkReply::NetworkError networkError = reply->error();
    const QString networkText = reply->errorString();
    reply->deleteLater();

    const auto finish = [this, pending]() {
        delete pending;
    };

    //  Transport failures: no HTTP status was received at all.
    //  传输层失败：完全没有收到 HTTP 状态码。
    if (networkError != QNetworkReply::NoError && status <= 0) {
        AiError error;
        error.kind = networkError == QNetworkReply::TimeoutError ? AiError::Timeout
                                                                  : AiError::Network;
        const QString detail = redactSecrets(networkText);
        if (error.kind == AiError::Timeout) {
            error.messageEn = QStringLiteral("The request timed out before a reply arrived.");
            error.messageZh = QStringLiteral("请求超时，未收到任何响应。");
        } else {
            error.messageEn = QStringLiteral("The network request failed: %1").arg(detail);
            error.messageZh = QStringLiteral("网络请求失败：%1").arg(detail);
        }
        error.detail = detail;
        finish();
        emit failed(id, error);
        return;
    }

    if (status >= 400) {
        AiError error = classifyHttpError(status, tail, m_vendor.labelEn.isEmpty()
                                                              ? m_vendor.id
                                                              : m_vendor.labelEn);
        finish();
        emit failed(id, error);
        return;
    }

    if (!streaming) {
        QJsonParseError parseError{};
        const QJsonDocument doc = QJsonDocument::fromJson(tail, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            AiError error;
            error.kind = AiError::Parse;
            error.httpStatus = status;
            error.messageEn = QStringLiteral("The reply was not valid JSON.");
            error.messageZh = QStringLiteral("响应不是有效的 JSON。");
            error.detail = redactSecrets(clampDetail(QString::fromUtf8(tail)));
            finish();
            emit failed(id, error);
            return;
        }
        ChatMessage message;
        AiError error;
        if (!parseResponse(flavor, doc.object(), &message, &error)) {
            finish();
            emit failed(id, error);
            return;
        }
        if (!message.content.isEmpty())
            emit textDelta(id, message.content);
        if (!message.reasoning.isEmpty())
            emit reasoningDelta(id, message.reasoning);
        if (!message.toolCalls.isEmpty())
            emit toolCallReady(id, message.toolCalls);
        finish();
        emit completed(id, message);
        return;
    }

    //  Streaming: flush whatever is left in the buffer, then complete.
    //  流式：先冲掉缓冲区中剩余内容，然后结束。
    if (!pending->buffer.isEmpty()) {
        const QByteArray rest = pending->buffer;
        pending->buffer.clear();
        const QStringList lines = payloadLines(rest);
        for (const QString &rawLine : lines) {
            const QString data = stripSseField(rawLine, QStringLiteral("data:"));
            if (data.isEmpty() || data == QStringLiteral("[DONE]"))
                continue;
            applyStreamEvent(this, pending, parseStreamChunk(flavor, data.toUtf8()));
        }
    }

    ChatMessage message;
    message.role = ChatRole::Assistant;
    message.content = pending->text;
    message.reasoning = pending->reasoning;
    message.toolCalls = pending->toolCalls;
    message.timestamp = QDateTime::currentDateTimeUtc();

    if (!pending->text.isEmpty() || !pending->toolCalls.isEmpty())
        emit completed(id, message);
    else if (networkError != QNetworkReply::NoError) {
        AiError error;
        error.kind = AiError::Network;
        error.httpStatus = status;
        error.messageEn = QStringLiteral("The stream ended unexpectedly: %1")
                              .arg(redactSecrets(networkText));
        error.messageZh = QStringLiteral("流式传输意外中断：%1").arg(redactSecrets(networkText));
        error.detail = redactSecrets(networkText);
        finish();
        emit failed(id, error);
        return;
    } else {
        emit completed(id, message);
    }
    finish();
}

// ---------------------------------------------------------------------------
//  onTimeout
//  onTimeout —— 请求超时
// ---------------------------------------------------------------------------
void AiClient::onTimeout()
{
    auto *timer = qobject_cast<QTimer *>(sender());
    if (!timer)
        return;

    QString id;
    Pending *pending = nullptr;
    for (auto it = m_replies.constBegin(); it != m_replies.constEnd(); ++it) {
        if (it.value() && it.value()->timer == timer) {
            pending = it.value();
            id = it.key();
            break;
        }
    }
    if (!pending || pending->done)
        return;

    pending->done = true;
    m_replies.remove(id);
    if (pending->reply) {
        pending->reply->disconnect(this);
        pending->reply->abort();
        pending->reply->deleteLater();
        pending->reply = nullptr;
    }
    timer->deleteLater();
    pending->timer = nullptr;
    delete pending;

    AiError error;
    error.kind = AiError::Timeout;
    error.messageEn = QStringLiteral("The request exceeded the configured timeout (%1 ms).")
                          .arg(m_config.requestTimeoutMs);
    error.messageZh = QStringLiteral("请求超过了配置的超时时间（%1 毫秒）。")
                          .arg(m_config.requestTimeoutMs);
    emit failed(id, error);
}

} // namespace tcbw
