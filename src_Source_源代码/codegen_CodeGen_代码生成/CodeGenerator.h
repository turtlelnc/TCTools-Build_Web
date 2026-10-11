// ============================================================================
//  CodeGenerator.h -- the single entry point used by the UI and the AI tools
//  CodeGenerator.h —— 界面层与 AI 工具层使用的唯一入口
// ============================================================================
#pragma once

#include "EmitOptions.h"
#include "ProjectDocument.h"

#include <QString>
#include <QStringList>

namespace tcbw {

// ---------------------------------------------------------------------------
//  CodeGenerator
//  Turns a ProjectDocument into the files the user asked for. All entry points
//  are const and re-entrant: generating code must never mutate the project.
//  将 ProjectDocument 转换为用户所需的文件。所有入口均为 const 且可重入：
//  生成代码绝不能修改工程。
// ---------------------------------------------------------------------------
class CodeGenerator
{
public:
    explicit CodeGenerator(const EmitOptions &options = EmitOptions());

    void setOptions(const EmitOptions &options);
    const EmitOptions &options() const { return m_options; }

    //  Generates every file for the whole project according to the export mode.
    //  依据导出模式为整个工程生成全部文件。
    EmitResult generateProject(const ProjectDocument &project) const;

    //  Generates the output for a single page.
    //  为单个页面生成输出。
    EmitResult generatePage(const Page &page, const QVector<BlockScript *> &program,
                            const ProjectDocument *project = nullptr) const;

    //  Convenience wrappers used by the export dialog.
    //  导出对话框使用的便捷封装。
    QString generateHtml(const ProjectDocument &project, const QString &pageId = QString()) const;
    QString generateJavaScript(const ProjectDocument &project,
                               const QString &pageId = QString()) const;
    QString generateCss(const ProjectDocument &project, const QString &pageId = QString()) const;

    //  Writes an EmitResult into `directory`, creating sub-directories as
    //  needed. Refuses to write outside `directory` and refuses symlinked
    //  targets, so an AI-driven export cannot escape the chosen folder.
    //  将 EmitResult 写入 `directory`，必要时创建子目录。拒绝写出 `directory`
    //  之外，并拒绝符号链接目标，使 AI 驱动的导出无法逃逸所选文件夹。
    static bool writeToDirectory(const EmitResult &result, const QString &directory,
                                 QStringList *errors = nullptr);

    //  Suggests a file name for a page respecting the export mode.
    //  依据导出模式为页面建议文件名。
    QString suggestedFileName(const Page &page) const;

    //  Human readable summary of what a generation produced, for the UI.
    //  面向界面的生成结果摘要。
    static QString summarise(const EmitResult &result, bool chinese);

private:
    QString buildDocument(const Page &page, const QVector<BlockScript *> &program,
                          const ProjectDocument *project) const;
    QString buildStylesheet(const Page &page, const ProjectDocument *project) const;
    QString buildScript(const Page &page, const QVector<BlockScript *> &program,
                        const ProjectDocument *project) const;

    EmitOptions m_options;
};

} // namespace tcbw
