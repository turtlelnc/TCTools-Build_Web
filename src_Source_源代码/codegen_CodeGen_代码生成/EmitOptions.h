// ============================================================================
//  EmitOptions.h -- shared options for every code emitter
//  EmitOptions.h —— 所有代码生成器共享的选项
// ============================================================================
#pragma once

#include "BlockCompiler.h"
#include "Types.h"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace tcbw {

// ---------------------------------------------------------------------------
//  EmitOptions
//  生成选项
// ---------------------------------------------------------------------------
struct EmitOptions
{
    ExportMode mode = ExportMode::SingleHtml;

    bool pretty = true;
    int indentWidth = 2;
    bool minifyCss = false;
    bool minifyJs = false;
    bool includeSourceComments = true;

    bool responsive = true;              // emit tablet/mobile media queries
    bool includeResetCss = true;
    bool includeAccessibilityHelpers = true;
    bool includeNoScriptFallback = true;

    QString siteTitle;
    QString siteDescription;
    QString author;
    QString language = QStringLiteral("en");

    QString cssFileName = QStringLiteral("styles.css");
    QString jsFileName = QStringLiteral("app.js");
    QString assetsDir = QStringLiteral("assets");

    bool inlineCss = false;              // <style> instead of <link>
    bool inlineJs = false;               // <script> instead of <script src>

    //  Extra fragments injected by the backend pre-linking subsystem.
    //  由后端预链接子系统注入的额外片段。
    QStringList extraHeadHtml;
    QStringList extraBodyEndHtml;
    QStringList extraScriptUrls;

    CompileOptions compile;

    //  Derives the compiler options that match this emitter configuration.
    //  推导出与当前生成器配置匹配的编译器选项。
    CompileOptions compileOptions() const;
};

// ---------------------------------------------------------------------------
//  EmittedFile
//  生成的文件
// ---------------------------------------------------------------------------
struct EmittedFile
{
    QString relativePath;    // e.g. "index.html" / 例如 "index.html"
    QString contents;        // text payload / 文本内容
    QByteArray bytes;        // binary payload (assets) / 二进制内容（资源）
    QString mimeType = QStringLiteral("text/plain");
    bool binary = false;

    qint64 size() const { return binary ? bytes.size() : contents.toUtf8().size(); }
};

// ---------------------------------------------------------------------------
//  EmitResult
//  生成结果
// ---------------------------------------------------------------------------
struct EmitResult
{
    bool ok = false;
    QVector<EmittedFile> files;
    QStringList errors;
    QStringList warnings;
    QStringList requiredBrowserApis;

    const EmittedFile *file(const QString &relativePath) const;
    QString fileContents(const QString &relativePath) const;
    qint64 totalBytes() const;
};

// ---------------------------------------------------------------------------
//  Shared text helpers
//  共享文本辅助函数
// ---------------------------------------------------------------------------
QString indentBlock(const QString &text, int spaces);
QString escapeHtml(const QString &text, bool attributeContext = false);
QString escapeJsForHtml(const QString &text);
QString escapeCssIdentifier(const QString &text);
QString slugify(const QString &text);
bool isSafeUrl(const QString &url);
bool isSafeAttributeName(const QString &name);
bool isSafeTagName(const QString &tag);

} // namespace tcbw
