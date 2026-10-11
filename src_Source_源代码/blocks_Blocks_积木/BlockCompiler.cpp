// ============================================================================
//  BlockCompiler.cpp -- turns block scripts into safe JavaScript
//  BlockCompiler.cpp —— 将积木脚本编译为安全的 JavaScript
//
//  Design notes / 设计说明:
//    * The compiler is deterministic and single pass. It never executes user
//      input; it only produces source text.
//    * 编译器是确定性、单遍的。它从不执行用户输入，只产生源码文本。
//    * Every value that lands in string position goes through jsString(), every
//      value that lands in identifier position goes through
//      sanitiseIdentifier(). Numbers are re-emitted as literals or as a
//      sanitised identifier reference, never as raw user text.
//    * 所有进入字符串位置的值都经过 jsString()，所有进入标识符位置的值都经过
//      sanitiseIdentifier()。数值会被重新输出为字面量或净化后的标识符引用，
//      绝不直接输出用户原文。
// ============================================================================
#include "BlockCompiler.h"

#include "BlockRegistry.h"

#include <QChar>
#include <QMap>
#include <QPair>

#include <algorithm>
#include <cmath>

namespace tcbw {

namespace {

//  Hard recursion cap; a corrupted script must not blow the stack.
//  递归硬上限；损坏的脚本不得撑爆调用栈。
constexpr int kMaxCompileDepth = 200;

QString bilingual(const QString &en, const QString &zh)
{
    return en + QStringLiteral(" / ") + zh;
}

//  JS reserved words that may not be used as a plain identifier.
//  不能作为普通标识符使用的 JS 保留字。
bool isReservedWord(const QString &word)
{
    static const QStringList reserved = {
        QStringLiteral("abstract"), QStringLiteral("arguments"), QStringLiteral("await"),
        QStringLiteral("boolean"), QStringLiteral("break"), QStringLiteral("byte"),
        QStringLiteral("case"), QStringLiteral("catch"), QStringLiteral("char"),
        QStringLiteral("class"), QStringLiteral("const"), QStringLiteral("continue"),
        QStringLiteral("debugger"), QStringLiteral("default"), QStringLiteral("delete"),
        QStringLiteral("do"), QStringLiteral("double"), QStringLiteral("else"),
        QStringLiteral("enum"), QStringLiteral("eval"), QStringLiteral("export"),
        QStringLiteral("extends"), QStringLiteral("false"), QStringLiteral("final"),
        QStringLiteral("finally"), QStringLiteral("float"), QStringLiteral("for"),
        QStringLiteral("function"), QStringLiteral("goto"), QStringLiteral("if"),
        QStringLiteral("implements"), QStringLiteral("import"), QStringLiteral("in"),
        QStringLiteral("instanceof"), QStringLiteral("int"), QStringLiteral("interface"),
        QStringLiteral("let"), QStringLiteral("long"), QStringLiteral("native"),
        QStringLiteral("new"), QStringLiteral("null"), QStringLiteral("package"),
        QStringLiteral("private"), QStringLiteral("protected"), QStringLiteral("public"),
        QStringLiteral("return"), QStringLiteral("short"), QStringLiteral("static"),
        QStringLiteral("super"), QStringLiteral("switch"), QStringLiteral("synchronized"),
        QStringLiteral("this"), QStringLiteral("throw"), QStringLiteral("throws"),
        QStringLiteral("transient"), QStringLiteral("true"), QStringLiteral("try"),
        QStringLiteral("typeof"), QStringLiteral("var"), QStringLiteral("void"),
        QStringLiteral("volatile"), QStringLiteral("while"), QStringLiteral("with"),
        QStringLiteral("yield")
    };
    return reserved.contains(word);
}

//  Strips every character that cannot appear in an ASCII JS identifier.
//  去掉所有不能出现在 ASCII JS 标识符中的字符。
QString sanitiseCore(const QString &value)
{
    QString out;
    out.reserve(value.size());
    for (const QChar ch : value) {
        const ushort code = ch.unicode();
        const bool isAlpha = (code >= 'a' && code <= 'z') || (code >= 'A' && code <= 'Z');
        const bool isDigit = (code >= '0' && code <= '9');
        if (isAlpha || ch == QLatin1Char('_') || ch == QLatin1Char('$')) {
            out.append(ch);
        } else if (isDigit) {
            if (out.isEmpty())
                out.append(QLatin1Char('_'));
            out.append(ch);
        } else if (!out.isEmpty() && !out.endsWith(QLatin1Char('_'))) {
            out.append(QLatin1Char('_'));
        }
    }
    while (out.endsWith(QLatin1Char('_')))
        out.chop(1);
    return out;
}

//  True when the text is a plain JS number literal. Hexadecimal, exponent and
//  decimal forms are accepted; Infinity/NaN are handled as identifiers.
//  当文本是普通 JS 数字字面量时返回 true。接受十进制、指数与十六进制；
//  Infinity/NaN 按标识符处理。
bool looksLikeNumberLiteral(const QString &text, QString *literal)
{
    const QString t = text.trimmed();
    if (t.isEmpty())
        return false;

    bool ok = false;
    const double value = t.toDouble(&ok);
    if (!ok || !std::isfinite(value))
        return false;
    if (literal)
        *literal = t;
    return true;
}

//  Renders a numeric field: a literal when it parses as a number, otherwise a
//  sanitised identifier reference. Raw user text never reaches code position.
//  渲染数值字段：可解析为数字时输出字面量，否则输出净化后的标识符引用。
//  用户原文绝不进入代码位置。
QString numericLiteralOrIdentifier(const QString &raw)
{
    QString literal;
    if (looksLikeNumberLiteral(raw, &literal))
        return literal;
    return BlockCompiler::sanitiseIdentifier(raw, QStringLiteral("value"));
}

//  Mirrors the registry's static value-kind heuristic so the compiler can
//  report type mismatches without instantiating a checker.
//  与注册表的静态值种类启发式保持一致，使编译器无需类型检查器即可报告类型不匹配。
ValueKind blockValueKind(const QString &type, BlockShape shape)
{
    if (shape == BlockShape::Boolean)
        return ValueKind::Boolean;
    if (shape == BlockShape::Round)
        return ValueKind::Number;
    if (type == QLatin1String("text.length") || type == QLatin1String("data.length")
        || type == QLatin1String("variables.listLength"))
        return ValueKind::Number;
    if (type.startsWith(QLatin1String("math.")))
        return ValueKind::Number;
    if (type.startsWith(QLatin1String("text.")))
        return ValueKind::String;
    if (type.startsWith(QLatin1String("logic.")))
        return ValueKind::Boolean;
    return ValueKind::Any;
}

bool kindsCompatible(ValueKind wanted, ValueKind produced)
{
    if (wanted == ValueKind::Any || produced == ValueKind::Any)
        return true;
    if (wanted == produced)
        return true;
    if (wanted == ValueKind::String || produced == ValueKind::String)
        return true;
    if (wanted == ValueKind::Colour)
        return true;
    return false;
}

const BlockInputDef *findInput(const BlockDef &def, const QString &name)
{
    for (const BlockInputDef &socket : def.inputs) {
        if (socket.name == name)
            return &socket;
    }
    return nullptr;
}

//  The JS literal used when a socket has no connected block.
//  槽位没有连接积木时所使用的 JS 字面量。
QString shadowLiteralOf(const BlockInputDef &socket, bool *missing)
{
    if (missing)
        *missing = false;

    if (!socket.shadowValue.isEmpty()) {
        const QString raw = socket.shadowValue;
        switch (socket.kind) {
        case ValueKind::Number: {
            QString literal;
            if (looksLikeNumberLiteral(raw, &literal))
                return literal;
            return BlockCompiler::jsString(raw);
        }
        case ValueKind::Boolean: {
            const QString t = raw.trimmed().toLower();
            if (t == QLatin1String("true") || t == QLatin1String("false"))
                return t;
            return BlockCompiler::jsString(raw);
        }
        case ValueKind::List:
        case ValueKind::Object: {
            const QString t = raw.trimmed();
            //  JSON shaped defaults are already valid JS literals.
            //  JSON 形式的默认值本身就是合法的 JS 字面量。
            if ((t.startsWith(QLatin1Char('[')) && t.endsWith(QLatin1Char(']')))
                || (t.startsWith(QLatin1Char('{')) && t.endsWith(QLatin1Char('}'))))
                return t;
            return BlockCompiler::jsString(raw);
        }
        default:
            return BlockCompiler::jsString(raw);
        }
    }

    if (!socket.shadowType.isEmpty()) {
        //  A declared shadow block with no literal: emit the neutral value of
        //  the socket kind.
        //  声明了影子积木但没有字面量：输出该槽位种类的中性值。
        switch (socket.kind) {
        case ValueKind::Number:  return QStringLiteral("0");
        case ValueKind::Boolean: return QStringLiteral("true");
        case ValueKind::List:    return QStringLiteral("[]");
        case ValueKind::Object:  return QStringLiteral("{}");
        default:                 return QStringLiteral("''");
        }
    }

    if (missing)
        *missing = true;
    return QStringLiteral("undefined");
}

//  Indents a block of already indented lines by `levels` more levels.
//  把一段已有缩进的行再整体多缩进 `levels` 级。
QStringList shiftLines(const QStringList &lines, int levels, int width)
{
    if (levels <= 0)
        return lines;
    const QString pad(levels * qMax(0, width), QLatin1Char(' '));
    QStringList out;
    out.reserve(lines.size());
    for (const QString &line : lines)
        out.append(line.isEmpty() ? line : pad + line);
    return out;
}

} // namespace

// ===========================================================================
//  Impl -- all mutable compiler state
//  Impl —— 编译器的全部可变状态
// ===========================================================================
struct BlockCompiler::Impl
{
    explicit Impl(const CompileOptions &options)
        : opt(options)
    {
    }

