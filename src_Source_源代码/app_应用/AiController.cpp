// ============================================================================
//  AiController.cpp -- TI-UseAgent behind QML
//  AiController.cpp —— QML 背后的 TI-UseAgent
//
//  Part of TCTools-Build_Web (TC-BuildWeb).
//  属于 TCTools-Build_Web（TC-BuildWeb）。
//
//  SECURITY NOTE / 安全说明
//   The AI never touches the document directly. Every change goes through a
//   declared tool, and under the default policy ("ask-destructive") a mutating
//   tool is only executed after the user explicitly approves it here. The
//   approval callback installed below therefore *never* returns true on its own:
//   it records the request and refuses it so that the registry does not block.
//   AI 从不直接操作文档。每次变更都必须经过声明的工具，并且在默认策略
//   （"ask-destructive"）下，变更类工具只有在此处得到用户明确批准后才会执行。
//   因此下面安装的审批回调*绝不*自行返回 true：它记录请求并拒绝它，使注册表
//   不会阻塞。
// ============================================================================
#include "AiController.h"

#include "AppController.h"
#include "AppSettings.h"
#include "BuilderController.h"
#include "IdGenerator.h"
#include "ProviderRegistry.h"
#include "SecretStore.h"
#include "Types.h"

#include <QClipboard>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QSaveFile>
#include <QStandardPaths>
#include <QVariant>

#include <algorithm>
#include <utility>

namespace tcbw {

namespace {

// ---------------------------------------------------------------------------
//  Hard limits
//  A runaway model or a hostile MCP server must never be able to exhaust memory
//  through the transcript or the approval queue.
//  硬性上限
//  失控的模型或恶意的 MCP 服务器绝不能通过会话记录或审批队列耗尽内存。
// ---------------------------------------------------------------------------
constexpr int kMaxTranscriptEntries = 2000;
constexpr int kMaxPendingToolCalls = 100;
constexpr int kMaxToolSummaryChars = 240;
constexpr int kMaxMarkdownSectionChars = 20000;

// ---------------------------------------------------------------------------
//  Dynamic-property keys (the public header is frozen, so implementation state
//  lives in dynamic properties).
//  动态属性键（公开头文件已冻结，因此实现状态保存在动态属性中）。
// ---------------------------------------------------------------------------
constexpr char kBoundDocumentProperty[] = "_tcbwAiBoundDocument";
constexpr char kErrorToolCallsProperty[] = "_tcbwErrorToolCalls";
constexpr char kGatedToolCallsProperty[] = "_tcbwGatedToolCalls";
constexpr char kGateBypassProperty[] = "_tcbwApprovalBypass";

// ---------------------------------------------------------------------------
//  Localised helpers
//  本地化辅助函数
// ---------------------------------------------------------------------------
QString bilingual(const QString &en, const QString &zh)
{
    return en + QStringLiteral(" / ") + zh;
}

bool isChinese(const AppController *app)
{
    if (app)
        return app->chinese();
    return AppSettings::instance().isChinese();
}

QString firstLine(const QString &text, int maxChars = kMaxToolSummaryChars)
{
    QString line = text.section(QLatin1Char('\n'), 0, 0).trimmed();
    if (line.size() > maxChars)
        line = line.left(maxChars) + QStringLiteral("…");
    return line;
}

QString bilingualEn(const QString &text)
{
    const QString line = text.section(QLatin1Char('\n'), 0, 0).trimmed();
    return line.isEmpty() ? text.trimmed() : line;
}

QString bilingualZh(const QString &text)
{
    const QStringList lines = text.split(QLatin1Char('\n'));
    if (lines.size() <= 1)
        return text.trimmed();
    return lines.mid(1).join(QLatin1Char('\n')).trimmed();
}

// ---------------------------------------------------------------------------
//  Approval policy <-> stable string, including the Chinese labels the QML
//  combo box and the settings file may hold.
//  审批策略与稳定字符串互转，包含 QML 下拉框与设置文件可能保存的中文标签。
// ---------------------------------------------------------------------------
QString approvalPolicyToString(ApprovalPolicy policy)
{
    switch (policy) {
    case ApprovalPolicy::Never:          return QStringLiteral("never");
    case ApprovalPolicy::SafeOnly:       return QStringLiteral("safe-only");
    case ApprovalPolicy::AskEveryChange: return QStringLiteral("ask-every-change");
    case ApprovalPolicy::AskDestructive: return QStringLiteral("ask-destructive");
    }
    return QStringLiteral("ask-destructive");
}

bool approvalPolicyFromString(const QString &text, ApprovalPolicy *out)
{
    const QString key = text.trimmed().toLower();
    if (key.isEmpty())
        return false;
    if (key == QLatin1String("never") || key == QStringLiteral("从不")
        || key == QStringLiteral("从不允许") || key == QStringLiteral("禁止")) {
        if (out)
            *out = ApprovalPolicy::Never;
        return true;
    }
    if (key == QLatin1String("safe-only") || key == QLatin1String("safeonly")
        || key == QStringLiteral("仅安全") || key == QStringLiteral("仅安全操作")
        || key == QStringLiteral("只允许安全操作")) {
        if (out)
            *out = ApprovalPolicy::SafeOnly;
        return true;
    }
    if (key == QLatin1String("ask-every-change") || key == QLatin1String("askeverychange")
        || key == QStringLiteral("每次变更都询问") || key == QStringLiteral("每次变更")) {
        if (out)
            *out = ApprovalPolicy::AskEveryChange;
        return true;
    }
    if (key == QLatin1String("ask-destructive") || key == QLatin1String("askdestructive")
        || key == QStringLiteral("仅破坏性操作询问") || key == QStringLiteral("仅破坏性")
        || key == QStringLiteral("询问破坏性操作")) {
        if (out)
            *out = ApprovalPolicy::AskDestructive;
        return true;
    }
    return false;
}

ModelCapability capabilityFromString(const QString &text)
{
    const QString key = text.trimmed().toLower();
    if (key == QLatin1String("streaming"))
        return CapStreaming;
    if (key == QLatin1String("tools") || key == QLatin1String("tool"))
        return CapTools;
    if (key == QLatin1String("vision"))
        return CapVision;
    if (key == QLatin1String("reasoning"))
        return CapReasoning;
    if (key == QLatin1String("embeddings"))
        return CapEmbeddings;
    if (key == QLatin1String("image"))
        return CapImage;
    return CapChat;
}

QString describeCapabilities(ModelCapabilities capabilities)
{
    QStringList parts;
    if (capabilities.testFlag(CapChat))
        parts << QStringLiteral("chat");
    if (capabilities.testFlag(CapStreaming))
        parts << QStringLiteral("streaming");
    if (capabilities.testFlag(CapTools))
        parts << QStringLiteral("tools");
    if (capabilities.testFlag(CapVision))
        parts << QStringLiteral("vision");
    if (capabilities.testFlag(CapReasoning))
        parts << QStringLiteral("reasoning");
    if (capabilities.testFlag(CapEmbeddings))
        parts << QStringLiteral("embeddings");
    if (capabilities.testFlag(CapImage))
        parts << QStringLiteral("image");
    return parts.join(QStringLiteral(", "));
}

QVariantMap modelToVariant(const ModelInfo &model)
{
    QVariantMap map;
    map.insert(QStringLiteral("id"), model.id);
    map.insert(QStringLiteral("label"), model.label.isEmpty() ? model.id : model.label);
    map.insert(QStringLiteral("capabilities"), describeCapabilities(model.caps));
    map.insert(QStringLiteral("supportsTools"), model.caps.testFlag(CapTools));
    map.insert(QStringLiteral("supportsStreaming"), model.caps.testFlag(CapStreaming));
    map.insert(QStringLiteral("supportsVision"), model.caps.testFlag(CapVision));
    map.insert(QStringLiteral("supportsReasoning"), model.caps.testFlag(CapReasoning));
    map.insert(QStringLiteral("contextWindow"), model.contextWindow);
    map.insert(QStringLiteral("maxOutputTokens"), model.maxOutputTokens);
    map.insert(QStringLiteral("inputPricePerMillion"), model.inputPricePerMillion);
    map.insert(QStringLiteral("outputPricePerMillion"), model.outputPricePerMillion);
    map.insert(QStringLiteral("deprecated"), model.deprecated);
    return map;
}

// ---------------------------------------------------------------------------
//  The vendor's saved configuration, with the controller's live choices applied.
//  厂商已保存的配置，并应用控制器当前的实时选择。
// ---------------------------------------------------------------------------
ProviderConfig configFor(const QString &vendorId, const QString &model,
                         const QString &authMode)
{
    ProviderConfig config = AppSettings::instance().providerConfig(vendorId);
    config.vendorId = vendorId;
    if (!model.isEmpty())
        config.model = model;
    if (!authMode.isEmpty())
        config.authMode = authMode;
    return config;
}

// ---------------------------------------------------------------------------
//  The short human status string shown next to the credential fields.
//  显示在凭据字段旁的简短人类可读状态。
// ---------------------------------------------------------------------------
QString computeAuthStatus(const AuthManager &auth, const VendorInfo &vendor,
                          const ProviderConfig &config, bool chinese)
{
    if (!config.authMode.isEmpty() && config.authMode.startsWith(QLatin1String("oauth"))) {
        const OAuthToken token = auth.token(vendor.id);
        if (token.isValid() && !token.isExpired()) {
            const QString expires =
                token.expiresAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
            return chinese ? QStringLiteral("OAuth 令牌有效至 %1").arg(expires)
                           : QStringLiteral("OAuth token valid until %1").arg(expires);
        }
        if (auth.hasApiKey(vendor.id))
            return chinese ? QStringLiteral("已保存 API Key") : QStringLiteral("API key stored");
        return chinese ? QStringLiteral("未配置") : QStringLiteral("Not configured");
    }

    if (auth.hasApiKey(vendor.id))
        return chinese ? QStringLiteral("已保存 API Key") : QStringLiteral("API key stored");

    if (!vendor.authModes.isEmpty() && vendor.authModes.size() == 1
        && vendor.authModes.first() == AuthMode::None) {
        return chinese ? QStringLiteral("无需授权") : QStringLiteral("No authorisation required");
    }
    return chinese ? QStringLiteral("未配置") : QStringLiteral("Not configured");
}

QString statusFor(const AuthManager &auth, const QString &vendorId, const QString &model,
                  const QString &authMode, bool chinese)
{
    const VendorInfo *vendor = ProviderRegistry::instance().find(vendorId);
    if (!vendor)
        return chinese ? QStringLiteral("未选择服务商") : QStringLiteral("No vendor selected");
    return computeAuthStatus(auth, *vendor, configFor(vendorId, model, authMode), chinese);
}

// ---------------------------------------------------------------------------
//  MCP configuration <-> QVariantMap.
//  MCP 配置与 QVariantMap 互转。
// ---------------------------------------------------------------------------
McpServerConfig mcpConfigFromVariant(const QVariantMap &map)
{
    //  The canonical JSON form is reused so a hand-written map and a persisted
    //  file are parsed by exactly the same rules.
    //  复用规范的 JSON 形式，使手写的映射与持久化文件遵循完全相同的解析规则。
    const QJsonObject object = QJsonObject::fromVariantMap(map);
    return McpServerConfig::fromJson(object);
}

QVariantMap mcpConfigToVariant(const McpServerConfig &config)
{
    QVariantMap map;
    map.insert(QStringLiteral("id"), config.id);
    map.insert(QStringLiteral("name"), config.name);
    map.insert(QStringLiteral("transport"), toString(config.transport));
    map.insert(QStringLiteral("enabled"), config.enabled);
    map.insert(QStringLiteral("trusted"), config.trusted);
    map.insert(QStringLiteral("command"), config.command);
    map.insert(QStringLiteral("arguments"), config.arguments);
    map.insert(QStringLiteral("workingDirectory"), config.workingDirectory);
    map.insert(QStringLiteral("url"), config.url);
    map.insert(QStringLiteral("hasBearerToken"), !config.bearerToken.isEmpty());
    map.insert(QStringLiteral("allowedTools"), config.allowedTools);
    map.insert(QStringLiteral("blockedTools"), config.blockedTools);
    return map;
}

// ---------------------------------------------------------------------------
//  True when a JSON object carries a field whose name looks like a credential.
//  当 JSON 对象含有名字像凭据的字段时返回 true。
// ---------------------------------------------------------------------------
bool findCredentialLikeField(const QJsonObject &object, QString *fieldName)
{
    static const QStringList forbidden = {
        QStringLiteral("apikey"),   QStringLiteral("api_key"), QStringLiteral("secret"),
        QStringLiteral("token"),    QStringLiteral("password"), QStringLiteral("credential"),
        QStringLiteral("authorization"), QStringLiteral("bearertoken")};
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        const QString key = it.key().trimmed().toLower();
        for (const QString &needle : forbidden) {
            if (key.contains(needle)) {
                if (fieldName)
                    *fieldName = it.key();
                return true;
            }
        }
        if (it.value().isObject()
            && findCredentialLikeField(it.value().toObject(), fieldName)) {
            return true;
        }
    }
    return false;
}

QString roleKey(ChatRole role)
{
    return toString(role);
}

QString roleLabelEn(ChatRole role)
{
    switch (role) {
    case ChatRole::System:    return QStringLiteral("System");
    case ChatRole::User:      return QStringLiteral("You");
    case ChatRole::Assistant: return QStringLiteral("Assistant");
    case ChatRole::Tool:      return QStringLiteral("Tool");
    }
    return QStringLiteral("Assistant");
}

QString roleLabelZh(ChatRole role)
{
    switch (role) {
    case ChatRole::System:    return QStringLiteral("系统");
    case ChatRole::User:      return QStringLiteral("你");
    case ChatRole::Assistant: return QStringLiteral("助手");
    case ChatRole::Tool:      return QStringLiteral("工具");
    }
    return QStringLiteral("助手");
}

// ---------------------------------------------------------------------------
//  Resolves the OAuth client id: argument, then the vendor preset, then the
//  TCBW_OAUTH_CLIENT_ID environment variable.
//  解析 OAuth 客户端 id：参数、厂商预设，最后是 TCBW_OAUTH_CLIENT_ID 环境变量。
// ---------------------------------------------------------------------------
QString resolveOAuthClientId(const QString &clientId, const VendorInfo *vendor)
{
    QString resolved = clientId.trimmed();
    if (resolved.isEmpty() && vendor)
        resolved = vendor->oauthClientId.trimmed();
    if (resolved.isEmpty())
        resolved = qEnvironmentVariable("TCBW_OAUTH_CLIENT_ID").trimmed();
    return resolved;
}

// ---------------------------------------------------------------------------
//  A stable signature for a tool call, used to keep the approval queue free of
//  duplicates when the model re-issues the same request.
//  工具调用的稳定签名，用于在模型重复发出同一请求时避免审批队列出现重复项。
// ---------------------------------------------------------------------------
QString toolCallSignature(const ToolCall &call)
{
    return call.name
         + QLatin1Char('\u0001')
         + QString::fromUtf8(QJsonDocument(call.arguments).toJson(QJsonDocument::Compact));
}

} // namespace

