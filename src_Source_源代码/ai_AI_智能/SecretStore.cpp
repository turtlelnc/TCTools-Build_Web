// ============================================================================
//  SecretStore.cpp -- credential storage with an honest threat model
//  SecretStore.cpp —— 具有诚实威胁模型的凭据存储
//
//  WHAT THIS FILE GUARANTEES / 本文件提供的保证
//    * A secret never reaches a log line, a project file or a QSettings entry.
//      机密绝不会出现在日志、工程文件或 QSettings 条目中。
//    * On Windows the vault is sealed with DPAPI and is therefore bound to the
//      current user account on this machine.
//      在 Windows 上，保险库使用 DPAPI 密封，因此绑定于本机的当前用户账户。
//    * On macOS every secret is an individual login-keychain item.
//      在 macOS 上，每个机密都是登录钥匙串中的独立条目。
//    * Elsewhere the vault is obfuscated with a per-install key file. That is
//      NOT a security boundary and protectionDescription() says so.
//      在其他平台上，保险库使用每安装密钥文件做混淆。这并非安全边界，
//      protectionDescription() 会如实说明。
//
//  ON-DISK ENVELOPE / 磁盘信封格式
//      [0 .. 15]  16-byte magic: "TCBWSEC1" followed by 8 zero bytes
//                16 字节魔数："TCBWSEC1" 加 8 个零字节
//      [16 .. 27] 12-byte random nonce, regenerated on every write
//                12 字节随机 nonce，每次写入都重新生成
//      [28 ..   ] backend specific payload / 与后端相关的载荷
//                 Windows DPAPI : the CryptProtectData blob (DPAPI carries its
//                                 own integrity protection)
//                                 CryptProtectData 数据块（DPAPI 自带完整性保护）
//                 macOS         : plain UTF-8 JSON array of key NAMES (the
//                                 values themselves live in the Keychain and
//                                 this file never contains a secret)
//                                 纯文本 UTF-8 的键名 JSON 数组（值本身保存在钥匙串中，
//                                 该文件绝不含机密）
//                 file fallback : base64( ciphertext || 16-byte tag ) where the
//                                 ciphertext is the XOR of the JSON with a
//                                 SHA-256 keystream derived from key.bin and the
//                                 nonce, and the tag is the first 16 bytes of
//                                 SHA-256(key || nonce || ciphertext)
//                                 base64(密文 || 16 字节标签)，密文为 JSON 与
//                                 「key.bin + nonce 派生的 SHA-256 密钥流」的异或，
//                                 标签为 SHA-256(key || nonce || 密文) 的前 16 字节
//
//    The envelope therefore always has the documented magic + nonce + payload
//    shape. There is no AES-GCM tag because Qt ships no AES API and this
//    project forbids third-party dependencies; the fallback tag is an
//    integrity check, not a MAC, and the documentation says so.
//    因此信封始终是「魔数 + nonce + 载荷」的既定形态。这里没有 AES-GCM 标签，
//    因为 Qt 不提供 AES 接口且本项目禁止第三方依赖；回退方案的标签是完整性校验，
//    而不是消息认证码，文档中已如实说明。
// ============================================================================
//  NOTE / 注意: SecretStore.h declares `QMap<QString, QString> m_memory` but does
//  not include <QMap> itself. The header is frozen, so this translation unit
//  pulls the type in first. Reported to the Lead as a header nit.
//  注意：SecretStore.h 声明了 `QMap<QString, QString> m_memory`，但自身没有包含
//  <QMap>。该头文件已冻结，因此本编译单元先把它引入。已作为头文件小问题上报。
#include <QMap>

#include "SecretStore.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>

#include <utility>

#if defined(Q_OS_WIN)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
//  DPAPI is present in every supported Windows version. The macro below lets a
//  build opt out explicitly (TCBW_DISABLE_DPAPI) or opt in explicitly
//  (TCBW_ENABLE_DPAPI, e.g. from CMake); either way the code stays portable.
//  DPAPI 在所有受支持的 Windows 版本中都存在。下面的宏允许构建显式关闭
//  （TCBW_DISABLE_DPAPI）或显式开启（TCBW_ENABLE_DPAPI，例如由 CMake 定义）；
//  无论如何代码都保持可移植。
#  if !defined(TCBW_ENABLE_DPAPI) && !defined(TCBW_DISABLE_DPAPI)
#    define TCBW_ENABLE_DPAPI 1
#  endif
#  include <windows.h>
#  if defined(TCBW_ENABLE_DPAPI)
#    include <wincrypt.h>
#  endif
#endif

#if defined(Q_OS_MACOS)
#  include <QProcess>
#endif

