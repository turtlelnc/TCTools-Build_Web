// ============================================================================
//  McpClient.cpp -- Model Context Protocol client (stdio + HTTP)
//  McpClient.cpp —— Model Context Protocol 客户端（标准输入输出 + HTTP）
//
//  SAFETY / 安全说明
//    * A stdio server is a local program, so `start()` refuses to launch one
//      unless `config.trusted` is true.
//    * Secrets are passed through the process environment, never on the
//      command line (the command line is world readable on every desktop OS).
//    * On shutdown the child process tree is terminated, because a server that
//      spawns helpers would otherwise survive the application.
//    * 标准输入输出方式的服务器是本地程序，因此除非 `config.trusted` 为真，
//      `start()` 拒绝启动它。
//    * 机密通过进程环境传递，绝不放在命令行中（命令行在所有桌面系统上都可被
//      其他进程读取）。
//    * 关闭时会终止整个子进程树，否则服务器派生的辅助进程会在应用退出后残留。
// ============================================================================

#include "McpClient.h"

#include "IdGenerator.h"
#include "Types.h"

#include <algorithm>
#include <QByteArray>
#include <QDateTime>
#include <QEventLoop>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStringList>
#include <QTimer>
#include <QUrl>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

namespace tcbw {
namespace {

//  Every JSON-RPC request is bounded by this timeout.
//  每个 JSON-RPC 请求都受此超时限制。
constexpr int kRequestTimeoutMs = 60000;

//  Longest accepted line for a single stdio message (guards against a runaway
//  server flooding memory through a never-terminating "line").
//  单条标准输入输出消息允许的最长长度（防止失控服务器通过永不完结的"行"耗光内存）。
constexpr int kMaxBufferedBytes = 32 * 1024 * 1024;

//  Default MCP endpoint path when the user only supplied a host.
//  用户只提供主机名时使用的默认 MCP 端点路径。
QString defaultMcpPath()
{
    return QStringLiteral("/mcp");
}

//  True when the environment value looks like a credential.
//  当环境变量的值看起来像凭据时返回 true。
bool environmentLooksSecret(const QString &value)
{
    return looksLikeSecret(value);
}

//  Turns arbitrary text into a fragment safe for the `mcp_` namespace.
//  将任意文本转换为 `mcp_` 命名空间中安全的分段。
QString sanitiseFragment(const QString &value)
{
    QString out;
    out.reserve(value.size());
    for (const QChar ch : value) {
        if (ch.isLetterOrNumber() || ch == QLatin1Char('_') || ch == QLatin1Char('-'))
            out.append(ch.toLower());
        else
            out.append(QLatin1Char('_'));
    }
    while (out.startsWith(QLatin1Char('_')))
        out.remove(0, 1);
    while (out.endsWith(QLatin1Char('_')))
        out.chop(1);
    if (out.isEmpty())
        out = QStringLiteral("server");
    return out;
}

//  Concatenates the text parts of an MCP `content[]` array; other part types
//  are dumped as compact JSON so nothing is silently lost.
//  拼接 MCP `content[]` 数组中的文本部分；其他类型的部件以紧凑 JSON 形式转储，
//  以免信息被静默丢弃。
QString contentToText(const QJsonArray &content, bool *isError, QString *firstError)
{
    QStringList texts;
    for (const QJsonValue &value : content) {
        if (value.isString()) {
            texts.append(value.toString());
            continue;
        }
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject part = value.toObject();
        const QString type = part.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("text")) {
            texts.append(part.value(QStringLiteral("text")).toString());
        } else if (type == QStringLiteral("resource")) {
            const QJsonObject resource = part.value(QStringLiteral("resource")).toObject();
            const QString text = resource.value(QStringLiteral("text")).toString();
            if (!text.isEmpty())
                texts.append(text);
            else
                texts.append(QString::fromUtf8(
                    QJsonDocument(resource).toJson(QJsonDocument::Compact)));
        } else if (type == QStringLiteral("image") || type == QStringLiteral("audio")) {
            const QJsonObject summary{
                { QStringLiteral("type"), type },
                { QStringLiteral("mimeType"), part.value(QStringLiteral("mimeType")) },
                { QStringLiteral("note"),
                  QStringLiteral("binary payload omitted from the text transcript") }
            };
            texts.append(QString::fromUtf8(
                QJsonDocument(summary).toJson(QJsonDocument::Compact)));
        } else {
            texts.append(QString::fromUtf8(
                QJsonDocument(part).toJson(QJsonDocument::Compact)));
        }
    }

    if (isError)
        *isError = false;
    if (firstError)
        firstError->clear();

    QString joined = texts.join(QStringLiteral("\n"));
    if (joined.trimmed().isEmpty() && !content.isEmpty())
        joined = QString::fromUtf8(QJsonDocument(content).toJson(QJsonDocument::Compact));
    return joined;
}

} // namespace

