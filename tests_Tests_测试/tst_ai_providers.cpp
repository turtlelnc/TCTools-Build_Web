// ============================================================================
//  tst_ai_providers.cpp -- provider presets, request building, stream parsing,
//                          secret storage and the tool registry
//  tst_ai_providers.cpp —— 厂商预设、请求构建、流式解析、机密存储与工具注册表
// ============================================================================
#include "AgentTools.h"
#include "AiProvider.h"
#include "AiTypes.h"
#include "ProviderRegistry.h"
#include "SecretStore.h"
#include "Types.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using namespace tcbw;

class TestAiProviders : public QObject
{
    Q_OBJECT

private slots:
    // --- registry ----------------------------------------------------------
    void registryCoversFiftyVendors()
    {
        const ProviderRegistry &registry = ProviderRegistry::instance();

        QVERIFY2(registry.vendorCount() >= 50,
                 qPrintable(QStringLiteral("only %1 vendors").arg(registry.vendorCount())));
        QVERIFY2(registry.modelCount() >= 150,
                 qPrintable(QStringLiteral("only %1 models").arg(registry.modelCount())));
        QVERIFY(registry.regions().contains(QStringLiteral("global")));
        QVERIFY(registry.regions().contains(QStringLiteral("cn")));
        QVERIFY(!registry.localServers().isEmpty());
    }

    void everyVendorPresetIsWellFormed()
    {
        const ProviderRegistry &registry = ProviderRegistry::instance();
        QSet<QString> ids;

        for (const VendorInfo &vendor : registry.vendors()) {
            QVERIFY2(!vendor.id.isEmpty(), "empty vendor id / 空的厂商 id");
            QVERIFY2(!ids.contains(vendor.id), qPrintable(vendor.id));
            ids.insert(vendor.id);

            QVERIFY2(!vendor.labelEn.isEmpty(), qPrintable(vendor.id));
            QVERIFY2(!vendor.labelZh.isEmpty(), qPrintable(vendor.id));
            QVERIFY2(vendor.region == QLatin1String("global")
                         || vendor.region == QLatin1String("cn")
                         || vendor.region == QLatin1String("local"),
                     qPrintable(vendor.id + QLatin1Char(' ') + vendor.region));
            QVERIFY2(!vendor.baseUrl.isEmpty(), qPrintable(vendor.id));
            QVERIFY2(vendor.baseUrl.startsWith(QStringLiteral("http")), qPrintable(vendor.id));
            QVERIFY2(!vendor.authModes.isEmpty(), qPrintable(vendor.id));
            QVERIFY2(!vendor.defaultModel.isEmpty(), qPrintable(vendor.id));
            QVERIFY2(vendor.models.size() >= 1, qPrintable(vendor.id));

            //  Every vendor must be reachable with *some* auth mode, and the
            //  preferred mode must actually be offered.
            QVERIFY2(authModeFromString(vendor.preferredAuth) != AuthMode::ApiKey
                         || vendor.authModes.contains(AuthMode::ApiKey),
                     qPrintable(vendor.id));

            //  Every preset model must have a usable identifier and a sane
            //  capability set.
            QSet<QString> modelIds;
            for (const ModelInfo &model : vendor.models) {
                QVERIFY2(!model.id.isEmpty(), qPrintable(vendor.id));
                QVERIFY2(!modelIds.contains(model.id), qPrintable(vendor.id + QLatin1Char(' ') + model.id));
                modelIds.insert(model.id);
                //  Embedding-only models legitimately do not support chat, so a
                //  chat capability is required only of models that are not
                //  embeddings-only.
                //  纯嵌入模型本来就不支持对话，因此只要求非纯嵌入模型具备对话能力。
                const bool embeddingsOnly = model.caps.testFlag(CapEmbeddings)
                                            && !model.caps.testFlag(CapChat);
                QVERIFY2(model.caps.testFlag(CapChat) || embeddingsOnly,
                         qPrintable(vendor.id + QLatin1Char(' ') + model.id));
                QVERIFY2(model.contextWindow >= 0, qPrintable(model.id));
            }

            //  A vendor that advertises OAuth must publish the endpoints.
            if (vendor.supports(AuthMode::OAuthPkce))
                QVERIFY2(!vendor.oauthAuthorizeUrl.isEmpty(), qPrintable(vendor.id));
            if (vendor.supports(AuthMode::OAuthDevice))
                QVERIFY2(!vendor.oauthDeviceCodeUrl.isEmpty(), qPrintable(vendor.id));
        }
    }

