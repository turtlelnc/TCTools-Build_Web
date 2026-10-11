// ============================================================================
//  BackendConnector.cpp -- live testing and wiring of backend endpoints
//  BackendConnector.cpp —— 后端端点的实时测试与接线
//  Part of TCTools-Build_Web (TC-BuildWeb) v0.1.0-rc1
// ============================================================================
//
//  NOTE / 注意
//  BackendConnector.h names QNetworkRequest in a member declaration but does
//  not include or forward-declare it, so the Qt headers must come first here.
//  BackendConnector.h 在成员声明中使用了 QNetworkRequest，却没有包含或前置声明
//  它，因此这里必须先包含 Qt 头文件。
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include "BackendConnector.h"

#include "AuthManager.h"
#include "IdGenerator.h"
#include "SecretStore.h"

#include <QElapsedTimer>
#include <QRegularExpression>
#include <QTimer>
#include <QUrlQuery>

namespace tcbw {

namespace {

//  Hard limits that keep a hostile or broken server from hurting the builder.
//  防止恶意或故障服务器伤害构建器的硬性限制。
constexpr int kMaxRedirects = 5;
constexpr qint64 kMaxStoredBodyBytes = 2 * 1024 * 1024;   // 2 MB / 2 MB
constexpr int kHealthProbeTimeoutMs = 5000;

//  Cloud metadata services. A request test must never be usable as an SSRF
//  primitive against the machine's own cloud credentials, so these addresses
//  are refused outright. 169.254.0.0/16 is the link-local block that AWS,
//  Azure and OpenStack use for instance metadata.
//  云元数据服务。请求测试绝不能被当作攻击本机云凭据的 SSRF 原语，因此这些地址
//  被直接拒绝。169.254.0.0/16 是 AWS、Azure 与 OpenStack 用于实例元数据的
//  链路本地网段。
bool isCloudMetadataHost(const QString &host)
{
    QString h = host.trimmed().toLower();
    if (h.startsWith(QLatin1Char('[')) && h.endsWith(QLatin1Char(']')))
        h = h.mid(1, h.size() - 2);
    if (h.isEmpty())
        return false;

    if (h == QStringLiteral("metadata.google.internal")
        || h == QStringLiteral("metadata.goog")
        || h == QStringLiteral("instance-data")
        || h == QStringLiteral("169.254.169.254")
        || h == QStringLiteral("100.100.100.200")) {
        return true;
    }

    //  Whole link-local block, not just the well-known literal.
    //  整个链路本地网段，而不仅是众所周知的字面量。
    const QStringList octets = h.split(QLatin1Char('.'));
    if (octets.size() == 4 && octets.at(0) == QStringLiteral("169")
        && octets.at(1) == QStringLiteral("254")) {
        return true;
    }
    return false;
}

//  True when `host` is a loopback literal (localhost, 127.0.0.0/8, ::1).
//  当 `host` 是回环字面量（localhost、127.0.0.0/8、::1）时返回 true。
bool isLoopbackHost(const QString &host)
{
    QString h = host.trimmed().toLower();
    if (h.isEmpty())
        return false;
    if (h.startsWith(QLatin1Char('[')) && h.endsWith(QLatin1Char(']')))
        h = h.mid(1, h.size() - 2);
    if (h == QStringLiteral("localhost") || h == QStringLiteral("::1"))
        return true;

    const QStringList octets = h.split(QLatin1Char('.'));
    if (octets.size() != 4 || octets.at(0) != QStringLiteral("127"))
        return false;
    for (const QString &octet : octets) {
        bool ok = false;
        const int value = octet.toInt(&ok);
        if (!ok || value < 0 || value > 255)
            return false;
    }
    return true;
}

//  A header name must be an RFC 7230 token, otherwise it is dropped: a value
//  coming from a project file must never be able to smuggle a second header.
//  头字段名必须是 RFC 7230 的 token，否则会被丢弃：来自工程文件的值绝不能
//  夹带第二个头字段。
QString sanitiseHeaderName(const QString &name)
{
    static const QRegularExpression token(
        QStringLiteral("^[!#$%&'*+.^_`|~0-9A-Za-z-]+$"));
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || !token.match(trimmed).hasMatch())
        return QString();
    return trimmed;
}

//  Strips anything that could terminate a header line ("response splitting").
//  去除可能终止头行的字符（"响应拆分"）。
QString sanitiseHeaderValue(const QString &value)
{
    QString out;
    out.reserve(value.size());
    for (const QChar ch : value) {
        const char16_t unit = ch.unicode();
        if (unit == u'\r' || unit == u'\n' || unit == 0x00)
            continue;
        out += ch;
    }
    return out.trimmed();
}

//  Single-pass {{placeholder}} substitution without URL or JSON escaping, used
//  for query parameter values (QUrlQuery performs the encoding) and headers.
//  不做 URL 或 JSON 转义的单遍 {{占位符}} 替换，用于查询参数值
//  （由 QUrlQuery 负责编码）与请求头。
QString expandPlain(const QString &templ, const QMap<QString, QString> &variables)
{
    QString out;
    out.reserve(templ.size());
    int cursor = 0;
    while (cursor < templ.size()) {
        const int open = templ.indexOf(QStringLiteral("{{"), cursor);
        if (open < 0) {
            out += templ.mid(cursor);
            break;
        }
        out += templ.mid(cursor, open - cursor);
        const int close = templ.indexOf(QStringLiteral("}}"), open + 2);
        if (close < 0) {
            out += templ.mid(open);
            break;
        }
        const QString name = templ.mid(open + 2, close - open - 2).trimmed();
        const auto it = variables.constFind(name);
        if (it == variables.constEnd())
            out += templ.mid(open, close + 2 - open);
        else
            out += it.value();
        cursor = close + 2;   // single pass / 单遍
    }
    return out;
}

//  Default port for a scheme, matching the rule in QUrl::port(defaultPort).
//  协议的默认端口，与 QUrl::port(defaultPort) 的规则一致。
int schemeDefaultPort(const QString &scheme)
{
    const QString s = scheme.toLower();
    if (s == QStringLiteral("http") || s == QStringLiteral("ws"))
        return 80;
    if (s == QStringLiteral("https") || s == QStringLiteral("wss"))
        return 443;
    return -1;
}

QMap<QString, QString> responseHeadersOf(QNetworkReply *reply)
{
    QMap<QString, QString> headers;
    const QList<QPair<QByteArray, QByteArray>> pairs = reply->rawHeaderPairs();
    for (const auto &pair : pairs)
        headers.insert(QString::fromUtf8(pair.first), QString::fromUtf8(pair.second));
    return headers;
}

//  Caps a response body so one huge reply cannot exhaust the builder's memory.
//  限制响应正文大小，使单个巨大的响应无法耗尽构建器内存。
QByteArray capBody(const QByteArray &body)
{
    if (body.size() <= kMaxStoredBodyBytes)
        return body;
    const qint64 total = body.size();
    QByteArray capped = body.left(kMaxStoredBodyBytes);
    capped += QStringLiteral("\n… [truncated: %1 bytes received] / "
                             "已截断：共收到 %1 字节")
                  .arg(total)
                  .toUtf8();
    return capped;
}

} // namespace