// ---------------------------------------------------------------------------
//  Construction
//  构造
// ---------------------------------------------------------------------------
AiController::AiController(AppController *app, QObject *parent)
    : QObject(parent)
{
    m_network = std::make_unique<QNetworkAccessManager>();

    // --- AuthManager -------------------------------------------------------
    //  m_auth is a value member: it is default-constructed here and never
    //  heap-allocated, so its credentials live exactly as long as the controller.
    //  m_auth 是值成员：在此默认构造，绝不堆分配，因此其凭据的生命周期与控制器
    //  完全一致。
    connect(&m_auth, &AuthManager::credentialChanged, this,
            &AiController::onCredentialChanged);
    connect(&m_auth, &AuthManager::flowStarted, this,
            [this](const QString &vendorId, const QString &mode) {
                emit notice(QStringLiteral("Authorisation started for %1 (%2).")
                        .arg(vendorId, mode),
                        QStringLiteral("已开始为 %1（%2）进行授权。")
                        .arg(vendorId, mode),
                        false);
            });
    connect(&m_auth, &AuthManager::flowUserCodeReady, this,
            [this](const QString &vendorId, const QString &userCode,
                   const QString &verificationUri, int expiresInSeconds) {
                Q_UNUSED(vendorId)
                emit notice(QStringLiteral("Enter code %1 within %2 seconds at %3.")
                        .arg(userCode)
                        .arg(expiresInSeconds)
                        .arg(verificationUri),
                        QStringLiteral("请在 %2 秒内于 %3 输入代码 %1。")
                        .arg(userCode)
                        .arg(expiresInSeconds)
                        .arg(verificationUri),
                        false);
                emit userCodeReady(userCode, verificationUri);
            });
    connect(&m_auth, &AuthManager::flowSucceeded, this, [this](const QString &vendorId) {
        m_authStatus = statusFor(m_auth, m_vendorId, m_model, m_authMode, isChinese(m_app));
        persistConfig();
        emit authChanged();
        emit authorizationSucceeded(vendorId);
        emit notice(QStringLiteral("Authorisation succeeded for %1.").arg(vendorId),
                QStringLiteral("%1 授权成功。").arg(vendorId),
                false);
    });
    connect(&m_auth, &AuthManager::flowFailed, this,
            [this](const QString &vendorId, const QString &message) {
                Q_UNUSED(vendorId)
                m_authStatus = statusFor(m_auth, m_vendorId, m_model, m_authMode,
                                         isChinese(m_app));
                emit authChanged();
                emit authorizationFailed(message);
                emit notice(QStringLiteral("Authorisation failed: %1").arg(message),
                        QStringLiteral("授权失败：%1").arg(message),
                        true);
            });
    connect(&m_auth, &AuthManager::flowCancelled, this, [this](const QString &) {
        emit notice(QStringLiteral("Authorisation was cancelled."),
                QStringLiteral("授权已取消。"),
                false);
    });

    setAppController(app);
    loadConfig();
}

AiController::~AiController()
{
    //  MCP clients are raw pointers held in m_mcp, so this is where they are
    //  stopped and released; the registry is destroyed later and only drops the
    //  now dangling raw pointers without dereferencing them.
    //  MCP 客户端是保存在 m_mcp 中的裸指针，因此在此停止并释放它们；注册表稍后
    //  析构，只会丢弃这些已悬空的裸指针而不会解引用。
    for (McpClient *client : std::as_const(m_mcp)) {
        if (!client)
            continue;
        client->stop();
        delete client;
    }
    m_mcp.clear();
}

void AiController::setAppController(AppController *app)
{
    if (m_app == app)
        return;
    if (m_app)
        m_app->disconnect(this);

    m_app = app;
    if (!m_app)
        return;

    //  The document object changes on every new/open, so it is re-bound from
    //  projectChanged(); the AI configuration stored inside the project is read
    //  on each turn instead of being cached.
    //  用户每次新建 / 打开工程都会替换文档对象，因此在 projectChanged() 中重新
    //  绑定；工程内保存的 AI 配置在每轮对话时读取，而不做缓存。
    auto bindDocument = [this]() -> bool {
        ProjectDocument *document = m_app ? m_app->document() : nullptr;
        QObject *previous = property(kBoundDocumentProperty).value<QObject *>();
        if (previous == document)
            return false;
        if (previous)
            previous->disconnect(this);
        setProperty(kBoundDocumentProperty,
                    QVariant::fromValue(static_cast<QObject *>(document)));
        //  Nothing is connected here on purpose: the tool context (project
        //  pointer, active page, workspace directory and language) is rebuilt on
        //  every turn in sendMessage(), so a structural edit needs no work here.
        //  此处刻意不连接任何信号：工具上下文（工程指针、活动页面、工作目录与语言）
        //  会在每次 sendMessage() 时重建，因此结构变化在此不需要额外处理。
        return true;
    };

    bindDocument();

    connect(m_app, &AppController::projectChanged, this, [this, bindDocument]() {
        bindDocument();
        //  A different project may carry a different AI configuration, and its
        //  active page is what the AI sees, so the properties are re-announced.
        //  不同的工程可能带有不同的 AI 配置，其活动页面即 AI 所见内容，因此重新通告
        //  相关属性。
        emit authChanged();
        emit currentModelChanged();
    });
}

void AiController::setBuilderController(BuilderController *builder)
{
    m_builder = builder;
}

// ---------------------------------------------------------------------------
//  Configuration
//  配置
// ---------------------------------------------------------------------------
void AiController::loadConfig()
{
    const AppSettings &settings = AppSettings::instance();
    const ProviderRegistry &registry = ProviderRegistry::instance();

    //  The remembered vendor wins; an unknown or absent id falls back to the
    //  first registered vendor so the UI always has something to show.
    //  记住的服务商优先；未知或缺失的 id 回退到第一个已注册服务商，使界面始终
    //  有内容可显示。
    QString vendorId = settings.lastUsedVendor();
    if (!registry.contains(vendorId) && !registry.vendors().isEmpty())
        vendorId = registry.vendors().first().id;
    m_vendorId = vendorId;

    const VendorInfo *vendor = registry.find(vendorId);
    const ProviderConfig stored = settings.providerConfig(vendorId);
    m_authMode = stored.authMode.trimmed().isEmpty()
                     ? (vendor ? vendor->preferredAuth : QStringLiteral("api-key"))
                     : stored.authMode.trimmed();
    if (vendor)
        m_model = stored.model.trimmed().isEmpty() ? vendor->defaultModel
                                                   : stored.model.trimmed();
    else
        m_model = stored.model;

    if (!stored.systemPromptOverride.isEmpty()) {
        m_systemPrompt = stored.systemPromptOverride;
        m_systemPromptCustom = true;
    }

    const QString policy = settings.aiApprovalPolicy();
    if (!policy.trimmed().isEmpty())
        m_approvalPolicy = policy.trimmed();
    m_enabled = settings.aiEnabled();

    rebuildClients();
    m_authStatus = statusFor(m_auth, m_vendorId, m_model, m_authMode, isChinese(m_app));
}

void AiController::persistConfig()
{
    AppSettings &settings = AppSettings::instance();
    if (!m_vendorId.isEmpty())
        settings.setLastUsedVendor(m_vendorId);

    ProviderConfig config = m_client ? m_client->config()
                                     : settings.providerConfig(m_vendorId);
    config.vendorId = m_vendorId;
    config.model = m_model;
    config.authMode = m_authMode;
    if (m_systemPromptCustom)
        config.systemPromptOverride = m_systemPrompt;
    settings.setProviderConfig(config);
}