    void vendorLookupAndFilteringWork()
    {
        const ProviderRegistry &registry = ProviderRegistry::instance();

        QVERIFY(registry.find(QStringLiteral("openai")) != nullptr);
        QVERIFY(registry.find(QStringLiteral("anthropic")) != nullptr);
        QVERIFY(registry.find(QStringLiteral("deepseek")) != nullptr);
        QVERIFY(registry.find(QStringLiteral("qwen")) != nullptr);
        QVERIFY(registry.find(QStringLiteral("nope")) == nullptr);

        QVERIFY(!registry.byRegion(QStringLiteral("cn")).isEmpty());
        QVERIFY(!registry.byRegion(QStringLiteral("global")).isEmpty());
        QVERIFY(!registry.supporting(AuthMode::ApiKey).isEmpty());
        QVERIFY(!registry.modelsWithCapability(CapTools).isEmpty());
        QVERIFY(!registry.search(QStringLiteral("qwen")).isEmpty());
        QVERIFY(registry.search(QStringLiteral("zzzz")).isEmpty());
    }

    void customVendorIsUsable()
    {
        const VendorInfo custom = ProviderRegistry::makeCustomVendor(
            QStringLiteral("my-gateway"), QStringLiteral("My Gateway"),
            QStringLiteral("http://127.0.0.1:8000/v1"),
            ApiFlavor::OpenAiCompatible, QStringLiteral("my-model"));

        QCOMPARE(custom.id, QStringLiteral("my-gateway"));
        //  Scoped enums are compared as integers. QTest formats a failure by
        //  calling an unqualified toString() found by ADL, and `tcbw::toString`
        //  returns QString where QTest needs char*, which does not compile.
        //  作用域枚举按整数比较。QTest 在生成失败信息时会调用经 ADL 找到的非限定
        //  toString()，而 `tcbw::toString` 返回 QString、QTest 需要 char*，因此无法编译。
        QCOMPARE(static_cast<int>(custom.flavor),
                 static_cast<int>(ApiFlavor::OpenAiCompatible));
        QVERIFY(custom.supports(AuthMode::ApiKey) || custom.supports(AuthMode::None));
    }

    // --- request building --------------------------------------------------
    void openAiRequestBodyMatchesTheSpec()
    {
        QVector<ChatMessage> messages;
        messages.append(ChatMessage::system(QStringLiteral("be brief")));
        messages.append(ChatMessage::user(QStringLiteral("hello")));

        ProviderConfig config;
        config.streaming = true;
        config.maxTokens = 512;
        config.temperature = 0.3;

        QVector<ToolDescriptor> tools;
        ToolDescriptor tool;
        tool.name = QStringLiteral("add_element");
        tool.descriptionEn = QStringLiteral("Add an element.");
        tool.inputSchema = agenttemplates::schemaObject(
            {{QStringLiteral("tag"), agenttemplates::propString(QStringLiteral("The tag."))}},
            {QStringLiteral("tag")});
        tools.append(tool);

        const QJsonObject body = AiClient::buildRequestBody(
            ApiFlavor::OpenAiCompatible, messages, tools, config,
            QStringLiteral("gpt-4o-mini"), true);

        QCOMPARE(body.value(QStringLiteral("model")).toString(), QStringLiteral("gpt-4o-mini"));
        QCOMPARE(body.value(QStringLiteral("stream")).toBool(), true);
        QVERIFY(body.contains(QStringLiteral("messages")));
        QCOMPARE(body.value(QStringLiteral("messages")).toArray().size(), 2);
        QVERIFY(body.contains(QStringLiteral("tools")));
        QVERIFY(body.value(QStringLiteral("temperature")).toDouble() > 0.29);
    }

