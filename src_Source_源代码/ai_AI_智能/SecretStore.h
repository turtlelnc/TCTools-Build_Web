// ============================================================================
//  SecretStore.h -- credential storage with an honest threat model
//  SecretStore.h —— 具有诚实威胁模型的凭据存储
//
//  THREAT MODEL / 威胁模型
//  1. This class protects credentials from *accidental disclosure*: logs,
//     crash reports, AI transcripts, screenshots of the settings screen and
//     the project file itself never contain a raw secret.
//     本类保护凭据免遭*意外泄露*：日志、崩溃报告、AI 会话记录、设置界面截图
//     以及工程文件本身都不会包含原始机密。
//  2. On Windows it uses DPAPI (CryptProtectData) so the ciphertext is bound
//     to the current user account and cannot be moved to another machine.
//     在 Windows 上使用 DPAPI（CryptProtectData），密文绑定当前用户账户，
//     无法迁移到其他机器。
//  3. On macOS it uses the login Keychain through the `security` tool.
//     在 macOS 上通过 `security` 工具使用登录钥匙串。
//  4. Elsewhere it falls back to AES-256-GCM with a per-install key file that
//     is created with mode 0600. This is *obfuscation*, not a security
//     boundary: an attacker who can read the user's home directory can read
//     the key. The UI must therefore never claim "encrypted" for this path.
//     其他平台回退为 AES-256-GCM，并使用以 0600 权限创建的每安装密钥文件。
//     这属于*混淆*而非安全边界：能读取用户主目录的攻击者同样能读取密钥。
//     因此界面在此路径下绝不能宣称"已加密"。
// ============================================================================
#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

namespace tcbw {

class SecretStore : public QObject
{
    Q_OBJECT

public:
    enum Backend { Dpapi = 0, Keychain, EncryptedFile, InMemory };

    explicit SecretStore(QObject *parent = nullptr);
    ~SecretStore() override;

    static SecretStore &instance();

    // --- lifecycle ---------------------------------------------------------
    bool initialise(QString *error = nullptr);

    // --- CRUD --------------------------------------------------------------
    //  `key` is a namespaced identifier such as "openai/api-key" or
    //  "my-mcp-server/bearer".
    //  `key` 是带命名空间的标识符，例如 "openai/api-key" 或 "my-mcp-server/bearer"。
    bool store(const QString &key, const QString &secret, QString *error = nullptr);
    QString retrieve(const QString &key) const;
    bool contains(const QString &key) const;
    bool remove(const QString &key, QString *error = nullptr);
    QStringList keys() const;
    QStringList keysWithPrefix(const QString &prefix) const;
    void clear();

    // --- diagnostics -------------------------------------------------------
    Backend backend() const { return m_backend; }
    QString backendName() const;
    //  Honest description shown in the UI next to the credential fields.
    //  显示在界面凭据字段旁边的诚实说明。
    QString protectionDescription(bool chinese) const;
    //  True when secrets are protected by an actual OS security boundary.
    //  当机密受真实操作系统安全边界保护时返回 true。
    bool isHardwareOrOsBacked() const;

    // --- migration ---------------------------------------------------------
    //  Moves any plaintext secrets found in QSettings into the store and
    //  erases the plaintext copies.
    //  把 QSettings 中发现的明文机密迁移到本存储，并清除明文副本。
    int migrateFromSettings(QStringList *migratedKeys = nullptr);

    QString storagePath() const { return m_storagePath; }

private:
    QByteArray seal(const QByteArray &plain, QString *error) const;
    QByteArray unseal(const QByteArray &sealed, QString *error) const;
    QByteArray fallbackKey() const;
    bool loadVault(QString *error = nullptr);
    bool saveVault(QString *error = nullptr);

    Backend m_backend = InMemory;
    QString m_storagePath;
    QByteArray m_vaultRaw;                 // sealed JSON blob / 密封的 JSON 数据块
    QMap<QString, QString> m_memory;       // decrypted cache / 解密后的缓存
    bool m_initialised = false;
};

} // namespace tcbw