void AiController::rebuildClients()
{
    const ProviderRegistry &registry = ProviderRegistry::instance();
    const VendorInfo *vendor = registry.find(m_vendorId);
    if (!vendor && !registry.vendors().isEmpty()) {
        m_vendorId = registry.vendors().first().id;
        vendor = registry.find(m_vendorId);
    }
    if (!vendor) {
        //  No vendor catalogue at all: leave every client unset so every entry
        //  point degrades to a bilingual notice instead of crashing.
        //  完全没有服务商目录：不创建任何客户端，使每个入口都退化为双语提示而不是
        //  崩溃。
        m_client.reset();
        m_registry.reset();
        m_agent.reset();
        return;
    }

    ProviderConfig config = AppSettings::instance().providerConfig(m_vendorId);
    config.vendorId = m_vendorId;
    config.authMode = m_authMode;
    if (config.model.trimmed().isEmpty() && m_model.isEmpty())
        m_model = vendor->defaultModel;
    config.model = m_model.isEmpty() ? vendor->defaultModel : m_model;
    if (m_model.isEmpty())
        m_model = config.model;
    config.autoApproveSafeTools = AppSettings::instance().aiAutoApproveSafeTools();
    if (!m_systemPromptCustom && !config.systemPromptOverride.isEmpty()) {
        m_systemPrompt = config.systemPromptOverride;
        m_systemPromptCustom = true;
    }

    m_client = std::make_unique<AiClient>(m_network.get(), &m_auth);
    m_client->setVendor(*vendor);
    m_client->setConfig(config);

    m_registry = std::make_unique<AgentToolRegistry>();
    m_registry->registerBuiltinTools();
    ApprovalPolicy policy = ApprovalPolicy::AskDestructive;
    approvalPolicyFromString(m_approvalPolicy, &policy);
    m_registry->setApprovalPolicy(policy);

    //  THE APPROVAL GATE / 审批闸门
    //  This callback records the request for the QML approval strip and returns
    //  false immediately, so it never blocks the agent loop: the registry turns
    //  the refusal into a tool result and the conversation keeps flowing. The
    //  tool is executed later, and only after the user approves it. Under the
    //  default policy the AI therefore never mutates the document without
    //  explicit approval.
    //  本回调把请求记录到 QML 审批条并立即返回 false，因此绝不会阻塞智能体循环：
    //  注册表会把拒绝转换为工具结果，对话得以继续。工具稍后执行，且只在用户批准
    //  之后执行。因此在默认策略下，AI 绝不在未获明确批准时修改文档。
    m_registry->setApprovalCallback([this](const ToolCall &call, const ToolDescriptor &desc) {
        Q_UNUSED(desc)
        //  A temporarily permissive window, opened only by approveToolCall().
        //  临时放行窗口，仅由 approveToolCall() 打开。
        if (property(kGateBypassProperty).toBool())
            return true;
        //  Session-scoped memory of what the user already approved.
        //  本次会话中用户已批准内容的记忆。
        if (m_autoApproved.contains(call.name))
            return true;

        const QString signature = toolCallSignature(call);
        for (const ToolCall &pending : std::as_const(m_pending)) {
            if (pending.id == call.id || toolCallSignature(pending) == signature)
                return false;   // already waiting for the user / 已在等待用户处理
        }
        if (m_pending.size() < kMaxPendingToolCalls)
            m_pending.append(call);

        QStringList gated = property(kGatedToolCallsProperty).toStringList();
        if (!gated.contains(call.id))
            gated.append(call.id);
        //  Bounded bookkeeping: a long session must not grow this list forever.
        //  有界记录：长时间会话不得让该列表无限增长。
        while (gated.size() > kMaxPendingToolCalls)
            gated.removeFirst();
        setProperty(kGatedToolCallsProperty, gated);

        emit pendingToolCallsChanged();
        emit toolCallEvent(call.name, false,
                           bilingual(QStringLiteral("Waiting for your approval."),
                                     QStringLiteral("正在等待你的批准。")));
        return false;
    });

    //  MCP tools are attached to the fresh registry so a rebuild keeps them.
    //  MCP 工具挂接到新的注册表，使重建后仍然可用。
    for (McpClient *client : std::as_const(m_mcp)) {
        if (client)
            m_registry->attachMcpClient(client);
    }

    connect(m_registry.get(), &AgentToolRegistry::toolInvoked, this,
            [this](const QString &name, bool success) {
                Q_UNUSED(name)
                Q_UNUSED(success)
                emit toolsChanged();
            });
    connect(m_registry.get(), &AgentToolRegistry::toolDenied, this,
            [this](const QString &name, const QString &reason) {
                emit toolCallEvent(name, false, firstLine(reason));
                emit notice(bilingualEn(reason), bilingualZh(reason), true);
            });

    m_agent = std::make_unique<AgentRuntime>(m_client.get(), m_registry.get());
    const AppSettings &settings = AppSettings::instance();
    m_agent->setMaxToolIterations(settings.aiMaxToolIterations());
    m_agent->setMaxTranscriptMessages(settings.aiMaxTranscriptMessages());
    m_agent->setIncludeProjectOutline(settings.aiIncludeProjectOutline());
    m_agent->setSystemPrompt(systemPrompt());

    connect(m_agent.get(), &AgentRuntime::started, this, [this]() {
        m_streamingText.clear();
        emit streamingChanged();
        emit busyChanged();
    });
    connect(m_agent.get(), &AgentRuntime::messageAdded, this, &AiController::onTranscriptAdded);
    connect(m_agent.get(), &AgentRuntime::assistantDelta, this,
            &AiController::onAssistantDelta);
    connect(m_agent.get(), &AgentRuntime::reasoningDelta, this,
            &AiController::onReasoningDelta);
    connect(m_agent.get(), &AgentRuntime::toolCallQueued, this,
            &AiController::onToolCallQueued);
    connect(m_agent.get(), &AgentRuntime::toolCallStarted, this,
            &AiController::onToolCallStarted);
    connect(m_agent.get(), &AgentRuntime::toolCallFinished, this,
            &AiController::onToolCallFinished);
    connect(m_agent.get(), &AgentRuntime::turnFinished, this,
            &AiController::onTurnFinished);
    connect(m_agent.get(), &AgentRuntime::failed, this, &AiController::onAgentFailed);
    connect(m_agent.get(), &AgentRuntime::usageUpdated, this,
            &AiController::onUsageUpdated);
}

// ---------------------------------------------------------------------------
//  Vendors
//  服务商
// ---------------------------------------------------------------------------
QVariantMap AiController::vendorToVariant(const VendorInfo &vendor) const
{
    QVariantMap map;
    map.insert(QStringLiteral("id"), vendor.id);
    map.insert(QStringLiteral("labelEn"), vendor.labelEn);
    map.insert(QStringLiteral("labelZh"), vendor.labelZh.isEmpty() ? vendor.labelEn
                                                                  : vendor.labelZh);
    map.insert(QStringLiteral("label"),
               isChinese(m_app) && !vendor.labelZh.isEmpty() ? vendor.labelZh
                                                             : vendor.labelEn);
    map.insert(QStringLiteral("region"), vendor.region);
    map.insert(QStringLiteral("homepageUrl"), vendor.homepageUrl);
    map.insert(QStringLiteral("docsUrl"), vendor.docsUrl);
    map.insert(QStringLiteral("keysUrl"), vendor.keysUrl);
    map.insert(QStringLiteral("baseUrl"), vendor.baseUrl);
    map.insert(QStringLiteral("chatPath"), vendor.chatPath);
    map.insert(QStringLiteral("flavor"), toString(vendor.flavor));
    map.insert(QStringLiteral("defaultModel"), vendor.defaultModel);
    map.insert(QStringLiteral("modelCount"), static_cast<int>(vendor.models.size()));
    map.insert(QStringLiteral("preferredAuth"), vendor.preferredAuth);
    map.insert(QStringLiteral("supportsTools"), vendor.supportsTools);
    map.insert(QStringLiteral("supportsStreaming"), vendor.supportsStreaming);
    map.insert(QStringLiteral("supportsVision"), vendor.supportsVision);
    map.insert(QStringLiteral("supportsReasoning"), vendor.supportsReasoning);
    map.insert(QStringLiteral("isLocal"), vendor.isLocal);
    map.insert(QStringLiteral("oauthSupported"), !vendor.oauthAuthorizeUrl.isEmpty()
                                                     || !vendor.oauthDeviceCodeUrl.isEmpty());
    map.insert(QStringLiteral("oauthRequiresUserClientId"),
               vendor.oauthRequiresUserClientId);
    map.insert(QStringLiteral("keyMinLength"), vendor.keyMinLength);
    map.insert(QStringLiteral("notesEn"), vendor.notesEn);
    map.insert(QStringLiteral("notesZh"), vendor.notesZh);

    QStringList authModes;
    for (AuthMode mode : vendor.authModes)
        authModes << toString(mode);
    map.insert(QStringLiteral("authModes"), authModes);

    QVariantList models;
    for (const ModelInfo &model : vendor.models)
        models.append(modelToVariant(model));
    map.insert(QStringLiteral("models"), models);
    return map;
}

QVariantList AiController::vendors() const
{
    QVariantList list;
    for (const VendorInfo &vendor : ProviderRegistry::instance().vendors())
        list.append(vendorToVariant(vendor));
    return list;
}

QVariantList AiController::filteredVendors() const
{
    const ProviderRegistry &registry = ProviderRegistry::instance();
    const QString needle = m_vendorFilter.trimmed();
    const QString region = m_regionFilter.trimmed();

    //  The two filters are combined so the region chips and the search box can be
    //  used at the same time.
    //  两个筛选条件会合并，使地区切换与搜索框可以同时使用。
    QVector<const VendorInfo *> matches;
    if (needle.isEmpty() && region.isEmpty()) {
        for (const VendorInfo &vendor : registry.vendors())
            matches.append(&vendor);
    } else if (needle.isEmpty()) {
        matches = registry.byRegion(region);
    } else if (region.isEmpty()) {
        matches = registry.search(needle);
    } else {
        for (const VendorInfo *vendor : registry.search(needle)) {
            if (vendor && vendor->region == region)
                matches.append(vendor);
        }
    }

    QVariantList list;
    for (const VendorInfo *vendor : matches) {
        if (vendor)
            list.append(vendorToVariant(*vendor));
    }
    return list;
}

void AiController::setVendorFilter(const QString &value)
{
    if (m_vendorFilter == value)
        return;
    m_vendorFilter = value;
    emit filterChanged();
}

void AiController::setRegionFilter(const QString &value)
{
    if (m_regionFilter == value)
        return;
    m_regionFilter = value;
    emit filterChanged();
}

int AiController::vendorCount() const
{
    return ProviderRegistry::instance().vendorCount();
}

int AiController::modelCount() const
{
    return ProviderRegistry::instance().modelCount();
}

QVariantList AiController::searchVendors(const QString &needle) const
{
    QVariantList list;
    if (needle.trimmed().isEmpty())
        return list;
    for (const VendorInfo *vendor : ProviderRegistry::instance().search(needle)) {
        if (vendor)
            list.append(vendorToVariant(*vendor));
    }
    return list;
}

QVariantMap AiController::vendorById(const QString &vendorId) const
{
    if (const VendorInfo *vendor = ProviderRegistry::instance().find(vendorId.trimmed()))
        return vendorToVariant(*vendor);
    return QVariantMap();
}

QVariantList AiController::modelsWithCapability(const QString &capability) const
{
    QVariantList list;
    for (const ProviderRegistry::ModelRef &ref :
         ProviderRegistry::instance().modelsWithCapability(capabilityFromString(capability))) {
        QVariantMap map = modelToVariant(ref.model);
        map.insert(QStringLiteral("vendorId"), ref.vendorId);
        map.insert(QStringLiteral("vendorLabel"), ref.vendorLabel);
        list.append(map);
    }
    return list;
}

void AiController::setCurrentVendorId(const QString &vendorId)
{
    const QString wanted = vendorId.trimmed();
    if (wanted.isEmpty() || wanted == m_vendorId)
        return;

    const ProviderRegistry &registry = ProviderRegistry::instance();
    const VendorInfo *vendor = registry.find(wanted);
    if (!vendor) {
        //  An unknown id is ignored rather than stored, so a typo in QML cannot
        //  leave the controller pointing at nothing.
        //  未知 id 会被忽略而不保存，因此 QML 中的拼写错误不会让控制器指向空。
        emit notice(QStringLiteral("Unknown AI vendor \"%1\".").arg(wanted),
                QStringLiteral("未知的 AI 服务商 \"%1\"。").arg(wanted),
                true);
        return;
    }

    m_vendorId = wanted;
    const ProviderConfig stored = AppSettings::instance().providerConfig(wanted);
    m_model = stored.model.trimmed().isEmpty() ? vendor->defaultModel : stored.model.trimmed();
    m_authMode = stored.authMode.trimmed().isEmpty() ? vendor->preferredAuth
                                                     : stored.authMode.trimmed();
    if (!stored.systemPromptOverride.isEmpty()) {
        m_systemPrompt = stored.systemPromptOverride;
        m_systemPromptCustom = true;
    }

    rebuildClients();
    m_authStatus = statusFor(m_auth, m_vendorId, m_model, m_authMode, isChinese(m_app));
    persistConfig();

    emit currentVendorChanged();
    emit currentModelChanged();
    emit authChanged();
}