// ---------------------------------------------------------------------------
//  PendingTest -- one in-flight request
//  进行中的一次请求
// ---------------------------------------------------------------------------
struct BackendConnector::PendingTest
{
    QString requestId;
    QString endpointId;
    Endpoint endpoint;
    QNetworkRequest request;
    QByteArray body;
    QString method = QStringLiteral("GET");
    QUrl origin;
    QNetworkReply *reply = nullptr;
    QTimer *timeoutTimer = nullptr;
    QElapsedTimer elapsed;
    int timeoutMs = 30000;
    int redirects = 0;
    bool healthProbe = false;
    bool timedOut = false;
    bool settled = false;

    void dispatch(BackendConnector *owner);
};

void BackendConnector::PendingTest::dispatch(BackendConnector *owner)
{
    QNetworkAccessManager *nam = owner->network();
    const QByteArray verb = method.trimmed().toUpper().toUtf8();

    //  Every verb the endpoint UI offers must be reachable, including
    //  PUT/PATCH/DELETE with a body (hence sendCustomRequest).
    //  端点界面提供的所有方法都必须可达，包括携带正文的 PUT/PATCH/DELETE
    //  （因此使用 sendCustomRequest）。
    if (verb == QByteArrayLiteral("GET") && body.isEmpty())
        reply = nam->get(request);
    else if (verb == QByteArrayLiteral("HEAD"))
        reply = nam->head(request);
    else if (verb == QByteArrayLiteral("POST"))
        reply = nam->post(request, body);
    else if (verb == QByteArrayLiteral("PUT"))
        reply = nam->put(request, body);
    else if (verb == QByteArrayLiteral("DELETE") && body.isEmpty())
        reply = nam->deleteResource(request);
    else
        reply = nam->sendCustomRequest(request, verb, body);

    if (!reply)
        return;

    reply->setProperty("tcbwRequestId", requestId);
    QObject::connect(reply, &QNetworkReply::finished, owner,
                     &BackendConnector::onReplyFinished);

    if (!timeoutTimer) {
        timeoutTimer = new QTimer(owner);
        timeoutTimer->setSingleShot(true);
        QObject::connect(timeoutTimer, &QTimer::timeout, owner,
                         [owner, id = requestId]() {
            const auto it = owner->m_pending.find(id);
            if (it == owner->m_pending.end() || !(*it))
                return;
            PendingTest *pending = *it;
            pending->timedOut = true;
            //  abort() reaches the finished handler, which owns the cleanup.
            //  abort() 会走到 finished 处理器，清理工作由它负责。
            if (pending->reply)
                pending->reply->abort();
        });
    }
    timeoutTimer->start(timeoutMs);
    elapsed.start();
}

// ---------------------------------------------------------------------------
//  BackendConnector
//  后端连接器
// ---------------------------------------------------------------------------
BackendConnector::BackendConnector(QObject *parent)
    : QObject(parent)
{
}

BackendConnector::~BackendConnector()
{
    //  PendingTest objects are owned by this class, not by Qt.
    //  PendingTest 对象由本类拥有，而非 Qt。
    const QList<QString> ids = m_pending.keys();
    for (const QString &id : ids) {
        PendingTest *pending = m_pending.take(id);
        if (!pending)
            continue;
        if (pending->reply) {
            pending->reply->disconnect(this);
            pending->reply->abort();
            pending->reply->deleteLater();
        }
        if (pending->timeoutTimer) {
            pending->timeoutTimer->stop();
            pending->timeoutTimer->deleteLater();
        }
        delete pending;
    }
}

