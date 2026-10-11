// ============================================================================
//  McpClient.h -- Model Context Protocol client (stdio + HTTP)
//  McpClient.h —— Model Context Protocol 客户端（标准输入输出 + HTTP）
//
//  Implements the 2024-11-05 / 2025-03-26 revision of the protocol:
//  JSON-RPC 2.0 framing, `initialize`, `tools/list`, `tools/call`,
//  `resources/list`, `resources/read` and server notifications.
//  实现 2024-11-05 / 2025-03-26 版本协议：JSON-RPC 2.0 帧格式、
//  `initialize`、`tools/list`、`tools/call`、`resources/list`、
//  `resources/read` 以及服务端通知。
//
//  SAFETY / 安全说明
//  Starting an MCP server executes a local program. This client therefore:
//    * refuses to start a server whose config is not marked `trusted`;
//    * never passes secrets through the command line (environment only);
//    * kills the child process tree when the client is destroyed.
//  启动 MCP 服务器会执行本地程序。因此本客户端：
//    * 拒绝启动未标记为 `trusted` 的服务器配置；
//    * 绝不通过命令行传递机密（只使用环境变量）；
//    * 在客户端销毁时终止整个子进程树。
// ============================================================================
#pragma once

#include "AiTypes.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>

#include <memory>

QT_BEGIN_NAMESPACE
class QNetworkAccessManager;
class QProcess;
class QTimer;
QT_END_NAMESPACE

namespace tcbw {

// ---------------------------------------------------------------------------
//  McpClient
//  MCP 客户端
// ---------------------------------------------------------------------------
class McpClient : public QObject
{
    Q_OBJECT

public:
    explicit McpClient(const McpServerConfig &config, QObject *parent = nullptr);
    ~McpClient() override;

    // --- lifecycle ---------------------------------------------------------
    //  Starts the transport. For stdio this spawns the configured program.
    //  启动传输层。对 stdio 而言会派生已配置的程序。
    bool start(QString *error = nullptr);
    void stop();
    bool isRunning() const;
    bool isInitialised() const { return m_initialised; }

    const McpServerConfig &config() const { return m_config; }
    QString serverName() const { return m_serverName; }
    QString serverVersion() const { return m_serverVersion; }
    QJsonObject serverCapabilities() const { return m_capabilities; }
    QString negotiatedProtocolVersion() const { return m_protocolVersion; }

    // --- requests ----------------------------------------------------------
    void initialize();
    void listTools();
    void listResources();
    void listPrompts();
    void callTool(const QString &name, const QJsonObject &arguments,
                  const QString &requestId = QString());
    void readResource(const QString &uri, const QString &requestId = QString());

    //  Tools discovered so far, already converted to the app's descriptor type.
    //  已发现的工具，已转换为应用的描述符类型。
    QVector<ToolDescriptor> tools() const { return m_tools; }
    QJsonArray rawResources() const { return m_resources; }

    //  Applies the allow/deny lists from the config to a tool name.
    //  依据配置中的允许/拒绝列表判断工具名是否可用。
    bool isToolAllowed(const QString &toolName) const;

    // --- diagnostics -------------------------------------------------------
    QString lastError() const { return m_lastError; }
    QJsonObject lastServerInfo() const { return m_serverInfo; }

    //  Sanitises a tool name into the `mcp_<server>_<tool>` namespace used by
    //  the agent so that MCP tools can never shadow a built-in tool.
    //  将工具名净化为 `mcp_<server>_<tool>` 命名空间，使 MCP 工具永远无法
    //  覆盖内置工具。
    static QString namespacedToolName(const QString &serverId, const QString &toolName);

signals:
    void started();
    void stopped();
    void initialised(const QJsonObject &serverInfo);
    void toolsListed(const QVector<ToolDescriptor> &tools);
    void resourcesListed(const QJsonArray &resources);
    void promptsListed(const QJsonArray &prompts);
    void resourceRead(const QString &uri, const QString &text);
    void toolResult(const QString &requestId, const ToolResult &result);
    void notification(const QString &method, const QJsonObject &params);
    void errorOccurred(const QString &message);

private slots:
    void onStdoutReady();
    void onStderrReady();
    void onProcessError();
    void onProcessFinished(int exitCode);
    void onHttpFinished();
    void onRequestTimeout();

private:
    struct PendingRequest;
    void sendRequest(const QString &method, const QJsonObject &params,
                     const QString &requestId);
    void sendNotification(const QString &method, const QJsonObject &params);
    void handleMessage(const QJsonObject &message);
    void handleResult(const QJsonValue &id, const QJsonObject &result);
    void handleError(const QJsonValue &id, const QJsonObject &error);
    void fail(const QString &message);
    void startStdio(QString *error);
    void startHttp(QString *error);
    void sendHttpPayload(const QByteArray &payload);
    void parseToolList(const QJsonArray &tools);

    McpServerConfig m_config;
    std::unique_ptr<QProcess> m_process;
    std::unique_ptr<QNetworkAccessManager> m_network;
    std::unique_ptr<QTimer> m_timeoutTimer;
    QByteArray m_readBuffer;
    QHash<QString, PendingRequest *> m_pending;
    QHash<QString, QString> m_requestIds;   // jsonrpc id -> caller request id
    int m_nextId = 1;
    QString m_serverName;
    QString m_serverVersion;
    QString m_protocolVersion = QStringLiteral("2025-03-26");
    QJsonObject m_capabilities;
    QJsonObject m_serverInfo;
    QVector<ToolDescriptor> m_tools;
    QJsonArray m_resources;
    QString m_lastError;
    QString m_sessionId;                    // streamable HTTP session / 流式 HTTP 会话
    bool m_initialised = false;
    bool m_stopping = false;
};

} // namespace tcbw