QVariantMap AiController::currentVendor() const
{
    if (const VendorInfo *vendor = ProviderRegistry::instance().find(m_vendorId))
        return vendorToVariant(*vendor);
    return QVariantMap();
}

QVariantList AiController::currentVendorModels() const
{
    QVariantList list;
    const VendorInfo *vendor = ProviderRegistry::instance().find(m_vendorId);
    if (!vendor)
        return list;
    for (const ModelInfo &model : vendor->models) {
        QVariantMap entry;
        //  AppComboBox reads `value` and `text`.
        //  AppComboBox 读取 `value` 与 `text`。
        entry.insert(QStringLiteral("value"), model.id);
        entry.insert(QStringLiteral("text"), model.label.isEmpty() ? model.id : model.label);
        QString description = describeCapabilities(model.caps);
        if (model.contextWindow > 0) {
            description += QStringLiteral("  ·  %1 ctx").arg(model.contextWindow);
        }
        entry.insert(QStringLiteral("description"), description);
        entry.insert(QStringLiteral("id"), model.id);
        entry.insert(QStringLiteral("capabilities"), describeCapabilities(model.caps));
        list.append(entry);
    }
    return list;
}

void AiController::setCurrentModel(const QString &modelId)
{
    const QString wanted = modelId.trimmed();
    if (wanted.isEmpty() || wanted == m_model)
        return;
    m_model = wanted;
    if (m_client) {
        ProviderConfig config = m_client->config();
        config.model = wanted;
        m_client->setConfig(config);
    }
    persistConfig();
    emit currentModelChanged();
}

void AiController::setAuthMode(const QString &mode)
{
    const QString key = mode.trimmed().toLower();
    if (key.isEmpty() || key == m_authMode)
        return;

    const AuthMode parsed = authModeFromString(key, AuthMode::ApiKey);
    const QString canonical = toString(parsed);
    const VendorInfo *vendor = ProviderRegistry::instance().find(m_vendorId);
    if (vendor && !vendor->authModes.isEmpty() && !vendor->supports(parsed)) {
        emit notice(QStringLiteral("%1 does not support the \"%2\" sign-in mode.")
                .arg(vendor->labelEn.isEmpty() ? vendor->id
                : vendor->labelEn, key),
                QStringLiteral("%1 不支持 \"%2\" 登录方式。")
                .arg(vendor->labelZh.isEmpty() ? vendor->id
                : vendor->labelZh, key),
                true);
        return;
    }

    m_authMode = canonical;
    if (m_client) {
        ProviderConfig config = m_client->config();
        config.authMode = canonical;
        m_client->setConfig(config);
    }
    m_authStatus = statusFor(m_auth, m_vendorId, m_model, m_authMode, isChinese(m_app));
    persistConfig();
    emit authChanged();
}

bool AiController::authorised() const
{
    const VendorInfo *vendor = ProviderRegistry::instance().find(m_vendorId);
    if (!vendor)
        return false;
    return m_auth.isAuthorised(*vendor, configFor(m_vendorId, m_model, m_authMode));
}

bool AiController::hasStoredKey() const
{
    return m_auth.hasApiKey(m_vendorId);
}

QString AiController::protectionDescription() const
{
    return SecretStore::instance().protectionDescription(isChinese(m_app));
}

QString AiController::describeBackend() const
{
    const VendorInfo *vendor = ProviderRegistry::instance().find(m_vendorId);
    if (!vendor)
        return QStringLiteral("no vendor / 未选择服务商");
    return QStringLiteral("%1 · %2 · %3")
        .arg(vendor->id, m_model.isEmpty() ? vendor->defaultModel : m_model, m_authStatus);
}

// ---------------------------------------------------------------------------
//  Credentials
//  凭据
// ---------------------------------------------------------------------------
bool AiController::saveApiKey(const QString &apiKey)
{
    const VendorInfo *vendor = ProviderRegistry::instance().find(m_vendorId);
    if (!vendor) {
        emit notice(QStringLiteral("Select an AI vendor before saving a key."),
                QStringLiteral("请先选择 AI 服务商再保存密钥。"),
                true);
        return false;
    }

    const QString trimmed = apiKey.trimmed();
    if (trimmed.isEmpty()) {
        emit notice(QStringLiteral("The API key is empty."),
                QStringLiteral("API Key 为空。"),
                true);
        return false;
    }

    QString error;
    if (!m_auth.setApiKey(m_vendorId, trimmed, &error)) {
        //  The key itself is never echoed back, in either language.
        //  无论哪种语言，都绝不回显密钥本身。
        emit notice(QStringLiteral("The API key could not be stored: %1")
                .arg(error.isEmpty() ? QStringLiteral("unknown error")
                : error),
                QStringLiteral("无法保存 API Key：%1")
                .arg(error.isEmpty() ? QStringLiteral("未知错误") : error),
                true);
        return false;
    }

    m_authMode = QStringLiteral("api-key");
    m_authStatus = statusFor(m_auth, m_vendorId, m_model, m_authMode, isChinese(m_app));
    persistConfig();
    emit authChanged();
    emit notice(QStringLiteral("The API key for %1 was stored securely.")
            .arg(vendor->labelEn.isEmpty() ? vendor->id : vendor->labelEn),
            QStringLiteral("%1 的 API Key 已安全保存。")
            .arg(vendor->labelZh.isEmpty() ? vendor->id : vendor->labelZh),
            false);
    return true;
}

bool AiController::clearApiKey()
{
    if (!m_auth.clearApiKey(m_vendorId)) {
        emit notice(QStringLiteral("There is no stored API key for this vendor."),
                QStringLiteral("该服务商没有已保存的 API Key。"),
                true);
        return false;
    }
    m_authStatus = statusFor(m_auth, m_vendorId, m_model, m_authMode, isChinese(m_app));
    emit authChanged();
    emit notice(QStringLiteral("The stored API key was deleted."),
            QStringLiteral("已删除保存的 API Key。"),
            false);
    return true;
}

QString AiController::maskStoredKey() const
{
    const QString key = m_auth.apiKey(m_vendorId);
    //  Only the first and last four characters are ever shown, and short keys are
    //  not shown at all: the mask must not be usable to reconstruct a secret.
    //  只显示前四个与后四个字符，过短的密钥完全不显示：掩码不得被用来重建机密。
    if (key.size() < 12)
        return QString();
    return key.left(4) + QStringLiteral("…") + key.right(4);
}

void AiController::startOAuthPkce(const QString &clientId, const QString &scopes)
{
    const VendorInfo *vendor = ProviderRegistry::instance().find(m_vendorId);
    if (!vendor) {
        emit notice(QStringLiteral("Select an AI vendor before signing in."),
                QStringLiteral("请先选择 AI 服务商再登录。"),
                true);
        return;
    }
    if (vendor->oauthAuthorizeUrl.isEmpty() || vendor->oauthTokenUrl.isEmpty()) {
        emit notice(
                QStringLiteral("%1 does not offer OAuth sign-in; use an API key.")
                .arg(vendor->labelEn.isEmpty() ? vendor->id : vendor->labelEn),
                QStringLiteral("%1 不提供 OAuth 登录；请使用 API Key。")
                .arg(vendor->labelZh.isEmpty() ? vendor->id : vendor->labelZh),
                true);
        return;
    }

    const QString resolved = resolveOAuthClientId(clientId, vendor);
    if (resolved.isEmpty() && vendor->oauthRequiresUserClientId) {
        emit notice(
                QStringLiteral("%1 requires your own OAuth client id. Create one, then "
                "pass it here or set TCBW_OAUTH_CLIENT_ID.")
                .arg(vendor->labelEn.isEmpty() ? vendor->id : vendor->labelEn),
                QStringLiteral("%1 需要你自己的 OAuth 客户端 id。请先创建，然后在此传入"
                "或设置环境变量 TCBW_OAUTH_CLIENT_ID。")
                .arg(vendor->labelZh.isEmpty() ? vendor->id : vendor->labelZh),
                true);
        return;
    }

    m_authMode = QStringLiteral("oauth-pkce");
    if (m_client) {
        ProviderConfig config = m_client->config();
        config.authMode = m_authMode;
        m_client->setConfig(config);
    }
    persistConfig();

    const QString scopeText = scopes.trimmed().isEmpty() ? vendor->oauthScopes : scopes.trimmed();
    const QString state = m_auth.beginPkceFlow(*vendor, resolved, scopeText,
                                               vendor->oauthRedirectUri);
    if (state.isEmpty()) {
        emit notice(QStringLiteral("Sign-in could not be started: %1")
                .arg(m_auth.lastError()),
                QStringLiteral("无法开始登录：%1").arg(m_auth.lastError()),
                true);
        return;
    }
    emit authChanged();
    emit notice(QStringLiteral("Your browser was opened to finish signing in."),
            QStringLiteral("已打开浏览器以完成登录。"),
            false);
}

void AiController::startOAuthDevice(const QString &clientId, const QString &scopes)
{
    const VendorInfo *vendor = ProviderRegistry::instance().find(m_vendorId);
    if (!vendor) {
        emit notice(QStringLiteral("Select an AI vendor before signing in."),
                QStringLiteral("请先选择 AI 服务商再登录。"),
                true);
        return;
    }
    if (vendor->oauthDeviceCodeUrl.isEmpty()) {
        emit notice(
                QStringLiteral("%1 does not offer device-code sign-in.")
                .arg(vendor->labelEn.isEmpty() ? vendor->id : vendor->labelEn),
                QStringLiteral("%1 不提供设备码登录。")
                .arg(vendor->labelZh.isEmpty() ? vendor->id : vendor->labelZh),
                true);
        return;
    }

    const QString resolved = resolveOAuthClientId(clientId, vendor);
    if (resolved.isEmpty() && vendor->oauthRequiresUserClientId) {
        emit notice(
                QStringLiteral("%1 requires your own OAuth client id. Create one, then "
                "pass it here or set TCBW_OAUTH_CLIENT_ID.")
                .arg(vendor->labelEn.isEmpty() ? vendor->id : vendor->labelEn),
                QStringLiteral("%1 需要你自己的 OAuth 客户端 id。请先创建，然后在此传入"
                "或设置环境变量 TCBW_OAUTH_CLIENT_ID。")
                .arg(vendor->labelZh.isEmpty() ? vendor->id : vendor->labelZh),
                true);
        return;
    }

    const QString scopeText = scopes.trimmed().isEmpty() ? vendor->oauthScopes : scopes.trimmed();
    QString userCode;
    QString verificationUri;
    QString error;
    if (!m_auth.beginDeviceFlow(*vendor, resolved, scopeText, &userCode, &verificationUri,
                                &error)) {
        emit authorizationFailed(error);
        emit notice(QStringLiteral("Device sign-in could not be started: %1").arg(error),
                QStringLiteral("无法开始设备码登录：%1").arg(error),
                true);
        return;
    }

    m_authMode = QStringLiteral("oauth-device");
    if (m_client) {
        ProviderConfig config = m_client->config();
        config.authMode = m_authMode;
        m_client->setConfig(config);
    }
    persistConfig();
    m_authStatus = statusFor(m_auth, m_vendorId, m_model, m_authMode, isChinese(m_app));
    emit authChanged();
    if (!userCode.isEmpty())
        emit userCodeReady(userCode, verificationUri);
}

