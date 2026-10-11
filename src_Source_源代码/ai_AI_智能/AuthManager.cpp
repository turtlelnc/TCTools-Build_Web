// ============================================================================
//  AuthManager.cpp -- API keys, OAuth 2.0 (PKCE + device code) and token refresh
//  AuthManager.cpp —— API Key、OAuth 2.0（PKCE + 设备码）与令牌刷新
//
//  SECURITY RULES ENFORCED HERE / 本文件强制执行的安全规则
//    1. Token and authorize URLs must be https, unless the host is a loopback
//       literal (127.0.0.1 / ::1 / localhost), which local development servers
//       legitimately use.
//       令牌与授权地址必须是 https；除非主机是回环字面量（127.0.0.1 / ::1 /
//       localhost）——本地开发服务器确实会这样使用。
//    2. The PKCE flow verifies `state` BEFORE exchanging the code. A mismatch
//       aborts the flow and the code is never sent anywhere.
//       PKCE 流程在换取令牌*之前*校验 `state`。不匹配即中止流程，授权码绝不外发。
//    3. A secret is only ever written into the header map returned by
//       authHeaders(); it never reaches m_lastError, a log line or a signal.
//       机密只会写入 authHeaders() 返回的请求头集合，绝不进入 m_lastError、
//       日志或任何信号。
//    4. Every QNetworkReply gets a finished() connection and a 30 s abort timer.
//       每个 QNetworkReply 都会连接 finished() 并挂上 30 秒中止定时器。
// ============================================================================
#include "AuthManager.h"

#include "IdGenerator.h"
#include "Types.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QEventLoop>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>

namespace tcbw {
namespace {

//  Every token request is aborted after this long.
//  每个令牌请求在此时间后被中止。
constexpr int kTokenRequestTimeoutMs = 30000;

//  Polling defaults for the device authorization grant (RFC 8628).
//  设备授权流程（RFC 8628）的轮询默认值。
constexpr int kDefaultPollSeconds = 5;
constexpr int kSlowDownIncrementSeconds = 5;
constexpr int kDefaultDeviceCodeLifetimeSeconds = 600;

bool isLoopbackHost(const QString &host)
{
    const QString h = host.trimmed().toLower();
    return h == QLatin1String("127.0.0.1") || h == QLatin1String("localhost")
           || h == QLatin1String("::1") || h == QLatin1String("[::1]")
           || h.startsWith(QLatin1String("127."));
}

//  https anywhere, or http only on loopback.
//  任意位置允许 https；http 仅允许回环地址。
bool isAcceptableEndpoint(const QUrl &url)
{
    if (!url.isValid() || url.host().isEmpty())
        return false;
    const QString scheme = url.scheme().toLower();
    if (scheme == QLatin1String("https"))
        return true;
    if (scheme == QLatin1String("http"))
        return isLoopbackHost(url.host());
    return false;
}

//  Constant-time comparison for the OAuth state value.
//  OAuth state 取值的常量时间比较。
bool constantTimeEquals(const QString &a, const QString &b)
{
    const QByteArray left = a.toUtf8();
    const QByteArray right = b.toUtf8();
    if (left.size() != right.size())
        return false;
    unsigned char diff = 0;
    for (int i = 0; i < left.size(); ++i)
        diff |= static_cast<unsigned char>(left.at(i) ^ right.at(i));
    return diff == 0;
}

QString jsonString(const QJsonObject &obj, std::initializer_list<const char *> names)
{
    for (const char *name : names) {
        const QJsonValue value = obj.value(QString::fromLatin1(name));
        if (value.isString() && !value.toString().isEmpty())
            return value.toString();
    }
    return QString();
}

int jsonInt(const QJsonObject &obj, std::initializer_list<const char *> names, int fallback)
{
    for (const char *name : names) {
        const QJsonValue value = obj.value(QString::fromLatin1(name));
        if (value.isDouble())
            return value.toInt();
        if (value.isString()) {
            bool ok = false;
            const int parsed = value.toString().toInt(&ok);
            if (ok)
                return parsed;
        }
    }
    return fallback;
}

//  The browser landing page after a loopback redirect. Self-contained: no
//  scripts, no fonts, no network requests, nothing that could be mistaken for
//  phishing.
//  回环重定向后浏览器看到的落地页。完全自包含：无脚本、无外部字体、无网络请求，
//  不会被误认为钓鱼页面。
QByteArray resultPage(bool success, const QString &reasonEn, const QString &reasonZh)
{
    const QString colour = success ? QStringLiteral("#12805c") : QStringLiteral("#b3261e");
    const QString titleEn = success ? QStringLiteral("Authorization succeeded")
                                    : QStringLiteral("Authorization failed");
    const QString titleZh = success ? QStringLiteral("授权成功") : QStringLiteral("授权失败");

    QString detailHtml;
    if (!reasonEn.isEmpty() || !reasonZh.isEmpty()) {
        detailHtml = QStringLiteral("<p class=\"detail\">%1</p><p class=\"detail\">%2</p>")
                         .arg(reasonEn.toHtmlEscaped(), reasonZh.toHtmlEscaped());
    }

    const QString html =
        QStringLiteral(
            "<!doctype html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n"
            "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n"
            "<title>TC-BuildWeb - %1</title>\n<style>\n"
            "body{font-family:system-ui,-apple-system,'Segoe UI',Roboto,sans-serif;margin:0;"
            "display:flex;align-items:center;justify-content:center;min-height:100vh;"
            "background:#f4f5f7;color:#1b1d21}\n"
            ".card{max-width:34rem;margin:1.5rem;padding:2rem 2.25rem;background:#fff;"
            "border-radius:14px;box-shadow:0 10px 30px rgba(0,0,0,.08)}\n"
            "h1{font-size:1.2rem;margin:0 0 .9rem;color:%2}\n"
            "p{margin:.35rem 0;line-height:1.55}\n"
            ".detail{color:#5a5f68;font-size:.92rem}\n"
            "</style>\n</head>\n<body>\n<div class=\"card\">\n"
            "<h1>%3 / %4</h1>\n"
            "<p>You can close this tab and return to TC-BuildWeb.</p>\n"
            "<p>你可以关闭此标签页并返回 TC-BuildWeb。</p>\n"
            "%5\n</div>\n</body>\n</html>\n")
            .arg(titleEn, colour, titleEn, titleZh, detailHtml);

    return html.toUtf8();
}

//  Human readable, bilingual text for an OAuth error code.
//  针对 OAuth 错误码的双语提示。
QString describeOAuthError(const QString &code, bool chinese)
{
    if (code == QLatin1String("authorization_pending")) {
        return chinese ? QStringLiteral("用户尚未完成授权，继续等待。")
                       : QStringLiteral("The user has not finished authorising yet; still waiting.");
    }
    if (code == QLatin1String("slow_down")) {
        return chinese ? QStringLiteral("轮询过于频繁，服务器要求放慢速度。")
                       : QStringLiteral("Polling too quickly; the server asked us to slow down.");
    }
    if (code == QLatin1String("access_denied")) {
        return chinese ? QStringLiteral("用户拒绝了本次授权请求。")
                       : QStringLiteral("The user denied the authorization request.");
    }
    if (code == QLatin1String("expired_token")) {
        return chinese ? QStringLiteral("设备码已过期，请重新开始授权。")
                       : QStringLiteral("The device code expired; start the authorization again.");
    }
    if (code == QLatin1String("invalid_grant")) {
        return chinese ? QStringLiteral("授权码或刷新令牌无效、已使用或已过期。")
                       : QStringLiteral("The code or refresh token is invalid, already used or "
                                        "expired.");
    }
    if (code == QLatin1String("invalid_client")) {
        return chinese ? QStringLiteral("客户端 id 不被该厂商接受。")
                       : QStringLiteral("The vendor rejected this client id.");
    }
    return code;
}

} // namespace

// ---------------------------------------------------------------------------
//  PendingFlow -- one authorization attempt
//  PendingFlow —— 一次授权尝试
// ---------------------------------------------------------------------------
struct AuthManager::PendingFlow
{
    QString id;                       // flow id handed to the caller / 返回给调用方的流程 id
    QString vendorId;
    QString mode;                     // "pkce" | "device" / 流程类型
    VendorInfo vendor;
    QString clientId;
    QString redirectUri;
    QString codeVerifier;
    QString state;
    QString scopes;