    CompileOptions opt;

    QStringList errors;
    QStringList warnings;
    mutable QStringList requiredApis;
    mutable QSet<QString> usedVariables;
    mutable QMap<QString, QString> variableNames; // alias -> original / 别名 -> 原名

    int statementCount = 0;
    int scriptCount = 0;
    int loopDepth = 0;
    int uniqueSeq = 0;
    int depth = 0;

    void reset()
    {
        errors.clear();
        warnings.clear();
        requiredApis.clear();
        usedVariables.clear();
        variableNames.clear();
        statementCount = 0;
        scriptCount = 0;
        loopDepth = 0;
        uniqueSeq = 0;
        depth = 0;
    }

    //  Effective indentation width; `pretty == false` flattens the output.
    //  有效缩进宽度；`pretty == false` 时输出不缩进。
    int width() const
    {
        return opt.pretty ? qMax(0, opt.indentWidth) : 0;
    }

    QString ind(int level) const
    {
        return QString(qMax(0, level) * width(), QLatin1Char(' '));
    }

    void requireApi(const QString &api) const
    {
        if (!api.isEmpty() && !requiredApis.contains(api))
            requiredApis.append(api);
    }

    void noteVariable(const QString &rawName) const
    {
        const QString alias = BlockCompiler::sanitiseIdentifier(rawName, QStringLiteral("value"));
        usedVariables.insert(alias);
        variableNames.insert(alias, rawName);
    }

    void pushLine(QStringList *out, int indent, const QString &text, bool statement = true)
    {
        out->append(ind(indent) + text);
        if (statement)
            ++statementCount;
    }

    // -- expressions -------------------------------------------------------
    QString inputExpr(const Block &parent, const BlockDef &def, const BlockInputDef &socket,
                      QStringList *errs) const;
    QString expr(const Block &block, QStringList *errs) const;

    // -- statements --------------------------------------------------------
    //  NB: `emit` is a Qt keyword macro, hence the name `emitStatement`.
    //  注意：`emit` 是 Qt 的宏，因此命名为 `emitStatement`。
    void emitStatement(const Block &block, int indent, QStringList *out);
    void emitBody(const QVector<Block *> &blocks, int indent, QStringList *out);
    QString emitCShape(const Block &block, const BlockDef &def, int indent, QStringList *out);
    QStringList scriptSnippet(const BlockScript &script);

