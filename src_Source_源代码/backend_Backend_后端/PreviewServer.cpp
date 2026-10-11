// ============================================================================
//  PreviewServer.cpp -- loopback HTTP server for real browser preview
//  PreviewServer.cpp —— 用于真实浏览器预览的回环 HTTP 服务器
//  Part of TCTools-Build_Web (TC-BuildWeb) v0.1.0-rc1
// ============================================================================
#include "PreviewServer.h"

#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QtAlgorithms>

#include <utility>

namespace tcbw {

namespace {

constexpr int kHeartbeatIntervalMs = 15000;

//  A request larger than this before its header terminator is hostile.
//  在头部结束符之前就超过此大小的请求是恶意的。
constexpr qsizetype kMaxHeaderBytes = 64 * 1024;

const char kHeartbeatObjectName[] = "tcbwPreviewHeartbeat";
const char kReloadCounterProperty[] = "tcbwReloadCounter";
const char kBufferProperty[] = "tcbwRequestBuffer";
const char kHeadProperty[] = "tcbwHeadRequest";

const char kEventsPath[] = "/__tcbw/events";

//  Sources allowed to load on a previewed page:
//   * 'self'          -- the served files themselves;
//   * 'unsafe-inline' -- the generated page carries inline <style>/<script>;
//   * data:/blob:     -- inlined assets;
//   * http:/https:/ws:/wss: in connect-src only, because calling the pre-linked
//     backend is the entire point of the preview.
//  预览页面允许加载的来源：
//   * 'self'          —— 所提供的文件本身；
//   * 'unsafe-inline' —— 生成的页面带有内联 <style>/<script>；
//   * data:/blob:     —— 内联资源；
//   * 仅在 connect-src 中允许 http:/https:/ws:/wss:，因为调用预链接的后端
//     正是预览存在的意义。
const char kContentSecurityPolicy[] =
    "default-src 'self'; "
    "script-src 'self' 'unsafe-inline'; "
    "style-src 'self' 'unsafe-inline'; "
    "img-src 'self' data: blob:; "
    "font-src 'self' data:; "
    "media-src 'self' data: blob:; "
    "connect-src 'self' http: https: ws: wss:; "
    "object-src 'none'; "
    "base-uri 'none'; "
    "form-action 'self'; "
    "frame-ancestors 'none'";

QString statusReason(int status)
{
    switch (status) {
    case 200: return QStringLiteral("OK");
    case 204: return QStringLiteral("No Content");
    case 400: return QStringLiteral("Bad Request");
    case 403: return QStringLiteral("Forbidden");
    case 404: return QStringLiteral("Not Found");
    case 405: return QStringLiteral("Method Not Allowed");
    case 431: return QStringLiteral("Request Header Fields Too Large");
    case 500: return QStringLiteral("Internal Server Error");
    case 505: return QStringLiteral("HTTP Version Not Supported");
    default:  return QStringLiteral("Error");
    }
}

//  Content type by file extension; empty when the extension is unknown.
//  按文件扩展名判定内容类型；未知扩展名时返回空。
QString contentTypeForPath(const QString &path)
{
    const int dot = path.lastIndexOf(QLatin1Char('.'));
    if (dot < 0 || dot == path.size() - 1)
        return QString();
    const QString suffix = path.mid(dot + 1).toLower();
    const QString charset = QStringLiteral("; charset=utf-8");

    if (suffix == QStringLiteral("html") || suffix == QStringLiteral("htm"))
        return QStringLiteral("text/html") + charset;
    if (suffix == QStringLiteral("css"))
        return QStringLiteral("text/css") + charset;
    if (suffix == QStringLiteral("js") || suffix == QStringLiteral("mjs"))
        return QStringLiteral("text/javascript") + charset;
    if (suffix == QStringLiteral("json") || suffix == QStringLiteral("map"))
        return QStringLiteral("application/json") + charset;
    if (suffix == QStringLiteral("svg"))
        return QStringLiteral("image/svg+xml");
    if (suffix == QStringLiteral("png"))
        return QStringLiteral("image/png");
    if (suffix == QStringLiteral("jpg") || suffix == QStringLiteral("jpeg"))
        return QStringLiteral("image/jpeg");
    if (suffix == QStringLiteral("gif"))
        return QStringLiteral("image/gif");
    if (suffix == QStringLiteral("webp"))
        return QStringLiteral("image/webp");
    if (suffix == QStringLiteral("ico"))
        return QStringLiteral("image/x-icon");
    if (suffix == QStringLiteral("woff"))
        return QStringLiteral("font/woff");
    if (suffix == QStringLiteral("woff2"))
        return QStringLiteral("font/woff2");
    if (suffix == QStringLiteral("ttf"))
        return QStringLiteral("font/ttf");
    if (suffix == QStringLiteral("otf"))
        return QStringLiteral("font/otf");
    if (suffix == QStringLiteral("txt"))
        return QStringLiteral("text/plain") + charset;
    if (suffix == QStringLiteral("md"))
        return QStringLiteral("text/markdown") + charset;
    if (suffix == QStringLiteral("wasm"))
        return QStringLiteral("application/wasm");
    if (suffix == QStringLiteral("xml"))
        return QStringLiteral("application/xml") + charset;
    if (suffix == QStringLiteral("webmanifest"))
        return QStringLiteral("application/manifest+json") + charset;
    return QString();
}

//  Index key: '/' separators, exactly one leading slash.
//  索引键：使用 '/' 分隔，且恰好以一个斜杠开头。
QString indexKey(const QString &relativePath)
{
    QString key = relativePath;
    key.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (key.startsWith(QStringLiteral("//")))
        key.remove(0, 1);
    if (!key.startsWith(QLatin1Char('/')))
        key.prepend(QLatin1Char('/'));
    return key;
}

QByteArray plainBody(const QString &text)
{
    return text.toUtf8();
}

} // namespace

// ---------------------------------------------------------------------------
//  Client -- one Server-Sent Events subscriber
//  客户端 —— 一个 Server-Sent Events 订阅者
// ---------------------------------------------------------------------------
struct PreviewServer::Client
{
    QTcpSocket *socket = nullptr;
    bool isEventStream = false;
};

// ---------------------------------------------------------------------------
//  PreviewServer
//  预览服务器
// ---------------------------------------------------------------------------
PreviewServer::PreviewServer(QObject *parent)
    : QObject(parent)
{
}

PreviewServer::~PreviewServer()
{
    if (m_server)
        m_server->close();
    qDeleteAll(m_clients);
    m_clients.clear();
}

bool PreviewServer::start(quint16 preferredPort, QString *error)
{
    if (isRunning())
        return true;

    if (!m_server) {
        m_server = std::make_unique<QTcpServer>();
        connect(m_server.get(), &QTcpServer::newConnection, this,
                &PreviewServer::onNewConnection);
    }

    //  LOOPBACK ONLY / 仅回环
    //  The socket is bound to 127.0.0.1, never to QHostAddress::Any: a preview
    //  server that is reachable from the network would expose the user's
    //  in-progress work to everybody on the LAN.
    //  套接字绑定到 127.0.0.1，绝不绑定 QHostAddress::Any：若预览服务器可被
    //  网络访问，就会把用户尚未完成的工作暴露给局域网内的所有人。
    if (!m_server->listen(QHostAddress::LocalHost, preferredPort)) {
        const QString message =
            QStringLiteral("Could not listen on 127.0.0.1:%1 (%2) / "
                           "无法监听 127.0.0.1:%1（%2）")
                .arg(preferredPort)
                .arg(m_server->errorString());
        if (error)
            *error = message;
        emit errorOccurred(message);
        return false;
    }

    m_port = m_server->serverPort();

    //  The header is frozen and declares no QTimer member, so the single
    //  heartbeat timer is parented to this object and found by name.
    //  头文件已冻结且未声明 QTimer 成员，因此唯一的保活定时器以本对象为父对象，
    //  并通过名字查找。
    auto *heartbeat = findChild<QTimer *>(QString::fromLatin1(kHeartbeatObjectName));
    if (!heartbeat) {
        heartbeat = new QTimer(this);
        heartbeat->setObjectName(QString::fromLatin1(kHeartbeatObjectName));
        heartbeat->setInterval(kHeartbeatIntervalMs);
        connect(heartbeat, &QTimer::timeout, this, [this]() {
            //  One timer for the whole server, never one per client.
            //  整个服务器只有一个定时器，绝不按客户端创建。
            const QByteArray ping = QByteArrayLiteral(": heartbeat\n\n");
            for (Client *client : std::as_const(m_clients)) {
                if (!client || !client->socket || !client->isEventStream)
                    continue;
                if (client->socket->state() != QAbstractSocket::ConnectedState)
                    continue;
                client->socket->write(ping);
                client->socket->flush();
            }
        });
    }
    heartbeat->start();

    emit started(m_port);
    return true;
}

void PreviewServer::stop()
{
    const bool wasRunning = isRunning();

    if (m_server)
        m_server->close();
    m_port = 0;

    const QList<QTcpSocket *> sockets = m_clients.keys();
    const bool hadClients = !m_clients.isEmpty();
    for (QTcpSocket *socket : sockets) {
        Client *client = m_clients.take(socket);
        delete client;
        if (socket) {
            socket->disconnect(this);
            socket->disconnectFromHost();
            socket->deleteLater();
        }
    }

    if (auto *heartbeat = findChild<QTimer *>(QString::fromLatin1(kHeartbeatObjectName))) {
        heartbeat->stop();
        heartbeat->deleteLater();
    }

    if (hadClients)
        emit clientDisconnected(static_cast<int>(m_clients.size()));
    if (wasRunning)
        emit stopped();
}

bool PreviewServer::isRunning() const
{
    return m_server && m_server->isListening();
}

QString PreviewServer::baseUrl() const
{
    if (!isRunning() || m_port == 0)
        return QString();
    return QStringLiteral("http://127.0.0.1:%1/").arg(m_port);
}

QString PreviewServer::urlFor(const QString &relativePath) const
{
    const QString base = baseUrl();
    if (base.isEmpty())
        return QString();

    QString path = relativePath;
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));

    QStringList encoded;
    const QStringList segments = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &segment : segments)
        encoded << QString::fromLatin1(QUrl::toPercentEncoding(segment));
    return base + encoded.join(QLatin1Char('/'));
}