    //  Device authorization grant / 设备码授权
    QString deviceCode;
    int intervalSeconds = kDefaultPollSeconds;
    int expiresInSeconds = kDefaultDeviceCodeLifetimeSeconds;
    QDateTime expiresAt;

    //  Which kind of token reply is currently in flight.
    //  当前在途的令牌回复属于哪种类型。
    bool isDevicePoll = false;
    QNetworkReply *reply = nullptr;
    bool timedOut = false;
};

// ---------------------------------------------------------------------------
//  OAuthToken
//  OAuth 令牌
// ---------------------------------------------------------------------------
bool OAuthToken::isExpired(int skewSeconds) const
{
    //  A token with no expiry never expires as far as this class is concerned:
    //  several vendors issue non-expiring personal tokens.
    //  没有过期时间的令牌在本类看来永不过期：部分厂商签发的是长期有效的个人令牌。
    if (!expiresAt.isValid())
        return false;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    return now >= expiresAt.addSecs(-qMax(0, skewSeconds));
}

QJsonObject OAuthToken::toJson() const
{
    //  WARNING / 警告: the returned object contains live credentials. It may only
    //  be handed to SecretStore, never written to a project file or a log.
    //  警告：返回的对象包含有效凭据。只能交给 SecretStore，绝不能写入工程文件或日志。
    QJsonObject obj;
    obj.insert(QStringLiteral("accessToken"), accessToken);
    if (!refreshToken.isEmpty())
        obj.insert(QStringLiteral("refreshToken"), refreshToken);
    obj.insert(QStringLiteral("tokenType"), tokenType.isEmpty()
                                                ? QStringLiteral("Bearer")
                                                : tokenType);
    if (!scope.isEmpty())
        obj.insert(QStringLiteral("scope"), scope);
    if (expiresAt.isValid())
        obj.insert(QStringLiteral("expiresAt"), expiresAt.toUTC().toString(Qt::ISODateWithMs));
    if (!idToken.isEmpty())
        obj.insert(QStringLiteral("idToken"), idToken);
    return obj;
}

OAuthToken OAuthToken::fromJson(const QJsonObject &obj)
{
    OAuthToken token;
    token.accessToken = jsonString(obj, {"accessToken", "access_token"});
    token.refreshToken = jsonString(obj, {"refreshToken", "refresh_token"});
    token.tokenType = jsonString(obj, {"tokenType", "token_type"});
    if (token.tokenType.isEmpty())
        token.tokenType = QStringLiteral("Bearer");
    token.scope = jsonString(obj, {"scope"});
    token.idToken = jsonString(obj, {"idToken", "id_token"});

    const QString stamp = jsonString(obj, {"expiresAt", "expires_at"});
    if (!stamp.isEmpty()) {
        token.expiresAt = QDateTime::fromString(stamp, Qt::ISODateWithMs);
        if (!token.expiresAt.isValid())
            token.expiresAt = QDateTime::fromString(stamp, Qt::ISODate);
        if (token.expiresAt.isValid())
            token.expiresAt = token.expiresAt.toUTC();
    } else {
        const int seconds = jsonInt(obj, {"expiresIn", "expires_in"}, 0);
        if (seconds > 0)
            token.expiresAt = QDateTime::currentDateTimeUtc().addSecs(seconds);
    }
    return token;
}

OAuthToken OAuthToken::fromTokenResponse(const QJsonObject &obj, int httpStatus)
{
    OAuthToken token;

    //  RFC 6749 section 5.2: an error response carries "error".
    //  RFC 6749 第 5.2 节：错误响应包含 "error"。
    const QString error = jsonString(obj, {"error", "error_code"});
    if (!error.isEmpty() || httpStatus >= 400)
        return token;                      // invalid token / 无效令牌

    token.accessToken = jsonString(obj, {"access_token", "accessToken"});
    token.refreshToken = jsonString(obj, {"refresh_token", "refreshToken"});
    token.tokenType = jsonString(obj, {"token_type", "tokenType"});
    if (token.tokenType.isEmpty())
        token.tokenType = QStringLiteral("Bearer");
    token.scope = jsonString(obj, {"scope"});
    token.idToken = jsonString(obj, {"id_token", "idToken"});

    const int expiresIn = jsonInt(obj, {"expires_in", "expiresIn"}, 0);
    if (expiresIn > 0)
        token.expiresAt = QDateTime::currentDateTimeUtc().addSecs(expiresIn);
    //  No expires_in means a non-expiring token (isExpired() honours that).
    //  没有 expires_in 表示长期令牌（isExpired() 已考虑这一点）。
    return token;
}

// ---------------------------------------------------------------------------
//  AuthManager
//  AuthManager
// ---------------------------------------------------------------------------
AuthManager::AuthManager(QObject *parent)
    : QObject(parent)
    , m_network(std::make_unique<QNetworkAccessManager>())
    , m_devicePollTimer(std::make_unique<QTimer>())
{
    m_devicePollTimer->setSingleShot(true);
    connect(m_devicePollTimer.get(), &QTimer::timeout, this, &AuthManager::onDevicePollTimeout);
}

AuthManager::~AuthManager()
{
    //  Tear everything down without emitting signals from a destructor.
    //  全部清理，且不在析构函数中发出信号。
    if (m_flow && m_flow->reply) {
        QNetworkReply *reply = m_flow->reply;
        m_flow->reply = nullptr;
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
    }
    if (m_redirectServer) {
        m_redirectServer->close();
        m_redirectServer.reset();
    }
    if (m_devicePollTimer)
        m_devicePollTimer->stop();
    m_flow.reset();
    m_flowState = FlowIdle;
}

QString AuthManager::storeKey(const QString &vendorId, const QString &suffix) const
{
    return vendorId.trimmed().toLower() + QLatin1Char('/') + suffix;
}

// ---------------------------------------------------------------------------
//  API keys
//  API Key
// ---------------------------------------------------------------------------
bool AuthManager::setApiKey(const QString &vendorId, const QString &apiKey, QString *error)
{
    const QString id = vendorId.trimmed();
    if (id.isEmpty()) {
        if (error)
            *error = QStringLiteral("No vendor was selected / 尚未选择厂商");
        return false;
    }

    const QString key = apiKey.trimmed();
    if (key.isEmpty()) {
        if (error)
            *error = QStringLiteral("The API key is empty; use Clear to remove a stored key / "
                                    "API Key 为空；如需删除已保存的密钥请使用「清除」");
        return false;
    }

    //  Hard floor: anything shorter than eight characters cannot be a real key,
    //  so storing it would only produce confusing 401s later.
    //  硬性下限：少于八个字符的不可能是真实密钥，存下来只会在后续产生令人困惑的 401。
    if (key.size() < 8) {
        if (error)
            *error = QStringLiteral("This does not look like an API key - it is only %1 characters "
                                    "long. Please paste the complete key. / 这看起来不是 API Key"
                                    "——它只有 %1 个字符。请粘贴完整的密钥。")
                         .arg(key.size());
        return false;
    }

    //  Soft validation: warn about a wrong prefix or an unusually short key but
    //  still store it, because vendors rename key formats without notice and
    //  the user may be pasting a key issued by a proxy.
    //  软校验：前缀不符或长度异常时给出警告但仍然保存，因为厂商会不打招呼地更改密钥
    //  格式，用户也可能粘贴的是代理签发的密钥。
    QString warning;
    if (const VendorInfo *vendor = ProviderRegistry::instance().find(id)) {
        if (!vendor->keyPrefixHint.isEmpty() && !key.startsWith(vendor->keyPrefixHint)) {
            warning = QStringLiteral("Heads up: %1 keys usually start with \"%2\". Stored anyway. / "
                                     "提示：%1 的密钥通常以「%2」开头。仍已保存。")
                          .arg(vendor->labelEn, vendor->keyPrefixHint);
        } else if (vendor->keyMinLength > 0 && key.size() < vendor->keyMinLength) {
            warning = QStringLiteral("Heads up: %1 keys are usually at least %2 characters. Stored "
                                     "anyway. / 提示：%1 的密钥通常至少 %2 个字符。仍已保存。")
                          .arg(vendor->labelEn)
                          .arg(vendor->keyMinLength);
        }
    }

    QString storeError;
    if (!SecretStore::instance().store(storeKey(id, QStringLiteral("api-key")), key, &storeError)) {
        if (error)
            *error = storeError.isEmpty()
                         ? QStringLiteral("The credential store refused to save the key / "
                                          "凭据存储拒绝保存该密钥")
                         : storeError;
        return false;
    }

    //  Only the warning (never the key) is surfaced or logged.
    //  只会向外暴露或记录警告文本，绝不记录密钥本身。
    if (error)
        *error = warning;
    emit credentialChanged(id);
    return true;
}

QString AuthManager::apiKey(const QString &vendorId) const
{
    if (vendorId.trimmed().isEmpty())
        return QString();
    return SecretStore::instance().retrieve(storeKey(vendorId, QStringLiteral("api-key")));
}

bool AuthManager::hasApiKey(const QString &vendorId) const
{
    return !apiKey(vendorId).isEmpty();
}

bool AuthManager::clearApiKey(const QString &vendorId)
{
    const QString id = vendorId.trimmed();
    if (id.isEmpty())
        return false;
    QString error;
    const bool ok = SecretStore::instance().remove(storeKey(id, QStringLiteral("api-key")), &error);
    if (!ok && !error.isEmpty())
        m_lastError = redactForPrompt(error);
    emit credentialChanged(id);
    return ok;
}

// ---------------------------------------------------------------------------
//  OAuth: authorization code + PKCE
//  OAuth：授权码 + PKCE
// ---------------------------------------------------------------------------
QString AuthManager::beginPkceFlow(const VendorInfo &vendor, const QString &clientId,
                                   const QString &scopes, const QString &redirectUri)
{
    cancelFlow();
    m_lastError.clear();

    if (vendor.id.isEmpty()) {
        failFlow(QStringLiteral("No vendor was selected / 尚未选择厂商"));
        return QString();
    }

    const QString resolvedClientId =
        clientId.trimmed().isEmpty() ? vendor.oauthClientId.trimmed() : clientId.trimmed();
    if (resolvedClientId.isEmpty()) {
        failFlow(QStringLiteral("This vendor needs a client id before authorization can start / "
                                "开始授权前需要先填写该厂商的客户端 id"));
        return QString();
    }

    const QUrl authorizeUrl(vendor.oauthAuthorizeUrl);
    if (vendor.oauthAuthorizeUrl.isEmpty() || !isAcceptableEndpoint(authorizeUrl)) {
        failFlow(QStringLiteral("Refusing to open an authorization page that is not https (only "
                                "loopback addresses may use http) / 拒绝打开非 https 的授权页面"
                                "（仅回环地址允许使用 http）"));
        return QString();
    }

    // --- loopback listener / 回环监听器 -----------------------------------
    QString redirect = redirectUri.trimmed();
    int pinnedPort = 0;
    if (redirect.isEmpty() && !vendor.oauthRedirectUri.isEmpty()) {
        redirect = vendor.oauthRedirectUri;
        pinnedPort = QUrl(redirect).port();
    } else if (!redirect.isEmpty()) {
        pinnedPort = QUrl(redirect).port();
    }

    auto server = std::make_unique<QTcpServer>();
    if (!server->listen(QHostAddress::LocalHost, static_cast<quint16>(qMax(0, pinnedPort)))) {
        failFlow(QStringLiteral("Could not open the loopback callback port: %1 / 无法打开回环回调"
                                "端口：%1")
                     .arg(server->errorString()));
        return QString();
    }

    if (redirect.isEmpty()) {
        redirect = QStringLiteral("http://127.0.0.1:%1/oauth/callback")
                       .arg(server->serverPort());
    }
    //  The listener must outlive this method, and its real port is what the
    //  vendor will call back on.
    //  监听器必须比本方法活得更久，且厂商回调使用的就是它的真实端口。
    m_redirectServer = std::move(server);
    connect(m_redirectServer.get(), &QTcpServer::newConnection,
            this, &AuthManager::onRedirectConnection);

    // --- PKCE and CSRF material / PKCE 与 CSRF 材料 ------------------------
    auto flow = std::make_unique<PendingFlow>();
    flow->id = IdGenerator::randomToken(24);
    flow->vendorId = vendor.id;
    flow->mode = QStringLiteral("pkce");
    flow->vendor = vendor;
    flow->clientId = resolvedClientId;
    flow->redirectUri = redirect;
    flow->codeVerifier = IdGenerator::randomToken(64);
    flow->state = IdGenerator::randomToken(32);
    flow->scopes = scopes.isEmpty() ? vendor.oauthScopes : scopes;

    QUrl url(vendor.oauthAuthorizeUrl);
    QUrlQuery query(url);
    query.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    query.addQueryItem(QStringLiteral("client_id"), flow->clientId);
    query.addQueryItem(QStringLiteral("redirect_uri"), flow->redirectUri);
    if (!flow->scopes.isEmpty())
        query.addQueryItem(QStringLiteral("scope"), flow->scopes);
    query.addQueryItem(QStringLiteral("state"), flow->state);
    if (vendor.oauthUsesPkce) {
        query.addQueryItem(QStringLiteral("code_challenge"),
                           IdGenerator::pkceChallenge(flow->codeVerifier));
        query.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
    }
    url.setQuery(query);

    const QString flowId = flow->id;
    m_flow = std::move(flow);
    m_flowState = FlowPkce;

    emit flowStarted(vendor.id, QStringLiteral("pkce"));

    if (!QDesktopServices::openUrl(url)) {
        //  No browser: fail honestly and hand the URL to the user through the
        //  error text so they can open it themselves.
        //  无法打开浏览器：如实失败，并把地址放进错误文本，让用户自行打开。
        failFlow(QStringLiteral("Could not open your browser. Open this address manually: %1 / "
                                "无法打开浏览器。请手动打开以下地址：%1")
                     .arg(url.toString()));
        return QString();
    }

    return flowId;
}

void AuthManager::onRedirectConnection()
{
    if (!m_redirectServer)
        return;

    //  Take a snapshot of the pending sockets before handling any of them:
    //  completing or failing a flow closes and destroys the listener, so the
    //  loop must never look at m_redirectServer again afterwards. (Touching it
    //  after failFlow() used to dereference a null pointer.)
    //  在开始处理前先对等待中的连接做一次快照：完成或中止流程都会关闭并销毁监听器，
    //  因此之后绝不能再访问 m_redirectServer。（此前在 failFlow() 之后再访问它会
    //  解引用空指针。）
    QVector<QTcpSocket *> pending;
    while (m_redirectServer->hasPendingConnections()) {
        if (QTcpSocket *socket = m_redirectServer->nextPendingConnection())
            pending.append(socket);
    }

    for (QTcpSocket *socket : pending) {
        //  Own the socket from here on so destroying the listener does not pull
        //  the socket out from under this loop.
        //  从这里开始由本对象持有套接字，避免销毁监听器时把套接字从本循环脚下抽走。
        socket->setParent(this);

        //  The browser sends the request line immediately after connecting; a
        //  short bounded wait keeps this slot simple without ever hanging.
        //  浏览器会在连接后立即发送请求行；短暂且有上限的等待让本槽保持简单，
        //  同时不会真正卡住。
        socket->waitForReadyRead(3000);
        const QByteArray request = socket->readAll();

        const int lineEnd = request.indexOf("\r\n");
        const QByteArray requestLine = lineEnd >= 0 ? request.left(lineEnd) : request;
        const QList<QByteArray> parts = requestLine.split(' ');
        if (parts.size() < 2 || parts.at(0) != "GET") {
            socket->write("HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            socket->flush();
            socket->disconnectFromHost();
            socket->deleteLater();
            continue;
        }

        const QByteArray target = parts.at(1);
        const int questionMark = target.indexOf('?');
        const QByteArray rawQuery = questionMark >= 0 ? target.mid(questionMark + 1) : QByteArray();
        const QUrlQuery query(QString::fromUtf8(rawQuery));
        const QString code = query.queryItemValue(QStringLiteral("code"),
                                                  QUrl::FullyDecoded);
        const QString state = query.queryItemValue(QStringLiteral("state"),
                                                   QUrl::FullyDecoded);
        const QString error = query.queryItemValue(QStringLiteral("error"),
                                                   QUrl::FullyDecoded);
        const QString errorDescription =
            query.queryItemValue(QStringLiteral("error_description"), QUrl::FullyDecoded);

        //  Ignore unrelated hits such as /favicon.ico: they carry no OAuth
        //  parameters and must not abort a flow that is still waiting.
        //  忽略 /favicon.ico 之类的无关请求：它们不带 OAuth 参数，
        //  不能中断仍在等待的流程。
        if (code.isEmpty() && state.isEmpty() && error.isEmpty()) {
            const QByteArray body =
                resultPage(false,
                           QStringLiteral("This page is the TC-BuildWeb authorization callback. "
                                          "Nothing to do here."),
                           QStringLiteral("这是 TC-BuildWeb 的授权回调页面，无需在此操作。"));
            QByteArray response =
                "HTTP/1.1 404 Not Found\r\nContent-Type: text/html; charset=utf-8\r\n"
                "Cache-Control: no-store\r\nConnection: close\r\nContent-Length: ";
            response += QByteArray::number(body.size());
            response += "\r\n\r\n";
            response += body;
            socket->write(response);
            socket->flush();
            socket->disconnectFromHost();
            socket->deleteLater();
            continue;
        }

        //  From here on this connection is ours; stop listening so a second
        //  callback cannot race with this one. The closed server is destroyed by
        //  failFlow()/completeFlow(), so close it through the null-safe guard.
        //  从这里开始这次连接归我们处理；停止监听，避免第二个回调与当前处理竞争。
        //  已关闭的监听器会由 failFlow()/completeFlow() 销毁，因此通过空指针安全的
        //  方式关闭它。
        if (m_redirectServer)
            m_redirectServer->close();

        const auto respond = [&socket](bool success, const QString &en, const QString &zh) {
            const QByteArray body = resultPage(success, en, zh);
            QByteArray response =
                "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
                "Cache-Control: no-store\r\nConnection: close\r\nContent-Length: ";
            response += QByteArray::number(body.size());
            response += "\r\n\r\n";
            response += body;
            socket->write(response);
            socket->flush();
            socket->disconnectFromHost();
            socket->deleteLater();
        };

        if (m_flowState != FlowPkce || !m_flow) {
            respond(false, QStringLiteral("This callback does not belong to an active "
                                          "authorization attempt."),
                    QStringLiteral("该回调不属于任何进行中的授权流程。"));
            continue;
        }

        if (!error.isEmpty()) {
            const QString en = QStringLiteral("The vendor reported: %1")
                                   .arg(redactForPrompt(errorDescription.isEmpty()
                                                            ? error
                                                            : errorDescription));
            const QString zh = QStringLiteral("厂商返回：%1")
                                   .arg(redactForPrompt(describeOAuthError(error, true)));
            respond(false, en, zh);
            failFlow(en + QStringLiteral(" / ") + zh);
            continue;
        }

        // --- CSRF defence: the state must match before anything else --------
        // --- CSRF 防护：必须先匹配 state，然后才做其他任何事 ----------------
        if (state.isEmpty() || !constantTimeEquals(state, m_flow->state)) {
            const QString en = QStringLiteral("Authorization was rejected: the state parameter did "
                                              "not match, so the code was NOT exchanged.");
            const QString zh = QStringLiteral("授权被拒绝：state 参数不匹配，因此未使用该授权码换取"
                                              "令牌。");
            respond(false, en, zh);
            //  failFlow() resets the flow, so the code above must stay unused.
            //  failFlow() 会重置流程，因此上面的授权码绝不会被使用。
            failFlow(en + QStringLiteral(" / ") + zh);
            continue;
        }

        if (code.isEmpty()) {
            const QString en = QStringLiteral("The callback did not contain an authorization code.");
            const QString zh = QStringLiteral("回调中没有授权码。");
            respond(false, en, zh);
            failFlow(en + QStringLiteral(" / ") + zh);
            continue;
        }

        respond(true, QString(), QString());

        //  Exchange the code. The client secret is deliberately not part of a
        //  public PKCE client.
        //  换取令牌。公共 PKCE 客户端刻意不包含客户端密钥。
        QMap<QString, QString> form;
        form.insert(QStringLiteral("grant_type"), QStringLiteral("authorization_code"));
        form.insert(QStringLiteral("code"), code);
        form.insert(QStringLiteral("redirect_uri"), m_flow->redirectUri);
        form.insert(QStringLiteral("client_id"), m_flow->clientId);
        form.insert(QStringLiteral("code_verifier"), m_flow->codeVerifier);
        if (!postTokenRequest(m_flow->vendor, form, m_flow->vendorId, false)) {
            //  postTokenRequest already reported a bilingual error.
            //  postTokenRequest 已经报告了双语错误。
            return;
        }
    }
}

// ---------------------------------------------------------------------------
//  OAuth: device authorization grant
//  OAuth：设备码授权
// ---------------------------------------------------------------------------
bool AuthManager::beginDeviceFlow(const VendorInfo &vendor, const QString &clientId,
                                  const QString &scopes, QString *userCode, QString *verifyUri,
                                  QString *error)
{
    cancelFlow();
    m_lastError.clear();

    const auto fail = [this, error](const QString &message) {
        m_lastError = redactForPrompt(message);
        if (error)
            *error = m_lastError;
        emit flowFailed(QString(), m_lastError);
        return false;
    };

    if (vendor.id.isEmpty())
        return fail(QStringLiteral("No vendor was selected / 尚未选择厂商"));

    const QString resolvedClientId =
        clientId.trimmed().isEmpty() ? vendor.oauthClientId.trimmed() : clientId.trimmed();
    if (resolvedClientId.isEmpty()) {
        return fail(QStringLiteral("This vendor needs a client id before authorization can start / "
                                   "开始授权前需要先填写该厂商的客户端 id"));
    }

    const QUrl deviceUrl(vendor.oauthDeviceCodeUrl);
    if (vendor.oauthDeviceCodeUrl.isEmpty() || !isAcceptableEndpoint(deviceUrl)) {
        return fail(QStringLiteral("Refusing to contact a device-code endpoint that is not https "
                                   "(only loopback addresses may use http) / 拒绝访问非 https 的"
                                   "设备码端点（仅回环地址允许使用 http）"));
    }

    // --- request the device code / 申请设备码 ------------------------------
    QNetworkRequest request(deviceUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    const QString effectiveScopes = scopes.isEmpty() ? vendor.oauthScopes : scopes;
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("client_id"), resolvedClientId);
    if (!effectiveScopes.isEmpty())
        form.addQueryItem(QStringLiteral("scope"), effectiveScopes);

    QNetworkReply *reply = m_network->post(request, form.toString(QUrl::FullyEncoded).toUtf8());
    QTimer abortTimer;
    abortTimer.setSingleShot(true);
    abortTimer.setInterval(kTokenRequestTimeoutMs);

    bool timedOut = false;
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    connect(&abortTimer, &QTimer::timeout, &loop, [&] {
        timedOut = true;
        reply->abort();
    });
    abortTimer.start();
    //  A bounded nested loop: the user asked for this, and the request must be
    //  finished before a user code can be shown.
    //  有界的嵌套事件循环：这是用户主动发起的操作，且必须先拿到用户码才能展示。
    loop.exec();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    const auto networkError = reply->error();
    const QString networkErrorText = reply->errorString();
    reply->deleteLater();

    if (timedOut) {
        return fail(QStringLiteral("The vendor did not answer within 30 seconds (device code "
                                   "request) / 厂商在 30 秒内没有响应（设备码请求）"));
    }
    if (networkError != QNetworkReply::NoError) {
        return fail(QStringLiteral("The device code request failed: %1 / 设备码请求失败：%1")
                        .arg(redactForPrompt(networkErrorText)));
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (!doc.isObject()) {
        return fail(QStringLiteral("The device code response was not JSON (HTTP %1) / 设备码响应"
                                   "不是 JSON（HTTP %1）")
                        .arg(status));
    }
    const QJsonObject obj = doc.object();

    const QString deviceCode = jsonString(obj, {"device_code"});
    const QString code = jsonString(obj, {"user_code"});
    QString verification = jsonString(obj, {"verification_uri_complete", "verification_uri",
                                            "verification_url"});
    if (deviceCode.isEmpty() || code.isEmpty() || verification.isEmpty()) {
        const QString reported = jsonString(obj, {"error_description", "error"});
        return fail(QStringLiteral("The vendor did not return a usable device code%1 / 厂商未返回"
                                   "可用的设备码%1")
                        .arg(reported.isEmpty()
                                 ? QString()
                                 : QStringLiteral(" (") + redactForPrompt(reported)
                                       + QStringLiteral(")")));
    }

    const int expiresIn = jsonInt(obj, {"expires_in"}, kDefaultDeviceCodeLifetimeSeconds);
    int interval = jsonInt(obj, {"interval"}, kDefaultPollSeconds);
    if (interval < 1)
        interval = kDefaultPollSeconds;

    auto flow = std::make_unique<PendingFlow>();
    flow->id = IdGenerator::randomToken(24);
    flow->vendorId = vendor.id;
    flow->mode = QStringLiteral("device");
    flow->vendor = vendor;
    flow->clientId = resolvedClientId;
    flow->scopes = effectiveScopes;
    flow->deviceCode = deviceCode;
    flow->intervalSeconds = interval;
    flow->expiresInSeconds = expiresIn;
    flow->expiresAt = QDateTime::currentDateTimeUtc().addSecs(expiresIn);

    m_flow = std::move(flow);
    m_flowState = FlowDevice;

    if (userCode)
        *userCode = code;
    if (verifyUri)
        *verifyUri = verification;
    if (error)
        error->clear();

    emit flowStarted(vendor.id, QStringLiteral("device"));
    emit flowUserCodeReady(vendor.id, code, verification, expiresIn);

    //  Give the user a moment before the first poll: a brand new code is never
    //  authorized yet, and an immediate poll just burns rate limit.
    //  第一次轮询前给用户一点时间：新生成的设备码一定还没被授权，
    //  立刻轮询只会白白消耗限流额度。
    m_devicePollTimer->start(interval * 1000);
    return true;
}

void AuthManager::onDevicePollTimeout()
{
    if (m_flowState != FlowDevice || !m_flow)
        return;

    if (m_flow->expiresAt.isValid()
        && QDateTime::currentDateTimeUtc() >= m_flow->expiresAt) {
        failFlow(describeOAuthError(QStringLiteral("expired_token"), false)
                 + QStringLiteral(" / ")
                 + describeOAuthError(QStringLiteral("expired_token"), true));
        return;
    }

    QMap<QString, QString> form;
    form.insert(QStringLiteral("grant_type"),
                QStringLiteral("urn:ietf:params:oauth:grant-type:device_code"));
    form.insert(QStringLiteral("device_code"), m_flow->deviceCode);
    form.insert(QStringLiteral("client_id"), m_flow->clientId);

    postTokenRequest(m_flow->vendor, form, m_flow->vendorId, true);
}

// ---------------------------------------------------------------------------
//  Token requests
//  令牌请求
// ---------------------------------------------------------------------------
bool AuthManager::postTokenRequest(const VendorInfo &vendor,
                                   const QMap<QString, QString> &form,
                                   const QString &vendorId, bool isDevicePoll)
{
    if (!m_flow) {
        m_lastError = QStringLiteral("No authorization flow is active / 没有进行中的授权流程");
        return false;
    }

    const QUrl tokenUrl(vendor.oauthTokenUrl);
    if (vendor.oauthTokenUrl.isEmpty() || !isAcceptableEndpoint(tokenUrl)) {
        failFlow(QStringLiteral("Refusing to send credentials to a token endpoint that is not "
                                "https (only loopback addresses may use http) / 拒绝把凭据发送到"
                                "非 https 的令牌端点（仅回环地址允许使用 http）"));
        return false;
    }
    //  Belt and braces: even on an acceptable endpoint, never put a client
    //  secret or a refresh token on the wire in clear text.
    //  双保险：即使在可接受的端点上，也绝不把客户端密钥或刷新令牌以明文发出。
    if (tokenUrl.scheme().compare(QLatin1String("https"), Qt::CaseInsensitive) != 0
        && (form.contains(QStringLiteral("client_secret"))
            || form.contains(QStringLiteral("refresh_token")))) {
        failFlow(QStringLiteral("Refusing to send a client secret or refresh token over plain "
                                "http / 拒绝通过明文 http 发送客户端密钥或刷新令牌"));
        return false;
    }

    QNetworkRequest request(tokenUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);

    QUrlQuery body;
    for (auto it = form.constBegin(); it != form.constEnd(); ++it)
        body.addQueryItem(it.key(), it.value());

    QNetworkReply *reply = m_network->post(request, body.toString(QUrl::FullyEncoded).toUtf8());
    if (!reply) {
        failFlow(QStringLiteral("The token request could not be created / 无法创建令牌请求"));
        return false;
    }

    m_flow->isDevicePoll = isDevicePoll;
    m_flow->reply = reply;
    m_flow->timedOut = false;

    //  A reply must never outlive its request: a strictly single-shot abort
    //  timer is parented to the reply so it is destroyed with it.
    //  回复绝不能比请求活得更久：严格的单次中止定时器以回复为父对象，
    //  从而随其一同销毁。
    auto *abortTimer = new QTimer(reply);
    abortTimer->setSingleShot(true);
    abortTimer->setInterval(kTokenRequestTimeoutMs);
    connect(abortTimer, &QTimer::timeout, reply, [this, reply] {
        if (m_flow && m_flow->reply == reply)
            m_flow->timedOut = true;
        reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, &AuthManager::onTokenReplyFinished);
    abortTimer->start();

    Q_UNUSED(vendorId);
    return true;
}

void AuthManager::onTokenReplyFinished()
{
    auto *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply)
        return;
    reply->deleteLater();

    if (!m_flow || m_flow->reply != reply)
        return;                            // stale reply / 过期回复

    m_flow->reply = nullptr;
    const bool isDevicePoll = m_flow->isDevicePoll;
    const bool timedOut = m_flow->timedOut;
    const QString vendorId = m_flow->vendorId;

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    const QNetworkReply::NetworkError networkError = reply->error();
    const QString networkErrorText = reply->errorString();

    if (timedOut) {
        if (isDevicePoll) {
            //  A single slow poll is not fatal; keep waiting until the device
            //  code expires.
            //  单次轮询过慢并不致命；继续等待直到设备码过期。
            if (m_flow && m_flow->expiresAt.isValid()
                && QDateTime::currentDateTimeUtc() < m_flow->expiresAt) {
                m_devicePollTimer->start(m_flow->intervalSeconds * 1000);
                return;
            }
            failFlow(QStringLiteral("The vendor stopped answering while waiting for authorization / "
                                    "等待授权期间厂商不再响应"));
            return;
        }
        failFlow(QStringLiteral("The token request timed out after 30 seconds / 令牌请求在 30 秒后"
                                "超时"));
        return;
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    const QJsonObject obj = doc.isObject() ? doc.object() : QJsonObject();

    if (networkError != QNetworkReply::NoError && obj.isEmpty()) {
        failFlow(QStringLiteral("The token request failed: %1 / 令牌请求失败：%1")
                     .arg(redactForPrompt(networkErrorText)));
        return;
    }

    const QString errorCode = jsonString(obj, {"error", "error_code"});
    if (isDevicePoll) {
        // --- RFC 8628 polling outcomes / RFC 8628 轮询结果 ------------------
        if (!errorCode.isEmpty()) {
            if (errorCode == QLatin1String("authorization_pending")) {
                if (m_flow && m_flow->expiresAt.isValid()
                    && QDateTime::currentDateTimeUtc() >= m_flow->expiresAt) {
                    failFlow(describeOAuthError(QStringLiteral("expired_token"), false)
                             + QStringLiteral(" / ")
                             + describeOAuthError(QStringLiteral("expired_token"), true));
                    return;
                }
                m_devicePollTimer->start(m_flow->intervalSeconds * 1000);
                return;
            }
            if (errorCode == QLatin1String("slow_down")) {
                if (m_flow) {
                    m_flow->intervalSeconds += kSlowDownIncrementSeconds;
                    m_devicePollTimer->start(m_flow->intervalSeconds * 1000);
                }
                return;
            }
            failFlow(describeOAuthError(errorCode, false) + QStringLiteral(" / ")
                     + describeOAuthError(errorCode, true));
            return;
        }

        const OAuthToken token = OAuthToken::fromTokenResponse(obj, status);
        if (!token.isValid()) {
            failFlow(QStringLiteral("The vendor returned no access token while polling / "
                                    "轮询时厂商未返回访问令牌"));
            return;
        }
        completeFlow(vendorId, token);
        return;
    }

    // --- authorization-code exchange / 授权码换取令牌 ----------------------
    if (!errorCode.isEmpty()) {
        const QString description = jsonString(obj, {"error_description", "error_message"});
        const QString en = QStringLiteral("The token exchange failed: %1")
                               .arg(redactForPrompt(describeOAuthError(errorCode, false)));
        const QString zh = QStringLiteral("令牌换取失败：%1")
                               .arg(redactForPrompt(description.isEmpty()
                                                        ? describeOAuthError(errorCode, true)
                                                        : description));
        failFlow(en + QStringLiteral(" / ") + zh);
        return;
    }

    const OAuthToken token = OAuthToken::fromTokenResponse(obj, status);
    if (!token.isValid()) {
        failFlow(QStringLiteral("The token exchange returned no access token (HTTP %1) / 令牌换取"
                                "未返回访问令牌（HTTP %1）")
                     .arg(status));
        return;
    }
    completeFlow(vendorId, token);
}

// ---------------------------------------------------------------------------
//  Flow helpers
//  流程辅助
// ---------------------------------------------------------------------------
void AuthManager::completeFlow(const QString &vendorId, const OAuthToken &token)
{
    const QString id = vendorId.isEmpty() && m_flow ? m_flow->vendorId : vendorId;

    //  Stop listening and clear the pending state before announcing success, so
    //  a signal handler can immediately start another flow.
    //  在宣告成功之前停止监听并清理待处理状态，使信号处理函数可以立即开始新流程。
    if (m_redirectServer) {
        m_redirectServer->close();
        m_redirectServer.reset();
    }
    if (m_devicePollTimer)
        m_devicePollTimer->stop();
    m_flow.reset();
    m_flowState = FlowIdle;

    if (!storeToken(id, token)) {
        failFlow(QStringLiteral("The access token was received but could not be stored securely / "
                                "已收到访问令牌，但无法安全保存"));
        return;
    }
    m_lastError.clear();
    emit flowSucceeded(id);
}

void AuthManager::failFlow(const QString &message)
{
    const QString id = m_flow ? m_flow->vendorId : QString();

    if (m_flow && m_flow->reply) {
        QNetworkReply *reply = m_flow->reply;
        m_flow->reply = nullptr;
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
    }
    if (m_redirectServer) {
        m_redirectServer->close();
        m_redirectServer.reset();
    }
    if (m_devicePollTimer)
        m_devicePollTimer->stop();

    m_flow.reset();
    m_flowState = FlowIdle;

    //  Redaction is applied here as well: a vendor error string can echo back a
    //  token fragment.
    //  这里同样做遮蔽：厂商的错误文本可能回显令牌片段。
    m_lastError = redactForPrompt(message);
    emit flowFailed(id, m_lastError);
}

void AuthManager::cancelFlow()
{
    if (m_flowState == FlowIdle)
        return;

    const QString id = m_flow ? m_flow->vendorId : QString();

    if (m_flow && m_flow->reply) {
        QNetworkReply *reply = m_flow->reply;
        m_flow->reply = nullptr;
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
    }
    if (m_redirectServer) {
        m_redirectServer->close();
        m_redirectServer.reset();
    }
    if (m_devicePollTimer)
        m_devicePollTimer->stop();

    m_flow.reset();
    m_flowState = FlowIdle;

    emit flowCancelled(id);
}

// ---------------------------------------------------------------------------
//  Token storage
//  令牌保存
// ---------------------------------------------------------------------------
OAuthToken AuthManager::token(const QString &vendorId) const
{
    if (vendorId.trimmed().isEmpty())
        return OAuthToken();

    const QString raw =
        SecretStore::instance().retrieve(storeKey(vendorId, QStringLiteral("oauth-token")));
    if (raw.isEmpty())
        return OAuthToken();

    const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8());
    if (!doc.isObject())
        return OAuthToken();
    return OAuthToken::fromJson(doc.object());
}

bool AuthManager::hasValidToken(const QString &vendorId) const
{
    const OAuthToken stored = token(vendorId);
    return stored.isValid() && !stored.isExpired();
}

bool AuthManager::storeToken(const QString &vendorId, const OAuthToken &token)
{
    const QString id = vendorId.trimmed();
    if (id.isEmpty())
        return false;

    if (!token.isValid()) {
        return clearToken(id);
    }

    const QByteArray json = QJsonDocument(token.toJson()).toJson(QJsonDocument::Compact);
    QString error;
    if (!SecretStore::instance().store(storeKey(id, QStringLiteral("oauth-token")),
                                       QString::fromUtf8(json), &error)) {
        if (!error.isEmpty())
            m_lastError = redactForPrompt(error);
        return false;
    }
    emit credentialChanged(id);
    return true;
}

bool AuthManager::clearToken(const QString &vendorId)
{
    const QString id = vendorId.trimmed();
    if (id.isEmpty())
        return false;
    QString error;
    const bool ok =
        SecretStore::instance().remove(storeKey(id, QStringLiteral("oauth-token")), &error);
    if (!ok && !error.isEmpty())
        m_lastError = redactForPrompt(error);
    emit credentialChanged(id);
    return ok;
}

// ---------------------------------------------------------------------------
//  Refresh
//  刷新
// ---------------------------------------------------------------------------
bool AuthManager::refreshToken(const VendorInfo &vendor, const QString &clientId)
{
    m_lastError.clear();

    const OAuthToken current = token(vendor.id);
    if (current.refreshToken.isEmpty()) {
        m_lastError = QStringLiteral("There is no refresh token for %1; sign in again / %1 没有刷新"
                                     "令牌，请重新登录")
                          .arg(vendor.labelEn.isEmpty() ? vendor.id : vendor.labelEn);
        return false;
    }

    const QString resolvedClientId =
        clientId.trimmed().isEmpty() ? vendor.oauthClientId.trimmed() : clientId.trimmed();
    if (resolvedClientId.isEmpty()) {
        m_lastError = QStringLiteral("A client id is required to refresh this token / 刷新该令牌需要"
                                     "客户端 id");
        return false;
    }

    const QUrl tokenUrl(vendor.oauthTokenUrl);
    if (vendor.oauthTokenUrl.isEmpty() || !isAcceptableEndpoint(tokenUrl)) {
        m_lastError = QStringLiteral("Refusing to send a refresh token to an endpoint that is not "
                                     "https (only loopback addresses may use http) / 拒绝把刷新令牌"
                                     "发送到非 https 的端点（仅回环地址允许使用 http）");
        return false;
    }

    QNetworkRequest request(tokenUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QUrlQuery body;
    body.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("refresh_token"));
    body.addQueryItem(QStringLiteral("refresh_token"), current.refreshToken);
    body.addQueryItem(QStringLiteral("client_id"), resolvedClientId);
    if (!current.scope.isEmpty())
        body.addQueryItem(QStringLiteral("scope"), current.scope);

    QNetworkReply *reply = m_network->post(request, body.toString(QUrl::FullyEncoded).toUtf8());
    if (!reply) {
        m_lastError = QStringLiteral("The refresh request could not be created / 无法创建刷新请求");
        return false;
    }

    QTimer abortTimer;
    abortTimer.setSingleShot(true);
    abortTimer.setInterval(kTokenRequestTimeoutMs);
    bool timedOut = false;

    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    connect(&abortTimer, &QTimer::timeout, &loop, [&] {
        timedOut = true;
        reply->abort();
    });
    abortTimer.start();
    loop.exec();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray responseBody = reply->readAll();
    const QNetworkReply::NetworkError networkError = reply->error();
    const QString networkErrorText = reply->errorString();
    reply->deleteLater();

    if (timedOut) {
        m_lastError = QStringLiteral("The refresh request timed out after 30 seconds / 刷新请求在 "
                                     "30 秒后超时");
        return false;
    }
    if (networkError != QNetworkReply::NoError && responseBody.isEmpty()) {
        m_lastError = redactForPrompt(
            QStringLiteral("The refresh request failed: %1 / 刷新请求失败：%1")
                .arg(networkErrorText));
        return false;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(responseBody);
    const QJsonObject obj = doc.isObject() ? doc.object() : QJsonObject();

    const QString errorCode = jsonString(obj, {"error", "error_code"});
    if (!errorCode.isEmpty()) {
        m_lastError = describeOAuthError(errorCode, false) + QStringLiteral(" / ")
                      + describeOAuthError(errorCode, true);
        return false;
    }

    OAuthToken refreshed = OAuthToken::fromTokenResponse(obj, status);
    if (!refreshed.isValid()) {
        m_lastError = QStringLiteral("The refresh response contained no access token (HTTP %1) / "
                                     "刷新响应中没有访问令牌（HTTP %1）")
                          .arg(status);
        return false;
    }
    //  Several vendors rotate refresh tokens and several do not: keep the old
    //  one when the response omits a new one.
    //  部分厂商会轮换刷新令牌，部分不会：若响应中没有新的刷新令牌就保留旧的。
    if (refreshed.refreshToken.isEmpty())
        refreshed.refreshToken = current.refreshToken;
    if (refreshed.scope.isEmpty())
        refreshed.scope = current.scope;

    return storeToken(vendor.id, refreshed);
}

// ---------------------------------------------------------------------------
//  Request headers
//  请求头
// ---------------------------------------------------------------------------
QMap<QString, QString> AuthManager::authHeaders(const VendorInfo &vendor,
                                                const ProviderConfig &config,
                                                QString *error) const
{
    QMap<QString, QString> headers;

    //  An unexpired OAuth access token always beats a static API key: it is the
    //  more recently granted, more narrowly scoped credential.
    //  未过期的 OAuth 访问令牌始终优先于静态 API Key：它是更新授予、权限更窄的凭据。
    const OAuthToken stored = token(vendor.id);
    const bool useToken = stored.isValid() && !stored.isExpired();
    const QString secret = useToken ? stored.accessToken : apiKey(vendor.id);
    const QString headerHint = vendor.keyHeaderNames.value(0);

    if (secret.isEmpty()) {
        //  Local servers that declare AuthMode::None are legitimately anonymous.
        //  声明了 AuthMode::None 的本地服务可以合法地匿名访问。
        if (vendor.supports(AuthMode::None) || vendor.isLocal) {
            if (error)
                error->clear();
            return headers;
        }
        if (error) {
            *error = QStringLiteral("No credential is stored for %1. Add an API key or sign in "
                                    "first. / %1 没有已保存的凭据，请先添加 API Key 或登录。")
                         .arg(vendor.labelEn.isEmpty() ? vendor.id : vendor.labelEn);
        }
        return headers;
    }

    switch (vendor.flavor) {
    case ApiFlavor::Anthropic: {
        //  Anthropic uses x-api-key for API keys and Bearer for OAuth tokens.
        //  Anthropic 的 API Key 使用 x-api-key，OAuth 令牌使用 Bearer。
        if (useToken)
            headers.insert(QStringLiteral("Authorization"),
                           QStringLiteral("Bearer ") + secret);
        else
            headers.insert(QStringLiteral("x-api-key"), secret);
        headers.insert(QStringLiteral("anthropic-version"), QStringLiteral("2023-06-01"));
        break;
    }
    case ApiFlavor::GoogleGemini: {
        //  A Google API key is not a bearer token: it belongs in x-goog-api-key
        //  (or ?key=). An OAuth 2.0 access token does use Authorization.
        //  Google 的 API Key 不是承载令牌：它应放在 x-goog-api-key（或 ?key=）中。
        //  OAuth 2.0 访问令牌才使用 Authorization。
        if (!useToken && secret.startsWith(QLatin1String("AIza")))
            headers.insert(QStringLiteral("x-goog-api-key"), secret);
        else
            headers.insert(QStringLiteral("Authorization"),
                           QStringLiteral("Bearer ") + secret);
        break;
    }
    case ApiFlavor::Ollama: {
        //  Usually empty; only a proxied or hosted Ollama needs a bearer.
        //  通常为空；只有经过代理或托管的 Ollama 才需要承载令牌。
        headers.insert(QStringLiteral("Authorization"), QStringLiteral("Bearer ") + secret);
        break;
    }
    case ApiFlavor::AwsBedrock: {
        if (useToken || !secret.isEmpty()) {
            //  Bedrock API keys travel as bearer tokens; SigV4 signing of the
            //  request body has to happen in the HTTP client.
            //  Bedrock 的 API Key 以承载令牌形式传输；请求体的 SigV4 签名需要由
            //  HTTP 客户端完成。
            headers.insert(QStringLiteral("Authorization"),
                           QStringLiteral("Bearer ") + secret);
        }
        if (config.authMode == QLatin1String("aws-sigv4") && error && error->isEmpty()) {
            *error = QStringLiteral("This vendor is configured for AWS Signature V4; the request "
                                    "body must be signed by the HTTP client. / 该厂商配置为 AWS "
                                    "签名 V4，请求体需要由 HTTP 客户端签名。");
        }
        break;
    }
    case ApiFlavor::Custom:
    case ApiFlavor::OpenAiCompatible:
    default: {
        //  Azure OpenAI expects the key in its own api-key header; everything
        //  else in this family uses a bearer token.
        //  Azure OpenAI 要求把密钥放在专用的 api-key 头中；该家族的其他厂商都使用
        //  承载令牌。
        if (!useToken && headerHint.compare(QLatin1String("api-key"), Qt::CaseInsensitive) == 0) {
            headers.insert(QStringLiteral("api-key"), secret);
        } else {
            headers.insert(QStringLiteral("Authorization"),
                           QStringLiteral("Bearer ") + secret);
        }
        break;
    }
    }

    // --- vendor specific extras / 厂商专属附加头 ---------------------------
    if (vendor.id.compare(QLatin1String("openai"), Qt::CaseInsensitive) == 0) {
        if (!config.organisation.isEmpty())
            headers.insert(QStringLiteral("OpenAI-Organization"), config.organisation);
        if (!config.project.isEmpty())
            headers.insert(QStringLiteral("OpenAI-Project"), config.project);
    }
    if (vendor.id.compare(QLatin1String("openrouter"), Qt::CaseInsensitive) == 0) {
        //  OpenRouter attributes traffic to an application through these two
        //  headers; without them the request still works.
        //  OpenRouter 通过这两个头把流量归属到具体应用；不带它们请求依然可用。
        headers.insert(QStringLiteral("HTTP-Referer"), QStringLiteral("https://github.com/turtlelnc/TCTools-Build_Web"));
        headers.insert(QStringLiteral("X-Title"), QStringLiteral("TC-BuildWeb"));
    }

    if (error)
        error->clear();
    return headers;
}

bool AuthManager::isAuthorised(const VendorInfo &vendor, const ProviderConfig &config) const
{
    Q_UNUSED(config);

    if (hasValidToken(vendor.id))
        return true;
    if (hasApiKey(vendor.id))
        return true;
    //  A local server or a vendor that declares "none" needs no credential.
    //  本地服务或声明了「无需授权」的厂商不需要凭据。
    return vendor.isLocal || vendor.supports(AuthMode::None);
}

} // namespace tcbw