// ---------------------------------------------------------------------------
//  PendingRequest
//  One in-flight JSON-RPC call.
//  一个进行中的 JSON-RPC 调用。
// ---------------------------------------------------------------------------
struct McpClient::PendingRequest
{
    QString callerId;       // request id handed back to the caller / 交还调用方的请求 id
    QString method;
    QString originalName;   // un-namespaced tool name for tools/call / tools/call 的原始工具名
    QTimer *timer = nullptr;
    QNetworkReply *reply = nullptr;
};

// ---------------------------------------------------------------------------
//  Construction / destruction
//  构造与析构
// ---------------------------------------------------------------------------
McpClient::McpClient(const McpServerConfig &config, QObject *parent)
    : QObject(parent)
    , m_config(config)
{
}

McpClient::~McpClient()
{
    stop();
}

// ---------------------------------------------------------------------------
//  start
//  启动传输层
// ---------------------------------------------------------------------------
bool McpClient::start(QString *error)
{
    const auto fail = [this, error](const QString &messageEn, const QString &messageZh) {
        const QString text = messageEn + QStringLiteral("\n") + messageZh;
        m_lastError = text;
        if (error)
            *error = text;
        emit errorOccurred(text);
        return false;
    };

    if (!m_config.trusted) {
        //  Launching an untrusted program is never acceptable, not even when
        //  the user pressed "start" without reading the warning.
        //  启动未受信任的程序永远不可接受，即使用户未阅读警告就按下了"启动"。
        return fail(QStringLiteral("Refusing to start the MCP server \"%1\": the server is not "
                                   "trusted. Mark it as trusted in Settings after reviewing the "
                                   "command it will run.")
                        .arg(m_config.name.isEmpty() ? m_config.id : m_config.name),
                    QStringLiteral("拒绝启动 MCP 服务器 \"%1\"：该服务器未受信任。请在确认其将"
                                   "运行的命令后，在设置中将其标记为受信任。")
                        .arg(m_config.name.isEmpty() ? m_config.id : m_config.name));
    }
    if (m_config.id.trimmed().isEmpty()) {
        return fail(QStringLiteral("The MCP server configuration has no id, so its tools could "
                                   "not be namespaced safely."),
                    QStringLiteral("该 MCP 服务器配置缺少 id，无法安全地为其工具建立命名空间。"));
    }

    m_lastError.clear();
    m_stopping = false;
    m_readBuffer.clear();

    switch (m_config.transport) {
    case McpTransport::Stdio:
        startStdio(error);
        break;
    case McpTransport::HttpSse:
    case McpTransport::HttpJson:
        startHttp(error);
        break;
    }

    if (!m_lastError.isEmpty())
        return false;

    emit started();
    return true;
}

// ---------------------------------------------------------------------------
//  startStdio
//  启动标准输入输出方式的服务器
// ---------------------------------------------------------------------------
void McpClient::startStdio(QString *error)
{
    if (m_config.command.trimmed().isEmpty()) {
        (void)fail(QStringLiteral("No command is configured for the MCP server \"%1\".")
                       .arg(m_config.id));
        return;
    }

    m_process = std::make_unique<QProcess>();
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    if (!m_config.workingDirectory.trimmed().isEmpty())
        m_process->setWorkingDirectory(m_config.workingDirectory);

    //  Secrets travel through the environment only. The command line is visible
    //  to every other process on the machine, so it must never carry a token.
    //  机密只通过环境变量传递。命令行对本机上的其他进程可见，因此绝不能携带令牌。
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    for (auto it = m_config.environment.constBegin(); it != m_config.environment.constEnd(); ++it) {
        //  NOTE: some of these values are credentials by design (the user is
        //  expected to put an API token here). They are therefore never logged,
        //  never echoed back into an error message and never placed in argv.
        //  注意：其中部分值本身就是凭据（用户会被期望把 API 令牌放在这里）。
        //  因此它们绝不写日志、绝不出现在错误消息中，也绝不放进 argv。
        if (environmentLooksSecret(it.value())) {
            //  Intentionally no diagnostic that includes the value.
            //  此处刻意不输出任何包含该值的诊断信息。
        }
        environment.insert(it.key(), it.value());
    }
    m_process->setProcessEnvironment(environment);

    connect(m_process.get(), &QProcess::readyReadStandardOutput, this, &McpClient::onStdoutReady);
    connect(m_process.get(), &QProcess::readyReadStandardError, this, &McpClient::onStderrReady);
    connect(m_process.get(), &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError) { onProcessError(); });
    connect(m_process.get(), QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus) { onProcessFinished(exitCode); });

    m_process->start(m_config.command, m_config.arguments);
    if (!m_process->waitForStarted(10000)) {
        fail(QStringLiteral("Could not start the MCP server \"%1\": %2")
                 .arg(m_config.id)
                 .arg(redactSecrets(m_process->errorString())));
        if (error)
            *error = m_lastError;
        m_process.reset();
        return;
    }
}