namespace tcbw {
namespace {

// ---------------------------------------------------------------------------
//  Constants
//  常量
// ---------------------------------------------------------------------------
constexpr int kMagicSize = 16;
constexpr int kNonceSize = 12;
constexpr int kHeaderSize = kMagicSize + kNonceSize;
constexpr int kTagSize = 16;
constexpr int kKeySize = 32;

//  "TCBWSEC1" followed by eight zero bytes, per the documented envelope.
//  "TCBWSEC1" 加 8 个零字节，符合既定的信封格式。
const char kMagic[kMagicSize] = "TCBWSEC1";

const char kKeyFileName[] = "key.bin";
const char kVaultFileName[] = "vault.bin";

//  macOS Keychain service name; matches MACOSX_BUNDLE_GUI_IDENTIFIER.
//  macOS 钥匙串服务名；与 MACOSX_BUNDLE_GUI_IDENTIFIER 一致。
const char kKeychainService[] = "com.turtlelnc.tctools.buildweb";

QByteArray randomBytes(int count)
{
    QByteArray out;
    out.reserve(count + 4);
    while (out.size() < count) {
        const quint32 word = QRandomGenerator::system()->generate();
        out.append(reinterpret_cast<const char *>(&word), 4);
    }
    out.truncate(count);
    return out;
}

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

QString ownerOnlyPermissionsHint()
{
    return QStringLiteral("(requested mode 0600 / 请求权限 0600)");
}

// ---------------------------------------------------------------------------
//  Keystream: SHA-256(key || nonce || counter) concatenated until long enough.
//  密钥流：把 SHA-256(key || nonce || 计数器) 依次拼接至足够长度。
// ---------------------------------------------------------------------------
QByteArray keystream(const QByteArray &key, const QByteArray &nonce, int length)
{
    QByteArray out;
    out.reserve(length + 32);
    quint32 counter = 0;
    while (out.size() < length) {
        QByteArray block = key;
        block.append(nonce);
        block.append(static_cast<char>((counter >> 24) & 0xFF));
        block.append(static_cast<char>((counter >> 16) & 0xFF));
        block.append(static_cast<char>((counter >> 8) & 0xFF));
        block.append(static_cast<char>(counter & 0xFF));
        out.append(QCryptographicHash::hash(block, QCryptographicHash::Sha256));
        ++counter;
    }
    out.truncate(length);
    return out;
}

QByteArray xorBytes(const QByteArray &data, const QByteArray &stream)
{
    QByteArray out = data;
    const int limit = qMin(out.size(), stream.size());
    for (int i = 0; i < limit; ++i)
        out[i] = static_cast<char>(out.at(i) ^ stream.at(i));
    return out;
}

// ---------------------------------------------------------------------------
//  Vault JSON: { "openai/api-key": "<base64 secret>", ... }
//  保险库 JSON：{ "openai/api-key": "<base64 机密>", ... }
// ---------------------------------------------------------------------------
QByteArray encodeVault(const QMap<QString, QString> &memory)
{
    QJsonObject obj;
    for (auto it = memory.constBegin(); it != memory.constEnd(); ++it) {
        if (it.value().isEmpty())
            continue;
        obj.insert(it.key(), QString::fromLatin1(it.value().toUtf8().toBase64()));
    }
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

bool decodeVault(const QByteArray &json, QMap<QString, QString> *memory, QString *error)
{
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (doc.isNull()) {
        setError(error, QStringLiteral("The credential store could not be parsed: %1 / "
                                       "凭据存储无法解析：%1")
                            .arg(parseError.errorString()));
        return false;
    }

    memory->clear();
    if (doc.isArray()) {
        //  macOS index: names only, values are fetched from the Keychain.
        //  macOS 索引：只有名称，值从钥匙串获取。
        const QJsonArray array = doc.array();
        for (const QJsonValue &entry : array) {
            if (entry.isString())
                memory->insert(entry.toString(), QString());
        }
        return true;
    }

    const QJsonObject obj = doc.object();
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        const QString encoded = it.value().toString();
        if (encoded.isEmpty()) {
            memory->insert(it.key(), QString());
            continue;
        }
        const QByteArray decoded = QByteArray::fromBase64(encoded.toLatin1());
        //  A value that is not valid base64 is kept verbatim so a hand-edited
        //  file still round-trips instead of silently losing data.
        //  非合法 base64 的值按原样保留，使手工编辑过的文件仍能往返，
        //  而不会静默丢数据。
        memory->insert(it.key(), decoded.isEmpty() ? encoded : QString::fromUtf8(decoded));
    }
    return true;
}

#if defined(Q_OS_WIN) && defined(TCBW_ENABLE_DPAPI)
// ---------------------------------------------------------------------------
//  Windows DPAPI
//  Windows DPAPI
// ---------------------------------------------------------------------------
QByteArray dpapiProtect(const QByteArray &plain, const QByteArray &entropy, QString *error)
{
    DATA_BLOB in{};
    DATA_BLOB ent{};
    DATA_BLOB out{};
    in.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(plain.constData()));
    in.cbData = static_cast<DWORD>(plain.size());