QNetworkAccessManager *BackendConnector::network()
{
    if (!m_network)
        m_network = std::make_unique<QNetworkAccessManager>();
    return m_network.get();
}

QNetworkRequest BackendConnector::buildRequest(const Endpoint &endpoint,
                                               const QMap<QString, QString> &variables,
                                               QByteArray *bodyOut, QString *error)
{
    if (error)
        error->clear();

    QNetworkRequest request;

    //  ---- URL -----------------------------------------------------------------
    const QString expandedUrl = EndpointStore::expandTemplate(endpoint.url, variables, true);
    QUrl url(expandedUrl);
    if (!url.isValid() || url.host().isEmpty()) {
        if (error) {
            *error = QStringLiteral("Endpoint URL \"%1\" is not a usable URL / "
                                    "端点 URL \"%1\" 不是可用的 URL")
                         .arg(expandedUrl);
        }
        return request;
    }

    QString method = endpoint.methodUpper();
    if (method.isEmpty())
        method = QStringLiteral("GET");
    if (endpoint.isGraphQl && (method == QStringLiteral("GET")))
        method = QStringLiteral("POST");

    //  ---- body ----------------------------------------------------------------
    QByteArray body;
    if (!endpoint.bodyTemplate.isEmpty()) {
        body = EndpointStore::expandTemplate(endpoint.bodyTemplate, variables, false).toUtf8();
    } else if (method == QStringLiteral("POST") || method == QStringLiteral("PUT")
               || method == QStringLiteral("PATCH")) {
        body = QByteArrayLiteral("{}");
    }

    //  ---- headers -------------------------------------------------------------
    if (!body.isEmpty() && !endpoint.contentType.trimmed().isEmpty())
        request.setHeader(QNetworkRequest::ContentTypeHeader, endpoint.contentType.trimmed());

    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("TC-BuildWeb/0.1.0-rc1"));
    request.setRawHeader(QByteArrayLiteral("Accept"),
                         QByteArrayLiteral("application/json, text/plain, */*"));

    for (auto it = endpoint.headers.constBegin(); it != endpoint.headers.constEnd(); ++it) {
        const QString name = sanitiseHeaderName(it.key());
        if (name.isEmpty())
            continue;
        const QString value = sanitiseHeaderValue(expandPlain(it.value(), variables));
        request.setRawHeader(name.toLatin1(), value.toUtf8());
    }

    //  ---- query ---------------------------------------------------------------
    QUrlQuery query(url);
    for (auto it = endpoint.query.constBegin(); it != endpoint.query.constEnd(); ++it) {
        const QString key = expandPlain(it.key(), variables).trimmed();
        if (key.isEmpty())
            continue;
        query.addQueryItem(key, expandPlain(it.value(), variables));
    }
    url.setQuery(query);
    request.setUrl(url);

    //  Redirects are resolved by hand so a cross-origin hop can be refused.
    //  重定向由本类手动处理，从而可以拒绝跨源跳转。
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);

    if (bodyOut)
        *bodyOut = body;
    return request;
}