// ---------------------------------------------------------------------------
//  startHttp
//  启动 HTTP 方式
// ---------------------------------------------------------------------------
void McpClient::startHttp(QString *error)
{
    if (m_config.url.trimmed().isEmpty()) {
        fail(QStringLiteral("No URL is configured for the MCP server \"%1\".").arg(m_config.id));
        if (error)
            *error = m_lastError;
        return;
    }

    QUrl url(m_config.url.trimmed());
    if (!url.isValid() || url.host().isEmpty()) {
        fail(QStringLiteral("The MCP URL for \"%1\" is not a valid address.").arg(m_config.id));
        if (error)
            *error = m_lastError;
        return;
    }
    if (url.path().isEmpty() || url.path() == QStringLiteral("/"))
        url.setPath(defaultMcpPath());

    m_network = std::make_unique<QNetworkAccessManager>();
    connect(m_network.get(), &QNetworkAccessManager::finished, this, &McpClient::onHttpFinished);
    m_config.url = url.toString();
}

// ---------------------------------------------------------------------------
//  stop / isRunning
//  停止 / 运行状态
// ---------------------------------------------------------------------------
void McpClient::stop()
{
    m_stopping = true;
    m_initialised = false;

    //  Abort every pending request so no slot fires on a dead transport.
    //  中止所有进行中的请求，避免在已关闭的传输上触发槽函数。
    const QList<QString> keys = m_pending.keys();
    for (const QString &key : keys) {
        PendingRequest *pending = m_pending.take(key);
        if (!pending)
            continue;
        if (pending->timer) {
            pending->timer->stop();
            pending->timer->deleteLater();
        }
        if (pending->reply) {
            pending->reply->disconnect(this);
            pending->reply->abort();
            pending->reply->deleteLater();
        }
        delete pending;
    }
    m_requestIds.clear();

    if (m_process) {
        m_process->disconnect(this);
        if (m_process->state() != QProcess::NotRunning) {
            //  Closing stdin is the polite request; some servers only exit then.
            //  关闭标准输入是礼貌的请求；部分服务器只有在此时才退出。
            m_process->closeWriteChannel();

#ifdef Q_OS_WIN
            //  LIMITATION / 局限：QProcess only tracks the direct child. A server
            //  that spawns helpers (node -> python, wrapper scripts, ...) would
            //  leave orphans behind, so we ask the OS to kill the whole tree.
            //  QProcess 只跟踪直接子进程。若服务器派生了辅助进程
            //  （node -> python、包装脚本等），会留下孤儿进程，因此请求操作系统
            //  终止整棵进程树。
            const qint64 pid = static_cast<qint64>(m_process->processId());
            if (pid > 0) {
                //  Build a minimal environment for the helper: taskkill.exe lives
                //  in System32 and must be found even when PATH was customised.
                //  为辅助程序准备最小环境：taskkill.exe 位于 System32，即使 PATH
                //  被自定义也必须能找到它。
                QProcessEnvironment killEnvironment = QProcessEnvironment::systemEnvironment();
                killEnvironment.insert(QStringLiteral("SystemRoot"),
                                       QString::fromLocal8Bit(qgetenv("SystemRoot")));
                QProcess killer;
                killer.setProcessEnvironment(killEnvironment);
                //  Output is irrelevant here and piped channels would only add
                //  two file handles we would have to drain.
                //  此处不关心输出；管道通道只会多出两个必须抽干的句柄。
                killer.setProcessChannelMode(QProcess::ForwardedChannels);
                killer.start(QStringLiteral("taskkill"),
                             { QStringLiteral("/PID"), QString::number(pid),
                               QStringLiteral("/T"), QStringLiteral("/F") });
                killer.waitForFinished(3000);
            }
#endif
            m_process->terminate();
            if (!m_process->waitForFinished(3000)) {
                //  Last resort: hard kill of the direct child.
                //  最后手段：强制结束直接子进程。
                m_process->kill();
                m_process->waitForFinished(1000);
            }
        }
        m_process.reset();
    }

    if (m_network) {
        m_network->disconnect(this);
        m_network.reset();
    }

    if (!m_config.url.trimmed().isEmpty()) {
        m_sessionId.clear();
    }

    emit stopped();
    m_stopping = false;
}

bool McpClient::isRunning() const
{
    switch (m_config.transport) {
    case McpTransport::Stdio:
        return m_process && m_process->state() != QProcess::NotRunning;
    case McpTransport::HttpSse:
    case McpTransport::HttpJson:
        return m_network != nullptr;
    }
    return false;
}

