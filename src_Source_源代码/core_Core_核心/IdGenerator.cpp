// ============================================================================
//  IdGenerator.cpp -- collision-free, human-friendly identifiers
//  IdGenerator.cpp —— 无冲突且便于阅读的标识符生成器
// ============================================================================
#include "IdGenerator.h"

#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QUuid>

namespace tcbw {

QString IdGenerator::next(const QString &prefix)
{
    //  QUuid::createUuid() is documented to use a cryptographically secure
    //  source on all supported platforms.
    //  QUuid::createUuid() 在所有受支持平台上均使用密码学安全随机源。
    const QString raw = QUuid::createUuid().toString(QUuid::Id128);
    return prefix + QLatin1Char('-') + raw.left(8);
}

QString IdGenerator::elementId() { return next(QStringLiteral("el")); }
QString IdGenerator::pageId()    { return next(QStringLiteral("pg")); }
QString IdGenerator::blockId()   { return next(QStringLiteral("bk")); }
QString IdGenerator::scriptId()  { return next(QStringLiteral("sc")); }
QString IdGenerator::projectId() { return next(QStringLiteral("prj")); }

QString IdGenerator::randomToken(int bytes)
{
    if (bytes < 8)
        bytes = 8;
    if (bytes > 256)
        bytes = 256;

    QByteArray buffer;
    buffer.reserve(bytes + 4);
    while (buffer.size() < bytes) {
        const quint32 word = QRandomGenerator::system()->generate();
        buffer.append(reinterpret_cast<const char *>(&word), 4);
    }
    buffer.truncate(bytes);

    //  base64url without padding, per RFC 7636 section 4.
    //  按 RFC 7636 第 4 节要求，使用无填充的 base64url。
    return QString::fromLatin1(
        buffer.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QString IdGenerator::pkceChallenge(const QString &verifier)
{
    const QByteArray digest =
        QCryptographicHash::hash(verifier.toUtf8(), QCryptographicHash::Sha256);
    return QString::fromLatin1(
        digest.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

bool IdGenerator::isValid(const QString &id)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z][A-Za-z0-9_\\-]{0,63}$"));
    return re.match(id).hasMatch();
}

} // namespace tcbw