    DATA_BLOB *entropyPtr = nullptr;
    if (!entropy.isEmpty()) {
        ent.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(entropy.constData()));
        ent.cbData = static_cast<DWORD>(entropy.size());
        entropyPtr = &ent;
    }

    //  CRYPTPROTECT_UI_FORBIDDEN: never pop a Windows dialog from a settings
    //  screen; a failure must be an error string, not a modal prompt.
    //  CRYPTPROTECT_UI_FORBIDDEN：绝不在设置界面弹出 Windows 对话框；
    //  失败时应返回错误字符串，而不是模态提示。
    //
    //  Argument order matters: (pDataIn, szDataDescr, pOptionalEntropy,
    //  pvReserved, pPromptStruct, dwFlags, pDataOut). The envelope nonce is the
    //  optional entropy, so it must be the third argument and pvReserved must
    //  stay null - mixing them up produces a blob that CryptUnprotectData
    //  rejects with ERROR_INVALID_DATA.
    //  参数顺序很关键：(pDataIn, szDataDescr, pOptionalEntropy, pvReserved,
    //  pPromptStruct, dwFlags, pDataOut)。信封里的 nonce 就是可选熵，必须作为
    //  第三个参数，pvReserved 必须保持为空——弄混会生成 CryptUnprotectData
    //  以 ERROR_INVALID_DATA 拒绝的数据块。
    if (!CryptProtectData(&in, L"TC-BuildWeb credential", entropyPtr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        setError(error, QStringLiteral("Windows DPAPI refused to protect the credential store "
                                       "(error %1) / Windows DPAPI 拒绝保护凭据存储（错误 %1）")
                            .arg(static_cast<qulonglong>(GetLastError())));
        return QByteArray();
    }

    QByteArray result(reinterpret_cast<const char *>(out.pbData), static_cast<int>(out.cbData));
    LocalFree(out.pbData);
    SecureZeroMemory(in.pbData, in.cbData);
    return result;
}

QByteArray dpapiUnprotect(const QByteArray &sealed, const QByteArray &entropy, QString *error)
{
    DATA_BLOB in{};
    DATA_BLOB ent{};
    DATA_BLOB out{};
    in.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(sealed.constData()));
    in.cbData = static_cast<DWORD>(sealed.size());

    DATA_BLOB *entropyPtr = nullptr;
    if (!entropy.isEmpty()) {
        ent.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(entropy.constData()));
        ent.cbData = static_cast<DWORD>(entropy.size());
        entropyPtr = &ent;
    }

    if (!CryptUnprotectData(&in, nullptr, entropyPtr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        setError(error, QStringLiteral("Windows DPAPI could not decrypt the credential store. It "
                                       "was probably created by a different Windows user account "
                                       "or on another machine (error %1) / Windows DPAPI 无法解密"
                                       "凭据存储，它很可能由其他 Windows 用户账户或在其他机器上"
                                       "创建（错误 %1）")
                            .arg(static_cast<qulonglong>(GetLastError())));
        return QByteArray();
    }

    QByteArray result(reinterpret_cast<const char *>(out.pbData), static_cast<int>(out.cbData));
    LocalFree(out.pbData);
    return result;
}
#endif // Q_OS_WIN && TCBW_ENABLE_DPAPI

#if defined(Q_OS_MACOS)
// ---------------------------------------------------------------------------
//  macOS Keychain through /usr/bin/security
//  通过 /usr/bin/security 使用 macOS 钥匙串
//
//  NOTE / 注意: the secret is passed as a command line argument, which means it
//  is briefly visible in the process table. Using the Security framework
//  directly would avoid that, at the cost of an Objective-C++ translation unit.
//  注意：机密作为命令行参数传递，因此会在进程表中短暂可见。直接调用
//  Security 框架可以避免这一点，但代价是需要一个 Objective-C++ 编译单元。
// ---------------------------------------------------------------------------
const QString &securityTool()
{
    static const QString path = QStringLiteral("/usr/bin/security");
    return path;
}

bool securityAvailable()
{
    return QFileInfo::exists(securityTool());
}

int runSecurity(const QStringList &arguments, QByteArray *stdOut = nullptr)
{
    QProcess process;
    process.setProgram(securityTool());
    process.setArguments(arguments);
    //  Detach stdin so 'security' can never block waiting for a password.
    //  断开标准输入，避免 security 阻塞等待密码。
    process.setStandardInputFile(QProcess::nullDevice());
    process.start();
    if (!process.waitForStarted(5000))
        return -1;
    if (!process.waitForFinished(15000)) {
        process.kill();
        process.waitForFinished(2000);
        return -1;
    }
    if (stdOut)
        *stdOut = process.readAllStandardOutput();
    if (process.exitStatus() != QProcess::NormalExit)
        return -1;
    return process.exitCode();
}

