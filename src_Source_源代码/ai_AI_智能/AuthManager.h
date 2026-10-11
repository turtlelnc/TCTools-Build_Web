// ============================================================================
//  AuthManager.h -- API keys, OAuth 2.0 (PKCE + device code) and token refresh
//  AuthManager.h —— API Key、OAuth 2.0（PKCE + 设备码）与令牌刷新
// ============================================================================
#pragma once

#include "AiTypes.h"
#include "ProviderRegistry.h"
#include "SecretStore.h"

#include <QObject>
#include <QString>
#include <QUrl>

#include <memory>

QT_BEGIN_NAMESPACE
class QNetworkAccessManager;
class QNetworkReply;
class QTcpServer;
class QTimer;
QT_END_NAMESPACE

namespace tcbw {

// ---------------------------------------------------------------------------
//  OAuthToken
//  OAuth 令牌
// ---------------------------------------------------------------------------
struct OAuthToken
{
    QString accessToken;
    QString refreshToken;
    QString tokenType = QStringLiteral("Bearer");
    QString scope;
    QDateTime expiresAt;
    QString idToken;

    bool isValid() const { return !accessToken.isEmpty(); }
    bool isExpired(int skewSeconds = 60) const;
    QJsonObject toJson() const;
    static OAuthToken fromJson(const QJsonObject &obj);
    static OAuthToken fromTokenResponse(const QJsonObject &obj, int httpStatus);
};

// ---------------------------------------------------------------------------
//  AuthManager
//  Owns every credential the application uses. It performs the OAuth dances
//  with a real loopback redirect listener (PKCE) or the device-code grant, and
//  it is the ONLY component allowed to read raw secrets.
//  持有应用程序使用的全部凭据。它通过真实的回环重定向监听器（PKCE）或设备码
//  授权完成 OAuth 流程，并且是唯一被允许读取原始机密的组件。
// ---------------------------------------------------------------------------
class AuthManager : public QObject
{
    Q_OBJECT

public:
    explicit AuthManager(QObject *parent = nullptr);
    ~AuthManager() override;

    // --- API keys ----------------------------------------------------------
    bool setApiKey(const QString &vendorId, const QString &apiKey, QString *error = nullptr);
    QString apiKey(const QString &vendorId) const;
    bool hasApiKey(const QString &vendorId) const;
    bool clearApiKey(const QString &vendorId);

    // --- OAuth: authorization code + PKCE ----------------------------------
    //  Starts a flow. The redirect URI defaults to
    //  http://127.0.0.1:<ephemeral-port>/oauth/callback and the user's browser
    //  is opened automatically.
    //  启动流程。重定向 URI 默认为
    //  http://127.0.0.1:<临时端口>/oauth/callback，并自动打开用户浏览器。
    QString beginPkceFlow(const VendorInfo &vendor, const QString &clientId,
                          const QString &scopes = QString(),
                          const QString &redirectUri = QString());

    // --- OAuth: device authorization grant ---------------------------------
    //  Returns the user code + verification URI through `userCode`/`verifyUri`
    //  and starts polling.
    //  通过 `userCode`/`verifyUri` 返回用户码与验证地址，并开始轮询。
    bool beginDeviceFlow(const VendorInfo &vendor, const QString &clientId,
                         const QString &scopes, QString *userCode, QString *verifyUri,
                         QString *error = nullptr);

    void cancelFlow();

    // --- token access ------------------------------------------------------
    OAuthToken token(const QString &vendorId) const;
    bool hasValidToken(const QString &vendorId) const;
    bool refreshToken(const VendorInfo &vendor, const QString &clientId);
    bool storeToken(const QString &vendorId, const OAuthToken &token);
    bool clearToken(const QString &vendorId);

    // --- resolution --------------------------------------------------------
    //  Returns the header set to attach to a request for `config`.
    //  返回为 `config` 的请求附加的请求头集合。
    QMap<QString, QString> authHeaders(const VendorInfo &vendor,
                                       const ProviderConfig &config,
                                       QString *error = nullptr) const;

    //  True when the vendor is usable right now (key or unexpired token).
    //  当厂商当前可用（有密钥或未过期令牌）时返回 true。
    bool isAuthorised(const VendorInfo &vendor,
                      const ProviderConfig &config) const;

    // --- diagnostics -------------------------------------------------------
    QString lastError() const { return m_lastError; }
    bool flowInProgress() const { return m_flowState != FlowIdle; }

signals:
    void flowStarted(const QString &vendorId, const QString &mode);
    void flowUserCodeReady(const QString &vendorId, const QString &userCode,
                           const QString &verificationUri, int expiresInSeconds);
    void flowSucceeded(const QString &vendorId);
    void flowFailed(const QString &vendorId, const QString &message);
    void flowCancelled(const QString &vendorId);
    void credentialChanged(const QString &vendorId);

private slots:
    void onRedirectConnection();
    void onTokenReplyFinished();
    void onDevicePollTimeout();

private:
    enum FlowState { FlowIdle = 0, FlowPkce, FlowDevice };
    struct PendingFlow;

    QString storeKey(const QString &vendorId, const QString &suffix) const;
    void failFlow(const QString &message);
    void completeFlow(const QString &vendorId, const OAuthToken &token);
    bool postTokenRequest(const VendorInfo &vendor, const QMap<QString, QString> &form,
                          const QString &vendorId, bool isDevicePoll);

    std::unique_ptr<QNetworkAccessManager> m_network;
    std::unique_ptr<QTcpServer> m_redirectServer;
    std::unique_ptr<QTimer> m_devicePollTimer;
    std::unique_ptr<PendingFlow> m_flow;
    FlowState m_flowState = FlowIdle;
    QString m_lastError;
};

} // namespace tcbw