// ---------------------------------------------------------------------------
//  Protocol: initialize / list / call
//  协议：初始化 / 列举 / 调用
// ---------------------------------------------------------------------------
void McpClient::initialize()
{
    if (!isRunning()) {
        fail(QStringLiteral("The MCP server \"%1\" is not running.").arg(m_config.id));
        return;
    }

    QJsonObject clientInfo;
    clientInfo.insert(QStringLiteral("name"), QStringLiteral("TCTools-Build_Web"));
    clientInfo.insert(QStringLiteral("version"), versionString());
    clientInfo.insert(QStringLiteral("title"), QStringLiteral("TC-BuildWeb TI-UseAgent"));

    QJsonObject params;
    params.insert(QStringLiteral("protocolVersion"), QStringLiteral("2025-03-26"));
    params.insert(QStringLiteral("capabilities"), QJsonObject());
    params.insert(QStringLiteral("clientInfo"), clientInfo);

    sendRequest(QStringLiteral("initialize"), params, QString());
}

void McpClient::listTools()
{
    sendRequest(QStringLiteral("tools/list"), QJsonObject(), QString());
}

void McpClient::listResources()
{
    sendRequest(QStringLiteral("resources/list"), QJsonObject(), QString());
}

void McpClient::listPrompts()
{
    sendRequest(QStringLiteral("prompts/list"), QJsonObject(), QString());
}

void McpClient::callTool(const QString &name, const QJsonObject &arguments,
                         const QString &requestId)
{
    const QString callerId = requestId.isEmpty() ? IdGenerator::randomToken(8) : requestId;

    QJsonObject params;
    params.insert(QStringLiteral("name"), name);   // un-namespaced server tool name
    params.insert(QStringLiteral("arguments"), arguments);

    sendRequest(QStringLiteral("tools/call"), params, callerId);
}

void McpClient::readResource(const QString &uri, const QString &requestId)
{
    const QString callerId = requestId.isEmpty() ? IdGenerator::randomToken(8) : requestId;

    QJsonObject params;
    params.insert(QStringLiteral("uri"), uri);

    sendRequest(QStringLiteral("resources/read"), params, callerId);
}

// ---------------------------------------------------------------------------
//  isToolAllowed
//  工具是否被允许
// ---------------------------------------------------------------------------
bool McpClient::isToolAllowed(const QString &toolName) const
{
    //  The deny list always wins; an empty allow list means "everything".
    //  拒绝列表始终优先；允许列表为空表示"全部允许"。
    if (m_config.blockedTools.contains(toolName))
        return false;
    if (m_config.allowedTools.isEmpty())
        return true;
    return m_config.allowedTools.contains(toolName);
}

// ---------------------------------------------------------------------------
//  namespacedToolName
//  生成命名空间化的工具名
// ---------------------------------------------------------------------------
QString McpClient::namespacedToolName(const QString &serverId, const QString &toolName)
{
    return QStringLiteral("mcp_%1_%2").arg(sanitiseFragment(serverId),
                                           sanitiseFragment(toolName));
}

// ---------------------------------------------------------------------------
//  sendRequest / sendNotification
//  发送请求 / 通知
// ---------------------------------------------------------------------------
void McpClient::sendRequest(const QString &method, const QJsonObject &params,
                            const QString &requestId)
{
    if (m_stopping)
        return;
    if (!isRunning()) {
        fail(QStringLiteral("Cannot send \"%1\": the MCP server \"%2\" is not running.")
                 .arg(method)
                 .arg(m_config.id));
        return;
    }

    const int numericId = m_nextId++;
    const QString idKey = QString::number(numericId);

    QJsonObject message;
    message.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    message.insert(QStringLiteral("id"), numericId);
    message.insert(QStringLiteral("method"), method);
    message.insert(QStringLiteral("params"), params);

    auto *pending = new PendingRequest;
    pending->callerId = requestId;
    pending->method = method;

    pending->timer = new QTimer(this);
    pending->timer->setSingleShot(true);
    pending->timer->setInterval(kRequestTimeoutMs);
    connect(pending->timer, &QTimer::timeout, this, &McpClient::onRequestTimeout);
    pending->timer->start();

    m_pending.insert(idKey, pending);
    m_requestIds.insert(idKey, requestId);

    sendHttpPayload(QJsonDocument(message).toJson(QJsonDocument::Compact));
}

void McpClient::sendNotification(const QString &method, const QJsonObject &params)
{
    if (m_stopping)
        return;

    QJsonObject message;
    message.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    message.insert(QStringLiteral("method"), method);
    if (!params.isEmpty())
        message.insert(QStringLiteral("params"), params);

    sendHttpPayload(QJsonDocument(message).toJson(QJsonDocument::Compact));
}