void PreviewServer::setFiles(const QVector<EmittedFile> &files)
{
    m_files = files;
    m_fileIndex.clear();
    for (int i = 0; i < m_files.size(); ++i)
        m_fileIndex.insert(indexKey(m_files.at(i).relativePath), i);

    //  The header documents setFiles() as "installs ... and notifies connected
    //  browsers", so a live rebuild reaches the browser without an extra call.
    //  头文件把 setFiles() 描述为"安装新的文件集合并通知已连接的浏览器"，
    //  因此实时重建无需额外调用即可抵达浏览器。
    if (isRunning())
        notifyReload();
}

void PreviewServer::setDefaultDocument(const QString &relativePath)
{
    if (relativePath.trimmed().isEmpty())
        return;
    m_defaultDocument = indexKey(relativePath.trimmed()).mid(1);
}

void PreviewServer::notifyReload()
{
    //  The header is frozen and declares no counter member, so the counter
    //  lives in a dynamic property on this object.
    //  头文件已冻结且未声明计数器成员，因此计数器存放在本对象的动态属性中。
    const qint64 counter = property(kReloadCounterProperty).toLongLong() + 1;
    setProperty(kReloadCounterProperty, counter);

    const QByteArray frame = QByteArrayLiteral("event: reload\ndata: ")
                           + QByteArray::number(counter) + QByteArrayLiteral("\n\n");
    for (Client *client : std::as_const(m_clients)) {
        if (!client || !client->socket || !client->isEventStream)
            continue;
        if (client->socket->state() != QAbstractSocket::ConnectedState)
            continue;
        client->socket->write(frame);
        client->socket->flush();
    }
}

