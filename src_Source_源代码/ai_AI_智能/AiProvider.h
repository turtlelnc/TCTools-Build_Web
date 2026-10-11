// ============================================================================
//  AiProvider.h -- HTTP client that speaks every supported wire protocol
//  AiProvider.h —— 支持全部已接入协议的 HTTP 客户端
// ============================================================================
#pragma once

#include "AiTypes.h"
#include "AuthManager.h"
#include "ProviderRegistry.h"
#include "Types.h"

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>

#include <memory>

QT_BEGIN_NAMESPACE
class QNetworkAccessManager;
class QNetworkReply;
QT_END_NAMESPACE

namespace tcbw {

// ---------------------------------------------------------------------------
//  AiClient
//  One in-flight chat request at a time per instance is the supported usage,
//  but requests are tracked by id so several instances may share a network
//  manager safely.
//  每个实例支持同时进行一个对话请求，但请求按 id 跟踪，因此多个实例可以安全地
//  共享同一个网络管理器。
// ---------------------------------------------------------------------------
class AiClient : public QObject
{
    Q_OBJECT

public:
    explicit AiClient(QNetworkAccessManager *network, AuthManager *auth,
                      QObject *parent = nullptr);
    ~AiClient() override;

    void setVendor(const VendorInfo &vendor);
    const VendorInfo &vendor() const { return m_vendor; }

    void setConfig(const ProviderConfig &config);
    const ProviderConfig &config() const { return m_config; }

    //  Sends a chat request. Returns the request id, or an empty string when
    //  the configuration is obviously invalid (the error signal fires
    //  immediately in that case).
    //  发送对话请求。返回请求 id；当配置明显无效时返回空字符串
    //  （此时会立即发出错误信号）。
    QString send(const QVector<ChatMessage> &messages,
                 const QVector<ToolDescriptor> &tools = {},
                 const QString &requestId = QString());

    void cancel(const QString &requestId);
    void cancelAll();
    bool isBusy() const { return !m_replies.isEmpty(); }

    // --- pure helpers (unit-testable without any network) ------------------
    // --- 纯函数辅助（无需网络即可单元测试） --------------------------------

    //  Builds the fully-serialised request for a flavour.
    //  为某种协议构建完整序列化的请求。
    static QJsonObject buildRequestBody(ApiFlavor flavor,
                                        const QVector<ChatMessage> &messages,
                                        const QVector<ToolDescriptor> &tools,
                                        const ProviderConfig &config,
                                        const QString &model,
                                        bool stream);

    //  Full URL for a chat request.
    //  对话请求的完整 URL。
    static QUrl chatUrl(const VendorInfo &vendor, const ProviderConfig &config);

    //  Header set for a request (auth headers are supplied by AuthManager).
    //  请求头集合（授权头由 AuthManager 提供）。
    static QMap<QString, QString> baseHeaders(ApiFlavor flavor);

    //  Parses one Server-Sent-Events payload chunk into a normalised event.
    //  将单个 Server-Sent-Events 数据块解析为规范化事件。
    struct StreamEvent
    {
        enum Type { Ignore = 0, Text, Reasoning, ToolCallStart, ToolCallDelta,
                    ToolCallDone, Usage, Done, Error } type = Ignore;
        QString text;
        QString toolCallId;
        QString toolName;
        QString argumentsDelta;
        int inputTokens = 0;
        int outputTokens = 0;
        QString errorMessage;
    };
    static StreamEvent parseStreamChunk(ApiFlavor flavor, const QByteArray &ssePayload);

    //  Parses a complete (non-streaming) response body into a ChatMessage.
    //  将完整的（非流式）响应体解析为 ChatMessage。
    static bool parseResponse(ApiFlavor flavor, const QJsonObject &body,
                              ChatMessage *out, AiError *error);

    //  Normalises an HTTP failure into an AiError with a friendly message.
    //  将 HTTP 失败规范化为带友好提示的 AiError。
    static AiError classifyHttpError(int status, const QByteArray &body,
                                     const QString &context);

signals:
    void streamStarted(const QString &requestId);
    void textDelta(const QString &requestId, const QString &delta);
    void reasoningDelta(const QString &requestId, const QString &delta);
    void toolCallReady(const QString &requestId, const QVector<ToolCall> &calls);
    void usageReported(const QString &requestId, int inputTokens, int outputTokens);
    void completed(const QString &requestId, const ChatMessage &message);
    void failed(const QString &requestId, const AiError &error);

private slots:
    void onReadyRead();
    void onFinished();
    void onTimeout();

private:
    struct Pending;
    Pending *pending(const QString &requestId) const;

    QNetworkAccessManager *m_network = nullptr;
    AuthManager *m_auth = nullptr;
    VendorInfo m_vendor;
    ProviderConfig m_config;
    QHash<QString, Pending *> m_replies;
};

} // namespace tcbw