bool keychainStore(const QString &account, const QString &secret, QString *error)
{
    if (!securityAvailable()) {
        setError(error, QStringLiteral("The macOS 'security' tool was not found, so the login "
                                       "keychain cannot be used / 未找到 macOS 的 security 工具，"
                                       "因此无法使用登录钥匙串"));
        return false;
    }
    const QStringList arguments{
        QStringLiteral("add-generic-password"), QStringLiteral("-U"),
        QStringLiteral("-s"), QString::fromLatin1(kKeychainService),
        QStringLiteral("-a"), account,
        QStringLiteral("-w"), secret
    };
    const int code = runSecurity(arguments);
    if (code != 0) {
        setError(error, QStringLiteral("The login keychain rejected the credential (security exit "
                                       "code %1) / 登录钥匙串拒绝了该凭据（security 退出码 %1）")
                            .arg(code));
        return false;
    }
    return true;
}

QString keychainLoad(const QString &account)
{
    if (!securityAvailable())
        return QString();
    QByteArray out;
    const QStringList arguments{
        QStringLiteral("find-generic-password"),
        QStringLiteral("-s"), QString::fromLatin1(kKeychainService),
        QStringLiteral("-a"), account,
        QStringLiteral("-w")
    };
    if (runSecurity(arguments, &out) != 0)
        return QString();
    while (out.endsWith('\n') || out.endsWith('\r'))
        out.chop(1);
    return QString::fromUtf8(out);
}

bool keychainRemove(const QString &account)
{
    if (!securityAvailable())
        return false;
    const QStringList arguments{
        QStringLiteral("delete-generic-password"),
        QStringLiteral("-s"), QString::fromLatin1(kKeychainService),
        QStringLiteral("-a"), account
    };
    return runSecurity(arguments) == 0;
}
#endif // Q_OS_MACOS

} // namespace

// ---------------------------------------------------------------------------
//  Lifecycle
//  生命周期
// ---------------------------------------------------------------------------
SecretStore::SecretStore(QObject *parent)
    : QObject(parent)
{
}

SecretStore::~SecretStore()
{
    //  Wipe the decrypted cache; the file on disk keeps its own protection.
    //  清除解密后的缓存；磁盘文件仍有各自的保护。
    m_memory.clear();
}

SecretStore &SecretStore::instance()
{
    static SecretStore store;
    return store;
}

bool SecretStore::initialise(QString *error)
{
    if (m_initialised)
        return true;

    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty()) {
        m_backend = InMemory;
        m_initialised = true;
        setError(error, QStringLiteral("No writable application data directory is available, so "
                                       "credentials are kept in memory for this session only / "
                                       "没有可写的应用数据目录，因此凭据仅在本次会话中保存在内存里"));
        return true;
    }

    const QString directory = base + QStringLiteral("/secrets");
    if (!QDir().mkpath(directory)) {
        m_backend = InMemory;
        m_initialised = true;
        setError(error, QStringLiteral("Could not create %1, so credentials are kept in memory for "
                                       "this session only / 无法创建 %1，因此凭据仅在本次会话中"
                                       "保存在内存里")
                            .arg(QDir::toNativeSeparators(directory)));
        return true;
    }

    m_storagePath = directory + QLatin1Char('/') + QString::fromLatin1(kVaultFileName);

    // --- backend selection / 后端选择 -------------------------------------
#if defined(Q_OS_WIN) && defined(TCBW_ENABLE_DPAPI)
    m_backend = Dpapi;
#elif defined(Q_OS_MACOS)
    if (securityAvailable()) {
        m_backend = Keychain;
    } else {
        m_backend = EncryptedFile;
        setError(error, QStringLiteral("The macOS 'security' tool was not found; falling back to "
                                       "the obfuscated file store, which is not a security "
                                       "boundary / 未找到 macOS 的 security 工具；回退到混淆文件"
                                       "存储，它并非安全边界"));
    }
#else
    //  Linux and anything else: obfuscation with a per-install key file.
    //  Linux 及其他平台：使用每安装密钥文件做混淆。
    m_backend = EncryptedFile;
#endif

    m_initialised = true;

    //  A failure to load is reported but never fatal: the app must still start,
    //  and the user can re-enter the credential.
    //  加载失败会报告，但绝不致命：应用仍须启动，用户可以重新输入凭据。
    QString loadError;
    if (!loadVault(&loadError)) {
        if (error && !loadError.isEmpty())
            *error = loadError;
        else
            setError(error, loadError);
        return true;
    }
    return true;
}

