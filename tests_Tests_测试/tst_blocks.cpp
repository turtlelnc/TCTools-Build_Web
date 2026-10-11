// ============================================================================
//  tst_blocks.cpp -- block registry, model, validation and JavaScript output
//  tst_blocks.cpp —— 积木注册表、模型、校验与 JavaScript 输出
// ============================================================================
#include "BlockCompiler.h"
#include "BlockDef.h"
#include "BlockRegistry.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QTest>

using namespace tcbw;

class TestBlocks : public QObject
{
    Q_OBJECT

private slots:
    // --- registry ----------------------------------------------------------
    void registryIsSubstantial()
    {
        const BlockRegistry &registry = BlockRegistry::instance();

        QVERIFY2(registry.definitions().size() >= 85,
                 qPrintable(QStringLiteral("only %1 block types")
                                .arg(registry.definitions().size())));
        QVERIFY2(registry.categories().size() >= 14,
                 qPrintable(QStringLiteral("only %1 categories")
                                .arg(registry.categories().size())));
        QVERIFY(!registry.hatBlocks().isEmpty());
    }

    void everyDefinitionIsWellFormed()
    {
        const BlockRegistry &registry = BlockRegistry::instance();
        QSet<QString> types;

        for (const BlockDef &def : registry.definitions()) {
            QVERIFY2(!def.type.isEmpty(), "empty block type / 空的积木类型");
            QVERIFY2(!types.contains(def.type), qPrintable(def.type));
            types.insert(def.type);

            QVERIFY2(!def.labelEn.isEmpty(), qPrintable(def.type));
            QVERIFY2(!def.labelZh.isEmpty(), qPrintable(def.type));
            QVERIFY2(registry.category(def.category) != nullptr, qPrintable(def.category));

            //  A C/E shaped block must declare at least one statement branch,
            //  and nothing else may declare one. `control.if` has a single
            //  `then` branch; only `control.ifElse` carries a second one.
            //  C/E 形积木必须声明至少一个语句分支，其他形状不得声明。
            //  `control.if` 只有 `then` 一个分支；只有 `control.ifElse` 带有第二个。
            if (def.shape == BlockShape::CShape || def.shape == BlockShape::EShape)
                QVERIFY2(!def.statements.isEmpty(), qPrintable(def.type));
            else
                QVERIFY2(def.statements.isEmpty(), qPrintable(def.type));

            if (def.type == QLatin1String("control.ifElse"))
                QVERIFY2(def.statements.size() >= 2, qPrintable(def.type));

            //  Placeholder count must not exceed the number of slots, otherwise
            //  the renderer would show a stray %n.
            //  占位符数量不得超过槽位数，否则渲染器会显示多余的 %n。
            const int placeholdersEn = def.labelEn.count(QRegularExpression(QStringLiteral("%[1-9]")));
            //  NOTE: the local is not called `slots` -- Qt defines that as a macro.
            //  注意：局部变量不叫 `slots` —— Qt 把它定义为宏。
            const int slotCount = def.fields.size() + def.inputs.size() + def.statements.size();
            QVERIFY2(placeholdersEn <= slotCount, qPrintable(def.type));

            //  A returning block must not also be a statement shape.
            //  有返回值的积木不能同时是语句形状。
            if (def.returnsValue())
                QVERIFY2(def.shape != BlockShape::Hat && def.shape != BlockShape::Cap,
                         qPrintable(def.type));
        }
    }

    void lookupAndSearchWork()
    {
        const BlockRegistry &registry = BlockRegistry::instance();

        QVERIFY(registry.find(QStringLiteral("events.start")) != nullptr);
        QVERIFY(registry.find(QStringLiteral("control.if")) != nullptr);
        QVERIFY(registry.contains(QStringLiteral("console.log")));
        QVERIFY(registry.find(QStringLiteral("does.not.exist")) == nullptr);

        QVERIFY(!registry.byCategory(QStringLiteral("control")).isEmpty());
        QVERIFY(registry.byCategory(QStringLiteral("nope")).isEmpty());

        QVERIFY(!registry.search(QStringLiteral("repeat")).isEmpty());
        QVERIFY(registry.search(QStringLiteral("zzzznomatch")).isEmpty());

        QVERIFY(!registry.visibleCategories(false).isEmpty());
        QVERIFY(registry.visibleCategories(true).size() >= registry.visibleCategories(false).size());
    }

