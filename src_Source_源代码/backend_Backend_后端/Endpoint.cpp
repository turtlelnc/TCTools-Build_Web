// ============================================================================
//  Endpoint.cpp -- pre-linked backend / server endpoints
//  Endpoint.cpp —— 预链接的后端 / 服务器端点
//  Part of TCTools-Build_Web (TC-BuildWeb) v0.1.0-rc1
// ============================================================================
#include "Endpoint.h"

#include "IdGenerator.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QStringList>
#include <QUrl>

namespace tcbw {

namespace {

//  Minimum / maximum acceptable request timeout.
//  可接受的最小 / 最大请求超时时间。
constexpr int kMinTimeoutMs = 1000;
constexpr int kMaxTimeoutMs = 300000;

//  Schemes a pre-linked endpoint may use.
//  预链接端点可以使用的协议。
bool isAllowedScheme(const QString &scheme)
{
    const QString s = scheme.trimmed().toLower();
    return s == QStringLiteral("http") || s == QStringLiteral("https")
        || s == QStringLiteral("ws") || s == QStringLiteral("wss");
}

//  True when the raw URL text embeds credentials ("user:pass@host").
//  当原始 URL 文本内嵌了凭据（"user:pass@host"）时返回 true。
bool hasEmbeddedCredentials(const QString &raw)
{
    const int schemeEnd = raw.indexOf(QStringLiteral("://"));
    if (schemeEnd < 0)
        return false;

    const int authorityStart = schemeEnd + 3;
    int authorityEnd = raw.size();
    for (int i = authorityStart; i < raw.size(); ++i) {
        const QChar c = raw.at(i);
        if (c == QLatin1Char('/') || c == QLatin1Char('?') || c == QLatin1Char('#')) {
            authorityEnd = i;
            break;
        }
    }
    return raw.mid(authorityStart, authorityEnd - authorityStart).contains(QLatin1Char('@'));
}

//  True when `host` is a loopback literal (localhost, 127.0.0.0/8, ::1).
//  当 `host` 是回环字面量（localhost、127.0.0.0/8、::1）时返回 true。
bool isLoopbackLiteralHost(const QString &host)
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

//  JSON string escaping for values substituted into a JSON body template.
//  用于替换进 JSON 正文模板的值的 JSON 字符串转义。
QString jsonEscapeForBody(const QString &value)
{
    QString out;
    out.reserve(value.size() + 8);
    for (const QChar ch : value) {
        const char16_t unit = ch.unicode();
        switch (unit) {
        case u'"':  out += QStringLiteral("\\\""); break;
        case u'\\': out += QStringLiteral("\\\\"); break;
        case u'\b': out += QStringLiteral("\\b");  break;
        case u'\f': out += QStringLiteral("\\f");  break;
        case u'\n': out += QStringLiteral("\\n");  break;
        case u'\r': out += QStringLiteral("\\r");  break;
        case u'\t': out += QStringLiteral("\\t");  break;
        default:
            if (unit < 0x20 || unit == 0x2028 || unit == 0x2029) {
                //  Control characters and the JS line separators must never
                //  appear raw inside a generated JSON document.
                //  控制字符与 JS 行分隔符绝不能原样出现在生成的 JSON 文档中。
                out += QStringLiteral("\\u%1").arg(static_cast<uint>(unit), 4, 16,
                                                   QLatin1Char('0'));
            } else {
                out += ch;
            }
            break;
        }
    }
    return out;
}

QJsonObject stringMapToJson(const QMap<QString, QString> &map)
{
    QJsonObject obj;
    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
        obj.insert(it.key(), it.value());
    return obj;
}

QMap<QString, QString> stringMapFromJson(const QJsonObject &obj)
{
    QMap<QString, QString> map;
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
        map.insert(it.key(), it.value().toString());
    return map;
}

QJsonArray stringListToJson(const QStringList &list)
{
    QJsonArray arr;
    for (const QString &entry : list)
        arr.append(entry);
    return arr;
}

QStringList stringListFromJson(const QJsonArray &arr)
{
    QStringList list;
    for (const QJsonValue &value : arr) {
        const QString text = value.toString();
        if (!text.isEmpty())
            list.append(text);
    }
    return list;
}

//  Regenerates a missing id, preserving any id the caller already supplied.
//  为缺失的 id 重新生成，保留调用方已提供的 id。
void ensureId(Endpoint &endpoint)
{
    if (endpoint.id.trimmed().isEmpty())
        endpoint.id = IdGenerator::next(QStringLiteral("ep"));
}

} // namespace

// ---------------------------------------------------------------------------
//  EndpointAuth
//  端点授权
// ---------------------------------------------------------------------------
QJsonObject EndpointAuth::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("mode"), toString(mode));
    obj.insert(QStringLiteral("headerName"), headerName);
    obj.insert(QStringLiteral("schemePrefix"), schemePrefix);
    //  Only the SecretStore KEY is ever serialised -- never the secret itself.
    //  只序列化 SecretStore 的键 —— 绝不序列化机密本身。
    obj.insert(QStringLiteral("secretKey"), secretKey);
    obj.insert(QStringLiteral("queryParameter"), queryParameter);
    obj.insert(QStringLiteral("enabled"), enabled);
    return obj;
}