    //  Target selector for DOM helpers: block field, then the compile options,
    //  then `document`.
    //  DOM 辅助函数的目标选择器：积木字段、编译选项，最后回退到 `document`。
    QString targetOf(const Block &block, const QString &fieldName = QStringLiteral("selector")) const
    {
        QString selector = block.fieldValue(fieldName).trimmed();
        if (selector.isEmpty())
            selector = opt.targetElementSelector.trimmed();
        if (selector.isEmpty() || selector == QLatin1String("document"))
            return QStringLiteral("document");
        if (selector == QLatin1String("window"))
            return QStringLiteral("window");
        if (selector == QLatin1String("body"))
            return QStringLiteral("document.body");
        return BlockCompiler::jsString(selector);
    }
};

// ---------------------------------------------------------------------------
//  Expression compilation
//  表达式编译
// ---------------------------------------------------------------------------
QString BlockCompiler::Impl::inputExpr(const Block &parent, const BlockDef &def,
                                       const BlockInputDef &socket, QStringList *errs) const
{
    const QVector<Block *> &wired = parent.inputBlocks(socket.name);
    for (const Block *child : wired) {
        if (!child)
            continue;

        const BlockDef *childDef = BlockRegistry::instance().find(child->type);
        if (childDef && childDef->returnsValue()) {
            const ValueKind produced = blockValueKind(childDef->type, childDef->shape);
            if (!kindsCompatible(socket.kind, produced)) {
                errs->append(bilingual(
                    QStringLiteral("Input '%1' of '%2' expects a %3 value but '%4' produces %5.")
                        .arg(socket.name, def.type, toString(socket.kind), childDef->type,
                             toString(produced)),
                    QStringLiteral("积木“%2”的输入槽“%1”需要 %3 类型的值，而“%4”产出 %5。")
                        .arg(socket.name, def.type, toString(socket.kind), childDef->type,
                             toString(produced))));
            }
        }
        return expr(*child, errs);
    }

    bool missing = false;
    const QString literal = shadowLiteralOf(socket, &missing);
    if (missing) {
        errs->append(bilingual(
            QStringLiteral("Input '%1' of '%2' is empty and has no default value.")
                .arg(socket.name, def.type),
            QStringLiteral("积木“%2”的输入槽“%1”为空且没有默认值。").arg(socket.name, def.type)));
    }
    return literal;
}

QString BlockCompiler::Impl::expr(const Block &block, QStringList *errs) const
{
    const BlockRegistry &registry = BlockRegistry::instance();
    const BlockDef *def = registry.find(block.type);

    if (!def) {
        errs->append(bilingual(
            QStringLiteral("Unknown block type '%1'.").arg(block.type),
            QStringLiteral("未知的积木类型“%1”。").arg(block.type)));
        return QStringLiteral("undefined");
    }
    if (!def->returnsValue()) {
        errs->append(bilingual(
            QStringLiteral("Block '%1' does not return a value and cannot be used here.")
                .arg(block.type),
            QStringLiteral("积木“%1”没有返回值，不能在此使用。").arg(block.type)));
        return QStringLiteral("undefined");
    }
    if (depth > kMaxCompileDepth) {
        errs->append(bilingual(QStringLiteral("Expression nesting too deep."),
                               QStringLiteral("表达式嵌套过深。")));
        return QStringLiteral("undefined");
    }

    const QString type = block.type;
    const auto A = [&](const QString &name) -> QString {
        const BlockInputDef *socket = findInput(*def, name);
        if (!socket) {
            errs->append(bilingual(
                QStringLiteral("Block '%1' has no input named '%2'.").arg(type, name),
                QStringLiteral("积木“%1”没有名为“%2”的输入槽。").arg(type, name)));
            return QStringLiteral("undefined");
        }
        return inputExpr(block, *def, *socket, errs);
    };
    const auto F = [&](const QString &name) { return block.fieldValue(name).trimmed(); };
    const auto S = [&](const QString &name) { return BlockCompiler::jsString(F(name)); };

    // --- text -------------------------------------------------------------
    // --- 文本 -------------------------------------------------------------
    if (type == QLatin1String("text.value"))
        return BlockCompiler::jsString(block.fieldValue(QStringLiteral("value")));
    if (type == QLatin1String("text.join"))
        return QStringLiteral("(String(%1) + String(%2))").arg(A(QStringLiteral("a")),
                                                              A(QStringLiteral("b")));
    if (type == QLatin1String("text.length"))
        return QStringLiteral("String(%1).length").arg(A(QStringLiteral("text")));
    if (type == QLatin1String("text.upper"))
        return QStringLiteral("String(%1).toUpperCase()").arg(A(QStringLiteral("text")));
    if (type == QLatin1String("text.lower"))
        return QStringLiteral("String(%1).toLowerCase()").arg(A(QStringLiteral("text")));
    if (type == QLatin1String("text.trim"))
        return QStringLiteral("String(%1).trim()").arg(A(QStringLiteral("text")));
    if (type == QLatin1String("text.replace"))
        return QStringLiteral("String(%1).split(String(%2)).join(String(%3))")
            .arg(A(QStringLiteral("text")), A(QStringLiteral("search")),
                 A(QStringLiteral("replacement")));
    if (type == QLatin1String("text.split"))
        return QStringLiteral("String(%1).split(String(%2))")
            .arg(A(QStringLiteral("text")), A(QStringLiteral("separator")));
    if (type == QLatin1String("text.contains"))
        return QStringLiteral("(String(%1).indexOf(String(%2)) !== -1)")
            .arg(A(QStringLiteral("text")), A(QStringLiteral("needle")));
    if (type == QLatin1String("text.substring"))
        return QStringLiteral("String(%3).substring(Math.max(0, (%1) - 1), (%2))")
            .arg(A(QStringLiteral("from")), A(QStringLiteral("to")),
                 A(QStringLiteral("text")));
    if (type == QLatin1String("text.template")) {
        requireApi(QStringLiteral("TCBW.text"));
        return QStringLiteral("TCBW.text.template(%1, %2)")
            .arg(S(QStringLiteral("template")), A(QStringLiteral("values")));
    }

    // --- logic ------------------------------------------------------------
    // --- 逻辑 ------------------------------------------------------------
    if (type == QLatin1String("logic.true") || type == QLatin1String("logic.false"))
        return F(QStringLiteral("value")) == QLatin1String("false")
                   ? QStringLiteral("false") : QStringLiteral("true");
    if (type == QLatin1String("logic.and"))
        return QStringLiteral("((%1) && (%2))").arg(A(QStringLiteral("a")), A(QStringLiteral("b")));
    if (type == QLatin1String("logic.or"))
        return QStringLiteral("((%1) || (%2))").arg(A(QStringLiteral("a")), A(QStringLiteral("b")));
    if (type == QLatin1String("logic.not"))
        return QStringLiteral("!(%1)").arg(A(QStringLiteral("value")));
    if (type == QLatin1String("logic.equals"))
        return QStringLiteral("TCBW.eq(%1, %2)").arg(A(QStringLiteral("a")), A(QStringLiteral("b")));
    if (type == QLatin1String("logic.notEquals"))
        return QStringLiteral("!TCBW.eq(%1, %2)").arg(A(QStringLiteral("a")), A(QStringLiteral("b")));
    if (type == QLatin1String("logic.greater"))
        return QStringLiteral("((%1) > (%2))").arg(A(QStringLiteral("a")), A(QStringLiteral("b")));
    if (type == QLatin1String("logic.less"))
        return QStringLiteral("((%1) < (%2))").arg(A(QStringLiteral("a")), A(QStringLiteral("b")));
    if (type == QLatin1String("logic.greaterOrEqual"))
        return QStringLiteral("((%1) >= (%2))").arg(A(QStringLiteral("a")), A(QStringLiteral("b")));
    if (type == QLatin1String("logic.lessOrEqual"))
        return QStringLiteral("((%1) <= (%2))").arg(A(QStringLiteral("a")), A(QStringLiteral("b")));
    if (type == QLatin1String("logic.isNull"))
        return QStringLiteral("TCBW.isEmpty(%1)").arg(A(QStringLiteral("value")));
    if (type == QLatin1String("logic.between"))
        return QStringLiteral("TCBW.between(%1, %2, %3)")
            .arg(A(QStringLiteral("value")), A(QStringLiteral("min")), A(QStringLiteral("max")));

    // --- math -------------------------------------------------------------
    // --- 数学 -------------------------------------------------------------
    if (type == QLatin1String("math.number"))
        return numericLiteralOrIdentifier(block.fieldValue(QStringLiteral("value")));
    if (type == QLatin1String("math.arithmetic")) {
        const QString op = F(QStringLiteral("op"));
        const QString a = A(QStringLiteral("a"));
        const QString b = A(QStringLiteral("b"));
        if (op == QLatin1String("+") || op == QLatin1String("-") || op == QLatin1String("*")
            || op == QLatin1String("/") || op == QLatin1String("%"))
            return QStringLiteral("((%1) %2 (%3))").arg(a, op, b);
        if (op == QLatin1String("^"))
            return QStringLiteral("Math.pow(%1, %2)").arg(a, b);
        errs->append(bilingual(
            QStringLiteral("Unknown arithmetic operator '%1'.").arg(op),
            QStringLiteral("未知的算术运算符“%1”。").arg(op)));
        return QStringLiteral("((%1) + (%2))").arg(a, b);
    }
    if (type == QLatin1String("math.random")) {
        const QString to = A(QStringLiteral("to"));
        const QString from = A(QStringLiteral("from"));
        return QStringLiteral("(Math.floor(Math.random() * ((%1) - (%2) + 1)) + (%2))")
            .arg(to, from);
    }
    if (type == QLatin1String("math.round"))
        return QStringLiteral("Math.round(%1)").arg(A(QStringLiteral("value")));
    if (type == QLatin1String("math.floor"))
        return QStringLiteral("Math.floor(%1)").arg(A(QStringLiteral("value")));
    if (type == QLatin1String("math.ceil"))
        return QStringLiteral("Math.ceil(%1)").arg(A(QStringLiteral("value")));
    if (type == QLatin1String("math.abs"))
        return QStringLiteral("Math.abs(%1)").arg(A(QStringLiteral("value")));
    if (type == QLatin1String("math.min"))
        return QStringLiteral("Math.min(%1, %2)").arg(A(QStringLiteral("a")), A(QStringLiteral("b")));
    if (type == QLatin1String("math.max"))
        return QStringLiteral("Math.max(%1, %2)").arg(A(QStringLiteral("a")), A(QStringLiteral("b")));
    if (type == QLatin1String("math.sqrt"))
        return QStringLiteral("Math.sqrt(%1)").arg(A(QStringLiteral("value")));
    if (type == QLatin1String("math.pow"))
        return QStringLiteral("Math.pow(%1, %2)")
            .arg(A(QStringLiteral("base")), A(QStringLiteral("exponent")));
    if (type == QLatin1String("math.mod"))
        return QStringLiteral("((%1) %% (%2))").arg(A(QStringLiteral("a")), A(QStringLiteral("b")));
    if (type == QLatin1String("math.clamp"))
        return QStringLiteral("Math.min(Math.max(%1, %2), %3)")
            .arg(A(QStringLiteral("value")), A(QStringLiteral("min")), A(QStringLiteral("max")));
    if (type == QLatin1String("math.trig")) {
        const QString fn = F(QStringLiteral("fn"));
        QString js;
        if (fn == QLatin1String("sin"))       js = QStringLiteral("Math.sin");
        else if (fn == QLatin1String("cos"))  js = QStringLiteral("Math.cos");
        else if (fn == QLatin1String("tan"))  js = QStringLiteral("Math.tan");
        else if (fn == QLatin1String("asin")) js = QStringLiteral("Math.asin");
        else if (fn == QLatin1String("acos")) js = QStringLiteral("Math.acos");
        else if (fn == QLatin1String("atan")) js = QStringLiteral("Math.atan");
        else if (fn == QLatin1String("ln"))   js = QStringLiteral("Math.log");
        else if (fn == QLatin1String("log"))  js = QStringLiteral("Math.log10");
        else if (fn == QLatin1String("exp"))  js = QStringLiteral("Math.exp");
        else {
            errs->append(bilingual(
                QStringLiteral("Unknown math function '%1'.").arg(fn),
                QStringLiteral("未知的数学函数“%1”。").arg(fn)));
            js = QStringLiteral("Math.sin");
        }
        return QStringLiteral("%1(%2)").arg(js, A(QStringLiteral("value")));
    }

    // --- variables --------------------------------------------------------
    // --- 变量 -------------------------------------------------------------
    if (type == QLatin1String("variables.get")) {
        const QString name = block.fieldValue(QStringLiteral("name"));
        noteVariable(name);
        return BlockCompiler::sanitiseIdentifier(name, QStringLiteral("value"));
    }
    if (type == QLatin1String("variables.listGet")) {
        requireApi(QStringLiteral("TCBW.lists"));
        return QStringLiteral("TCBW.lists.get(%1, %2)")
            .arg(S(QStringLiteral("name")), A(QStringLiteral("index")));
    }
    if (type == QLatin1String("variables.listLength")) {
        requireApi(QStringLiteral("TCBW.lists"));
        return QStringLiteral("TCBW.lists.length(%1)").arg(S(QStringLiteral("name")));
    }

    // --- storage ----------------------------------------------------------
    // --- 存储 -------------------------------------------------------------
    if (type == QLatin1String("storage.get")) {
        requireApi(QStringLiteral("TCBW.store"));
        requireApi(QStringLiteral("localStorage"));
        return QStringLiteral("TCBW.store.get(%1)").arg(S(QStringLiteral("key")));
    }
    if (type == QLatin1String("storage.sessionGet")) {
        requireApi(QStringLiteral("TCBW.store"));
        requireApi(QStringLiteral("sessionStorage"));
        return QStringLiteral("TCBW.store.sessionGet(%1)").arg(S(QStringLiteral("key")));
    }
    if (type == QLatin1String("storage.cookieGet")) {
        requireApi(QStringLiteral("TCBW.store"));
        requireApi(QStringLiteral("document.cookie"));
        return QStringLiteral("TCBW.store.cookieGet(%1)").arg(S(QStringLiteral("key")));
    }

    // --- http -------------------------------------------------------------
    // --- 网络请求 ---------------------------------------------------------
    if (type.startsWith(QLatin1String("http."))) {
        requireApi(QStringLiteral("TCBW.http"));
        requireApi(QStringLiteral("fetch"));
        if (type == QLatin1String("http.get"))
            return QStringLiteral("TCBW.http.get(%1, { headers: TCBW.http.headers() })")
                .arg(A(QStringLiteral("url")));
        if (type == QLatin1String("http.post"))
            return QStringLiteral("TCBW.http.post(%1, %2, { headers: TCBW.http.headers() })")
                .arg(A(QStringLiteral("url")), A(QStringLiteral("body")));
        if (type == QLatin1String("http.put"))
            return QStringLiteral("TCBW.http.put(%1, %2, { headers: TCBW.http.headers() })")
                .arg(A(QStringLiteral("url")), A(QStringLiteral("body")));
        if (type == QLatin1String("http.patch"))
            return QStringLiteral("TCBW.http.patch(%1, %2, { headers: TCBW.http.headers() })")
                .arg(A(QStringLiteral("url")), A(QStringLiteral("body")));
        if (type == QLatin1String("http.delete"))
            return QStringLiteral("TCBW.http.delete(%1, { headers: TCBW.http.headers() })")
                .arg(A(QStringLiteral("url")));
        if (type == QLatin1String("http.request"))
            return QStringLiteral("TCBW.http.request(%1, %2, %3)")
                .arg(S(QStringLiteral("method")), A(QStringLiteral("url")),
                     A(QStringLiteral("body")));
        if (type == QLatin1String("http.parseJson")) {
            requireApi(QStringLiteral("TCBW.data"));
            return QStringLiteral("TCBW.data.parse(%1)").arg(A(QStringLiteral("text")));
        }
    }

    // --- backend ----------------------------------------------------------
    // --- 后端 -------------------------------------------------------------
    if (type.startsWith(QLatin1String("backend."))) {
        requireApi(QStringLiteral("TCBW.backend"));
        if (type == QLatin1String("backend.callEndpoint"))
            return QStringLiteral("TCBW.backend.call(%1, %2)")
                .arg(S(QStringLiteral("endpoint")), A(QStringLiteral("variables")));
        if (type == QLatin1String("backend.callGraphQl"))
            return QStringLiteral("TCBW.backend.graphql(%1, %2)")
                .arg(A(QStringLiteral("query")), A(QStringLiteral("variables")));
        if (type == QLatin1String("backend.uploadFile"))
            return QStringLiteral("TCBW.backend.upload(%1, %2)")
                .arg(A(QStringLiteral("file")), A(QStringLiteral("url")));
        if (type == QLatin1String("backend.authLogin"))
            return QStringLiteral("TCBW.backend.login(%1, %2)")
                .arg(A(QStringLiteral("email")), A(QStringLiteral("password")));
        if (type == QLatin1String("backend.getUser"))
            return QStringLiteral("TCBW.backend.user()");
    }

    // --- data -------------------------------------------------------------
    // --- 数据 -------------------------------------------------------------
    if (type.startsWith(QLatin1String("data."))) {
        requireApi(QStringLiteral("TCBW.data"));
        if (type == QLatin1String("data.parseJson"))
            return QStringLiteral("TCBW.data.parse(%1)").arg(A(QStringLiteral("text")));
        if (type == QLatin1String("data.stringify")) {
            requireApi(QStringLiteral("JSON"));
            return QStringLiteral("JSON.stringify(%1)").arg(A(QStringLiteral("value")));
        }
        if (type == QLatin1String("data.getField"))
            return QStringLiteral("TCBW.data.get(%1, %2)")
                .arg(A(QStringLiteral("object")), S(QStringLiteral("path")));
        if (type == QLatin1String("data.filterList"))
            return QStringLiteral("TCBW.data.filter(%1, function (item) { return %2; })")
                .arg(A(QStringLiteral("list")), A(QStringLiteral("predicate")));
        if (type == QLatin1String("data.mapList"))
            return QStringLiteral("TCBW.data.map(%1, function (item) { return %2; })")
                .arg(A(QStringLiteral("list")), A(QStringLiteral("expression")));
        if (type == QLatin1String("data.sortList"))
            return QStringLiteral("TCBW.data.sort(%1, %2)")
                .arg(A(QStringLiteral("list")), S(QStringLiteral("direction")));
        if (type == QLatin1String("data.length"))
            return QStringLiteral("TCBW.data.length(%1)").arg(A(QStringLiteral("list")));
    }

    // --- ai ---------------------------------------------------------------
    // --- 人工智能 ---------------------------------------------------------
    if (type.startsWith(QLatin1String("ai."))) {
        requireApi(QStringLiteral("TCBW.ai"));
        const QString model = F(QStringLiteral("model"));
        const QString target = block.fieldValue(QStringLiteral("target")).trimmed();
        const QString targetExpr = target.isEmpty()
                                       ? (opt.targetElementSelector.isEmpty()
                                              ? QStringLiteral("'body'")
                                              : BlockCompiler::jsString(opt.targetElementSelector))
                                       : BlockCompiler::jsString(target);

        if (type == QLatin1String("ai.ask") || type == QLatin1String("ai.askWithContext")) {
            QStringList parts;
            parts << QStringLiteral("prompt: ") + A(QStringLiteral("prompt"));
            if (type == QLatin1String("ai.askWithContext"))
                parts << QStringLiteral("context: ") + A(QStringLiteral("context"));
            parts << QStringLiteral("target: ") + targetExpr;
            if (!model.isEmpty())
                parts << QStringLiteral("model: ") + BlockCompiler::jsString(model);
            return QStringLiteral("TCBW.ai.ask({ %1 })").arg(parts.join(QStringLiteral(", ")));
        }
        if (type == QLatin1String("ai.summarise"))
            return QStringLiteral("TCBW.ai.summarise(%1, %2)")
                .arg(A(QStringLiteral("text")), S(QStringLiteral("length")));
        if (type == QLatin1String("ai.translate"))
            return QStringLiteral("TCBW.ai.translate(%1, %2)")
                .arg(A(QStringLiteral("text")), S(QStringLiteral("language")));
        if (type == QLatin1String("ai.generateText")) {
            QStringList parts;
            parts << QStringLiteral("prompt: ") + A(QStringLiteral("prompt"));
            if (!model.isEmpty())
                parts << QStringLiteral("model: ") + BlockCompiler::jsString(model);
            return QStringLiteral("TCBW.ai.generateText({ %1 })")
                .arg(parts.join(QStringLiteral(", ")));
        }
        if (type == QLatin1String("ai.generateImage")) {
            QStringList parts;
            parts << QStringLiteral("prompt: ") + A(QStringLiteral("prompt"));
            parts << QStringLiteral("size: ") + S(QStringLiteral("size"));
            if (!model.isEmpty())
                parts << QStringLiteral("model: ") + BlockCompiler::jsString(model);
            return QStringLiteral("TCBW.ai.generateImage({ %1 })")
                .arg(parts.join(QStringLiteral(", ")));
        }
        if (type == QLatin1String("ai.classify"))
            return QStringLiteral("TCBW.ai.classify(%1, %2)")
                .arg(A(QStringLiteral("text")), S(QStringLiteral("labels")));
    }

    // --- mcp --------------------------------------------------------------
    // --- MCP --------------------------------------------------------------
    if (type.startsWith(QLatin1String("mcp."))) {
        requireApi(QStringLiteral("TCBW.mcp"));
        if (type == QLatin1String("mcp.callTool"))
            return QStringLiteral("TCBW.mcp.callTool(%1, %2, %3)")
                .arg(S(QStringLiteral("server")), S(QStringLiteral("tool")),
                     A(QStringLiteral("arguments")));
        if (type == QLatin1String("mcp.listTools"))
            return QStringLiteral("TCBW.mcp.listTools(%1)").arg(S(QStringLiteral("server")));
        if (type == QLatin1String("mcp.readResource"))
            return QStringLiteral("TCBW.mcp.readResource(%1, %2)")
                .arg(S(QStringLiteral("server")), A(QStringLiteral("uri")));
    }

    // --- timing -----------------------------------------------------------
    // --- 定时 -------------------------------------------------------------
    if (type == QLatin1String("timing.dateNow")) {
        requireApi(QStringLiteral("Date"));
        return QStringLiteral("new Date()");
    }
    if (type == QLatin1String("timing.formatDate")) {
        requireApi(QStringLiteral("TCBW.timing"));
        return QStringLiteral("TCBW.timing.format(%1, %2)")
            .arg(A(QStringLiteral("date")), A(QStringLiteral("format")));
    }

    errs->append(bilingual(
        QStringLiteral("No expression generator for block '%1'.").arg(type),
        QStringLiteral("积木“%1”没有表达式生成器。").arg(type)));
    return QStringLiteral("undefined");
}

// ---------------------------------------------------------------------------
//  Statement compilation
//  语句编译
// ---------------------------------------------------------------------------
QString BlockCompiler::Impl::emitCShape(const Block &block, const BlockDef &def, int indent,
                                        QStringList *out)
{
    const QString type = block.type;
    const auto branch = [&](const QString &name) { return block.statementBlocks(name); };

    if (type == QLatin1String("control.if") || type == QLatin1String("control.ifElse")) {
        const BlockInputDef *socket = findInput(def, QStringLiteral("condition"));
        const QString condition = socket
                                      ? inputExpr(block, def, *socket, &errors)
                                      : QStringLiteral("false");
        pushLine(out, indent, QStringLiteral("if (%1) {").arg(condition));
        emitBody(branch(QStringLiteral("then")), indent + 1, out);
        if (type == QLatin1String("control.ifElse")) {
            pushLine(out, indent, QStringLiteral("} else {"), false);
            emitBody(branch(QStringLiteral("else")), indent + 1, out);
        }
        pushLine(out, indent, QStringLiteral("}"), false);
        return QStringLiteral("if");
    }

    if (type == QLatin1String("control.repeat")) {
        //  A fresh loop variable per nesting level keeps loops re-entrant.
        //  每层嵌套使用新的循环变量，保证循环可重入。
        const QString loopVar = QStringLiteral("i") + QString::number(++uniqueSeq);
        const BlockInputDef *socket = findInput(def, QStringLiteral("times"));
        const QString times = socket ? inputExpr(block, def, *socket, &errors)
                                     : QStringLiteral("0");
        pushLine(out, indent,
                 QStringLiteral("for (var %1 = 0; %1 < (%2); %1 += 1) {").arg(loopVar, times));
        ++loopDepth;
        emitBody(branch(QStringLiteral("body")), indent + 1, out);
        --loopDepth;
        pushLine(out, indent, QStringLiteral("}"), false);
        return QStringLiteral("for");
    }

    if (type == QLatin1String("control.repeatWhile")) {
        const BlockInputDef *socket = findInput(def, QStringLiteral("condition"));
        const QString condition = socket
                                      ? inputExpr(block, def, *socket, &errors)
                                      : QStringLiteral("false");
        pushLine(out, indent, QStringLiteral("while (%1) {").arg(condition));
        ++loopDepth;
        emitBody(branch(QStringLiteral("body")), indent + 1, out);
        --loopDepth;
        pushLine(out, indent, QStringLiteral("}"), false);
        return QStringLiteral("while");
    }

    if (type == QLatin1String("control.forEach")) {
        const QString loopVar =
            BlockCompiler::sanitiseIdentifier(block.fieldValue(QStringLiteral("variable")),
                                              QStringLiteral("item"));
        const BlockInputDef *socket = findInput(def, QStringLiteral("list"));
        const QString list = socket ? inputExpr(block, def, *socket, &errors)
                                    : QStringLiteral("[]");
        pushLine(out, indent, QStringLiteral("for (var %1 of %2) {").arg(loopVar, list));
        ++loopDepth;
        emitBody(branch(QStringLiteral("body")), indent + 1, out);
        --loopDepth;
        pushLine(out, indent, QStringLiteral("}"), false);
        return QStringLiteral("for-of");
    }

    if (type == QLatin1String("timing.after") || type == QLatin1String("timing.every")) {
        const BlockInputDef *socket = findInput(def, QStringLiteral("seconds"));
        const QString seconds = socket ? inputExpr(block, def, *socket, &errors)
                                       : QStringLiteral("1");
        const bool once = (type == QLatin1String("timing.after"));
        requireApi(once ? QStringLiteral("setTimeout") : QStringLiteral("setInterval"));
        pushLine(out, indent, (once ? QStringLiteral("setTimeout(function () {")
                                    : QStringLiteral("setInterval(function () {")));
        emitBody(branch(QStringLiteral("body")), indent + 1, out);
        pushLine(out, indent, QStringLiteral("}, (%1) * 1000);").arg(seconds));
        return once ? QStringLiteral("setTimeout") : QStringLiteral("setInterval");
    }

    //  Unknown C/E shaped block: keep the children, flag the gap.
    //  未知的 C/E 形积木：保留子语句并报告缺口。
    warnings.append(bilingual(
        QStringLiteral("No control-flow generator for block '%1'; its body is emitted inline.")
            .arg(type),
        QStringLiteral("积木“%1”没有控制流生成器；其主体将内联输出。").arg(type)));
    for (const BlockStatementDef &slot : def.statements)
        emitBody(branch(slot.name), indent, out);
    return QString();
}

void BlockCompiler::Impl::emitBody(const QVector<Block *> &blocks, int indent, QStringList *out)
{
    for (int i = 0; i < blocks.size(); ++i) {
        const Block *block = blocks.at(i);
        if (!block)
            continue;

        const BlockDef *def = BlockRegistry::instance().find(block->type);
        if (!def) {
            errors.append(bilingual(
                QStringLiteral("Unknown block type '%1'.").arg(block->type),
                QStringLiteral("未知的积木类型“%1”。").arg(block->type)));
            continue;
        }
        if (def->shape == BlockShape::Hat) {
            warnings.append(bilingual(
                QStringLiteral("Block '%1' is a hat and starts its own script.")
                    .arg(block->type),
                QStringLiteral("积木“%1”是帽子块，应作为独立脚本的起点。").arg(block->type)));
            continue;
        }

        //  `wait` splits the chain: everything below runs in a later tick.
        //  `wait` 会切断语句链：其后的语句在稍后的时间点执行。
        if (block->type == QLatin1String("control.wait")) {
            const BlockInputDef *socket = findInput(*def, QStringLiteral("seconds"));
            const QString seconds = socket ? inputExpr(*block, *def, *socket, &errors)
                                           : QStringLiteral("1");
            requireApi(QStringLiteral("setTimeout"));
            pushLine(out, indent, QStringLiteral("setTimeout(function () {"));
            emitBody(blocks.mid(i + 1), indent + 1, out);
            pushLine(out, indent, QStringLiteral("}, (%1) * 1000);").arg(seconds));
            return;
        }

        emitStatement(*block, indent, out);

        if (def->terminal)
            return;
    }
}

void BlockCompiler::Impl::emitStatement(const Block &block, int indent, QStringList *out)
{
    const BlockRegistry &registry = BlockRegistry::instance();
    const BlockDef *def = registry.find(block.type);
    if (!def) {
        errors.append(bilingual(
            QStringLiteral("Unknown block type '%1'.").arg(block.type),
            QStringLiteral("未知的积木类型“%1”。").arg(block.type)));
        return;
    }

    const QString type = block.type;

    //  A reporter/boolean block in statement position is a structural error.
    //  返回值或布尔积木出现在语句位置属于结构性错误。
    if (def->returnsValue()) {
        errors.append(bilingual(
            QStringLiteral("Block '%1' returns a value and cannot be used as a statement.")
                .arg(type),
            QStringLiteral("积木“%1”有返回值，不能作为语句使用。").arg(type)));
        return;
    }

    const auto socketExpr = [&](const QString &name) -> QString {
        const BlockInputDef *socket = findInput(*def, name);
        if (!socket)
            return QStringLiteral("undefined");
        return inputExpr(block, *def, *socket, &errors);
    };
    const auto fieldOf = [&](const QString &name) { return block.fieldValue(name).trimmed(); };
    const auto fieldStr = [&](const QString &name) { return BlockCompiler::jsString(fieldOf(name)); };

    // --- C / E shaped control flow ----------------------------------------
    // --- C / E 形控制流 ---------------------------------------------------
    if (type == QLatin1String("http.onSuccess") || type == QLatin1String("http.onError")) {
        requireApi(QStringLiteral("TCBW.http"));
        requireApi(QStringLiteral("fetch"));
        const bool success = (type == QLatin1String("http.onSuccess"));
        pushLine(out, indent,
                 QStringLiteral("Promise.resolve(%1).%2(function (%3) {")
                     .arg(socketExpr(QStringLiteral("request")),
                          success ? QStringLiteral("then") : QStringLiteral("catch"),
                          success ? QStringLiteral("result") : QStringLiteral("error")));
        emitBody(block.statementBlocks(QStringLiteral("body")), indent + 1, out);
        pushLine(out, indent, QStringLiteral("});"));
        return;
    }

    if (type == QLatin1String("backend.subscribe")) {
        requireApi(QStringLiteral("TCBW.backend"));
        pushLine(out, indent,
                 QStringLiteral("TCBW.backend.subscribe(%1, function (payload) {")
                     .arg(socketExpr(QStringLiteral("channel"))));
        emitBody(block.statementBlocks(QStringLiteral("body")), indent + 1, out);
        pushLine(out, indent, QStringLiteral("});"));
        return;
    }

    if (def->shape == BlockShape::CShape || def->shape == BlockShape::EShape) {
        emitCShape(block, *def, indent, out);
        return;
    }

    // --- interface --------------------------------------------------------
    // --- 界面 -------------------------------------------------------------
    if (type.startsWith(QLatin1String("ui."))) {
        requireApi(QStringLiteral("TCBW.dom"));
        if (type == QLatin1String("ui.show")) {
            pushLine(out, indent, QStringLiteral("TCBW.dom.show(%1);").arg(targetOf(block)));
        } else if (type == QLatin1String("ui.hide")) {
            pushLine(out, indent, QStringLiteral("TCBW.dom.hide(%1);").arg(targetOf(block)));
        } else if (type == QLatin1String("ui.toggle")) {
            pushLine(out, indent, QStringLiteral("TCBW.dom.toggle(%1);").arg(targetOf(block)));
        } else if (type == QLatin1String("ui.setText")) {
            pushLine(out, indent, QStringLiteral("TCBW.dom.setText(%1, %2);")
                                      .arg(targetOf(block), socketExpr(QStringLiteral("value"))));
        } else if (type == QLatin1String("ui.setStyle")) {
            pushLine(out, indent, QStringLiteral("TCBW.dom.setStyle(%1, %2, %3);")
                                      .arg(targetOf(block), fieldStr(QStringLiteral("property")),
                                           socketExpr(QStringLiteral("value"))));
        } else if (type == QLatin1String("ui.setBackground")) {
            pushLine(out, indent,
                     QStringLiteral("TCBW.dom.setStyle(%1, 'background-color', %2);")
                         .arg(targetOf(block), fieldStr(QStringLiteral("colour"))));
        } else if (type == QLatin1String("ui.addClass")) {
            pushLine(out, indent, QStringLiteral("TCBW.dom.addClass(%1, %2);")
                                      .arg(targetOf(block), fieldStr(QStringLiteral("className"))));
        } else if (type == QLatin1String("ui.removeClass")) {
            pushLine(out, indent,
                     QStringLiteral("TCBW.dom.removeClass(%1, %2);")
                         .arg(targetOf(block), fieldStr(QStringLiteral("className"))));
        } else if (type == QLatin1String("ui.setAttribute")) {
            pushLine(out, indent, QStringLiteral("TCBW.dom.setAttribute(%1, %2, %3);")
                                      .arg(targetOf(block), fieldStr(QStringLiteral("name")),
                                           socketExpr(QStringLiteral("value"))));
        } else if (type == QLatin1String("ui.scrollTo")) {
            pushLine(out, indent, QStringLiteral("TCBW.dom.scrollTo(%1, %2);")
                                      .arg(targetOf(block),
                                           socketExpr(QStringLiteral("position"))));
        } else if (type == QLatin1String("ui.focus")) {
            pushLine(out, indent, QStringLiteral("TCBW.dom.focus(%1);").arg(targetOf(block)));
        } else if (type == QLatin1String("ui.animate")) {
            pushLine(out, indent, QStringLiteral("TCBW.dom.animate(%1, %2, %3);")
                                      .arg(targetOf(block),
                                           fieldStr(QStringLiteral("animation")),
                                           socketExpr(QStringLiteral("duration"))));
        } else if (type == QLatin1String("ui.createElement")) {
            const QString alias = BlockCompiler::sanitiseIdentifier(
                fieldOf(QStringLiteral("variable")), QStringLiteral("element"));
            noteVariable(fieldOf(QStringLiteral("variable")));
            pushLine(out, indent, QStringLiteral("var %1 = TCBW.dom.create(%2);")
                                      .arg(alias, fieldStr(QStringLiteral("tag"))));
        } else if (type == QLatin1String("ui.appendTo")) {
            pushLine(out, indent, QStringLiteral("TCBW.dom.append(%1, %2);")
                                      .arg(fieldStr(QStringLiteral("source")), targetOf(block)));
        } else if (type == QLatin1String("ui.remove")) {
            pushLine(out, indent, QStringLiteral("TCBW.dom.remove(%1);").arg(targetOf(block)));
        } else if (type == QLatin1String("ui.setImage")) {
            pushLine(out, indent, QStringLiteral("TCBW.dom.setImage(%1, %2);")
                                      .arg(targetOf(block),
                                           socketExpr(QStringLiteral("source"))));
        } else if (type == QLatin1String("ui.setHtml")) {
            pushLine(out, indent, QStringLiteral("TCBW.dom.setHtml(%1, %2);")
                                      .arg(targetOf(block), socketExpr(QStringLiteral("html"))));
        } else {
            warnings.append(bilingual(
                QStringLiteral("No code generator for block '%1'.").arg(type),
                QStringLiteral("积木“%1”没有代码生成器。").arg(type)));
        }
        return;
    }

    // --- control ----------------------------------------------------------
    // --- 控制 -------------------------------------------------------------
    if (type == QLatin1String("control.break") || type == QLatin1String("control.continue")) {
        if (loopDepth <= 0) {
            errors.append(bilingual(
                QStringLiteral("'%1' can only be used inside a loop.")
                    .arg(type.section(QLatin1Char('.'), 1)),
                QStringLiteral("“%1”只能用在循环内部。")
                    .arg(type.section(QLatin1Char('.'), 1))));
        }
        pushLine(out, indent, type == QLatin1String("control.break")
                                  ? QStringLiteral("break;") : QStringLiteral("continue;"));
        return;
    }
    if (type == QLatin1String("control.return")) {
        pushLine(out, indent, QStringLiteral("return %1;").arg(socketExpr(QStringLiteral("value"))));
        return;
    }
    if (type == QLatin1String("control.stop")) {
        pushLine(out, indent, QStringLiteral("return;"));
        return;
    }
    if (type == QLatin1String("control.wait")) {
        //  Reached when `wait` is the last statement of a chain.
        //  当 `wait` 是语句链的最后一条时走到这里。
        requireApi(QStringLiteral("setTimeout"));
        pushLine(out, indent, QStringLiteral("setTimeout(function () {"));
        pushLine(out, indent, QStringLiteral("}, (%1) * 1000);")
                              .arg(socketExpr(QStringLiteral("seconds"))));
        return;
    }

    // --- variables --------------------------------------------------------
    // --- 变量 -------------------------------------------------------------
    if (type == QLatin1String("variables.set") || type == QLatin1String("variables.change")) {
        const QString rawName = fieldOf(QStringLiteral("name"));
        noteVariable(rawName);
        requireApi(QStringLiteral("TCBW.vars"));
        const QString alias = BlockCompiler::sanitiseIdentifier(rawName, QStringLiteral("value"));
        if (type == QLatin1String("variables.set")) {
            pushLine(out, indent, QStringLiteral("%1 = %2;")
                                      .arg(alias, socketExpr(QStringLiteral("value"))));
        } else {
            //  Number() coercion keeps a counter numeric even when the stored
            //  value started life as text.
            //  使用 Number() 强制转换，使计数器即使初始为文本也保持数值语义。
            pushLine(out, indent, QStringLiteral("%1 = (Number(%1) || 0) + (Number(%2) || 0);")
                                      .arg(alias, socketExpr(QStringLiteral("value"))));
        }
        pushLine(out, indent, QStringLiteral("TCBW.vars.set(%1, %2);")
                                  .arg(BlockCompiler::jsString(rawName), alias));
        return;
    }
    if (type == QLatin1String("variables.listNew")) {
        requireApi(QStringLiteral("TCBW.lists"));
        pushLine(out, indent, QStringLiteral("TCBW.lists.create(%1, %2);")
                                  .arg(fieldStr(QStringLiteral("name")),
                                       socketExpr(QStringLiteral("items"))));
        return;
    }
    if (type == QLatin1String("variables.listAdd")) {
        requireApi(QStringLiteral("TCBW.lists"));
        pushLine(out, indent, QStringLiteral("TCBW.lists.add(%1, %2);")
                                  .arg(fieldStr(QStringLiteral("name")),
                                       socketExpr(QStringLiteral("item"))));
        return;
    }
    if (type == QLatin1String("variables.listRemove")) {
        requireApi(QStringLiteral("TCBW.lists"));
        pushLine(out, indent, QStringLiteral("TCBW.lists.remove(%1, %2);")
                                  .arg(fieldStr(QStringLiteral("name")),
                                       socketExpr(QStringLiteral("index"))));
        return;
    }

    // --- storage ----------------------------------------------------------
    // --- 存储 -------------------------------------------------------------
    if (type.startsWith(QLatin1String("storage."))) {
        requireApi(QStringLiteral("TCBW.store"));
        if (type == QLatin1String("storage.set")) {
            requireApi(QStringLiteral("localStorage"));
            pushLine(out, indent, QStringLiteral("TCBW.store.set(%1, %2);")
                                      .arg(fieldStr(QStringLiteral("key")),
                                           socketExpr(QStringLiteral("value"))));
        } else if (type == QLatin1String("storage.remove")) {
            requireApi(QStringLiteral("localStorage"));
            pushLine(out, indent, QStringLiteral("TCBW.store.remove(%1);")
                                      .arg(fieldStr(QStringLiteral("key"))));
        } else if (type == QLatin1String("storage.clear")) {
            requireApi(QStringLiteral("localStorage"));
            pushLine(out, indent, QStringLiteral("TCBW.store.clear();"));
        } else if (type == QLatin1String("storage.sessionSet")) {
            requireApi(QStringLiteral("sessionStorage"));
            pushLine(out, indent, QStringLiteral("TCBW.store.sessionSet(%1, %2);")
                                      .arg(fieldStr(QStringLiteral("key")),
                                           socketExpr(QStringLiteral("value"))));
        } else if (type == QLatin1String("storage.cookieSet")) {
            requireApi(QStringLiteral("document.cookie"));
            pushLine(out, indent, QStringLiteral("TCBW.store.cookieSet(%1, %2, %3);")
                                      .arg(fieldStr(QStringLiteral("key")),
                                           socketExpr(QStringLiteral("value")),
                                           socketExpr(QStringLiteral("days"))));
        } else {
            warnings.append(bilingual(
                QStringLiteral("No code generator for block '%1'.").arg(type),
                QStringLiteral("积木“%1”没有代码生成器。").arg(type)));
        }
        return;
    }

    // --- http -------------------------------------------------------------
    // --- 网络请求 ---------------------------------------------------------
    if (type == QLatin1String("http.setHeader")) {
        requireApi(QStringLiteral("TCBW.http"));
        pushLine(out, indent, QStringLiteral("TCBW.http.setHeader(%1, %2);")
                                  .arg(fieldStr(QStringLiteral("name")),
                                       socketExpr(QStringLiteral("value"))));
        return;
    }

    // --- backend ----------------------------------------------------------
    // --- 后端 -------------------------------------------------------------
    if (type == QLatin1String("backend.authLogout")) {
        requireApi(QStringLiteral("TCBW.backend"));
        pushLine(out, indent, QStringLiteral("TCBW.backend.logout();"));
        return;
    }

    // --- data -------------------------------------------------------------
    // --- 数据 -------------------------------------------------------------
    if (type == QLatin1String("data.setField")) {
        requireApi(QStringLiteral("TCBW.data"));
        pushLine(out, indent, QStringLiteral("TCBW.data.set(%1, %2, %3);")
                                  .arg(socketExpr(QStringLiteral("object")),
                                       fieldStr(QStringLiteral("path")),
                                       socketExpr(QStringLiteral("value"))));
        return;
    }

    // --- ai ---------------------------------------------------------------
    // --- 人工智能 ---------------------------------------------------------
    if (type == QLatin1String("ai.chatMessage")) {
        requireApi(QStringLiteral("TCBW.ai"));
        pushLine(out, indent, QStringLiteral("TCBW.ai.chatMessage(%1, %2);")
                                  .arg(fieldStr(QStringLiteral("role")),
                                       socketExpr(QStringLiteral("content"))));
        return;
    }
    if (type == QLatin1String("ai.setSystemPrompt")) {
        requireApi(QStringLiteral("TCBW.ai"));
        pushLine(out, indent, QStringLiteral("TCBW.ai.setSystemPrompt(%1);")
                                  .arg(socketExpr(QStringLiteral("prompt"))));
        return;
    }

    // --- timing -----------------------------------------------------------
    // --- 定时 -------------------------------------------------------------
    if (type == QLatin1String("timing.debounce")) {
        requireApi(QStringLiteral("TCBW.timing"));
        pushLine(out, indent, QStringLiteral("TCBW.timing.debounce(%1, %2);")
                                  .arg(fieldStr(QStringLiteral("eventName")),
                                       socketExpr(QStringLiteral("milliseconds"))));
        return;
    }

    // --- console ----------------------------------------------------------
    // --- 控制台 -----------------------------------------------------------
    if (type.startsWith(QLatin1String("console."))) {
        requireApi(QStringLiteral("console"));
        if (type == QLatin1String("console.log")) {
            pushLine(out, indent, QStringLiteral("console.log(%1);")
                                      .arg(socketExpr(QStringLiteral("message"))));
        } else if (type == QLatin1String("console.warn")) {
            pushLine(out, indent, QStringLiteral("console.warn(%1);")
                                      .arg(socketExpr(QStringLiteral("message"))));
        } else if (type == QLatin1String("console.error")) {
            pushLine(out, indent, QStringLiteral("console.error(%1);")
                                      .arg(socketExpr(QStringLiteral("message"))));
        } else if (type == QLatin1String("console.table")) {
            pushLine(out, indent, QStringLiteral("console.table(%1);")
                                      .arg(socketExpr(QStringLiteral("message"))));
        } else if (type == QLatin1String("console.time")) {
            pushLine(out, indent, QStringLiteral("console.time(%1);")
                                      .arg(fieldStr(QStringLiteral("label"))));
        } else if (type == QLatin1String("console.timeEnd")) {
            pushLine(out, indent, QStringLiteral("console.timeEnd(%1);")
                                      .arg(fieldStr(QStringLiteral("label"))));
        } else {
            warnings.append(bilingual(
                QStringLiteral("No code generator for block '%1'.").arg(type),
                QStringLiteral("积木“%1”没有代码生成器。").arg(type)));
        }
        return;
    }

    warnings.append(bilingual(
        QStringLiteral("No code generator for block '%1'.").arg(type),
        QStringLiteral("积木“%1”没有代码生成器。").arg(type)));
    pushLine(out, indent, QStringLiteral("/* %1: no code generator */")
                              .arg(BlockCompiler::sanitiseIdentifier(type, QStringLiteral("block"))),
             false);
}

// ---------------------------------------------------------------------------
//  Script / program assembly
//  脚本与程序组装
// ---------------------------------------------------------------------------
QStringList BlockCompiler::Impl::scriptSnippet(const BlockScript &script)
{
    QStringList out;

    const Block *hat = script.hat.get();
    if (!hat) {
        errors.append(bilingual(QStringLiteral("Script '%1' has no hat block.").arg(script.id),
                                QStringLiteral("脚本“%1”没有帽子块。").arg(script.id)));
        return out;
    }

    const QString hatType = hat->type.isEmpty() ? script.eventType : hat->type;
    const BlockDef *hatDef = BlockRegistry::instance().find(hatType);
    if (!hatDef)
        warnings.append(bilingual(
            QStringLiteral("Unknown hat block '%1'; a named function is emitted instead.")
                .arg(hatType),
            QStringLiteral("未知的帽子块“%1”；改为输出具名函数。").arg(hatType)));

    //  Body first: the variable prelude depends on what the body uses.
    //  先编译主体：变量前置声明取决于主体使用了哪些变量。
    const QSet<QString> before = usedVariables;
    QStringList body;
    ++depth;
    emitBody(script.body, 1, &body);
    --depth;

    QStringList localVars;
    for (const QString &alias : usedVariables) {
        if (!before.contains(alias))
            localVars.append(alias);
    }
    std::sort(localVars.begin(), localVars.end());

    QStringList prelude;
    for (const QString &alias : localVars) {
        requireApi(QStringLiteral("TCBW.vars"));
        const QString original = variableNames.value(alias, alias);
        prelude.append(ind(1) + QStringLiteral("var %1 = TCBW.vars.get(%2);")
                                   .arg(alias, BlockCompiler::jsString(original)));
    }
    statementCount += prelude.size();

    const QString selectorField = QStringLiteral("selector");
    QString head;
    QString tail;
    QStringList after;

    if (hatType == QLatin1String("events.start")) {
        requireApi(QStringLiteral("TCBW.ready"));
        head = QStringLiteral("TCBW.ready(function () {");
        tail = QStringLiteral("});");
    } else if (hatType == QLatin1String("events.timer")) {
        requireApi(QStringLiteral("setInterval"));
        head = QStringLiteral("setInterval(function () {");
        tail = QStringLiteral("}, %1);").arg(numericLiteralOrIdentifier(
            hat->fieldValue(QStringLiteral("interval")).trimmed().isEmpty()
                ? QStringLiteral("1000")
                : hat->fieldValue(QStringLiteral("interval"))));
    } else if (hatType == QLatin1String("events.custom")) {
        const QString eventName = hat->fieldValue(QStringLiteral("eventName")).trimmed();
        const QString fnName = BlockCompiler::sanitiseIdentifier(
            eventName.isEmpty() ? QStringLiteral("onCustomEvent") : eventName,
            QStringLiteral("onCustomEvent"));
        head = QStringLiteral("function %1(event) {").arg(fnName);
        tail = QStringLiteral("}");
        requireApi(QStringLiteral("TCBW.on"));
        after.append(QStringLiteral("TCBW.on(document, %1, %2);")
                         .arg(BlockCompiler::jsString(eventName.isEmpty()
                                                          ? QStringLiteral("myEvent")
                                                          : eventName),
                              fnName));
    } else if (hatType == QLatin1String("events.click")
               || hatType == QLatin1String("events.doubleClick")
               || hatType == QLatin1String("events.submit")
               || hatType == QLatin1String("events.scroll")
               || hatType == QLatin1String("events.resize")
               || hatType == QLatin1String("events.keyPress")) {
        QString domEvent;
        if (hatType == QLatin1String("events.click"))            domEvent = QStringLiteral("click");
        else if (hatType == QLatin1String("events.doubleClick")) domEvent = QStringLiteral("dblclick");
        else if (hatType == QLatin1String("events.submit"))      domEvent = QStringLiteral("submit");
        else if (hatType == QLatin1String("events.scroll"))      domEvent = QStringLiteral("scroll");
        else if (hatType == QLatin1String("events.resize"))      domEvent = QStringLiteral("resize");
        else                                                    domEvent = QStringLiteral("keydown");

        requireApi(QStringLiteral("TCBW.on"));
        head = QStringLiteral("TCBW.on(%1, '%2', function (event) {")
                   .arg(targetOf(*hat, selectorField), domEvent);
        tail = QStringLiteral("});");

        if (hatType == QLatin1String("events.keyPress")) {
            const QString key = hat->fieldValue(QStringLiteral("key")).trimmed();
            if (!key.isEmpty()) {
                //  Guard inside the handler, so the body only runs for one key.
                //  在处理器内部加判断，使主体只在指定按键时执行。
                QStringList guarded;
                guarded.append(ind(1) + QStringLiteral("if (event.key === %1) {")
                                           .arg(BlockCompiler::jsString(key)));
                guarded += shiftLines(body.isEmpty() ? prelude : prelude + body, 1, width());
                guarded.append(ind(1) + QStringLiteral("}"));
                body = guarded;
                prelude.clear();
            }
        }
    } else {
        //  Unknown or visual-only hat: emit a named function the page can call.
        //  未知或仅用于显示的帽子块：输出页面可调用的具名函数。
        const QString fnName = BlockCompiler::sanitiseIdentifier(
            script.name.isEmpty() ? hatType : script.name, QStringLiteral("TCBWScript"));
        head = QStringLiteral("function %1(event) {").arg(fnName);
        tail = QStringLiteral("}");
    }

    out.append(head);
    out += prelude;
    out += body;
    out.append(tail);
    for (const QString &line : after)
        out.append(line);

    if (hatDef)
        ++statementCount; // the wrapper itself is an emitted statement / 包装本身也是一条语句
    return out;
}

// ===========================================================================
//  Public API
//  公共接口
// ===========================================================================
BlockCompiler::BlockCompiler(const CompileOptions &options)
    : d(std::make_unique<Impl>(options))
{
}

BlockCompiler::~BlockCompiler() = default;

CompileResult BlockCompiler::compileScript(const BlockScript &script)
{
    d->reset();

    QStringList body;
    if (!script.enabled) {
        //  A disabled script compiles to a comment and nothing else.
        //  已禁用的脚本只编译出一条注释。
        body.append(QStringLiteral("// script '%1' is disabled")
                        .arg(BlockCompiler::sanitiseIdentifier(
                            script.name.isEmpty() ? script.id : script.name,
                            QStringLiteral("script"))));
        CompileResult result;
        result.code = body.join(QLatin1Char('\n')) + QLatin1Char('\n');
        result.ok = true;
        result.scriptCount = 0;
        return result;
    }

    ++d->scriptCount;
    body = d->scriptSnippet(script);

    QStringList out;
    if (d->opt.useStrict)
        out.append(QStringLiteral("\"use strict\";"));

    if (d->opt.esm) {
        const QString module = sanitiseIdentifier(d->opt.moduleName,
                                                  QStringLiteral("TCBuildWebApp"));
        out.append(QStringLiteral("const %1 = (function () {").arg(module));
        out += shiftLines(body, 1, d->width());
        out.append(d->ind(1) + QStringLiteral("return { name: %1, scripts: %2 };")
                       .arg(jsString(d->opt.moduleName))
                       .arg(d->scriptCount));
        out.append(QStringLiteral("})();"));
        out.append(QStringLiteral("export default %1;").arg(module));
    } else if (d->opt.wrapInIife) {
        out.append(QStringLiteral("(function () {"));
        out += shiftLines(body, 1, d->width());
        out.append(QStringLiteral("})();"));
    } else {
        out += body;
    }

    if (d->opt.emitSourceMapComment) {
        out.append(QStringLiteral("//# sourceMappingURL=%1.js.map")
                       .arg(sanitiseIdentifier(d->opt.moduleName,
                                               QStringLiteral("app"))));
    }

    CompileResult result;
    result.code = out.join(QLatin1Char('\n')) + QLatin1Char('\n');
    result.errors = d->errors;
    result.warnings = d->warnings;
    result.requiredApis = d->requiredApis;
    result.usedVariables = d->usedVariables;
    result.statementCount = d->statementCount;
    result.scriptCount = d->scriptCount;
    result.ok = result.errors.isEmpty();
    return result;
}

CompileResult BlockCompiler::compileProgram(const QVector<BlockScript *> &scripts)
{
    d->reset();

    QStringList body;
    for (const BlockScript *script : scripts) {
        if (!script)
            continue;
        if (!script->enabled) {
            if (!body.isEmpty())
                body.append(QString());
            body.append(QStringLiteral("// script '%1' is disabled")
                            .arg(sanitiseIdentifier(
                                script->name.isEmpty() ? script->id : script->name,
                                QStringLiteral("script"))));
            continue;
        }

        ++d->scriptCount;
        const QStringList snippet = d->scriptSnippet(*script);
        if (snippet.isEmpty())
            continue;
        if (!body.isEmpty())
            body.append(QString());
        body += snippet;
    }

    QStringList out;
    if (d->opt.useStrict && !d->opt.esm)
        out.append(QStringLiteral("\"use strict\";"));

    if (d->opt.esm) {
        const QString module = sanitiseIdentifier(d->opt.moduleName,
                                                  QStringLiteral("TCBuildWebApp"));
        out.append(QStringLiteral("const %1 = (function () {").arg(module));
        out += shiftLines(body, 1, d->width());
        out.append(d->ind(1) + QStringLiteral("return { name: %1, scripts: %2 };")
                       .arg(jsString(d->opt.moduleName))
                       .arg(d->scriptCount));
        out.append(QStringLiteral("})();"));
        out.append(QStringLiteral("export default %1;").arg(module));
    } else if (d->opt.wrapInIife) {
        out.append(QStringLiteral("(function () {"));
        out += shiftLines(body, 1, d->width());
        out.append(QStringLiteral("})();"));
    } else {
        out += body;
    }

    if (d->opt.emitSourceMapComment) {
        out.append(QStringLiteral("//# sourceMappingURL=%1.js.map")
                       .arg(sanitiseIdentifier(d->opt.moduleName,
                                               QStringLiteral("app"))));
    }

    CompileResult result;
    result.code = out.join(QLatin1Char('\n')) + QLatin1Char('\n');
    result.errors = d->errors;
    result.warnings = d->warnings;
    result.requiredApis = d->requiredApis;
    result.usedVariables = d->usedVariables;
    result.statementCount = d->statementCount;
    result.scriptCount = d->scriptCount;
    result.ok = result.errors.isEmpty();
    return result;
}

QString BlockCompiler::compileExpression(const Block &block, QStringList *errors) const
{
    QStringList local;
    QStringList *sink = errors ? errors : &local;
    return d->expr(block, sink);
}

// ===========================================================================
//  Shared helpers
//  共享辅助函数
// ===========================================================================
QString BlockCompiler::escapeJsString(const QString &value)
{
    QString out;
    out.reserve(value.size() + 8);
    for (const QChar ch : value) {
        const ushort code = ch.unicode();
        switch (code) {
        case '\\': out += QStringLiteral("\\\\"); break;
        case '\'': out += QStringLiteral("\\'"); break;
        case '"':  out += QStringLiteral("\\\""); break;
        case '\n': out += QStringLiteral("\\n"); break;
        case '\r': out += QStringLiteral("\\r"); break;
        case '\t': out += QStringLiteral("\\t"); break;
        case '\b': out += QStringLiteral("\\b"); break;
        case '\f': out += QStringLiteral("\\f"); break;
        case 0x0B: out += QStringLiteral("\\v"); break;
        case 0x00: out += QStringLiteral("\\u0000"); break;
        case 0x2028: out += QStringLiteral("\\u2028"); break;
        case 0x2029: out += QStringLiteral("\\u2029"); break;
        //  Neutralises "<", ">" and "&" so an inline <script> cannot be broken
        //  out of by page content.
        //  转义 "<"、">" 与 "&"，避免内联 <script> 被内容截断。
        case '<': out += QStringLiteral("\\u003C"); break;
        case '>': out += QStringLiteral("\\u003E"); break;
        case '&': out += QStringLiteral("\\u0026"); break;
        default:
            if (code < 0x20 || code == 0x7F) {
                out += QStringLiteral("\\u%1")
                           .arg(static_cast<uint>(code), 4, 16, QLatin1Char('0'));
            } else {
                out += ch;
            }
            break;
        }
    }
    return out;
}

QString BlockCompiler::escapeJsTemplate(const QString &value)
{
    QString out;
    out.reserve(value.size() + 8);
    for (int i = 0; i < value.size(); ++i) {
        const QChar ch = value.at(i);
        const ushort code = ch.unicode();
        switch (code) {
        case '\\': out += QStringLiteral("\\\\"); break;
        case '`':  out += QStringLiteral("\\`"); break;
        case '\n': out += QStringLiteral("\\n"); break;
        case '\r': out += QStringLiteral("\\r"); break;
        case 0x2028: out += QStringLiteral("\\u2028"); break;
        case 0x2029: out += QStringLiteral("\\u2029"); break;
        case '$':
            //  "${" starts an interpolation; escape it.
            //  "${" 会开始插值，必须转义。
            if (i + 1 < value.size() && value.at(i + 1) == QLatin1Char('{'))
                out += QStringLiteral("\\$");
            else
                out += ch;
            break;
        default:
            out += ch;
            break;
        }
    }
    return out;
}

QString BlockCompiler::sanitiseIdentifier(const QString &value, const QString &fallback)
{
    QString out = sanitiseCore(value);
    if (out.isEmpty())
        out = sanitiseCore(fallback);
    if (out.isEmpty())
        out = QStringLiteral("value");
    if (out.at(0).isDigit())
        out.prepend(QLatin1Char('_'));
    if (isReservedWord(out))
        out += QLatin1Char('_');
    if (out.size() > 64)
        out.truncate(64);
    return out;
}

QString BlockCompiler::jsString(const QString &value)
{
    QString out;
    out.reserve(value.size() + 2);
    out += QLatin1Char('\'');
    out += escapeJsString(value);
    out += QLatin1Char('\'');
    return out;
}

double BlockCompiler::parseNumber(const QString &text, double fallback)
{
    bool ok = false;
    const double value = text.trimmed().toDouble(&ok);
    if (!ok || !std::isfinite(value))
        return fallback;
    return value;
}

QString BlockCompiler::numericExpression(const QString &raw) const
{
    return numericLiteralOrIdentifier(raw);
}

// ===========================================================================
//  Private helpers declared by the header
//  头文件声明的私有辅助函数
// ===========================================================================
QString BlockCompiler::compileBlock(const Block &block, int indent, QStringList *out)
{
    const BlockDef *def = BlockRegistry::instance().find(block.type);
    if (!def) {
        d->errors.append(bilingual(
            QStringLiteral("Unknown block type '%1'.").arg(block.type),
            QStringLiteral("未知的积木类型“%1”。").arg(block.type)));
        return QString();
    }
    if (def->returnsValue())
        return d->expr(block, &d->errors);

    QStringList scratch;
    d->emitStatement(block, indent, out ? out : &scratch);
    return QString();
}

QString BlockCompiler::compileCShape(const Block &block, const BlockDef &def, int indent,
                                     QStringList *out)
{
    QStringList scratch;
    return d->emitCShape(block, def, indent, out ? out : &scratch);
}

QString BlockCompiler::shadowValue(const Block &parent, const QString &inputName,
                                   const BlockDef &def, const BlockInputDef &inputDef) const
{
    Q_UNUSED(def);
    //  Older project files kept the socket text in a field of the same name.
    //  旧工程文件会把槽位文本存放在同名字段里。
    const QString stored = parent.fieldValue(inputName);
    if (!stored.isEmpty()) {
        if (inputDef.kind == ValueKind::Number)
            return numericLiteralOrIdentifier(stored);
        return jsString(stored);
    }
    bool missing = false;
    return shadowLiteralOf(inputDef, &missing);
}

QString BlockCompiler::indentText(int level) const
{
    return d->ind(level);
}

} // namespace tcbw