QString BackendConnector::test(const Endpoint &endpoint,
                               const QMap<QString, QString> &variables,
                               const QString &requestId)
{
    const QString id = requestId.trimmed().isEmpty()
                           ? IdGenerator::next(QStringLiteral("req"))
                           : requestId.trimmed();

    emit testStarted(id, endpoint.id);

    //  A configuration failure never reaches the network; both signals are
    //  emitted synchronously so the caller's UI is never left waiting.
    //  配置错误不会走到网络；两个信号同步发出，调用方界面不会一直等待。
    const auto fail = [this, &id](const QString &en, const QString &zh) {
        EndpointTestResult result;
        result.ok = false;
        result.errorEn = en;
        result.errorZh = zh;
        emit testFinished(id, result);
    };

    QString why;
    if (!endpoint.isValid(&why)) {
        fail(why, why);
        return id;
    }

    QString buildError;
    QByteArray body;
    QNetworkRequest request = buildRequest(endpoint, variables, &body, &buildError);
    if (!buildError.isEmpty()) {
        fail(buildError, buildError);
        return id;
    }

    QString reasonEn;
    QString reasonZh;
    const QUrl url = request.url();
    if (!isSafeTestUrl(url, &reasonEn, &reasonZh)) {
        fail(reasonEn, reasonZh);
        return id;
    }

    const bool carriesCredential = endpoint.auth.enabled;
    if (!isLoopbackHost(url.host()) && isInsecureCredentialUrl(url, carriesCredential)) {
        //  Never send a secret over plaintext to a remote host.
        //  绝不通过明文把机密发送到远程主机。
        fail(QStringLiteral("Refusing to send a credential over plaintext http:// to %1 / "
                            "拒绝通过明文 http:// 向 %1 发送凭据")
                 .arg(url.host()),
             QStringLiteral("拒绝通过明文 http:// 向 %1 发送凭据").arg(url.host()));
        return id;
    }

    //  ---- credential -----------------------------------------------------------
    //  auth.secretKey is the KEY inside the SecretStore, never the secret.
    //  auth.secretKey 是 SecretStore 中的键，绝不是机密本身。
    if (carriesCredential) {
        const QString key = endpoint.auth.secretKey.trimmed();
        if (key.isEmpty()) {
            fail(QStringLiteral("Authentication is enabled but no secret key is configured / "
                                "已启用认证，但没有配置密钥键名"),
                 QStringLiteral("已启用认证，但没有配置密钥键名"));
            return id;
        }

        QString credential;
        if (m_auth)
            credential = m_auth->apiKey(key);
        if (credential.isEmpty())
            credential = SecretStore::instance().retrieve(key);

        if (credential.isEmpty()) {
            fail(QStringLiteral("No credential found for secret key \"%1\"; refusing to send an "
                                "authenticated request without one / "
                                "密钥键 \"%1\" 未找到凭据；拒绝在没有凭据的情况下发送"
                                "需要认证的请求")
                     .arg(key),
                 QStringLiteral("密钥键 \"%1\" 未找到凭据；拒绝在没有凭据的情况下发送"
                                "需要认证的请求")
                     .arg(key));
            return id;
        }

        //  Strip CR/LF so a stored secret can never inject a header.
        //  去除 CR/LF，使存储的机密永远无法注入头字段。
        credential.remove(QLatin1Char('\r'));
        credential.remove(QLatin1Char('\n'));

        const QString queryParameter = endpoint.auth.queryParameter.trimmed();
        if (!queryParameter.isEmpty()) {
            QUrl queryUrl = request.url();
            QUrlQuery query(queryUrl);
            query.addQueryItem(queryParameter, credential);
            queryUrl.setQuery(query);
            request.setUrl(queryUrl);
        } else {
            QString headerName = sanitiseHeaderName(endpoint.auth.headerName);
            if (headerName.isEmpty())
                headerName = QStringLiteral("Authorization");
            const QString value =
                sanitiseHeaderValue(endpoint.auth.schemePrefix + credential);
            request.setRawHeader(headerName.toLatin1(), value.toUtf8());
        }
    }

    //  ---- send -----------------------------------------------------------------
    auto *pending = new PendingTest;
    pending->requestId = id;
    pending->endpointId = endpoint.id;
    pending->endpoint = endpoint;
    pending->request = request;
    pending->body = body;
    pending->method = endpoint.methodUpper().isEmpty() ? QStringLiteral("GET")
                                                       : endpoint.methodUpper();
    if (endpoint.isGraphQl && pending->method == QStringLiteral("GET"))
        pending->method = QStringLiteral("POST");
    pending->origin = request.url();
    pending->timeoutMs = endpoint.timeoutMs;

    m_pending.insert(id, pending);
    pending->dispatch(this);
    return id;
}

void BackendConnector::cancel(const QString &requestId)
{
    PendingTest *pending = m_pending.take(requestId);
    if (!pending)
        return;

    const QString id = pending->requestId;
    const bool probe = pending->healthProbe;

    if (pending->reply) {
        pending->reply->disconnect(this);
        pending->reply->abort();
        pending->reply->deleteLater();
    }
    if (pending->timeoutTimer) {
        pending->timeoutTimer->stop();
        pending->timeoutTimer->deleteLater();
    }
    delete pending;

    if (probe) {
        emit healthProbeFinished(id, false,
                                 QStringLiteral("Health probe cancelled / 健康探测已取消"));
    } else {
        EndpointTestResult result;
        result.ok = false;
        result.errorEn = QStringLiteral("Request cancelled");
        result.errorZh = QStringLiteral("请求已取消");
        emit testFinished(id, result);
    }
}

void BackendConnector::cancelAll()
{
    const QList<QString> ids = m_pending.keys();
    for (const QString &id : ids)
        cancel(id);
}

QString BackendConnector::probeHealth(const BackendTarget &target, const QString &baseUrl,
                                      const QString &requestId)
{
    const QString id = requestId.trimmed().isEmpty()
                           ? IdGenerator::next(QStringLiteral("probe"))
                           : requestId.trimmed();

    QString base = baseUrl.trimmed();
    if (base.isEmpty())
        base = target.defaultBaseUrl.trimmed();
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);

    QString path = target.healthPath.trimmed();
    if (path.isEmpty())
        path = QStringLiteral("/health");
    if (!path.startsWith(QLatin1Char('/')))
        path.prepend(QLatin1Char('/'));

    const QUrl url(base + path);

    QString reasonEn;
    QString reasonZh;
    if (!isSafeTestUrl(url, &reasonEn, &reasonZh)) {
        emit healthProbeFinished(id, false, reasonEn + QStringLiteral(" / ") + reasonZh);
        return id;
    }

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("TC-BuildWeb/0.1.0-rc1"));
    request.setRawHeader(QByteArrayLiteral("Accept"), QByteArrayLiteral("application/json"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);

    auto *pending = new PendingTest;
    pending->requestId = id;
    pending->request = request;
    pending->method = QStringLiteral("GET");
    pending->origin = url;
    pending->timeoutMs = kHealthProbeTimeoutMs;
    pending->healthProbe = true;

    m_pending.insert(id, pending);
    pending->dispatch(this);
    return id;
}

