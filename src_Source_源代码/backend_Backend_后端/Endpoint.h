// ============================================================================
//  Endpoint.h -- pre-linked backend / server endpoints
//  Endpoint.h —— 预链接的后端 / 服务器端点
// ============================================================================
#pragma once

#include "AiTypes.h"
#include "Types.h"

#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

namespace tcbw {

// ---------------------------------------------------------------------------
//  EndpointAuth
//  How a request to this endpoint is authorised.
//  该端点的请求如何授权。
// ---------------------------------------------------------------------------
struct EndpointAuth
{
    AuthMode mode = AuthMode::None;
    QString headerName = QStringLiteral("Authorization");
    QString schemePrefix;                 // e.g. "Bearer " / 例如 "Bearer "
    QString secretKey;                    // SecretStore key, never the secret
    QString queryParameter;               // when the credential goes in the URL
    bool enabled = false;

    QJsonObject toJson() const;
    static EndpointAuth fromJson(const QJsonObject &obj);
};

// ---------------------------------------------------------------------------
//  Endpoint
//  One REST/GraphQL/WebSocket target the generated page can call.
//  生成的页面可调用的一个 REST/GraphQL/WebSocket 目标。
// ---------------------------------------------------------------------------
struct Endpoint
{
    QString id;
    QString name;
    QString descriptionEn;
    QString descriptionZh;
    QString method = QStringLiteral("GET");     // GET POST PUT PATCH DELETE / GraphQL
    QString url;                               // supports {{variable}} / 支持 {{变量}}
    QString contentType = QStringLiteral("application/json");
    QString bodyTemplate;                      // JSON with {{vars}} / 含 {{变量}} 的 JSON
    QMap<QString, QString> headers;
    QMap<QString, QString> query;
    EndpointAuth auth;
    int timeoutMs = 30000;
    bool enabled = true;
    bool isGraphQl = false;

    //  Bindings to the visual layer.
    //  与可视化层的绑定关系。
    QStringList boundBlockTypes;    // blocks that may reference it / 可引用它的积木
    QStringList boundPageIds;       // empty == all pages / 为空表示全部页面
    QStringList exampleVariables;   // "name=value" pairs for tests / 测试用变量

    bool isValid(QString *why = nullptr) const;
    QString methodUpper() const { return method.trimmed().toUpper(); }

    QJsonObject toJson() const;
    static Endpoint fromJson(const QJsonObject &obj);
};

// ---------------------------------------------------------------------------
//  EndpointTestResult
//  端点测试结果
// ---------------------------------------------------------------------------
struct EndpointTestResult
{
    bool ok = false;
    int httpStatus = 0;
    qint64 elapsedMs = 0;
    QByteArray body;
    QMap<QString, QString> responseHeaders;
    QString errorEn;
    QString errorZh;

    QString bodyPreview(int maxChars = 4096) const;
};

// ---------------------------------------------------------------------------
//  BackendTarget -- a server platform the user can pre-link to
//  后端目标 —— 用户可以预链接的服务器平台
// ---------------------------------------------------------------------------
struct BackendTarget
{
    QString id;                   // "node-express", "python-fastapi", "supabase", ...
    QString labelEn;
    QString labelZh;
    QString language;             // "javascript", "python", "go", "sql"
    QString descriptionEn;
    QString descriptionZh;
    QString defaultBaseUrl;       // local dev URL / 本地开发地址
    QString healthPath = QStringLiteral("/health");
    QString docsUrl;
    QStringList provides;         // "auth", "database", "storage", "realtime"
    bool requiresAccount = false;
    int order = 0;
    QJsonObject toJson() const;
};

// ---------------------------------------------------------------------------
//  EndpointStore -- the collection held by a project
//  端点存储 —— 工程持有的端点集合
// ---------------------------------------------------------------------------
class EndpointStore
{
public:
    EndpointStore() = default;

    int count() const { return static_cast<int>(m_endpoints.size()); }
    const QVector<Endpoint> &endpoints() const { return m_endpoints; }
    Endpoint *find(const QString &id);
    const Endpoint *find(const QString &id) const;
    Endpoint *findByName(const QString &name);

    Endpoint &add(const Endpoint &endpoint);
    Endpoint &insert(const Endpoint &endpoint, int index);
    bool remove(const QString &id);
    bool update(const Endpoint &endpoint);
    void clear();

    Endpoint &move(int from, int to);

    //  Replaces {{placeholders}} in a URL/body using the supplied variables.
    //  Values are URL-encoded for URLs and JSON-escaped for bodies.
    //  使用提供的变量替换 URL/正文中的 {{占位符}}。URL 中的值会被 URL 编码，
    //  正文中的值会被 JSON 转义。
    static QString expandTemplate(const QString &templ,
                                  const QMap<QString, QString> &variables,
                                  bool forUrl);

    //  Endpoints that reference a given block type.
    //  引用了指定积木类型的端点。
    QVector<const Endpoint *> referencingBlock(const QString &blockType) const;

    QJsonObject toJson() const;
    static EndpointStore fromJson(const QJsonObject &obj);

private:
    QVector<Endpoint> m_endpoints;
};

} // namespace tcbw