// ---------------------------------------------------------------------------
//  CRUD
//  增删改查
// ---------------------------------------------------------------------------
bool SecretStore::store(const QString &key, const QString &secret, QString *error)
{
    if (key.trimmed().isEmpty()) {
        setError(error, QStringLiteral("An empty credential key was rejected / 空的凭据键已被拒绝"));
        return false;
    }
    if (!m_initialised && !initialise(error))
        return false;

    //  Storing an empty value means "forget this credential".
    //  存入空值表示「忘记该凭据」。
    if (secret.isEmpty())
        return remove(key, error);

#if defined(Q_OS_MACOS)
    if (m_backend == Keychain) {
        if (!keychainStore(key, secret, error))
            return false;
    }
#endif

    m_memory.insert(key, secret);
    return saveVault(error);
}

QString SecretStore::retrieve(const QString &key) const
{
    if (key.isEmpty() || !m_initialised)
        return QString();
#if defined(Q_OS_MACOS)
    if (m_backend == Keychain)
        return keychainLoad(key);
#endif
    return m_memory.value(key);
}

bool SecretStore::contains(const QString &key) const
{
    if (key.isEmpty() || !m_initialised)
        return false;
    return m_memory.contains(key);
}

bool SecretStore::remove(const QString &key, QString *error)
{
    if (key.isEmpty())
        return false;
    if (!m_initialised && !initialise(error))
        return false;
    if (!m_memory.contains(key))
        return true;

#if defined(Q_OS_MACOS)
    if (m_backend == Keychain)
        keychainRemove(key);
#endif

    m_memory.remove(key);
    return saveVault(error);
}

QStringList SecretStore::keys() const
{
    if (!m_initialised)
        return QStringList();
    return m_memory.keys();
}

QStringList SecretStore::keysWithPrefix(const QString &prefix) const
{
    QStringList out;
    if (!m_initialised)
        return out;
    for (auto it = m_memory.constBegin(); it != m_memory.constEnd(); ++it) {
        if (it.key().startsWith(prefix))
            out.append(it.key());
    }
    return out;
}

void SecretStore::clear()
{
    if (!m_initialised)
        initialise();

    //  Remove the Keychain items first: once the index is gone there is no way
    //  to find them again.
    //  先删除钥匙串条目：索引一旦丢失，就再也找不到它们了。
#if defined(Q_OS_MACOS)
    if (m_backend == Keychain) {
        const QStringList existing = m_memory.keys();
        for (const QString &key : existing)
            keychainRemove(key);
    }
#endif

    m_memory.clear();
    saveVault();
}

// ---------------------------------------------------------------------------
//  Diagnostics
//  诊断
// ---------------------------------------------------------------------------
QString SecretStore::backendName() const
{
    switch (m_backend) {
    case Dpapi:
        return QStringLiteral("Windows DPAPI");
    case Keychain:
        return QStringLiteral("macOS Keychain");
    case EncryptedFile:
        return QStringLiteral("Per-install key file");
    case InMemory:
        break;
    }
    return QStringLiteral("Memory only");
}

QString SecretStore::protectionDescription(bool chinese) const
{
    switch (m_backend) {
    case Dpapi:
        return chinese
            ? QStringLiteral("由 Windows DPAPI 保护：只有本机上你的 Windows 用户账户可以解密这些"
                             "数值。把文件复制到其他机器或其他用户账户后将无法读取。")
            : QStringLiteral("Protected by Windows DPAPI: only your Windows user account on this "
                             "machine can decrypt these values. Copying the file to another "
                             "machine or another user account makes it unreadable.");
    case Keychain:
        return chinese
            ? QStringLiteral("保存在 macOS 登录钥匙串中，由系统加密并在你解锁登录时保护。")
            : QStringLiteral("Stored in your macOS login Keychain, encrypted by the system and "
                             "unlocked while you are logged in.");
    case EncryptedFile:
        //  Deliberately blunt: this path is obfuscation, and the UI must not
        //  claim otherwise.
        //  刻意直白：这条路径只是混淆，界面绝不能宣称其他说法。
        return chinese
            ? QStringLiteral("磁盘上仅做混淆——并非安全边界。任何能读取你主目录的人都能读到这些"
                             "数值。")
            : QStringLiteral("Obfuscated on disk — not a security boundary. Anyone who can read "
                             "your home directory can read these values.");
    case InMemory:
        break;
    }
    return chinese
        ? QStringLiteral("仅保存在本次会话的内存中，不会写入磁盘；重启后需要重新输入。")
        : QStringLiteral("Kept in memory for this session only; nothing is written to disk, so "
                         "credentials must be entered again after a restart.");
}