const EmittedFile *PreviewServer::lookup(const QString &path) const
{
    const auto it = m_fileIndex.constFind(indexKey(path));
    if (it == m_fileIndex.constEnd())
        return nullptr;
    const int index = it.value();
    if (index < 0 || index >= m_files.size())
        return nullptr;
    return &m_files.at(index);
}

// ---------------------------------------------------------------------------
//  Connections
//  连接处理
// ---------------------------------------------------------------------------
void PreviewServer::onNewConnection()
{
    while (m_server && m_server->hasPendingConnections()) {
        QTcpSocket *socket = m_server->nextPendingConnection();
        if (!socket)
            break;

        socket->setProperty(kHeadProperty, false);
        socket->setProperty(kBufferProperty, QByteArray());

        connect(socket, &QTcpSocket::readyRead, this, &PreviewServer::onClientReadyRead);
        connect(socket, &QTcpSocket::disconnected, this, &PreviewServer::onClientDisconnected);
    }
}

void PreviewServer::onClientReadyRead()
{
    auto *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket)
        return;

    //  Already upgraded to an event stream: nothing more is expected.
    //  已升级为事件流：不再期待更多数据。
    if (m_clients.contains(socket)) {
        socket->readAll();
        return;
    }

    QByteArray buffer = socket->property(kBufferProperty).toByteArray();
    buffer += socket->readAll();

    const int terminator = buffer.indexOf("\r\n\r\n");
    if (terminator < 0) {
        if (buffer.size() > kMaxHeaderBytes) {
            socket->setProperty(kBufferProperty, QByteArray());
            emit requestServed(QString(), 431);
            respond(socket, 431, statusReason(431),
                    plainBody(QStringLiteral("Request header section too large / 请求头过大")),
                    QStringLiteral("text/plain; charset=utf-8"));
            return;
        }
        socket->setProperty(kBufferProperty, buffer);
        return;
    }

    socket->setProperty(kBufferProperty, QByteArray());
    handleRequest(socket, buffer.left(terminator + 4));
}