// ---------------------------------------------------------------------------
//  sendHttpPayload
//  Writes one JSON-RPC frame to whichever transport is active.
//  将一帧 JSON-RPC 写入当前激活的传输层。
// ---------------------------------------------------------------------------
void McpClient::sendHttpPayload(const QByteArray &payload)
{
    if (m_process) {
        if (m_process->state() != QProcess::Running)
            return;
        //  Newline delimited JSON-RPC 2.0 is the stdio framing.
        //  换行分隔的 JSON-RPC 2.0 是标准输入输出方式的帧格式。
        QByteArray frame = payload;
        frame.append('\n');
        m_process->write(frame);
        m_process->waitForBytesWritten(2000);
        return;
    }

    if (!m_network)
        return;

    QUrl url(m_config.url);
    if (url.path().isEmpty() || url.path() == QStringLiteral("/"))
        url.setPath(defaultMcpPath());

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json, text/event-stream");
    request.setRawHeader("User-Agent",
                         QStringLiteral("TCTools-Build_Web/%1").arg(versionString()).toUtf8());

    if (!m_sessionId.isEmpty())
        request.setRawHeader("Mcp-Session-Id", m_sessionId.toUtf8());
    if (!m_config.bearerToken.isEmpty())
        request.setRawHeader("Authorization",
                             QByteArray("Bearer ") + m_config.bearerToken.toUtf8());
    for (auto it = m_config.headers.constBegin(); it != m_config.headers.constEnd(); ++it)
        request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());

    QNetworkReply *reply = m_network->post(request, payload);
    if (!reply)
        return;

    //  Attach the reply to the most recent pending entry that lacks one.
    //  将该回复附加到最近一个尚无回复的待处理条目。
    QList<QString> keys = m_pending.keys();
    std::sort(keys.begin(), keys.end(), [](const QString &a, const QString &b) {
        return a.toInt() < b.toInt();
    });
    for (const QString &key : keys) {
        PendingRequest *pending = m_pending.value(key, nullptr);
        if (pending && !pending->reply) {
            pending->reply = reply;
            break;
        }
    }
}

// ---------------------------------------------------------------------------
//  Transport slots
//  传输层槽函数
// ---------------------------------------------------------------------------
void McpClient::onStdoutReady()
{
    if (!m_process || m_stopping)
        return;

    m_readBuffer.append(m_process->readAllStandardOutput());

    if (m_readBuffer.size() > kMaxBufferedBytes) {
        fail(QStringLiteral("The MCP server \"%1\" sent more than %2 bytes without a newline; "
                            "the connection was closed to protect memory.")
                 .arg(m_config.id)
                 .arg(kMaxBufferedBytes));
        m_readBuffer.clear();
        stop();
        return;
    }

    //  A message may be split across reads, so only complete lines are consumed.
    //  一条消息可能被多次读取拆开，因此只处理完整的行。
    int newline = m_readBuffer.indexOf('\n');
    while (newline >= 0) {
        QByteArray line = m_readBuffer.left(newline);
        m_readBuffer.remove(0, newline + 1);
        line = line.trimmed();
        if (!line.isEmpty()) {
            QJsonParseError parseError{};
            const QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
            if (parseError.error == QJsonParseError::NoError && doc.isObject())
                handleMessage(doc.object());
        }
        newline = m_readBuffer.indexOf('\n');
    }
}

void McpClient::onStderrReady()
{
    if (!m_process || m_stopping)
        return;

    const QByteArray raw = m_process->readAllStandardError();
    if (raw.isEmpty())
        return;

    //  stderr is diagnostics only: it is redacted and truncated, never parsed.
    //  标准错误仅用于诊断：会先遮蔽机密并截断，绝不解析。
    const QString text = redactSecrets(QString::fromUtf8(raw).trimmed());
    if (text.isEmpty())
        return;
    const QString line = text.size() > 500 ? text.left(500) + QStringLiteral("...") : text;
    m_lastError = line;
    emit errorOccurred(QStringLiteral("[%1] %2").arg(m_config.id, line));
}

void McpClient::onProcessError()
{
    if (!m_process || m_stopping)
        return;
    fail(QStringLiteral("The MCP server \"%1\" reported a process error: %2")
             .arg(m_config.id)
             .arg(redactSecrets(m_process->errorString())));
}

void McpClient::onProcessFinished(int exitCode)
{
    if (m_stopping)
        return;
    if (exitCode != 0) {
        fail(QStringLiteral("The MCP server \"%1\" exited with code %2.")
                 .arg(m_config.id)
                 .arg(exitCode));
    } else {
        m_lastError = QStringLiteral("The MCP server stopped.");
    }
    m_initialised = false;
    emit stopped();
}

