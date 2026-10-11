// ============================================================================
//  BackendScaffold.h -- generate a matching server project for the built page
//  BackendScaffold.h —— 为所构建的页面生成配套的服务器工程
// ============================================================================
#pragma once

#include "Endpoint.h"
#include "EmitOptions.h"

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace tcbw {

// ---------------------------------------------------------------------------
//  ScaffoldRequest
//  脚手架请求
// ---------------------------------------------------------------------------
struct ScaffoldRequest
{
    QString targetId;                 // BackendTarget::id / 后端目标 id
    QString projectName = QStringLiteral("tcbw-backend");
    QString outputDirectory;
    EndpointStore endpoints;
    bool includeCors = true;
    bool includeAuthStubs = true;
    bool includeDocker = false;
    bool includeReadme = true;
    bool includeEnvExample = true;
    QString nodeVersion = QStringLiteral("22");
    QString pythonVersion = QStringLiteral("3.12");
    bool chineseComments = false;

    QJsonObject toJson() const;
};

// ---------------------------------------------------------------------------
//  BackendScaffold
//  Emits a runnable starter server that already exposes the endpoints the user
//  defined, so the exported page works end to end out of the box.
//  生成可运行的起始服务器，它已暴露用户定义的端点，使导出的页面开箱即用、
//  端到端可通。
// ---------------------------------------------------------------------------
class BackendScaffold
{
public:
    BackendScaffold() = default;

    //  Generates the file set for a target. Returns an EmitResult so it can be
    //  written with CodeGenerator::writeToDirectory.
    //  为目标生成文件集合。返回 EmitResult，因此可以用
    //  CodeGenerator::writeToDirectory 写出。
    EmitResult generate(const ScaffoldRequest &request) const;

    //  File names a target would produce (for previewing in the UI).
    //  目标将产生的文件名（用于界面预览）。
    QStringList plannedFiles(const ScaffoldRequest &request) const;

    //  Human readable "what you get" description.
    //  人类可读的"你将得到什么"说明。
    static QString describe(const BackendTarget &target, bool chinese);

private:
    EmitResult generateNodeExpress(const ScaffoldRequest &request) const;
    EmitResult generatePythonFastApi(const ScaffoldRequest &request) const;
    EmitResult generateCloudflareWorker(const ScaffoldRequest &request) const;
    EmitResult generateSupabaseEdge(const ScaffoldRequest &request) const;
    EmitResult generateFirebaseFunctions(const ScaffoldRequest &request) const;
    EmitResult generateGoNetHttp(const ScaffoldRequest &request) const;
    EmitResult generateStaticJsonServer(const ScaffoldRequest &request) const;

    static EmittedFile textFile(const QString &path, const QString &contents,
                               const QString &mime = QStringLiteral("text/plain"));
};

} // namespace tcbw