void PreviewServer::onClientDisconnected()
{
    auto *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket)
        return;

    const auto it = m_clients.find(socket);
    if (it != m_clients.end()) {
        delete it.value();
        m_clients.erase(it);
        emit clientDisconnected(static_cast<int>(m_clients.size()));
    }
    socket->deleteLater();
}

// ---------------------------------------------------------------------------
//  Request handling
//  请求处理
// ---------------------------------------------------------------------------
void PreviewServer::handleRequest(QTcpSocket *socket, const QByteArray &request)
{
    if (!socket)
        return;

    const int firstLineEnd = request.indexOf("\r\n");
    const QByteArray requestLine =
        firstLineEnd < 0 ? request : request.left(firstLineEnd);
    const QList<QByteArray> parts = requestLine.split(' ');
    if (parts.size() < 3) {
        emit requestServed(QString(), 400);
        respond(socket, 400, statusReason(400),
                plainBody(QStringLiteral("Malformed request line / 请求行格式错误")),
                QStringLiteral("text/plain; charset=utf-8"));
        return;
    }

    const QString method = QString::fromLatin1(parts.at(0)).trimmed().toUpper();
    const QString target = QString::fromUtf8(parts.at(1));
    const QString version = QString::fromLatin1(parts.at(2)).trimmed().toUpper();

    //  ---- headers -------------------------------------------------------------
    QHash<QString, QString> headers;
    bool duplicateHost = false;
    int cursor = firstLineEnd + 2;
    while (cursor > 1 && cursor < request.size()) {
        const int lineEnd = request.indexOf("\r\n", cursor);
        if (lineEnd < 0 || lineEnd == cursor)
            break;
        const QByteArray line = request.mid(cursor, lineEnd - cursor);
        cursor = lineEnd + 2;

        const int colon = line.indexOf(':');
        if (colon <= 0)
            continue;
        const QString key = QString::fromLatin1(line.left(colon)).trimmed().toLower();
        const QString value = QString::fromUtf8(line.mid(colon + 1)).trimmed();
        if (headers.contains(key)) {
            //  Duplicate headers are dropped, and a duplicated Host is fatal:
            //  it is the classic request-smuggling shape.
            //  重复的请求头会被丢弃，而重复的 Host 直接判为致命错误：
            //  这是经典的请求走私形态。
            if (key == QStringLiteral("host"))
                duplicateHost = true;
            continue;
        }
        headers.insert(key, value);
    }

    //  ---- DNS-rebinding defence -------------------------------------------------
    //  A malicious page can point evil.example at 127.0.0.1; requiring a loopback
    //  Host header makes such a request fail, because the browser sends the
    //  attacker's host name.
    //  恶意页面可以把 evil.example 指向 127.0.0.1；要求 Host 头为回环字面量后，
    //  这类请求会失败，因为浏览器发送的是攻击者的主机名。
    if (duplicateHost || !isLoopbackHostHeader(headers.value(QStringLiteral("host")))) {
        emit requestServed(target, 403);
        respond(socket, 403, statusReason(403),
                plainBody(QStringLiteral("Preview is only reachable through 127.0.0.1 / "
                                         "预览只能通过 127.0.0.1 访问")),
                QStringLiteral("text/plain; charset=utf-8"));
        return;
    }

    if (version != QStringLiteral("HTTP/1.1") && version != QStringLiteral("HTTP/1.0")) {
        emit requestServed(target, 505);
        respond(socket, 505, statusReason(505),
                plainBody(QStringLiteral("Only HTTP/1.0 and HTTP/1.1 are supported / "
                                         "仅支持 HTTP/1.0 与 HTTP/1.1")),
                QStringLiteral("text/plain; charset=utf-8"));
        return;
    }

    const bool isHead = method == QStringLiteral("HEAD");
    socket->setProperty(kHeadProperty, isHead);

    if (method != QStringLiteral("GET") && !isHead) {
        emit requestServed(target, 405);
        respond(socket, 405, statusReason(405),
                plainBody(QStringLiteral("Only GET and HEAD are supported / "
                                         "仅支持 GET 与 HEAD")),
                QStringLiteral("text/plain; charset=utf-8"));
        return;
    }

    //  ---- path ------------------------------------------------------------------
    const QString path = normaliseRequestPath(target);
    if (path.isEmpty()) {
        //  normaliseRequestPath() returns an empty string for anything that tries
        //  to leave the root, so the request is refused, not resolved.
        //  normaliseRequestPath() 对任何试图离开根目录的输入返回空字符串，
        //  因此请求被拒绝，而不会被解析。
        emit requestServed(target, 400);
        respond(socket, 400, statusReason(400),
                plainBody(QStringLiteral("Unsafe request path / 不安全的请求路径")),
                QStringLiteral("text/plain; charset=utf-8"));
        return;
    }

    //  ---- Server-Sent Events ----------------------------------------------------
    if (path == QString::fromLatin1(kEventsPath)) {
        if (isHead) {
            emit requestServed(path, 405);
            respond(socket, 405, statusReason(405),
                    plainBody(QStringLiteral("The event stream needs GET / "
                                             "事件流需要 GET")),
                    QStringLiteral("text/plain; charset=utf-8"));
            return;
        }

        QByteArray head;
        head += QByteArrayLiteral("HTTP/1.1 200 OK\r\n");
        head += QByteArrayLiteral("Content-Type: text/event-stream; charset=utf-8\r\n");
        head += QByteArrayLiteral("Cache-Control: no-store\r\n");
        head += QByteArrayLiteral("X-Content-Type-Options: nosniff\r\n");
        head += QByteArrayLiteral("Connection: keep-alive\r\n");
        head += QByteArrayLiteral("Content-Security-Policy: ")
              + QByteArray(kContentSecurityPolicy) + QByteArrayLiteral("\r\n");
        head += QByteArrayLiteral("\r\n");
        head += QByteArrayLiteral(": connected\n\n");
        socket->write(head);
        socket->flush();

        auto *client = new Client;
        client->socket = socket;
        client->isEventStream = true;
        m_clients.insert(socket, client);

        emit requestServed(path, 200);
        emit clientConnected(static_cast<int>(m_clients.size()));
        return;
    }

    //  ---- file ------------------------------------------------------------------
    QString requested = path;
    if (requested == QStringLiteral("/")) {
        requested = indexKey(m_defaultDocument);
        if (!m_fileIndex.contains(requested))
            requested = QStringLiteral("/index.html");
    }

    const EmittedFile *file = lookup(requested);
    if (!file) {
        emit requestServed(requested, 404);
        //  The requested path is never echoed back into the response.
        //  请求路径绝不会被回显到响应中。
        respond(socket, 404, statusReason(404),
                plainBody(QStringLiteral("Not found in the generated site / "
                                         "在生成的站点中未找到")),
                QStringLiteral("text/plain; charset=utf-8"));
        return;
    }

    QString contentType = contentTypeForPath(requested);
    if (contentType.isEmpty())
        contentType = file->mimeType.trimmed();
    if (contentType.isEmpty())
        contentType = QStringLiteral("application/octet-stream");

    const QByteArray body = file->binary ? file->bytes : file->contents.toUtf8();
    emit requestServed(requested, 200);
    respond(socket, 200, statusReason(200), body, contentType);
}