EndpointAuth EndpointAuth::fromJson(const QJsonObject &obj)
{
    EndpointAuth auth;
    auth.mode = authModeFromString(obj.value(QStringLiteral("mode")).toString(),
                                   AuthMode::None);
    auth.headerName = obj.value(QStringLiteral("headerName"))
                          .toString(QStringLiteral("Authorization"));
    auth.schemePrefix = obj.value(QStringLiteral("schemePrefix")).toString();
    auth.secretKey = obj.value(QStringLiteral("secretKey")).toString();
    auth.queryParameter = obj.value(QStringLiteral("queryParameter")).toString();
    auth.enabled = obj.value(QStringLiteral("enabled")).toBool(false);
    return auth;
}

// ---------------------------------------------------------------------------
//  Endpoint
//  端点
// ---------------------------------------------------------------------------
bool Endpoint::isValid(QString *why) const
{
    const auto fail = [why](const QString &message) {
        if (why)
            *why = message;
        return false;
    };

    const QString raw = url.trimmed();
    if (raw.isEmpty())
        return fail(QStringLiteral("Endpoint URL is empty / 端点 URL 为空"));

    if (hasEmbeddedCredentials(raw)) {
        return fail(QStringLiteral(
            "Endpoint URL must not embed credentials (user:pass@host) / "
            "端点 URL 不得内嵌凭据（user:pass@host）"));
    }

    const QUrl parsed(raw);
    if (!parsed.isValid())
        return fail(QStringLiteral("Endpoint URL is not a valid URL / 端点 URL 不是合法的 URL"));

    const QString scheme = parsed.scheme().toLower();
    if (!isAllowedScheme(scheme)) {
        return fail(QStringLiteral("Unsupported URL scheme \"%1\" (allowed: http, https, ws, wss) / "
                                   "不支持的 URL 协议 \"%1\"（允许：http、https、ws、wss）")
                        .arg(scheme));
    }

    if (parsed.host().trimmed().isEmpty()) {
        return fail(QStringLiteral("Endpoint URL has no host / 端点 URL 没有主机名"));
    }

    //  THE PLAINTEXT-SECRET RULE / 明文机密规则
    //  A secret must never travel over plaintext: when authentication is
    //  enabled the endpoint has to use https (wss for sockets). The single
    //  exception is a loopback literal, where the traffic never leaves the
    //  machine and local development would otherwise be impossible.
    //  机密绝不能通过明文传输：启用认证时端点必须使用 https（套接字用 wss）。
    //  唯一的例外是回环字面量，其流量不会离开本机，否则本地开发将无法进行。
    if (auth.enabled && !isLoopbackLiteralHost(parsed.host())) {
        if (scheme == QStringLiteral("http") || scheme == QStringLiteral("ws")) {
            return fail(QStringLiteral(
                "https (or wss) is required when a credential is attached; "
                "plaintext %1 is refused except on loopback hosts / "
                "附加凭据时必须使用 https（或 wss）；除回环主机外拒绝明文 %1")
                            .arg(scheme));
        }
    }

    if (timeoutMs < kMinTimeoutMs || timeoutMs > kMaxTimeoutMs) {
        return fail(QStringLiteral("timeoutMs must be between %1 and %2 ms (got %3) / "
                                   "timeoutMs 必须在 %1 到 %2 毫秒之间（当前为 %3）")
                        .arg(kMinTimeoutMs)
                        .arg(kMaxTimeoutMs)
                        .arg(timeoutMs));
    }

    if (isGraphQl && bodyTemplate.trimmed().isEmpty()) {
        return fail(QStringLiteral(
            "GraphQL endpoints need a non-empty bodyTemplate / "
            "GraphQL 端点需要非空的 bodyTemplate"));
    }

    if (why)
        why->clear();
    return true;
}