bool SecretStore::isHardwareOrOsBacked() const
{
    return m_backend == Dpapi || m_backend == Keychain;
}

// ---------------------------------------------------------------------------
//  Sealing
//  密封
// ---------------------------------------------------------------------------
QByteArray SecretStore::seal(const QByteArray &plain, QString *error) const
{
    const QByteArray nonce = randomBytes(kNonceSize);

    QByteArray envelope;
    envelope.reserve(kHeaderSize + plain.size() + 64);
    envelope.append(kMagic, kMagicSize);
    envelope.append(nonce);

    switch (m_backend) {
    case Dpapi: {
#if defined(Q_OS_WIN) && defined(TCBW_ENABLE_DPAPI)
        const QByteArray sealed = dpapiProtect(plain, nonce, error);
        if (sealed.isEmpty())
            return QByteArray();
        envelope.append(sealed);
#else
        setError(error, QStringLiteral("This build was compiled without DPAPI support / "
                                       "本次构建未启用 DPAPI 支持"));
        return QByteArray();
#endif
        break;
    }
    case Keychain:
        //  Index only: the values themselves never reach this file.
        //  仅索引：数值本身绝不进入该文件。
        envelope.append(plain);
        break;
    case EncryptedFile: {
        const QByteArray key = fallbackKey();
        const QByteArray cipher = xorBytes(plain, keystream(key, nonce, plain.size()));
        QByteArray tagInput = key;
        tagInput.append(nonce);
        tagInput.append(cipher);
        const QByteArray tag =
            QCryptographicHash::hash(tagInput, QCryptographicHash::Sha256).left(kTagSize);
        envelope.append((cipher + tag).toBase64());
        break;
    }
    case InMemory:
        envelope.append(plain);
        break;
    }
    return envelope;
}

QByteArray SecretStore::unseal(const QByteArray &sealed, QString *error) const
{
    if (sealed.size() < kHeaderSize || !sealed.startsWith(QByteArray(kMagic, kMagicSize))) {
        setError(error, QStringLiteral("The credential store file is not a TC-BuildWeb vault / "
                                       "该凭据存储文件不是 TC-BuildWeb 保险库"));
        return QByteArray();
    }

    const QByteArray nonce = sealed.mid(kMagicSize, kNonceSize);
    const QByteArray payload = sealed.mid(kHeaderSize);

    switch (m_backend) {
    case Dpapi: {
#if defined(Q_OS_WIN) && defined(TCBW_ENABLE_DPAPI)
        return dpapiUnprotect(payload, nonce, error);
#else
        setError(error, QStringLiteral("This build was compiled without DPAPI support / "
                                       "本次构建未启用 DPAPI 支持"));
        return QByteArray();
#endif
    }
    case Keychain:
        return payload;
    case EncryptedFile: {
        const QByteArray raw = QByteArray::fromBase64(payload);
        if (raw.size() < kTagSize) {
            setError(error, QStringLiteral("The credential store is truncated / 凭据存储已损坏"
                                           "（长度不足）"));
            return QByteArray();
        }
        const QByteArray cipher = raw.left(raw.size() - kTagSize);
        const QByteArray tag = raw.right(kTagSize);

        const QByteArray key = fallbackKey();
        QByteArray tagInput = key;
        tagInput.append(nonce);
        tagInput.append(cipher);
        const QByteArray expected =
            QCryptographicHash::hash(tagInput, QCryptographicHash::Sha256).left(kTagSize);
        if (expected != tag) {
            //  Wrong tag means either a damaged file or a different key.bin.
            //  Either way the plaintext must not be handed out.
            //  标签不符意味着文件损坏或 key.bin 不同。无论哪种情况都不能返回明文。
            setError(error, QStringLiteral("The credential store failed its integrity check (the "
                                           "per-install key file may have changed) / 凭据存储未通过"
                                           "完整性校验（每安装密钥文件可能已改变）"));
            return QByteArray();
        }
        return xorBytes(cipher, keystream(key, nonce, cipher.size()));
    }
    case InMemory:
        break;
    }
    return payload;
}

