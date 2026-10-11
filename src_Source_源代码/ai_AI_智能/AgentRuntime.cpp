// ============================================================================
//  AgentRuntime.cpp -- the tool-calling conversation loop
//  AgentRuntime.cpp —— 工具调用对话循环
//
//  LOOP / 循环
//    user turn -> model -> (tool call -> result)* -> final answer
//    ^--------------------------------------------------|
//    bounded by `m_maxIterations` so a confused model cannot burn the user's
//    quota or lock the UI in an endless tool cycle.
//    用户轮次 -> 模型 -> （工具调用 -> 结果）* -> 最终回答，由 `m_maxIterations`
//    限定，避免模型困惑时耗尽用户额度或让界面陷入无尽的工具循环。
// ============================================================================

#include "AgentRuntime.h"

#include "AiProvider.h"
#include "AiTypes.h"
#include "AgentTools.h"
#include "BlockDef.h"
#include "IdGenerator.h"
#include "Page.h"
#include "ProjectDocument.h"
#include "Types.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

#include <algorithm>

namespace tcbw {
namespace {

//  Rough tokens-per-character ratios for the context meter. These are
//  deliberately cheap heuristics: the exact tokenizer is model specific and
//  running it for a UI meter would cost more than it is worth.
//  上下文计量用的粗略"字符转令牌"比率。这是刻意采用的廉价启发式：精确分词器
//  依模型而异，为界面计量而运行它得不偿失。
constexpr double kTokensPerLatinChar = 1.0 / 4.0;
constexpr double kTokensPerCjkChar = 1.0 / 1.5;

//  Escapes a heading fragment.
//  转义标题片段。
QString escapeForContext(const QString &text)
{
    QString out = text;
    out.replace(QLatin1Char('\n'), QLatin1Char(' '));
    out.replace(QLatin1Char('\r'), QLatin1Char(' '));
    return out.simplified();
}

//  Orders pages so the active one is described first.
//  对页面排序，使活动页面排在前面。
QVector<const Page *> pagesActiveFirst(const ProjectDocument *project)
{
    QVector<const Page *> pages;
    if (!project)
        return pages;

    QString activeId = project->activePageId;
    if (activeId.isEmpty() && project->pageCount() > 0 && project->pageAt(0))
        activeId = project->pageAt(0)->id;

    for (int i = 0; i < project->pageCount(); ++i) {
        Page *page = project->pageAt(i);
        if (page && page->id == activeId)
            pages.prepend(page);
        else if (page)
            pages.append(page);
    }
    return pages;
}

} // namespace

// ---------------------------------------------------------------------------
//  Construction
//  构造
// ---------------------------------------------------------------------------
AgentRuntime::AgentRuntime(AiClient *client, AgentToolRegistry *registry, QObject *parent)
    : QObject(parent)
    , m_client(client)
    , m_registry(registry)
{
    if (m_client) {
        connect(m_client, &AiClient::streamStarted, this, &AgentRuntime::onStreamStarted);
        connect(m_client, &AiClient::textDelta, this, &AgentRuntime::onTextDelta);
        connect(m_client, &AiClient::reasoningDelta, this, &AgentRuntime::onReasoningDelta);
        connect(m_client, &AiClient::toolCallReady, this, &AgentRuntime::onToolCallReady);
        connect(m_client, &AiClient::usageReported, this, &AgentRuntime::onUsage);
        connect(m_client, &AiClient::completed, this, &AgentRuntime::onCompleted);
        connect(m_client, &AiClient::failed, this, &AgentRuntime::onFailed);
    }

    if (m_registry) {
        //  Approval decisions are reported by the registry itself: `toolDenied`
        //  reaches the UI through `AgentToolRegistry`, and every executed tool
        //  is surfaced here as `toolCallStarted`/`toolCallFinished`.
        //  审批决定由注册表自身报告：`toolDenied` 通过 `AgentToolRegistry` 送达
        //  界面，而每个已执行的工具都会以 `toolCallStarted`/`toolCallFinished`
        //  在此处呈现。
    }

    m_systemPrompt = defaultSystemPrompt(false);
}

AgentRuntime::~AgentRuntime() = default;

// ---------------------------------------------------------------------------
//  Configuration
//  配置
// ---------------------------------------------------------------------------
void AgentRuntime::setSystemPrompt(const QString &prompt)
{
    m_systemPrompt = prompt;
}

void AgentRuntime::setMaxToolIterations(int iterations)
{
    //  Clamp to a sane window: zero would disable tools entirely, and an
    //  unbounded value defeats the purpose of the cap.
    //  限制在合理范围内：0 会完全禁用工具，无上限则失去该限制的意义。
    m_maxIterations = std::clamp(iterations, 1, 200);
}

void AgentRuntime::setMaxTranscriptMessages(int messages)
{
    m_maxTranscriptMessages = std::max(2, messages);
}

// ---------------------------------------------------------------------------
//  sendUserMessage
//  发送用户消息
// ---------------------------------------------------------------------------
void AgentRuntime::sendUserMessage(const QString &text)
{
    if (m_busy) {
        //  One turn at a time: overlapping turns would interleave tool results.
        //  一次只处理一个轮次：重叠的轮次会导致工具结果交错。
        return;
    }
    if (text.trimmed().isEmpty())
        return;

    appendMessage(ChatMessage::user(text));
    m_iteration = 0;
    m_cancelled = false;
    m_streamingText.clear();
    m_pendingToolCalls.clear();

    emit started();
    dispatchToModel();
}

// ---------------------------------------------------------------------------
//  cancel
//  取消当前轮次
// ---------------------------------------------------------------------------
void AgentRuntime::cancel()
{
    m_cancelled = true;

    if (m_client && !m_requestId.isEmpty())
        m_client->cancel(m_requestId);

    //  The transcript is deliberately left intact so the user can retry.
    //  刻意保留会话记录，以便用户重试。
    finishTurn(false, QStringLiteral("Cancelled") + QStringLiteral("\n")
                          + QStringLiteral("已取消"));
}

// ---------------------------------------------------------------------------
//  reset
//  重置会话
// ---------------------------------------------------------------------------
void AgentRuntime::reset()
{
    if (m_busy) {
        if (m_client && !m_requestId.isEmpty())
            m_client->cancel(m_requestId);
    }

    m_transcript.clear();
    m_pendingToolCalls.clear();
    m_streamingText.clear();
    m_requestId.clear();
    m_iteration = 0;
    m_inputTokens = 0;
    m_outputTokens = 0;
    m_busy = false;
    m_cancelled = false;

    emit usageUpdated(0, 0);
    emit turnFinished(true, QString());
}

// ---------------------------------------------------------------------------
//  retryLast
//  重试最后一条用户消息
// ---------------------------------------------------------------------------
void AgentRuntime::retryLast()
{
    if (m_busy)
        return;

    //  Drop the trailing assistant/tool exchange, keeping the user message.
    //  丢弃末尾的助手/工具交互，保留用户消息。
    while (!m_transcript.isEmpty()
           && m_transcript.last().role != ChatRole::User) {
        m_transcript.removeLast();
    }
    //  Nothing to retry when the transcript has no user turn at all.
    //  当会话记录中完全没有用户轮次时，无需重试。
    if (m_transcript.isEmpty())
        return;

    m_iteration = 0;
    m_cancelled = false;
    m_streamingText.clear();
    m_pendingToolCalls.clear();

    emit started();
    dispatchToModel();
}

// ---------------------------------------------------------------------------
//  dispatchToModel
//  向模型发起一次请求
// ---------------------------------------------------------------------------
void AgentRuntime::dispatchToModel()
{
    if (!m_client) {
        finishTurn(false, QStringLiteral("No AI client is attached to this conversation.")
                              + QStringLiteral("\n")
                              + QStringLiteral("此对话未挂接 AI 客户端。"));
        return;
    }

    //  The system message is rebuilt on every turn so the outline stays fresh.
    //  每轮都重建系统消息，使工程纲要保持最新。
    QString system = m_systemPrompt;
    if (m_includeOutline) {
        const QString outline = buildProjectContext();
        if (!outline.isEmpty()) {
            system += QStringLiteral("\n\n");
            system += outline;
        }
    }

    QVector<ChatMessage> request;
    request.append(ChatMessage::system(system));
    const QVector<ChatMessage> windowed = windowedTranscript();
    for (const ChatMessage &message : windowed)
        request.append(message);

    QVector<ToolDescriptor> tools;
    if (m_registry)
        tools = m_registry->descriptorsForModel();

    m_requestId = IdGenerator::randomToken(8);
    m_busy = true;

    const QString newId = m_client->send(request, tools, m_requestId);
    if (newId.isEmpty()) {
        //  `send` emits `failed` for an invalid configuration; if it did not,
        //  make sure the turn still terminates.
        //  `send` 对无效配置会发出 `failed`；若未发出，也要确保轮次终止。
        if (m_busy) {
            finishTurn(false, QStringLiteral("The request could not be started. Check the "
                                             "provider settings.")
                                  + QStringLiteral("\n")
                                  + QStringLiteral("无法发起请求。请检查厂商设置。"));
        }
        return;
    }
    m_requestId = newId;
}

// ---------------------------------------------------------------------------
//  runPendingTools
//  依次执行待处理的工具调用
// ---------------------------------------------------------------------------
void AgentRuntime::runPendingTools()
{
    if (m_pendingToolCalls.isEmpty()) {
        finishTurn(true, QString());
        return;
    }

    //  Iteration budget: the counter counts model round trips, so the check
    //  happens before the tools that would feed the next one.
    //  迭代预算：计数器统计模型往返次数，因此在会触发下一次往返的工具之前检查。
    if (m_iteration >= m_maxIterations) {
        m_pendingToolCalls.clear();
        finishTurn(false,
                   QStringLiteral("Stopped after %1 tool round trips without a final answer. "
                                  "Ask for a narrower change.")
                       .arg(m_maxIterations)
                       + QStringLiteral("\n")
                       + QStringLiteral("已完成 %1 轮工具调用仍未得到最终回答，已停止。"
                                        "请提出更具体的修改要求。")
                             .arg(m_maxIterations));
        return;
    }

    const QVector<ToolCall> calls = m_pendingToolCalls;
    m_pendingToolCalls.clear();

    for (const ToolCall &call : calls) {
        if (m_cancelled) {
            finishTurn(false, QStringLiteral("Cancelled") + QStringLiteral("\n")
                                  + QStringLiteral("已取消"));
            return;
        }

        emit toolCallStarted(call);

        ToolResult result;
        if (m_registry) {
            result = m_registry->invoke(call, m_context);
        } else {
            result.isError = true;
            result.toolCallId = call.id;
            result.content = QStringLiteral("No tool registry is attached.")
                             + QStringLiteral("\n")
                             + QStringLiteral("未挂接工具注册表。");
        }
        if (result.toolCallId.isEmpty())
            result.toolCallId = call.id;

        emit toolCallFinished(call, result);

        ChatMessage toolMessage = ChatMessage::tool(call.id, call.name, result.content,
                                                    result.isError);
        appendMessage(toolMessage);
    }

    ++m_iteration;
    dispatchToModel();
}

// ---------------------------------------------------------------------------
//  finishTurn
//  结束当前轮次
// ---------------------------------------------------------------------------
void AgentRuntime::finishTurn(bool ok, const QString &errorText)
{
    if (m_client && !m_requestId.isEmpty())
        m_client->cancel(m_requestId);

    m_busy = false;
    m_requestId.clear();
    m_pendingToolCalls.clear();
    m_streamingText.clear();

    emit turnFinished(ok, errorText);
}

// ---------------------------------------------------------------------------
//  appendMessage
//  追加消息并通知界面
// ---------------------------------------------------------------------------
void AgentRuntime::appendMessage(const ChatMessage &message)
{
    ChatMessage stored = message;
    if (!stored.timestamp.isValid())
        stored.timestamp = QDateTime::currentDateTimeUtc();
    m_transcript.append(stored);
    emit messageAdded(m_transcript.last());
}

// ---------------------------------------------------------------------------
//  windowedTranscript
//  裁剪会话记录，且绝不切断助手消息与它的工具结果
// ---------------------------------------------------------------------------
QVector<ChatMessage> AgentRuntime::windowedTranscript() const
{
    QVector<ChatMessage> out;
    if (m_transcript.isEmpty())
        return out;

    const int total = m_transcript.size();

    //  An assistant message and the tool results it requested form one atomic
    //  unit: providers reject a tool result without its parent call.
    //  助手消息与它请求的工具结果构成一个原子单元：厂商会拒绝没有父调用的
    //  工具结果。
    const auto isTool = [this](int index) {
        return index >= 0 && index < m_transcript.size()
               && m_transcript.at(index).role == ChatRole::Tool;
    };

    int start = std::max(0, total - std::max(1, m_maxTranscriptMessages));

    //  Extend backwards while the window would begin with orphaned tool results.
    //  当窗口会以孤立的工具结果开头时，向前扩展。
    while (start > 0 && isTool(start))
        --start;

    for (int i = start; i < total; ++i) {
        const ChatMessage &message = m_transcript.at(i);
        //  System messages are rebuilt per turn by `dispatchToModel`.
        //  系统消息由 `dispatchToModel` 每轮重建。
        if (message.role == ChatRole::System)
            continue;
        if (message.role == ChatRole::Assistant && message.content.isEmpty()
            && message.toolCalls.isEmpty()) {
            continue;
        }
        out.append(message);
    }
    return out;
}

// ---------------------------------------------------------------------------
//  estimatedContextTokens
//  估算上下文令牌数
// ---------------------------------------------------------------------------
int AgentRuntime::estimatedContextTokens() const
{
    //  CJK characters carry close to one token each while Latin text averages
    //  about four characters per token; counting them separately keeps the
    //  meter honest for both scripts.
    //  中文字符大致一字一令牌，而拉丁文本平均约四字符一令牌；分别统计可使
    //  两种文字下的计量都较为诚实。
    const auto tokensFor = [](const QString &text) {
        double tokens = 0.0;
        for (const QChar ch : text) {
            const ushort code = ch.unicode();
            const bool cjk = (code >= 0x2E80 && code <= 0x9FFF)
                             || (code >= 0xAC00 && code <= 0xD7AF)
                             || (code >= 0xF900 && code <= 0xFAFF)
                             || (code >= 0xFF00 && code <= 0xFFEF);
            tokens += cjk ? kTokensPerCjkChar : kTokensPerLatinChar;
        }
        return tokens;
    };

    double total = tokensFor(m_systemPrompt);
    if (m_includeOutline)
        total += tokensFor(buildProjectContext());

    for (const ChatMessage &message : m_transcript) {
        total += tokensFor(message.content);
        total += tokensFor(message.reasoning);
        total += tokensFor(message.name);
        for (const ToolCall &call : message.toolCalls) {
            total += tokensFor(call.name);
            total += tokensFor(QString::fromUtf8(
                QJsonDocument(call.arguments).toJson(QJsonDocument::Compact)));
        }
    }

    //  Tool schemas are part of every request and are surprisingly large.
    //  工具 schema 属于每次请求的一部分，且出人意料地占空间。
    if (m_registry) {
        for (const ToolDescriptor &descriptor : m_registry->descriptorsForModel()) {
            total += tokensFor(descriptor.name);
            total += tokensFor(descriptor.descriptionEn);
            total += tokensFor(QString::fromUtf8(
                QJsonDocument(descriptor.inputSchema).toJson(QJsonDocument::Compact)));
        }
    }

    return static_cast<int>(total + 0.5);
}

// ---------------------------------------------------------------------------
//  buildProjectContext
//  构建注入系统提示词的工程摘要
// ---------------------------------------------------------------------------
QString AgentRuntime::buildProjectContext() const
{
    const ProjectDocument *project = m_context.project;
    if (!project)
        return QString();

    QStringList lines;
    lines.append(QStringLiteral("### Project context (generated by the application)"));
    lines.append(QStringLiteral("### 工程上下文（由应用程序生成）"));
    lines.append(QStringLiteral("name: %1").arg(escapeForContext(project->name)));
    if (!project->description.isEmpty())
        lines.append(QStringLiteral("description: %1")
                         .arg(escapeForContext(project->description)));
    lines.append(QStringLiteral("pages: %1").arg(project->pageCount()));

    // --- page list ---------------------------------------------------------
    // --- 页面列表 -----------------------------------------------------------
    const QVector<const Page *> ordered = pagesActiveFirst(project);
    for (const Page *page : ordered) {
        if (!page)
            continue;
        lines.append(QStringLiteral("- page id=%1 name=%2 title=\"%3\"%4")
                         .arg(page->id, escapeForContext(page->name),
                              escapeForContext(page->title),
                              page->isHome ? QStringLiteral(" (home)") : QString()));
    }

    // --- active page outline ----------------------------------------------
    // --- 活动页面纲要 ------------------------------------------------------
    const Page *active = nullptr;
    QString activeId = m_context.activePageId.trimmed();
    if (activeId.isEmpty())
        activeId = project->activePageId;
    active = project->pageById(activeId);
    if (!active && !ordered.isEmpty())
        active = ordered.first();

    if (active && active->root) {
        lines.append(QStringLiteral("### Element outline of page \"%1\"")
                         .arg(escapeForContext(active->name)));
        lines.append(QStringLiteral("### 页面 \"%1\" 的元素纲要")
                         .arg(escapeForContext(active->name)));

        //  Capped at ~80 nodes so a huge page cannot flood the prompt.
        //  上限约 80 个节点，避免超大页面淹没提示词。
        constexpr int kMaxContextNodes = 80;
        constexpr int kMaxContextDepth = 6;
        int emitted = 0;
        QStringList outline;

        //  Iterative traversal: a page tree can be arbitrarily deep and an
        //  accidental cycle must not become a stack overflow.
        //  迭代式遍历：页面树可能极深，且意外的环绝不能变成栈溢出。
        struct Frame
        {
            const DocumentNode *node = nullptr;
            int depth = 0;
        };
        QVector<Frame> stack;
        stack.append(Frame{ active->root.get(), 0 });
        while (!stack.isEmpty() && emitted < kMaxContextNodes) {
            const Frame frame = stack.takeLast();
            if (!frame.node)
                continue;
            ++emitted;
            const int indentDepth = std::min(frame.depth, kMaxContextDepth);
            outline.append(QStringLiteral("%1%2 %3%4")
                               .arg(QString(indentDepth, QLatin1Char(' ')),
                                    frame.node->id, frame.node->tag,
                                    frame.node->text.isEmpty()
                                        ? QString()
                                        : QStringLiteral(" \"%1\"")
                                              .arg(escapeForContext(frame.node->text.left(40)))));
            if (frame.depth >= kMaxContextDepth)
                continue;
            const std::vector<std::unique_ptr<DocumentNode>> &children = frame.node->children();
            for (auto it = children.rbegin(); it != children.rend(); ++it) {
                if (stack.size() + emitted >= kMaxContextNodes + 32)
                    break;
                stack.append(Frame{ it->get(), frame.depth + 1 });
            }
        }
        lines.append(outline.join(QStringLiteral("\n")));
        if (emitted >= kMaxContextNodes)
            lines.append(QStringLiteral("... outline truncated at %1 nodes")
                             .arg(kMaxContextNodes));
    }

    // --- block scripts -----------------------------------------------------
    // --- 积木脚本 ----------------------------------------------------------
    lines.append(QStringLiteral("### Block scripts"));
    lines.append(QStringLiteral("### 积木脚本"));
    const QVector<BlockScript *> &program = project->program();
    if (program.isEmpty()) {
        lines.append(QStringLiteral("(none)"));
    } else {
        for (BlockScript *script : program) {
            if (!script)
                continue;
            lines.append(QStringLiteral("- script id=%1 name=\"%2\" event=%3 enabled=%4 "
                                        "statements=%5")
                             .arg(script->id, escapeForContext(script->name), script->eventType,
                                  script->enabled ? QStringLiteral("yes") : QStringLiteral("no"))
                             .arg(script->blockCount()));
        }
    }

    // --- endpoints ---------------------------------------------------------
    // --- 端点 --------------------------------------------------------------
    const QJsonArray endpoints =
        project->backendConfig.value(QStringLiteral("endpoints")).toArray();
    lines.append(QStringLiteral("### Backend endpoints"));
    lines.append(QStringLiteral("### 后端端点"));
    if (endpoints.isEmpty()) {
        lines.append(QStringLiteral("(none)"));
    } else {
        for (const QJsonValue &value : endpoints) {
            const QJsonObject entry = value.toObject();
            //  Only the name and the method are shown; a URL may embed a token
            //  and the project must never leak one into a prompt.
            //  仅显示名称与方法；URL 中可能内嵌令牌，工程绝不能把它泄露进提示词。
            lines.append(QStringLiteral("- %1 (%2)")
                             .arg(escapeForContext(entry.value(QStringLiteral("name")).toString()),
                                  entry.value(QStringLiteral("method")).toString()));
        }
    }

    return lines.join(QStringLiteral("\n"));
}

// ---------------------------------------------------------------------------
//  Provider slots
//  厂商信号槽
// ---------------------------------------------------------------------------
void AgentRuntime::onStreamStarted(const QString &requestId)
{
    if (!m_busy || requestId != m_requestId)
        return;
}

void AgentRuntime::onTextDelta(const QString &requestId, const QString &delta)
{
    if (!m_busy || requestId != m_requestId || delta.isEmpty())
        return;
    m_streamingText += delta;
    emit assistantDelta(delta);
}

void AgentRuntime::onReasoningDelta(const QString &requestId, const QString &delta)
{
    if (!m_busy || requestId != m_requestId || delta.isEmpty())
        return;
    emit reasoningDelta(delta);
}

void AgentRuntime::onToolCallReady(const QString &requestId, const QVector<ToolCall> &calls)
{
    if (!m_busy || requestId != m_requestId || calls.isEmpty())
        return;

    m_pendingToolCalls = calls;
    for (const ToolCall &call : calls)
        emit toolCallQueued(call);
}

void AgentRuntime::onUsage(const QString &requestId, int in, int out)
{
    if (!m_busy || requestId != m_requestId)
        return;
    if (in > 0)
        m_inputTokens += in;
    if (out > 0)
        m_outputTokens += out;
    emit usageUpdated(m_inputTokens, m_outputTokens);
}

void AgentRuntime::onCompleted(const QString &requestId, const ChatMessage &message)
{
    if (!m_busy || requestId != m_requestId)
        return;
    if (m_cancelled)
        return;

    ChatMessage assistant = message;
    assistant.role = ChatRole::Assistant;
    assistant.timestamp = assistant.timestamp.isValid() ? assistant.timestamp
                                                        : QDateTime::currentDateTimeUtc();

    //  The streamed text was already emitted as deltas, so it is adopted rather
    //  than emitted again; `content` may already carry it from a non-streaming
    //  reply.
    //  流式文本已作为增量发出，因此此处只采用而不再重复发出；非流式回复可能
    //  已经在 `content` 中携带了它。
    if (assistant.content.isEmpty() && !m_streamingText.isEmpty())
        assistant.content = m_streamingText;
    assistant.streaming = false;

    m_streamingText.clear();
    appendMessage(assistant);

    if (m_pendingToolCalls.isEmpty()) {
        finishTurn(true, QString());
        return;
    }

    //  A tool round trip happened: spend one iteration and continue the loop.
    //  发生了一次工具往返：消耗一次迭代并继续循环。
    ++m_iteration;
    if (m_iteration > m_maxIterations) {
        m_pendingToolCalls.clear();
        finishTurn(false,
                   QStringLiteral("Stopped after %1 tool round trips without a final answer. "
                                  "Ask for a narrower change.")
                       .arg(m_maxIterations)
                       + QStringLiteral("\n")
                       + QStringLiteral("已完成 %1 轮工具调用仍未得到最终回答，已停止。"
                                        "请提出更具体的修改要求。")
                             .arg(m_maxIterations));
        return;
    }

    runPendingTools();
}

void AgentRuntime::onFailed(const QString &requestId, const AiError &error)
{
    if (requestId != m_requestId)
        return;
    if (m_cancelled) {
        //  `cancel()` already reported the turn as finished.
        //  `cancel()` 已经报告该轮次结束。
        return;
    }

    emit failed(error);

    QString messageEn = error.messageEn;
    QString messageZh = error.messageZh;
    if (messageEn.isEmpty()) {
        switch (error.kind) {
        case AiError::Network:
            messageEn = QStringLiteral("The request could not reach the provider. Check the "
                                       "network and the API address.");
            messageZh = QStringLiteral("请求无法到达厂商。请检查网络与接口地址。");
            break;
        case AiError::Auth:
            messageEn = QStringLiteral("Authentication failed. Check the API key or OAuth token "
                                       "for this provider.");
            messageZh = QStringLiteral("身份验证失败。请检查该厂商的 API Key 或 OAuth 令牌。");
            break;
        case AiError::RateLimit:
            messageEn = QStringLiteral("The provider is rate limiting this account. Wait a moment "
                                       "and try again.");
            messageZh = QStringLiteral("厂商正在限制该账户的请求频率。请稍候重试。");
            break;
        case AiError::BadRequest:
            messageEn = QStringLiteral("The provider rejected the request. Try a shorter "
                                       "conversation or a different model.");
            messageZh = QStringLiteral("厂商拒绝了该请求。请尝试缩短对话或更换模型。");
            break;
        case AiError::Server:
            messageEn = QStringLiteral("The provider reported a server error. This is usually "
                                       "temporary.");
            messageZh = QStringLiteral("厂商报告了服务器错误，通常是暂时性的。");
            break;
        case AiError::Cancelled:
            messageEn = QStringLiteral("Cancelled");
            messageZh = QStringLiteral("已取消");
            break;
        case AiError::Parse:
            messageEn = QStringLiteral("The reply could not be parsed.");
            messageZh = QStringLiteral("无法解析响应。");
            break;
        case AiError::Policy:
            messageEn = QStringLiteral("A policy blocked the request.");
            messageZh = QStringLiteral("某项策略阻止了该请求。");
            break;
        case AiError::Timeout:
            messageEn = QStringLiteral("The request timed out. Try again or shorten the "
                                       "conversation.");
            messageZh = QStringLiteral("请求超时。请重试或缩短对话长度。");
            break;
        case AiError::Unknown:
        case AiError::None:
        default:
            messageEn = QStringLiteral("The request failed for an unknown reason.");
            messageZh = QStringLiteral("请求因未知原因失败。");
            break;
        }
    }
    if (messageZh.isEmpty())
        messageZh = messageEn;

    const QString combined = messageEn + QStringLiteral("\n") + messageZh
                             + (error.detail.isEmpty()
                                    ? QString()
                                    : QStringLiteral("\n") + error.detail);
    finishTurn(false, combined);
}

} // namespace tcbw
