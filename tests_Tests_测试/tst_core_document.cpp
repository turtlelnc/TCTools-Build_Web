// ============================================================================
//  tst_core_document.cpp -- element model, styles, commands and project IO
//  tst_core_document.cpp —— 元素模型、样式、命令与工程读写
// ============================================================================
#include "Commands.h"
#include "DocumentNode.h"
#include "ElementCatalog.h"
#include "IdGenerator.h"
#include "Page.h"
#include "ProjectDocument.h"
#include "ProjectIO.h"
#include "StyleSheet.h"
#include "Types.h"

#include <QTest>
#include <QTemporaryDir>

using namespace tcbw;

class TestCoreDocument : public QObject
{
    Q_OBJECT

private slots:
    // --- identifiers -------------------------------------------------------
    void idsAreUniqueAndValid()
    {
        QSet<QString> seen;
        for (int i = 0; i < 2000; ++i) {
            const QString id = IdGenerator::elementId();
            QVERIFY2(IdGenerator::isValid(id), qPrintable(id));
            QVERIFY2(!seen.contains(id), "identifier collision / 标识符冲突");
            seen.insert(id);
        }
    }

    void pkceChallengeIsDeterministicAndUrlSafe()
    {
        const QString verifier = IdGenerator::randomToken(64);
        const QString challenge = IdGenerator::pkceChallenge(verifier);
        QCOMPARE(challenge, IdGenerator::pkceChallenge(verifier));
        QVERIFY(!challenge.contains(QLatin1Char('+')));
        QVERIFY(!challenge.contains(QLatin1Char('/')));
        QVERIFY(!challenge.contains(QLatin1Char('=')));
        //  A SHA-256 digest is 32 bytes => 43 base64url characters.
        //  SHA-256 摘要为 32 字节 => 43 个 base64url 字符。
        QCOMPARE(challenge.size(), 43);
    }

    // --- tree --------------------------------------------------------------
    void treeRefusesCycles()
    {
        auto root = DocumentNode::makeRoot();
        auto child = DocumentNode::makeElement(QStringLiteral("div"));
        DocumentNode *childPtr = root->appendChild(std::move(child));
        QVERIFY(childPtr != nullptr);

        //  A node cannot be inserted into its own descendant.
        //  节点不能插入到自己的后代中。
        QVERIFY(!childPtr->appendChild(root->takeChild(0)));

        //  A void element cannot own children.
        //  空元素不能拥有子节点。
        auto image = DocumentNode::makeElement(QStringLiteral("img"));
        QVERIFY(image->isVoidElement());
        auto span = DocumentNode::makeElement(QStringLiteral("span"));
        QVERIFY(image->appendChild(std::move(span)) == nullptr);
    }

    void treeCloneIsIndependent()
    {
        auto root = DocumentNode::makeRoot();
        auto section = DocumentNode::makeElement(QStringLiteral("section"));
        section->setAttribute(QStringLiteral("id"), QStringLiteral("hero"));
        section->style.base().set(QStringLiteral("padding"), QStringLiteral("24px"));
        root->appendChild(std::move(section));

        auto copy = root->clone(false);
        QVERIFY(copy != nullptr);
        QCOMPARE(copy->childCount(), 1);
        QVERIFY(copy->childAt(0)->id != root->childAt(0)->id);
        QCOMPARE(copy->childAt(0)->attribute(QStringLiteral("id")), QStringLiteral("hero"));

        copy->childAt(0)->setAttribute(QStringLiteral("id"), QStringLiteral("other"));
        QCOMPARE(root->childAt(0)->attribute(QStringLiteral("id")), QStringLiteral("hero"));
    }

    void validateDetectsDuplicateIds()
    {
        auto root = DocumentNode::makeRoot();
        auto a = DocumentNode::makeElement(QStringLiteral("div"), QStringLiteral("dup-1"));
        auto b = DocumentNode::makeElement(QStringLiteral("div"), QStringLiteral("dup-1"));
        root->appendChild(std::move(a));
        root->appendChild(std::move(b));

        QStringList problems;
        QVERIFY(!root->validate(&problems));
        QVERIFY(!problems.isEmpty());
    }