QJsonObject Endpoint::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), id);
    obj.insert(QStringLiteral("name"), name);
    obj.insert(QStringLiteral("descriptionEn"), descriptionEn);
    obj.insert(QStringLiteral("descriptionZh"), descriptionZh);
    obj.insert(QStringLiteral("method"), methodUpper());
    obj.insert(QStringLiteral("url"), url);
    obj.insert(QStringLiteral("contentType"), contentType);
    obj.insert(QStringLiteral("bodyTemplate"), bodyTemplate);
    obj.insert(QStringLiteral("headers"), stringMapToJson(headers));
    obj.insert(QStringLiteral("query"), stringMapToJson(query));
    obj.insert(QStringLiteral("auth"), auth.toJson());
    obj.insert(QStringLiteral("timeoutMs"), timeoutMs);
    obj.insert(QStringLiteral("enabled"), enabled);
    obj.insert(QStringLiteral("isGraphQl"), isGraphQl);
    obj.insert(QStringLiteral("boundBlockTypes"), stringListToJson(boundBlockTypes));
    obj.insert(QStringLiteral("boundPageIds"), stringListToJson(boundPageIds));
    obj.insert(QStringLiteral("exampleVariables"), stringListToJson(exampleVariables));
    return obj;
}

Endpoint Endpoint::fromJson(const QJsonObject &obj)
{
    Endpoint endpoint;
    endpoint.id = obj.value(QStringLiteral("id")).toString();
    endpoint.name = obj.value(QStringLiteral("name")).toString();
    endpoint.descriptionEn = obj.value(QStringLiteral("descriptionEn")).toString();
    endpoint.descriptionZh = obj.value(QStringLiteral("descriptionZh")).toString();
    const QString method = obj.value(QStringLiteral("method")).toString().trimmed().toUpper();
    endpoint.method = method.isEmpty() ? QStringLiteral("GET") : method;
    endpoint.url = obj.value(QStringLiteral("url")).toString();
    endpoint.contentType = obj.value(QStringLiteral("contentType"))
                               .toString(QStringLiteral("application/json"));
    endpoint.bodyTemplate = obj.value(QStringLiteral("bodyTemplate")).toString();
    endpoint.headers = stringMapFromJson(obj.value(QStringLiteral("headers")).toObject());
    endpoint.query = stringMapFromJson(obj.value(QStringLiteral("query")).toObject());
    endpoint.auth = EndpointAuth::fromJson(obj.value(QStringLiteral("auth")).toObject());
    endpoint.timeoutMs = obj.value(QStringLiteral("timeoutMs")).toInt(30000);
    endpoint.enabled = obj.value(QStringLiteral("enabled")).toBool(true);
    endpoint.isGraphQl = obj.value(QStringLiteral("isGraphQl")).toBool(false);
    endpoint.boundBlockTypes =
        stringListFromJson(obj.value(QStringLiteral("boundBlockTypes")).toArray());
    endpoint.boundPageIds =
        stringListFromJson(obj.value(QStringLiteral("boundPageIds")).toArray());
    endpoint.exampleVariables =
        stringListFromJson(obj.value(QStringLiteral("exampleVariables")).toArray());
    ensureId(endpoint);
    return endpoint;
}