    void anthropicRequestBodyHasRequiredMaxTokens()
    {
        QVector<ChatMessage> messages;
        messages.append(ChatMessage::system(QStringLiteral("be brief")));
        messages.append(ChatMessage::user(QStringLiteral("hello")));

        ProviderConfig config;
        config.maxTokens = 256;

        const QJsonObject body = AiClient::buildRequestBody(
            ApiFlavor::Anthropic, messages, {}, config,
            QStringLiteral("claude-3-5-sonnet-latest"), true);

        //  Anthropic requires max_tokens, and takes `system` top-level.
        //  Anthropic 要求 max_tokens，并把 system 放在顶层。
        QVERIFY(body.contains(QStringLiteral("max_tokens")));
        QVERIFY(body.contains(QStringLiteral("system")));
        QVERIFY(!body.contains(QStringLiteral("temperature"))
                || body.value(QStringLiteral("temperature")).isDouble());
    }

    void geminiRequestBodyUsesContents()
    {
        QVector<ChatMessage> messages;
        messages.append(ChatMessage::user(QStringLiteral("hello")));

        const QJsonObject body = AiClient::buildRequestBody(
            ApiFlavor::GoogleGemini, messages, {}, ProviderConfig{},
            QStringLiteral("gemini-2.0-flash"), false);

        QVERIFY(body.contains(QStringLiteral("contents")));
    }

    void chatUrlIsBuiltPerFlavour()
    {
        const ProviderRegistry &registry = ProviderRegistry::instance();
        const VendorInfo *openai = registry.find(QStringLiteral("openai"));
        const VendorInfo *ollama = registry.find(QStringLiteral("ollama"));
        QVERIFY(openai != nullptr);
        QVERIFY(ollama != nullptr);

        ProviderConfig config;
        config.model = openai->defaultModel;
        const QUrl url = AiClient::chatUrl(*openai, config);
        QVERIFY(url.isValid());
        QVERIFY(url.toString().startsWith(QStringLiteral("https://")));
        QVERIFY(url.toString().contains(QStringLiteral("/chat/completions")));

        ProviderConfig ollamaConfig;
        ollamaConfig.model = ollama->defaultModel;
        const QUrl ollamaUrl = AiClient::chatUrl(*ollama, ollamaConfig);
        QVERIFY(ollamaUrl.isValid());
        QVERIFY(ollamaUrl.toString().contains(QStringLiteral("/api/chat")));
    }

    // --- stream parsing ----------------------------------------------------
    void openAiStreamChunksAreParsed()
    {
        const QByteArray chunk =
            QByteArrayLiteral("data: {\"choices\":[{\"delta\":{\"content\":\"Hel\"}}]}\n\n");
        const AiClient::StreamEvent event =
            AiClient::parseStreamChunk(ApiFlavor::OpenAiCompatible, chunk);
        QCOMPARE(static_cast<int>(event.type),
                 static_cast<int>(AiClient::StreamEvent::Text));
        QCOMPARE(event.text, QStringLiteral("Hel"));

        const AiClient::StreamEvent done =
            AiClient::parseStreamChunk(ApiFlavor::OpenAiCompatible,
                                       QByteArrayLiteral("data: [DONE]\n\n"));
        QCOMPARE(static_cast<int>(done.type),
                 static_cast<int>(AiClient::StreamEvent::Done));
    }

    void malformedStreamChunksAreIgnoredNotFatal()
    {
        const AiClient::StreamEvent event =
            AiClient::parseStreamChunk(ApiFlavor::OpenAiCompatible,
                                       QByteArrayLiteral("data: {not json\n\n"));
        QCOMPARE(static_cast<int>(event.type),
                 static_cast<int>(AiClient::StreamEvent::Ignore));

        const AiClient::StreamEvent empty =
            AiClient::parseStreamChunk(ApiFlavor::Anthropic, QByteArray());
        QCOMPARE(static_cast<int>(empty.type),
                 static_cast<int>(AiClient::StreamEvent::Ignore));
    }