QByteArray SecretStore::fallbackKey() const
{
    if (m_storagePath.isEmpty())
        return QByteArray(kKeySize, '\0');

    const QString directory = QFileInfo(m_storagePath).absolutePath();
    const QString keyPath = directory + QLatin1Char('/') + QString::fromLatin1(kKeyFileName);

    QFile existing(keyPath);
    if (existing.open(QIODevice::ReadOnly)) {
        const QByteArray key = existing.readAll();
        existing.close();
        if (key.size() == kKeySize)
            return key;
    }

    QByteArray fresh = randomBytes(kKeySize);
    QFile file(keyPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(fresh);
        file.close();
        //  Owner read/write only; the obfuscation is worthless if the key is
        //  world readable.
        //  仅所有者可读写；若密钥对所有人可读，这层混淆就毫无意义。
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
#if !defined(Q_OS_WIN)
        {
            const QFile::Permissions actual = QFile::permissions(keyPath);
            if (actual.testFlag(QFile::ReadGroup) || actual.testFlag(QFile::ReadOther)) {
                //  Only the file name is logged, never its contents.
                //  日志中只出现文件名，绝不出现文件内容。
                qWarning("SecretStore: %s is readable by other accounts; the fallback store is "
                         "only obfuscation", qPrintable(QFileInfo(keyPath).fileName()));
            }
        }
#endif
        return fresh;
    }

    //  Degraded path: no key file could be written. Derive a stable key from
    //  the user name so the vault at least round-trips within one session set.
    //  降级路径：无法写入密钥文件。改用用户名派生一个稳定密钥，使保险库至少能在
    //  同一用户下往返。
    QByteArray material = QByteArrayLiteral("TC-BuildWeb/no-keyfile/") + qgetenv("USERNAME")
                          + qgetenv("USER");
    return QCryptographicHash::hash(material, QCryptographicHash::Sha256);
}

// ---------------------------------------------------------------------------
//  Vault I/O
//  保险库读写
// ---------------------------------------------------------------------------
bool SecretStore::loadVault(QString *error)
{
    m_vaultRaw.clear();
    m_memory.clear();

    if (m_storagePath.isEmpty())
        return true;

    QFile file(m_storagePath);
    if (!file.exists())
        return true;                       // a fresh install / 全新安装
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, QStringLiteral("The credential store could not be opened: %1 / "
                                       "无法打开凭据存储：%1")
                            .arg(file.errorString()));
        return false;
    }

    m_vaultRaw = file.readAll();
    file.close();

    if (m_vaultRaw.isEmpty())
        return true;

    QString unsealError;
    const QByteArray plain = unseal(m_vaultRaw, &unsealError);
    if (plain.isEmpty() && !unsealError.isEmpty()) {
        //  Keep the unreadable file for forensics instead of letting the next
        //  save silently destroy it; only the file name is logged.
        //  保留无法读取的文件以便排查，避免下一次保存静默覆盖它；日志中只出现文件名。
        const QString backup = m_storagePath + QStringLiteral(".unreadable");
        QFile::remove(backup);
        QFile::rename(m_storagePath, backup);
        qWarning("SecretStore: vault at %s could not be read and was moved aside",
                 qPrintable(QFileInfo(backup).fileName()));
        setError(error, unsealError);
        return false;
    }

    QString decodeError;
    if (!decodeVault(plain, &m_memory, &decodeError)) {
        setError(error, decodeError);
        return false;
    }
    return true;
}