void AiController::cancelAuthorization()
{
    m_auth.cancelFlow();
    m_authStatus = statusFor(m_auth, m_vendorId, m_model, m_authMode, isChinese(m_app));
    emit authChanged();
}

bool AiController::refreshAccessToken(const QString &clientId)
{
    const VendorInfo *vendor = ProviderRegistry::instance().find(m_vendorId);
    if (!vendor) {
        emit notice(QStringLiteral("Select an AI vendor before refreshing a token."),
                QStringLiteral("请先选择 AI 服务商再刷新令牌。"),
                true);
        return false;
    }
    const QString resolved = resolveOAuthClientId(clientId, vendor);
    if (resolved.isEmpty() && vendor->oauthRequiresUserClientId) {
        emit notice(QStringLiteral("Refreshing requires your OAuth client id."),
                QStringLiteral("刷新需要你的 OAuth 客户端 id。"),
                true);
        return false;
    }

    if (!m_auth.refreshToken(*vendor, resolved)) {
        emit notice(QStringLiteral("The token could not be refreshed: %1")
                .arg(m_auth.lastError()),
                QStringLiteral("无法刷新令牌：%1").arg(m_auth.lastError()),
                true);
        return false;
    }
    m_authStatus = statusFor(m_auth, m_vendorId, m_model, m_authMode, isChinese(m_app));
    emit authChanged();
    emit notice(QStringLiteral("The access token was refreshed."),
            QStringLiteral("访问令牌已刷新。"),
            false);
    return true;
}

void AiController::signOut()
{
    m_auth.cancelFlow();
    m_auth.clearApiKey(m_vendorId);
    m_auth.clearToken(m_vendorId);
    m_authStatus = statusFor(m_auth, m_vendorId, m_model, m_authMode, isChinese(m_app));
    emit authChanged();
    emit notice(QStringLiteral("Signed out of %1; every stored credential for it "
            "was deleted.")
            .arg(m_vendorId),
            QStringLiteral("已退出 %1；其全部已保存凭据均已删除。").arg(m_vendorId),
            false);
}

void AiController::onCredentialChanged(const QString &vendorId)
{
    if (vendorId != m_vendorId)
        return;
    m_authStatus = statusFor(m_auth, m_vendorId, m_model, m_authMode, isChinese(m_app));
    emit authChanged();
}

// ---------------------------------------------------------------------------
//  Chat
//  对话
// ---------------------------------------------------------------------------
QVariantList AiController::transcript() const
{
    const QStringList errorIds = property(kErrorToolCallsProperty).toStringList();

    QVariantList list;
    list.reserve(m_transcript.size());
    for (int index = 0; index < m_transcript.size(); ++index) {
        const ChatMessage &message = m_transcript.at(index);
        QVariantMap entry;
        entry.insert(QStringLiteral("index"), index);
        entry.insert(QStringLiteral("role"), roleKey(message.role));
        entry.insert(QStringLiteral("roleLabelEn"), roleLabelEn(message.role));
        entry.insert(QStringLiteral("roleLabelZh"), roleLabelZh(message.role));
        //  Every transcript string is redacted, so a secret pasted into the
        //  composer can never reappear in the chat, the export or a screenshot.
        //  每条会话记录字符串都经过脱敏，因此粘贴到输入框的机密绝不会再次出现在
        //  对话、导出内容或截图中。
        entry.insert(QStringLiteral("content"), redactSecrets(message.content));
        entry.insert(QStringLiteral("reasoning"), redactSecrets(message.reasoning));
        entry.insert(QStringLiteral("isStreaming"), message.streaming);
        entry.insert(QStringLiteral("toolName"), message.name);
        entry.insert(QStringLiteral("toolCallId"), message.toolCallId);
        entry.insert(QStringLiteral("isError"),
                     message.role == ChatRole::Tool && errorIds.contains(message.toolCallId));
        entry.insert(QStringLiteral("timestamp"),
                     message.timestamp.isValid()
                         ? message.timestamp.toLocalTime().toString(Qt::ISODate)
                         : QString());

        QVariantList calls;
        for (const ToolCall &call : message.toolCalls) {
            QVariantMap callMap;
            callMap.insert(QStringLiteral("id"), call.id);
            callMap.insert(QStringLiteral("name"), call.name);
            callMap.insert(QStringLiteral("argumentsJson"),
                           redactSecrets(QString::fromUtf8(
                               QJsonDocument(call.arguments).toJson(QJsonDocument::Compact))));
            calls.append(callMap);
        }
        entry.insert(QStringLiteral("toolCalls"), calls);
        list.append(entry);
    }
    return list;
}

bool AiController::busy() const
{
    return m_agent ? m_agent->isBusy() : false;
}

bool AiController::enabled() const
{
    return m_enabled;
}

void AiController::setEnabled(bool value)
{
    if (m_enabled == value)
        return;
    m_enabled = value;
    AppSettings::instance().setAiEnabled(value);
    emit enabledChanged();
    if (!value) {
        emit notice(QStringLiteral("TI-UseAgent is turned off; no request will leave "
                "this machine."),
                QStringLiteral("TI-UseAgent 已关闭；不会有任何请求离开本机。"),
                false);
    }
}

QVariantList AiController::pendingToolCalls() const
{
    QVariantList list;
    list.reserve(m_pending.size());
    for (const ToolCall &call : m_pending) {
        QVariantMap entry;
        entry.insert(QStringLiteral("id"), call.id);
        entry.insert(QStringLiteral("name"), call.name);
        entry.insert(QStringLiteral("arguments"),
                     redactSecrets(QString::fromUtf8(
                         QJsonDocument(call.arguments).toJson(QJsonDocument::Compact))));
        entry.insert(QStringLiteral("argumentsJson"),
                     redactSecrets(QString::fromUtf8(
                         QJsonDocument(call.arguments).toJson(QJsonDocument::Compact))));
        list.append(entry);
    }
    return list;
}

void AiController::setApprovalPolicy(const QString &policy)
{
    ApprovalPolicy parsed = ApprovalPolicy::AskDestructive;
    if (!approvalPolicyFromString(policy, &parsed)) {
        emit notice(QStringLiteral("Unknown approval policy \"%1\".").arg(policy),
                QStringLiteral("未知的审批策略 \"%1\"。").arg(policy),
                true);
        return;
    }

    const QString canonical = approvalPolicyToString(parsed);
    if (m_approvalPolicy == canonical && m_registry
        && m_registry->approvalPolicy() == parsed) {
        return;
    }

    m_approvalPolicy = canonical;
    if (m_registry)
        m_registry->setApprovalPolicy(parsed);
    AppSettings::instance().setAiApprovalPolicy(canonical);
    emit approvalPolicyChanged();
}

QString AiController::systemPrompt() const
{
    if (m_systemPromptCustom && !m_systemPrompt.trimmed().isEmpty())
        return m_systemPrompt;
    return defaultSystemPrompt(isChinese(m_app));
}

void AiController::setSystemPrompt(const QString &prompt)
{
    m_systemPrompt = prompt;
    m_systemPromptCustom = !prompt.trimmed().isEmpty();
    if (m_agent)
        m_agent->setSystemPrompt(systemPrompt());
    persistConfig();
}

void AiController::resetSystemPrompt()
{
    m_systemPrompt.clear();
    m_systemPromptCustom = false;
    if (m_agent)
        m_agent->setSystemPrompt(systemPrompt());
    persistConfig();
    emit notice(QStringLiteral("The default system prompt was restored."),
            QStringLiteral("已恢复默认系统提示词。"),
            false);
}

void AiController::sendMessage(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return;

    if (!m_enabled) {
        emit notice(QStringLiteral("TI-UseAgent is turned off in the settings."),
                QStringLiteral("TI-UseAgent 已在设置中关闭。"),
                true);
        return;
    }
    if (busy()) {
        //  One turn at a time: queueing would interleave tool results with the
        //  running turn, so the message is refused with an explanation.
        //  一次只处理一轮：排队会让工具结果与进行中的轮次交错，因此拒绝该消息并说明
        //  原因。
        emit notice(QStringLiteral("The assistant is still answering; wait for it to "
                "finish."),
                QStringLiteral("助手仍在回答中；请等待其完成。"),
                true);
        return;
    }
    if (!m_agent || !m_client) {
        emit notice(QStringLiteral("No AI vendor is configured; choose one in the AI "
                "settings."),
                QStringLiteral("尚未配置 AI 服务商；请在 AI 设置中选择。"),
                true);
        return;
    }

    // --- tool context ------------------------------------------------------
    ProjectDocument *document = m_app ? m_app->document() : nullptr;
    ToolContext &context = m_agent->toolContext();
    context.project = document;
    context.activePageId = m_app ? m_app->currentPageId() : QString();
    context.chinese = isChinese(m_app);
    QString workspace;
    if (document && !document->filePath.trimmed().isEmpty())
        workspace = QFileInfo(document->filePath).absolutePath();
    if (workspace.isEmpty())
        workspace = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    context.workspaceDirectory = workspace;

    const AppSettings &settings = AppSettings::instance();
    m_agent->setIncludeProjectOutline(settings.aiIncludeProjectOutline());
    m_agent->setMaxToolIterations(settings.aiMaxToolIterations());
    m_agent->setSystemPrompt(systemPrompt());

    // --- the project may override parts of the provider configuration -------
    // --- 工程可以覆盖服务商配置的某些部分 --------------------------------
    if (document && !document->aiConfig.isEmpty()) {
        const ProviderConfig projectConfig = ProviderConfig::fromJson(document->aiConfig);
        if (projectConfig.vendorId.isEmpty() || projectConfig.vendorId == m_vendorId) {
            ProviderConfig live = m_client->config();
            const QJsonObject raw = document->aiConfig;
            if (raw.contains(QStringLiteral("model")) && !projectConfig.model.isEmpty())
                live.model = projectConfig.model;
            if (raw.contains(QStringLiteral("systemPromptOverride")))
                live.systemPromptOverride = projectConfig.systemPromptOverride;
            if (raw.contains(QStringLiteral("baseUrlOverride")))
                live.baseUrlOverride = projectConfig.baseUrlOverride;
            if (raw.contains(QStringLiteral("temperature")))
                live.temperature = projectConfig.temperature;
            if (raw.contains(QStringLiteral("maxTokens")))
                live.maxTokens = projectConfig.maxTokens;
            if (raw.contains(QStringLiteral("streaming")))
                live.streaming = projectConfig.streaming;
            if (raw.contains(QStringLiteral("toolsEnabled")))
                live.toolsEnabled = projectConfig.toolsEnabled;
            live.vendorId = m_vendorId;
            live.authMode = m_authMode;
            m_client->setConfig(live);
        }
    }

    // --- secret redaction --------------------------------------------------
    QString outgoing = trimmed;
    if (settings.redactSecretsInTranscripts()) {
        const QString redacted = redactSecrets(trimmed);
        if (redacted != trimmed) {
            outgoing = redacted;
            emit notice(
                    QStringLiteral("A secret was detected in your message and removed "
                    "before sending."),
                    QStringLiteral("在你的消息中检测到机密，已在发送前移除。"),
                    true);
        }
    }

    m_streamingText.clear();
    emit streamingChanged();
    m_agent->sendUserMessage(outgoing);
}

void AiController::cancelResponse()
{
    if (!m_agent)
        return;
    m_agent->cancel();
}

void AiController::clearTranscript()
{
    m_transcript.clear();
    m_pending.clear();
    m_streamingText.clear();
    m_autoApproved.clear();
    setProperty(kErrorToolCallsProperty, QStringList());
    setProperty(kGatedToolCallsProperty, QStringList());
    if (m_agent)
        m_agent->reset();

    emit transcriptChanged();
    emit streamingChanged();
    emit pendingToolCallsChanged();
    emit busyChanged();
    emit usageChanged();
}