    void anthropicStreamChunksAreParsed()
    {
        const QByteArray chunk = QByteArrayLiteral(
            "event: content_block_delta\n"
            "data: {\"type\":\"content_block_delta\",\"delta\":{\"type\":\"text_delta\","
            "\"text\":\"world\"}}\n\n");
        const AiClient::StreamEvent event =
            AiClient::parseStreamChunk(ApiFlavor::Anthropic, chunk);
        QCOMPARE(static_cast<int>(event.type),
                 static_cast<int>(AiClient::StreamEvent::Text));
        QCOMPARE(event.text, QStringLiteral("world"));
    }

    void responseParsingExtractsText()
    {
        //  The JSON is written with escaped quotes rather than a raw string
        //  literal: moc's lexer does not tokenise `R"(...)"`, and a raw literal
        //  anywhere in a file that contains Q_OBJECT silently makes moc emit an
        //  empty .moc, which then fails to link with "vtable ... undefined".
        //  JSON 使用转义引号而非原始字符串字面量：moc 的词法分析器无法识别
        //  `R"(...)"`，而含 Q_OBJECT 的文件中只要出现原始字面量，moc 就会静默产出
        //  空的 .moc，随后链接时报 "vtable ... undefined"。
        static const char *const bodyJson =
            "{"
            "\"choices\":[{\"message\":{\"role\":\"assistant\","
            "\"content\":\"hi there\"}}],"
            "\"usage\":{\"prompt_tokens\":5,\"completion_tokens\":2}"
            "}";

        const QJsonObject body = QJsonDocument::fromJson(QByteArray(bodyJson)).object();

        ChatMessage message;
        AiError error;
        QVERIFY(AiClient::parseResponse(ApiFlavor::OpenAiCompatible, body, &message, &error));
        QCOMPARE(message.content, QStringLiteral("hi there"));
        QCOMPARE(static_cast<int>(message.role), static_cast<int>(ChatRole::Assistant));
        QVERIFY(!error.isValid());
    }

    void errorClassificationIsActionable()
    {
        const AiError unauthorized = AiClient::classifyHttpError(
            401, QByteArrayLiteral("{\"error\":{\"message\":\"bad key\"}}"),
            QStringLiteral("OpenAI"));
        //  QCOMPARE needs a QTest::toString overload, which a scoped enum does
        //  not have, so the values are compared as integers.
        //  QCOMPARE 需要 QTest::toString 重载，而作用域枚举没有该重载，
        //  因此按整数比较。
        QCOMPARE(static_cast<int>(unauthorized.kind), static_cast<int>(AiError::Auth));
        QVERIFY(!unauthorized.messageEn.isEmpty());
        QVERIFY(!unauthorized.messageZh.isEmpty());

        QCOMPARE(static_cast<int>(
                     AiClient::classifyHttpError(429, QByteArray(), QStringLiteral("x")).kind),
                 static_cast<int>(AiError::RateLimit));
        QCOMPARE(static_cast<int>(
                     AiClient::classifyHttpError(500, QByteArray(), QStringLiteral("x")).kind),
                 static_cast<int>(AiError::Server));
        QCOMPARE(static_cast<int>(
                     AiClient::classifyHttpError(504, QByteArray(), QStringLiteral("x")).kind),
                 static_cast<int>(AiError::Timeout));
    }

    void errorDetailsNeverLeakSecrets()
    {
        const QByteArray body = QByteArrayLiteral(
            "{\"error\":{\"message\":\"Invalid key sk-abcdefghijklmnopqrstuvwxyz012345\"}}");
        const AiError error = AiClient::classifyHttpError(401, body, QStringLiteral("OpenAI"));
        QVERIFY(!error.detail.contains(QStringLiteral("sk-abcdefghijklmnopqrstuvwxyz012345")));
    }