void BackendConnector::onReplyFinished()
{
    auto *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply)
        return;

    const QString id = reply->property("tcbwRequestId").toString();
    const auto it = m_pending.find(id);
    if (it == m_pending.end() || (*it)->reply != reply) {
        reply->deleteLater();
        return;
    }

    PendingTest *pending = *it;
    if (pending->settled) {
        reply->deleteLater();
        return;
    }

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    EndpointTestResult result;
    result.httpStatus = status;
    result.elapsedMs = pending->elapsed.elapsed();

    QUrl redirectTarget;

    //  ---- redirects ------------------------------------------------------------
    const QVariant redirectAttr = reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
    if (redirectAttr.isValid() && status >= 300 && status < 400) {
        redirectTarget = redirectAttr.toUrl();
        if (redirectTarget.isRelative())
            redirectTarget = reply->url().resolved(redirectTarget);

        //  A redirect that leaves the origin is refused: following it would
        //  hand the credential (and the request body) to a third party.
        //  离开同源范围的重定向会被拒绝：跟随它等于把凭据（以及请求正文）
        //  交给第三方。
        if (isCrossOriginRedirect(pending->origin, redirectTarget)) {
            result.ok = false;
            result.responseHeaders = responseHeadersOf(reply);
            result.errorEn = QStringLiteral("Refused to follow cross-origin redirect from %1 to %2")
                                 .arg(pending->origin.toString(), redirectTarget.toString());
            result.errorZh = QStringLiteral("拒绝跟随跨源重定向：从 %1 到 %2")
                                 .arg(pending->origin.toString(), redirectTarget.toString());
        } else if (pending->redirects >= kMaxRedirects) {
            result.ok = false;
            result.responseHeaders = responseHeadersOf(reply);
            result.errorEn = QStringLiteral("Too many redirects (%1) for %2")
                                 .arg(pending->redirects)
                                 .arg(pending->origin.toString());
            result.errorZh = QStringLiteral("重定向次数过多（%1）：%2")
                                 .arg(pending->redirects)
                                 .arg(pending->origin.toString());
        } else {
            ++pending->redirects;
            pending->request.setUrl(redirectTarget);
            if (status == 303) {
                //  303 See Other always continues with a bodyless GET.
                //  303 See Other 一律以无正文的 GET 继续。
                pending->method = QStringLiteral("GET");
                pending->body.clear();
            }
            pending->reply = nullptr;
            reply->deleteLater();
            pending->dispatch(this);
            return;
        }
    }

    //  ---- payload --------------------------------------------------------------
    if (result.errorEn.isEmpty()) {
        result.responseHeaders = responseHeadersOf(reply);

        if (pending->timedOut) {
            result.ok = false;
            result.errorEn = QStringLiteral("Request timed out after %1 ms")
                                 .arg(pending->timeoutMs);
            result.errorZh = QStringLiteral("请求在 %1 毫秒后超时").arg(pending->timeoutMs);
        } else if (status <= 0) {
            result.ok = false;
            result.errorEn = reply->errorString();
            result.errorZh = QStringLiteral("网络错误：%1").arg(reply->errorString());
            result.body = capBody(reply->readAll());
        } else {
            result.body = capBody(reply->readAll());
            result.ok = status >= 200 && status < 300;
            if (!result.ok) {
                const QString reason =
                    reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
                result.errorEn = QStringLiteral("HTTP %1 %2").arg(status).arg(reason).trimmed();
                result.errorZh = QStringLiteral("HTTP %1 %2").arg(status).arg(reason).trimmed();
            }
        }
    } else {
        reply->readAll();   // drain so the socket can be reused / 排空以便复用连接
    }

    //  ---- settle ---------------------------------------------------------------
    //  Detach before emitting: a re-entrant cancel()/cancelAll() must not be able
    //  to free this object a second time.
    //  发信号前先摘除：可重入的 cancel()/cancelAll() 不得再次释放本对象。
    m_pending.erase(it);
    pending->settled = true;
    if (pending->timeoutTimer) {
        pending->timeoutTimer->stop();
        pending->timeoutTimer->deleteLater();
    }

    const bool probe = pending->healthProbe;
    const QString requestId = pending->requestId;
    delete pending;

    if (probe) {
        QString detail;
        if (result.ok) {
            detail = QStringLiteral("HTTP %1 · %2 ms").arg(result.httpStatus).arg(result.elapsedMs);
        } else {
            detail = result.errorEn;
            if (!result.errorZh.isEmpty() && result.errorZh != result.errorEn)
                detail += QStringLiteral(" / ") + result.errorZh;
        }
        emit healthProbeFinished(requestId, result.ok, detail);
    } else {
        emit testFinished(requestId, result);
    }

    //  The reply is always deleted here, in its own finished handler.
    //  响应对象始终在此处（即它自己的 finished 处理器中）被删除。
    reply->deleteLater();
}

// ---------------------------------------------------------------------------
//  Safety helpers
//  安全辅助函数
// ---------------------------------------------------------------------------
bool BackendConnector::isInsecureCredentialUrl(const QUrl &url, bool carriesCredential)
{
    if (!carriesCredential)
        return false;
    //  Plaintext http:// with a credential is insecure, full stop. Callers that
    //  legitimately talk to a loopback dev server apply that exemption
    //  themselves (see test()), so this predicate stays literal.
    //  携带凭据时的明文 http:// 一律不安全。需要访问回环开发服务器的调用方自行
    //  施加该豁免（见 test()），因此本判定保持字面语义。
    return url.scheme().compare(QStringLiteral("http"), Qt::CaseInsensitive) == 0;
}