    // --- styles ------------------------------------------------------------
    void responsiveStyleOverridesInherit()
    {
        ResponsiveStyle style;
        style.base().set(QStringLiteral("padding"), QStringLiteral("16px"));
        style.overrideFor(Breakpoint::Tablet).set(QStringLiteral("padding"),
                                                  QStringLiteral("12px"));
        style.overrideFor(Breakpoint::Mobile).set(QStringLiteral("font-size"),
                                                  QStringLiteral("14px"));

        QCOMPARE(style.resolve(Breakpoint::Desktop, QStringLiteral("padding")),
                 QStringLiteral("16px"));
        QCOMPARE(style.resolve(Breakpoint::Tablet, QStringLiteral("padding")),
                 QStringLiteral("12px"));
        //  Mobile inherits the tablet override and adds its own.
        //  手机会继承平板的覆盖值，并加入自己的覆盖值。
        QCOMPARE(style.resolve(Breakpoint::Mobile, QStringLiteral("padding")),
                 QStringLiteral("12px"));
        QCOMPARE(style.resolve(Breakpoint::Mobile, QStringLiteral("font-size")),
                 QStringLiteral("14px"));
    }

    void styleSheetRejectsInjection()
    {
        StyleSheet sheet;
        sheet.set(QStringLiteral("color"), QStringLiteral("red; } body { display:none"));
        QVERIFY(!sheet.has(QStringLiteral("color")));

        sheet.set(QStringLiteral("background"), QStringLiteral("url(</style><script>x</script>)"));
        QVERIFY(!sheet.has(QStringLiteral("background")));

        sheet.set(QStringLiteral("color"), QStringLiteral("#0A84FF"));
        QVERIFY(sheet.has(QStringLiteral("color")));
        QCOMPARE(sheet.get(QStringLiteral("color")), QStringLiteral("#0A84FF"));
    }

    void styleSheetRoundTripsThroughJson()
    {
        StyleSheet sheet;
        sheet.set(QStringLiteral("padding"), QStringLiteral("8px 16px"));
        sheet.set(QStringLiteral("border-radius"), QStringLiteral("12px"));
        const StyleSheet restored = StyleSheet::fromJson(sheet.toJson());
        QCOMPARE(restored.get(QStringLiteral("padding")), QStringLiteral("8px 16px"));
        QCOMPARE(restored.get(QStringLiteral("border-radius")), QStringLiteral("12px"));
    }

    // --- catalogue ---------------------------------------------------------
    void catalogueIsComplete()
    {
        const ElementCatalog &catalog = ElementCatalog::instance();
        QVERIFY2(catalog.elements().size() >= 75,
                 qPrintable(QStringLiteral("only %1 elements").arg(catalog.elements().size())));
        QVERIFY2(catalog.composites().size() >= 20,
                 qPrintable(QStringLiteral("only %1 composites").arg(catalog.composites().size())));

        //  Void tags must be flagged, and their definitions must not allow
        //  children.
        //  空元素必须被正确标记，且其定义不得允许子节点。
        const QStringList voids = {QStringLiteral("img"), QStringLiteral("br"),
                                   QStringLiteral("input"), QStringLiteral("hr"),
                                   QStringLiteral("meta"), QStringLiteral("link")};
        for (const QString &tag : voids) {
            const ElementDef *def = catalog.find(tag);
            QVERIFY2(def != nullptr, qPrintable(tag));
            QVERIFY2(def->isVoid, qPrintable(tag));
            QVERIFY2(!def->allowsChildren, qPrintable(tag));
            QVERIFY(ElementCatalog::isVoidTag(tag));
        }

        //  Every catalogue entry must produce a usable node.
        //  每个目录条目都必须能生成可用的节点。
        for (const ElementDef &def : catalog.elements()) {
            auto node = catalog.create(def.tag, false);
            QVERIFY2(node != nullptr, qPrintable(def.tag));
            QCOMPARE(node->tag, def.tag.toLower());
            QVERIFY(!node->id.isEmpty());
        }
    }