    // --- tool registry -----------------------------------------------------
    void registryExposesThirtyTools()
    {
        AgentToolRegistry registry;
        registry.registerBuiltinTools();

        QVERIFY2(registry.toolCount() >= 30,
                 qPrintable(QStringLiteral("only %1 tools").arg(registry.toolCount())));

        const QVector<ToolDescriptor> tools = registry.descriptors();
        QSet<QString> names;
        for (const ToolDescriptor &tool : tools) {
            QVERIFY2(!tool.name.isEmpty(), "empty tool name / 空的工具名");
            QVERIFY2(!names.contains(tool.name), qPrintable(tool.name));
            names.insert(tool.name);

            //  Tool names are part of the model-facing contract, so they must
            //  be stable snake_case identifiers.
            //  工具名是面向模型的契约，因此必须是稳定的下划线命名标识符。
            QVERIFY2(QRegularExpression(QStringLiteral("^[a-z][a-z0-9_]*$"))
                         .match(tool.name).hasMatch(),
                     qPrintable(tool.name));
            QVERIFY2(!tool.descriptionEn.isEmpty(), qPrintable(tool.name));
            QVERIFY2(tool.inputSchema.value(QStringLiteral("type")).toString()
                         == QLatin1String("object"),
                     qPrintable(tool.name));
            QVERIFY2(tool.inputSchema.contains(QStringLiteral("properties")),
                     qPrintable(tool.name));
        }

        QVERIFY(names.contains(QStringLiteral("add_element")));
        QVERIFY(names.contains(QStringLiteral("delete_element")));
        QVERIFY(names.contains(QStringLiteral("add_block")));
        QVERIFY(names.contains(QStringLiteral("export_site")));
    }

    void destructiveToolsAreFlagged()
    {
        AgentToolRegistry registry;
        registry.registerBuiltinTools();

        QVERIFY(registry.isMutating(QStringLiteral("add_element")));
        QVERIFY(registry.isDestructive(QStringLiteral("delete_element")));
        QVERIFY(registry.isDestructive(QStringLiteral("delete_page")));
        QVERIFY(!registry.isDestructive(QStringLiteral("list_pages")));
        QVERIFY(!registry.isMutating(QStringLiteral("list_pages")));

        //  Under the default policy, read-only tools run unattended.
        //  在默认策略下，只读工具可无人值守运行。
        QVERIFY(registry.isAutoApproved(QStringLiteral("list_pages")));
    }

    void neverPolicyRefusesEveryMutation()
    {
        AgentToolRegistry registry;
        registry.registerBuiltinTools();
        registry.setApprovalPolicy(ApprovalPolicy::Never);

        ToolContext context;
        ToolCall call;
        call.id = QStringLiteral("call-1");
        call.name = QStringLiteral("add_element");
        call.arguments = QJsonObject{{QStringLiteral("tag"), QStringLiteral("div")}};

        const ToolResult result = registry.invoke(call, context);
        QVERIFY(result.isError);
        QVERIFY(!result.content.isEmpty());
    }

    void unknownToolIsReportedNotThrown()
    {
        AgentToolRegistry registry;
        registry.registerBuiltinTools();

        ToolContext context;
        ToolCall call;
        call.id = QStringLiteral("call-2");
        call.name = QStringLiteral("no_such_tool");

        const ToolResult result = registry.invoke(call, context);
        QVERIFY(result.isError);
        QVERIFY(!result.content.isEmpty());
    }

    void openAiToolSchemaIsWellFormed()
    {
        ToolDescriptor tool;
        tool.name = QStringLiteral("add_element");
        tool.descriptionEn = QStringLiteral("Add an element to the page.");

        const QJsonObject schema = agenttemplates::schemaObject(
            {{QStringLiteral("tag"), agenttemplates::propString(QStringLiteral("Tag name."))},
             {QStringLiteral("index"), agenttemplates::propNumber(QStringLiteral("Position."))}},
            {QStringLiteral("tag")});
        tool.inputSchema = schema;

        const QJsonObject wrapped = tool.toOpenAiTool();
        QCOMPARE(wrapped.value(QStringLiteral("type")).toString(), QStringLiteral("function"));
        const QJsonObject function = wrapped.value(QStringLiteral("function")).toObject();
        QCOMPARE(function.value(QStringLiteral("name")).toString(), QStringLiteral("add_element"));
        QVERIFY(function.contains(QStringLiteral("parameters")));

        const QJsonArray anthropic = buildAnthropicToolSchema({tool});
        QCOMPARE(anthropic.size(), 1);
        QVERIFY(anthropic.first().toObject().contains(QStringLiteral("input_schema")));
    }

