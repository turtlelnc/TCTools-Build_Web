// ============================================================================
//  tst_backend.cpp -- endpoints, scaffolding and the loopback preview server
//  tst_backend.cpp —— 端点、脚手架与回环预览服务器
// ============================================================================
#include "BackendConnector.h"
#include "BackendScaffold.h"
#include "Endpoint.h"
#include "PreviewServer.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace tcbw;

class TestBackend : public QObject
{
    Q_OBJECT

private slots:
    // --- endpoints ---------------------------------------------------------
    void endpointValidationEnforcesTransportSecurity()
    {
        Endpoint endpoint;
        endpoint.name = QStringLiteral("Test");
        endpoint.url = QStringLiteral("https://api.example.com/items");
        QVERIFY(endpoint.isValid());

        //  A plaintext URL is fine while no credential is attached...
        //  未附加凭据时，明文地址是可接受的……
        Endpoint insecure;
        insecure.url = QStringLiteral("http://api.example.com/items");
        QVERIFY(insecure.isValid());

        //  ...but must be refused once the request carries a credential.
        //  ……但一旦请求携带凭据，就必须被拒绝。
        insecure.auth.enabled = true;
        insecure.auth.mode = AuthMode::Bearer;
        QVERIFY(!insecure.isValid());

        //  Loopback is the documented exception for local development.
        //  回环地址是本地开发的文档化例外。
        Endpoint loopback;
        loopback.url = QStringLiteral("http://127.0.0.1:8080/items");
        loopback.auth.enabled = true;
        loopback.auth.mode = AuthMode::Bearer;
        QVERIFY(loopback.isValid());

        //  Embedded credentials in the URL are never acceptable.
        //  URL 中内嵌的凭据永远不可接受。
        Endpoint embedded;
        embedded.url = QStringLiteral("https://user:pass@api.example.com/x");
        QVERIFY(!embedded.isValid());

        Endpoint badScheme;
        badScheme.url = QStringLiteral("file:///etc/passwd");
        QVERIFY(!badScheme.isValid());

        Endpoint badTimeout;
        badTimeout.url = QStringLiteral("https://api.example.com/x");
        badTimeout.timeoutMs = 10;
        QVERIFY(!badTimeout.isValid());
    }

    void templateExpansionIsSinglePassAndEncoded()
    {
        const QMap<QString, QString> variables = {
            {QStringLiteral("id"), QStringLiteral("a b")},
            {QStringLiteral("q"), QStringLiteral("x{{id}}y")}
        };

        //  URL context percent-encodes the value.
        //  URL 上下文会对值做百分号编码。
        const QString url = EndpointStore::expandTemplate(
            QStringLiteral("/items/{{id}}"), variables, true);
        QCOMPARE(url, QStringLiteral("/items/a%20b"));

        //  The substituted value is not re-scanned, so an injected placeholder
        //  stays literal instead of expanding.
        //  替换后的值不会被再次扫描，因此注入的占位符保持字面量而不会被展开。
        const QString nested = EndpointStore::expandTemplate(
            QStringLiteral("/q/{{q}}"), variables, true);
        QVERIFY(nested.contains(QStringLiteral("%7B%7Bid%7D%7D"))
                || nested.contains(QStringLiteral("{{id}}")));
        QVERIFY(!nested.contains(QStringLiteral("/q/xa%20by")));

        //  Body context JSON-escapes instead.
        //  正文上下文改为做 JSON 转义。
        const QString body = EndpointStore::expandTemplate(
            QStringLiteral("{\"name\":\"{{id}}\"}"), variables, false);
        QVERIFY(body.contains(QStringLiteral("a b")));

        //  Unknown placeholders are preserved verbatim.
        //  未知占位符原样保留。
        QCOMPARE(EndpointStore::expandTemplate(QStringLiteral("/{{nope}}"), variables, true),
                 QStringLiteral("/{{nope}}"));
    }