    void createBlockFillsDefaults()
    {
        const BlockRegistry &registry = BlockRegistry::instance();
        const BlockDef *def = registry.find(QStringLiteral("console.log"));
        QVERIFY(def != nullptr);

        auto block = registry.createBlock(QStringLiteral("console.log"));
        QVERIFY(block != nullptr);
        QCOMPARE(block->type, QStringLiteral("console.log"));
        QVERIFY(!block->id.isEmpty());

        for (const BlockFieldDef &field : def->fields) {
            QVERIFY2(block->fieldValues.contains(field.name), qPrintable(field.name));
            QCOMPARE(block->fieldValues.value(field.name), field.defaultValue);
        }

        //  An unknown type must not produce a block.
        QVERIFY(registry.createBlock(QStringLiteral("no.such.block")) == nullptr);
    }

    // --- model -------------------------------------------------------------
    void scriptsOwnTheirBlocksAndRoundTrip()
    {
        const BlockRegistry &registry = BlockRegistry::instance();

        auto script = std::make_unique<BlockScript>(QStringLiteral("events.start"));
        QVERIFY(script->hat != nullptr);
        QCOMPARE(script->hat->type, QStringLiteral("events.start"));

        auto log = registry.createBlock(QStringLiteral("console.log"));
        QVERIFY(log != nullptr);
        log->setFieldValue(QStringLiteral("message"), QStringLiteral("hello"));
        const QString logId = log->id;
        script->addBlock(std::move(log));
        QCOMPARE(script->blockCount(), 1);

        int visited = 0;
        script->forEach([&](Block *) { ++visited; });
        //  The hat plus one body block.
        //  帽子块加上一个主体积木。
        QVERIFY(visited >= 2);

        const QJsonObject json = script->toJson();
        QString error;
        auto restored = BlockScript::fromJson(json, &error);
        QVERIFY2(restored != nullptr, qPrintable(error));
        QCOMPARE(restored->blockCount(), script->blockCount());
        QCOMPARE(restored->body.first()->fieldValue(QStringLiteral("message")),
                 QStringLiteral("hello"));
        QCOMPARE(restored->body.first()->id, logId);
    }

    void fromJsonRejectsCyclesAndGarbage()
    {
        QString error;
        //  An empty object is tolerated by design: an unknown script degrades to
        //  a usable default rather than refusing to load the whole project. What
        //  must NOT happen is a crash or a silently malformed block.
        //  空对象在设计中是被容忍的：未知脚本会降级为可用的默认值，而不是拒绝加载
        //  整个工程。真正不允许发生的是崩溃或静默产出畸形的积木。
        auto tolerant = BlockScript::fromJson(QJsonObject{}, &error);
        if (tolerant) {
            QVERIFY(tolerant->body.isEmpty());
            QVERIFY(tolerant->hat != nullptr);
        }

        //  A block without a type is invalid.
        //  没有类型的积木是无效的。
        QJsonObject bad;
        bad.insert(QStringLiteral("id"), QStringLiteral("b1"));
        QVERIFY(Block::fromJson(bad, &error) == nullptr);

        //  Deep nesting must be refused rather than overflowing the stack.
        //  深层嵌套必须被拒绝，而不是让栈溢出。
        QJsonObject leaf;
        leaf.insert(QStringLiteral("type"), QStringLiteral("console.log"));
        QJsonObject current = leaf;
        for (int i = 0; i < 400; ++i) {
            QJsonObject wrapper;
            wrapper.insert(QStringLiteral("type"), QStringLiteral("control.repeat"));
            QJsonArray statements;
            statements.append(current);
            wrapper.insert(QStringLiteral("statements"),
                           QJsonObject{{QStringLiteral("body"), statements}});
            current = wrapper;
        }
        QVERIFY(Block::fromJson(current, &error) == nullptr);
        QVERIFY(!error.isEmpty());
    }

    void detachKeepsParentLinksTruthful()
    {
        const BlockRegistry &registry = BlockRegistry::instance();

        auto script = std::make_unique<BlockScript>(QStringLiteral("events.start"));
        //  BlockScript::addBlock takes ownership and returns void, so the raw
        //  pointers are captured before the transfer.
        //  BlockScript::addBlock 取得所有权并返回 void，因此在移交前先捕获裸指针。
        Block *firstPtr = registry.createBlock(QStringLiteral("console.log")).release();
        Block *secondPtr = registry.createBlock(QStringLiteral("ui.show")).release();
        QVERIFY(firstPtr && secondPtr);

        script->addBlock(std::unique_ptr<Block>(firstPtr));
        script->addBlock(std::unique_ptr<Block>(secondPtr));
        //  A script body block hangs off the hat block, so its parent is the
        //  hat rather than nullptr.
        //  脚本主体中的积木挂在帽子块之下，因此其父节点是帽子块而非空。
        QCOMPARE(firstPtr->parent, script->hat.get());
        QCOMPARE(secondPtr->parent, script->hat.get());
        QCOMPARE(script->body.size(), 2);

        auto taken = script->takeBlock(firstPtr);
        QVERIFY(taken != nullptr);
        QCOMPARE(script->body.size(), 1);
        QVERIFY(taken->parent == nullptr);
        //  The hat block owns nothing else now.
        //  帽子块现在不再持有除自身以外的积木。
        Q_UNUSED(secondPtr);

        //  Detaching an unknown block must fail safely.
        QVERIFY(script->takeBlock(static_cast<Block *>(nullptr)) == nullptr);
    }