    // --- secrets -----------------------------------------------------------
    void secretStoreProtectionIsDescribedHonestly()
    {
        SecretStore &store = SecretStore::instance();
        QString error;
        store.initialise(&error);

        const QString description = store.protectionDescription(false);
        QVERIFY(!description.isEmpty());
        const QString descriptionZh = store.protectionDescription(true);
        QVERIFY(!descriptionZh.isEmpty());
        QVERIFY(descriptionZh != description);

        //  When the backend is not OS-backed the wording must not claim
        //  encryption, because a co-located key file is only obfuscation.
        //  当后端并非由操作系统保护时，措辞不得声称已加密，因为与数据同处的密钥文件
        //  只能算混淆。
        if (!store.isHardwareOrOsBacked()) {
            QVERIFY(!description.contains(QStringLiteral("encrypted"), Qt::CaseInsensitive)
                    || description.contains(QStringLiteral("not"), Qt::CaseInsensitive));
        }
    }

    void redactForPromptDelegatesToTheSharedRedactor()
    {
        const QString text = QStringLiteral("token=sk-abcdefghijklmnopqrstuvwxyz012345");
        QVERIFY(!redactForPrompt(text).contains(QStringLiteral("sk-abcdefghijklmnopqrstuvwxyz012345")));
    }

    void systemPromptMentionsTheGuardRails()
    {
        const QString english = defaultSystemPrompt(false);
        QVERIFY(!english.isEmpty());
        QVERIFY(english.contains(QStringLiteral("tool"), Qt::CaseInsensitive));

        const QString chinese = defaultSystemPrompt(true);
        QVERIFY(!chinese.isEmpty());
        //  The Chinese prompt must not simply be the English one.
        //  中文提示词不能只是英文提示词的副本。
        QVERIFY(chinese != english);
    }

    void messageFactoriesAndJsonRoundTrip()
    {
        const ChatMessage user = ChatMessage::user(QStringLiteral("hi"));
        QCOMPARE(static_cast<int>(user.role), static_cast<int>(ChatRole::User));
        QCOMPARE(user.content, QStringLiteral("hi"));

        const ChatMessage tool = ChatMessage::tool(QStringLiteral("call-9"),
                                                   QStringLiteral("add_element"),
                                                   QStringLiteral("{\"ok\":true}"));
        QCOMPARE(static_cast<int>(tool.role), static_cast<int>(ChatRole::Tool));
        QCOMPARE(tool.toolCallId, QStringLiteral("call-9"));

        const ChatMessage restored = ChatMessage::fromJson(user.toJson());
        QCOMPARE(static_cast<int>(restored.role), static_cast<int>(ChatRole::User));
        QCOMPARE(restored.content, QStringLiteral("hi"));
    }

    void toolCallParsesBothWireShapes()
    {
        //  OpenAI shape / OpenAI 形态 (escaped, not a raw string -- see above)
        static const char *const openAiJson =
            "{"
            "\"id\":\"call_1\","
            "\"type\":\"function\","
            "\"function\":{\"name\":\"add_element\","
            "\"arguments\":\"{\\\"tag\\\":\\\"div\\\"}\"}"
            "}";
        const QJsonObject openAi =
            QJsonDocument::fromJson(QByteArray(openAiJson)).object();
        const ToolCall parsedOpenAi = ToolCall::fromJson(openAi);
        QCOMPARE(parsedOpenAi.name, QStringLiteral("add_element"));
        QCOMPARE(parsedOpenAi.arguments.value(QStringLiteral("tag")).toString(),
                 QStringLiteral("div"));

        //  Anthropic shape / Anthropic 形态 (escaped, not a raw string)
        static const char *const anthropicJson =
            "{"
            "\"id\":\"toolu_1\","
            "\"name\":\"add_element\","
            "\"input\":{\"tag\":\"section\"}"
            "}";
        const QJsonObject anthropic =
            QJsonDocument::fromJson(QByteArray(anthropicJson)).object();
        const ToolCall parsedAnthropic = ToolCall::fromJson(anthropic);
        QCOMPARE(parsedAnthropic.name, QStringLiteral("add_element"));
        QCOMPARE(parsedAnthropic.arguments.value(QStringLiteral("tag")).toString(),
                 QStringLiteral("section"));
    }
};

QTEST_MAIN(TestAiProviders)
#include "tst_ai_providers.moc"
