// ============================================================================
//  PreviewServer.h -- loopback HTTP server for real browser preview
//  PreviewServer.h —— 用于真实浏览器预览的回环 HTTP 服务器
//
//  Qt WebEngine is optional in this project, so the authoritative preview is
//  "open the generated site in the user's real browser". This small server
//  makes that possible without writing to disk: it serves the generated files
//  straight from memory and pushes live-reload events over Server-Sent Events.
//  本项目中的 Qt WebEngine 是可选的，因此权威预览方式是"在用户真实浏览器中打开
//  生成的站点"。这个小服务器无需写入磁盘即可实现：它直接从内存提供生成的文件，
//  并通过 Server-Sent Events 推送实时重载事件。
//
//  SAFETY / 安全说明
//   * binds to 127.0.0.1 only -- never to a public interface;
//   * rejects any request whose Host header is not a loopback literal, which
//     blocks DNS-rebinding attacks from a malicious page;
//   * serves only the in-memory file set, so it can never read arbitrary files.
//   * 仅绑定 127.0.0.1 —— 绝不绑定公网接口；
//   * 拒绝任何 Host 头不是回环字面量的请求，从而阻断来自恶意页面的
//     DNS 重绑定攻击；
//   * 仅提供内存中的文件集合，因此永远无法读取任意文件。
// ============================================================================
#pragma once

#include "EmitOptions.h"

#include <QObject>
#include <QString>
#include <QVector>

#include <memory>

QT_BEGIN_NAMESPACE
class QTcpServer;
class QTcpSocket;
QT_END_NAMESPACE

namespace tcbw {

// ---------------------------------------------------------------------------
//  PreviewServer
//  预览服务器
// ---------------------------------------------------------------------------
class PreviewServer : public QObject
{
    Q_OBJECT

public:
    explicit PreviewServer(QObject *parent = nullptr);
    ~PreviewServer() override;

    //  Starts listening on an ephemeral loopback port.
    //  在临时回环端口上开始监听。
    bool start(quint16 preferredPort = 0, QString *error = nullptr);
    void stop();
    bool isRunning() const;

    quint16 port() const { return m_port; }

    //  Root URL of the running site, or an empty string when stopped.
    //  运行站点的根 URL；停止时返回空字符串。
    QString baseUrl() const;
    QString urlFor(const QString &relativePath) const;

    //  Installs a new in-memory file set and notifies connected browsers.
    //  安装新的内存文件集合，并通知已连接的浏览器。
    void setFiles(const QVector<EmittedFile> &files);
    void setDefaultDocument(const QString &relativePath);
    const QVector<EmittedFile> &files() const { return m_files; }

    //  Pushes a live-reload event to every connected browser.
    //  向所有已连接的浏览器推送实时重载事件。
    void notifyReload();

    //  Number of browsers currently connected.
    //  当前已连接的浏览器数量。
    int clientCount() const { return static_cast<int>(m_clients.size()); }

    // --- safety helpers (unit-testable) ------------------------------------
    //  True when the Host header names the loopback interface.
    //  当 Host 头指向回环接口时返回 true。
    static bool isLoopbackHostHeader(const QString &hostHeader);

    //  Resolves a request path inside the served file set, refusing traversal.
    //  在已提供的文件集合中解析请求路径，并拒绝目录穿越。
    static QString normaliseRequestPath(const QString &rawPath);

signals:
    void started(quint16 port);
    void stopped();
    void requestServed(const QString &path, int statusCode);
    void clientConnected(int totalClients);
    void clientDisconnected(int totalClients);
    void errorOccurred(const QString &message);

private slots:
    void onNewConnection();
    void onClientReadyRead();
    void onClientDisconnected();

private:
    struct Client;
    const EmittedFile *lookup(const QString &path) const;
    void respond(QTcpSocket *socket, int status, const QString &reason,
                 const QByteArray &body, const QString &contentType);
    void handleRequest(QTcpSocket *socket, const QByteArray &request);

    std::unique_ptr<QTcpServer> m_server;
    QVector<EmittedFile> m_files;
    QHash<QTcpSocket *, Client *> m_clients;
    QHash<QString, int> m_fileIndex;
    QString m_defaultDocument = QStringLiteral("index.html");
    quint16 m_port = 0;
};

} // namespace tcbw