void AiController::retryLastMessage()
{
    if (!m_agent)
        return;
    if (busy()) {
        emit notice(QStringLiteral("Wait for the current answer before retrying."),
                QStringLiteral("请等待当前回答完成后再重试。"),
                true);
        return;
    }
    m_agent->retryLast();
}

void AiController::stopAfterCurrentTurn()
{
    //  Best effort: AgentRuntime exposes no "finish this turn, then stop" switch,
    //  so the tool round-trip budget is squeezed to its minimum. The current
    //  reply still completes and no further tool round trip is started; the
    //  configured budget is restored at the end of the turn.
    //  尽力而为：AgentRuntime 没有提供"完成本轮后停止"的开关，因此把工具往返预算
    //  压到最小值。当前回答仍会完成，且不会再发起工具往返；回合结束后恢复配置的
    //  预算。
    if (m_agent)
        m_agent->setMaxToolIterations(1);
    emit notice(QStringLiteral("The assistant will stop after this reply."),
            QStringLiteral("助手将在本次回答后停止。"),
            false);
}

// ---------------------------------------------------------------------------
//  Approval gate
//  审批闸门
// ---------------------------------------------------------------------------
void AiController::approveToolCall(const QString &toolCallId)
{
    int index = -1;
    for (int i = 0; i < m_pending.size(); ++i) {
        if (m_pending.at(i).id == toolCallId) {
            index = i;
            break;
        }
    }
    if (index < 0) {
        emit notice(QStringLiteral("That request is no longer waiting for approval."),
                QStringLiteral("该请求已不在等待批准。"),
                false);
        return;
    }

    const ToolCall call = m_pending.at(index);
    m_pending.removeAt(index);
    emit pendingToolCallsChanged();

    if (!m_registry || !m_agent || !m_client) {
        emit notice(QStringLiteral("The tool registry is not available."),
                QStringLiteral("工具注册表不可用。"),
                true);
        return;
    }

    //  Remember the approval for this session: if the model re-issues the same
    //  tool the user is not asked a second time.
    //  记住本次会话的批准：若模型再次发出同一工具，不会二次询问用户。
    if (!m_autoApproved.contains(call.name))
        m_autoApproved.append(call.name);

    const ApprovalPolicy previousPolicy = m_registry->approvalPolicy();

    if (!m_agent->isBusy()) {
        //  Preferred path: the runtime is idle, so the last user turn is replayed
        //  with the approval in place. The tool then really executes inside the
        //  registry, the runtime appends its ChatMessage::tool result to the
        //  model's transcript itself, and the conversation continues with the
        //  result — exactly the behaviour of a normal turn, and the tool runs
        //  exactly once.
        //  首选路径：运行时处于空闲，因此在已批准的状态下重放最后一轮用户消息。
        //  工具随后真正在注册表内执行，运行时自行把 ChatMessage::tool 结果追加到
        //  模型会话记录，对话带着该结果继续——与正常轮次完全一致，且工具只执行一次。
        m_registry->setApprovalPolicy(ApprovalPolicy::SafeOnly);
        m_agent->retryLast();
        emit toolCallEvent(call.name, true,
                           bilingual(QStringLiteral("Approved; running now."),
                                     QStringLiteral("已批准；正在执行。")));
        return;
    }

    //  The runtime is mid-turn and the frozen header offers no "resume" API, so
    //  the approved call is executed synchronously through the same registry
    //  with a temporarily permissive policy, and its result is appended to the
    //  visible transcript here. The model's own context still holds the earlier
    //  refusal; the user's next message is what continues the conversation. The
    //  tool still runs exactly once.
    //  运行时正在轮次中，而冻结的头文件没有提供"继续"接口，因此已批准的调用会以
    //  临时放行策略通过同一注册表同步执行，其结果在此追加到可见的会话记录。模型
    //  自身的上下文仍保存着先前的拒绝记录；继续对话依靠用户的下一条消息。工具
    //  依然只执行一次。
    m_registry->setApprovalPolicy(ApprovalPolicy::SafeOnly);
    setProperty(kGateBypassProperty, true);
    ToolContext &context = m_agent->toolContext();
    ToolResult result = m_registry->invoke(call, context);
    setProperty(kGateBypassProperty, false);
    m_registry->setApprovalPolicy(previousPolicy);

    if (result.toolCallId.isEmpty())
        result.toolCallId = call.id;
    if (result.isError) {
        QStringList errorIds = property(kErrorToolCallsProperty).toStringList();
        if (!errorIds.contains(call.id))
            errorIds.append(call.id);
        setProperty(kErrorToolCallsProperty, errorIds);
    }

    emit toolCallEvent(call.name, !result.isError, firstLine(result.content));
    onTranscriptAdded(ChatMessage::tool(call.id, call.name, result.content, result.isError));
    emit notice(
            QStringLiteral("The approved tool %1 ran. Send your next message to "
            "continue.")
            .arg(call.name),
            QStringLiteral("已批准的工具 %1 已执行。发送下一条消息以继续。")
            .arg(call.name),
            false);
}

void AiController::rejectToolCall(const QString &toolCallId)
{
    int index = -1;
    for (int i = 0; i < m_pending.size(); ++i) {
        if (m_pending.at(i).id == toolCallId) {
            index = i;
            break;
        }
    }
    if (index < 0)
        return;

    const ToolCall call = m_pending.at(index);
    m_pending.removeAt(index);
    emit pendingToolCallsChanged();

    QStringList errorIds = property(kErrorToolCallsProperty).toStringList();
    if (!errorIds.contains(call.id))
        errorIds.append(call.id);
    setProperty(kErrorToolCallsProperty, errorIds);

    emit toolCallEvent(call.name, false,
                       bilingual(QStringLiteral("Declined."), QStringLiteral("已拒绝。")));
    onTranscriptAdded(ChatMessage::tool(
        call.id, call.name,
        QStringLiteral("The user declined this action. / 用户拒绝了此操作。"), true));
    emit notice(QStringLiteral("The request was declined; the project was not "
            "changed."),
            QStringLiteral("已拒绝该请求；工程未发生任何变更。"),
            false);
}

void AiController::approveAllPending()
{
    const QVector<ToolCall> queued = m_pending;
    for (const ToolCall &call : queued)
        approveToolCall(call.id);
}

void AiController::rejectAllPending()
{
    const QVector<ToolCall> queued = m_pending;
    for (const ToolCall &call : queued)
        rejectToolCall(call.id);
}

// ---------------------------------------------------------------------------
//  AgentRuntime slots
//  AgentRuntime 槽
// ---------------------------------------------------------------------------
void AiController::onTranscriptAdded(const ChatMessage &message)
{
    ChatMessage stored = message;
    if (!stored.timestamp.isValid())
        stored.timestamp = QDateTime::currentDateTimeUtc();
    m_transcript.append(stored);
    while (m_transcript.size() > kMaxTranscriptEntries)
        m_transcript.removeFirst();

    emit transcriptChanged();
    //  A message added by the model ends the streaming bubble.
    //  由模型追加的消息意味着流式气泡结束。
    if (stored.role != ChatRole::Assistant || !stored.streaming) {
        m_streamingText.clear();
        emit streamingChanged();
    }
    emit usageChanged();
}

void AiController::onAssistantDelta(const QString &delta)
{
    if (delta.isEmpty())
        return;
    //  Deltas are redacted as they arrive so a partially typed secret is masked
    //  in the live bubble as well.
    //  增量到达时即脱敏，使正在输入的机密在实时气泡中也被遮蔽。
    m_streamingText += redactSecrets(delta);
    emit streamingChanged();
}

void AiController::onReasoningDelta(const QString &delta)
{
    //  Chain-of-thought is rendered from the finished message (transcript()),
    //  not token by token, so the deltas only keep the UI aware of progress.
    //  思维链由完成后的消息（transcript()）渲染，而不是逐令牌渲染，因此这里的
    //  增量只让界面知道仍在进行中。
    if (!delta.isEmpty())
        emit streamingChanged();
}

void AiController::onToolCallQueued(const ToolCall &call)
{
    if (!m_registry)
        return;
    if (m_autoApproved.contains(call.name) || m_registry->isAutoApproved(call.name))
        return;

    const QString signature = toolCallSignature(call);
    for (const ToolCall &pending : std::as_const(m_pending)) {
        if (pending.id == call.id || toolCallSignature(pending) == signature)
            return;
    }
    if (m_pending.size() < kMaxPendingToolCalls)
        m_pending.append(call);
    emit pendingToolCallsChanged();
}

void AiController::onToolCallStarted(const ToolCall &call)
{
    //  A call that was auto-approved needs no approval card; one that was gated
    //  keeps its card so the user can still decide.
    //  自动批准过的调用不需要审批卡片；被闸门拦下的调用保留卡片，以便用户仍可决定。
    const QStringList gated = property(kGatedToolCallsProperty).toStringList();
    if (gated.contains(call.id))
        return;
    for (int i = 0; i < m_pending.size(); ++i) {
        if (m_pending.at(i).id == call.id) {
            m_pending.removeAt(i);
            emit pendingToolCallsChanged();
            return;
        }
    }
}

void AiController::onToolCallFinished(const ToolCall &call, const ToolResult &result)
{
    if (result.isError) {
        QStringList errorIds = property(kErrorToolCallsProperty).toStringList();
        if (!errorIds.contains(call.id))
            errorIds.append(call.id);
        setProperty(kErrorToolCallsProperty, errorIds);
    }
    emit toolCallEvent(call.name, !result.isError, firstLine(result.content));

    const QStringList gated = property(kGatedToolCallsProperty).toStringList();
    if (gated.contains(call.id))
        return;   // still waiting for the user / 仍在等待用户
    for (int i = 0; i < m_pending.size(); ++i) {
        if (m_pending.at(i).id == call.id) {
            m_pending.removeAt(i);
            emit pendingToolCallsChanged();
            return;
        }
    }
}

void AiController::onTurnFinished(bool ok, const QString &errorText)
{
    m_streamingText.clear();
    emit streamingChanged();
    emit busyChanged();

    //  Restore the configured budget and the user's approval policy after the
    //  temporarily permissive window opened by approveToolCall().
    //  在 approveToolCall() 打开的临时放行窗口之后，恢复配置的预算与用户的审批策略。
    if (m_agent)
        m_agent->setMaxToolIterations(AppSettings::instance().aiMaxToolIterations());
    if (m_registry) {
        ApprovalPolicy policy = ApprovalPolicy::AskDestructive;
        approvalPolicyFromString(m_approvalPolicy, &policy);
        m_registry->setApprovalPolicy(policy);
    }

    if (!ok && !errorText.trimmed().isEmpty() && !errorText.startsWith(QLatin1String("Cancelled")))
        emit notice(bilingualEn(errorText), bilingualZh(errorText), true);
    emit usageChanged();
}

void AiController::onAgentFailed(const AiError &error)
{
    const QString en = error.messageEn.isEmpty() ? error.text(false) : error.messageEn;
    const QString zh = error.messageZh.isEmpty() ? error.text(true) : error.messageZh;
    m_streamingText.clear();
    emit streamingChanged();
    emit busyChanged();
    emit notice(en, zh, true);
}

void AiController::onUsageUpdated(int in, int out)
{
    Q_UNUSED(in)
    Q_UNUSED(out)
    emit usageChanged();
}

// ---------------------------------------------------------------------------
//  MCP servers
//  MCP 服务器
// ---------------------------------------------------------------------------
QVariantList AiController::mcpServers() const
{
    QVariantList list;
    const QVector<McpServerConfig> servers = AppSettings::instance().mcpServers();
    list.reserve(servers.size());
    for (const McpServerConfig &server : servers) {
        QVariantMap entry = mcpConfigToVariant(server);
        const McpClient *client = m_mcp.value(server.id, nullptr);
        entry.insert(QStringLiteral("running"), client ? client->isRunning() : false);
        entry.insert(QStringLiteral("initialised"), client ? client->isInitialised() : false);
        entry.insert(QStringLiteral("toolCount"),
                     client ? static_cast<int>(client->tools().size()) : 0);
        list.append(entry);
    }
    return list;
}