// ---------------------------------------------------------------------------
//  EndpointTestResult
//  端点测试结果
// ---------------------------------------------------------------------------
QString EndpointTestResult::bodyPreview(int maxChars) const
{
    if (maxChars <= 0)
        maxChars = 4096;

    const QString text = QString::fromUtf8(body);

    //  Strip control characters so a hostile response cannot rewrite the log.
    //  去除控制字符，使恶意响应无法篡改日志。
    QString cleaned;
    cleaned.reserve(text.size());
    for (const QChar ch : text) {
        const char16_t unit = ch.unicode();
        if (unit < 0x20 && unit != u'\n' && unit != u'\r' && unit != u'\t')
            continue;
        cleaned += ch;
    }

    if (cleaned.size() <= maxChars)
        return cleaned;
    return cleaned.left(maxChars)
         + QStringLiteral("\n… truncated (%1 characters total) / 已截断（共 %1 个字符）")
               .arg(cleaned.size());
}

// ---------------------------------------------------------------------------
//  BackendTarget
//  后端目标
// ---------------------------------------------------------------------------
QJsonObject BackendTarget::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), id);
    obj.insert(QStringLiteral("labelEn"), labelEn);
    obj.insert(QStringLiteral("labelZh"), labelZh);
    obj.insert(QStringLiteral("language"), language);
    obj.insert(QStringLiteral("descriptionEn"), descriptionEn);
    obj.insert(QStringLiteral("descriptionZh"), descriptionZh);
    obj.insert(QStringLiteral("defaultBaseUrl"), defaultBaseUrl);
    obj.insert(QStringLiteral("healthPath"), healthPath);
    obj.insert(QStringLiteral("docsUrl"), docsUrl);
    obj.insert(QStringLiteral("provides"), stringListToJson(provides));
    obj.insert(QStringLiteral("requiresAccount"), requiresAccount);
    obj.insert(QStringLiteral("order"), order);
    return obj;
}

// ---------------------------------------------------------------------------
//  EndpointStore
//  端点存储
// ---------------------------------------------------------------------------
Endpoint *EndpointStore::find(const QString &id)
{
    if (id.isEmpty())
        return nullptr;
    for (Endpoint &endpoint : m_endpoints) {
        if (endpoint.id == id)
            return &endpoint;
    }
    return nullptr;
}

const Endpoint *EndpointStore::find(const QString &id) const
{
    if (id.isEmpty())
        return nullptr;
    for (const Endpoint &endpoint : m_endpoints) {
        if (endpoint.id == id)
            return &endpoint;
    }
    return nullptr;
}

Endpoint *EndpointStore::findByName(const QString &name)
{
    if (name.isEmpty())
        return nullptr;
    for (Endpoint &endpoint : m_endpoints) {
        if (endpoint.name == name)
            return &endpoint;
    }
    return nullptr;
}

Endpoint &EndpointStore::add(const Endpoint &endpoint)
{
    Endpoint copy = endpoint;
    ensureId(copy);
    m_endpoints.append(copy);
    return m_endpoints.last();
}

Endpoint &EndpointStore::insert(const Endpoint &endpoint, int index)
{
    Endpoint copy = endpoint;
    ensureId(copy);

    if (index < 0)
        index = 0;
    if (index > m_endpoints.size())
        index = static_cast<int>(m_endpoints.size());
    m_endpoints.insert(index, copy);
    return m_endpoints[index];
}

bool EndpointStore::remove(const QString &id)
{
    for (int i = 0; i < m_endpoints.size(); ++i) {
        if (m_endpoints.at(i).id == id) {
            m_endpoints.remove(i);
            return true;
        }
    }
    return false;
}