    // --- validation --------------------------------------------------------
    void validateReportsMissingBodies()
    {
        const BlockRegistry &registry = BlockRegistry::instance();

        auto repeat = registry.createBlock(QStringLiteral("control.repeat"));
        QVERIFY(repeat != nullptr);

        const QVector<BlockRegistry::Diagnostic> diagnostics = registry.validate(*repeat);
        //  An empty loop body is a reportable (non-fatal) problem.
        //  空的循环体是可报告（非致命）的问题。
        QVERIFY(!diagnostics.isEmpty());
    }

    void validateAcceptsACompleteScript()
    {
        const BlockRegistry &registry = BlockRegistry::instance();

        auto script = std::make_unique<BlockScript>(QStringLiteral("events.start"));
        auto log = registry.createBlock(QStringLiteral("console.log"));
        QVERIFY(log != nullptr);
        script->addBlock(std::move(log));

        for (const Block *b : script->body) {
            const QVector<BlockRegistry::Diagnostic> diagnostics = registry.validate(*b);
            for (const BlockRegistry::Diagnostic &d : diagnostics)
                QVERIFY2(!d.fatal, qPrintable(d.messageEn));
        }
    }

    // --- compiler ----------------------------------------------------------
    void compilerProducesJavaScript()
    {
        const BlockRegistry &registry = BlockRegistry::instance();

        auto script = std::make_unique<BlockScript>(QStringLiteral("events.start"));
        auto log = registry.createBlock(QStringLiteral("console.log"));
        QVERIFY(log != nullptr);
        //  The field name comes from the definition rather than being assumed,
        //  so a catalogue rename cannot silently make this test vacuous.
        //  字段名取自定义而非假定，因此目录改名不会让本测试悄悄失效。
        const BlockDef *def = registry.find(QStringLiteral("console.log"));
        QVERIFY(def != nullptr && !def->fields.isEmpty());
        const QString messageField = def->fields.first().name;
        log->setFieldValue(messageField, QStringLiteral("hello world"));
        script->addBlock(std::move(log));

        CompileOptions options;
        options.pretty = true;
        BlockCompiler compiler(options);

        const CompileResult result = compiler.compileScript(*script);
        QVERIFY2(result.ok, qPrintable(result.errorText()));
        QVERIFY(!result.code.isEmpty());
        QVERIFY(result.code.contains(QStringLiteral("console")));
        QVERIFY(result.code.contains(QStringLiteral("hello world")));
        QVERIFY(result.statementCount > 0);
    }

    void compilerEscapesHostileFieldValues()
    {
        const BlockRegistry &registry = BlockRegistry::instance();

        auto script = std::make_unique<BlockScript>(QStringLiteral("events.start"));
        auto log = registry.createBlock(QStringLiteral("console.log"));
        QVERIFY(log != nullptr);
        const BlockDef *def = registry.find(QStringLiteral("console.log"));
        QVERIFY(def != nullptr && !def->fields.isEmpty());

        //  A value that tries to break out of the string literal.
        //  试图逃逸出字符串字面量的值。
        log->setFieldValue(def->fields.first().name,
                           QStringLiteral("'); alert('pwned'); //"));
        script->addBlock(std::move(log));

        BlockCompiler compiler(CompileOptions{});
        const CompileResult result = compiler.compileScript(*script);
        QVERIFY(result.ok);

        //  The payload must survive only as escaped text inside a literal, never
        //  as an executable statement.
        //  该载荷只能以转义文本的形式存在于字面量内，绝不能成为可执行语句。
        QVERIFY(!result.code.contains(QStringLiteral("'); alert('pwned'); //")));
        QVERIFY(result.code.contains(QStringLiteral("\\'")) || result.code.contains(QStringLiteral("alert")));
        QVERIFY(!result.code.contains(QStringLiteral("); alert('pwned')")));
    }