    void compositesProduceTrees()
    {
        const ElementCatalog &catalog = ElementCatalog::instance();
        for (const CompositeDef &def : catalog.composites()) {
            auto node = catalog.createComposite(def.id, false);
            QVERIFY2(node != nullptr, qPrintable(def.id));
            QVERIFY2(node->childCount() > 0, qPrintable(def.id));
            QStringList problems;
            QVERIFY2(node->validate(&problems), qPrintable(problems.join(QLatin1String("; "))));
        }
    }

    // --- pages and project -------------------------------------------------
    void starterProjectIsUsable()
    {
        auto project = ProjectDocument::makeStarter(false);
        QVERIFY(project != nullptr);
        QVERIFY(project->pageCount() >= 1);

        Page *home = project->homePage();
        QVERIFY(home != nullptr);
        QVERIFY(home->root != nullptr);
        QCOMPARE(home->root->tag, QStringLiteral("body"));
        QVERIFY(home->nodeCount() > 3);

        QStringList problems;
        QVERIFY2(home->root->validate(&problems),
                 qPrintable(problems.join(QLatin1String("; "))));
    }

    void projectRoundTripsThroughJson()
    {
        auto project = ProjectDocument::makeStarter(true);

        ProjectIO io;
        const QByteArray bytes = io.toJsonBytes(*project);
        QVERIFY(!bytes.isEmpty());

        QString error;
        auto restored = ProjectIO::fromJson(bytes, &error);
        QVERIFY2(restored != nullptr, qPrintable(error));
        QCOMPARE(restored->pageCount(), project->pageCount());
        QCOMPARE(restored->pageAt(0)->name, project->pageAt(0)->name);
        QCOMPARE(restored->pageAt(0)->nodeCount(), project->pageAt(0)->nodeCount());
    }

    void projectIoRejectsGarbage()
    {
        QString error;
        QVERIFY(ProjectIO::fromJson(QByteArrayLiteral("not json at all"), &error) == nullptr);
        QVERIFY(!error.isEmpty());

        QVERIFY(ProjectIO::fromJson(QByteArrayLiteral("{}"), &error) == nullptr);

        QVERIFY(ProjectIO::hasProjectExtension(QStringLiteral("a.tcbw")));
        QVERIFY(ProjectIO::hasProjectExtension(QStringLiteral("a.TCBW")));
        QVERIFY(!ProjectIO::hasProjectExtension(QStringLiteral("a.html")));
    }

    void projectIoWritesAtomically()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        auto project = ProjectDocument::makeStarter(false);
        const QString path = dir.filePath(QStringLiteral("site.tcbw"));

        ProjectIO io;
        QString error;
        QVERIFY2(io.save(*project, path, &error), qPrintable(error));
        QVERIFY(QFile::exists(path));

        auto loaded = io.load(path, &error);
        QVERIFY2(loaded != nullptr, qPrintable(error));
        QCOMPARE(loaded->pageCount(), project->pageCount());