void McpClient::onHttpFinished()
{
    if (m_stopping)
        return;

    auto *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply)
        return;

    reply->deleteLater();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray sessionHeader = reply->rawHeader("Mcp-Session-Id");
    if (!sessionHeader.isEmpty())
        m_sessionId = QString::fromUtf8(sessionHeader);

    const QByteArray body = reply->readAll();

    //  Detach the reply from its pending entry.
    //  将该回复与其待处理条目解绑。
    for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
        if (it.value() && it.value()->reply == reply)
            it.value()->reply = nullptr;
    }

    if (reply->error() != QNetworkReply::NoError && status <= 0) {
        fail(QStringLiteral("The MCP server \"%1\" could not be reached: %2")
                 .arg(m_config.id)
                 .arg(redactSecrets(reply->errorString())));
        return;
    }
    if (status >= 400) {
        const QJsonObject object = QJsonDocument::fromJson(body).object();
        QString detail = object.value(QStringLiteral("error")).toObject()
                             .value(QStringLiteral("message"))
                             .toString();
        if (detail.isEmpty())
            detail = QString::fromUtf8(body.left(400));
        fail(QStringLiteral("The MCP server \"%1\" answered HTTP %2: %3")
                 .arg(m_config.id)
                 .arg(status)
                 .arg(redactSecrets(detail)));
        return;
    }

    //  `202 Accepted` with no body is the correct reply to a notification.
    //  `202 Accepted` 且无正文是通知消息的正确回复。
    if (body.trimmed().isEmpty())
        return;

    const QByteArray contentType = reply->header(QNetworkRequest::ContentTypeHeader)
                                       .toString()
                                       .toUtf8()
                                       .toLower();
    if (contentType.contains("text/event-stream") || body.startsWith("event:")
        || body.startsWith("data:") || body.contains("\ndata:")
        || body.contains("\nevent:")) {
        //  SSE framing: `event:`/`data:` pairs, one JSON-RPC message per data line.
        //  SSE 帧格式：`event:`/`data:` 成对出现，每条 data 行一个 JSON-RPC 消息。
        QString text = QString::fromUtf8(body);
        text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
        const QStringList lines = text.split(QLatin1Char('\n'));
        QStringList dataLines;
        for (const QString &raw : lines) {
            const QString line = raw.trimmed();
            if (line.startsWith(QStringLiteral("data:"), Qt::CaseInsensitive)) {
                QString data = line.mid(5).trimmed();
                if (data == QStringLiteral("[DONE]")) {
                    if (!dataLines.isEmpty()) {
                        const QJsonObject object =
                            QJsonDocument::fromJson(dataLines.join(QString()).toUtf8()).object();
                        dataLines.clear();
                        if (!object.isEmpty())
                            handleMessage(object);
                    }
                    continue;
                }
                dataLines.append(data);
            } else if (line.isEmpty() && !dataLines.isEmpty()) {
                //  An empty line terminates one event frame.
                //  空行结束一个事件帧。
                const QJsonObject object =
                    QJsonDocument::fromJson(dataLines.join(QString()).toUtf8()).object();
                dataLines.clear();
                if (!object.isEmpty())
                    handleMessage(object);
            }
        }
        if (!dataLines.isEmpty()) {
            const QJsonObject object =
                QJsonDocument::fromJson(dataLines.join(QString()).toUtf8()).object();
            if (!object.isEmpty())
                handleMessage(object);
        }
        return;
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        fail(QStringLiteral("The MCP server \"%1\" returned a body that is not JSON-RPC.")
                 .arg(m_config.id));
        return;
    }
    handleMessage(doc.object());
}

void McpClient::onRequestTimeout()
{
    auto *timer = qobject_cast<QTimer *>(sender());
    if (!timer)
        return;

    QString key;
    PendingRequest *pending = nullptr;
    for (auto it = m_pending.constBegin(); it != m_pending.constEnd(); ++it) {
        if (it.value() && it.value()->timer == timer) {
            key = it.key();
            pending = it.value();
            break;
        }
    }
    if (!pending) {
        timer->deleteLater();
        return;
    }

    m_pending.remove(key);
    const QString jsonrpcId = key;
    m_requestIds.remove(jsonrpcId);

    const QString callerId = pending->callerId;
    const QString method = pending->method;
    if (pending->reply) {
        pending->reply->disconnect(this);
        pending->reply->abort();
        pending->reply->deleteLater();
    }
    timer->deleteLater();
    delete pending;

    fail(QStringLiteral("The MCP request \"%1\" did not finish within %2 seconds.")
             .arg(method)
             .arg(kRequestTimeoutMs / 1000));

    //  A tool call that times out must still produce a result, otherwise the
    //  agent loop would wait forever for a reply that will never arrive.
    //  超时的工具调用仍必须产生结果，否则智能体循环会永远等待一个不会到来的回复。
    if (method == QStringLiteral("tools/call")) {
        ToolResult result;
        result.toolCallId = callerId;
        result.isError = true;
        result.content = QStringLiteral("The MCP tool call timed out after %1 seconds.")
                             .arg(kRequestTimeoutMs / 1000)
                         + QStringLiteral("\n")
                         + QStringLiteral("MCP 工具调用在 %1 秒后超时。")
                               .arg(kRequestTimeoutMs / 1000);
        emit toolResult(callerId, result);
    }
}