bool BackendConnector::isCrossOriginRedirect(const QUrl &origin, const QUrl &redirect)
{
    if (!origin.isValid() || !redirect.isValid())
        return true;

    QUrl target = redirect;
    if (target.isRelative())
        target = origin.resolved(redirect);

    const QString originScheme = origin.scheme().toLower();
    const QString targetScheme = target.scheme().toLower();
    if (originScheme != targetScheme)
        return true;

    if (origin.host().compare(target.host(), Qt::CaseInsensitive) != 0)
        return true;

    //  Compare effective ports: the default port for the scheme counts as equal
    //  to an explicit one ("https://a/" == "https://a:443/").
    //  比较有效端口：协议默认端口与显式端口视为相同
    //  （"https://a/" 等于 "https://a:443/"）。
    const int originPort = origin.port(schemeDefaultPort(originScheme));
    const int targetPort = target.port(schemeDefaultPort(targetScheme));
    return originPort != targetPort;
}

bool BackendConnector::isSafeTestUrl(const QUrl &url, QString *reasonEn, QString *reasonZh)
{
    const auto fail = [reasonEn, reasonZh](const QString &en, const QString &zh) {
        if (reasonEn)
            *reasonEn = en;
        if (reasonZh)
            *reasonZh = zh;
        return false;
    };

    if (!url.isValid())
        return fail(QStringLiteral("URL is not valid"), QStringLiteral("URL 不合法"));

    const QString scheme = url.scheme().toLower();
    if (scheme.isEmpty())
        return fail(QStringLiteral("URL has no scheme"), QStringLiteral("URL 没有协议"));

    //  Only the two web schemes may be tested. file:, ftp: and data: are
    //  refused because they can read the local disk or smuggle inline payloads.
    //  只允许测试两种 Web 协议。拒绝 file:、ftp: 与 data:，因为它们可以读取
    //  本地磁盘或夹带内联载荷。
    if (scheme != QStringLiteral("http") && scheme != QStringLiteral("https")) {
        return fail(QStringLiteral("Only http and https URLs can be tested (got \"%1\")").arg(scheme),
                    QStringLiteral("只能测试 http 与 https 的 URL（当前为 \"%1\"）").arg(scheme));
    }

    if (url.host().trimmed().isEmpty())
        return fail(QStringLiteral("URL has no host"), QStringLiteral("URL 没有主机名"));

    //  Credentials embedded in the URL bypass the SecretStore entirely.
    //  URL 中内嵌的凭据会完全绕过 SecretStore。
    if (!url.userName().isEmpty() || !url.password().isEmpty() || !url.userInfo().isEmpty()) {
        return fail(QStringLiteral("URL must not embed credentials (user:pass@host)"),
                    QStringLiteral("URL 不得内嵌凭据（user:pass@host）"));
    }

    if (isCloudMetadataHost(url.host())) {
        return fail(QStringLiteral("Requests to the cloud metadata service (%1) are blocked")
                        .arg(url.host()),
                    QStringLiteral("发往云元数据服务（%1）的请求已被拦截").arg(url.host()));
    }

    if (reasonEn)
        reasonEn->clear();
    if (reasonZh)
        reasonZh->clear();
    return true;
}