void PreviewServer::respond(QTcpSocket *socket, int status, const QString &reason,
                            const QByteArray &body, const QString &contentType)
{
    if (!socket)
        return;

    //  The frozen signature carries no method, so the HEAD flag travels on the
    //  socket and the body is dropped after the headers are computed.
    //  已冻结的签名不携带方法，因此 HEAD 标记随套接字传递；头部计算完成后丢弃正文。
    const bool headOnly = socket->property(kHeadProperty).toBool();
    const QString why = reason.trimmed().isEmpty() ? statusReason(status) : reason.trimmed();
    const QString type = contentType.trimmed().isEmpty()
                             ? QStringLiteral("application/octet-stream")
                             : contentType.trimmed();

    QByteArray response;
    response.reserve(body.size() + 512);
    response += QByteArrayLiteral("HTTP/1.1 ") + QByteArray::number(status) + QByteArrayLiteral(" ")
              + why.toUtf8() + QByteArrayLiteral("\r\n");
    response += QByteArrayLiteral("Content-Type: ") + type.toUtf8() + QByteArrayLiteral("\r\n");
    response += QByteArrayLiteral("Content-Length: ") + QByteArray::number(body.size())
              + QByteArrayLiteral("\r\n");
    //  A preview must never serve a stale build from a cache.
    //  预览绝不能从缓存中提供过期的构建产物。
    response += QByteArrayLiteral("Cache-Control: no-store\r\n");
    response += QByteArrayLiteral("X-Content-Type-Options: nosniff\r\n");
    response += QByteArrayLiteral("Content-Security-Policy: ")
              + QByteArray(kContentSecurityPolicy) + QByteArrayLiteral("\r\n");
    response += QByteArrayLiteral("Referrer-Policy: no-referrer\r\n");
    if (status == 405)
        response += QByteArrayLiteral("Allow: GET, HEAD\r\n");
    response += QByteArrayLiteral("Connection: close\r\n\r\n");
    if (!headOnly)
        response += body;

    socket->write(response);
    socket->flush();
    socket->setProperty(kHeadProperty, false);
    socket->disconnectFromHost();
}