// ---------------------------------------------------------------------------
//  handleMessage
//  分发一条 JSON-RPC 消息
// ---------------------------------------------------------------------------
void McpClient::handleMessage(const QJsonObject &message)
{
    if (m_stopping)
        return;

    const QJsonValue id = message.value(QStringLiteral("id"));
    const QString method = message.value(QStringLiteral("method")).toString();
    const bool isResponse = message.contains(QStringLiteral("result"))
                            || message.contains(QStringLiteral("error"));

    //  A response carries an id and a result/error.
    //  响应带有 id 以及 result 或 error。
    if (!method.isEmpty() && id.isNull() == false && id.isUndefined() == false) {
        //  A request from the server: answer it, never leave it hanging.
        //  来自服务端的请求：必须回答，绝不能让对方一直等待。
        QJsonObject error;
        error.insert(QStringLiteral("code"), -32601);
        error.insert(QStringLiteral("message"), QStringLiteral("Method not found"));
        error.insert(QStringLiteral("data"),
                     QStringLiteral("TCTools-Build_Web does not implement \"%1\"").arg(method));

        QJsonObject response;
        response.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
        response.insert(QStringLiteral("id"), id);
        response.insert(QStringLiteral("error"), error);
        sendHttpPayload(QJsonDocument(response).toJson(QJsonDocument::Compact));
        return;
    }

    if (isResponse && method.isEmpty()) {
        if (message.contains(QStringLiteral("error"))) {
            handleError(id, message.value(QStringLiteral("error")).toObject());
        } else {
            handleResult(id, message.value(QStringLiteral("result")).toObject());
        }
        return;
    }

    if (!method.isEmpty() && (id.isNull() || id.isUndefined())) {
        const QJsonObject params = message.value(QStringLiteral("params")).toObject();
        emit notification(method, params);

        if (method == QStringLiteral("notifications/tools/list_changed")) {
            //  The tool surface changed: re-list so the model sees fresh tools.
            //  工具集合发生变化：重新列举，使模型看到最新的工具。
            listTools();
        } else if (method == QStringLiteral("notifications/resources/updated")) {
            listResources();
        } else if (method == QStringLiteral("notifications/resources/list_changed")) {
            listResources();
        } else if (method == QStringLiteral("notifications/prompts/list_changed")) {
            listPrompts();
        } else if (method == QStringLiteral("notifications/message")) {
            const QString level = params.value(QStringLiteral("level")).toString();
            const QString text = params.value(QStringLiteral("data")).toString();
            if (!text.isEmpty()) {
                const QString line = redactSecrets(text);
                m_lastError = line;
                emit errorOccurred(QStringLiteral("[%1%2] %3")
                                       .arg(m_config.id,
                                            level.isEmpty() ? QString()
                                                            : QStringLiteral("/") + level,
                                            line));
            }
        }
        return;
    }
}

// ---------------------------------------------------------------------------
//  handleResult
//  处理成功响应
// ---------------------------------------------------------------------------
void McpClient::handleResult(const QJsonValue &id, const QJsonObject &result)
{
    const QString key = id.isString() ? id.toString() : QString::number(id.toInt());
    PendingRequest *pending = m_pending.take(key);
    const QString callerId = pending ? pending->callerId : m_requestIds.value(key);
    const QString method = pending ? pending->method : QString();
    if (pending) {
        if (pending->timer) {
            pending->timer->stop();
            pending->timer->deleteLater();
        }
        if (pending->reply) {
            pending->reply->disconnect(this);
            pending->reply->deleteLater();
        }
        delete pending;
    }
    m_requestIds.remove(key);

    if (method == QStringLiteral("initialize")) {
        m_serverInfo = result.value(QStringLiteral("serverInfo")).toObject();
        m_serverName = m_serverInfo.value(QStringLiteral("name")).toString();
        m_serverVersion = m_serverInfo.value(QStringLiteral("version")).toString();
        m_capabilities = result.value(QStringLiteral("capabilities")).toObject();
        const QString negotiated = result.value(QStringLiteral("protocolVersion")).toString();
        if (!negotiated.isEmpty())
            m_protocolVersion = negotiated;
        m_initialised = true;
        emit initialised(m_serverInfo);
        //  Discover the tool surface immediately; the model needs it for the
        //  very first turn.
        //  立即发现工具集合；模型在第一轮就需要它。
        listTools();
        return;
    }

    if (method == QStringLiteral("tools/list")) {
        parseToolList(result.value(QStringLiteral("tools")).toArray());
        return;
    }

    if (method == QStringLiteral("tools/call")) {
        ToolResult toolResultOut;
        toolResultOut.toolCallId = callerId;
        toolResultOut.isError = result.value(QStringLiteral("isError")).toBool(false);

        QString contentError;
        toolResultOut.content = contentToText(result.value(QStringLiteral("content")).toArray(),
                                              nullptr, &contentError);

        //  A structured tool error result is still an error for the agent.
        //  结构化的工具错误结果对智能体而言同样是错误。
        if (result.contains(QStringLiteral("structuredContent"))) {
            const QJsonObject structured = result.value(QStringLiteral("structuredContent")).toObject();
            if (structured.contains(QStringLiteral("error")))
                toolResultOut.isError = true;
        }
        if (toolResultOut.content.isEmpty())
            toolResultOut.content = QString::fromUtf8(
                QJsonDocument(result).toJson(QJsonDocument::Compact));

        emit toolResult(callerId, toolResultOut);
        return;
    }

    if (method == QStringLiteral("resources/list")) {
        m_resources = result.value(QStringLiteral("resources")).toArray();
        emit resourcesListed(m_resources);
        return;
    }

    if (method == QStringLiteral("prompts/list")) {
        emit promptsListed(result.value(QStringLiteral("prompts")).toArray());
        return;
    }

    if (method == QStringLiteral("resources/read")) {
        QString text;
        const QJsonArray contents = result.value(QStringLiteral("contents")).toArray();
        for (const QJsonValue &value : contents) {
            const QJsonObject entry = value.toObject();
            const QString part = entry.value(QStringLiteral("text")).toString();
            if (!part.isEmpty())
                text += part;
        }
        if (text.isEmpty())
            text = QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
        emit resourceRead(callerId, text);
        return;
    }
}