    void endpointStoreCrudPreservesOrder()
    {
        EndpointStore store;
        QCOMPARE(store.count(), 0);

        Endpoint first;
        first.id = QStringLiteral("ep-first");
        first.name = QStringLiteral("First");
        first.url = QStringLiteral("https://api.example.com/a");
        Endpoint &addedFirst = store.add(first);
        QVERIFY(!addedFirst.id.isEmpty());
        QCOMPARE(addedFirst.id, QStringLiteral("ep-first"));

        Endpoint second;
        second.id = QStringLiteral("ep-second");
        second.name = QStringLiteral("Second");
        second.url = QStringLiteral("https://api.example.com/b");
        store.add(second);

        QCOMPARE(store.count(), 2);
        QCOMPARE(store.endpoints().at(0).name, QStringLiteral("First"));
        QCOMPARE(store.endpoints().at(1).name, QStringLiteral("Second"));

        QVERIFY(store.find(addedFirst.id) != nullptr);
        QVERIFY(store.findByName(QStringLiteral("Second")) != nullptr);
        QVERIFY(store.findByName(QStringLiteral("Nope")) == nullptr);

        //  A change of referenced blocks must be discoverable, because deleting
        //  an endpoint that a block calls would silently break the page.
        //  必须能发现被积木引用的关系，因为删除被积木调用的端点会悄悄破坏页面。
        Endpoint bound = addedFirst;
        bound.boundBlockTypes = {QStringLiteral("backend.callEndpoint")};
        QVERIFY(store.update(bound));
        QVERIFY(!store.referencingBlock(QStringLiteral("backend.callEndpoint")).isEmpty());

        QVERIFY(store.remove(QStringLiteral("ep-second")));
        QCOMPARE(store.count(), 1);
        QVERIFY(!store.remove(QStringLiteral("not-an-id")));
    }

    void endpointStoreRoundTripsThroughJson()
    {
        EndpointStore store;
        Endpoint endpoint;
        endpoint.name = QStringLiteral("Round trip");
        endpoint.method = QStringLiteral("POST");
        endpoint.url = QStringLiteral("https://api.example.com/v1/items");
        endpoint.headers.insert(QStringLiteral("X-Trace"), QStringLiteral("abc"));
        endpoint.timeoutMs = 45000;
        store.add(endpoint);

        const EndpointStore restored = EndpointStore::fromJson(store.toJson());
        QCOMPARE(restored.count(), 1);
        QCOMPARE(restored.endpoints().first().method, QStringLiteral("POST"));
        QCOMPARE(restored.endpoints().first().headers.value(QStringLiteral("X-Trace")),
                 QStringLiteral("abc"));
        QCOMPARE(restored.endpoints().first().timeoutMs, 45000);
    }

    // --- connector safety --------------------------------------------------
    void connectorRefusesUnsafeTestUrls()
    {
        QString reasonEn;
        QString reasonZh;

        QVERIFY(BackendConnector::isSafeTestUrl(QUrl(QStringLiteral("https://api.example.com")),
                                                &reasonEn, &reasonZh));
        QVERIFY(BackendConnector::isSafeTestUrl(QUrl(QStringLiteral("http://127.0.0.1:3000/x")),
                                                &reasonEn, &reasonZh));

        //  Cloud metadata endpoints are the classic SSRF target.
        //  云元数据端点是典型的 SSRF 目标。
        QVERIFY(!BackendConnector::isSafeTestUrl(
            QUrl(QStringLiteral("http://169.254.169.254/latest/meta-data/")), &reasonEn, &reasonZh));
        QVERIFY(!reasonEn.isEmpty());
        QVERIFY(!reasonZh.isEmpty());

        QVERIFY(!BackendConnector::isSafeTestUrl(QUrl(QStringLiteral("file:///etc/passwd")),
                                                 nullptr, nullptr));
        QVERIFY(!BackendConnector::isSafeTestUrl(QUrl(QStringLiteral("ftp://example.com")),
                                                 nullptr, nullptr));
        QVERIFY(!BackendConnector::isSafeTestUrl(
            QUrl(QStringLiteral("https://user:pass@example.com")), nullptr, nullptr));
    }

    void crossOriginRedirectDetectionIsExact()
    {
        const QUrl origin(QStringLiteral("https://api.example.com/v1/items"));

        QVERIFY(!BackendConnector::isCrossOriginRedirect(
            origin, QUrl(QStringLiteral("https://api.example.com/v2/items"))));
        QVERIFY(BackendConnector::isCrossOriginRedirect(
            origin, QUrl(QStringLiteral("https://evil.example.net/v1/items"))));
        QVERIFY(BackendConnector::isCrossOriginRedirect(
            origin, QUrl(QStringLiteral("http://api.example.com/v1/items"))));
        QVERIFY(BackendConnector::isCrossOriginRedirect(
            origin, QUrl(QStringLiteral("https://api.example.com:8443/v1/items"))));
    }

    void insecureCredentialUrlsAreFlagged()
    {
        QVERIFY(BackendConnector::isInsecureCredentialUrl(
            QUrl(QStringLiteral("http://api.example.com/x")), true));
        QVERIFY(!BackendConnector::isInsecureCredentialUrl(
            QUrl(QStringLiteral("https://api.example.com/x")), true));
        QVERIFY(!BackendConnector::isInsecureCredentialUrl(
            QUrl(QStringLiteral("http://api.example.com/x")), false));
        //  `isInsecureCredentialUrl()` reports the literal transport fact, so a
        //  loopback plaintext URL is still "insecure" by this predicate. The
        //  loopback exemption for local development lives in
        //  BackendConnector::test(), which is where a request is actually sent.
        //  `isInsecureCredentialUrl()` 报告的是字面意义上的传输事实，因此回环明文
        //  地址在本谓词下仍算"不安全"。面向本地开发的回环豁免位于实际发起请求的
        //  BackendConnector::test() 中。
        QVERIFY(BackendConnector::isInsecureCredentialUrl(
            QUrl(QStringLiteral("http://127.0.0.1:3000/x")), true));
    }