QVariantMap AiController::addMcpServer(const QVariantMap &config)
{
    McpServerConfig server = mcpConfigFromVariant(config);
    if (server.id.trimmed().isEmpty())
        server.id = IdGenerator::next(QStringLiteral("mcp"));
    if (server.name.trimmed().isEmpty())
        server.name = server.id;
    server.enabled = config.contains(QStringLiteral("enabled"))
                         ? config.value(QStringLiteral("enabled")).toBool()
                         : true;

    //  A NEW SERVER IS NEVER TRUSTED IMPLICITLY: starting an MCP server spawns a
    //  local program, so McpClient::start refuses an untrusted configuration and
    //  the user has to opt in explicitly.
    //  新建服务器绝不自动受信任：启动 MCP 服务器会派生本地程序，因此
    //  McpClient::start 会拒绝未受信任的配置，必须由用户明确同意。
    server.trusted = false;

    QVector<McpServerConfig> servers = AppSettings::instance().mcpServers();
    bool replaced = false;
    for (McpServerConfig &existing : servers) {
        if (existing.id == server.id) {
            existing = server;
            replaced = true;
            break;
        }
    }
    if (!replaced)
        servers.append(server);
    AppSettings::instance().setMcpServers(servers);

    emit mcpServersChanged();
    emit notice(
            QStringLiteral("The MCP server \"%1\" was added untrusted. Mark it trusted "
            "before it can be started.")
            .arg(server.name),
            QStringLiteral("MCP 服务器 \"%1\" 已添加为不受信任。请先标记为受信任，"
            "之后才能启动它。")
            .arg(server.name),
            false);
    return mcpConfigToVariant(server);
}

bool AiController::updateMcpServer(const QVariantMap &config)
{
    const QString id = config.value(QStringLiteral("id")).toString().trimmed();
    if (id.isEmpty()) {
        emit notice(QStringLiteral("The MCP server has no id."),
                QStringLiteral("该 MCP 服务器没有 id。"),
                true);
        return false;
    }

    QVector<McpServerConfig> servers = AppSettings::instance().mcpServers();
    for (McpServerConfig &existing : servers) {
        if (existing.id != id)
            continue;

        McpServerConfig updated = mcpConfigFromVariant(config);
        updated.id = id;
        if (updated.name.trimmed().isEmpty())
            updated.name = existing.name;
        //  Trust is an explicit user decision and is never imported from a map
        //  that merely round-tripped through the UI.
        //  信任是用户的明确决定，绝不从一个仅经由界面往返的映射导入。
        updated.trusted = config.contains(QStringLiteral("trusted"))
                              ? config.value(QStringLiteral("trusted")).toBool()
                              : existing.trusted;
        if (!config.contains(QStringLiteral("enabled")))
            updated.enabled = existing.enabled;
        if (updated.bearerToken.isEmpty())
            updated.bearerToken = existing.bearerToken;

        existing = updated;
        AppSettings::instance().setMcpServers(servers);
        emit mcpServersChanged();
        return true;
    }

    emit notice(QStringLiteral("No MCP server with id \"%1\" exists.").arg(id),
            QStringLiteral("不存在 id 为 \"%1\" 的 MCP 服务器。").arg(id),
            true);
    return false;
}

bool AiController::removeMcpServer(const QString &serverId)
{
    const QString id = serverId.trimmed();
    QVector<McpServerConfig> servers = AppSettings::instance().mcpServers();
    bool found = false;
    for (int i = 0; i < servers.size(); ++i) {
        if (servers.at(i).id == id) {
            servers.removeAt(i);
            found = true;
            break;
        }
    }
    if (!found)
        return false;

    //  Stop and release the client before forgetting the configuration, so an
    //  orphaned child process can never survive the removal.
    //  在遗忘配置之前停止并释放客户端，使孤儿子进程绝不会在删除后继续存活。
    disconnectMcpServer(id);
    AppSettings::instance().setMcpServers(servers);
    emit mcpServersChanged();
    emit notice(QStringLiteral("The MCP server \"%1\" was removed.").arg(id),
            QStringLiteral("已移除 MCP 服务器 \"%1\"。").arg(id),
            false);
    return true;
}

bool AiController::setMcpServerEnabled(const QString &serverId, bool enabled)
{
    const QString id = serverId.trimmed();
    QVector<McpServerConfig> servers = AppSettings::instance().mcpServers();
    for (McpServerConfig &server : servers) {
        if (server.id != id)
            continue;
        if (server.enabled == enabled)
            return true;
        server.enabled = enabled;
        AppSettings::instance().setMcpServers(servers);
        if (!enabled)
            disconnectMcpServer(id);
        emit mcpServersChanged();
        emit notice(QStringLiteral("The MCP server \"%1\" was %2.")
                .arg(id, enabled ? QStringLiteral("enabled")
                : QStringLiteral("disabled")),
                QStringLiteral("MCP 服务器 \"%1\" 已%2。")
                .arg(id, enabled ? QStringLiteral("启用")
                : QStringLiteral("停用")),
                false);
        return true;
    }
    return false;
}

bool AiController::setMcpServerTrusted(const QString &serverId, bool trusted)
{
    const QString id = serverId.trimmed();
    QVector<McpServerConfig> servers = AppSettings::instance().mcpServers();
    for (McpServerConfig &server : servers) {
        if (server.id != id)
            continue;
        server.trusted = trusted;
        AppSettings::instance().setMcpServers(servers);
        if (!trusted)
            disconnectMcpServer(id);
        emit mcpServersChanged();
        emit notice(
                trusted ? QStringLiteral("\"%1\" is now trusted; starting it will run "
                "its configured program.")
                .arg(id)
                : QStringLiteral("\"%1\" is no longer trusted and cannot be "
                "started.")
                .arg(id),
                trusted ? QStringLiteral("\"%1\" 现已受信任；启动它会运行其配置的程序。")
                .arg(id)
                : QStringLiteral("\"%1\" 不再受信任，无法启动。").arg(id),
                !trusted);
        return true;
    }
    return false;
}

void AiController::connectMcpServer(const QString &serverId)
{
    const QString id = serverId.trimmed();
    if (id.isEmpty())
        return;
    if (m_mcp.contains(id)) {
        emit notice(QStringLiteral("The MCP server is already connected."),
                QStringLiteral("该 MCP 服务器已连接。"),
                false);
        return;
    }

    McpServerConfig config;
    bool found = false;
    for (const McpServerConfig &candidate : AppSettings::instance().mcpServers()) {
        if (candidate.id == id) {
            config = candidate;
            found = true;
            break;
        }
    }
    if (!found) {
        emit notice(QStringLiteral("No MCP server with id \"%1\" exists.").arg(id),
                QStringLiteral("不存在 id 为 \"%1\" 的 MCP 服务器。").arg(id),
                true);
        return;
    }
    if (!config.enabled) {
        emit notice(QStringLiteral("The MCP server \"%1\" is disabled.").arg(id),
                QStringLiteral("MCP 服务器 \"%1\" 已停用。").arg(id),
                true);
        return;
    }

    auto *client = new McpClient(config);
    m_mcp.insert(id, client);

    //  The registry refreshes its descriptor table whenever the server lists its
    //  tools, and every failure surfaces to the user as a notice.
    //  服务器每次列举工具时注册表都会刷新描述符表，每次失败都会以提示告知用户。
    connect(client, &McpClient::toolsListed, this, [this](const QVector<ToolDescriptor> &) {
        emit toolsChanged();
        emit mcpServersChanged();
    });
    connect(client, &McpClient::initialised, this, [this](const QJsonObject &) {
        emit mcpServersChanged();
    });
    connect(client, &McpClient::stopped, this, [this]() { emit mcpServersChanged(); });
    connect(client, &McpClient::errorOccurred, this, [this](const QString &message) {
        emit notice(QStringLiteral("MCP error: %1").arg(message),
                QStringLiteral("MCP 错误：%1").arg(message),
                true);
        emit mcpServersChanged();
    });
    if (m_registry)
        m_registry->attachMcpClient(client);

    QString error;
    const bool started = client->start(&error);
    if (!started) {
        if (!config.trusted) {
            //  McpClient refuses an untrusted configuration itself; the notice
            //  explains why rather than repeating its own wording twice.
            //  McpClient 自身会拒绝未受信任的配置；此提示解释原因，而不是重复它的
            //  措辞两次。
            emit notice(
                    QStringLiteral("The MCP server \"%1\" was not started because it is not "
                    "trusted. Review the command it runs and mark it trusted first.")
                    .arg(id),
                    QStringLiteral("MCP 服务器 \"%1\" 未启动，因为它未受信任。请先确认它将运行的"
                    "命令并标记为受信任。")
                    .arg(id),
                    true);
        } else {
            const QString reason = error.isEmpty() ? client->lastError() : error;
            emit notice(
                    QStringLiteral("The MCP server \"%1\" could not be started: %2")
                    .arg(id, reason.isEmpty() ? QStringLiteral("unknown error") : reason),
                    QStringLiteral("无法启动 MCP 服务器 \"%1\"：%2")
                    .arg(id, reason.isEmpty() ? QStringLiteral("未知错误") : reason),
                    true);
        }
    } else {
        emit notice(QStringLiteral("The MCP server \"%1\" is starting.").arg(id),
                QStringLiteral("MCP 服务器 \"%1\" 正在启动。").arg(id),
                false);
    }
    emit mcpServersChanged();
    emit toolsChanged();
}

void AiController::disconnectMcpServer(const QString &serverId)
{
    const QString id = serverId.trimmed();
    McpClient *client = m_mcp.take(id);
    if (!client)
        return;

    client->stop();

    //  The frozen AgentToolRegistry header exposes no detachMcpClient(), and it
    //  stores raw McpClient pointers. Deleting the client would therefore leave a
    //  dangling entry behind, so the descriptor table is rebuilt after the
    //  deletion and the remaining servers are re-attached. The honest trade-off
    //  is that the model's own transcript restarts; the visible transcript kept
    //  by this controller is not affected.
    //  冻结的 AgentToolRegistry 头文件没有提供 detachMcpClient()，而且它保存的是
    //  McpClient 裸指针。因此直接删除客户端会留下悬空条目：删除后重建描述符表，
    //  并重新挂接其余服务器。诚实的代价是模型自身的会话记录会重新开始；本控制器
    //  保存的可见会话记录不受影响。
    delete client;
    rebuildClients();

    emit mcpServersChanged();
    emit toolsChanged();
    emit notice(QStringLiteral("The MCP server \"%1\" was disconnected.").arg(id),
            QStringLiteral("已断开 MCP 服务器 \"%1\"。").arg(id),
            false);
}

QVariantMap AiController::mcpServerStatus(const QString &serverId) const
{
    const QString id = serverId.trimmed();
    QVariantMap status;
    status.insert(QStringLiteral("id"), id);

    McpServerConfig config;
    bool found = false;
    for (const McpServerConfig &candidate : AppSettings::instance().mcpServers()) {
        if (candidate.id == id) {
            config = candidate;
            found = true;
            break;
        }
    }
    status.insert(QStringLiteral("name"), found ? config.name : QString());
    status.insert(QStringLiteral("transport"), found ? toString(config.transport) : QString());
    status.insert(QStringLiteral("enabled"), found ? config.enabled : false);
    status.insert(QStringLiteral("trusted"), found ? config.trusted : false);

    const McpClient *client = m_mcp.value(id, nullptr);
    status.insert(QStringLiteral("running"), client ? client->isRunning() : false);
    status.insert(QStringLiteral("initialised"), client ? client->isInitialised() : false);
    status.insert(QStringLiteral("serverName"), client ? client->serverName() : QString());
    status.insert(QStringLiteral("serverVersion"), client ? client->serverVersion() : QString());
    status.insert(QStringLiteral("toolCount"),
                  client ? static_cast<int>(client->tools().size()) : 0);
    status.insert(QStringLiteral("lastError"), client ? client->lastError() : QString());
    return status;
}

