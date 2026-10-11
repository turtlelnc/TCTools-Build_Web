// ============================================================================
//  ProviderRegistry.cpp -- the preset catalogue of AI vendors
//  ProviderRegistry.cpp —— AI 厂商预设目录
//
//  SCOPE / 范围
//    Only public, documented facts live here: endpoint shape, authentication
//    style, published model identifiers and their documented limits. No
//    credentials, no account-specific values, no price guessing.
//    这里只收录公开且可考证的事实：接口形态、授权方式、公开的模型标识及其文档化的
//    限制。不含任何凭据、账号相关取值，也不猜测价格。
//
//  PRICES / 价格
//    Every price field is -1.0 ("unknown") on purpose. Vendor price lists change
//    frequently and a wrong number in a cost estimate is worse than no number.
//    pricing() exists so a maintainer who has verified a price can record it.
//    所有价格字段一律为 -1.0（未知），这是刻意为之：厂商价格表变动频繁，
//    费用估算里错误的数字比没有数字更糟。pricing() 供已核实价格的维护者填写。
// ============================================================================
#include "ProviderRegistry.h"

#include <QStringList>

#include <algorithm>

namespace tcbw {
namespace {

// ---------------------------------------------------------------------------
//  Capability shorthands
//  能力简写
// ---------------------------------------------------------------------------
const ModelCapabilities kChat          = CapChat | CapStreaming;
const ModelCapabilities kChatTools     = CapChat | CapStreaming | CapTools;
const ModelCapabilities kChatVision    = CapChat | CapStreaming | CapTools | CapVision;
const ModelCapabilities kChatReason    = CapChat | CapStreaming | CapTools | CapReasoning;
const ModelCapabilities kChatVisReason = CapChat | CapStreaming | CapTools | CapVision
                                       | CapReasoning;
const ModelCapabilities kEmbed         = CapEmbeddings;
const ModelCapabilities kImage         = CapImage;
//  Speech / video endpoints: they stream a result but have no chat surface.
//  语音 / 视频端点：结果可流式返回，但没有对话接口。
const ModelCapabilities kMedia         = CapStreaming;

// ---------------------------------------------------------------------------
//  VendorBuilder -- keeps the table below readable
//  厂商构造器 —— 让下面的表格保持可读
// ---------------------------------------------------------------------------
class VendorBuilder
{
public:
    VendorBuilder(const char *id, const char *labelEn, const char *labelZh,
                  const char *region, ApiFlavor flavor)
    {
        m_v.id = QString::fromUtf8(id);
        m_v.labelEn = QString::fromUtf8(labelEn);
        m_v.labelZh = QString::fromUtf8(labelZh);
        m_v.region = QString::fromUtf8(region);
        m_v.flavor = flavor;
        m_v.isLocal = m_v.region == QLatin1String("local");
    }

    VendorBuilder &links(const char *homepage, const char *docs, const char *keys)
    {
        m_v.homepageUrl = QString::fromUtf8(homepage);
        m_v.docsUrl = QString::fromUtf8(docs);
        m_v.keysUrl = QString::fromUtf8(keys);
        return *this;
    }

    VendorBuilder &api(const char *baseUrl, const char *chatPath)
    {
        m_v.baseUrl = QString::fromUtf8(baseUrl);
        m_v.chatPath = QString::fromUtf8(chatPath);
        return *this;
    }

    VendorBuilder &auth(std::initializer_list<AuthMode> modes, const char *preferred)
    {
        m_v.authModes = QVector<AuthMode>(modes.begin(), modes.end());
        m_v.preferredAuth = QString::fromUtf8(preferred);
        return *this;
    }

    VendorBuilder &keyHint(const char *prefix, int minLength,
                           std::initializer_list<const char *> headerNames)
    {
        m_v.keyPrefixHint = QString::fromUtf8(prefix);
        m_v.keyMinLength = minLength;
        m_v.keyHeaderNames.clear();
        for (const char *name : headerNames)
            m_v.keyHeaderNames.append(QString::fromUtf8(name));
        return *this;
    }

    VendorBuilder &flags(bool tools, bool streaming, bool vision, bool reasoning)
    {
        m_v.supportsTools = tools;
        m_v.supportsStreaming = streaming;
        m_v.supportsVision = vision;
        m_v.supportsReasoning = reasoning;
        return *this;
    }

    VendorBuilder &model(const char *id, ModelCapabilities caps,
                         int contextWindow, int maxOutputTokens, const char *label = nullptr)
    {
        ModelInfo info;
        info.id = QString::fromUtf8(id);
        info.label = label ? QString::fromUtf8(label) : info.id;
        info.caps = caps;
        info.contextWindow = contextWindow;
        info.maxOutputTokens = maxOutputTokens;
        m_v.models.append(info);
        return *this;
    }

    //  Records a verified price for the model added last (USD per 1M tokens).
    //  为最后添加的模型记录已核实的价格（美元 / 百万令牌）。
    VendorBuilder &pricing(double inputPerMillion, double outputPerMillion)
    {
        if (!m_v.models.isEmpty()) {
            m_v.models.last().inputPricePerMillion = inputPerMillion;
            m_v.models.last().outputPricePerMillion = outputPerMillion;
        }
        return *this;
    }

    VendorBuilder &fallbackModel(const char *id) { m_v.defaultModel = QString::fromUtf8(id);
                                                   return *this; }

    VendorBuilder &oauth(const char *authorize, const char *token, const char *deviceCode,
                         const char *scopes, bool requiresUserClientId, bool pkce = true)
    {
        m_v.oauthAuthorizeUrl = QString::fromUtf8(authorize);
        m_v.oauthTokenUrl = QString::fromUtf8(token);
        m_v.oauthDeviceCodeUrl = QString::fromUtf8(deviceCode);
        m_v.oauthScopes = QString::fromUtf8(scopes);
        m_v.oauthRequiresUserClientId = requiresUserClientId;
        m_v.oauthUsesPkce = pkce;
        return *this;
    }

    VendorBuilder &note(const char *en, const char *zh)
    {
        m_v.notesEn.append(QString::fromUtf8(en));
        m_v.notesZh.append(QString::fromUtf8(zh));
        return *this;
    }