    void backendTargetsAreSubstantial()
    {
        QVERIFY2(BackendConnector::targets().size() >= 10,
                 qPrintable(QStringLiteral("only %1 targets")
                                .arg(BackendConnector::targets().size())));

        QSet<QString> ids;
        for (const BackendTarget &target : BackendConnector::targets()) {
            QVERIFY(!target.id.isEmpty());
            QVERIFY2(!ids.contains(target.id), qPrintable(target.id));
            ids.insert(target.id);
            QVERIFY2(!target.labelEn.isEmpty(), qPrintable(target.id));
            QVERIFY2(!target.labelZh.isEmpty(), qPrintable(target.id));
            QVERIFY2(!target.language.isEmpty(), qPrintable(target.id));
            QVERIFY2(!target.defaultBaseUrl.isEmpty(), qPrintable(target.id));
        }

        QVERIFY(BackendConnector::findTarget(QStringLiteral("node-express")) != nullptr);
        QVERIFY(BackendConnector::findTarget(QStringLiteral("python-fastapi")) != nullptr);
        QVERIFY(BackendConnector::findTarget(QStringLiteral("nope")) == nullptr);
    }

    // --- scaffolding -------------------------------------------------------
    void scaffoldGeneratesARunnableProject()
    {
        BackendScaffold scaffold;

        ScaffoldRequest request;
        request.targetId = QStringLiteral("node-express");
        request.projectName = QStringLiteral("demo-backend");
        request.includeCors = true;
        request.includeReadme = true;
        request.includeEnvExample = true;
        request.includeDocker = true;

        Endpoint endpoint;
        endpoint.name = QStringLiteral("Items");
        endpoint.method = QStringLiteral("GET");
        endpoint.url = QStringLiteral("https://api.example.com/v1/items");
        request.endpoints.add(endpoint);

        const EmitResult result = scaffold.generate(request);
        QVERIFY2(result.ok, qPrintable(result.errors.join(QLatin1String("; "))));
        QVERIFY(!result.files.isEmpty());

        QStringList paths;
        for (const EmittedFile &file : result.files)
            paths.append(file.relativePath);

        QVERIFY(paths.contains(QStringLiteral("package.json")));
        QVERIFY(paths.contains(QStringLiteral("server.js")) || paths.contains(QStringLiteral("index.js")));
        QVERIFY(paths.contains(QStringLiteral("README.md")));

        //  A scaffold must never ship a real secret.
        for (const EmittedFile &file : result.files) {
            QVERIFY2(!file.contents.contains(QStringLiteral("sk-")),
                     qPrintable(file.relativePath));
            QVERIFY2(!file.contents.contains(QStringLiteral("password=")),
                     qPrintable(file.relativePath));
        }

        //  The planned file list must match what generate() actually produces.
        const QStringList planned = scaffold.plannedFiles(request);
        QCOMPARE(planned.size(), paths.size());
        for (const QString &path : planned)
            QVERIFY2(paths.contains(path), qPrintable(path));
    }

    void scaffoldCoversEveryAdvertisedTarget()
    {
        BackendScaffold scaffold;
        for (const BackendTarget &target : BackendConnector::targets()) {
            ScaffoldRequest request;
            request.targetId = target.id;

            const EmitResult result = scaffold.generate(request);
            QVERIFY2(result.ok, qPrintable(target.id));
            QVERIFY2(!result.files.isEmpty(), qPrintable(target.id));
        }
    }

    void scaffoldFallsBackForUnknownTarget()
    {
        BackendScaffold scaffold;
        ScaffoldRequest request;
        request.targetId = QStringLiteral("no-such-platform");

        const EmitResult result = scaffold.generate(request);
        //  An unknown target must degrade to something usable and say so.
        //  未知目标必须降级为可用结果，并明确说明。
        QVERIFY(result.ok);
        QVERIFY(!result.files.isEmpty());
        QVERIFY(!result.warnings.isEmpty());
    }