bool EndpointStore::update(const Endpoint &endpoint)
{
    if (endpoint.id.isEmpty())
        return false;
    for (Endpoint &existing : m_endpoints) {
        if (existing.id == endpoint.id) {
            existing = endpoint;
            return true;
        }
    }
    return false;
}

void EndpointStore::clear()
{
    m_endpoints.clear();
}

Endpoint &EndpointStore::move(int from, int to)
{
    //  The frozen signature returns a reference, so an empty store needs a
    //  stable object to return; callers can detect the failure with count().
    //  该签名（已冻结）返回引用，因此空存储需要一个稳定的返回对象；
    //  调用方可以通过 count() 判断是否失败。
    static Endpoint fallback;

    if (m_endpoints.isEmpty()) {
        fallback = Endpoint();
        return fallback;
    }

    if (from < 0)
        from = 0;
    if (from >= m_endpoints.size())
        from = static_cast<int>(m_endpoints.size()) - 1;
    if (to < 0)
        to = 0;
    if (to >= m_endpoints.size())
        to = static_cast<int>(m_endpoints.size()) - 1;
    if (from == to)
        return m_endpoints[from];

    m_endpoints.move(from, to);
    return m_endpoints[to];
}

QString EndpointStore::expandTemplate(const QString &templ,
                                      const QMap<QString, QString> &variables,
                                      bool forUrl)
{
    QString out;
    out.reserve(templ.size());

    int cursor = 0;
    const int length = templ.size();
    while (cursor < length) {
        const int open = templ.indexOf(QStringLiteral("{{"), cursor);
        if (open < 0) {
            out += templ.mid(cursor);
            break;
        }
        out += templ.mid(cursor, open - cursor);

        const int close = templ.indexOf(QStringLiteral("}}"), open + 2);
        if (close < 0) {
            //  An unterminated placeholder is copied verbatim.
            //  未闭合的占位符原样复制。
            out += templ.mid(open);
            break;
        }

        const QString name = templ.mid(open + 2, close - open - 2).trimmed();
        const auto it = variables.constFind(name);
        if (it == variables.constEnd()) {
            //  Unknown placeholders are preserved verbatim.
            //  未知占位符原样保留。
            out += templ.mid(open, close + 2 - open);
        } else if (forUrl) {
            out += QString::fromLatin1(QUrl::toPercentEncoding(it.value()));
        } else {
            out += jsonEscapeForBody(it.value());
        }

        //  SINGLE PASS / 单遍替换
        //  The scan resumes after the original placeholder, so a substituted
        //  value can never inject "{{"/"}}" that a later pass would expand.
        //  扫描从原占位符之后继续，因此被替换的值无法注入 "{{"/"}}" 让后续
        //  替换再次展开。
        cursor = close + 2;
    }
    return out;
}

QVector<const Endpoint *> EndpointStore::referencingBlock(const QString &blockType) const
{
    QVector<const Endpoint *> result;
    if (blockType.isEmpty())
        return result;
    for (const Endpoint &endpoint : m_endpoints) {
        if (endpoint.boundBlockTypes.contains(blockType))
            result.append(&endpoint);
    }
    return result;
}

QJsonObject EndpointStore::toJson() const
{
    QJsonObject obj;
    QJsonArray array;
    for (const Endpoint &endpoint : m_endpoints)
        array.append(endpoint.toJson());
    obj.insert(QStringLiteral("version"), 1);
    obj.insert(QStringLiteral("endpoints"), array);
    return obj;
}

EndpointStore EndpointStore::fromJson(const QJsonObject &obj)
{
    EndpointStore store;

    //  Tolerate both the wrapped form and a bare object holding one endpoint.
    //  同时兼容包装形式与仅包含单个端点的裸对象。
    QJsonArray array = obj.value(QStringLiteral("endpoints")).toArray();
    if (array.isEmpty() && obj.contains(QStringLiteral("url")))
        array.append(obj);

    for (const QJsonValue &value : array) {
        if (!value.isObject())
            continue;
        store.add(Endpoint::fromJson(value.toObject()));
    }
    return store;
}

} // namespace tcbw
