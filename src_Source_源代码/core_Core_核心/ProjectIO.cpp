// ============================================================================
//  ProjectIO.cpp -- reading, writing, autosaving and recent-file tracking
//  ProjectIO.cpp —— 读取、写入、自动保存与最近文件记录
// ============================================================================
#include "ProjectIO.h"

#include "AppSettings.h"
#include "IdGenerator.h"
#include "Types.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>

namespace tcbw {

namespace {

//  Version of the on-disk envelope (independent of the project schema).
//  磁盘信封的版本（与工程模式版本相互独立）。
constexpr int kFileEnvelopeVersion = 1;

//  Written as the very first bytes of every project file.
//  作为每个工程文件最前面的字节写入。
const QByteArray &magicLiteral()
{
    static const QByteArray magic = QByteArrayLiteral("TCTools-Build_Web");
    return magic;
}

//  The canonical, byte-exact prefix of a file we wrote. validateHeader() checks
//  this cheaply before paying for a full JSON parse; writeEnvelope() starts from
//  the same string so the two can never drift.
//  由我们写出的文件的规范、逐字节前缀。validateHeader() 先廉价检查它，再付出完整
//  JSON 解析的代价；writeEnvelope() 从同一字符串开始，二者因此永不错位。
QByteArray canonicalPrefix()
{
    QByteArray prefix;
    prefix.append("{\n  \"magic\": \"");
    prefix.append(magicLiteral());
    prefix.append("\",\n  \"schemaVersion\": ");
    prefix.append(QByteArray::number(ProjectDocument::kSchemaVersion));
    return prefix;
}

//  Localised error helper: English first, Chinese second.
//  本地化错误辅助：先英文，后中文。
QString err(const char *en, const char *zh)
{
    return QString::fromUtf8(en) + QStringLiteral(" / ") + QString::fromUtf8(zh);
}

//  The recent-file list lives in the same INI file as AppSettings. Rather than
//  adding a member to the frozen AppSettings header, ProjectIO constructs a
//  QSettings that points at exactly the same file, so both views stay in sync.
//  最近文件列表与 AppSettings 存放在同一个 INI 文件中。为避免修改冻结的
//  AppSettings 头文件，ProjectIO 构造一个指向同一文件的 QSettings，使两个视图
//  始终保持同步。
std::unique_ptr<QSettings> projectSettings()
{
    //  QSettings is neither copyable nor movable, so it is handed out as a
    //  unique_ptr.
    //  QSettings 既不可拷贝也不可移动，因此以 unique_ptr 形式返回。
    auto settings = std::make_unique<QSettings>(QSettings::IniFormat, QSettings::UserScope,
                                                QStringLiteral("turtlelnc"),
                                                QStringLiteral("TCTools-Build_Web"));
    //  AppSettings::settingsFilePath() reports the directory its own QSettings
    //  was pointed at, so following it guarantees both views share one file.
    //  AppSettings::settingsFilePath() 返回其自身 QSettings 所指向的目录，跟随它
    //  即可保证两个视图共用同一个文件。
    const QFileInfo info(AppSettings::instance().settingsFilePath());
    const QString directory = info.absolutePath();
    if (!directory.isEmpty())
        settings->setPath(QSettings::IniFormat, QSettings::UserScope, directory);
    return settings;
}

//  The directory that holds autosaves for the current user.
//  当前用户的自动保存目录。
QString autosaveDirectory()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        base = QDir::tempPath();
    return QDir(base).filePath(QStringLiteral("autosave"));
}

//  SHA-1 of a canonical path, used as the autosave file name.
//  规范化路径的 SHA-1，用作自动保存文件名。
QString pathDigest(const QString &filePath)
{
    const QString canonical = QFileInfo(filePath).absoluteFilePath();
    const QByteArray digest =
        QCryptographicHash::hash(canonical.toUtf8(), QCryptographicHash::Sha1);
    return QString::fromLatin1(digest.toHex());
}

//  Writes one node-free envelope around the project payload. The literal prefix
//  is emitted first so the file starts with the canonical bytes.
//  为工程载荷写入信封。先写出字面前缀，使文件以规范字节开头。
QByteArray writeEnvelope(const QJsonObject &project)
{
    QByteArray out = canonicalPrefix();
    out.append(",\n  \"fileVersion\": ");
    out.append(QByteArray::number(kFileEnvelopeVersion));
    out.append(",\n  \"application\": {\"name\": \"TCTools-Build_Web\", \"version\": \"");
    out.append(versionString().toUtf8());
    out.append("\"},\n  \"savedAt\": \"");
    out.append(QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toUtf8());
    out.append("\",\n  \"project\": ");
    out.append(QJsonDocument(project).toJson(QJsonDocument::Indented));
    out.append("}\n");
    return out;
}

//  Structural sanity check run after a successful parse.
//  解析成功之后运行的结构合理性检查。
bool structuralCheck(const ProjectDocument &project, QString *error)
{
    if (project.pageCount() == 0) {
        if (error)
            *error = err("The project contains no pages.",
                         "工程不包含任何页面。");
        return false;
    }
    for (int i = 0; i < project.pageCount(); ++i) {
        const Page *page = project.pageAt(i);
        if (!page || !page->root) {
            if (error)
                *error = err("A page has no element tree.",
                             "某个页面缺少元素树。");
            return false;
        }
        QStringList problems;
        if (!page->root->validate(&problems)) {
            if (error) {
                *error = err("A page failed validation: ", "某个页面校验失败：")
                         + problems.join(QStringLiteral("; "));
            }
            return false;
        }
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
//  Construction
//  构造
// ---------------------------------------------------------------------------
ProjectIO::ProjectIO(QObject *parent)
    : QObject(parent)
{
}

ProjectIO::~ProjectIO() = default;

// ---------------------------------------------------------------------------
//  Constants
//  常量
// ---------------------------------------------------------------------------
QString ProjectIO::fileFilter(bool chinese)
{
    return chinese ? QStringLiteral("TCTools-Build_Web 工程 (*.tcbw);;所有文件 (*)")
                   : QStringLiteral("TCTools-Build_Web project (*.tcbw);;All files (*)");
}

QByteArray ProjectIO::fileMagic()
{
    return canonicalPrefix();
}

bool ProjectIO::hasProjectExtension(const QString &path)
{
    return QFileInfo(path).suffix().compare(fileExtension(), Qt::CaseInsensitive) == 0;
}

// ---------------------------------------------------------------------------
//  Validation
//  校验
// ---------------------------------------------------------------------------
bool ProjectIO::validateHeader(const QByteArray &raw, QString *error)
{
    const QByteArray prefix = canonicalPrefix();
    if (raw.size() < prefix.size() || !raw.startsWith(prefix)) {
        if (error) {
            *error = err("This is not a TCTools-Build_Web project file "
                         "(magic header mismatch).",
                         "这不是 TCTools-Build_Web 工程文件（魔数头不匹配）。");
        }
        return false;
    }

    //  The header carries the schema version, so a cheap textual check can
    //  reject a file from a future release before parsing it.
    //  文件头携带模式版本，因此可以用廉价的文本检查在解析前拒绝来自未来版本的文件。
    const int versionToken = raw.indexOf("\"schemaVersion\":", 0);
    if (versionToken < 0 || versionToken > 512) {
        if (error) {
            *error = err("The project header is malformed (no schema version).",
                         "工程文件头损坏（缺少模式版本）。");
        }
        return false;
    }
    const int digitsStart = versionToken + static_cast<int>(qstrlen("\"schemaVersion\":"));
    int end = digitsStart;
    while (end < raw.size() && (raw.at(end) == ' ' || raw.at(end) == '\t'))
        ++end;
    int digitsEnd = end;
    while (digitsEnd < raw.size() && raw.at(digitsEnd) >= '0' && raw.at(digitsEnd) <= '9')
        ++digitsEnd;
    bool ok = false;
    const int version = raw.mid(end, digitsEnd - end).toInt(&ok);
    if (!ok) {
        if (error)
            *error = err("The project header is malformed (bad schema version).",
                         "工程文件头损坏（模式版本无效）。");
        return false;
    }
    if (version > ProjectDocument::kSchemaVersion) {
        if (error) {
            *error = QStringLiteral("This project was written by a newer version of "
                                    "TCTools-Build_Web (schema %1 > %2). / "
                                    "该工程由更新版本的 TCTools-Build_Web 写入"
                                    "（模式 %1 > %2）。")
                         .arg(version)
                         .arg(ProjectDocument::kSchemaVersion);
        }
        return false;
    }
    return true;
}

bool ProjectIO::validateSchema(const QJsonObject &root, QString *error)
{
    if (!root.contains(QStringLiteral("magic"))
        || root.value(QStringLiteral("magic")).toString() != QString::fromLatin1(magicLiteral())) {
        if (error)
            *error = err("Missing or wrong magic value.", "魔数缺失或不正确。");
        return false;
    }

    if (!root.contains(QStringLiteral("schemaVersion"))
        || !root.value(QStringLiteral("schemaVersion")).isDouble()) {
        if (error)
            *error = err("Missing or non-numeric schema version.",
                         "模式版本缺失或不是数字。");
        return false;
    }

    const int version = root.value(QStringLiteral("schemaVersion")).toInt(-1);
    if (version <= 0) {
        if (error)
            *error = err("The schema version must be positive.",
                         "模式版本必须为正数。");
        return false;
    }
    if (version > ProjectDocument::kSchemaVersion) {
        if (error) {
            *error = QStringLiteral("Unsupported schema version %1 (this build reads up to "
                                    "%2). / 不支持的模式版本 %1（本版本最高支持 %2）。")
                         .arg(version)
                         .arg(ProjectDocument::kSchemaVersion);
        }
        return false;
    }

    const QJsonValue project = root.value(QStringLiteral("project"));
    if (!project.isObject()) {
        if (error)
            *error = err("The file has no project payload.",
                         "文件中缺少工程内容。");
        return false;
    }
    const QJsonObject payload = project.toObject();
    if (!payload.contains(QStringLiteral("pages"))
        || !payload.value(QStringLiteral("pages")).isArray()) {
        if (error)
            *error = err("The project payload has no page array.",
                         "工程内容中缺少页面数组。");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
//  Loading
//  读取
// ---------------------------------------------------------------------------
std::unique_ptr<ProjectDocument> ProjectIO::load(const QString &filePath, QString *error)
{
    QString localError;
    emit loadStarted(filePath);

    //  Every failure path funnels through this lambda so loadFinished() is
    //  emitted exactly once and the message is always localised.
    //  所有失败路径都经由该 lambda，保证 loadFinished() 只发出一次，且消息始终本地化。
    auto fail = [&](const QString &message) -> std::unique_ptr<ProjectDocument> {
        if (error)
            *error = message;
        emit loadFinished(filePath, false, message);
        return nullptr;
    };

    if (filePath.trimmed().isEmpty())
        return fail(err("No file path was given.", "未提供文件路径。"));

    if (!hasProjectExtension(filePath)) {
        return fail(QStringLiteral("Only .%1 project files can be opened. / "
                                   "只能打开 .%1 工程文件。")
                        .arg(fileExtension()));
    }

    const QFileInfo info(filePath);
    if (!info.exists() || !info.isFile())
        return fail(err("The file does not exist.", "文件不存在。"));

    if (info.size() > static_cast<qint64>(kMaxProjectBytes)) {
        return fail(QStringLiteral("The project is larger than the %1 MB limit. / "
                                   "工程超过 %1 MB 上限。")
                        .arg(kMaxProjectBytes / (1024 * 1024)));
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return fail(QStringLiteral("Cannot open the file for reading: %1 / "
                                   "无法以只读方式打开文件：%1")
                        .arg(file.errorString()));
    }
    const QByteArray raw = file.readAll();
    file.close();

    if (raw.isEmpty())
        return fail(err("The file is empty.", "文件为空。"));

    if (!validateHeader(raw, &localError))
        return fail(localError);

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(raw, &parseError);
    if (document.isNull() || !document.isObject()) {
        return fail(QStringLiteral("The project file is not valid JSON: %1 / "
                                   "工程文件不是有效的 JSON：%1")
                        .arg(parseError.errorString()));
    }

    const QJsonObject root = document.object();
    if (!validateSchema(root, &localError))
        return fail(localError);

    auto project = ProjectDocument::fromJson(root.value(QStringLiteral("project")).toObject(),
                                             &localError);
    if (!project) {
        return fail(localError.isEmpty()
                        ? err("The project payload could not be read.",
                              "无法读取工程内容。")
                        : localError);
    }

    if (!structuralCheck(*project, &localError))
        return fail(localError);

    project->filePath = QFileInfo(filePath).absoluteFilePath();
    project->setModified(false);

    emit loadFinished(filePath, true, QString());
    return project;
}

std::unique_ptr<ProjectDocument> ProjectIO::fromJson(const QByteArray &json, QString *error)
{
    if (json.trimmed().isEmpty()) {
        if (error)
            *error = err("No JSON was supplied.", "未提供 JSON 内容。");
        return nullptr;
    }
    if (json.size() > kMaxProjectBytes) {
        if (error) {
            *error = QStringLiteral("The JSON payload is larger than the %1 MB limit. / "
                                    "JSON 内容超过 %1 MB 上限。")
                         .arg(kMaxProjectBytes / (1024 * 1024));
        }
        return nullptr;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (document.isNull() || !document.isObject()) {
        if (error) {
            *error = QStringLiteral("Not valid JSON: %1 / 不是有效的 JSON：%1")
                         .arg(parseError.errorString());
        }
        return nullptr;
    }

    const QJsonObject root = document.object();

    //  Both a bare project payload (tests, "paste JSON") and a full envelope are
    //  accepted; the envelope wins when the magic is present.
    if (root.contains(QStringLiteral("magic"))) {
        QString localError;
        if (!validateSchema(root, &localError)) {
            if (error)
                *error = localError;
            return nullptr;
        }
        auto project =
            ProjectDocument::fromJson(root.value(QStringLiteral("project")).toObject(), error);
        if (project && !structuralCheck(*project, error))
            return nullptr;
        if (project)
            project->setModified(false);
        return project;
    }

    if (!root.contains(QStringLiteral("pages"))) {
        if (error) {
            *error = err("This JSON object is neither a project payload nor a project file.",
                         "该 JSON 对象既不是工程内容也不是工程文件。");
        }
        return nullptr;
    }

    auto project = ProjectDocument::fromJson(root, error);
    if (project && !structuralCheck(*project, error))
        return nullptr;
    if (project)
        project->setModified(false);
    return project;
}

// ---------------------------------------------------------------------------
//  Saving
//  写入
// ---------------------------------------------------------------------------
QJsonObject ProjectIO::wrapForDisk(const ProjectDocument &project)
{
    QJsonObject root;
    root.insert(QStringLiteral("magic"), QString::fromLatin1(magicLiteral()));
    root.insert(QStringLiteral("schemaVersion"), ProjectDocument::kSchemaVersion);
    root.insert(QStringLiteral("fileVersion"), kFileEnvelopeVersion);

    QJsonObject application;
    application.insert(QStringLiteral("name"), QStringLiteral("TCTools-Build_Web"));
    application.insert(QStringLiteral("version"), versionString());
    root.insert(QStringLiteral("application"), application);

    root.insert(QStringLiteral("savedAt"),
                QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    root.insert(QStringLiteral("project"), project.toJson());
    return root;
}

QByteArray ProjectIO::toJsonBytes(const ProjectDocument &project) const
{
    return writeEnvelope(project.toJson());
}

QString ProjectIO::tempPathFor(const QString &filePath) const
{
    return filePath + QStringLiteral(".tmp-") + QString::number(QCoreApplication::applicationPid());
}

bool ProjectIO::atomicWrite(const QString &filePath, const QByteArray &data, QString *error)
{
    const QString tempPath = tempPathFor(filePath);

    //  A stale temp file from a crashed run must never confuse the rename.
    //  上次崩溃残留的临时文件绝不能干扰改名操作。
    QFile::remove(tempPath);

    QFile temp(tempPath);
    if (!temp.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) {
            *error = QStringLiteral("Cannot create the temporary file %1: %2 / "
                                    "无法创建临时文件 %1：%2")
                         .arg(tempPath, temp.errorString());
        }
        return false;
    }

    const qint64 written = temp.write(data);
    if (written != data.size()) {
        const QString message = temp.errorString();
        temp.close();
        QFile::remove(tempPath);
        if (error) {
            *error = QStringLiteral("Could not write the whole project: %1 / "
                                    "未能完整写入工程：%1")
                         .arg(message);
        }
        return false;
    }

    //  flush() pushes the QFile buffer to the OS and, on the QFile backend,
    //  fsyncs the descriptor, which is the durability guarantee the atomic
    //  rename relies on.
    //  flush() 把 QFile 缓冲区写入操作系统，并在 QFile 后端上对描述符执行 fsync，
    //  这正是一次性改名所依赖的持久化保证。
    if (!temp.flush()) {
        const QString message = temp.errorString();
        temp.close();
        QFile::remove(tempPath);
        if (error) {
            *error = QStringLiteral("Could not flush the project to disk: %1 / "
                                    "无法将工程刷新到磁盘：%1")
                         .arg(message);
        }
        return false;
    }

    temp.close();

    //  A truncated write is a real failure even when every call succeeded.
    //  即使每一步调用都成功，写入被截断仍是真实故障。
    if (QFileInfo(tempPath).size() != data.size()) {
        QFile::remove(tempPath);
        if (error) {
            *error = err("The temporary project file was written incompletely.",
                         "临时工程文件写入不完整。");
        }
        return false;
    }

    //  Atomic on POSIX; on Windows QFile::rename replaces an existing target.
    //  在 POSIX 上是原子的；在 Windows 上 QFile::rename 会替换已存在的目标。
    if (!QFile::rename(tempPath, filePath)) {
        QFile::remove(tempPath);
        if (error) {
            *error = QStringLiteral("Cannot replace %1 (is it open in another program?) / "
                                    "无法替换 %1（是否被其他程序占用？）")
                         .arg(filePath);
        }
        return false;
    }
    return true;
}

bool ProjectIO::save(const ProjectDocument &project, const QString &filePath, QString *error)
{
    auto fail = [&](const QString &message) {
        if (error)
            *error = message;
        emit saveFinished(filePath, false, message);
        return false;
    };

    if (filePath.trimmed().isEmpty())
        return fail(err("No file path was given.", "未提供文件路径。"));

    if (!hasProjectExtension(filePath)) {
        return fail(QStringLiteral("A project must be saved with the .%1 extension. / "
                                   "工程必须以 .%1 扩展名保存。")
                        .arg(fileExtension()));
    }

    const QFileInfo info(filePath);

    //  Refuse to clobber an unrelated file: an existing target must either be
    //  absent or already look like one of our projects.
    if (info.exists() && info.isFile()) {
        if (info.size() > static_cast<qint64>(kMaxProjectBytes)) {
            return fail(err("The existing file is larger than the project size limit; "
                            "refusing to overwrite it.",
                            "已存在的文件超过工程大小上限，拒绝覆盖。"));
        }
        QFile existing(filePath);
        if (existing.open(QIODevice::ReadOnly)) {
            const QByteArray head = existing.read(256);
            existing.close();
            QString headerError;
            if (!head.isEmpty() && !validateHeader(head, &headerError)
                && !head.startsWith("{")) {
                return fail(err("Refusing to overwrite a file that is not a project.",
                                "拒绝覆盖非工程文件。"));
            }
        }
    }

    const QDir parent = info.absoluteDir();
    if (!parent.exists() && !QDir().mkpath(parent.absolutePath())) {
        return fail(QStringLiteral("Cannot create the target directory %1 / "
                                   "无法创建目标目录 %1")
                        .arg(parent.absolutePath()));
    }

    const QByteArray data = toJsonBytes(project);
    if (data.size() > kMaxProjectBytes) {
        return fail(QStringLiteral("The project exceeds the %1 MB limit. / "
                                   "工程超过 %1 MB 上限。")
                        .arg(kMaxProjectBytes / (1024 * 1024)));
    }

    QString writeError;
    if (!atomicWrite(filePath, data, &writeError))
        return fail(writeError);

    emit saveFinished(filePath, true, QString());
    return true;
}

bool ProjectIO::saveAs(const ProjectDocument &project, const QString &directory,
                       const QString &baseName, QString *error)
{
    if (directory.trimmed().isEmpty()) {
        const QString message = err("No target directory was given.", "未提供目标目录。");
        if (error)
            *error = message;
        emit saveFinished(directory, false, message);
        return false;
    }

    QString name = baseName.trimmed();
    if (name.isEmpty())
        name = QStringLiteral("untitled");
    //  Strip any directory part and any extension the caller may have typed.
    //  去掉调用方可能输入的目录部分与扩展名。
    name = QFileInfo(name).completeBaseName();
    if (name.isEmpty())
        name = QStringLiteral("untitled");

    const QString target = QDir(directory).filePath(name + QLatin1Char('.') + fileExtension());
    return save(project, target, error);
}

// ---------------------------------------------------------------------------
//  Autosave / crash recovery
//  自动保存与崩溃恢复
// ---------------------------------------------------------------------------
void ProjectIO::enableAutosave(bool enabled, int intervalSeconds)
{
    m_autosaveEnabled = enabled;
    if (intervalSeconds > 0)
        m_autosaveIntervalSeconds = intervalSeconds;
}

QString ProjectIO::autosavePathFor(const QString &filePath) const
{
    return QDir(autosaveDirectory())
        .filePath(pathDigest(filePath) + QLatin1Char('.') + fileExtension());
}

bool ProjectIO::writeAutosave(const ProjectDocument &project, QString *error)
{
    //  Autosave always lands next to the user's application data, never next to
    //  the project, so a failed autosave can never damage the real file.
    //  自动保存始终写入用户应用数据目录，绝不写在工程旁边，因此自动保存失败永远
    //  不会损坏真正的工程文件。
    if (project.filePath.trimmed().isEmpty()) {
        if (error)
            *error = err("The project has never been saved, so there is nothing to "
                         "autosave against.",
                         "该工程从未保存过，没有可用的自动保存位置。");
        return false;
    }

    const QString directory = autosaveDirectory();
    if (!QDir().mkpath(directory)) {
        if (error) {
            *error = QStringLiteral("Cannot create the autosave directory %1 / "
                                    "无法创建自动保存目录 %1")
                         .arg(directory);
        }
        return false;
    }

    const QString target = autosavePathFor(project.filePath);
    QString writeError;
    if (!atomicWrite(target, toJsonBytes(project), &writeError)) {
        if (error)
            *error = writeError;
        return false;
    }

    emit autosaveWritten(target);
    return true;
}

bool ProjectIO::hasRecoverableAutosave(const QString &filePath) const
{
    if (filePath.trimmed().isEmpty())
        return false;

    const QFileInfo autosave(autosavePathFor(filePath));
    if (!autosave.exists() || !autosave.isFile())
        return false;

    const QFileInfo original(filePath);
    if (!original.exists())
        return true;    // the project itself is gone: the autosave is all we have
                        // 工程文件本身已不存在：自动保存是唯一凭据

    //  An autosave older than the project describes a state the user already
    //  moved past, so it is not worth offering.
    //  早于工程的自动保存描述的是用户已经越过的状态，不值得提示恢复。
    return autosave.lastModified() > original.lastModified();
}

std::unique_ptr<ProjectDocument> ProjectIO::recoverAutosave(const QString &filePath,
                                                            QString *error)
{
    if (filePath.trimmed().isEmpty()) {
        if (error)
            *error = err("No file path was given.", "未提供文件路径。");
        return nullptr;
    }

    const QString autosavePath = autosavePathFor(filePath);
    if (!QFileInfo::exists(autosavePath)) {
        if (error)
            *error = err("There is no autosave for this project.",
                         "该工程没有自动保存文件。");
        return nullptr;
    }

    auto project = load(autosavePath, error);
    if (!project)
        return nullptr;

    //  The recovered document belongs to the real project path, not to the
    //  autosave file, so saving it again writes where the user expects.
    //  恢复出的文档属于真正的工程路径，而不是自动保存文件，因此再次保存会写回用户
    //  期望的位置。
    project->filePath = QFileInfo(filePath).absoluteFilePath();
    project->setModified(true);
    return project;
}

void ProjectIO::discardAutosave(const QString &filePath)
{
    if (filePath.trimmed().isEmpty())
        return;
    QFile::remove(autosavePathFor(filePath));
}

// ---------------------------------------------------------------------------
//  Recent files
//  最近文件
// ---------------------------------------------------------------------------
QStringList ProjectIO::recentFiles() const
{
    const std::unique_ptr<QSettings> settings = projectSettings();
    const QStringList stored = settings->value(QStringLiteral("tcbw/recentFiles")).toStringList();

    //  Entries that no longer exist are dropped as they are read.
    //  读取时丢弃已不存在的条目。
    QStringList alive;
    for (const QString &entry : stored) {
        if (entry.trimmed().isEmpty())
            continue;
        if (!QFileInfo::exists(entry))
            continue;
        if (!alive.contains(entry))
            alive.append(entry);
        if (alive.size() >= kMaxRecentFiles)
            break;
    }

    if (alive != stored) {
        settings->setValue(QStringLiteral("tcbw/recentFiles"), alive);
        settings->sync();
    }
    return alive;
}

void ProjectIO::addRecentFile(const QString &filePath)
{
    if (filePath.trimmed().isEmpty())
        return;

    const QString absolute = QFileInfo(filePath).absoluteFilePath();
    QStringList list = recentFiles();
    list.removeAll(absolute);
    list.prepend(absolute);
    while (list.size() > kMaxRecentFiles)
        list.removeLast();

    const std::unique_ptr<QSettings> settings = projectSettings();
    settings->setValue(QStringLiteral("tcbw/recentFiles"), list);
    settings->sync();
}

void ProjectIO::removeRecentFile(const QString &filePath)
{
    const QString absolute = QFileInfo(filePath).absoluteFilePath();
    QStringList list = recentFiles();
    if (!list.removeAll(absolute))
        return;

    const std::unique_ptr<QSettings> settings = projectSettings();
    settings->setValue(QStringLiteral("tcbw/recentFiles"), list);
    settings->sync();
}

void ProjectIO::clearRecentFiles()
{
    const std::unique_ptr<QSettings> settings = projectSettings();
    settings->setValue(QStringLiteral("tcbw/recentFiles"), QStringList());
    settings->sync();
}

} // namespace tcbw