    // --- preview server ----------------------------------------------------
    void previewServerServesOnlyInMemoryFiles()
    {
        PreviewServer server;
        QString error;
        QVERIFY2(server.start(0, &error), qPrintable(error));
        QVERIFY(server.isRunning());
        QVERIFY(server.port() > 0);
        QVERIFY(server.baseUrl().startsWith(QStringLiteral("http://127.0.0.1:")));

        QVector<EmittedFile> files;
        EmittedFile index;
        index.relativePath = QStringLiteral("index.html");
        index.contents = QStringLiteral("<!DOCTYPE html><title>t</title>");
        index.mimeType = QStringLiteral("text/html");
        files.append(index);

        EmittedFile asset;
        asset.relativePath = QStringLiteral("assets/app.js");
        asset.contents = QStringLiteral("console.log(1);");
        asset.mimeType = QStringLiteral("text/javascript");
        files.append(asset);

        server.setFiles(files);
        QCOMPARE(server.files().size(), 2);

        server.stop();
        QVERIFY(!server.isRunning());
    }

    void previewUrlCompositionIsStable()
    {
        PreviewServer server;
        QVERIFY(server.start(0, nullptr));

        QCOMPARE(server.urlFor(QString()), server.baseUrl());
        QVERIFY(server.urlFor(QStringLiteral("/index.html")).endsWith(QStringLiteral("/index.html")));
        QVERIFY(server.urlFor(QStringLiteral("index.html")).endsWith(QStringLiteral("/index.html")));

        server.stop();
    }

    void loopbackHostHeaderIsStrict()
    {
        QVERIFY(PreviewServer::isLoopbackHostHeader(QStringLiteral("localhost")));
        QVERIFY(PreviewServer::isLoopbackHostHeader(QStringLiteral("localhost:8080")));
        QVERIFY(PreviewServer::isLoopbackHostHeader(QStringLiteral("127.0.0.1")));
        QVERIFY(PreviewServer::isLoopbackHostHeader(QStringLiteral("127.0.0.1:1234")));
        QVERIFY(PreviewServer::isLoopbackHostHeader(QStringLiteral("127.1.2.3")));
        QVERIFY(PreviewServer::isLoopbackHostHeader(QStringLiteral("[::1]:8080")));
        QVERIFY(PreviewServer::isLoopbackHostHeader(QStringLiteral("LOCALHOST")));

        //  The rebinding attack arrives with an attacker-controlled Host.
        //  重绑定攻击会带着攻击者控制的 Host 到达。
        QVERIFY(!PreviewServer::isLoopbackHostHeader(QStringLiteral("0.0.0.0")));
        QVERIFY(!PreviewServer::isLoopbackHostHeader(QStringLiteral("evil.example.com")));
        QVERIFY(!PreviewServer::isLoopbackHostHeader(QStringLiteral("example.com:127.0.0.1")));
        QVERIFY(!PreviewServer::isLoopbackHostHeader(QString()));
    }

    void requestPathsCannotEscapeTheRoot()
    {
        QCOMPARE(PreviewServer::normaliseRequestPath(QStringLiteral("/")),
                 QStringLiteral("/"));
        QCOMPARE(PreviewServer::normaliseRequestPath(QStringLiteral("/index.html")),
                 QStringLiteral("/index.html"));
        QCOMPARE(PreviewServer::normaliseRequestPath(QStringLiteral("/a/b/../c.js")),
                 QStringLiteral("/a/c.js"));
        QCOMPARE(PreviewServer::normaliseRequestPath(QStringLiteral("/index.html?v=1#x")),
                 QStringLiteral("/index.html"));
        QCOMPARE(PreviewServer::normaliseRequestPath(QStringLiteral("//a//b")),
                 QStringLiteral("/a/b"));
        QCOMPARE(PreviewServer::normaliseRequestPath(QStringLiteral("/a%20b.html")),
                 QStringLiteral("/a b.html"));

        //  Traversal attempts must be rejected outright.
        //  目录穿越尝试必须被直接拒绝。
        QVERIFY(PreviewServer::normaliseRequestPath(QStringLiteral("/../../etc/passwd")).isEmpty());
        QVERIFY(PreviewServer::normaliseRequestPath(QStringLiteral("/a/../../../etc/passwd")).isEmpty());
        QVERIFY(PreviewServer::normaliseRequestPath(QStringLiteral("/%2e%2e/%2e%2e/etc/passwd")).isEmpty());
        QVERIFY(PreviewServer::normaliseRequestPath(QStringLiteral("..%2f..%2fetc%2fpasswd")).isEmpty());
    }

    void previewServerBindsOnlyToLoopback()
    {
        PreviewServer server;
        QVERIFY(server.start(0, nullptr));
        //  The advertised URL must always be the loopback literal, never 0.0.0.0.
        //  对外公布的地址必须始终是回环字面量，而绝不能是 0.0.0.0。
        QVERIFY(server.baseUrl().contains(QStringLiteral("127.0.0.1")));
        QVERIFY(!server.baseUrl().contains(QStringLiteral("0.0.0.0")));
        server.stop();
    }
};

QTEST_MAIN(TestBackend)
#include "tst_backend.moc"
