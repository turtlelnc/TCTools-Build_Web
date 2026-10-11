// ============================================================================
//  BackendController.h -- endpoints, scaffolds and the live preview server
//  BackendController.h —— 端点、脚手架与实时预览服务器
// ============================================================================
#pragma once

#include "BackendConnector.h"
#include "BackendScaffold.h"
#include "Endpoint.h"
#include "PreviewServer.h"
#include "Types.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

namespace tcbw {

class AppController;

// ---------------------------------------------------------------------------
//  BackendController
//  Lets the user define the server endpoints the page will call, test them
//  from inside the builder, scaffold a matching server project, and preview
//  the generated site in a real browser through the loopback preview server.
//  让用户定义页面将调用的服务器端点，在构建器内部测试它们，生成配套的服务器
//  工程，并通过回环预览服务器在真实浏览器中预览生成的站点。
// ---------------------------------------------------------------------------
class BackendController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QVariantList endpoints READ endpoints NOTIFY endpointsChanged)
    Q_PROPERTY(QVariantList backendTargets READ backendTargets NOTIFY targetsChanged)
    Q_PROPERTY(QVariantList scaffoldPlan READ scaffoldPlan NOTIFY scaffoldPlanChanged)
    Q_PROPERTY(QString scaffoldTargetId READ scaffoldTargetId WRITE setScaffoldTargetId NOTIFY scaffoldPlanChanged)
    Q_PROPERTY(QVariantMap lastTestResult READ lastTestResult NOTIFY testResultChanged)
    Q_PROPERTY(bool testing READ testing NOTIFY testingChanged)

    Q_PROPERTY(bool previewRunning READ previewRunning NOTIFY previewChanged)
    Q_PROPERTY(QString previewUrl READ previewUrl NOTIFY previewChanged)
    Q_PROPERTY(int previewClients READ previewClients NOTIFY previewClientsChanged)
    Q_PROPERTY(QString previewStatus READ previewStatus NOTIFY previewChanged)

public:
    explicit BackendController(AppController *app, QObject *parent = nullptr);
    ~BackendController() override;

    void setAppController(AppController *app);
    void setScaffoldOutputDirectory(const QString &directory);

    QVariantList endpoints() const;
    QVariantList backendTargets() const;
    QVariantList scaffoldPlan() const;
    QString scaffoldTargetId() const { return m_scaffoldTargetId; }
    void setScaffoldTargetId(const QString &targetId);
    QVariantMap lastTestResult() const { return m_lastTestResult; }
    bool testing() const { return m_testing; }

    bool previewRunning() const;
    QString previewUrl() const;
    int previewClients() const;
    QString previewStatus() const { return m_previewStatus; }

    // -----------------------------------------------------------------------
    //  Invokable API
    //  可调用接口
    // -----------------------------------------------------------------------
    Q_INVOKABLE QVariantMap addEndpoint(const QVariantMap &endpoint);
    Q_INVOKABLE bool updateEndpoint(const QVariantMap &endpoint);
    Q_INVOKABLE bool removeEndpoint(const QString &endpointId);
    Q_INVOKABLE bool duplicateEndpoint(const QString &endpointId);
    Q_INVOKABLE QVariantMap endpointById(const QString &endpointId) const;
    Q_INVOKABLE QVariantList endpointsReferencingBlock(const QString &blockType) const;
    Q_INVOKABLE QVariantMap emptyEndpointTemplate() const;
    Q_INVOKABLE bool validateEndpoint(const QVariantMap &endpoint, bool reportErrors = true);

    Q_INVOKABLE QString testEndpoint(const QString &endpointId,
                                     const QVariantMap &variables = {});
    Q_INVOKABLE QString probeBackend(const QString &targetId, const QString &baseUrl);
    Q_INVOKABLE void cancelTests();

    Q_INVOKABLE QVariantList scaffoldOptions() const;
    Q_INVOKABLE QVariantMap plannedScaffoldFiles(const QString &targetId = QString()) const;
    Q_INVOKABLE QString scaffoldBackend(const QString &targetId,
                                        const QString &outputDirectory,
                                        const QVariantMap &options = {});
    Q_INVOKABLE QString exportScaffoldToDialog(const QString &targetId);

    Q_INVOKABLE bool startPreview();
    Q_INVOKABLE void stopPreview();
    Q_INVOKABLE void refreshPreviewFiles();
    Q_INVOKABLE void openPreviewInBrowser();
    Q_INVOKABLE QString previewUrlFor(const QString &relativePath) const;
    Q_INVOKABLE QString copyPreviewUrl() const;

    Q_INVOKABLE QVariantList generatedFileList(const QString &mode = QString()) const;
    Q_INVOKABLE QString generatedFileContents(const QString &relativePath,
                                              const QString &mode = QString()) const;

signals:
    void endpointsChanged();
    void targetsChanged();
    void scaffoldPlanChanged();
    void testResultChanged();
    void testingChanged();
    void previewChanged();
    void previewClientsChanged();
    void notice(const QString &messageEn, const QString &messageZh, bool isError);

private slots:
    void onTestFinished(const QString &requestId, const EndpointTestResult &result);
    void onHealthProbe(const QString &requestId, bool ok, const QString &detail);
    void onPreviewStarted(quint16 port);
    void onPreviewStopped();

private:
    EndpointStore &store();
    const EndpointStore &store() const;
    void persistEndpoints();
    QVariantMap endpointToVariant(const Endpoint &endpoint) const;
    Endpoint variantToEndpoint(const QVariantMap &map) const;
    EmitResult currentBuild(const QString &mode) const;

    AppController *m_app = nullptr;
    BackendConnector m_connector;
    PreviewServer m_preview;
    BackendScaffold m_scaffold;

    QString m_scaffoldTargetId = QStringLiteral("node-express");
    QString m_scaffoldOutputDirectory;
    QVariantMap m_lastTestResult;
    QString m_pendingTestId;
    QString m_pendingHealthId;
    QString m_previewStatus;
    bool m_testing = false;
};

} // namespace tcbw