bool SecretStore::saveVault(QString *error)
{
    if (m_storagePath.isEmpty()) {
        m_backend = InMemory;
        return true;
    }

    //  On the Keychain backend this file holds key names, never values; on the
    //  other backends it holds the sealed vault.
    //  在钥匙串后端，该文件只保存键名而不保存数值；在其他后端保存密封后的保险库。
    const QByteArray plain = (m_backend == Keychain || m_backend == InMemory)
                                 ? QJsonDocument(
                                       [this] {
                                           QJsonArray names;
                                           for (auto it = m_memory.constBegin();
                                                it != m_memory.constEnd(); ++it) {
                                               names.append(it.key());
                                           }
                                           return names;
                                       }())
                                       .toJson(QJsonDocument::Compact)
                                 : encodeVault(m_memory);

    QString sealError;
    const QByteArray sealed = seal(plain, &sealError);
    if (sealed.isEmpty()) {
        setError(error, sealError.isEmpty()
                            ? QStringLiteral("The credential store could not be sealed / "
                                             "无法密封凭据存储")
                            : sealError);
        return false;
    }

    const QFileInfo info(m_storagePath);
    if (!QDir().mkpath(info.absolutePath())) {
        setError(error, QStringLiteral("Could not create %1 / 无法创建 %1")
                            .arg(QDir::toNativeSeparators(info.absolutePath())));
        return false;
    }

    //  QSaveFile gives us atomic replacement: a crash mid-write can never leave
    //  a half-written vault behind.
    //  QSaveFile 提供原子替换：写入过程中崩溃也不会留下半截保险库。
    QSaveFile file(m_storagePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setError(error, QStringLiteral("The credential store could not be written: %1 / "
                                       "无法写入凭据存储：%1")
                            .arg(file.errorString()));
        return false;
    }
    if (file.write(sealed) != sealed.size() || !file.commit()) {
        setError(error, QStringLiteral("The credential store could not be written: %1 / "
                                       "无法写入凭据存储：%1")
                            .arg(file.errorString()));
        return false;
    }

    //  Restrict to owner read/write (0600) after the write, as documented.
    //  写入后按文档要求收紧为仅所有者读写（0600）。
    if (!file.setPermissions(QFile::ReadOwner | QFile::WriteOwner)) {
        //  Not fatal on filesystems without POSIX permissions (FAT, some network
        //  shares) but the caller deserves to know.
        //  在不支持 POSIX 权限的文件系统（FAT、部分网络共享）上不算致命，
        //  但调用方有权知道。
        if (error && error->isEmpty())
            *error = QStringLiteral("Stored, but the vault permissions could not be restricted %1 / "
                                    "已保存，但无法收紧保险库权限 %1")
                         .arg(ownerOnlyPermissionsHint());
    } else {
#if !defined(Q_OS_WIN)
        //  Verify rather than trust: on POSIX the whole point of the fallback
        //  design is that nobody else can read the key file or the vault.
        //  Windows has no POSIX permission bits to check - DPAPI is the real
        //  boundary there - so the check is skipped on purpose.
        //  要验证而不是相信：在 POSIX 上，回退方案的全部意义就在于别人读不到密钥
        //  文件与保险库。Windows 没有可检查的 POSIX 权限位——那里 DPAPI 才是真正的
        //  安全边界——因此刻意跳过该检查。
        const QFile::Permissions actual = QFile::permissions(m_storagePath);
        if (actual.testFlag(QFile::ReadGroup) || actual.testFlag(QFile::ReadOther)
            || actual.testFlag(QFile::WriteGroup) || actual.testFlag(QFile::WriteOther)) {
            if (error && error->isEmpty())
                *error = QStringLiteral("Stored, but %1 is still readable by other accounts on this "
                                        "machine %2 / 已保存，但 %1 对本机其他账户仍然可读 %2")
                             .arg(QFileInfo(m_storagePath).fileName(),
                                  ownerOnlyPermissionsHint());
        }
#endif
    }

    m_vaultRaw = sealed;
    return true;
}

// ---------------------------------------------------------------------------
//  Migration
//  迁移
// ---------------------------------------------------------------------------
int SecretStore::migrateFromSettings(QStringList *migratedKeys)
{
    if (!m_initialised)
        initialise();

    //  Matches api-key, api_key, apikey, token, accessToken, client_secret,
    //  password, ... anywhere in the settings key.
    //  在设置键的任意位置匹配 api-key、api_key、apikey、token、accessToken、
    //  client_secret、password 等。
    static const QRegularExpression secretish(
        QStringLiteral("api[-_]?key|token|secret|password"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression unsafeSegment(QStringLiteral("[^a-z0-9_\\-]"));

    QSettings settings;
    const QStringList all = settings.allKeys();

    int moved = 0;
    for (const QString &key : all) {
        if (key.isEmpty() || !secretish.match(key).hasMatch())
            continue;

        const QString value = settings.value(key).toString();
        if (value.trimmed().isEmpty()) {
            //  An empty leftover is still plaintext clutter; drop it.
            //  空的遗留项同样是明文残留，直接清除。
            settings.remove(key);
            continue;
        }

        //  Derive a vendor id from the settings path: the trailing segment is
        //  the property name ("apiKey", "client_secret", ...), the segment
        //  before it is the vendor. "ai/openai/apiKey" becomes "openai/api-key"
        //  and "openai/apiKey" becomes "openai/api-key" as well.
        //  从设置路径推导厂商 id：最后一段是属性名（"apiKey"、"client_secret" 等），
        //  它前面的一段是厂商。"ai/openai/apiKey" 变为 "openai/api-key"，
        //  "openai/apiKey" 同样变为 "openai/api-key"。
        QStringList segments = key.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        if (segments.size() > 1)
            segments.removeLast();
        QString vendorId = segments.isEmpty() ? QString() : segments.constLast().toLower();
        vendorId.remove(unsafeSegment);
        if (vendorId.isEmpty())
            vendorId = QStringLiteral("imported");
        const QString storeKey = vendorId + QStringLiteral("/api-key");

        QString storeError;
        if (store(storeKey, value, &storeError)) {
            settings.remove(key);
            ++moved;
            //  Only the key NAME is logged. Never the value.
            //  日志中只出现键「名」，绝不出现值。
            qInfo("SecretStore: migrated settings key '%s' into the credential store",
                  qPrintable(key));
            if (migratedKeys)
                migratedKeys->append(storeKey);
        } else {
            qWarning("SecretStore: could not migrate settings key '%s': %s",
                     qPrintable(key), qPrintable(storeError));
        }
    }

    if (moved > 0)
        settings.sync();
    return moved;
}

} // namespace tcbw
