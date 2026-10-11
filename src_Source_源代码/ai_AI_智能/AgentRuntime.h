// ============================================================================
//  AgentRuntime.h -- the tool-calling conversation loop
//  AgentRuntime.h —— 工具调用对话循环
// ============================================================================
#pragma once

#include "AgentTools.h"
#include "AiProvider.h"
#include "AiTypes.h"

#include <QObject>
#include <QString>
#include <QVector>

#include <memory>

namespace tcbw {

// ---------------------------------------------------------------------------
//  AgentRuntime
//  Drives one conversation: user turn -> model -> (tool calls -> results)* ->
//  final answer. Every step is observable through signals so the UI can render
//  streaming text and tool cards.
//  驱动一次对话：用户轮次 -> 模型 -> （工具调用 -> 结果）* -> 最终回答。
//  每一步都通过信号可观测，以便界面渲染流式文本与工具卡片。
// ---------------------------------------------------------------------------
class AgentRuntime : public QObject
{
    Q_OBJECT

public:
    AgentRuntime(AiClient *client, AgentToolRegistry *registry, QObject *parent = nullptr);
    ~AgentRuntime() override;

    // --- configuration -----------------------------------------------------
    void setSystemPrompt(const QString &prompt);
    QString systemPrompt() const { return m_systemPrompt; }

    void setToolContext(const ToolContext &context) { m_context = context; }
    ToolContext &toolContext() { return m_context; }

    //  Hard cap on model<->tool round trips for a single user turn. Prevents a
    //  misbehaving model from looping forever and burning the user's quota.
    //  单个用户轮次中模型与工具往返的硬上限。防止行为异常的模型无限循环、
    //  消耗用户额度。
    void setMaxToolIterations(int iterations);
    int maxToolIterations() const { return m_maxIterations; }

    void setMaxTranscriptMessages(int messages);
    void setIncludeProjectOutline(bool include) { m_includeOutline = include; }

    // --- conversation ------------------------------------------------------
    void sendUserMessage(const QString &text);
    void cancel();
    void reset();
    void retryLast();

    const QVector<ChatMessage> &transcript() const { return m_transcript; }
    bool isBusy() const { return m_busy; }
    int totalInputTokens() const { return m_inputTokens; }
    int totalOutputTokens() const { return m_outputTokens; }

    //  Approximate context usage, for the UI meter.
    //  供界面显示的上下文占用估算。
    int estimatedContextTokens() const;

    //  Compact summary of the project injected into the system prompt so the
    //  model can act without a round trip.
    //  注入系统提示词的工程摘要，使模型无需往返即可行动。
    QString buildProjectContext() const;

signals:
    void started();
    void messageAdded(const ChatMessage &message);
    void assistantDelta(const QString &delta);
    void reasoningDelta(const QString &delta);
    void toolCallQueued(const ToolCall &call);
    void toolCallStarted(const ToolCall &call);
    void toolCallFinished(const ToolCall &call, const ToolResult &result);
    void usageUpdated(int inputTokens, int outputTokens);
    void turnFinished(bool ok, const QString &errorText);
    void failed(const AiError &error);

private slots:
    void onStreamStarted(const QString &requestId);
    void onTextDelta(const QString &requestId, const QString &delta);
    void onReasoningDelta(const QString &requestId, const QString &delta);
    void onToolCallReady(const QString &requestId, const QVector<ToolCall> &calls);
    void onUsage(const QString &requestId, int in, int out);
    void onCompleted(const QString &requestId, const ChatMessage &message);
    void onFailed(const QString &requestId, const AiError &error);

private:
    void dispatchToModel();
    void runPendingTools();
    void finishTurn(bool ok, const QString &errorText);
    void appendMessage(const ChatMessage &message);
    QVector<ChatMessage> windowedTranscript() const;

    AiClient *m_client = nullptr;
    AgentToolRegistry *m_registry = nullptr;
    ToolContext m_context;
    QVector<ChatMessage> m_transcript;
    QString m_systemPrompt;
    QString m_requestId;
    QVector<ToolCall> m_pendingToolCalls;
    int m_maxIterations = 12;
    int m_iteration = 0;
    int m_maxTranscriptMessages = 40;
    int m_inputTokens = 0;
    int m_outputTokens = 0;
    bool m_busy = false;
    bool m_includeOutline = true;
    bool m_cancelled = false;
    QString m_streamingText;
};

} // namespace tcbw