        //  No temporary file may survive a successful save.
        //  成功保存后不得残留临时文件。
        const QStringList leftovers = QDir(dir.path()).entryList(
            {QStringLiteral("*.tmp-*")}, QDir::Files);
        QCOMPARE(leftovers.size(), 0);
    }

    // --- commands ----------------------------------------------------------
    void addAndRemoveNodeCommandsUndoCleanly()
    {
        auto project = ProjectDocument::makeStarter(false);
        CommandStack stack(project.get());
        Page *page = project->pageAt(0);
        QVERIFY(page != nullptr);

        const int before = page->nodeCount();
        const QString pageId = page->id;

        auto node = ElementCatalog::instance().create(QStringLiteral("section"), false);
        QVERIFY(node != nullptr);
        const QString newId = stack.addNode(pageId, QString(), -1, std::move(node),
                                            QStringLiteral("Add section"),
                                            QStringLiteral("添加区块"));
        QVERIFY(!newId.isEmpty());
        QCOMPARE(page->nodeCount(), before + 1);
        QVERIFY(page->findNode(newId) != nullptr);

        stack.undo();
        QCOMPARE(page->nodeCount(), before);
        QVERIFY(page->findNode(newId) == nullptr);

        stack.redo();
        QCOMPARE(page->nodeCount(), before + 1);
        QVERIFY(page->findNode(newId) != nullptr);
    }

    void setTextCommandIsNoopAware()
    {
        auto project = ProjectDocument::makeStarter(false);
        CommandStack stack(project.get());
        Page *page = project->pageAt(0);

        //  Find the first node that can carry text.
        //  找到第一个可以承载文本的节点。
        DocumentNode *textNode = nullptr;
        page->root->forEach([&](DocumentNode *n) {
            if (textNode == nullptr && n->tag == QStringLiteral("h1"))
                textNode = n;
        });
        if (textNode == nullptr) {
            textNode = page->root->childAt(0);
        }
        QVERIFY(textNode != nullptr);

        const QString original = textNode->text;
        QVERIFY(stack.setText(page->id, textNode->id, QStringLiteral("Hello"),
                              QStringLiteral("Set text"), QStringLiteral("设置文本")));
        QCOMPARE(textNode->text, QStringLiteral("Hello"));

        stack.undo();
        QCOMPARE(textNode->text, original);
    }

    // --- security helpers --------------------------------------------------
    void secretsAreDetectedAndRedacted()
    {
        QVERIFY(looksLikeSecret(QStringLiteral("sk-abcdefghijklmnopqrstuvwxyz012345")));
        QVERIFY(looksLikeSecret(QStringLiteral("ghp_0123456789abcdefghijklmnopqrstuvwx")));
        QVERIFY(!looksLikeSecret(QStringLiteral("hello world")));

        const QString text = QStringLiteral(
            "key=sk-abcdefghijklmnopqrstuvwxyz012345 and Authorization: Bearer abcdef0123456789xyz");
        const QString redacted = redactSecrets(text);
        QVERIFY(!redacted.contains(QStringLiteral("sk-abcdefghijklmnopqrstuvwxyz012345")));
        QVERIFY(redacted.contains(QStringLiteral("REDACTED")));
        //  Idempotent: redacting twice changes nothing further.
        //  幂等：再次遮蔽不会产生更多变化。
        QCOMPARE(redactSecrets(redacted), redacted);
    }

    void versionStringsAreStable()
    {
        QVERIFY(!versionString().isEmpty());
        QVERIFY(versionString().startsWith(QStringLiteral("0.1.0")));
        QCOMPARE(versionChannel(), QStringLiteral("rc1"));
    }

    void enumRoundTrips()
    {
        const QVector<ExportMode> modes = {ExportMode::SingleHtml,
                                           ExportMode::HtmlWithAssets,
                                           ExportMode::ScriptOnly,
                                           ExportMode::HeadlessBundle};
        for (ExportMode mode : modes)
            QCOMPARE(static_cast<int>(exportModeFromString(toString(mode))),
                     static_cast<int>(mode));

        const QVector<Breakpoint> breakpoints = {Breakpoint::Desktop, Breakpoint::Tablet,
                                                 Breakpoint::Mobile};
        for (Breakpoint bp : breakpoints)
            QCOMPARE(static_cast<int>(breakpointFromString(toString(bp))),
                     static_cast<int>(bp));

        const QVector<AuthMode> authModes = {AuthMode::ApiKey, AuthMode::OAuthPkce,
                                             AuthMode::OAuthDevice, AuthMode::Bearer,
                                             AuthMode::AwsSigV4, AuthMode::None};
        for (AuthMode mode : authModes)
            QCOMPARE(static_cast<int>(authModeFromString(toString(mode))),
                     static_cast<int>(mode));

        //  Unknown strings must fall back rather than throw. Scoped enums are
        //  compared as integers because QTest's ADL lookup would otherwise pick
        //  `tcbw::toString` (returning QString) where it needs char*.
        //  未知字符串必须回退，而不是抛出异常。作用域枚举按整数比较，否则 QTest 的
        //  ADL 查找会选中返回 QString 的 `tcbw::toString`，而它需要 char*。
        QCOMPARE(static_cast<int>(exportModeFromString(QStringLiteral("nonsense"))),
                 static_cast<int>(ExportMode::SingleHtml));
        QCOMPARE(static_cast<int>(nodeKindFromString(QString())),
                 static_cast<int>(NodeKind::Element));
    }
};

QTEST_MAIN(TestCoreDocument)
#include "tst_core_document.moc"
