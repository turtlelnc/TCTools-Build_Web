// ============================================================================
//  BackendController.cpp -- endpoints, scaffolds and the live preview server
//  BackendController.cpp —— 端点、脚手架与实时预览服务器
//
//  Part of TCTools-Build_Web (TC-BuildWeb).
//  属于 TCTools-Build_Web（TC-BuildWeb）。
// ============================================================================
#include "BackendController.h"

#include "AppController.h"
#include "AppSettings.h"
#include "BlockDef.h"
#include "CodeGenerator.h"
#include "IdGenerator.h"
#include "ProjectDocument.h"
#include "Types.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonObject>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>
#include <QVariant>

#include <algorithm>

namespace tcbw {

namespace {

// ---------------------------------------------------------------------------
//  Hard limits
//  A hostile or misconfigured server must never be able to push an unbounded
//  payload into the user interface.
//  硬性上限
//  恶意或配置错误的服务器绝不能把无界负载推入用户界面。
// ---------------------------------------------------------------------------
constexpr int kMaxTestPreviewChars = 64 * 1024;      // 64 KB of response body / 64 KB 响应正文
constexpr int kMaxFileContentsChars = 512 * 1024;    // 512 KB of generated source / 512 KB 生成源码

// ---------------------------------------------------------------------------
//  Dynamic-property keys (the public header is frozen).
//  动态属性键（公开头文件已冻结）。
// ---------------------------------------------------------------------------
constexpr char kBoundDocumentProperty[] = "_tcbwBackendBoundDocument";
constexpr char kPendingEndpointIdProperty[] = "_tcbwPendingTestEndpointId";

QString bilingual(const QString &en, const QString &zh)
{
    return en + QStringLiteral(" / ") + zh;
}

QString noProjectMessage()
{
    return QStringLiteral("No project is open. / 当前没有打开的工程。");
}

// ---------------------------------------------------------------------------
//  The endpoint store.
//
//  ProjectDocument::backendConfig IS THE SINGLE SOURCE OF TRUTH: the frozen
//  header declares no cache member, so the store is deserialised from the
//  document on every access and written back on every mutation. The shared
//  instance below only carries the value across the few statements of one
//  operation; the controllers are QML singletons and only the GUI thread calls
//  them, so no locking is required.
//
//  端点存储。
//
//  ProjectDocument::backendConfig 是唯一真实来源：冻结的头文件没有声明缓存
//  成员，因此每次访问都从文档反序列化，并在每次修改后写回。下面的共享实例只在
//  一次操作的少数几条语句之间承载该值；控制器是 QML 单例且只有界面线程调用，
//  因此无需加锁。
// ---------------------------------------------------------------------------
EndpointStore &sharedEndpointStore()
{
    static EndpointStore store;
    return store;
}

void reloadEndpointStore(const ProjectDocument *document)
{
    sharedEndpointStore() =
        EndpointStore::fromJson(document ? document->backendConfig : QJsonObject());
}

// ---------------------------------------------------------------------------
//  Builds the emitter options for a preview or a listing from the persisted
//  preferences, mirroring what AppController does for a real export.
//  依据持久化的偏好为预览或列表构建生成器选项，与 AppController 的真实导出保持一致。
// ---------------------------------------------------------------------------
EmitOptions emitOptionsFor(const AppSettings &settings, const ProjectDocument &project,
                           ExportMode mode)
{
    EmitOptions options;
    options.mode = mode;
    options.pretty = settings.prettyPrint();
    options.indentWidth = qBound(1, settings.indentWidth(), 8);
    options.includeResetCss = settings.includeResetCss();
    options.siteTitle = project.name;
    options.siteDescription = project.description;
    options.author = project.author;
    options.language = project.defaultLanguage.isEmpty() ? QStringLiteral("en")
                                                         : project.defaultLanguage;
    options.inlineCss = (mode == ExportMode::SingleHtml);
    options.inlineJs = (mode == ExportMode::SingleHtml);
    return options;
}

QStringList stringListValue(const QVariant &value)
{
    QStringList list;
    for (const QVariant &entry : value.toList()) {
        const QString text = entry.toString();
        if (!text.isEmpty())
            list.append(text);
    }
    if (list.isEmpty() && !value.toString().isEmpty())
        list.append(value.toString());
    return list;
}

QMap<QString, QString> stringMapValue(const QVariant &value)
{
    QMap<QString, QString> map;
    const QVariantMap source = value.toMap();
    for (auto it = source.constBegin(); it != source.constEnd(); ++it)
        map.insert(it.key(), it.value().toString());
    return map;
}

QVariantMap variantFromStringMap(const QMap<QString, QString> &map)
{
    QVariantMap out;
    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
        out.insert(it.key(), it.value());
    return out;
}

ExportMode exportModeOf(const QString &mode, ExportMode fallback)
{
    if (mode.trimmed().isEmpty())
        return fallback;
    return exportModeFromString(mode, fallback);
}

// ---------------------------------------------------------------------------
//  An endpoint id that is not used yet.
//  一个尚未被使用的端点 id。
// ---------------------------------------------------------------------------
QString uniqueEndpointId(const EndpointStore &store, const QString &preferred)
{
    QString candidate = preferred.trimmed();
    if (candidate.isEmpty())
        candidate = IdGenerator::next(QStringLiteral("ep"));
    while (store.find(candidate)) {
        const QString base = candidate;
        candidate = IdGenerator::next(QStringLiteral("ep"));
        if (candidate == base)
            break;
    }
    return candidate;
}

// ---------------------------------------------------------------------------
//  Every block type the project's programs actually use.
//  工程程序中实际使用的全部积木类型。
// ---------------------------------------------------------------------------
QSet<QString> blockTypesInUse(const ProjectDocument *document)
{
    QSet<QString> types;
    if (!document)
        return types;
    for (const BlockScript *script : document->program()) {
        if (!script)
            continue;
        script->forEach([&types](const Block *block) {
            if (block && !block->type.isEmpty())
                types.insert(block->type);
        });
    }
    return types;
}

// ---------------------------------------------------------------------------
//  A scaffold request from the current state of the application.
//  由应用程序当前状态构建的脚手架请求。
// ---------------------------------------------------------------------------
ScaffoldRequest scaffoldRequestFor(const QString &targetId, const QString &outputDirectory,
                                   const EndpointStore &endpoints,
                                   const QVariantMap &options, bool chinese,
                                   const QString &projectName)
{
    ScaffoldRequest request;
    request.targetId = targetId;
    request.outputDirectory = outputDirectory;
    request.endpoints = endpoints;
    if (!projectName.trimmed().isEmpty())
        request.projectName = projectName.trimmed();

    const auto flag = [&options](const char *key, bool fallback) {
        const QString name = QString::fromLatin1(key);
        return options.contains(name) ? options.value(name).toBool() : fallback;
    };
    request.includeCors = flag("includeCors", request.includeCors);
    request.includeAuthStubs = flag("includeAuthStubs", request.includeAuthStubs);
    request.includeDocker = flag("includeDocker", request.includeDocker);
    request.includeReadme = flag("includeReadme", request.includeReadme);
    request.includeEnvExample = flag("includeEnvExample", request.includeEnvExample);
    request.chineseComments =
        options.contains(QStringLiteral("chineseComments"))
            ? options.value(QStringLiteral("chineseComments")).toBool()
            : chinese;

    if (options.contains(QStringLiteral("projectName")))
        request.projectName = options.value(QStringLiteral("projectName")).toString();
    if (options.contains(QStringLiteral("nodeVersion")))
        request.nodeVersion = options.value(QStringLiteral("nodeVersion")).toString();
    if (options.contains(QStringLiteral("pythonVersion")))
        request.pythonVersion = options.value(QStringLiteral("pythonVersion")).toString();
    return request;
}

// ---------------------------------------------------------------------------
//  The directory a scaffold is written to: the explicit argument, then the
//  remembered per-session directory, then the last export directory, then
//  Documents.
//  脚手架写入的目录：显式参数、本次会话记住的目录、上次导出目录，最后是"文稿"。
// ---------------------------------------------------------------------------
QString resolveScaffoldDirectory(const QString &requested, const QString &remembered)
{
    const QString explicitDirectory = requested.trimmed();
    if (!explicitDirectory.isEmpty())
        return QDir::fromNativeSeparators(explicitDirectory);
    if (!remembered.trimmed().isEmpty())
        return QDir::fromNativeSeparators(remembered.trimmed());

    const QString lastExport = AppSettings::instance().lastExportDirectory();
    if (!lastExport.trimmed().isEmpty())
        return QDir::fromNativeSeparators(lastExport.trimmed());

    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
}

QString summariseScaffold(const EmitResult &result, const QString &directory, bool chinese)
{
    const QString base = CodeGenerator::summarise(result, chinese);
    return bilingual(QStringLiteral("%1 Written to %2.").arg(base, directory),
                     QStringLiteral("%1 已写入 %2。").arg(base, directory));
}

} // namespace

// ---------------------------------------------------------------------------
//  Construction
//  构造
// ---------------------------------------------------------------------------
BackendController::BackendController(AppController *app, QObject *parent)
    : QObject(parent)
{
    //  The connector and the preview server are value members: they are wired
    //  here and destroyed with the controller.
    //  连接器与预览服务器是值成员：在此接线，并随控制器一同销毁。
    connect(&m_connector, &BackendConnector::testStarted, this,
            [this](const QString &, const QString &) {
                if (!m_testing) {
                    m_testing = true;
                    emit testingChanged();
                }
            });
    connect(&m_connector, &BackendConnector::testFinished, this,
            &BackendController::onTestFinished);
    connect(&m_connector, &BackendConnector::healthProbeFinished, this,
            &BackendController::onHealthProbe);

    connect(&m_preview, &PreviewServer::started, this, &BackendController::onPreviewStarted);
    connect(&m_preview, &PreviewServer::stopped, this, &BackendController::onPreviewStopped);
    connect(&m_preview, &PreviewServer::clientConnected, this, [this](int) {
        emit previewClientsChanged();
    });
    connect(&m_preview, &PreviewServer::clientDisconnected, this, [this](int) {
        emit previewClientsChanged();
    });
    connect(&m_preview, &PreviewServer::errorOccurred, this, [this](const QString &message) {
        emit notice(QStringLiteral("Preview server error: %1").arg(message),
                QStringLiteral("预览服务器错误：%1").arg(message),
                true);
    });

    setAppController(app);
}

BackendController::~BackendController()
{
    m_connector.cancelAll();
    if (m_preview.isRunning())
        m_preview.stop();
}

void BackendController::setAppController(AppController *app)
{
    if (m_app == app)
        return;
    if (m_app)
        m_app->disconnect(this);

    m_app = app;
    if (!m_app)
        return;

    //  The document object is replaced on every new/open, so it is re-bound from
    //  projectChanged(); the endpoint store itself is always read from the
    //  document, never cached.
    //  每次新建 / 打开都会替换文档对象，因此在 projectChanged() 中重新绑定；
    //  端点存储始终从文档读取，绝不缓存。
    auto bindDocument = [this]() -> bool {
        ProjectDocument *document = m_app ? m_app->document() : nullptr;
        QObject *previous = property(kBoundDocumentProperty).value<QObject *>();
        if (previous == document)
            return false;
        if (previous)
            previous->disconnect(this);
        setProperty(kBoundDocumentProperty,
                    QVariant::fromValue(static_cast<QObject *>(document)));
        //  A structural change may add or remove blocks that reference an
        //  endpoint, so the endpoint list is re-announced.
        //  结构变化可能增删引用端点的积木，因此重新通告端点列表。
        if (document) {
            connect(document, &ProjectDocument::structureChanged, this, [this]() {
                emit endpointsChanged();
                emit scaffoldPlanChanged();
            });
        }
        return true;
    };

    bindDocument();

    connect(m_app, &AppController::projectChanged, this, [this, bindDocument]() {
        bindDocument();
        m_lastTestResult.clear();
        m_pendingTestId.clear();
        m_pendingHealthId.clear();
        m_testing = false;
        emit endpointsChanged();
        emit scaffoldPlanChanged();
        emit testResultChanged();
        emit testingChanged();
        emit previewChanged();
        emit previewClientsChanged();
    });
    connect(m_app, &AppController::exportModeChanged, this, [this]() {
        emit scaffoldPlanChanged();
    });
}

void BackendController::setScaffoldOutputDirectory(const QString &directory)
{
    if (m_scaffoldOutputDirectory == directory)
        return;
    m_scaffoldOutputDirectory = directory;
    emit scaffoldPlanChanged();
}

// ---------------------------------------------------------------------------
//  Endpoint store plumbing
//  端点存储的接线
// ---------------------------------------------------------------------------
EndpointStore &BackendController::store()
{
    reloadEndpointStore(m_app ? m_app->document() : nullptr);
    return sharedEndpointStore();
}

const EndpointStore &BackendController::store() const
{
    reloadEndpointStore(m_app ? m_app->document() : nullptr);
    return sharedEndpointStore();
}

void BackendController::persistEndpoints()
{
    if (!m_app || !m_app->document())
        return;
    m_app->document()->backendConfig = sharedEndpointStore().toJson();
    m_app->document()->touch();
}

// ---------------------------------------------------------------------------
//  Endpoint serialisation
//  端点的序列化
// ---------------------------------------------------------------------------
QVariantMap BackendController::endpointToVariant(const Endpoint &endpoint) const
{
    QString reason;
    const bool valid = endpoint.isValid(&reason);

    QVariantMap map;
    map.insert(QStringLiteral("id"), endpoint.id);
    map.insert(QStringLiteral("name"), endpoint.name);
    map.insert(QStringLiteral("method"), endpoint.methodUpper());
    map.insert(QStringLiteral("url"), endpoint.url);
    map.insert(QStringLiteral("contentType"), endpoint.contentType);
    map.insert(QStringLiteral("bodyTemplate"), endpoint.bodyTemplate);
    map.insert(QStringLiteral("enabled"), endpoint.enabled);
    map.insert(QStringLiteral("isGraphQl"), endpoint.isGraphQl);
    map.insert(QStringLiteral("timeoutMs"), endpoint.timeoutMs);
    map.insert(QStringLiteral("descriptionEn"), endpoint.descriptionEn);
    map.insert(QStringLiteral("descriptionZh"),
               endpoint.descriptionZh.isEmpty() ? endpoint.descriptionEn
                                                : endpoint.descriptionZh);
    map.insert(QStringLiteral("authMode"), toString(endpoint.auth.mode));
    map.insert(QStringLiteral("authHeader"), endpoint.auth.headerName);
    map.insert(QStringLiteral("authEnabled"), endpoint.auth.enabled);
    map.insert(QStringLiteral("authScheme"), endpoint.auth.schemePrefix);
    map.insert(QStringLiteral("authSecretKey"), endpoint.auth.secretKey);
    map.insert(QStringLiteral("authQueryParameter"), endpoint.auth.queryParameter);
    map.insert(QStringLiteral("headers"), variantFromStringMap(endpoint.headers));
    map.insert(QStringLiteral("query"), variantFromStringMap(endpoint.query));
    map.insert(QStringLiteral("boundBlockTypes"), endpoint.boundBlockTypes);
    map.insert(QStringLiteral("boundPageIds"), endpoint.boundPageIds);
    map.insert(QStringLiteral("exampleVariables"), endpoint.exampleVariables);
    map.insert(QStringLiteral("valid"), valid);
    //  Endpoint::isValid() already answers bilingually, so the same text serves
    //  both fields and neither language is left blank.
    //  Endpoint::isValid() 已经给出双语答案，因此同一段文本同时用于两个字段，
    //  两种语言都不会为空。
    map.insert(QStringLiteral("validationEn"), reason);
    map.insert(QStringLiteral("validationZh"), reason);
    return map;
}

Endpoint BackendController::variantToEndpoint(const QVariantMap &map) const
{
    Endpoint endpoint;
    endpoint.id = map.value(QStringLiteral("id")).toString().trimmed();
    endpoint.name = map.value(QStringLiteral("name")).toString();
    const QString method = map.value(QStringLiteral("method")).toString().trimmed();
    if (!method.isEmpty())
        endpoint.method = method.toUpper();
    endpoint.url = map.value(QStringLiteral("url")).toString().trimmed();
    const QString contentType = map.value(QStringLiteral("contentType")).toString();
    if (!contentType.isEmpty())
        endpoint.contentType = contentType;
    endpoint.bodyTemplate = map.value(QStringLiteral("bodyTemplate")).toString();
    if (map.contains(QStringLiteral("enabled")))
        endpoint.enabled = map.value(QStringLiteral("enabled")).toBool();
    if (map.contains(QStringLiteral("isGraphQl")))
        endpoint.isGraphQl = map.value(QStringLiteral("isGraphQl")).toBool();
    if (map.contains(QStringLiteral("timeoutMs"))) {
        bool ok = false;
        const int timeout = map.value(QStringLiteral("timeoutMs")).toInt(&ok);
        if (ok)
            endpoint.timeoutMs = timeout;
    }
    endpoint.descriptionEn = map.value(QStringLiteral("descriptionEn")).toString();
    endpoint.descriptionZh = map.value(QStringLiteral("descriptionZh")).toString();

    endpoint.auth.mode = authModeFromString(map.value(QStringLiteral("authMode")).toString(),
                                           AuthMode::None);
    const QString header = map.value(QStringLiteral("authHeader")).toString().trimmed();
    if (!header.isEmpty())
        endpoint.auth.headerName = header;
    endpoint.auth.enabled = map.value(QStringLiteral("authEnabled")).toBool();
    endpoint.auth.schemePrefix = map.value(QStringLiteral("authScheme")).toString();
    endpoint.auth.secretKey = map.value(QStringLiteral("authSecretKey")).toString();
    endpoint.auth.queryParameter = map.value(QStringLiteral("authQueryParameter")).toString();

    endpoint.headers = stringMapValue(map.value(QStringLiteral("headers")));
    endpoint.query = stringMapValue(map.value(QStringLiteral("query")));
    endpoint.boundBlockTypes = stringListValue(map.value(QStringLiteral("boundBlockTypes")));
    endpoint.boundPageIds = stringListValue(map.value(QStringLiteral("boundPageIds")));
    endpoint.exampleVariables = stringListValue(map.value(QStringLiteral("exampleVariables")));

    if (endpoint.name.trimmed().isEmpty()) {
        endpoint.name = endpoint.url.isEmpty()
                            ? QStringLiteral("Endpoint")
                            : QStringLiteral("%1 %2").arg(endpoint.methodUpper(), endpoint.url);
    }
    return endpoint;
}

// ---------------------------------------------------------------------------
//  Endpoint reads
//  端点读取
// ---------------------------------------------------------------------------
QVariantList BackendController::endpoints() const
{
    QVariantList list;
    for (const Endpoint &endpoint : store().endpoints())
        list.append(endpointToVariant(endpoint));
    return list;
}

QVariantMap BackendController::endpointById(const QString &endpointId) const
{
    if (const Endpoint *endpoint = store().find(endpointId.trimmed()))
        return endpointToVariant(*endpoint);
    return QVariantMap();
}

QVariantList BackendController::endpointsReferencingBlock(const QString &blockType) const
{
    QVariantList list;
    for (const Endpoint *endpoint : store().referencingBlock(blockType.trimmed())) {
        if (endpoint)
            list.append(endpointToVariant(*endpoint));
    }
    return list;
}

QVariantMap BackendController::emptyEndpointTemplate() const
{
    QVariantMap map;
    map.insert(QStringLiteral("id"), QString());
    map.insert(QStringLiteral("name"),
               QStringLiteral("New endpoint / 新建端点"));
    map.insert(QStringLiteral("method"), QStringLiteral("GET"));
    //  A usable default URL keeps "add endpoint" a one-click action: an empty URL
    //  would fail validation and return an empty map to the caller.
    //  可用的默认 URL 让"添加端点"只需一次点击：空 URL 会校验失败并向调用方返回空映射。
    map.insert(QStringLiteral("url"), QStringLiteral("https://api.example.com/v1/items"));
    map.insert(QStringLiteral("contentType"), QStringLiteral("application/json"));
    map.insert(QStringLiteral("bodyTemplate"), QString());
    map.insert(QStringLiteral("enabled"), true);
    map.insert(QStringLiteral("isGraphQl"), false);
    map.insert(QStringLiteral("timeoutMs"), 30000);
    map.insert(QStringLiteral("descriptionEn"), QString());
    map.insert(QStringLiteral("descriptionZh"), QString());
    map.insert(QStringLiteral("authMode"), QStringLiteral("none"));
    map.insert(QStringLiteral("authHeader"), QStringLiteral("Authorization"));
    map.insert(QStringLiteral("authEnabled"), false);
    map.insert(QStringLiteral("authScheme"), QString());
    map.insert(QStringLiteral("authSecretKey"), QString());
    map.insert(QStringLiteral("authQueryParameter"), QString());
    map.insert(QStringLiteral("headers"), QVariantMap());
    map.insert(QStringLiteral("query"), QVariantMap());
    map.insert(QStringLiteral("boundBlockTypes"), QStringList());
    map.insert(QStringLiteral("boundPageIds"), QStringList());
    map.insert(QStringLiteral("exampleVariables"), QStringList());
    map.insert(QStringLiteral("valid"), false);
    map.insert(QStringLiteral("validationEn"), QString());
    map.insert(QStringLiteral("validationZh"), QString());
    return map;
}

// ---------------------------------------------------------------------------
//  Endpoint mutations
//  端点的增删改
// ---------------------------------------------------------------------------
QVariantMap BackendController::addEndpoint(const QVariantMap &endpoint)
{
    if (!m_app || !m_app->document()) {
        emit notice(noProjectMessage(), noProjectMessage(), true);
        return QVariantMap();
    }

    Endpoint created = variantToEndpoint(endpoint);
    EndpointStore &current = store();
    created.id = uniqueEndpointId(current, created.id);

    QString reason;
    if (!created.isValid(&reason)) {
        emit notice(QStringLiteral("The endpoint was not added: %1").arg(reason),
                QStringLiteral("未添加该端点：%1").arg(reason),
                true);
        return QVariantMap();
    }

    Endpoint &stored = current.add(created);
    persistEndpoints();
    emit endpointsChanged();
    emit scaffoldPlanChanged();
    return endpointToVariant(stored);
}

bool BackendController::updateEndpoint(const QVariantMap &endpoint)
{
    if (!m_app || !m_app->document()) {
        emit notice(noProjectMessage(), noProjectMessage(), true);
        return false;
    }

    EndpointStore &current = store();
    const QString wantedId = endpoint.value(QStringLiteral("id")).toString().trimmed();
    Endpoint *existing = wantedId.isEmpty() ? nullptr : current.find(wantedId);
    if (!existing) {
        //  EndpointEditor round-trips the whole map, so an id is always present;
        //  the name is a defensive second chance for hand-built maps.
        const QString wantedName = endpoint.value(QStringLiteral("name")).toString().trimmed();
        if (!wantedName.isEmpty())
            existing = current.findByName(wantedName);
    }
    if (!existing) {
        emit notice(QStringLiteral("The endpoint no longer exists."),
                QStringLiteral("该端点已不存在。"),
                true);
        return false;
    }

    Endpoint updated = variantToEndpoint(endpoint);
    updated.id = existing->id;
    QString reason;
    if (!updated.isValid(&reason)) {
        emit notice(QStringLiteral("The endpoint was not saved: %1").arg(reason),
                QStringLiteral("未保存该端点：%1").arg(reason),
                true);
        return false;
    }

    if (!current.update(updated))
        return false;
    persistEndpoints();
    emit endpointsChanged();
    emit scaffoldPlanChanged();
    return true;
}

bool BackendController::removeEndpoint(const QString &endpointId)
{
    if (!m_app || !m_app->document()) {
        emit notice(noProjectMessage(), noProjectMessage(), true);
        return false;
    }

    const QString id = endpointId.trimmed();
    if (id.isEmpty())
        return false;

    //  REFUSAL: an endpoint that is still bound to a block type the project uses
    //  must not be deleted, because the exported page would keep calling it while
    //  the builder no longer knows anything about it. EndpointStore::referencingBlock()
    //  answers "which endpoints declare this block type", so the check runs in the
    //  other direction: for every block type the program actually contains, look
    //  for the endpoint being removed.
    //  拒绝删除：仍绑定到工程正在使用的积木类型的端点不得删除，否则导出的页面会继续
    //  调用它，而构建器已对它一无所知。EndpointStore::referencingBlock() 回答的是
    //  "哪些端点声明了该积木类型"，因此检查反向进行：对程序中实际存在的每种积木
    //  类型，查找正在删除的这个端点。
    const QSet<QString> usedTypes = blockTypesInUse(m_app->document());
    if (!usedTypes.isEmpty()) {
        const EndpointStore &current = store();
        for (const QString &type : usedTypes) {
            for (const Endpoint *referencing : current.referencingBlock(type)) {
                if (!referencing || referencing->id != id)
                    continue;
                emit notice(
                        QStringLiteral("The endpoint \"%1\" is still used by \"%2\" blocks "
                        "in this project; remove those blocks first.")
                        .arg(referencing->name.isEmpty() ? id : referencing->name,
                        type),
                        QStringLiteral("端点 \"%1\" 仍被本工程的 \"%2\" 积木使用；"
                        "请先移除那些积木。")
                        .arg(referencing->name.isEmpty() ? id : referencing->name,
                        type),
                        true);
                return false;
            }
        }
    }

    EndpointStore &current = store();
    if (!current.remove(id)) {
        emit notice(QStringLiteral("The endpoint no longer exists."),
                QStringLiteral("该端点已不存在。"),
                true);
        return false;
    }
    persistEndpoints();
    emit endpointsChanged();
    emit scaffoldPlanChanged();
    return true;
}

bool BackendController::duplicateEndpoint(const QString &endpointId)
{
    if (!m_app || !m_app->document()) {
        emit notice(noProjectMessage(), noProjectMessage(), true);
        return false;
    }

    EndpointStore &current = store();
    const Endpoint *original = current.find(endpointId.trimmed());
    if (!original) {
        emit notice(QStringLiteral("The endpoint no longer exists."),
                QStringLiteral("该端点已不存在。"),
                true);
        return false;
    }

    Endpoint copy = *original;
    copy.id = uniqueEndpointId(current, QString());
    copy.name = bilingual(QStringLiteral("Copy of %1").arg(original->name),
                          QStringLiteral("%1 的副本").arg(original->name));
    current.add(copy);
    persistEndpoints();
    emit endpointsChanged();
    emit scaffoldPlanChanged();
    return true;
}

bool BackendController::validateEndpoint(const QVariantMap &endpoint, bool reportErrors)
{
    const Endpoint candidate = variantToEndpoint(endpoint);
    QString reason;
    const bool valid = candidate.isValid(&reason);
    if (!valid && reportErrors) {
        emit notice(QStringLiteral("The endpoint is not valid: %1").arg(reason),
                QStringLiteral("端点不合法：%1").arg(reason),
                true);
    }
    return valid;
}

// ---------------------------------------------------------------------------
//  Live testing
//  实时测试
// ---------------------------------------------------------------------------
QString BackendController::testEndpoint(const QString &endpointId, const QVariantMap &variables)
{
    if (!m_app || !m_app->document()) {
        emit notice(noProjectMessage(), noProjectMessage(), true);
        return QString();
    }

    //  The endpoint is copied out of the store: the store is deserialised from
    //  the document on every access, so a pointer into it must not be held across
    //  a call that may access it again.
    //  端点从存储中复制出来：存储每次访问都从文档反序列化，因此不得在可能再次访问
    //  它的调用之间持有其内部指针。
    const Endpoint *found = store().find(endpointId.trimmed());
    if (!found) {
        emit notice(QStringLiteral("The endpoint no longer exists."),
                QStringLiteral("该端点已不存在。"),
                true);
        return QString();
    }
    const Endpoint endpoint = *found;

    QMap<QString, QString> merged;
    for (const QString &pair : endpoint.exampleVariables) {
        const int separator = pair.indexOf(QLatin1Char('='));
        if (separator <= 0)
            continue;
        merged.insert(pair.left(separator).trimmed(), pair.mid(separator + 1));
    }
    //  Caller-supplied variables win over the endpoint's own examples.
    //  调用方提供的变量优先于端点自带的示例。
    for (auto it = variables.constBegin(); it != variables.constEnd(); ++it)
        merged.insert(it.key(), it.value().toString());

    //  The connector resolves credentials through SecretStore when no
    //  AuthManager is attached, which is the case here: this controller has no
    //  AuthManager member, so the shared credential store is used instead.
    //  未挂接 AuthManager 时连接器会通过 SecretStore 解析凭据，此处正是如此：
    //  本控制器没有 AuthManager 成员，因此改用共享的凭据存储。
    const QString requestId = m_connector.test(endpoint, merged);
    if (requestId.isEmpty()) {
        emit notice(QStringLiteral("The test request could not be started."),
                QStringLiteral("无法开始测试请求。"),
                true);
        return QString();
    }

    m_pendingTestId = requestId;
    setProperty(kPendingEndpointIdProperty, endpoint.id);
    if (!m_testing) {
        m_testing = true;
        emit testingChanged();
    }
    return requestId;
}

QString BackendController::probeBackend(const QString &targetId, const QString &baseUrl)
{
    const BackendTarget *target = BackendConnector::findTarget(targetId.trimmed());
    if (!target) {
        emit notice(QStringLiteral("Unknown backend target \"%1\".").arg(targetId),
                QStringLiteral("未知的后端目标 \"%1\"。").arg(targetId),
                true);
        return QString();
    }

    const QString url = baseUrl.trimmed().isEmpty() ? target->defaultBaseUrl : baseUrl.trimmed();
    const QString requestId = m_connector.probeHealth(*target, url);
    if (requestId.isEmpty()) {
        emit notice(QStringLiteral("The health probe could not be started."),
                QStringLiteral("无法开始健康检查。"),
                true);
        return QString();
    }

    m_pendingHealthId = requestId;
    setProperty(kPendingEndpointIdProperty, QString());
    if (!m_testing) {
        m_testing = true;
        emit testingChanged();
    }
    return requestId;
}

void BackendController::cancelTests()
{
    m_connector.cancelAll();
    m_pendingTestId.clear();
    m_pendingHealthId.clear();
    setProperty(kPendingEndpointIdProperty, QString());
    if (m_testing) {
        m_testing = false;
        emit testingChanged();
    }
    emit notice(QStringLiteral("Running tests were cancelled."),
            QStringLiteral("正在运行的测试已取消。"),
            false);
}

void BackendController::onTestFinished(const QString &requestId, const EndpointTestResult &result)
{
    if (!m_pendingTestId.isEmpty() && requestId != m_pendingTestId) {
        //  A stale reply from a superseded test must not overwrite the newest one.
        //  已被取代的测试的过期响应不得覆盖最新的结果。
        return;
    }
    m_pendingTestId.clear();

    QVariantMap headers;
    for (auto it = result.responseHeaders.constBegin(); it != result.responseHeaders.constEnd();
         ++it) {
        headers.insert(it.key(), it.value());
    }

    QVariantMap map;
    map.insert(QStringLiteral("ok"), result.ok);
    map.insert(QStringLiteral("status"), result.httpStatus);
    map.insert(QStringLiteral("elapsedMs"), static_cast<qint64>(result.elapsedMs));
    //  The response body is capped at 64 KB before it reaches QML.
    //  响应正文在进入 QML 之前被限制为 64 KB。
    map.insert(QStringLiteral("bodyPreview"), result.bodyPreview(kMaxTestPreviewChars));
    map.insert(QStringLiteral("headers"), headers);
    map.insert(QStringLiteral("errorEn"), result.errorEn);
    map.insert(QStringLiteral("errorZh"),
               result.errorZh.isEmpty() ? result.errorEn : result.errorZh);
    map.insert(QStringLiteral("endpointId"),
               property(kPendingEndpointIdProperty).toString());
    m_lastTestResult = map;
    setProperty(kPendingEndpointIdProperty, QString());

    if (m_testing) {
        m_testing = false;
        emit testingChanged();
    }
    emit testResultChanged();
}

void BackendController::onHealthProbe(const QString &requestId, bool ok, const QString &detail)
{
    if (!m_pendingHealthId.isEmpty() && requestId != m_pendingHealthId)
        return;
    m_pendingHealthId.clear();

    QVariantMap map;
    map.insert(QStringLiteral("ok"), ok);
    map.insert(QStringLiteral("status"), ok ? 200 : 0);
    map.insert(QStringLiteral("elapsedMs"), static_cast<qint64>(0));
    map.insert(QStringLiteral("bodyPreview"), detail);
    map.insert(QStringLiteral("headers"), QVariantMap());
    map.insert(QStringLiteral("errorEn"), ok ? QString() : detail);
    map.insert(QStringLiteral("errorZh"), ok ? QString() : detail);
    map.insert(QStringLiteral("endpointId"), QString());
    m_lastTestResult = map;

    if (m_testing) {
        m_testing = false;
        emit testingChanged();
    }
    emit testResultChanged();
}

// ---------------------------------------------------------------------------
//  Backend targets and scaffolding
//  后端目标与脚手架
// ---------------------------------------------------------------------------
QVariantMap targetToVariant(const BackendTarget &target, bool chinese)
{
    QVariantMap map;
    map.insert(QStringLiteral("id"), target.id);
    map.insert(QStringLiteral("labelEn"), target.labelEn);
    map.insert(QStringLiteral("labelZh"),
               target.labelZh.isEmpty() ? target.labelEn : target.labelZh);
    //  A ready-made localised label, for call sites that do not want to branch.
    //  现成的本地化标签，供不愿分支的调用处使用。
    map.insert(QStringLiteral("label"),
               chinese && !target.labelZh.isEmpty() ? target.labelZh : target.labelEn);
    map.insert(QStringLiteral("language"), target.language);
    map.insert(QStringLiteral("descriptionEn"), target.descriptionEn);
    map.insert(QStringLiteral("descriptionZh"),
               target.descriptionZh.isEmpty() ? target.descriptionEn : target.descriptionZh);
    map.insert(QStringLiteral("defaultBaseUrl"), target.defaultBaseUrl);
    map.insert(QStringLiteral("healthPath"), target.healthPath);
    map.insert(QStringLiteral("docsUrl"), target.docsUrl);
    map.insert(QStringLiteral("provides"), target.provides);
    map.insert(QStringLiteral("requiresAccount"), target.requiresAccount);
    map.insert(QStringLiteral("order"), target.order);
    return map;
}

QVariantList BackendController::backendTargets() const
{
    const bool chinese = m_app ? m_app->chinese() : AppSettings::instance().isChinese();
    QVariantList list;
    for (const BackendTarget &target : BackendConnector::targets())
        list.append(targetToVariant(target, chinese));
    return list;
}

QVariantList BackendController::scaffoldOptions() const
{
    const bool chinese = m_app ? m_app->chinese() : AppSettings::instance().isChinese();
    QVariantList list;
    for (const BackendTarget &target : BackendConnector::targets()) {
        QVariantMap entry = targetToVariant(target, chinese);
        //  AppComboBox reads `value` and `text`.
        //  AppComboBox 读取 `value` 与 `text`。
        entry.insert(QStringLiteral("value"), target.id);
        entry.insert(QStringLiteral("text"),
                     chinese && !target.labelZh.isEmpty() ? target.labelZh : target.labelEn);
        entry.insert(QStringLiteral("description"),
                     chinese && !target.descriptionZh.isEmpty() ? target.descriptionZh
                                                                : target.descriptionEn);
        list.append(entry);
    }
    return list;
}

QVariantMap BackendController::plannedScaffoldFiles(const QString &targetId) const
{
    QVariantMap map;
    const QString wanted = targetId.trimmed().isEmpty() ? m_scaffoldTargetId
                                                        : targetId.trimmed();
    map.insert(QStringLiteral("targetId"), wanted);
    map.insert(QStringLiteral("count"), 0);
    map.insert(QStringLiteral("files"), QVariantList());

    const BackendTarget *target = BackendConnector::findTarget(wanted);
    if (!target)
        return map;

    const ProjectDocument *document = m_app ? m_app->document() : nullptr;
    const QString projectName = document ? document->name : QString();
    const ScaffoldRequest request = scaffoldRequestFor(
        wanted, m_scaffoldOutputDirectory, store(), QVariantMap(),
        m_app ? m_app->chinese() : AppSettings::instance().isChinese(), projectName);

    const QStringList files = m_scaffold.plannedFiles(request);
    QVariantList list;
    for (const QString &file : files)
        list.append(file);
    map.insert(QStringLiteral("files"), list);
    map.insert(QStringLiteral("count"), static_cast<int>(list.size()));
    return map;
}

QVariantList BackendController::scaffoldPlan() const
{
    return plannedScaffoldFiles(m_scaffoldTargetId).value(QStringLiteral("files")).toList();
}

void BackendController::setScaffoldTargetId(const QString &targetId)
{
    const QString wanted = targetId.trimmed();
    if (wanted.isEmpty() || wanted == m_scaffoldTargetId)
        return;
    m_scaffoldTargetId = wanted;
    emit scaffoldPlanChanged();
}

QString BackendController::scaffoldBackend(const QString &targetId,
                                           const QString &outputDirectory,
                                           const QVariantMap &options)
{
    if (!m_app || !m_app->document()) {
        emit notice(noProjectMessage(), noProjectMessage(), true);
        return QString();
    }

    QString wanted = targetId.trimmed().isEmpty() ? m_scaffoldTargetId : targetId.trimmed();
    const BackendTarget *target = BackendConnector::findTarget(wanted);
    if (!target) {
        emit notice(QStringLiteral("Unknown backend target \"%1\".").arg(wanted),
                QStringLiteral("未知的后端目标 \"%1\"。").arg(wanted),
                true);
        return QString();
    }

    const QString directory =
        resolveScaffoldDirectory(outputDirectory, m_scaffoldOutputDirectory);
    if (directory.trimmed().isEmpty()) {
        emit notice(QStringLiteral("No output directory was given."),
                QStringLiteral("未提供输出目录。"),
                true);
        return QString();
    }
    if (!QDir().mkpath(directory)) {
        emit notice(QStringLiteral("The directory %1 could not be created.").arg(directory),
                QStringLiteral("无法创建目录 %1。").arg(directory),
                true);
        return QString();
    }

    const ScaffoldRequest request = scaffoldRequestFor(
        wanted, directory, store(), options,
        m_app->chinese(), options.value(QStringLiteral("projectName")).toString().isEmpty()
                              ? m_app->document()->name
                              : options.value(QStringLiteral("projectName")).toString());

    const EmitResult result = m_scaffold.generate(request);
    if (!result.ok) {
        const QString first = result.errors.isEmpty()
                                  ? QStringLiteral("the generator reported no reason")
                                  : result.errors.first();
        emit notice(QStringLiteral("The scaffold could not be generated: %1").arg(first),
                QStringLiteral("无法生成脚手架：%1").arg(first),
                true);
        return QString();
    }
    if (result.files.isEmpty()) {
        emit notice(QStringLiteral("The scaffold produced no files."),
                QStringLiteral("脚手架没有产生任何文件。"),
                true);
        return QString();
    }

    QStringList errors;
    if (!CodeGenerator::writeToDirectory(result, directory, &errors)) {
        const QString first = errors.isEmpty() ? QStringLiteral("the files could not be written")
                                               : errors.first();
        emit notice(QStringLiteral("The scaffold could not be written: %1").arg(first),
                QStringLiteral("无法写入脚手架：%1").arg(first),
                true);
        return QString();
    }

    m_scaffoldOutputDirectory = directory;
    AppSettings::instance().setLastExportDirectory(directory);
    return summariseScaffold(result, directory, m_app->chinese());
}

QString BackendController::exportScaffoldToDialog(const QString &targetId)
{
    //  Native folder pickers belong to the QML layer (see AppController's long
    //  bilingual note); this entry point only works once a directory is known.
    //  原生文件夹选择器属于 QML 层（参见 AppController 中的双语说明）；本入口仅在
    //  已知目录时可用。
    if (!m_app || !m_app->document()) {
        emit notice(noProjectMessage(), noProjectMessage(), true);
        return QString();
    }
    if (m_scaffoldOutputDirectory.trimmed().isEmpty()) {
        emit notice(
                QStringLiteral("No scaffold output directory is known: choose a folder in "
                "QML and call scaffoldBackend(targetId, directory)."),
                QStringLiteral("尚不知道脚手架输出目录：请在 QML 中选择文件夹并调用 "
                "scaffoldBackend(targetId, directory)。"),
                true);
        return QString();
    }
    return scaffoldBackend(targetId, m_scaffoldOutputDirectory, QVariantMap());
}

// ---------------------------------------------------------------------------
//  Preview server
//  预览服务器
// ---------------------------------------------------------------------------
bool BackendController::previewRunning() const
{
    return m_preview.isRunning();
}

QString BackendController::previewUrl() const
{
    return m_preview.baseUrl();
}

int BackendController::previewClients() const
{
    return m_preview.clientCount();
}

bool BackendController::startPreview()
{
    if (!m_app || !m_app->document()) {
        emit notice(noProjectMessage(), noProjectMessage(), true);
        return false;
    }

    const EmitResult result = currentBuild(QString());
    if (!result.ok) {
        const QString first = result.errors.isEmpty()
                                  ? QStringLiteral("the generator reported no reason")
                                  : result.errors.first();
        emit notice(QStringLiteral("The preview could not be built: %1").arg(first),
                QStringLiteral("无法构建预览：%1").arg(first),
                true);
        return false;
    }

    //  The generated files are served straight from memory: nothing is written
    //  to disk for a preview.
    //  生成的文件直接从内存提供：预览不会写入磁盘。
    m_preview.setFiles(result.files);
    const Page *home = m_app->document()->homePage();
    if (home && !home->fileName.trimmed().isEmpty())
        m_preview.setDefaultDocument(home->fileName.trimmed());

    QString error;
    if (!m_preview.start(0, &error)) {
        m_previewStatus = bilingual(QStringLiteral("The preview server could not start: %1")
                                        .arg(error.isEmpty() ? QStringLiteral("unknown error")
                                                             : error),
                                    QStringLiteral("预览服务器无法启动：%1")
                                        .arg(error.isEmpty() ? QStringLiteral("未知错误")
                                                             : error));
        emit notice(m_previewStatus, m_previewStatus, true);
        emit previewChanged();
        return false;
    }

    m_previewStatus = bilingual(QStringLiteral("Preview server running at %1.")
                                    .arg(m_preview.baseUrl()),
                                QStringLiteral("预览服务器正在 %1 运行。")
                                    .arg(m_preview.baseUrl()));
    emit previewChanged();
    emit previewClientsChanged();
    return true;
}

void BackendController::stopPreview()
{
    if (!m_preview.isRunning()) {
        m_previewStatus = bilingual(QStringLiteral("The preview server is not running."),
                                    QStringLiteral("预览服务器未在运行。"));
        emit previewChanged();
        return;
    }
    m_preview.stop();
    m_previewStatus = bilingual(QStringLiteral("Preview server stopped."),
                                QStringLiteral("预览服务器已停止。"));
    emit previewChanged();
    emit previewClientsChanged();
}

void BackendController::refreshPreviewFiles()
{
    if (!m_app || !m_app->document() || !m_preview.isRunning())
        return;

    //  Rebuilding the whole site on every keystroke would be wasteful, so the
    //  file set is only regenerated when the document is actually modified.
    //  每次按键都重建整个站点是浪费的，因此仅在文档确实被修改时重新生成文件集合。
    if (m_app->modified()) {
        const EmitResult result = currentBuild(QString());
        if (result.ok && !result.files.isEmpty())
            m_preview.setFiles(result.files);
    }

    if (AppSettings::instance().previewReloadOnChange())
        m_preview.notifyReload();
    emit previewChanged();
}

void BackendController::openPreviewInBrowser()
{
    if (!m_preview.isRunning()) {
        emit notice(
                QStringLiteral("The preview server is not running; start it first."),
                QStringLiteral("预览服务器未在运行；请先启动它。"),
                true);
        return;
    }
    const QUrl url(m_preview.baseUrl());
    if (url.isEmpty() || !QDesktopServices::openUrl(url)) {
        emit notice(QStringLiteral("The browser could not be opened for %1.")
                .arg(m_preview.baseUrl()),
                QStringLiteral("无法为 %1 打开浏览器。").arg(m_preview.baseUrl()),
                true);
    }
}

QString BackendController::previewUrlFor(const QString &relativePath) const
{
    return m_preview.urlFor(relativePath);
}

QString BackendController::copyPreviewUrl() const
{
    const QString url = m_preview.baseUrl();
    if (url.isEmpty())
        return QString();
    if (QClipboard *clipboard = QGuiApplication::clipboard())
        clipboard->setText(url);
    return url;
}

void BackendController::onPreviewStarted(quint16 port)
{
    m_previewStatus = bilingual(QStringLiteral("Preview server listening on 127.0.0.1:%1.")
                                    .arg(port),
                                QStringLiteral("预览服务器正在 127.0.0.1:%1 监听。").arg(port));
    emit previewChanged();
    emit previewClientsChanged();
}

void BackendController::onPreviewStopped()
{
    m_previewStatus = bilingual(QStringLiteral("Preview server stopped."),
                                QStringLiteral("预览服务器已停止。"));
    emit previewChanged();
    emit previewClientsChanged();
}

// ---------------------------------------------------------------------------
//  Generated files (read-only)
//  生成的文件（只读）
// ---------------------------------------------------------------------------
EmitResult BackendController::currentBuild(const QString &mode) const
{
    EmitResult result;
    const ProjectDocument *document = m_app ? m_app->document() : nullptr;
    if (!document) {
        result.ok = false;
        result.errors.append(noProjectMessage());
        return result;
    }

    const ExportMode fallback = m_app ? m_app->activeExportMode() : ExportMode::SingleHtml;
    const ExportMode chosen = exportModeOf(mode, fallback);
    const CodeGenerator generator(emitOptionsFor(AppSettings::instance(), *document, chosen));
    return generator.generateProject(*document);
}

QVariantList BackendController::generatedFileList(const QString &mode) const
{
    QVariantList list;
    const EmitResult result = currentBuild(mode);
    for (const EmittedFile &file : result.files) {
        QVariantMap entry;
        entry.insert(QStringLiteral("path"), file.relativePath);
        entry.insert(QStringLiteral("size"), static_cast<qint64>(file.size()));
        entry.insert(QStringLiteral("mime"), file.mimeType);
        list.append(entry);
    }
    return list;
}

QString BackendController::generatedFileContents(const QString &relativePath,
                                                 const QString &mode) const
{
    const EmitResult result = currentBuild(mode);
    if (!result.ok && result.files.isEmpty())
        return QString();

    const QString wanted = relativePath.trimmed();
    QString contents;
    if (wanted.isEmpty() || wanted == QLatin1String("@preview")) {
        //  "@preview" is what CodeView and PreviewView show by default: the home
        //  page's generated .html document. Nothing is written to disk for this.
        //  "@preview" 是 CodeView 与 PreviewView 默认显示的内容：首页生成的 .html
        //  文档。此操作不写入任何磁盘内容。
        const ProjectDocument *document = m_app ? m_app->document() : nullptr;
        const Page *home = document ? document->homePage() : nullptr;
        if (home && !home->fileName.trimmed().isEmpty())
            contents = result.fileContents(home->fileName.trimmed());
        if (contents.isEmpty()) {
            for (const EmittedFile &file : result.files) {
                if (file.relativePath.endsWith(QLatin1String(".html"), Qt::CaseInsensitive)) {
                    contents = file.contents;
                    break;
                }
            }
        }
    } else {
        contents = result.fileContents(wanted);
    }

    if (contents.size() > kMaxFileContentsChars) {
        contents = contents.left(kMaxFileContentsChars)
                 + QStringLiteral("\n… [truncated at %1 characters] / "
                                  "[已在 %1 个字符处截断]")
                       .arg(kMaxFileContentsChars);
    }
    return contents;
}

} // namespace tcbw