// ---------------------------------------------------------------------------
//  Backend target catalogue
//  后端目标目录
// ---------------------------------------------------------------------------
const QVector<BackendTarget> &BackendConnector::targets()
{
    //  Built once, in a fixed order; `order` is ascending and starts at 1.
    //  只构建一次，顺序固定；`order` 从 1 开始递增。
    static const QVector<BackendTarget> table = [] {
        QVector<BackendTarget> list;

        const auto add = [&list](const QString &id, const QString &labelEn, const QString &labelZh,
                                 const QString &language, const QString &descEn,
                                 const QString &descZh, const QString &baseUrl,
                                 const QString &healthPath, const QString &docsUrl,
                                 const QStringList &provides, bool requiresAccount) {
            BackendTarget target;
            target.id = id;
            target.labelEn = labelEn;
            target.labelZh = labelZh;
            target.language = language;
            target.descriptionEn = descEn;
            target.descriptionZh = descZh;
            target.defaultBaseUrl = baseUrl;
            target.healthPath = healthPath;
            target.docsUrl = docsUrl;
            target.provides = provides;
            target.requiresAccount = requiresAccount;
            target.order = static_cast<int>(list.size()) + 1;
            list.append(target);
        };

        const QStringList db{QStringLiteral("database")};
        const QStringList auth{QStringLiteral("auth")};
        const QStringList storeQ{QStringLiteral("storage")};
        const QStringList rt{QStringLiteral("realtime")};
        const QStringList fn{QStringLiteral("functions")};

        add(QStringLiteral("node-express"), QStringLiteral("Node.js + Express"),
            QStringLiteral("Node.js + Express"), QStringLiteral("javascript"),
            QStringLiteral("Minimal Express server with one route per endpoint and CORS."),
            QStringLiteral("极简 Express 服务器，每个端点一条路由，并支持 CORS。"),
            QStringLiteral("http://127.0.0.1:3000"), QStringLiteral("/health"),
            QStringLiteral("https://expressjs.com/"), auth + fn, false);

        add(QStringLiteral("node-fastify"), QStringLiteral("Node.js + Fastify"),
            QStringLiteral("Node.js + Fastify"), QStringLiteral("javascript"),
            QStringLiteral("Fastify server with schema-friendly routing and JSON logging."),
            QStringLiteral("Fastify 服务器，路由友好并带 JSON 日志。"),
            QStringLiteral("http://127.0.0.1:3000"), QStringLiteral("/health"),
            QStringLiteral("https://fastify.dev/docs/latest/"), auth + fn, false);

        add(QStringLiteral("python-fastapi"), QStringLiteral("Python + FastAPI"),
            QStringLiteral("Python + FastAPI"), QStringLiteral("python"),
            QStringLiteral("FastAPI app with async routes, CORS and automatic OpenAPI docs."),
            QStringLiteral("FastAPI 应用，异步路由、CORS，并自动生成 OpenAPI 文档。"),
            QStringLiteral("http://127.0.0.1:8000"), QStringLiteral("/health"),
            QStringLiteral("https://fastapi.tiangolo.com/"), auth + fn, false);

        add(QStringLiteral("python-flask"), QStringLiteral("Python + Flask"),
            QStringLiteral("Python + Flask"), QStringLiteral("python"),
            QStringLiteral("Small Flask app that mirrors the endpoints with plain JSON views."),
            QStringLiteral("小型 Flask 应用，用纯 JSON 视图镜像这些端点。"),
            QStringLiteral("http://127.0.0.1:5000"), QStringLiteral("/health"),
            QStringLiteral("https://flask.palletsprojects.com/"), auth + fn, false);

        add(QStringLiteral("go-nethttp"), QStringLiteral("Go net/http"),
            QStringLiteral("Go net/http"), QStringLiteral("go"),
            QStringLiteral("Dependency-free Go server using the standard ServeMux router."),
            QStringLiteral("零依赖 Go 服务器，使用标准库 ServeMux 路由。"),
            QStringLiteral("http://127.0.0.1:8080"), QStringLiteral("/health"),
            QStringLiteral("https://pkg.go.dev/net/http"), auth + fn, false);

        add(QStringLiteral("rust-axum"), QStringLiteral("Rust + Axum"),
            QStringLiteral("Rust + Axum"), QStringLiteral("rust"),
            QStringLiteral("Tokio/Axum service with typed extractors and tower middleware."),
            QStringLiteral("Tokio/Axum 服务，带类型化提取器与 tower 中间件。"),
            QStringLiteral("http://127.0.0.1:3000"), QStringLiteral("/health"),
            QStringLiteral("https://docs.rs/axum/latest/axum/"), auth + fn, false);

        add(QStringLiteral("php-laravel"), QStringLiteral("PHP + Laravel"),
            QStringLiteral("PHP + Laravel"), QStringLiteral("php"),
            QStringLiteral("Laravel routes file plus a controller that returns JSON."),
            QStringLiteral("Laravel 路由文件与返回 JSON 的控制器。"),
            QStringLiteral("http://127.0.0.1:8000"), QStringLiteral("/health"),
            QStringLiteral("https://laravel.com/docs"), auth + db + fn, false);

        add(QStringLiteral("java-springboot"), QStringLiteral("Java + Spring Boot"),
            QStringLiteral("Java + Spring Boot"), QStringLiteral("java"),
            QStringLiteral("Spring Boot REST controller annotated per endpoint."),
            QStringLiteral("按端点注解的 Spring Boot REST 控制器。"),
            QStringLiteral("http://127.0.0.1:8080"), QStringLiteral("/health"),
            QStringLiteral("https://spring.io/projects/spring-boot"), auth + db + fn, false);

        add(QStringLiteral("dotnet-minimalapi"), QStringLiteral(".NET Minimal API"),
            QStringLiteral(".NET 最小 API"), QStringLiteral("csharp"),
            QStringLiteral("ASP.NET Core minimal API with MapGet/MapPost per endpoint."),
            QStringLiteral("ASP.NET Core 最小 API，每个端点对应 MapGet/MapPost。"),
            QStringLiteral("http://127.0.0.1:5000"), QStringLiteral("/health"),
            QStringLiteral("https://learn.microsoft.com/aspnet/core/fundamentals/minimal-apis"),
            auth + fn, false);

        add(QStringLiteral("supabase"), QStringLiteral("Supabase"),
            QStringLiteral("Supabase"), QStringLiteral("typescript"),
            QStringLiteral("Hosted Postgres, auth, storage and edge functions."),
            QStringLiteral("托管 Postgres、认证、存储与边缘函数。"),
            QStringLiteral("http://127.0.0.1:54321"), QStringLiteral("/auth/v1/health"),
            QStringLiteral("https://supabase.com/docs"), auth + db + storeQ + rt + fn, true);

        add(QStringLiteral("firebase-functions"), QStringLiteral("Firebase Functions"),
            QStringLiteral("Firebase Functions"), QStringLiteral("javascript"),
            QStringLiteral("Google Cloud Functions with Firestore, auth and hosting."),
            QStringLiteral("Google Cloud Functions，含 Firestore、认证与托管。"),
            QStringLiteral("http://127.0.0.1:5001"), QStringLiteral("/health"),
            QStringLiteral("https://firebase.google.com/docs/functions"),
            auth + db + storeQ + fn, true);

        add(QStringLiteral("cloudflare-workers"), QStringLiteral("Cloudflare Workers"),
            QStringLiteral("Cloudflare Workers"), QStringLiteral("javascript"),
            QStringLiteral("V8-isolate worker with KV, D1 and R2 bindings."),
            QStringLiteral("V8 隔离的 Worker，可绑定 KV、D1 与 R2。"),
            QStringLiteral("http://127.0.0.1:8787"), QStringLiteral("/health"),
            QStringLiteral("https://developers.cloudflare.com/workers/"),
            auth + db + storeQ + fn, true);

        add(QStringLiteral("deno-deploy"), QStringLiteral("Deno Deploy"),
            QStringLiteral("Deno Deploy"), QStringLiteral("typescript"),
            QStringLiteral("Deno server with native TypeScript and Deno KV."),
            QStringLiteral("Deno 服务器，原生 TypeScript 与 Deno KV。"),
            QStringLiteral("http://127.0.0.1:8000"), QStringLiteral("/health"),
            QStringLiteral("https://docs.deno.com/deploy/manual/"), auth + db + fn, true);

        add(QStringLiteral("netlify-functions"), QStringLiteral("Netlify Functions"),
            QStringLiteral("Netlify Functions"), QStringLiteral("javascript"),
            QStringLiteral("AWS Lambda-compatible handlers behind Netlify redirects."),
            QStringLiteral("通过 Netlify 重定向暴露的、兼容 AWS Lambda 的处理函数。"),
            QStringLiteral("http://127.0.0.1:8888"), QStringLiteral("/health"),
            QStringLiteral("https://docs.netlify.com/functions/overview/"), auth + fn, true);

        add(QStringLiteral("vercel-functions"), QStringLiteral("Vercel Functions"),
            QStringLiteral("Vercel Functions"), QStringLiteral("javascript"),
            QStringLiteral("Serverless functions with edge runtime and Vercel storage."),
            QStringLiteral("无服务器函数，支持边缘运行时与 Vercel 存储。"),
            QStringLiteral("http://127.0.0.1:3000"), QStringLiteral("/health"),
            QStringLiteral("https://vercel.com/docs/functions"), auth + db + storeQ + fn, true);

        add(QStringLiteral("static-json"), QStringLiteral("Static JSON (json-server style)"),
            QStringLiteral("静态 JSON（json-server 风格）"), QStringLiteral("javascript"),
            QStringLiteral("Zero-dependency Node server over a db.json file."),
            QStringLiteral("基于 db.json 的零依赖 Node 服务器。"),
            QStringLiteral("http://127.0.0.1:3001"), QStringLiteral("/health"),
            QStringLiteral("https://github.com/typicode/json-server"), db + fn, false);

        add(QStringLiteral("pocketbase"), QStringLiteral("PocketBase"),
            QStringLiteral("PocketBase"), QStringLiteral("go"),
            QStringLiteral("Single-file Go backend: SQLite, auth, files and realtime."),
            QStringLiteral("单文件 Go 后端：SQLite、认证、文件与实时订阅。"),
            QStringLiteral("http://127.0.0.1:8090"), QStringLiteral("/api/health"),
            QStringLiteral("https://pocketbase.io/docs/"), auth + db + storeQ + rt, false);

        add(QStringLiteral("appwrite"), QStringLiteral("Appwrite"),
            QStringLiteral("Appwrite"), QStringLiteral("typescript"),
            QStringLiteral("Self-hostable backend with accounts, databases and storage."),
            QStringLiteral("可自托管的后端，含账户、数据库与存储。"),
            QStringLiteral("http://127.0.0.1:8080/v1"), QStringLiteral("/health"),
            QStringLiteral("https://appwrite.io/docs"), auth + db + storeQ + rt + fn, false);

        add(QStringLiteral("nhost"), QStringLiteral("Nhost (Hasura)"),
            QStringLiteral("Nhost（Hasura）"), QStringLiteral("sql"),
            QStringLiteral("Postgres + Hasura GraphQL with auth, storage and functions."),
            QStringLiteral("Postgres + Hasura GraphQL，含认证、存储与函数。"),
            QStringLiteral("http://127.0.0.1:1337/v1"), QStringLiteral("/healthz"),
            QStringLiteral("https://docs.nhost.io/"), auth + db + storeQ + rt + fn, false);

        add(QStringLiteral("directus"), QStringLiteral("Directus"),
            QStringLiteral("Directus"), QStringLiteral("javascript"),
            QStringLiteral("Data platform that wraps any SQL database with a REST API."),
            QStringLiteral("为任意 SQL 数据库提供 REST API 的数据平台。"),
            QStringLiteral("http://127.0.0.1:8055"), QStringLiteral("/server/health"),
            QStringLiteral("https://directus.io/docs/"), auth + db + storeQ + fn, false);

        return list;
    }();
    return table;
}

const BackendTarget *BackendConnector::findTarget(const QString &id)
{
    const QVector<BackendTarget> &list = targets();
    for (const BackendTarget &target : list) {
        if (target.id == id)
            return &target;
    }
    return nullptr;
}

} // namespace tcbw