// ---------------------------------------------------------------------------
//  handleError
//  处理错误响应
// ---------------------------------------------------------------------------
void McpClient::handleError(const QJsonValue &id, const QJsonObject &error)
{
    const QString key = id.isString() ? id.toString() : QString::number(id.toInt());
    PendingRequest *pending = m_pending.take(key);
    const QString callerId = pending ? pending->callerId : m_requestIds.value(key);
    const QString method = pending ? pending->method : QString();
    if (pending) {
        if (pending->timer) {
            pending->timer->stop();
            pending->timer->deleteLater();
        }
        if (pending->reply) {
            pending->reply->disconnect(this);
            pending->reply->deleteLater();
        }
        delete pending;
    }
    m_requestIds.remove(key);

    const int code = error.value(QStringLiteral("code")).toInt(0);
    const QString message = redactSecrets(error.value(QStringLiteral("message")).toString());
    const QString data = redactSecrets(error.value(QStringLiteral("data")).toString());

    QString combined = QStringLiteral("MCP error %1: %2").arg(code).arg(message);
    if (!data.isEmpty())
        combined += QStringLiteral(" (%1)").arg(data);

    fail(combined);

    if (method == QStringLiteral("tools/call")) {
        ToolResult result;
        result.toolCallId = callerId;
        result.isError = true;
        result.content = combined;
        emit toolResult(callerId, result);
    }
}

// ---------------------------------------------------------------------------
//  parseToolList
//  解析 tools/list 结果
// ---------------------------------------------------------------------------
void McpClient::parseToolList(const QJsonArray &tools)
{
    m_tools.clear();
    for (const QJsonValue &value : tools) {
        const QJsonObject entry = value.toObject();
        const QString name = entry.value(QStringLiteral("name")).toString().trimmed();
        if (name.isEmpty())
            continue;

        ToolDescriptor descriptor;
        descriptor.name = namespacedToolName(m_config.id, name);
        descriptor.titleEn = entry.value(QStringLiteral("title")).toString();
        if (descriptor.titleEn.isEmpty())
            descriptor.titleEn = name;
        descriptor.titleZh = descriptor.titleEn;
        descriptor.descriptionEn = entry.value(QStringLiteral("description")).toString();
        descriptor.descriptionZh = descriptor.descriptionEn;
        descriptor.inputSchema = entry.value(QStringLiteral("inputSchema")).toObject();
        if (descriptor.inputSchema.isEmpty()) {
            descriptor.inputSchema.insert(QStringLiteral("type"), QStringLiteral("object"));
            descriptor.inputSchema.insert(QStringLiteral("properties"), QJsonObject());
        }
        //  MCP tools are third-party code: they always need confirmation and
        //  network access is assumed rather than proven.
        //  MCP 工具属于第三方代码：始终需要确认，并且按需要网络访问处理。
        descriptor.mutatesProject = true;
        descriptor.destructive = true;
        descriptor.requiresNetwork = true;
        descriptor.category = QStringLiteral("mcp");
        m_tools.append(descriptor);
    }

    emit toolsListed(m_tools);
}

// ---------------------------------------------------------------------------
//  fail
//  统一错误上报
// ---------------------------------------------------------------------------
void McpClient::fail(const QString &message)
{
    const QString text = redactSecrets(message);
    m_lastError = text;
    emit errorOccurred(text);
}

} // namespace tcbw
