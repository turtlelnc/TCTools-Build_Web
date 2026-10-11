// ============================================================================
//  BackendConnector.h -- live testing and wiring of backend endpoints
//  BackendConnector.h —— 后端端点的实时测试与接线
// ============================================================================
#pragma once

#include "Endpoint.h"

#include <QNetworkRequest>
#include <QObject>
#include <QString>
#include <QVector>

#include <memory>

QT_BEGIN_NAMESPACE
class QNetworkAccessManager;
class QNetworkReply;
QT_END_NAMESPACE

namespace tcbw {

class AuthManager;

// ---------------------------------------------------------------------------
//  BackendConnector
//  Sends test requests from inside the builder so the user can verify an
//  endpoint before exporting. It never follows redirects to a different host,
//  never sends credentials to a host other than the endpoint's own, and refuses
//  plain-HTTP requests that carry a secret.
//  从构建器内部发送测试请求，使用户在导出前即可验证端点。它从不跟随跨主机重定向，
//  从不把凭据发送到端点自身以外的主机，并拒绝携带机密的明文 HTTP 请求。
// ---------------------------------------------------------------------------
class BackendConnector : public QObject
{
    Q_OBJECT

public:
    explicit BackendConnector(QObject *parent = nullptr);
    ~BackendConnector() override;

    void setAuthManager(AuthManager *auth) { m_auth = auth; }

    //  Sends a test request. Returns a request id.
    //  发送测试请求，返回请求 id。
    QString test(const Endpoint &endpoint,
                 const QMap<QString, QString> &variables = {},
                 const QString &requestId = QString());

    void cancel(const QString &requestId);
    void cancelAll();

    // --- Backend target catalogue -----------------------------------------
    static const QVector<BackendTarget> &targets();
    static const BackendTarget *findTarget(const QString &id);

    //  Probes a backend target's health endpoint.
    //  探测后端目标的健康检查端点。
    QString probeHealth(const BackendTarget &target, const QString &baseUrl,
                        const QString &requestId = QString());

    // --- safety helpers (unit-testable) ------------------------------------
    //  True when `url` is http:// (not https) and carries a credential.
    //  当 `url` 为 http://（非 https）且携带凭据时返回 true。
    static bool isInsecureCredentialUrl(const QUrl &url, bool carriesCredential);

    //  True when `redirect` leaves the origin of `origin`.
    //  当 `redirect` 离开了 `origin` 的同源范围时返回 true。
    static bool isCrossOriginRedirect(const QUrl &origin, const QUrl &redirect);

    //  Validates a URL for outbound testing (scheme, host, no credentials in
    //  the userinfo component).
    //  校验用于出站测试的 URL（协议、主机，且用户信息部分不得包含凭据）。
    static bool isSafeTestUrl(const QUrl &url, QString *reasonEn = nullptr,
                              QString *reasonZh = nullptr);

signals:
    void testStarted(const QString &requestId, const QString &endpointId);
    void testFinished(const QString &requestId, const EndpointTestResult &result);
    void healthProbeFinished(const QString &requestId, bool ok, const QString &detail);

private slots:
    void onReplyFinished();

private:
    struct PendingTest;
    QNetworkAccessManager *network();
    QNetworkRequest buildRequest(const Endpoint &endpoint,
                                 const QMap<QString, QString> &variables,
                                 QByteArray *bodyOut, QString *error);

    std::unique_ptr<QNetworkAccessManager> m_network;
    QHash<QString, PendingTest *> m_pending;
    AuthManager *m_auth = nullptr;
};

} // namespace tcbw
