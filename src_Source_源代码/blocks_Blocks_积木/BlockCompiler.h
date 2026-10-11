// ============================================================================
//  BlockCompiler.h -- turns block scripts into JavaScript
//  BlockCompiler.h —— 将积木脚本编译为 JavaScript
// ============================================================================
#pragma once

#include "BlockDef.h"
#include "Types.h"

#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

namespace tcbw {

// ---------------------------------------------------------------------------
//  CompileOptions
//  编译选项
// ---------------------------------------------------------------------------
struct CompileOptions
{
    bool pretty = true;              // indented output / 缩进输出
    int indentWidth = 2;
    bool useStrict = true;           // emit "use strict" / 输出 "use strict"
    bool wrapInIife = true;          // isolate globals / 隔离全局变量
    bool emitSourceMapComment = false;
    bool esm = false;                // ES module output / ES 模块输出
    QString moduleName = QStringLiteral("TCBuildWebApp");
    QString targetElementSelector;   // default mount point / 默认挂载点
};

// ---------------------------------------------------------------------------
//  CompileResult
//  编译结果
// ---------------------------------------------------------------------------
struct CompileResult
{
    bool ok = false;
    QString code;
    QStringList errors;
    QStringList warnings;
    QStringList requiredApis;        // e.g. "fetch", "localStorage" / 需要的浏览器接口
    QSet<QString> usedVariables;
    int statementCount = 0;
    int scriptCount = 0;

    QString errorText() const { return errors.join(QStringLiteral("\n")); }
};

// ---------------------------------------------------------------------------
//  BlockCompiler
//  Deterministic, single-pass compiler. It never executes user input; it only
//  produces source text. Identifiers are sanitised so a malicious block field
//  cannot break out of its syntactic position.
//  确定性、单遍编译器。它从不执行用户输入，只产生源代码文本。标识符会被净化，
//  因此恶意字段无法突破其语法位置。
// ---------------------------------------------------------------------------
class BlockCompiler
{
public:
    explicit BlockCompiler(const CompileOptions &options = CompileOptions());
    ~BlockCompiler();

    //  Compiles one script. `scriptName` is used for the generated function
    //  name when no event handler wrapper is applicable.
    //  编译单个脚本。当不适用事件处理包装时，`scriptName` 用于生成函数名。
    CompileResult compileScript(const BlockScript &script);

    //  Compiles a whole program (all scripts of a page).
    //  编译整个程序（某页面的全部脚本）。
    CompileResult compileProgram(const QVector<BlockScript *> &scripts);

    //  Renders a reporter/boolean block as an expression string.
    //  将返回值/布尔积木渲染为表达式字符串。
    QString compileExpression(const Block &block, QStringList *errors = nullptr) const;

    // -- shared helpers, also used by the JS emitter and the AI tool layer --
    // -- 共享辅助函数，JS 生成器与 AI 工具层同样使用 --

    //  Escapes a value for inclusion inside a single-quoted JS string.
    //  转义值以便放入单引号 JS 字符串中。
    static QString escapeJsString(const QString &value);

    //  Escapes a value for inclusion inside a JS template literal.
    //  转义值以便放入 JS 模板字符串中。
    static QString escapeJsTemplate(const QString &value);

    //  Converts arbitrary text into a safe JavaScript identifier.
    //  将任意文本转换为安全的 JavaScript 标识符。
    static QString sanitiseIdentifier(const QString &value, const QString &fallback =
                                          QStringLiteral("value"));

    //  Renders a JS string literal (single quoted, fully escaped).
    //  渲染 JS 字符串字面量（单引号、完整转义）。
    static QString jsString(const QString &value);

    //  Parses a numeric field defensively; returns fallback when not a number.
    //  防御性解析数值字段；非数值时返回 fallback。
    static double parseNumber(const QString &text, double fallback = 0.0);

    //  Resolves a numeric field that may contain a variable reference.
    //  解析可能包含变量引用的数值字段。
    QString numericExpression(const QString &raw) const;

private:
    QString compileBlock(const Block &block, int indent, QStringList *out);
    QString compileCShape(const Block &block, const BlockDef &def, int indent,
                          QStringList *out);
    QString shadowValue(const Block &parent, const QString &inputName,
                        const BlockDef &def, const BlockInputDef &inputDef) const;
    QString indentText(int level) const;

    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace tcbw