    VendorInfo build() const { return m_v; }

private:
    VendorInfo m_v;
};

} // namespace

// ---------------------------------------------------------------------------
//  Construction
//  构造
// ---------------------------------------------------------------------------
ProviderRegistry::ProviderRegistry()
{
    registerVendors();
}

const ProviderRegistry &ProviderRegistry::instance()
{
    //  Function-local static: thread-safe initialisation since C++11, and the
    //  table is built exactly once.
    //  函数内静态变量：自 C++11 起线程安全初始化，表格只构建一次。
    static const ProviderRegistry registry;
    return registry;
}

void ProviderRegistry::add(const VendorInfo &vendor)
{
    if (vendor.id.isEmpty())
        return;
    //  Idempotent: a second registration of the same id is ignored, so the
    //  first (and most carefully reviewed) definition always wins and the
    //  registration order stays stable for the UI.
    //  幂等：重复注册同一 id 会被忽略，因此首个（也是经过最仔细校对的）定义始终
    //  生效，界面看到的注册顺序保持稳定。
    const QString id = vendor.id.toLower();
    if (m_byId.contains(id))
        return;
    m_byId.insert(id, static_cast<int>(m_vendors.size()));
    m_vendors.append(vendor);
}

void ProviderRegistry::registerVendors()
{
    m_vendors.reserve(64);
    m_byId.reserve(64);

    // =======================================================================
    //  GLOBAL VENDORS
    //  国际厂商
    // =======================================================================

    // --- OpenAI ------------------------------------------------------------
    add(VendorBuilder("openai", "OpenAI", "OpenAI（美国）", "global", ApiFlavor::OpenAiCompatible)
            .links("https://openai.com",
                   "https://platform.openai.com/docs/api-reference",
                   "https://platform.openai.com/api-keys")
            .api("https://api.openai.com/v1", "/chat/completions")
            .auth({AuthMode::ApiKey, AuthMode::Bearer, AuthMode::OAuthPkce}, "api-key")
            .keyHint("sk-", 40, {"Authorization"})
            .flags(true, true, true, true)
            .model("gpt-4o", kChatVision, 128000, 16384)
            .model("gpt-4o-mini", kChatVision, 128000, 16384)
            .model("gpt-4.1", kChatVision, 1047576, 32768)
            .model("o3", kChatReason, 200000, 100000)
            .fallbackModel("gpt-4o")
            .oauth("https://auth.openai.com/oauth/authorize",
                   "https://auth.openai.com/oauth/token", "",
                   "openid profile email offline_access", true)
            .note("Organisation and project ids are sent as OpenAI-Organization / OpenAI-Project "
                  "headers when configured.",
                  "配置了组织与项目 id 时，会以 OpenAI-Organization / OpenAI-Project 请求头发送。")
            .note("Reasoning models (o-series) reject temperature, top_p and system messages.",
                  "推理模型（o 系列）不接受 temperature、top_p，也不接受 system 消息。")
            .note("OAuth sign-in is published for OpenAI's own first-party clients; register your "
                  "own client id to use it here.",
                  "OAuth 登录目前只面向 OpenAI 自家客户端发布；如需在此使用，请自行注册客户端 id。")
            .build());

    // --- Anthropic ---------------------------------------------------------
    add(VendorBuilder("anthropic", "Anthropic (Claude)", "Anthropic（Claude）", "global",
                      ApiFlavor::Anthropic)
            .links("https://www.anthropic.com",
                   "https://docs.anthropic.com/en/api/messages",
                   "https://console.anthropic.com/settings/keys")
            .api("https://api.anthropic.com/v1", "/messages")
            .auth({AuthMode::ApiKey, AuthMode::Bearer}, "api-key")
            .keyHint("sk-ant-", 40, {"x-api-key"})
            .flags(true, true, true, true)
            .model("claude-sonnet-4-20250514", kChatVisReason, 200000, 64000)
            .model("claude-opus-4-20250514", kChatVisReason, 200000, 32000)
            .model("claude-3-7-sonnet-20250219", kChatVisReason, 200000, 64000)
            .model("claude-3-5-haiku-20241022", kChatVision, 200000, 8192)
            .fallbackModel("claude-sonnet-4-20250514")
            .note("Requires the anthropic-version: 2023-06-01 header on every request.",
                  "每个请求都必须带 anthropic-version: 2023-06-01 请求头。")
            .note("The system prompt is a top-level parameter, not a message with role system.",
                  "系统提示词是顶层参数，而不是 role 为 system 的消息。")
            .note("Abuse of the browser-direct header is blocked; a desktop app must set "
                  "anthropic-dangerous-direct-browser-access only when it really is a browser.",
                  "直接来自浏览器的请求会被拦截；只有在真正的浏览器环境中才应设置 "
                  "anthropic-dangerous-direct-browser-access。")
            .build());

    // --- Google AI Studio (Gemini API) -------------------------------------
    add(VendorBuilder("google-gemini", "Google Gemini (AI Studio)", "谷歌 Gemini（AI Studio）",
                      "global", ApiFlavor::GoogleGemini)
            .links("https://ai.google.dev",
                   "https://ai.google.dev/gemini-api/docs",
                   "https://aistudio.google.com/app/apikey")
            .api("https://generativelanguage.googleapis.com/v1beta",
                 "/models/{model}:generateContent")
            .auth({AuthMode::ApiKey, AuthMode::Bearer}, "api-key")
            .keyHint("AIza", 39, {"x-goog-api-key", "Authorization"})
            .flags(true, true, true, true)
            .model("gemini-2.5-pro", kChatVisReason, 1048576, 65536)
            .model("gemini-2.5-flash", kChatVisReason, 1048576, 65536)
            .model("gemini-2.0-flash", kChatVision, 1048576, 8192)
            .model("text-embedding-004", kEmbed, 2048, 0)
            .fallbackModel("gemini-2.5-flash")
            .note("The model id is part of the path ({model}); streaming uses "
                  ":streamGenerateContent?alt=sse.",
                  "模型 id 出现在路径中（{model}）；流式请求使用 "
                  ":streamGenerateContent?alt=sse。")
            .note("API keys go in the x-goog-api-key header (or ?key=); an OAuth 2.0 access "
                  "token goes in Authorization: Bearer.",
                  "API Key 放在 x-goog-api-key 请求头（或 ?key=）中；OAuth 2.0 访问令牌放在 "
                  "Authorization: Bearer 中。")
            .build());

    // --- Google Vertex AI --------------------------------------------------
    add(VendorBuilder("google-vertex", "Google Vertex AI", "谷歌 Vertex AI", "global",
                      ApiFlavor::GoogleGemini)
            .links("https://cloud.google.com/vertex-ai",
                   "https://cloud.google.com/vertex-ai/generative-ai/docs",
                   "https://console.cloud.google.com/apis/credentials")
            .api("https://{region}-aiplatform.googleapis.com/v1",
                 "/projects/{project}/locations/{region}/publishers/google/models/{model}"
                 ":generateContent")
            .auth({AuthMode::Bearer, AuthMode::OAuthPkce, AuthMode::OAuthDevice, AuthMode::ApiKey},
                  "bearer")
            .keyHint("", 16, {"Authorization", "x-goog-api-key"})
            .flags(true, true, true, true)
            .model("gemini-2.5-pro", kChatVisReason, 1048576, 65536)
            .model("gemini-2.5-flash", kChatVisReason, 1048576, 65536)
            .model("gemini-2.0-flash", kChatVision, 1048576, 8192)
            .model("imagen-3.0-generate-002", kImage, 480, 0)
            .fallbackModel("gemini-2.5-flash")
            .oauth("https://accounts.google.com/o/oauth2/v2/auth",
                   "https://oauth2.googleapis.com/token",
                   "https://oauth2.googleapis.com/device/code",
                   "https://www.googleapis.com/auth/cloud-platform", true)
            .note("The project id and region must be filled in; the host is region specific "
                  "(for example europe-west4-aiplatform.googleapis.com).",
                  "必须填写项目 id 与区域；主机名与区域相关（例如 "
                  "europe-west4-aiplatform.googleapis.com）。")
            .note("Vertex normally authenticates with an OAuth 2.0 access token or Application "
                  "Default Credentials; Express mode also accepts an API key.",
                  "Vertex 通常使用 OAuth 2.0 访问令牌或应用默认凭据；快捷模式（Express）也接受 "
                  "API Key。")
            .build());

    // --- Azure OpenAI ------------------------------------------------------
    add(VendorBuilder("azure-openai", "Azure OpenAI Service", "Azure OpenAI 服务", "global",
                      ApiFlavor::OpenAiCompatible)
            .links("https://azure.microsoft.com/products/ai-services/openai-service",
                   "https://learn.microsoft.com/azure/ai-services/openai/reference",
                   "https://portal.azure.com")
            .api("https://{resource}.openai.azure.com/openai",
                 "/deployments/{deployment}/chat/completions")
            .auth({AuthMode::ApiKey, AuthMode::Bearer, AuthMode::OAuthDevice}, "api-key")
            .keyHint("", 32, {"api-key", "Authorization"})
            .flags(true, true, true, false)
            .model("gpt-4o", kChatVision, 128000, 16384)
            .model("gpt-4o-mini", kChatVision, 128000, 16384)
            .model("gpt-4.1", kChatVision, 1047576, 32768)
            .model("o4-mini", kChatReason, 200000, 100000)
            .fallbackModel("gpt-4o")
            .oauth("https://login.microsoftonline.com/common/oauth2/v2.0/authorize",
                   "https://login.microsoftonline.com/common/oauth2/v2.0/token",
                   "https://login.microsoftonline.com/common/oauth2/v2.0/devicecode",
                   "https://cognitiveservices.azure.com/.default offline_access", true, false)
            .note("The endpoint is per resource and the model field must be the DEPLOYMENT name "
                  "you gave in Azure, not the model family name.",
                  "端点按资源分配；model 字段必须填写你在 Azure 中的部署名称，而不是模型系列名。")
            .note("Replace the {resource} placeholder in the base URL with your Azure OpenAI "
                  "resource name, for example https://contoso.openai.azure.com/openai.",
                  "请把根地址中的 {resource} 占位符替换为你的 Azure OpenAI 资源名称，"
                  "例如 https://contoso.openai.azure.com/openai。")
            .note("Every call needs an api-version query parameter (for example 2024-10-21).",
                  "每次调用都需要 api-version 查询参数（例如 2024-10-21）。")
            .note("Entra ID (Azure AD) access tokens are sent as Authorization: Bearer instead "
                  "of the api-key header.",
                  "Entra ID（Azure AD）访问令牌使用 Authorization: Bearer 发送，而不是 api-key 头。")
            .build());

    // --- AWS Bedrock -------------------------------------------------------
    add(VendorBuilder("aws-bedrock", "Amazon Bedrock", "亚马逊 Bedrock", "global",
                      ApiFlavor::Anthropic)
            .links("https://aws.amazon.com/bedrock",
                   "https://docs.aws.amazon.com/bedrock/latest/userguide/what-is-bedrock.html",
                   "https://console.aws.amazon.com/iam/home#/security_credentials")
            .api("https://bedrock-runtime.{region}.amazonaws.com", "/model/{model}/invoke")
            .auth({AuthMode::AwsSigV4, AuthMode::ApiKey, AuthMode::Bearer, AuthMode::None},
                  "aws-sigv4")
            .keyHint("", 20, {"Authorization"})
            .flags(true, true, true, false)
            .model("anthropic.claude-sonnet-4-20250514-v1:0", kChatVision, 200000, 64000)
            .model("anthropic.claude-3-5-sonnet-20241022-v2:0", kChatVision, 200000, 8192)
            .model("amazon.nova-pro-v1:0", kChatVision, 300000, 5120)
            .model("meta.llama3-3-70b-instruct-v1:0", kChatTools, 128000, 8192)
            .model("amazon.titan-embed-text-v2:0", kEmbed, 8192, 0)
            .fallbackModel("anthropic.claude-3-5-sonnet-20241022-v2:0")
            .note("Requests must be signed with AWS Signature Version 4 and the model must be "
                  "enabled for your account in that region.",
                  "请求必须使用 AWS 签名版本 4 签名，且该模型需在对应区域为你的账号开通。")
            .note("The body follows the Anthropic Messages shape for Claude models, but the "
                  "endpoint is /model/<modelId>/invoke, not /v1/messages.",
                  "Claude 模型的请求体遵循 Anthropic Messages 形态，但端点是 "
                  "/model/<modelId>/invoke，而不是 /v1/messages。")
            .note("Bedrock API keys can be used as Authorization: Bearer as an alternative to "
                  "SigV4.",
                  "作为 SigV4 的替代方案，也可以把 Bedrock API Key 用作 Authorization: Bearer。")
            .build());

    // --- Mistral AI --------------------------------------------------------
    add(VendorBuilder("mistral", "Mistral AI", "Mistral AI（法国）", "global",
                      ApiFlavor::OpenAiCompatible)
            .links("https://mistral.ai", "https://docs.mistral.ai",
                   "https://console.mistral.ai/api-keys")
            .api("https://api.mistral.ai/v1", "/chat/completions")
            .auth({AuthMode::ApiKey, AuthMode::Bearer}, "api-key")
            .keyHint("", 32, {"Authorization"})
            .flags(true, true, true, false)
            .model("mistral-large-latest", kChatTools, 131072, 32768)
            .model("mistral-small-latest", kChatTools, 131072, 32768)
            .model("open-mistral-nemo", kChatTools, 131072, 8192)
            .model("codestral-latest", kChatTools, 262144, 8192)
            .model("pixtral-large-latest", kChatVision, 131072, 8192)
            .fallbackModel("mistral-large-latest")
            .build());

    // --- Cohere ------------------------------------------------------------
    add(VendorBuilder("cohere", "Cohere", "Cohere（加拿大）", "global",
                      ApiFlavor::OpenAiCompatible)
            .links("https://cohere.com", "https://docs.cohere.com",
                   "https://dashboard.cohere.com/api-keys")
            .api("https://api.cohere.ai/compatibility/v1", "/chat/completions")
            .auth({AuthMode::ApiKey, AuthMode::Bearer}, "api-key")
            .keyHint("", 32, {"Authorization"})
            .flags(true, true, false, false)
            .model("command-a-03-2025", kChatTools, 256000, 8192)
            .model("command-r-plus-08-2024", kChatTools, 128000, 4096)
            .model("command-r-08-2024", kChatTools, 128000, 4096)
            .model("embed-multilingual-v3.0", kEmbed, 512, 0)
            .fallbackModel("command-r-plus-08-2024")
            .note("The base URL above is Cohere's OpenAI-compatibility layer; the native API is "
                  "https://api.cohere.com/v2/chat with a different body.",
                  "上面的根地址是 Cohere 的 OpenAI 兼容层；原生接口是 "
                  "https://api.cohere.com/v2/chat，请求体不同。")
            .build());

    // --- AI21 Labs ---------------------------------------------------------
    add(VendorBuilder("ai21", "AI21 Labs (Jamba)", "AI21 Labs（Jamba）", "global",
                      ApiFlavor::OpenAiCompatible)
            .links("https://www.ai21.com", "https://docs.ai21.com",
                   "https://studio.ai21.com/account/api-key")
            .api("https://api.ai21.com/studio/v1", "/chat/completions")
            .auth({AuthMode::ApiKey, AuthMode::Bearer}, "api-key")
            .keyHint("", 32, {"Authorization"})
            .flags(true, true, false, false)
            .model("jamba-1.5-large", kChatTools, 256000, 4096)
            .model("jamba-1.5-mini", kChatTools, 256000, 4096)
            .model("jamba-instruct", kChatTools, 256000, 4096)
            .fallbackModel("jamba-1.5-large")
            .build());

    // --- Perplexity --------------------------------------------------------
    add(VendorBuilder("perplexity", "Perplexity", "Perplexity（美国）", "global",
                      ApiFlavor::OpenAiCompatible)
            .links("https://www.perplexity.ai", "https://docs.perplexity.ai",
                   "https://www.perplexity.ai/settings/api")
            .api("https://api.perplexity.ai", "/chat/completions")
            .auth({AuthMode::ApiKey, AuthMode::Bearer}, "api-key")
            .keyHint("pplx-", 40, {"Authorization"})
            .flags(true, true, false, true)
            .model("sonar-pro", kChatReason, 200000, 8192)
            .model("sonar", kChat, 128000, 4096)
            .model("sonar-reasoning", kChatReason, 128000, 8192)
            .model("sonar-deep-research", kChatReason, 128000, 8192)
            .fallbackModel("sonar")
            .note("Responses carry citations in a separate field; there is no public embedding "
                  "or image endpoint.",
                  "回复中的引用信息位于单独字段；该平台没有公开的向量或图像端点。")
            .build());

    // --- xAI ---------------------------------------------------------------
    add(VendorBuilder("xai", "xAI (Grok)", "xAI（Grok）", "global", ApiFlavor::OpenAiCompatible)
            .links("https://x.ai", "https://docs.x.ai", "https://console.x.ai")
            .api("https://api.x.ai/v1", "/chat/completions")
            .auth({AuthMode::ApiKey, AuthMode::Bearer}, "api-key")
            .keyHint("xai-", 40, {"Authorization"})
            .flags(true, true, true, true)
            .model("grok-4", kChatVisReason, 256000, 32768)
            .model("grok-3", kChatTools, 131072, 8192)
            .model("grok-3-mini", kChatReason, 131072, 8192)
            .model("grok-2-vision-1212", kChatVision, 32768, 8192)
            .fallbackModel("grok-3")
            .build());

    // --- Groq --------------------------------------------------------------
    add(VendorBuilder("groq", "Groq", "Groq（美国）", "global", ApiFlavor::OpenAiCompatible)
            .links("https://groq.com", "https://console.groq.com/docs",
                   "https://console.groq.com/keys")
            .api("https://api.groq.com/openai/v1", "/chat/completions")
            .auth({AuthMode::ApiKey, AuthMode::Bearer}, "api-key")
            .keyHint("gsk_", 40, {"Authorization"})
            .flags(true, true, true, true)
            .model("llama-3.3-70b-versatile", kChatTools, 131072, 32768)
            .model("llama-3.1-8b-instant", kChatTools, 131072, 8192)
            .model("meta-llama/llama-4-scout-17b-16e-instruct", kChatVision, 131072, 8192)
            .model("qwen/qwen3-32b", kChatReason, 131072, 16384)
            .fallbackModel("llama-3.3-70b-versatile")
            .note("Model ids change frequently on Groq; read GET /openai/v1/models before "
                  "hard-coding one.",
                  "Groq 的模型标识变动频繁；写死之前请先调用 GET /openai/v1/models 查看。")
            .build());

    // --- Together AI -------------------------------------------------------
    add(VendorBuilder("together", "Together AI", "Together AI（美国）", "global",
                      ApiFlavor::OpenAiCompatible)
            .links("https://www.together.ai", "https://docs.together.ai",
                   "https://api.together.xyz/settings/api-keys")
            .api("https://api.together.xyz/v1", "/chat/completions")
            .auth({AuthMode::ApiKey, AuthMode::Bearer}, "api-key")
            .keyHint("", 40, {"Authorization"})
            .flags(true, true, true, true)
            .model("meta-llama/Llama-3.3-70B-Instruct-Turbo", kChatTools, 131072, 8192)
            .model("Qwen/Qwen2.5-72B-Instruct-Turbo", kChatTools, 32768, 8192)
            .model("deepseek-ai/DeepSeek-V3", kChatTools, 65536, 8192)
            .model("meta-llama/Llama-4-Scout-17B-16E-Instruct", kChatVision, 131072, 8192)
            .model("BAAI/bge-large-en-v1.5", kEmbed, 512, 0)
            .fallbackModel("meta-llama/Llama-3.3-70B-Instruct-Turbo")
            .build());

    // --- Fireworks AI ------------------------------------------------------
    add(VendorBuilder("fireworks", "Fireworks AI", "Fireworks AI（美国）", "global",
                      ApiFlavor::OpenAiCompatible)
            .links("https://fireworks.ai", "https://docs.fireworks.ai",
                   "https://fireworks.ai/account/api-keys")
            .api("https://api.fireworks.ai/inference/v1", "/chat/completions")
            .auth({AuthMode::ApiKey, AuthMode::Bearer}, "api-key")
            .keyHint("fw_", 32, {"Authorization"})
            .flags(true, true, true, true)
            .model("accounts/fireworks/models/llama-v3p3-70b-instruct", kChatTools, 131072, 16384)
            .model("accounts/fireworks/models/deepseek-v3", kChatTools, 65536, 8192)
            .model("accounts/fireworks/models/qwen2p5-72b-instruct", kChatTools, 32768, 8192)
            .model("accounts/fireworks/models/llama4-scout-instruct-basic", kChatVision, 131072,
                   8192)
            .fallbackModel("accounts/fireworks/models/llama-v3p3-70b-instruct")
            .note("Model ids are full resource paths that start with accounts/<you>/models/ for "
                  "fine-tuned deployments.",
                  "模型标识是完整资源路径；微调部署以 accounts/<你的账号>/models/ 开头。")
            .build());

    // --- Replicate ---------------------------------------------------------
    add(VendorBuilder("replicate", "Replicate", "Replicate（美国）", "global",
                      ApiFlavor::OpenAiCompatible)
            .links("https://replicate.com", "https://replicate.com/docs",
                   "https://replicate.com/account/api-tokens")
            .api("https://api.replicate.com/v1", "/chat/completions")
            .auth({AuthMode::ApiKey, AuthMode::Bearer}, "api-key")
            .keyHint("r8_", 40, {"Authorization"})
            .flags(true, true, true, false)
            .model("meta/meta-llama-3.1-405b-instruct", kChatTools, 128000, 8192)
            .model("anthropic/claude-3.7-sonnet", kChatVision, 200000, 8192)
            .model("openai/gpt-4o-mini", kChatVision, 128000, 16384)
            .model("black-forest-labs/flux-1.1-pro", kImage, 0, 0)
            .fallbackModel("meta/meta-llama-3.1-405b-instruct")
            .note("Replicate's own API is an asynchronous prediction API at /v1/predictions; the "
                  "OpenAI-compatible surface shown here covers language models only.",
                  "Replicate 原生接口是 /v1/predictions 的异步预测接口；此处展示的 OpenAI 兼容"
                  "接口只覆盖语言模型。")
            .build());

    // --- Hugging Face ------------------------------------------------------
    add(VendorBuilder("huggingface", "Hugging Face (Inference Providers)",
                      "Hugging Face（推理服务商）", "global", ApiFlavor::OpenAiCompatible)
            .links("https://huggingface.co",
                   "https://huggingface.co/docs/inference-providers",
                   "https://huggingface.co/settings/tokens")
            .api("https://router.huggingface.co/v1", "/chat/completions")
            .auth({AuthMode::ApiKey, AuthMode::Bearer}, "api-key")
            .keyHint("hf_", 34, {"Authorization"})
            .flags(true, true, true, true)
            .model("meta-llama/Llama-3.3-70B-Instruct", kChatTools, 131072, 8192)
            .model("Qwen/Qwen2.5-72B-Instruct", kChatTools, 32768, 8192)
            .model("deepseek-ai/DeepSeek-V3-0324", kChatTools, 65536, 8192)
            .model("meta-llama/Llama-4-Scout-17B-16E-Instruct", kChatVision, 131072, 8192)
            .fallbackModel("meta-llama/Llama-3.3-70B-Instruct")
            .note("A fine-grained token with the 'Make calls to Inference Providers' permission "
                  "is required; read-only tokens are rejected.",
                  "需要具备「调用推理服务商」权限的细粒度令牌；只读令牌会被拒绝。")
            .build());

    // --- DeepSeek ----------------------------------------------------------
    add(VendorBuilder("deepseek", "DeepSeek", "深度求索 DeepSeek", "global",
                      ApiFlavor::OpenAiCompatible)
            .links("https://www.deepseek.com", "https://api-docs.deepseek.com",
                   "https://platform.deepseek.com/api_keys")
            .api("https://api.deepseek.com/v1", "/chat/completions")
            .auth({AuthMode::ApiKey, AuthMode::Bearer}, "api-key")
            .keyHint("sk-", 32, {"Authorization"})
            .flags(true, true, false, true)
            .model("deepseek-chat", kChatTools, 65536, 8192)
            .model("deepseek-reasoner", kChatReason, 65536, 32768)
            .model("deepseek-coder", kChatTools, 131072, 4096)
            .fallbackModel("deepseek-chat")
            .note("deepseek-coder is legacy and kept only for existing projects; new work should "
                  "use deepseek-chat.",
                  "deepseek-coder 属于遗留模型，仅为兼容既有工程保留；新项目请使用 "
                  "deepseek-chat。")
            .note("deepseek-reasoner returns its chain of thought in reasoning_content and does "
                  "not accept a temperature.",
                  "deepseek-reasoner 通过 reasoning_content 返回思维链，且不接受 temperature。")
            .build());

    // --- OpenRouter --------------------------------------------------------
    add(VendorBuilder("openrouter", "OpenRouter", "OpenRouter（聚合）", "global",
                      ApiFlavor::OpenAiCompatible)
            .links("https://openrouter.ai", "https://openrouter.ai/docs",
                   "https://openrouter.ai/keys")
            .api("https://openrouter.ai/api/v1", "/chat/completions")
            .auth({AuthMode::ApiKey, AuthMode::Bearer}, "api-key")
            .keyHint("sk-or-", 40, {"Authorization"})
            .flags(true, true, true, true)
            .model("openai/gpt-4o", kChatVision, 128000, 16384)
            .model("anthropic/claude-sonnet-4", kChatVisReason, 200000, 64000)
            .model("deepseek/deepseek-chat-v3-0324", kChatTools, 65536, 8192)
            .model("google/gemini-2.5-pro-preview", kChatVisReason, 1048576, 65536)
            .fallbackModel("openai/gpt-4o")
            .note("Every model id is namespaced as <vendor>/<model>; the catalogue grows daily.",
                  "所有模型标识都带有 <厂商>/<模型> 命名空间；目录每天都在增加。")
            .note("Sending HTTP-Referer and X-Title headers is recommended so the request is "
                  "attributed to this application in the OpenRouter dashboard.",
                  "建议发送 HTTP-Referer 与 X-Title 请求头，以便在 OpenRouter 后台把请求归属到"
                  "本应用。")
            .build());

    // --- Cerebras ----------------------------------------------------------
    add(VendorBuilder("cerebras", "Cerebras Inference", "Cerebras 推理云", "global",
                      ApiFlavor::OpenAiCompatible)
            .links("https://www.cerebras.ai", "https://inference-docs.cerebras.ai",
                   "https://cloud.cerebras.ai")
            .api("https://api.cerebras.ai/v1", "/chat/completions")
            .auth({AuthMode::ApiKey, AuthMode::Bearer}, "api-key")
            .keyHint("csk-", 32, {"Authorization"})
            .flags(true, true, false, true)
            .model("llama-3.3-70b", kChatTools, 128000, 8192)
            .model("llama3.1-8b", kChatTools, 128000, 8192)
            .model("qwen-3-32b", kChatReason, 131072, 16384)
            .model("llama-4-scout-17b-16e-instruct", kChatVision, 131072, 8192)
            .fallbackModel("llama-3.3-70b")
            .build());

    // --- SambaNova ---------------------------------------------------------
    add(VendorBuilder("sambanova", "SambaNova Cloud", "SambaNova 云", "global",
                      ApiFlavor::OpenAiCompatible)
            .links("https://sambanova.ai", "https://docs.sambanova.ai",
                   "https://cloud.sambanova.ai/apis")
            .api("https://api.sambanova.ai/v1", "/chat/completions")
            .auth({AuthMode::ApiKey, AuthMode::Bearer}, "api-key")
            .keyHint("", 32, {"Authorization"})
            .flags(true, true, false, true)
            .model("Meta-Llama-3.3-70B-Instruct", kChatTools, 131072, 8192)
            .model("Meta-Llama-3.1-405B-Instruct", kChatTools, 16384, 4096)
            .model("DeepSeek-R1", kChatReason, 32768, 8192)
            .model("Qwen2.5-72B-Instruct", kChatTools, 32768, 8192)
            .fallbackModel("Meta-Llama-3.3-70B-Instruct")
            .build());

    // --- NVIDIA NIM --------------------------------------------------------
    add(VendorBuilder("nvidia-nim", "NVIDIA NIM (build.nvidia.com)", "英伟达 NIM", "global",
                      ApiFlavor::OpenAiCompatible)
            .links("https://build.nvidia.com", "https://docs.nvidia.com/nim",
                   "https://build.nvidia.com/settings/api-keys")
            .api("https://integrate.api.nvidia.com/v1", "/chat/completions")
            .auth({AuthMode::ApiKey, AuthMode::Bearer}, "api-key")
            .keyHint("nvapi-", 40, {"Authorization"})
            .flags(true, true, false, true)
            .model("meta/llama-3.3-70b-instruct", kChatTools, 131072, 8192)
            .model("nvidia/llama-3.1-nemotron-70b-instruct", kChatTools, 131072, 8192)
            .model("deepseek-ai/deepseek-r1", kChatReason, 131072, 32768)
            .model("qwen/qwen2.5-coder-32b-instruct", kChatTools, 32768, 8192)
            .fallbackModel("meta/llama-3.3-70b-instruct")
            .note("The hosted catalogue is a trial service; self-hosted NIM containers expose "
                  "the same /v1 API on your own host.",
                  "托管目录属于试用服务；自建 NIM 容器会在你自己的主机上提供相同的 /v1 接口。")
            .build());

    // --- Databricks --------------------------------------------------------
    add(VendorBuilder("databricks", "Databricks (Foundation Model APIs)", "Databricks 基础模型",
                      "global", ApiFlavor::OpenAiCompatible)
            .links("https://www.databricks.com",
                   "https://docs.databricks.com/en/machine-learning/foundation-models/index.html",
                   "https://docs.databricks.com/en/dev-tools/auth/pat.html")
            .api("https://{workspace}.databricks.com", "/serving-endpoints/chat/completions")
            .auth({AuthMode::Bearer, AuthMode::ApiKey, AuthMode::OAuthPkce}, "bearer")
            .keyHint("dapi", 32, {"Authorization"})
            .flags(true, true, false, false)
            .model("databricks-meta-llama-3-3-70b-instruct", kChatTools, 128000, 8192)
            .model("databricks-claude-3-7-sonnet", kChatVision, 200000, 8192)
            .model("databricks-dbrx-instruct", kChatTools, 32768, 4096)
            .model("databricks-bge-large-en", kEmbed, 512, 0)
            .fallbackModel("databricks-meta-llama-3-3-70b-instruct")
            .oauth("https://{workspace}.databricks.com/oidc/v1/authorize",
                   "https://{workspace}.databricks.com/oidc/v1/token", "",
                   "all-apis offline_access", true)
            .note("Replace {workspace} with your workspace host; a personal access token is sent "
                  "as Authorization: Bearer.",
                  "请把 {workspace} 替换为你的工作区主机名；个人访问令牌以 Authorization: Bearer "
                  "发送。")
            .build());

    // --- IBM watsonx.ai ----------------------------------------------------
    add(VendorBuilder("ibm-watsonx", "IBM watsonx.ai", "IBM watsonx.ai", "global",
                      ApiFlavor::Custom)
            .links("https://www.ibm.com/products/watsonx-ai",
                   "https://cloud.ibm.com/apidocs/watsonx-ai",
                   "https://cloud.ibm.com/iam/apikeys")
            .api("https://{region}.ml.cloud.ibm.com/ml/v1", "/text/chat")
            .auth({AuthMode::ApiKey, AuthMode::Bearer}, "api-key")
            .keyHint("", 40, {"Authorization"})
            .flags(true, true, false, false)
            .model("ibm/granite-3-8b-instruct", kChatTools, 128000, 8192)
            .model("mistralai/mistral-large", kChatTools, 131072, 8192)
            .model("meta-llama/llama-3-3-70b-instruct", kChatTools, 128000, 8192)
            .model("ibm/granite-embedding-278m-multilingual", kEmbed, 512, 0)
            .fallbackModel("ibm/granite-3-8b-instruct")
            .note("Not OpenAI shaped: the body uses model_id and project_id, and every request "
                  "needs a ?version=YYYY-MM-DD query parameter.",
                  "并非 OpenAI 形态：请求体使用 model_id 与 project_id，且每个请求都需要 "
                  "?version=YYYY-MM-DD 查询参数。")
            .note("An IBM Cloud API key must first be exchanged for an IAM bearer token.",
                  "必须先把 IBM Cloud API Key 换成 IAM 承载令牌。")
            .build());

    // --- Voyage AI ---------------------------------------------------------
    add(VendorBuilder("voyage", "Voyage AI", "Voyage AI（向量）", "global",
                      ApiFlavor::OpenAiCompatible)
            .links("https://www.voyageai.com", "https://docs.voyageai.com",
                   "https://dash.voyageai.com/api-keys")
            .api("https://api.voyageai.com/v1", "/embeddings")
            .auth({AuthMode::ApiKey, AuthMode::Bearer}, "api-key")
            .keyHint("pa-", 32, {"Authorization"})
            .flags(false, false, false, false)
            .model("voyage-3-large", kEmbed, 32000, 0)
            .model("voyage-3", kEmbed, 32000, 0)
            .model("voyage-code-3", kEmbed, 32000, 0)
            .model("voyage-multimodal-3", CapEmbeddings | CapVision, 32000, 0)
            .fallbackModel("voyage-3")
            .note("Embeddings only: there is no chat completions endpoint on this platform.",
                  "只有向量接口：该平台没有对话补全端点。")
            .build());

    // --- Stability AI ------------------------------------------------------
    add(VendorBuilder("stability", "Stability AI", "Stability AI（图像）", "global",
                      ApiFlavor::Custom)
            .links("https://stability.ai", "https://platform.stability.ai/docs/api-reference",
                   "https://platform.stability.ai/account/keys")
            .api("https://api.stability.ai", "/v2beta/stable-image/generate/core")
            .auth({AuthMode::ApiKey, AuthMode::Bearer}, "api-key")
            .keyHint("sk-", 32, {"Authorization"})
            .flags(false, false, false, false)
            .model("stable-image-ultra", kImage, 10000, 0)
            .model("sd3.5-large", kImage, 10000, 0)
            .model("core", kImage, 10000, 0)
            .fallbackModel("stable-image-ultra")
            .note("Image generation uses multipart/form-data, not JSON, and requires an Accept "
                  "header of image/* or application/json.",
                  "图像生成使用 multipart/form-data 而非 JSON，并要求 Accept 头为 image/* 或 "
                  "application/json。")
            .build());

    // --- ElevenLabs --------------------------------------------------------
    add(VendorBuilder("elevenlabs", "ElevenLabs", "ElevenLabs（语音）", "global",
                      ApiFlavor::Custom)
            .links("https://elevenlabs.io", "https://elevenlabs.io/docs/api-reference",
                   "https://elevenlabs.io/app/settings/api-keys")
            .api("https://api.elevenlabs.io/v1", "/text-to-speech/{voice_id}")
            .auth({AuthMode::ApiKey, AuthMode::Bearer}, "api-key")
            .keyHint("sk_", 32, {"xi-api-key"})
            .flags(false, false, false, false)
            .model("eleven_multilingual_v2", kMedia, 5000, 0)
            .model("eleven_turbo_v2_5", kMedia, 40000, 0)
            .model("eleven_flash_v2_5", kMedia, 40000, 0)
            .model("eleven_v3", kMedia, 5000, 0)
            .fallbackModel("eleven_multilingual_v2")
            .note("The API key goes in the xi-api-key header; output is streaming audio, not a "
                  "chat message.",
                  "API Key 放在 xi-api-key 请求头中；返回的是流式音频而不是对话消息。")
            .build());

    // --- Deepgram ----------------------------------------------------------
    add(VendorBuilder("deepgram", "Deepgram", "Deepgram（语音识别）", "global",
                      ApiFlavor::Custom)
            .links("https://deepgram.com", "https://developers.deepgram.com",
                   "https://console.deepgram.com")
            .api("https://api.deepgram.com/v1", "/listen")
            .auth({AuthMode::ApiKey, AuthMode::Bearer}, "api-key")
            .keyHint("", 32, {"Authorization", "Token"})
            .flags(false, false, false, false)
            .model("nova-3", kMedia, 0, 0)
            .model("nova-2", kMedia, 0, 0)
            .model("enhanced", kMedia, 0, 0)
            .model("aura-2-thalia-en", kMedia, 0, 0)
            .fallbackModel("nova-3")
            .note("Speech to text posts raw audio to /v1/listen; text to speech posts JSON to "
                  "/v1/speak. Deepgram tokens may also be sent as 'Token <key>'.",
                  "语音转文字把原始音频 POST 到 /v1/listen；文字转语音把 JSON POST 到 "
                  "/v1/speak。Deepgram 令牌也可以用 'Token <key>' 形式发送。")
            .build());

    // --- AssemblyAI --------------------------------------------------------
    add(VendorBuilder("assemblyai", "AssemblyAI", "AssemblyAI（语音识别）", "global",
                      ApiFlavor::Custom)
            .links("https://www.assemblyai.com", "https://www.assemblyai.com/docs",
                   "https://www.assemblyai.com/dashboard/signup")
            .api("https://api.assemblyai.com/v2", "/transcript")
            .auth({AuthMode::ApiKey, AuthMode::Bearer}, "api-key")
            .keyHint("", 32, {"Authorization"})
            .flags(false, false, false, false)
            .model("best", kMedia, 0, 0)
            .model("nano", kMedia, 0, 0)
            .model("slam-1", kMedia, 0, 0)
            .fallbackModel("best")
            .note("Asynchronous: POST a transcript job, then poll the returned id until its "
                  "status is completed.",
                  "异步接口：先 POST 提交转写任务，再轮询返回的 id 直到状态为 completed。")
            .build());

    // --- GitHub Models -----------------------------------------------------
    add(VendorBuilder("github-models", "GitHub Models", "GitHub Models", "global",
                      ApiFlavor::OpenAiCompatible)
            .links("https://github.com/marketplace/models",
                   "https://docs.github.com/en/github-models",
                   "https://github.com/settings/tokens")
            .api("https://models.inference.ai.azure.com", "/chat/completions")
            .auth({AuthMode::ApiKey, AuthMode::Bearer, AuthMode::OAuthPkce,
                   AuthMode::OAuthDevice}, "api-key")
            .keyHint("ghp_", 40, {"Authorization"})
            .flags(true, true, true, false)
            .model("gpt-4o", kChatVision, 128000, 16384)
            .model("gpt-4o-mini", kChatVision, 128000, 16384)
            .model("Meta-Llama-3.1-405B-Instruct", kChatTools, 131072, 8192)
            .model("Mistral-large-2411", kChatTools, 131072, 8192)
            .fallbackModel("gpt-4o-mini")
            .oauth("https://github.com/login/oauth/authorize",
                   "https://github.com/login/oauth/access_token",
                   "https://github.com/login/device/code",
                   "read:user models:read", true)
            .note("A GitHub personal access token or a GitHub OAuth token with the models:read "
                  "scope is required; fine-grained tokens must be allowed for GitHub Models.",
                  "需要带 models:read 权限的 GitHub 个人访问令牌或 OAuth 令牌；细粒度令牌必须"
                  "显式授权 GitHub Models。")
            .note("Free usage is rate limited per account tier.",
                  "免费额度按账号等级限流。")
            .build());

    // --- Cloudflare Workers AI ---------------------------------------------
    add(VendorBuilder("cloudflare-workers-ai", "Cloudflare Workers AI",
                      "Cloudflare Workers AI", "global", ApiFlavor::OpenAiCompatible)
            .links("https://developers.cloudflare.com/workers-ai",
                   "https://developers.cloudflare.com/workers-ai/configuration/open-ai-compatibility",
                   "https://dash.cloudflare.com/profile/api-tokens")
            .api("https://api.cloudflare.com/client/v4/accounts/{account_id}/ai/v1",
                 "/chat/completions")
            .auth({AuthMode::ApiKey, AuthMode::Bearer}, "api-key")
            .keyHint("", 40, {"Authorization"})
            .flags(true, true, true, true)
            .model("@cf/meta/llama-3.3-70b-instruct-fp8-fast", kChatTools, 24000, 4096)
            .model("@cf/qwen/qwen2.5-coder-32b-instruct", kChatTools, 32768, 4096)
            .model("@cf/deepseek-ai/deepseek-r1-distill-qwen-32b", kChatReason, 80000, 4096)
            .model("@cf/meta/llama-3.2-11b-vision-instruct", kChatVision, 131072, 4096)
            .fallbackModel("@cf/meta/llama-3.3-70b-instruct-fp8-fast")
            .note("The account id is part of the path; model ids always start with @cf/.",
                  "账号 id 出现在路径中；模型标识一律以 @cf/ 开头。")
            .build());

    // --- DeepInfra ---------------------------------------------------------
    add(VendorBuilder("deepinfra", "DeepInfra", "DeepInfra（美国）", "global",
                      ApiFlavor::OpenAiCompatible)
            .links("https://deepinfra.com", "https://deepinfra.com/docs",
                   "https://deepinfra.com/dash/api_keys")
            .api("https://api.deepinfra.com/v1/openai", "/chat/completions")
            .auth({AuthMode::ApiKey, AuthMode::Bearer}, "api-key")
            .keyHint("", 32, {"Authorization"})
            .flags(true, true, true, true)
            .model("meta-llama/Llama-3.3-70B-Instruct", kChatTools, 131072, 8192)
            .model("deepseek-ai/DeepSeek-V3", kChatTools, 65536, 8192)
            .model("Qwen/Qwen2.5-72B-Instruct", kChatTools, 32768, 8192)
            .model("microsoft/WizardLM-2-8x22B", kChatTools, 65536, 8192)
            .fallbackModel("meta-llama/Llama-3.3-70B-Instruct")
            .build());

    // --- Novita AI ---------------------------------------------------------
    add(VendorBuilder("novita", "Novita AI", "Novita AI（聚合）", "global",
                      ApiFlavor::OpenAiCompatible)
            .links("https://novita.ai", "https://novita.ai/docs",
                   "https://novita.ai/settings/key-management")
            .api("https://api.novita.ai/v3/openai", "/chat/completions")
            .auth({AuthMode::ApiKey, AuthMode::Bearer}, "api-key")
            .keyHint("sk_", 32, {"Authorization"})
            .flags(true, true, true, true)
            .model("deepseek/deepseek-v3-0324", kChatTools, 65536, 8192)
            .model("meta-llama/llama-3.3-70b-instruct", kChatTools, 131072, 8192)
            .model("qwen/qwen-2.5-72b-instruct", kChatTools, 32768, 8192)
            .model("deepseek/deepseek-r1", kChatReason, 65536, 32768)
            .fallbackModel("deepseek/deepseek-v3-0324")
            .build());

    // --- Hyperbolic --------------------------------------------------------
    add(VendorBuilder("hyperbolic", "Hyperbolic", "Hyperbolic（美国）", "global",
                      ApiFlavor::OpenAiCompatible)
            .links("https://hyperbolic.xyz", "https://docs.hyperbolic.xyz",
                   "https://app.hyperbolic.xyz/settings")
            .api("https://api.hyperbolic.xyz/v1", "/chat/completions")
            .auth({AuthMode::ApiKey, AuthMode::Bearer}, "api-key")
            .keyHint("", 40, {"Authorization"})
            .flags(true, true, false, true)
            .model("meta-llama/Llama-3.3-70B-Instruct", kChatTools, 131072, 8192)
            .model("deepseek-ai/DeepSeek-V3", kChatTools, 65536, 8192)
            .model("Qwen/Qwen2.5-72B-Instruct", kChatTools, 32768, 8192)
            .fallbackModel("meta-llama/Llama-3.3-70B-Instruct")
            .build());

    // --- Upstage -----------------------------------------------------------
    add(VendorBuilder("upstage", "Upstage (Solar)", "Upstage（Solar）", "global",
                      ApiFlavor::OpenAiCompatible)
            .links("https://www.upstage.ai", "https://developers.upstage.ai",
                   "https://console.upstage.ai/api-keys")
            .api("https://api.upstage.ai/v1", "/chat/completions")
            .auth({AuthMode::ApiKey, AuthMode::Bearer}, "api-key")
            .keyHint("up_", 32, {"Authorization"})
            .flags(true, true, false, false)
            .model("solar-pro2", kChatTools, 65536, 8192)
            .model("solar-mini", kChatTools, 32768, 4096)
            .model("solar-pro", kChatTools, 32768, 4096)
            .model("embedding-query", kEmbed, 4000, 0)
            .fallbackModel("solar-pro2")
            .note("Solar models handle Korean and English especially well and accept a "
                  "reasoning_effort parameter.",
                  "Solar 模型对韩语与英语支持尤佳，并接受 reasoning_effort 参数。")
            .build());

    // =======================================================================
    //  CHINESE VENDORS
    //  中国厂商
    // =======================================================================

    // --- Alibaba Cloud Qwen (DashScope) ------------------------------------
    add(VendorBuilder("qwen", "Alibaba Qwen (DashScope)", "阿里云通义千问（百炼 / DashScope）",
                      "cn", ApiFlavor::OpenAiCompatible)
            .links("https://www.aliyun.com/product/bailian", "https://help.aliyun.com/zh/model-studio",
                   "https://bailian.console.aliyun.com/?apiKey=1")
            .api("https://dashscope.aliyuncs.com/compatible-mode/v1", "/chat/completions")
            .auth({AuthMode::ApiKey, AuthMode::Bearer}, "api-key")
            .keyHint("sk-", 32, {"Authorization"})
            .flags(true, true, true, true)
            .model("qwen-max", kChatTools, 32768, 8192)
            .model("qwen-plus", kChatTools, 131072, 8192)
            .model("qwen-turbo", kChatTools, 1000000, 8192)
            .model("qwen-vl-max", kChatVision, 32768, 2048)
            .model("text-embedding-v3", kEmbed, 8192, 0)
            .fallbackModel("qwen-plus")
            .note("The compatible-mode root is the OpenAI-style entry point; the native DashScope "
                  "API lives under /api/v1 with a different body.",
                  "compatible-mode 根地址是 OpenAI 风格的入口；原生 DashScope 接口位于 /api/v1，"
                  "请求体不同。")
            .note("Overseas accounts must use the international host "
                  "dashscope-intl.aliyuncs.com instead.",
                  "海外账号必须改用国际站主机 dashscope-intl.aliyuncs.com。")
            .build());

    // --- Zhipu AI (GLM) ----------------------------------------------------
    add(VendorBuilder("zhipu", "Zhipu AI (GLM)", "智谱 AI（GLM / 开放平台）", "cn",
                      ApiFlavor::OpenAiCompatible)
            .links("https://www.zhipuai.cn", "https://open.bigmodel.cn/dev/api",
                   "https://open.bigmodel.cn/usercenter/apikeys")
            .api("https://open.bigmodel.cn/api/paas/v4", "/chat/completions")
            .auth({AuthMode::ApiKey, AuthMode::Bearer}, "api-key")
            .keyHint("", 32, {"Authorization"})
            .flags(true, true, true, true)
            .model("glm-4-plus", kChatTools, 131072, 4096)
            .model("glm-4-air", kChatTools, 131072, 4096)
            .model("glm-4-flash", kChatTools, 131072, 4096)
            .model("glm-4v-plus", kChatVision, 8192, 1024)
            .model("glm-z1-air", kChatReason, 131072, 8192)
            .fallbackModel("glm-4-plus")
            .note("Keys are issued as '<id>.<secret>' and are used directly as a bearer token.",
                  "密钥格式为「<id>.<secret>」，直接作为承载令牌使用。")
            .note("glm-4v models take images as content parts on the user message.",
                  "glm-4v 系列模型把图片放在用户消息的 content 分段中。")
            .build());

    // --- Moonshot AI (Kimi) ------------------------------------------------
    add(VendorBuilder("moonshot", "Moonshot AI (Kimi)", "月之暗面 Kimi（Moonshot）", "cn",
                      ApiFlavor::OpenAiCompatible)
            .links("https://www.moonshot.cn", "https://platform.moonshot.cn/docs",
                   "https://platform.moonshot.cn/console/api-keys")
            .api("https://api.moonshot.cn/v1", "/chat/completions")
            .auth({AuthMode::ApiKey, AuthMode::Bearer}, "api-key")
            .keyHint("sk-", 32, {"Authorization"})
            .flags(true, true, false, true)
            .model("moonshot-v1-8k", kChatTools, 8192, 4096)
            .model("moonshot-v1-32k", kChatTools, 32768, 4096)
            .model("moonshot-v1-128k", kChatTools, 131072, 4096)
            .model("kimi-k2-0711-preview", kChatTools, 131072, 8192)
            .fallbackModel("moonshot-v1-32k")
            .note("The international site uses api.moonshot.ai as the host.",
                  "国际站使用 api.moonshot.ai 作为主机。")
            .build());

    // --- Baidu Qianfan (Ernie) ---------------------------------------------
    add(VendorBuilder("baidu-qianfan", "Baidu Qianfan (Ernie)", "百度智能云千帆（文心一言）", "cn",
                      ApiFlavor::OpenAiCompatible)
            .links("https://cloud.baidu.com/product/wenxinworkshop",
                   "https://cloud.baidu.com/doc/WENXINWORKSHOP/index.html",
                   "https://console.bce.baidu.com/iam/#/iam/apikey/list")
            .api("https://qianfan.baidubce.com/v2", "/chat/completions")
            .auth({AuthMode::ApiKey, AuthMode::Bearer}, "api-key")
            .keyHint("bce-v3/ALTAK", 40, {"Authorization"})
            .flags(true, true, false, false)
            .model("ernie-4.5-turbo-128k", kChatTools, 131072, 8192)
            .model("ernie-4.0-8k", kChatTools, 8192, 2048)
            .model("ernie-3.5-8k", kChatTools, 8192, 2048)
            .model("ernie-speed-128k", kChat, 131072, 4096)
            .model("bge-large-zh", kEmbed, 512, 0)
            .fallbackModel("ernie-4.5-turbo-128k")
            .note("The v2 API authenticates with a Bearer API key; the older v1 API needed an "
                  "access_token obtained from an API Key and Secret Key pair.",
                  "v2 接口使用 Bearer API Key 认证；较早的 v1 接口需要先用 API Key 与 Secret Key "
                  "换取 access_token。")
            .build());

    // --- Tencent Hunyuan ---------------------------------------------------
    add(VendorBuilder("tencent-hunyuan", "Tencent Hunyuan", "腾讯混元大模型", "cn",
                      ApiFlavor::OpenAiCompatible)
            .links("https://hunyuan.tencent.com", "https://cloud.tencent.com/document/product/1729",
                   "https://console.cloud.tencent.com/hunyuan/api-key")
            .api("https://api.hunyuan.cloud.tencent.com/v1", "/chat/completions")
            .auth({AuthMode::ApiKey, AuthMode::Bearer}, "api-key")
            .keyHint("sk-", 32, {"Authorization"})
            .flags(true, true, true, true)
            .model("hunyuan-turbos-latest", kChatTools, 32768, 16384)
            .model("hunyuan-pro", kChatTools, 32768, 8192)
            .model("hunyuan-t1-latest", kChatReason, 32768, 8192)
            .model("hunyuan-vision", kChatVision, 8192, 2048)
            .fallbackModel("hunyuan-turbos-latest")
            .note("The OpenAI-compatible endpoint is provided by the Tencent Cloud Hunyuan "
                  "service; the same key also drives the native /v1/chat/completions SDK.",
                  "该 OpenAI 兼容端点由腾讯云混元服务提供；同一密钥也可用于原生 SDK 的 "
                  "/v1/chat/completions。")
            .build());

    // --- ByteDance Doubao (Volcengine Ark) ---------------------------------
    add(VendorBuilder("bytedance-doubao", "ByteDance Doubao (Volcengine Ark)",
                      "字节豆包（火山方舟）", "cn", ApiFlavor::OpenAiCompatible)
            .links("https://www.volcengine.com/product/doubao",
                   "https://www.volcengine.com/docs/82379",
                   "https://console.volcengine.com/ark/region:ark+cn-beijing/apiKey")
            .api("https://ark.cn-beijing.volces.com/api/v3", "/chat/completions")
            .auth({AuthMode::ApiKey, AuthMode::Bearer}, "api-key")
            .keyHint("", 32, {"Authorization"})
            .flags(true, true, true, true)
            .model("doubao-seed-1-6-250615", kChatVisReason, 262144, 16384)
            .model("doubao-1-5-pro-32k-250115", kChatTools, 32768, 12288)
            .model("doubao-1-5-vision-pro-32k-250115", kChatVision, 32768, 12288)
            .model("doubao-lite-32k", kChatTools, 32768, 4096)
            .model("doubao-embedding-large", kEmbed, 4096, 0)
            .fallbackModel("doubao-1-5-pro-32k-250115")
            .note("Accounts that use inference endpoints must pass the endpoint id (ep-...) in the "
                  "model field instead of the model name.",
                  "使用推理接入点的账号必须在 model 字段传入接入点 id（ep-...），而不是模型名称。")
            .note("The default host is the cn-beijing region; other regions have their own host.",
                  "默认主机为 cn-beijing 区域；其它区域有各自的主机名。")
            .build());

    // --- MiniMax -----------------------------------------------------------
    add(VendorBuilder("minimax", "MiniMax", "MiniMax（海螺）", "cn", ApiFlavor::OpenAiCompatible)
            .links("https://www.minimaxi.com", "https://platform.minimaxi.com/document",
                   "https://platform.minimaxi.com/user-center/basic-information/interface-key")
            .api("https://api.minimax.chat/v1", "/text/chatcompletion_v2")
            .auth({AuthMode::ApiKey, AuthMode::Bearer}, "api-key")
            .keyHint("", 40, {"Authorization"})
            .flags(true, true, true, false)
            .model("abab6.5s-chat", kChatTools, 245000, 8192)
            .model("MiniMax-Text-01", kChatTools, 1000000, 40000)
            .model("MiniMax-VL-01", kChatVision, 1000000, 40000)
            .model("abab6.5g-chat", kChatTools, 8192, 4096)
            .fallbackModel("abab6.5s-chat")
            .note("The chat endpoint is text/chatcompletion_v2 and older accounts must also send "
                  "a GroupId query parameter; the OpenAI-compatible v2 surface needs the "
                  "international host api.minimaxi.com.",
                  "对话端点是 text/chatcompletion_v2，较早的账号还需附带 GroupId 查询参数；"
                  "若使用 OpenAI 兼容接口，需要国际站主机 api.minimaxi.com。")
            .build());

    // --- 01.AI (Yi) --------------------------------------------------------
    add(VendorBuilder("lingyiwanwu", "01.AI (Yi)", "零一万物（Yi）", "cn",
                      ApiFlavor::OpenAiCompatible)
            .links("https://www.lingyiwanwu.com", "https://platform.lingyiwanwu.com/docs",
                   "https://platform.lingyiwanwu.com/apikeys")
            .api("https://api.lingyiwanwu.com/v1", "/chat/completions")
            .auth({AuthMode::ApiKey, AuthMode::Bearer}, "api-key")
            .keyHint("", 32, {"Authorization"})
            .flags(true, true, true, false)
            .model("yi-lightning", kChatTools, 16384, 4096)
            .model("yi-large", kChatTools, 32768, 4096)
            .model("yi-medium", kChatTools, 16384, 4096)
            .model("yi-vision-v2", kChatVision, 16384, 4096)
            .fallbackModel("yi-lightning")
            .build());

    // --- Baichuan ----------------------------------------------------------
    add(VendorBuilder("baichuan", "Baichuan AI", "百川智能（Baichuan）", "cn",
                      ApiFlavor::OpenAiCompatible)
            .links("https://www.baichuan-ai.com", "https://platform.baichuan-ai.com/docs/api",
                   "https://platform.baichuan-ai.com/console/apikey")
            .api("https://api.baichuan-ai.com/v1", "/chat/completions")
            .auth({AuthMode::ApiKey, AuthMode::Bearer}, "api-key")
            .keyHint("sk-", 32, {"Authorization"})
            .flags(true, true, false, false)
            .model("Baichuan4-Turbo", kChatTools, 32768, 8192)
            .model("Baichuan4", kChatTools, 32768, 4096)
            .model("Baichuan4-Air", kChatTools, 32768, 4096)
            .model("Baichuan3-Turbo-128k", kChatTools, 131072, 4096)
            .fallbackModel("Baichuan4-Turbo")
            .note("Baichuan requires a unique request id header on some endpoints and does not "
                  "offer a vision model through this API.",
                  "百川的某些端点要求唯一的请求 id 头，且该接口不提供视觉模型。")
            .build());

    // --- StepFun -----------------------------------------------------------
    add(VendorBuilder("stepfun", "StepFun (Step)", "阶跃星辰（Step）", "cn",
                      ApiFlavor::OpenAiCompatible)
            .links("https://www.stepfun.com", "https://platform.stepfun.com/docs",
                   "https://platform.stepfun.com/interface-key")
            .api("https://api.stepfun.com/v1", "/chat/completions")
            .auth({AuthMode::ApiKey, AuthMode::Bearer}, "api-key")
            .keyHint("", 32, {"Authorization"})
            .flags(true, true, true, false)
            .model("step-2-16k", kChatTools, 16384, 4096)
            .model("step-2-mini", kChatTools, 32768, 4096)
            .model("step-1-32k", kChatTools, 32768, 4096)
            .model("step-1v-8k", kChatVision, 8192, 4096)
            .fallbackModel("step-2-16k")
            .build());

    // --- iFlytek Spark -----------------------------------------------------
    add(VendorBuilder("iflytek-spark", "iFlytek Spark", "科大讯飞星火", "cn",
                      ApiFlavor::OpenAiCompatible)
            .links("https://xinghuo.xfyun.cn",
                   "https://www.xfyun.cn/doc/spark/HTTP%E8%B0%83%E7%94%A8%E6%96%87%E6%A1%A3.html",
                   "https://console.xfyun.cn/services/cbm")
            .api("https://spark-api-open.xf-yun.com/v1", "/chat/completions")
            .auth({AuthMode::ApiKey, AuthMode::Bearer}, "api-key")
            .keyHint("", 32, {"Authorization"})
            .flags(true, true, false, false)
            .model("4.0Ultra", kChatTools, 8192, 4096)
            .model("generalv3.5", kChatTools, 8192, 4096)
            .model("max-32k", kChatTools, 32768, 4096)
            .model("lite", kChat, 8192, 4096)
            .fallbackModel("generalv3.5")
            .note("The HTTP service is enabled per model in the console; the bearer value is the "
                  "'APIKey:APISecret' pair shown there.",
                  "HTTP 服务需在控制台中按模型逐个开通；承载令牌的取值是控制台中显示的 "
                  "「APIKey:APISecret」组合。")
            .build());

    // --- SenseTime SenseNova -----------------------------------------------
    add(VendorBuilder("sensetime-sensenova", "SenseTime SenseNova", "商汤日日新（SenseNova）", "cn",
                      ApiFlavor::OpenAiCompatible)
            .links("https://www.sensetime.com", "https://platform.sensenova.cn",
                   "https://platform.sensenova.cn")
            .api("https://api.sensenova.cn/compatible-mode/v1", "/chat/completions")
            .auth({AuthMode::ApiKey, AuthMode::Bearer}, "api-key")
            .keyHint("", 32, {"Authorization"})
            .flags(true, true, true, false)
            .model("SenseChat-5", kChatTools, 32768, 4096)
            .model("SenseChat-5-Cantonese", kChatTools, 32768, 4096)
            .model("SenseChat-Turbo", kChatTools, 32768, 4096)
            .model("SenseNova-V6-Pro", kChatVision, 131072, 8192)
            .fallbackModel("SenseChat-5")
            .note("Older SenseNova calls must first exchange the API key for a JWT; the "
                  "compatible-mode root accepts the key directly as a bearer token.",
                  "较早的 SenseNova 调用需要先用 API Key 换取 JWT；compatible-mode 根地址可以直接"
                  "把密钥当作承载令牌使用。")
            .build());

    // --- Kunlun Tiangong ---------------------------------------------------
    add(VendorBuilder("kunlun-tiangong", "Kunlun Tiangong (SkyWork)", "昆仑万维天工（SkyWork）", "cn",
                      ApiFlavor::Custom)
            .links("https://www.tiangong.cn", "https://open.singularity-ai.com",
                   "https://open.singularity-ai.com")
            .api("https://sky-api.singularity-ai.com/saas/api/v4", "/generate")
            .auth({AuthMode::ApiKey, AuthMode::Bearer}, "api-key")
            .keyHint("", 32, {"Authorization", "X-App-Key"})
            .flags(true, true, false, false)
            .model("SkyChat", kChatTools, 8192, 4096)
            .model("SkyChat-Pro", kChatTools, 32768, 4096)
            .model("SkyWork", kChatTools, 32768, 4096)
            .fallbackModel("SkyChat")
            .note("Not OpenAI shaped: the body uses messages plus an app id, and the platform "
                  "issues an app id / app key pair rather than a single key.",
                  "并非 OpenAI 形态：请求体使用 messages 加应用 id，平台发放的是「应用 id / 应用"
                  "密钥」对，而不是单个密钥。")
            .build());

    // --- SiliconFlow -------------------------------------------------------
    add(VendorBuilder("siliconflow", "SiliconFlow", "硅基流动（SiliconFlow）", "cn",
                      ApiFlavor::OpenAiCompatible)
            .links("https://siliconflow.cn", "https://docs.siliconflow.cn",
                   "https://cloud.siliconflow.cn/account/ak")
            .api("https://api.siliconflow.cn/v1", "/chat/completions")
            .auth({AuthMode::ApiKey, AuthMode::Bearer}, "api-key")
            .keyHint("sk-", 32, {"Authorization"})
            .flags(true, true, true, true)
            .model("deepseek-ai/DeepSeek-V3", kChatTools, 65536, 8192)
            .model("deepseek-ai/DeepSeek-R1", kChatReason, 65536, 32768)
            .model("Qwen/Qwen2.5-72B-Instruct", kChatTools, 32768, 8192)
            .model("Qwen/Qwen2.5-VL-72B-Instruct", kChatVision, 32768, 8192)
            .model("BAAI/bge-m3", kEmbed, 8192, 0)
            .fallbackModel("deepseek-ai/DeepSeek-V3")
            .note("Model ids keep their Hugging Face style owner/name form.",
                  "模型标识沿用 Hugging Face 风格的「归属/名称」形式。")
            .build());

    // --- ModelScope --------------------------------------------------------
    add(VendorBuilder("modelscope", "ModelScope (魔搭 API-Inference)", "魔搭社区 ModelScope", "cn",
                      ApiFlavor::OpenAiCompatible)
            .links("https://modelscope.cn", "https://www.modelscope.cn/docs/model-service/API-Inference/intro",
                   "https://modelscope.cn/my/myaccesstoken")
            .api("https://api-inference.modelscope.cn/v1", "/chat/completions")
            .auth({AuthMode::ApiKey, AuthMode::Bearer}, "api-key")
            .keyHint("ms-", 32, {"Authorization"})
            .flags(true, true, true, true)
            .model("Qwen/Qwen3-235B-A22B", kChatReason, 131072, 16384)
            .model("deepseek-ai/DeepSeek-V3.1", kChatTools, 131072, 16384)
            .model("Qwen/Qwen2.5-72B-Instruct", kChatTools, 32768, 8192)
            .model("deepseek-ai/DeepSeek-R1", kChatReason, 65536, 32768)
            .fallbackModel("Qwen/Qwen2.5-72B-Instruct")
            .note("A free daily quota applies to each model; exceeding it returns HTTP 429 until "
                  "the next day.",
                  "每个模型都有每日免费额度；超出后会返回 HTTP 429，直到次日恢复。")
            .build());

    // --- Gitee AI ----------------------------------------------------------
    add(VendorBuilder("gitee-ai", "Gitee AI", "Gitee AI（码云）", "cn",
                      ApiFlavor::OpenAiCompatible)
            .links("https://ai.gitee.com", "https://ai.gitee.com/docs/openapi/v1",
                   "https://ai.gitee.com/dashboard/settings/tokens")
            .api("https://ai.gitee.com/v1", "/chat/completions")
            .auth({AuthMode::ApiKey, AuthMode::Bearer}, "api-key")
            .keyHint("", 32, {"Authorization"})
            .flags(true, true, true, false)
            .model("DeepSeek-V3", kChatTools, 65536, 8192)
            .model("Qwen2.5-72B-Instruct", kChatTools, 32768, 8192)
            .model("glm-4-9b-chat", kChatTools, 32768, 4096)
            .model("FLUX.1-schnell", kImage, 0, 0)
            .fallbackModel("DeepSeek-V3")
            .note("Confirm the current OpenAI-compatible root in the Gitee AI console; it has "
                  "moved between /v1 and /api/v1 during the beta.",
                  "请在 Gitee AI 控制台确认当前的 OpenAI 兼容根地址；测试期内它在 /v1 与 "
                  "/api/v1 之间变动过。")
            .build());

    // --- Huawei ModelArts Studio (MaaS) ------------------------------------
    add(VendorBuilder("huawei-modelarts", "Huawei ModelArts Studio (MaaS)", "华为云 ModelArts 盘古",
                      "cn", ApiFlavor::OpenAiCompatible)
            .links("https://www.huaweicloud.com/product/modelarts.html",
                   "https://support.huaweicloud.com/modelarts/index.html",
                   "https://console.huaweicloud.com/modelarts/?region=cn-north-4#/maas/apiKey")
            .api("https://api.modelarts-maas.com/v1", "/chat/completions")
            .auth({AuthMode::ApiKey, AuthMode::Bearer}, "api-key")
            .keyHint("", 32, {"Authorization"})
            .flags(true, true, true, true)
            .model("DeepSeek-V3", kChatTools, 65536, 8192)
            .model("DeepSeek-R1", kChatReason, 65536, 32768)
            .model("Qwen2.5-72B-32K", kChatTools, 32768, 8192)
            .model("bge-m3", kEmbed, 8192, 0)
            .fallbackModel("DeepSeek-V3")
            .note("Each model must be subscribed to in the ModelArts Studio console before its "
                  "API key works.",
                  "每个模型都需要先在 ModelArts Studio 控制台开通，API Key 才能生效。")
            .build());

    // --- Alibaba Bailian (application mode) --------------------------------
    add(VendorBuilder("aliyun-bailian", "Alibaba Bailian (application API)",
                      "阿里云百炼（应用调用）", "cn", ApiFlavor::Custom)
            .links("https://bailian.console.aliyun.com",
                   "https://help.aliyun.com/zh/model-studio/developer-reference/application-call",
                   "https://bailian.console.aliyun.com/?apiKey=1")
            .api("https://dashscope.aliyuncs.com/api/v1", "/apps/{app_id}/completion")
            .auth({AuthMode::ApiKey, AuthMode::Bearer}, "api-key")
            .keyHint("sk-", 32, {"Authorization"})
            .flags(true, true, true, true)
            .model("qwen-max", kChatTools, 32768, 8192)
            .model("qwen-plus", kChatTools, 131072, 8192)
            .model("qwen-vl-max", kChatVision, 32768, 2048)
            .fallbackModel("qwen-plus")
            .note("This entry targets a published Bailian application (its app id replaces "
                  "{app_id} and the body is {\"input\": {...}, \"parameters\": {...}}). For plain "
                  "model access use the qwen preset, which shares the same platform.",
                  "此条目面向百炼中已发布的应用（应用 id 替换 {app_id}，请求体为 "
                  "{\"input\": {...}, \"parameters\": {...}}）。若要直接调用模型，请使用 qwen "
                  "预设，二者属于同一平台。")
            .build());

    // --- Tencent Cloud TI / LKEAP ------------------------------------------
    add(VendorBuilder("tencent-cloud-ti", "Tencent Cloud TI (LKEAP)", "腾讯云 TI / 大模型知识引擎",
                      "cn", ApiFlavor::OpenAiCompatible)
            .links("https://cloud.tencent.com/product/ti",
                   "https://cloud.tencent.com/document/product/1772",
                   "https://console.cloud.tencent.com/lkeap/api")
            .api("https://api.lkeap.cloud.tencent.com/v1", "/chat/completions")
            .auth({AuthMode::ApiKey, AuthMode::Bearer}, "api-key")
            .keyHint("sk-", 32, {"Authorization"})
            .flags(true, true, true, true)
            .model("deepseek-v3", kChatTools, 65536, 8192)
            .model("deepseek-r1", kChatReason, 65536, 32768)
            .model("hunyuan-turbos-latest", kChatTools, 32768, 16384)
            .fallbackModel("deepseek-v3")
            .note("Keys are created in the LKEAP console; the same endpoint also fronts Tencent's "
                  "knowledge-engine applications.",
                  "密钥在大模型知识引擎控制台创建；同一端点也承载腾讯云的知识引擎应用。")
            .build());

    // --- Shengshu / Vidu ---------------------------------------------------
    add(VendorBuilder("shengshu", "Shengshu Vidu", "生数科技 Vidu（视频生成）", "cn",
                      ApiFlavor::Custom)
            .links("https://www.vidu.cn", "https://platform.vidu.cn/docs",
                   "https://platform.vidu.cn")
            .api("https://api.vidu.cn/ent/v2", "/text2video")
            .auth({AuthMode::ApiKey, AuthMode::Bearer}, "api-key")
            .keyHint("vda_", 32, {"Authorization"})
            .flags(false, false, true, false)
            .model("vidu2.0", kImage, 0, 0)
            .model("vidu1.5", kImage, 0, 0)
            .model("viduq1", kImage, 0, 0)
            .fallbackModel("vidu2.0")
            .note("Video generation is asynchronous: /text2video returns a task id that must be "
                  "polled; the key is sent as 'Authorization: Token <key>' and requests are rate "
                  "limited per account.",
                  "视频生成是异步的：/text2video 返回任务 id，需要轮询查询；密钥以 "
                  "'Authorization: Token <key>' 发送，且按账号限流。")
            .build());

    // --- Zhihu Zhida -------------------------------------------------------
    add(VendorBuilder("zhihu-ai", "Zhihu Zhida", "知乎直答", "cn", ApiFlavor::Custom)
            .links("https://www.zhihu.com", "https://open.zhihu.com", "https://open.zhihu.com")
            .api("https://api.zhihu.com/v1", "/chat/completions")
            .auth({AuthMode::ApiKey, AuthMode::Bearer}, "api-key")
            .keyHint("", 32, {"Authorization"})
            .flags(true, true, false, false)
            .model("zhihu-zhida", kChatTools, 131072, 8192)
            .model("zhihu-zhida-lite", kChat, 32768, 4096)
            .model("zhihu-search", kChat, 32768, 4096)
            .fallbackModel("zhihu-zhida")
            .note("Access is granted by invitation through Zhihu's open platform: confirm the "
                  "base URL, model ids and rate limits with the vendor before relying on them.",
                  "该服务通过知乎开放平台邀请开通：在正式依赖之前，请与厂商确认根地址、模型标识"
                  "与限流规则。")
            .build());

    // --- 360 AI ------------------------------------------------------------
    add(VendorBuilder("360-ai", "360 ZhiNao (360 AI)", "360 智脑", "cn",
                      ApiFlavor::OpenAiCompatible)
            .links("https://ai.360.cn", "https://ai.360.cn/platform/docs",
                   "https://ai.360.cn/platform/keys")
            .api("https://api.360.cn/v1", "/chat/completions")
            .auth({AuthMode::ApiKey, AuthMode::Bearer}, "api-key")
            .keyHint("", 32, {"Authorization"})
            .flags(true, true, false, true)
            .model("360gpt2-pro", kChatTools, 32768, 8192)
            .model("360gpt-turbo", kChatTools, 8192, 4096)
            .model("360gpt2-o1", kChatReason, 32768, 8192)
            .fallbackModel("360gpt2-pro")
            .note("Keys are issued per application in the 360 open platform console.",
                  "密钥在 360 开放平台控制台中按应用发放。")
            .build());

    // =======================================================================
    //  LOCAL / SELF-HOSTED SERVERS
    //  本地 / 自建服务
    // =======================================================================

    // --- Ollama ------------------------------------------------------------
    add(VendorBuilder("ollama", "Ollama (local)", "Ollama（本地）", "local", ApiFlavor::Ollama)
            .links("https://ollama.com", "https://github.com/ollama/ollama/tree/main/docs", "")
            .api("http://127.0.0.1:11434", "/api/chat")
            .auth({AuthMode::None, AuthMode::Bearer, AuthMode::ApiKey}, "none")
            .keyHint("", 8, {"Authorization"})
            .flags(true, true, true, true)
            .model("llama3.2", kChatTools, 131072, 4096)
            .model("qwen2.5", kChatTools, 32768, 4096)
            .model("deepseek-r1", kChatReason, 65536, 8192)
            .model("llava", kChatVision, 32768, 4096)
            .model("nomic-embed-text", kEmbed, 8192, 0)
            .fallbackModel("llama3.2")
            .note("Ollama must be running locally (ollama serve); the model must be pulled first "
                  "with ollama pull <model>.",
                  "本机必须正在运行 Ollama（ollama serve）；模型需要先用 "
                  "ollama pull <模型> 下载。")
            .note("The native API is /api/chat with newline-delimited JSON streaming; the "
                  "OpenAI-compatible surface is /v1/chat/completions on the same port.",
                  "原生接口是同端口上的 /api/chat，使用换行分隔的 JSON 流；同一端口也提供 "
                  "OpenAI 兼容的 /v1/chat/completions。")
            .note("A remote Ollama behind a reverse proxy can require a bearer token; set the "
                  "base URL override accordingly.",
                  "反向代理后面的远程 Ollama 可能需要承载令牌；请相应设置根地址覆盖。")
            .build());

    // --- LM Studio ---------------------------------------------------------
    add(VendorBuilder("lmstudio", "LM Studio (local server)", "LM Studio（本地服务）", "local",
                      ApiFlavor::OpenAiCompatible)
            .links("https://lmstudio.ai", "https://lmstudio.ai/docs/local-server", "")
            .api("http://127.0.0.1:1234/v1", "/chat/completions")
            .auth({AuthMode::None, AuthMode::ApiKey}, "none")
            .keyHint("", 8, {"Authorization"})
            .flags(true, true, true, false)
            .model("qwen2.5-7b-instruct", kChatTools, 32768, 4096)
            .model("llama-3.2-3b-instruct", kChatTools, 131072, 4096)
            .model("mistral-7b-instruct-v0.3", kChatTools, 32768, 4096)
            .fallbackModel("qwen2.5-7b-instruct")
            .note("Model ids are the keys of the models currently loaded in LM Studio; call "
                  "GET /v1/models to list them.",
                  "模型标识就是 LM Studio 中当前已加载模型的键名；可调用 GET /v1/models 查看。")
            .note("The local server must be started from the LM Studio 'Developer' tab.",
                  "本地服务需要在 LM Studio 的「Developer」页面手动启动。")
            .build());

    // --- vLLM --------------------------------------------------------------
    add(VendorBuilder("vllm", "vLLM (self-hosted)", "vLLM（自建）", "local",
                      ApiFlavor::OpenAiCompatible)
            .links("https://docs.vllm.ai", "https://docs.vllm.ai/en/latest/serving/openai_compatible_server.html",
                   "")
            .api("http://127.0.0.1:8000/v1", "/chat/completions")
            .auth({AuthMode::None, AuthMode::ApiKey, AuthMode::Bearer}, "none")
            .keyHint("", 8, {"Authorization"})
            .flags(true, true, true, false)
            .model("meta-llama/Llama-3.1-8B-Instruct", kChatTools, 131072, 8192)
            .model("Qwen/Qwen2.5-7B-Instruct", kChatTools, 32768, 8192)
            .model("mistralai/Mistral-7B-Instruct-v0.3", kChatTools, 32768, 8192)
            .fallbackModel("Qwen/Qwen2.5-7B-Instruct")
            .note("Start the server with 'vllm serve <model>'; adding --api-key switches the "
                  "endpoint to bearer authentication.",
                  "使用 'vllm serve <模型>' 启动服务；加上 --api-key 后端点改用承载令牌认证。")
            .note("The model id must match the name the server was started with.",
                  "模型标识必须与启动服务时使用的名称一致。")
            .build());

    // --- llama.cpp ---------------------------------------------------------
    add(VendorBuilder("llama-cpp", "llama.cpp server (local)", "llama.cpp 服务（本地）", "local",
                      ApiFlavor::OpenAiCompatible)
            .links("https://github.com/ggml-org/llama.cpp",
                   "https://github.com/ggml-org/llama.cpp/tree/master/examples/server", "")
            .api("http://127.0.0.1:8080/v1", "/chat/completions")
            .auth({AuthMode::None, AuthMode::ApiKey}, "none")
            .keyHint("", 8, {"Authorization"})
            .flags(true, true, true, false)
            .model("local-model", kChatTools, 32768, 4096)
            .model("gpt-3.5-turbo", kChatTools, 32768, 4096)
            .model("default", kChatTools, 32768, 4096)
            .fallbackModel("local-model")
            .note("llama-server ignores the model name and serves the GGUF file it was started "
                  "with, but the field must still be present.",
                  "llama-server 会忽略模型名，直接服务启动时加载的 GGUF 文件，但该字段仍然必填。")
            .note("Only one model is served per process; the context window follows the --ctx-size "
                  "argument.",
                  "每个进程只服务一个模型；上下文窗口取决于 --ctx-size 参数。")
            .build());

    // --- Jan ---------------------------------------------------------------
    add(VendorBuilder("jan", "Jan (local)", "Jan（本地）", "local", ApiFlavor::OpenAiCompatible)
            .links("https://jan.ai", "https://jan.ai/docs", "")
            .api("http://127.0.0.1:1337/v1", "/chat/completions")
            .auth({AuthMode::None, AuthMode::ApiKey}, "none")
            .keyHint("", 8, {"Authorization"})
            .flags(true, true, false, false)
            .model("llama-3.2-3b-instruct", kChatTools, 131072, 4096)
            .model("qwen2.5-7b-instruct", kChatTools, 32768, 4096)
            .model("mistral-7b-instruct", kChatTools, 32768, 4096)
            .fallbackModel("llama-3.2-3b-instruct")
            .note("Jan exposes the local API only after 'Local API Server' is enabled in "
                  "Settings > Advanced.",
                  "只有在「设置 > 高级」中启用本地 API 服务后，Jan 才会暴露本地接口。")
            .build());

    // --- text-generation-webui ---------------------------------------------
    add(VendorBuilder("text-generation-webui", "text-generation-webui (oobabooga)",
                      "text-generation-webui（本地）", "local", ApiFlavor::OpenAiCompatible)
            .links("https://github.com/oobabooga/text-generation-webui",
                   "https://github.com/oobabooga/text-generation-webui/wiki/12-%E2%80%90-OpenAI-API",
                   "")
            .api("http://127.0.0.1:5000/v1", "/chat/completions")
            .auth({AuthMode::None, AuthMode::ApiKey}, "none")
            .keyHint("", 8, {"Authorization"})
            .flags(true, true, false, false)
            .model("local-model", kChatTools, 32768, 4096)
            .model("gpt-3.5-turbo", kChatTools, 32768, 4096)
            .model("qwen2.5-7b-instruct", kChatTools, 32768, 4096)
            .fallbackModel("local-model")
            .note("Start the server with --api and, for remote access, --api-key and "
                  "--listen-host.",
                  "启动时需要加上 --api 参数；若要远程访问还需 --api-key 与 --listen-host。")
            .note("The OpenAI-compatible extension must be enabled in the interface.",
                  "需要在界面中启用 OpenAI 兼容扩展。")
            .build());
}

// ---------------------------------------------------------------------------
//  Lookup
//  查询
// ---------------------------------------------------------------------------
const VendorInfo *ProviderRegistry::find(const QString &id) const
{
    if (id.isEmpty())
        return nullptr;

    const QString key = id.toLower();
    const auto it = m_byId.constFind(key);
    if (it != m_byId.constEnd())
        return &m_vendors.at(it.value());

    //  Tolerate differencing case, spaces and URL-ish input ("OpenAI ").
    //  容忍大小写、空格以及类似网址的输入（"OpenAI "）。
    const QString needle = key.trimmed();
    for (const VendorInfo &vendor : m_vendors) {
        if (vendor.id.compare(needle, Qt::CaseInsensitive) == 0)
            return &vendor;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
//  Filtering
//  过滤
// ---------------------------------------------------------------------------
QVector<const VendorInfo *> ProviderRegistry::byRegion(const QString &region) const
{
    QVector<const VendorInfo *> out;
    const QString wanted = region.trimmed().toLower();
    for (const VendorInfo &vendor : m_vendors) {
        if (wanted.isEmpty() || vendor.region.compare(wanted, Qt::CaseInsensitive) == 0)
            out.append(&vendor);
    }
    return out;
}

QVector<const VendorInfo *> ProviderRegistry::byFlavor(ApiFlavor flavor) const
{
    QVector<const VendorInfo *> out;
    for (const VendorInfo &vendor : m_vendors) {
        if (vendor.flavor == flavor)
            out.append(&vendor);
    }
    return out;
}

QVector<const VendorInfo *> ProviderRegistry::supporting(AuthMode mode) const
{
    QVector<const VendorInfo *> out;
    for (const VendorInfo &vendor : m_vendors) {
        if (vendor.supports(mode))
            out.append(&vendor);
    }
    return out;
}

QVector<const VendorInfo *> ProviderRegistry::localServers() const
{
    QVector<const VendorInfo *> out;
    for (const VendorInfo &vendor : m_vendors) {
        if (vendor.isLocal || vendor.region.compare(QLatin1String("local"), Qt::CaseInsensitive) == 0)
            out.append(&vendor);
    }
    return out;
}

QVector<const VendorInfo *> ProviderRegistry::search(const QString &needle) const
{
    const QString query = needle.trimmed().toLower();
    if (query.isEmpty())
        return byRegion(QString());

    QVector<const VendorInfo *> out;
    for (const VendorInfo &vendor : m_vendors) {
        bool hit = vendor.id.contains(query) || vendor.labelEn.toLower().contains(query)
                   || vendor.labelZh.contains(query) || vendor.defaultModel.toLower().contains(query)
                   || vendor.region.contains(query);
        if (!hit) {
            for (const ModelInfo &model : vendor.models) {
                if (model.id.toLower().contains(query) || model.label.toLower().contains(query)) {
                    hit = true;
                    break;
                }
            }
        }
        if (!hit) {
            for (const QString &note : vendor.notesEn) {
                if (note.toLower().contains(query)) {
                    hit = true;
                    break;
                }
            }
        }
        if (hit)
            out.append(&vendor);
    }
    return out;
}

// ---------------------------------------------------------------------------
//  Models
//  模型
// ---------------------------------------------------------------------------
QVector<ProviderRegistry::ModelRef> ProviderRegistry::allModels() const
{
    QVector<ModelRef> out;
    int total = 0;
    for (const VendorInfo &vendor : m_vendors)
        total += vendor.models.size();
    out.reserve(total);

    for (const VendorInfo &vendor : m_vendors) {
        for (const ModelInfo &model : vendor.models) {
            ModelRef ref;
            ref.vendorId = vendor.id;
            ref.vendorLabel = vendor.labelEn;
            ref.model = model;
            out.append(ref);
        }
    }

    //  Stable, deterministic order for combo boxes and documentation.
    //  为下拉框与文档提供稳定、确定的顺序。
    std::sort(out.begin(), out.end());
    return out;
}

QVector<ProviderRegistry::ModelRef>
ProviderRegistry::modelsWithCapability(ModelCapability capability) const
{
    QVector<ModelRef> out;
    for (const VendorInfo &vendor : m_vendors) {
        for (const ModelInfo &model : vendor.models) {
            if (!model.caps.testFlag(capability))
                continue;
            ModelRef ref;
            ref.vendorId = vendor.id;
            ref.vendorLabel = vendor.labelEn;
            ref.model = model;
            out.append(ref);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

const ModelInfo *ProviderRegistry::findModel(const QString &modelId) const
{
    if (modelId.isEmpty())
        return nullptr;

    for (const VendorInfo &vendor : m_vendors) {
        if (const ModelInfo *model = vendor.model(modelId))
            return model;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
//  Statistics
//  统计
// ---------------------------------------------------------------------------
int ProviderRegistry::modelCount() const
{
    int total = 0;
    for (const VendorInfo &vendor : m_vendors)
        total += vendor.models.size();
    return total;
}

QStringList ProviderRegistry::regions() const
{
    //  Distinct regions in registration order (global, cn, local), which keeps
    //  the filter chips in the UI stable between runs.
    //  按注册顺序返回去重后的地区（global、cn、local），使界面上的筛选标签在多次运行
    //  之间保持稳定。
    QStringList out;
    for (const VendorInfo &vendor : m_vendors) {
        if (!out.contains(vendor.region))
            out.append(vendor.region);
    }
    return out;
}

int ProviderRegistry::regionCount() const
{
    return static_cast<int>(regions().size());
}

// ---------------------------------------------------------------------------
//  Custom / self-hosted vendors
//  自定义 / 自建厂商
// ---------------------------------------------------------------------------
VendorInfo ProviderRegistry::makeCustomVendor(const QString &id, const QString &label,
                                              const QString &baseUrl, ApiFlavor flavor,
                                              const QString &defaultModel)
{
    VendorInfo vendor;
    vendor.id = id.trimmed();
    vendor.labelEn = label.trimmed().isEmpty() ? vendor.id : label.trimmed();
    vendor.labelZh = vendor.labelEn;

    QString root = baseUrl.trimmed();
    while (root.endsWith(QLatin1Char('/')))
        root.chop(1);
    vendor.baseUrl = root;

    vendor.flavor = flavor;

    //  Loopback and private-range hosts never leave the machine, so they are
    //  treated as local and default to no authentication at all.
    //  回环与内网地址不会离开本机，因此视为本地部署，默认完全不需要认证。
    static const QStringList localHints = {
        QStringLiteral("127.0.0.1"), QStringLiteral("localhost"), QStringLiteral("::1"),
        QStringLiteral("0.0.0.0"), QStringLiteral("10."), QStringLiteral("192.168."),
        QStringLiteral("172.16."), QStringLiteral("172.17."), QStringLiteral("172.18."),
        QStringLiteral("172.19."), QStringLiteral("172.2"), QStringLiteral("172.30."),
        QStringLiteral("172.31.")
    };
    bool local = root.isEmpty();
    for (const QString &hint : localHints) {
        if (root.contains(hint, Qt::CaseInsensitive)) {
            local = true;
            break;
        }
    }

    vendor.region = local ? QStringLiteral("local") : QStringLiteral("global");
    vendor.isLocal = local;

    switch (flavor) {
    case ApiFlavor::Anthropic:
        vendor.chatPath = QStringLiteral("/v1/messages");
        break;
    case ApiFlavor::GoogleGemini:
        vendor.chatPath = QStringLiteral("/v1beta/models/{model}:generateContent");
        break;
    case ApiFlavor::Ollama:
        vendor.chatPath = QStringLiteral("/api/chat");
        break;
    case ApiFlavor::AwsBedrock:
        vendor.chatPath = QStringLiteral("/model/{model}/invoke");
        break;
    default:
        vendor.chatPath = QStringLiteral("/chat/completions");
        break;
    }

    if (local) {
        vendor.authModes = {AuthMode::None, AuthMode::ApiKey, AuthMode::Bearer};
        vendor.preferredAuth = QStringLiteral("none");
    } else {
        vendor.authModes = {AuthMode::ApiKey, AuthMode::Bearer};
        vendor.preferredAuth = QStringLiteral("api-key");
    }

    vendor.keyHeaderNames = (flavor == ApiFlavor::Anthropic)
                                ? QStringList{QStringLiteral("x-api-key")}
                                : QStringList{QStringLiteral("Authorization")};
    vendor.keyMinLength = 8;
    vendor.oauthRequiresUserClientId = true;

    ModelInfo model;
    model.id = defaultModel.trimmed();
    vendor.defaultModel = model.id;
    if (!model.id.isEmpty()) {
        model.label = model.id;
        model.caps = kChatTools;
        vendor.models.append(model);
    }

    vendor.notesEn.append(QStringLiteral(
        "User supplied endpoint: verify the base URL, the chat path and the model id against "
        "your own server before sending a request."));
    vendor.notesZh.append(QStringLiteral(
        "用户自定义端点：发送请求前请先核对自己服务器的根地址、对话路径与模型标识。"));
    if (local) {
        vendor.notesEn.append(QStringLiteral(
            "The host is loopback or private, so requests never leave your network."));
        vendor.notesZh.append(QStringLiteral(
            "该地址属于回环或内网，请求不会离开你的网络。"));
    }

    return vendor;
}

} // namespace tcbw