QVariantList AiController::mcpServerTools(const QString &serverId) const
{
    QVariantList list;
    const McpClient *client = m_mcp.value(serverId.trimmed(), nullptr);
    if (!client)
        return list;
    for (const ToolDescriptor &tool : client->tools())
        list.append(toolToVariant(tool));
    return list;
}

// ---------------------------------------------------------------------------
//  Tools
//  工具
// ---------------------------------------------------------------------------
QVariantMap AiController::toolToVariant(const ToolDescriptor &tool) const
{
    QVariantMap map;
    map.insert(QStringLiteral("name"), tool.name);
    map.insert(QStringLiteral("titleEn"), tool.titleEn.isEmpty() ? tool.name : tool.titleEn);
    map.insert(QStringLiteral("titleZh"), tool.titleZh.isEmpty()
                                             ? (tool.titleEn.isEmpty() ? tool.name
                                                                       : tool.titleEn)
                                             : tool.titleZh);
    map.insert(QStringLiteral("descriptionEn"), tool.descriptionEn);
    map.insert(QStringLiteral("descriptionZh"), tool.descriptionZh.isEmpty()
                                                    ? tool.descriptionEn
                                                    : tool.descriptionZh);
    map.insert(QStringLiteral("category"), tool.category);
    map.insert(QStringLiteral("mutatesProject"), tool.mutatesProject);
    map.insert(QStringLiteral("destructive"), tool.destructive);
    map.insert(QStringLiteral("requiresNetwork"), tool.requiresNetwork);
    map.insert(QStringLiteral("schemaJson"),
               QString::fromUtf8(QJsonDocument(tool.inputSchema).toJson(QJsonDocument::Compact)));
    map.insert(QStringLiteral("autoApproved"),
               (m_registry && m_registry->isAutoApproved(tool.name))
                   || m_autoApproved.contains(tool.name));
    return map;
}

QVariantList AiController::availableTools() const
{
    QVariantList list;
    if (!m_registry)
        return list;
    for (const ToolDescriptor &tool : m_registry->descriptors())
        list.append(toolToVariant(tool));
    return list;
}

QVariantMap AiController::toolCatalog() const
{
    const QVariantList tools = availableTools();
    QStringList categories;
    for (const QVariant &entry : tools) {
        const QString category = entry.toMap().value(QStringLiteral("category")).toString();
        if (!category.isEmpty() && !categories.contains(category))
            categories.append(category);
    }
    categories.sort();

    QVariantMap map;
    map.insert(QStringLiteral("count"), static_cast<int>(tools.size()));
    map.insert(QStringLiteral("categories"), categories);
    map.insert(QStringLiteral("tools"), tools);
    return map;
}

int AiController::inputTokens() const
{
    return m_agent ? m_agent->totalInputTokens() : 0;
}

int AiController::outputTokens() const
{
    return m_agent ? m_agent->totalOutputTokens() : 0;
}

int AiController::estimatedContextTokens() const
{
    return m_agent ? m_agent->estimatedContextTokens() : 0;
}

// ---------------------------------------------------------------------------
//  Transcript export
//  会话记录导出
// ---------------------------------------------------------------------------
QString AiController::exportTranscriptMarkdown() const
{
    const VendorInfo *vendor = ProviderRegistry::instance().find(m_vendorId);
    const QString vendorLabel = vendor
                                    ? (isChinese(m_app) && !vendor->labelZh.isEmpty()
                                           ? vendor->labelZh
                                           : (vendor->labelEn.isEmpty() ? vendor->id
                                                                        : vendor->labelEn))
                                    : QStringLiteral("(none / 无)");

    QString out;
    out += QStringLiteral("# TCTools-Build_Web (TC-BuildWeb) — TI-UseAgent transcript\n");
    out += QStringLiteral("# TCTools-Build_Web（TC-BuildWeb）—— TI-UseAgent 会话记录\n\n");
    out += QStringLiteral("- Application / 应用程序: TCTools-Build_Web %1 (%2)\n")
               .arg(versionString(), versionChannel());
    out += QStringLiteral("- Vendor / 服务商: %1\n").arg(redactSecrets(vendorLabel));
    out += QStringLiteral("- Model / 模型: %1\n").arg(redactSecrets(m_model));
    out += QStringLiteral("- Exported / 导出时间: %1\n")
               .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    out += QStringLiteral("- Messages / 消息数: %1\n\n").arg(m_transcript.size());
    out += QStringLiteral("> Secrets are removed from this file when the privacy setting is on.\n");
    out += QStringLiteral("> 当隐私设置开启时，本文件中的机密会被移除。\n\n");

    const QStringList errorIds = property(kErrorToolCallsProperty).toStringList();
    for (int index = 0; index < m_transcript.size(); ++index) {
        const ChatMessage &message = m_transcript.at(index);
        QString content = redactSecrets(message.content);
        if (content.size() > kMaxMarkdownSectionChars)
            content = content.left(kMaxMarkdownSectionChars) + QStringLiteral("\n… [truncated]");

        out += QStringLiteral("## %1. %2\n\n").arg(index + 1).arg(roleLabelEn(message.role));
        if (!content.isEmpty()) {
            out += QStringLiteral("```text\n%1\n```\n\n").arg(content);
        }
        if (!message.reasoning.trimmed().isEmpty()) {
            QString reasoning = redactSecrets(message.reasoning);
            if (reasoning.size() > kMaxMarkdownSectionChars)
                reasoning = reasoning.left(kMaxMarkdownSectionChars)
                          + QStringLiteral("\n… [truncated]");
            out += QStringLiteral("### Reasoning / 思维链\n\n```text\n%1\n```\n\n")
                       .arg(reasoning);
        }
        if (message.role == ChatRole::Tool) {
            out += QStringLiteral("- Tool / 工具: `%1`\n")
                       .arg(redactSecrets(message.name));
            out += QStringLiteral("- Result / 结果: %1\n\n")
                       .arg(errorIds.contains(message.toolCallId) ? QStringLiteral("error")
                                                                  : QStringLiteral("ok"));
        }
        for (const ToolCall &call : message.toolCalls) {
            out += QStringLiteral("### Tool call / 工具调用: `%1`\n\n")
                       .arg(redactSecrets(call.name));
            out += QStringLiteral("```json\n%1\n```\n\n")
                       .arg(redactSecrets(QString::fromUtf8(
                           QJsonDocument(call.arguments).toJson(QJsonDocument::Indented))));
        }
    }
    return out;
}

bool AiController::saveTranscript(const QString &filePath) const
{
    //  This method is const, so failures cannot be announced through notice();
    //  they are reported through the application error channel instead, which is
    //  what the shell renders in its status bar.
    //  本方法为 const，因此无法通过 notice() 通告失败；改为通过应用程序错误通道
    //  报告，界面会在状态栏中呈现它。
    const auto fail = [this](const QString &en, const QString &zh) {
        const QString message = bilingual(en, zh);
        if (m_app)
            m_app->reportError(message);
        else
            qWarning("%s", qUtf8Printable(message));
        return false;
    };

    const QString target = filePath.trimmed();
    if (target.isEmpty())
        return fail(QStringLiteral("No transcript path was given."),
                    QStringLiteral("未提供会话记录路径。"));

    const QFileInfo info(target);
    const QString directory = info.absolutePath();
    if (!directory.isEmpty() && !QDir().mkpath(directory)) {
        return fail(QStringLiteral("The directory %1 could not be created.").arg(directory),
                    QStringLiteral("无法创建目录 %1。").arg(directory));
    }

    QSaveFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return fail(QStringLiteral("The transcript could not be written: %1")
                        .arg(file.errorString()),
                    QStringLiteral("无法写入会话记录：%1").arg(file.errorString()));
    }

    const QByteArray payload = exportTranscriptMarkdown().toUtf8();
    if (file.write(payload) != payload.size()) {
        file.cancelWriting();
        return fail(QStringLiteral("The transcript could not be written completely: %1")
                        .arg(file.errorString()),
                    QStringLiteral("会话记录未能完整写入：%1").arg(file.errorString()));
    }
    if (!file.commit()) {
        return fail(QStringLiteral("The transcript could not be saved: %1").arg(file.errorString()),
                    QStringLiteral("无法保存会话记录：%1").arg(file.errorString()));
    }
    return true;
}

// ---------------------------------------------------------------------------
//  Provider configuration exchange
//  服务商配置的导入导出
// ---------------------------------------------------------------------------
QString AiController::providerJson() const
{
    //  ProviderConfig contains only public settings: no key, token or secret is
    //  ever part of it, which is why it can be exported verbatim.
    //  ProviderConfig 只包含公开设置：其中绝无密钥、令牌或机密，因此可以原样导出。
    ProviderConfig config = m_client ? m_client->config()
                                     : AppSettings::instance().providerConfig(m_vendorId);
    config.vendorId = m_vendorId;
    config.model = m_model;
    config.authMode = m_authMode;
    return QString::fromUtf8(QJsonDocument(config.toJson()).toJson(QJsonDocument::Indented));
}

bool AiController::importProviderJson(const QString &json)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (document.isNull() || !document.isObject()) {
        emit notice(
                QStringLiteral("The provider configuration is not valid JSON: %1")
                .arg(parseError.errorString()),
                QStringLiteral("服务商配置不是合法的 JSON：%1").arg(parseError.errorString()),
                true);
        return false;
    }

    const QJsonObject object = document.object();
    QString secretField;
    if (findCredentialLikeField(object, &secretField)) {
        //  Refusing here is a deliberate safety rail: a pasted provider file must
        //  never carry a credential into the application.
        //  在此拒绝是刻意的安全护栏：粘贴的服务商文件绝不能把凭据带入应用程序。
        emit notice(
                QStringLiteral("The configuration contains the credential-like field "
                "\"%1\"; remove it and store the secret in the credential "
                "store instead.")
                .arg(secretField),
                QStringLiteral("配置中含有类似凭据的字段 \"%1\"；请移除它，并改为把机密"
                "保存到凭据存储中。")
                .arg(secretField),
                true);
        return false;
    }

    const ProviderConfig config = ProviderConfig::fromJson(object);
    const ProviderRegistry &registry = ProviderRegistry::instance();
    if (config.vendorId.trimmed().isEmpty() || !registry.contains(config.vendorId.trimmed())) {
        emit notice(
                QStringLiteral("The configuration names the unknown vendor \"%1\".")
                .arg(config.vendorId),
                QStringLiteral("配置中的服务商 \"%1\" 未知。").arg(config.vendorId),
                true);
        return false;
    }

    m_vendorId = config.vendorId.trimmed();
    const VendorInfo *vendor = registry.find(m_vendorId);
    m_model = config.model.trimmed().isEmpty() ? (vendor ? vendor->defaultModel : QString())
                                               : config.model.trimmed();
    m_authMode = config.authMode.trimmed().isEmpty() ? QStringLiteral("api-key")
                                                     : config.authMode.trimmed();
    if (!config.systemPromptOverride.isEmpty()) {
        m_systemPrompt = config.systemPromptOverride;
        m_systemPromptCustom = true;
    }

    rebuildClients();
    persistConfig();
    m_authStatus = statusFor(m_auth, m_vendorId, m_model, m_authMode, isChinese(m_app));

    emit currentVendorChanged();
    emit currentModelChanged();
    emit authChanged();
    emit notice(QStringLiteral("The provider configuration was imported."),
            QStringLiteral("服务商配置已导入。"),
            false);
    return true;
}

} // namespace tcbw