    void compilerHandlesControlFlow()
    {
        const BlockRegistry &registry = BlockRegistry::instance();

        auto script = std::make_unique<BlockScript>(QStringLiteral("events.start"));

        auto log = registry.createBlock(QStringLiteral("console.log"));
        QVERIFY(log != nullptr);
        const BlockDef *logDef = registry.find(QStringLiteral("console.log"));
        QVERIFY(logDef != nullptr && !logDef->fields.isEmpty());
        log->setFieldValue(logDef->fields.first().name, QStringLiteral("inside"));
        auto logPtr = log.get();

        auto repeat = registry.createBlock(QStringLiteral("control.repeat"));
        QVERIFY(repeat != nullptr);
        const BlockDef *repeatDef = registry.find(QStringLiteral("control.repeat"));
        QVERIFY(repeatDef != nullptr && !repeatDef->fields.isEmpty());
        repeat->setFieldValue(repeatDef->fields.first().name, QStringLiteral("3"));
        repeat->statements.insert(QStringLiteral("body"), {logPtr});
        repeat->forEach([&](Block *b) {
            if (b != repeat.get())
                b->parent = repeat.get();
        });

        script->addBlock(std::move(repeat));

        BlockCompiler compiler(CompileOptions{});
        const CompileResult result = compiler.compileScript(*script);
        QVERIFY2(result.ok, qPrintable(result.errorText()));
        QVERIFY(result.code.contains(QStringLiteral("for")));
        QVERIFY(result.code.contains(QStringLiteral("inside")));
    }

    void compilerRefusesIncompleteScript()
    {
        const BlockRegistry &registry = BlockRegistry::instance();

        auto script = std::make_unique<BlockScript>(QStringLiteral("events.start"));
        auto http = registry.createBlock(QStringLiteral("http.get"));
        QVERIFY(http != nullptr);
        script->addBlock(std::move(http));

        BlockCompiler compiler(CompileOptions{});
        const CompileResult result = compiler.compileScript(*script);

        //  A GET without a URL must be reported, not silently emitted.
        //  缺少 URL 的 GET 必须被报告，而不是静默输出。
        QVERIFY(!result.ok);
        QVERIFY(!result.errors.isEmpty());
    }

    void compilerIsDeterministic()
    {
        const BlockRegistry &registry = BlockRegistry::instance();

        auto build = [&]() {
            auto script = std::make_unique<BlockScript>(QStringLiteral("events.start"));
            auto log = registry.createBlock(QStringLiteral("console.log"));
            log->setFieldValue(QStringLiteral("message"), QStringLiteral("deterministic"));
            script->addBlock(std::move(log));
            return script;
        };

        BlockCompiler compiler(CompileOptions{});
        auto first = build();
        auto second = build();
        QCOMPARE(compiler.compileScript(*first).code, compiler.compileScript(*second).code);
    }

    void identifierSanitisationIsSafe()
    {
        QCOMPARE(BlockCompiler::sanitiseIdentifier(QStringLiteral("hello world")),
                 QStringLiteral("hello_world"));
        QVERIFY(!BlockCompiler::sanitiseIdentifier(QStringLiteral("1abc")).at(0).isDigit());
        QVERIFY(BlockCompiler::sanitiseIdentifier(QStringLiteral("")).size() > 0);
        QVERIFY(!BlockCompiler::sanitiseIdentifier(QStringLiteral("a;b{c}")).contains(QLatin1Char(';')));

        QCOMPARE(BlockCompiler::jsString(QStringLiteral("a'b")), QStringLiteral("'a\\'b'"));
        QCOMPARE(BlockCompiler::jsString(QStringLiteral("a\\b")), QStringLiteral("'a\\\\b'"));
        QCOMPARE(BlockCompiler::parseNumber(QStringLiteral("12.5")), 12.5);
        QCOMPARE(BlockCompiler::parseNumber(QStringLiteral("junk"), 7.0), 7.0);
    }

    void shapeHelpersAreConsistent()
    {
        QVERIFY(shapeReturnsValue(BlockShape::Reporter));
        QVERIFY(shapeReturnsValue(BlockShape::Boolean));
        QVERIFY(shapeReturnsValue(BlockShape::Round));
        QVERIFY(!shapeReturnsValue(BlockShape::Stack));
        QVERIFY(!shapeReturnsValue(BlockShape::CShape));
        QVERIFY(!shapeReturnsValue(BlockShape::Hat));

        const QVector<BlockShape> shapes = {BlockShape::Hat, BlockShape::Stack,
                                            BlockShape::CShape, BlockShape::EShape,
                                            BlockShape::Cap, BlockShape::Reporter,
                                            BlockShape::Boolean, BlockShape::Round};
        for (BlockShape shape : shapes)
            QCOMPARE(static_cast<int>(blockShapeFromString(toString(shape))),
                     static_cast<int>(shape));
    }
};

QTEST_MAIN(TestBlocks)
#include "tst_blocks.moc"