// ---------------------------------------------------------------------------
//  Safety helpers
//  安全辅助函数
// ---------------------------------------------------------------------------
bool PreviewServer::isLoopbackHostHeader(const QString &hostHeader)
{
    QString value = hostHeader.trimmed().toLower();
    if (value.isEmpty())
        return false;

    //  Bracketed IPv6 literal: only [::1] is accepted.
    //  方括号 IPv6 字面量：只接受 [::1]。
    if (value.startsWith(QLatin1Char('['))) {
        const int close = value.indexOf(QLatin1Char(']'));
        if (close < 0)
            return false;
        const QString rest = value.mid(close + 1);
        if (!rest.isEmpty() && !rest.startsWith(QLatin1Char(':')))
            return false;
        return value.mid(1, close - 1) == QStringLiteral("::1");
    }

    //  Optional port suffix.
    //  可选的端口后缀。
    const int colon = value.indexOf(QLatin1Char(':'));
    if (colon >= 0) {
        const QString port = value.mid(colon + 1);
        bool ok = false;
        const int portValue = port.toInt(&ok);
        if (!ok || portValue < 1 || portValue > 65535)
            return false;
        value = value.left(colon);
    }

    if (value.isEmpty())
        return false;
    if (value == QStringLiteral("localhost") || value == QStringLiteral("::1"))
        return true;

    //  127.x.y.z -- an exact literal match, so "0.0.0.0" and
    //  "localhost.attacker.example" are both refused.
    //  127.x.y.z —— 精确字面量匹配，因此 "0.0.0.0" 与
    //  "localhost.attacker.example" 都会被拒绝。
    const QStringList octets = value.split(QLatin1Char('.'));
    if (octets.size() != 4 || octets.at(0) != QStringLiteral("127"))
        return false;
    for (const QString &octet : octets) {
        bool ok = false;
        const int number = octet.toInt(&ok);
        if (!ok || number < 0 || number > 255)
            return false;
    }
    return true;
}

