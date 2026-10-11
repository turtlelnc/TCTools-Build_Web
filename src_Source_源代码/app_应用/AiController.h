// ============================================================================
//  AiController.h -- TI-UseAgent behind QML
//  AiController.h —— QML 背后的 TI-UseAgent
// ============================================================================
#pragma once

#include "AgentRuntime.h"
#include "AgentTools.h"
#include "AiProvider.h"
#include "AiTypes.h"
#include "AuthManager.h"
#include "McpClient.h"
#include "ProviderRegistry.h"
#include "SecretStore.h"
#include "Types.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

QT_BEGIN_NAMESPACE
class QNetworkAccessManager;
QT_END_NAMESPACE

namespace tcbw {

class AppController;
class BuilderController;

// ---------------------------------------------------------------------------
//  AiController
//  The QML facade for TI-UseAgent: vendor selection, credential entry, the
//  OAuth dances, MCP server management, the chat transcript and the approval
//  gate for mutating tools.
//  TI-UseAgent 的 QML 门面：厂商选择、凭据输入、OAuth 流程、MCP 服务器管理、
//  对话记录，以及变更类工具的审批闸门。
// ---------------------------------------------------------------------------
class AiController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QVariantList vendors READ vendors NOTIFY vendorsChanged)
    Q_PROPERTY(QVariantList filteredVendors READ filteredVendors NOTIFY filterChanged)
    Q_PROPERTY(QString vendorFilter READ vendorFilter WRITE setVendorFilter NOTIFY filterChanged)
    Q_PROPERTY(QString regionFilter READ regionFilter WRITE setRegionFilter NOTIFY filterChanged)
    Q_PROPERTY(int vendorCount READ vendorCount NOTIFY vendorsChanged)
    Q_PROPERTY(int modelCount READ modelCount NOTIFY vendorsChanged)

    Q_PROPERTY(QString currentVendorId READ currentVendorId WRITE setCurrentVendorId NOTIFY currentVendorChanged)
    Q_PROPERTY(QVariantMap currentVendor READ currentVendor NOTIFY currentVendorChanged)
    Q_PROPERTY(QVariantList currentVendorModels READ currentVendorModels NOTIFY currentVendorChanged)
    Q_PROPERTY(QString currentModel READ currentModel WRITE setCurrentModel NOTIFY currentModelChanged)
    Q_PROPERTY(QString authMode READ authMode WRITE setAuthMode NOTIFY authChanged)
    Q_PROPERTY(bool authorised READ authorised NOTIFY authChanged)
    Q_PROPERTY(bool hasStoredKey READ hasStoredKey NOTIFY authChanged)
    Q_PROPERTY(QString authStatus READ authStatus NOTIFY authChanged)
    Q_PROPERTY(QString protectionDescription READ protectionDescription CONSTANT)

    Q_PROPERTY(QVariantList transcript READ transcript NOTIFY transcriptChanged)
    Q_PROPERTY(QString streamingText READ streamingText NOTIFY streamingChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(QVariantList pendingToolCalls READ pendingToolCalls NOTIFY pendingToolCallsChanged)
    Q_PROPERTY(QString approvalPolicy READ approvalPolicy WRITE setApprovalPolicy NOTIFY approvalPolicyChanged)

    Q_PROPERTY(QVariantList mcpServers READ mcpServers NOTIFY mcpServersChanged)
    Q_PROPERTY(QVariantList availableTools READ availableTools NOTIFY toolsChanged)
    Q_PROPERTY(int inputTokens READ inputTokens NOTIFY usageChanged)
    Q_PROPERTY(int outputTokens READ outputTokens NOTIFY usageChanged)
    Q_PROPERTY(int estimatedContextTokens READ estimatedContextTokens NOTIFY usageChanged)

public:
    explicit AiController(AppController *app, QObject *parent = nullptr);
    ~AiController() override;

    void setAppController(AppController *app);
    void setBuilderController(BuilderController *builder);

    // --- vendors -----------------------------------------------------------
    QVariantList vendors() const;
    QVariantList filteredVendors() const;
    QString vendorFilter() const { return m_vendorFilter; }
    void setVendorFilter(const QString &value);
    QString regionFilter() const { return m_regionFilter; }
    void setRegionFilter(const QString &value);
    int vendorCount() const;
    int modelCount() const;

    QString currentVendorId() const { return m_vendorId; }
    void setCurrentVendorId(const QString &vendorId);
    QVariantMap currentVendor() const;
    QVariantList currentVendorModels() const;
    QString currentModel() const { return m_model; }
    void setCurrentModel(const QString &modelId);
    QString authMode() const { return m_authMode; }
    void setAuthMode(const QString &mode);
    bool authorised() const;
    bool hasStoredKey() const;
    QString authStatus() const { return m_authStatus; }
    QString protectionDescription() const;

    // --- chat --------------------------------------------------------------
    QVariantList transcript() const;
    QString streamingText() const { return m_streamingText; }
    bool busy() const;
    bool enabled() const;
    void setEnabled(bool value);
    QVariantList pendingToolCalls() const;
    QString approvalPolicy() const { return m_approvalPolicy; }
    void setApprovalPolicy(const QString &policy);

    // --- mcp ---------------------------------------------------------------
    QVariantList mcpServers() const;
    QVariantList availableTools() const;
    int inputTokens() const;
    int outputTokens() const;
    int estimatedContextTokens() const;

    // -----------------------------------------------------------------------
    //  Invokable API
    //  可调用接口
    // -----------------------------------------------------------------------
    Q_INVOKABLE QVariantList searchVendors(const QString &needle) const;
    Q_INVOKABLE QVariantMap vendorById(const QString &vendorId) const;
    Q_INVOKABLE QVariantList modelsWithCapability(const QString &capability) const;

    Q_INVOKABLE bool saveApiKey(const QString &apiKey);
    Q_INVOKABLE bool clearApiKey();
    Q_INVOKABLE QString maskStoredKey() const;

    Q_INVOKABLE void startOAuthPkce(const QString &clientId = QString(),
                                    const QString &scopes = QString());
    Q_INVOKABLE void startOAuthDevice(const QString &clientId = QString(),
                                      const QString &scopes = QString());
    Q_INVOKABLE void cancelAuthorization();
    Q_INVOKABLE bool refreshAccessToken(const QString &clientId = QString());
    Q_INVOKABLE void signOut();

    Q_INVOKABLE void sendMessage(const QString &text);
    Q_INVOKABLE void cancelResponse();
    Q_INVOKABLE void clearTranscript();
    Q_INVOKABLE void retryLastMessage();
    Q_INVOKABLE void stopAfterCurrentTurn();

    Q_INVOKABLE void approveToolCall(const QString &toolCallId);
    Q_INVOKABLE void rejectToolCall(const QString &toolCallId);
    Q_INVOKABLE void approveAllPending();
    Q_INVOKABLE void rejectAllPending();

    Q_INVOKABLE QVariantMap addMcpServer(const QVariantMap &config);
    Q_INVOKABLE bool updateMcpServer(const QVariantMap &config);
    Q_INVOKABLE bool removeMcpServer(const QString &serverId);
    Q_INVOKABLE bool setMcpServerEnabled(const QString &serverId, bool enabled);
    Q_INVOKABLE bool setMcpServerTrusted(const QString &serverId, bool trusted);
    Q_INVOKABLE void connectMcpServer(const QString &serverId);
    Q_INVOKABLE void disconnectMcpServer(const QString &serverId);
    Q_INVOKABLE QVariantMap mcpServerStatus(const QString &serverId) const;
    Q_INVOKABLE QVariantList mcpServerTools(const QString &serverId) const;

    Q_INVOKABLE QString systemPrompt() const;
    Q_INVOKABLE void setSystemPrompt(const QString &prompt);
    Q_INVOKABLE void resetSystemPrompt();

    Q_INVOKABLE QVariantMap toolCatalog() const;
    Q_INVOKABLE QString exportTranscriptMarkdown() const;
    Q_INVOKABLE bool saveTranscript(const QString &filePath) const;

    Q_INVOKABLE QString providerJson() const;
    Q_INVOKABLE bool importProviderJson(const QString &json);

signals:
    void vendorsChanged();
    void filterChanged();
    void currentVendorChanged();
    void currentModelChanged();
    void authChanged();
    void transcriptChanged();
    void streamingChanged();
    void busyChanged();
    void enabledChanged();
    void pendingToolCallsChanged();
    void approvalPolicyChanged();
    void mcpServersChanged();
    void toolsChanged();
    void usageChanged();
    void toolCallEvent(const QString &toolName, bool success, const QString &summary);
    void notice(const QString &messageEn, const QString &messageZh, bool isError);
    void userCodeReady(const QString &userCode, const QString &verificationUri);
    void authorizationSucceeded(const QString &vendorId);
    void authorizationFailed(const QString &message);

private slots:
    void onTranscriptAdded(const ChatMessage &message);
    void onAssistantDelta(const QString &delta);
    void onReasoningDelta(const QString &delta);
    void onToolCallQueued(const ToolCall &call);
    void onToolCallStarted(const ToolCall &call);
    void onToolCallFinished(const ToolCall &call, const ToolResult &result);
    void onTurnFinished(bool ok, const QString &errorText);
    void onAgentFailed(const AiError &error);
    void onUsageUpdated(int in, int out);
    void onCredentialChanged(const QString &vendorId);

private:
    void rebuildClients();
    void persistConfig();
    void loadConfig();
    QVariantMap vendorToVariant(const VendorInfo &vendor) const;
    QVariantMap toolToVariant(const ToolDescriptor &tool) const;
    QString describeBackend() const;

    AppController *m_app = nullptr;
    BuilderController *m_builder = nullptr;

    std::unique_ptr<QNetworkAccessManager> m_network;
    AuthManager m_auth;
    std::unique_ptr<AiClient> m_client;
    std::unique_ptr<AgentToolRegistry> m_registry;
    std::unique_ptr<AgentRuntime> m_agent;
    QHash<QString, McpClient *> m_mcp;

    QString m_vendorId;
    QString m_model;
    QString m_authMode = QStringLiteral("api-key");
    QString m_authStatus;
    QString m_vendorFilter;
    QString m_regionFilter;
    QString m_streamingText;
    QString m_approvalPolicy = QStringLiteral("ask-destructive");
    QVector<ChatMessage> m_transcript;
    QVector<ToolCall> m_pending;
    QStringList m_autoApproved;
    bool m_enabled = true;
    bool m_systemPromptCustom = false;
    QString m_systemPrompt;
};

} // namespace tcbw
