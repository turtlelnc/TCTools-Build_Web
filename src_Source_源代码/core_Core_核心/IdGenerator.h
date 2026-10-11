// ============================================================================
//  IdGenerator.h -- collision-free, human-friendly identifiers
//  IdGenerator.h —— 无冲突且便于阅读的标识符生成器
// ============================================================================
#pragma once

#include <QString>

namespace tcbw {

// Generates identifiers of the form "<prefix>-<8 hex chars>".
// 生成形如 "<prefix>-<8 位十六进制>" 的标识符。
//
// The sequence is backed by QUuid, which is seeded from the OS CSPRNG, so the
// generator is safe to use for security-relevant names such as OAuth state.
// 序列由 QUuid 提供，其种子来自操作系统密码学随机源，因此可安全用于
// OAuth state 等与安全相关的名称。
class IdGenerator
{
public:
    static QString next(const QString &prefix = QStringLiteral("n"));
    static QString elementId();
    static QString pageId();
    static QString blockId();
    static QString scriptId();
    static QString projectId();

    //  Random URL-safe token, used for OAuth state / PKCE verifiers.
    //  随机 URL 安全令牌，用于 OAuth state / PKCE verifier。
    static QString randomToken(int bytes = 32);

    //  RFC 7636 PKCE code challenge derived from a verifier.
    //  由 verifier 推导出的 RFC 7636 PKCE code challenge。
    static QString pkceChallenge(const QString &verifier);

    //  Validates that a string is a syntactically acceptable identifier.
    //  校验字符串是否为语法上可接受的标识符。
    static bool isValid(const QString &id);
};

} // namespace tcbw