QString PreviewServer::normaliseRequestPath(const QString &rawPath)
{
    if (rawPath.isEmpty())
        return QString();

    for (const QChar ch : rawPath) {
        const char16_t unit = ch.unicode();
        if (unit == 0x00 || unit == u'\r' || unit == u'\n')
            return QString();
    }

    QString path = rawPath;
    const int fragment = path.indexOf(QLatin1Char('#'));
    if (fragment >= 0)
        path = path.left(fragment);
    const int query = path.indexOf(QLatin1Char('?'));
    if (query >= 0)
        path = path.left(query);

    //  Decode BEFORE resolving "." and "..", otherwise "%2e%2e%2f" would slip
    //  through and the traversal check would be worthless.
    //  在解析 "." 与 ".." 之前先解码，否则 "%2e%2e%2f" 会溜过去，
    //  目录穿越检查将形同虚设。
    const QString decoded = QUrl::fromPercentEncoding(path.toUtf8());
    if (decoded.contains(QChar(0)))
        return QString();

    QString unified = decoded;
    unified.replace(QLatin1Char('\\'), QLatin1Char('/'));

    QStringList segments;
    const QStringList rawSegments = unified.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &segment : rawSegments) {
        if (segment == QStringLiteral("."))
            continue;
        if (segment == QStringLiteral("..")) {
            //  Escaping the root is never legitimate, so it is refused outright
            //  rather than clamped.
            //  逃出根目录永远不合法，因此直接拒绝而不是钳制。
            if (segments.isEmpty())
                return QString();
            segments.removeLast();
            continue;
        }
        segments.append(segment);
    }

    return QLatin1Char('/') + segments.join(QLatin1Char('/'));
}

} // namespace tcbw
