// ============================================================================
//  BackendScaffold.cpp -- generate a matching server project for the built page
//  BackendScaffold.cpp —— 为所构建的页面生成配套的服务器工程
//  Part of TCTools-Build_Web (TC-BuildWeb) v0.1.0-rc1
// ============================================================================
//
//  NOTE / 注意
//  BackendConnector.h (used here for the target catalogue) names
//  QNetworkRequest without declaring it, so the Qt header comes first.
//  BackendConnector.h（本文件用它读取目标目录）使用了 QNetworkRequest 却没有
//  声明它，因此这里必须先包含该 Qt 头文件。
#include <QNetworkRequest>

#include "BackendScaffold.h"

#include "BackendConnector.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStringList>
#include <QUrl>

namespace tcbw {

namespace {

const QString kGeneratedBy =
    QStringLiteral("TCTools-Build_Web (TC-BuildWeb) v0.1.0-rc1");

//  Lower-case ASCII slug usable as a package / module / directory name.
//  可作为包名 / 模块名 / 目录名的小写 ASCII 短名。
QString slugifyAscii(const QString &text, const QString &fallback)
{
    QString out;
    out.reserve(text.size());
    bool lastDash = false;
    for (const QChar ch : text) {
        const QChar lower = ch.toLower();
        if ((lower >= QLatin1Char('a') && lower <= QLatin1Char('z'))
            || (lower >= QLatin1Char('0') && lower <= QLatin1Char('9'))) {
            out += lower;
            lastDash = false;
        } else if (!lastDash && !out.isEmpty()) {
            out += QLatin1Char('-');
            lastDash = true;
        }
    }
    while (out.endsWith(QLatin1Char('-')))
        out.chop(1);
    return out.isEmpty() ? fallback : out;
}

//  Safe identifier for a route parameter in every generated language.
//  在所有生成语言中都安全的路径参数标识符。
QString identifierFor(const QString &raw, const QString &fallback = QStringLiteral("param"))
{
    QString out;
    for (const QChar ch : raw) {
        if (ch.isLetterOrNumber() && ch.unicode() < 128)
            out += ch;
        else if (ch == QLatin1Char('_') || ch == QLatin1Char('-') || ch == QLatin1Char('.'))
            out += QLatin1Char('_');
    }
    while (out.startsWith(QLatin1Char('_')))
        out.remove(0, 1);
    if (out.isEmpty())
        return fallback;
    if (out.at(0).isDigit())
        out.prepend(QLatin1Char('p'));
    return out;
}

//  Double-quoted string literal with the escapes understood by JS, Python and
//  Go alike (they share the \n, \t, \\, \" and \uXXXX forms).
//  使用 JS、Python 与 Go 都理解的转义生成双引号字符串字面量
//  （它们共享 \n、\t、\\、\" 与 \uXXXX 形式）。
QString quoted(const QString &value)
{
    QString out;
    out.reserve(value.size() + 2);
    out += QLatin1Char('"');
    for (const QChar ch : value) {
        const char16_t unit = ch.unicode();
        switch (unit) {
        case u'\\': out += QStringLiteral("\\\\"); break;
        case u'"':  out += QStringLiteral("\\\""); break;
        case u'\n': out += QStringLiteral("\\n"); break;
        case u'\r': out += QStringLiteral("\\r"); break;
        case u'\t': out += QStringLiteral("\\t"); break;
        default:
            if (unit < 0x20 || unit == 0x2028 || unit == 0x2029) {
                out += QStringLiteral("\\u%1").arg(static_cast<uint>(unit), 4, 16,
                                                   QLatin1Char('0'));
            } else {
                out += ch;
            }
            break;
        }
    }
    out += QLatin1Char('"');
    return out;
}

//  Comment text must stay on one line whatever the user typed.
//  无论用户输入什么，注释文本都必须保持单行。
QString oneLine(const QString &text)
{
    QString out = text;
    out.replace(QLatin1Char('\r'), QLatin1Char(' '));
    out.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return out.simplified();
}

QStringList comment(const ScaffoldRequest &request, const QString &marker,
                    const QString &english, const QString &chinese,
                    bool forceBilingual = false)
{
    QStringList lines;
    lines << (marker + QLatin1Char(' ') + oneLine(english));
    if (forceBilingual || request.chineseComments)
        lines << (marker + QLatin1Char(' ') + oneLine(chinese));
    return lines;
}

QString joinLines(const QStringList &lines)
{
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QString jsonText(const QJsonObject &object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Indented));
}

//  Endpoints that are actually enabled; a disabled endpoint is not wired up.
//  实际启用的端点；被禁用的端点不会接线。
QVector<Endpoint> activeEndpoints(const ScaffoldRequest &request)
{
    QVector<Endpoint> list;
    for (const Endpoint &endpoint : request.endpoints.endpoints()) {
        if (endpoint.enabled)
            list.append(endpoint);
    }
    return list;
}

QString normaliseRoutePath(const QString &path)
{
    QString out = path;
    while (out.contains(QStringLiteral("//")))
        out.replace(QStringLiteral("//"), QStringLiteral("/"));
    while (out.size() > 1 && out.endsWith(QLatin1Char('/')))
        out.chop(1);
    if (!out.startsWith(QLatin1Char('/')))
        out.prepend(QLatin1Char('/'));
    return out;
}

//  The request path an endpoint should be served at.
//  端点应被提供的请求路径。
QString endpointRoutePath(const Endpoint &endpoint)
{
    QString raw = endpoint.url.trimmed();
    const int cut = raw.indexOf(QRegularExpression(QStringLiteral("[?#]")));
    if (cut >= 0)
        raw = raw.left(cut);

    const QString fallback =
        QStringLiteral("/api/") + slugifyAscii(endpoint.name, QStringLiteral("endpoint"));

    if (raw.isEmpty())
        return normaliseRoutePath(fallback);
    if (raw.startsWith(QLatin1Char('/')))
        return normaliseRoutePath(raw);

    const QUrl url(raw);
    if (!url.scheme().isEmpty()) {
        const QString path = url.path();
        return normaliseRoutePath(path.isEmpty() ? fallback : path);
    }

    //  Template hosts such as "{{baseUrl}}/api/items": keep everything from the
    //  first slash onwards.
    //  形如 "{{baseUrl}}/api/items" 的模板主机：保留第一个斜杠之后的部分。
    const int slash = raw.indexOf(QLatin1Char('/'));
    if (slash >= 0)
        return normaliseRoutePath(raw.mid(slash));
    return normaliseRoutePath(fallback);
}

//  Rewrites {{name}} placeholders into the route syntax of a target language.
//  把 {{name}} 占位符改写为目标语言的路由语法。
QString rewritePlaceholders(const QString &path, const QString &style)
{
    QString out;
    int cursor = 0;
    while (cursor < path.size()) {
        const int open = path.indexOf(QStringLiteral("{{"), cursor);
        if (open < 0) {
            out += path.mid(cursor);
            break;
        }
        out += path.mid(cursor, open - cursor);
        const int close = path.indexOf(QStringLiteral("}}"), open + 2);
        if (close < 0) {
            out += path.mid(open);
            break;
        }
        const QString name = identifierFor(path.mid(open + 2, close - open - 2).trimmed());
        if (style == QStringLiteral("colon"))
            out += QLatin1Char(':') + name;
        else if (style == QStringLiteral("brace"))
            out += QLatin1Char('{') + name + QLatin1Char('}');
        else
            out += name;
        cursor = close + 2;
    }
    return out;
}

QStringList routeParameters(const QString &path)
{
    QStringList names;
    int cursor = 0;
    while (cursor < path.size()) {
        const int open = path.indexOf(QStringLiteral("{{"), cursor);
        if (open < 0)
            break;
        const int close = path.indexOf(QStringLiteral("}}"), open + 2);
        if (close < 0)
            break;
        const QString name = identifierFor(path.mid(open + 2, close - open - 2).trimmed());
        if (!name.isEmpty() && !names.contains(name))
            names << name;
        cursor = close + 2;
    }
    return names;
}

//  JS regular expression source with one capture group per route parameter.
//  每个路径参数一个捕获组的 JS 正则表达式源码。
QString routeRegexSource(const QString &path, QStringList *parameters)
{
    QString out;
    int cursor = 0;
    while (cursor < path.size()) {
        const int open = path.indexOf(QStringLiteral("{{"), cursor);
        if (open < 0) {
            out += QRegularExpression::escape(path.mid(cursor));
            break;
        }
        out += QRegularExpression::escape(path.mid(cursor, open - cursor));
        const int close = path.indexOf(QStringLiteral("}}"), open + 2);
        if (close < 0) {
            out += QRegularExpression::escape(path.mid(open));
            break;
        }
        const QString name = identifierFor(path.mid(open + 2, close - open - 2).trimmed());
        if (parameters && !parameters->contains(name))
            parameters->append(name);
        out += QStringLiteral("([^/]+)");
        cursor = close + 2;
    }
    return QStringLiteral("^") + out + QStringLiteral("/?$");
}

QString httpMethodOf(const Endpoint &endpoint)
{
    QString method = endpoint.methodUpper();
    if (method.isEmpty())
        method = QStringLiteral("GET");
    if (endpoint.isGraphQl && method == QStringLiteral("GET"))
        method = QStringLiteral("POST");
    return method;
}

bool methodCarriesBody(const QString &method)
{
    return method == QStringLiteral("POST") || method == QStringLiteral("PUT")
        || method == QStringLiteral("PATCH") || method == QStringLiteral("DELETE");
}

QString nodeMajorVersion(const ScaffoldRequest &request)
{
    static const QRegularExpression digits(QStringLiteral("(\\d+)"));
    const auto match = digits.match(request.nodeVersion);
    return match.hasMatch() ? match.captured(1) : QStringLiteral("22");
}

QString pythonVersionOr(const ScaffoldRequest &request, const QString &fallback)
{
    static const QRegularExpression version(QStringLiteral("^\\d+\\.\\d+"));
    const QString value = request.pythonVersion.trimmed();
    return version.match(value).hasMatch() ? value : fallback;
}

QString functionNameFor(const Endpoint &endpoint, int index)
{
    const QString base = slugifyAscii(endpoint.name, QStringLiteral("endpoint"));
    QString name = base;
    name.replace(QLatin1Char('-'), QLatin1Char('_'));
    if (name.isEmpty() || name.at(0).isDigit())
        name.prepend(QStringLiteral("ep_"));
    if (index >= 0)
        name += QLatin1Char('_') + QString::number(index);
    return name;
}

//  A unique handler name even when two endpoints share a name.
//  即使两个端点同名也能得到唯一处理函数名。
QStringList handlerNames(const QVector<Endpoint> &endpoints)
{
    QStringList names;
    QHash<QString, int> seen;
    for (const Endpoint &endpoint : endpoints) {
        const QString base = functionNameFor(endpoint, -1);
        const int count = seen.value(base, 0);
        seen.insert(base, count + 1);
        names << (count == 0 ? base
                             : base + QLatin1Char('_') + QString::number(count + 1));
    }
    return names;
}

QString envExampleText(const ScaffoldRequest &request, const QStringList &extraNames)
{
    //  Variable NAMES only -- never a value, never a real secret.
    //  仅包含变量名 —— 绝不含值，也绝不含真实机密。
    QStringList lines;
    lines << comment(request, QStringLiteral("#"),
                     QStringLiteral("Generated by ") + kGeneratedBy
                         + QStringLiteral(" -- copy to .env and fill in locally."),
                     QStringLiteral("由 ") + kGeneratedBy
                         + QStringLiteral(" 生成 —— 复制为 .env 并在本地填写。"));
    lines << QString();
    lines << QStringLiteral("PORT=");
    lines << QStringLiteral("CORS_ORIGIN=");
    if (request.includeAuthStubs)
        lines << QStringLiteral("TCBW_API_TOKEN=");
    for (const QString &name : extraNames)
        lines << name + QStringLiteral("=");
    return joinLines(lines);
}

QString readmeText(const ScaffoldRequest &request, const BackendTarget *target,
                   const QVector<Endpoint> &endpoints, const QStringList &runLines,
                   const QStringList &extraNotes)
{
    QStringList lines;
    lines << QStringLiteral("# ") + request.projectName;
    lines << QString();
    lines << QStringLiteral("Generated by ") + kGeneratedBy + QStringLiteral(".");
    lines << QStringLiteral("由 ") + kGeneratedBy + QStringLiteral(" 生成。");
    lines << QString();
    if (target) {
        lines << QStringLiteral("- Target / 目标: **") + target->labelEn + QStringLiteral("** (")
              + target->labelZh + QStringLiteral(")");
        if (!target->docsUrl.isEmpty())
            lines << QStringLiteral("- Docs / 文档: ") + target->docsUrl;
    }
    const QString healthPath = (target && !target->healthPath.trimmed().isEmpty())
                                   ? target->healthPath.trimmed()
                                   : QStringLiteral("/health");
    lines << QStringLiteral("- Health check / 健康检查: `GET ") + healthPath
                 + QStringLiteral("`");
    lines << QString();
    lines << QStringLiteral("## Endpoints / 端点");
    lines << QString();
    if (endpoints.isEmpty()) {
        lines << QStringLiteral("_No enabled endpoints in this project yet._");
        lines << QStringLiteral("_本工程暂无启用的端点。_");
    } else {
        lines << QStringLiteral("| Method | Path | Name |");
        lines << QStringLiteral("| --- | --- | --- |");
        for (const Endpoint &endpoint : endpoints) {
            lines << QStringLiteral("| `") + httpMethodOf(endpoint) + QStringLiteral("` | `")
                         + rewritePlaceholders(endpointRoutePath(endpoint), QStringLiteral("brace"))
                         + QStringLiteral("` | ") + oneLine(endpoint.name) + QStringLiteral(" |");
        }
    }
    lines << QString();
    lines << QStringLiteral("## Run / 运行");
    lines << QString();
    lines << QStringLiteral("```bash");
    for (const QString &line : runLines)
        lines << line;
    lines << QStringLiteral("```");
    lines << QString();
    if (request.includeAuthStubs) {
        lines << QStringLiteral("## Authentication / 认证");
        lines << QString();
        lines << QStringLiteral(
            "Every route except the health check requires an "
            "`Authorization: Bearer <token>` header, compared against the "
            "`TCBW_API_TOKEN` environment variable in constant time. This is a "
            "working stub, not a placeholder: replace it with your real identity "
            "provider before exposing the server.");
        lines << QString();
        lines << QStringLiteral(
            "除健康检查外的所有路由都需要 `Authorization: Bearer <token>` 头，"
            "并与 `TCBW_API_TOKEN` 环境变量做恒定时间比较。这是一个可用的桩实现"
            "（不是占位符）：对外暴露服务器前请替换为你真正的身份认证服务。");
        lines << QString();
    }
    if (!extraNotes.isEmpty()) {
        lines << QStringLiteral("## Notes / 说明");
        lines << QString();
        for (const QString &note : extraNotes)
            lines << QStringLiteral("- ") + note;
        lines << QString();
    }
    return joinLines(lines);
}

QString dockerfileForNode(const ScaffoldRequest &request, const QString &startCommand)
{
    QStringList lines;
    lines << comment(request, QStringLiteral("#"),
                     QStringLiteral("Generated by ") + kGeneratedBy,
                     QStringLiteral("由 ") + kGeneratedBy + QStringLiteral(" 生成"));
    lines << QStringLiteral("FROM node:") + nodeMajorVersion(request) + QStringLiteral("-alpine");
    lines << QStringLiteral("WORKDIR /app");
    lines << QStringLiteral("COPY package*.json ./");
    lines << QStringLiteral("RUN npm install --omit=dev");
    lines << QStringLiteral("COPY . .");
    lines << QStringLiteral("ENV PORT=3000");
    lines << QStringLiteral("EXPOSE 3000");
    lines << QStringLiteral("CMD ") + startCommand;
    return joinLines(lines);
}

//
//  node-express
//
QString expressServerJs(const ScaffoldRequest &request, const QVector<Endpoint> &endpoints)
{
    const QString name = slugifyAscii(request.projectName, QStringLiteral("tcbw-backend"));
    QStringList out;

    out << comment(request, QStringLiteral("//"),
                   QStringLiteral("Generated by ") + kGeneratedBy + QStringLiteral("."),
                   QStringLiteral("由 ") + kGeneratedBy + QStringLiteral(" 生成。"));
    out << comment(request, QStringLiteral("//"),
                   QStringLiteral("Run: npm install && cp .env.example .env && npm start"),
                   QStringLiteral("运行：npm install && cp .env.example .env && npm start"));
    out << QString();
    out << QStringLiteral("import express from 'express';");
    if (request.includeCors)
        out << QStringLiteral("import cors from 'cors';");
    if (request.includeAuthStubs)
        out << QStringLiteral("import { timingSafeEqual } from 'node:crypto';");
    out << QString();
    out << QStringLiteral("const app = express();");
    out << QStringLiteral("app.disable('x-powered-by');");
    out << QStringLiteral("app.use(express.json({ limit: '1mb' }));");

    if (request.includeCors) {
        out << QString();
        out << comment(request, QStringLiteral("//"),
                       QStringLiteral("CORS: set CORS_ORIGIN to a comma separated list of the "
                                      "origins you really serve."),
                       QStringLiteral("跨域：把 CORS_ORIGIN 设为真实服务来源的逗号分隔列表。"));
        out << QStringLiteral("const allowedOrigins = (process.env.CORS_ORIGIN || '*')");
        out << QStringLiteral("  .split(',')");
        out << QStringLiteral("  .map((value) => value.trim())");
        out << QStringLiteral("  .filter(Boolean);");
        out << QStringLiteral("app.use(cors({");
        out << QStringLiteral("  origin: allowedOrigins.includes('*') ? true : allowedOrigins,");
        out << QStringLiteral("  credentials: true,");
        out << QStringLiteral("}));");
    }

    if (request.includeAuthStubs) {
        out << QString();
        out << comment(request, QStringLiteral("//"),
                       QStringLiteral("Authentication stub: a real bearer-token check against "
                                      "TCBW_API_TOKEN."),
                       QStringLiteral("认证桩：针对 TCBW_API_TOKEN 的真实 bearer 令牌校验。"),
                       true);
        out << comment(request, QStringLiteral("//"),
                       QStringLiteral("It is deliberately a working check rather than a "
                                      "placeholder -- swap it for your identity provider."),
                       QStringLiteral("它刻意写成可用的校验而非占位符 —— 请替换为你的身份认证服务。"),
                       true);
        out << QStringLiteral("function safeEqual(a, b) {");
        out << QStringLiteral("  const left = Buffer.from(String(a), 'utf8');");
        out << QStringLiteral("  const right = Buffer.from(String(b), 'utf8');");
        out << QStringLiteral("  if (left.length !== right.length) return false;");
        out << QStringLiteral("  return timingSafeEqual(left, right);");
        out << QStringLiteral("}");
        out << QString();
        out << QStringLiteral("function requireToken(req, res, next) {");
        out << QStringLiteral("  const expected = process.env.TCBW_API_TOKEN;");
        out << QStringLiteral("  if (!expected) {");
        out << QStringLiteral("    return res.status(503).json({ error: 'server token is not "
                              "configured', error_zh: '服务器令牌未配置' });");
        out << QStringLiteral("  }");
        out << QStringLiteral("  const header = String(req.get('authorization') || '');");
        out << QStringLiteral("  const token = header.startsWith('Bearer ') ? header.slice(7) : '';");
        out << QStringLiteral("  if (!token || !safeEqual(token, expected)) {");
        out << QStringLiteral("    return res.status(401).json({ error: 'invalid bearer token', "
                              "error_zh: 'bearer 令牌无效' });");
        out << QStringLiteral("  }");
        out << QStringLiteral("  return next();");
        out << QStringLiteral("}");
    }

    out << QString();
    out << comment(request, QStringLiteral("//"),
                   QStringLiteral("Health check -- always public."),
                   QStringLiteral("健康检查 —— 始终公开。"));
    out << QStringLiteral("app.get('/health', (req, res) => {");
    out << QStringLiteral("  res.json({ status: 'ok', name: ") + quoted(name)
               + QStringLiteral(", uptimeSeconds: Math.round(process.uptime()) });");
    out << QStringLiteral("});");

    if (request.includeAuthStubs) {
        out << QString();
        out << QStringLiteral("// Everything registered after this line needs a bearer token.");
        out << QStringLiteral("app.use(requireToken);");
    }

    for (int i = 0; i < endpoints.size(); ++i) {
        const Endpoint &endpoint = endpoints.at(i);
        const QString method = httpMethodOf(endpoint);
        const QString path = rewritePlaceholders(endpointRoutePath(endpoint),
                                                 QStringLiteral("colon"));
        const QString verb = method.toLower();
        const QString call = (verb == QStringLiteral("get") || verb == QStringLiteral("post")
                              || verb == QStringLiteral("put") || verb == QStringLiteral("patch")
                              || verb == QStringLiteral("delete"))
                                 ? QStringLiteral("app.") + verb
                                 : QStringLiteral("app.all");

        out << QString();
        out << comment(request, QStringLiteral("//"),
                       method + QLatin1Char(' ') + path + QStringLiteral(" -- ")
                           + oneLine(endpoint.name),
                       method + QLatin1Char(' ') + path + QStringLiteral(" —— ")
                           + oneLine(endpoint.name));
        out << call + QStringLiteral("(") + quoted(path) + QStringLiteral(", (req, res) => {");
        out << QStringLiteral("  // Replace this echo with your real handler.");
        out << QStringLiteral("  // 请把这段回显替换为你真正的处理逻辑。");
        out << QStringLiteral("  res.json({");
        out << QStringLiteral("    ok: true,");
        out << QStringLiteral("    endpoint: ") + quoted(oneLine(endpoint.name)) + QStringLiteral(",");
        out << QStringLiteral("    method: ") + quoted(method) + QStringLiteral(",");
        out << QStringLiteral("    path: ") + quoted(path) + QStringLiteral(",");
        out << QStringLiteral("    params: req.params,");
        out << QStringLiteral("    query: req.query,");
        if (methodCarriesBody(method))
            out << QStringLiteral("    body: req.body ?? null,");
        else
            out << QStringLiteral("    body: null,");
        out << QStringLiteral("  });");
        out << QStringLiteral("});");
    }

    out << QString();
    out << QStringLiteral("app.use((req, res) => {");
    out << QStringLiteral("  res.status(404).json({ error: 'not found', error_zh: '未找到' });");
    out << QStringLiteral("});");
    out << QString();
    out << QStringLiteral("app.use((err, req, res, next) => {");
    out << QStringLiteral("  console.error('[tcbw]', err);");
    out << QStringLiteral("  res.status(err.status || 500).json({ error: 'internal error', "
                          "error_zh: '服务器内部错误' });");
    out << QStringLiteral("});");
    out << QString();
    out << QStringLiteral("const port = Number(process.env.PORT || 3000);");
    out << QStringLiteral("app.listen(port, '127.0.0.1', () => {");
    out << QStringLiteral("  console.log(`[tcbw] listening on http://127.0.0.1:${port}`);");
    out << QStringLiteral("});");

    return joinLines(out);
}

//
//  python-fastapi
//
QString fastApiMainPy(const ScaffoldRequest &request, const QVector<Endpoint> &endpoints)
{
    const QString name = slugifyAscii(request.projectName, QStringLiteral("tcbw-backend"));
    //  Unique function names, even when two endpoints share a display name.
    //  即使两个端点同名，也保证函数名唯一。
    const QStringList handlers = handlerNames(endpoints);
    QStringList out;

    out << comment(request, QStringLiteral("#"),
                   QStringLiteral("Generated by ") + kGeneratedBy + QStringLiteral("."),
                   QStringLiteral("由 ") + kGeneratedBy + QStringLiteral(" 生成。"));
    out << comment(request, QStringLiteral("#"),
                   QStringLiteral("Run: pip install -r requirements.txt && "
                                  "uvicorn main:app --reload"),
                   QStringLiteral("运行：pip install -r requirements.txt && "
                                  "uvicorn main:app --reload"));
    out << QString();
    out << QStringLiteral("from __future__ import annotations");
    out << QString();
    if (request.includeAuthStubs)
        out << QStringLiteral("import hmac");
    if (request.includeCors || request.includeAuthStubs)
        out << QStringLiteral("import os");
    out << QStringLiteral("from typing import Any");
    out << QString();
    if (request.includeAuthStubs)
        out << QStringLiteral("from fastapi import Depends, FastAPI, Header, HTTPException, "
                              "Request");
    else
        out << QStringLiteral("from fastapi import FastAPI, Request");
    if (request.includeCors)
        out << QStringLiteral("from fastapi.middleware.cors import CORSMiddleware");
    out << QString();
    out << QStringLiteral("app = FastAPI(title=") + quoted(request.projectName)
               + QStringLiteral(")");

    if (request.includeCors) {
        out << QString();
        out << comment(request, QStringLiteral("#"),
                       QStringLiteral("CORS: set CORS_ORIGIN to the origins you really serve."),
                       QStringLiteral("跨域：把 CORS_ORIGIN 设为真实服务来源。"));
        out << QStringLiteral("app.add_middleware(");
        out << QStringLiteral("    CORSMiddleware,");
        out << QStringLiteral("    allow_origins=[origin.strip() for origin in "
                              "os.environ.get(\"CORS_ORIGIN\", \"*\").split(\",\") if "
                              "origin.strip()],");
        out << QStringLiteral("    allow_credentials=True,");
        out << QStringLiteral("    allow_methods=[\"*\"],");
        out << QStringLiteral("    allow_headers=[\"*\"],");
        out << QStringLiteral(")");
    }

    out << QString();
    out << QStringLiteral("async def read_json(request: Request) -> Any:");
    out << QStringLiteral("    \"\"\"Returns the JSON body, or None when there is none.");
    out << QStringLiteral("    返回 JSON 正文；没有正文时返回 None。\"\"\"");
    out << QStringLiteral("    try:");
    out << QStringLiteral("        return await request.json()");
    out << QStringLiteral("    except Exception:  # noqa: BLE001 - an empty body is normal");
    out << QStringLiteral("        return None");

    if (request.includeAuthStubs) {
        out << QString();
        out << comment(request, QStringLiteral("#"),
                       QStringLiteral("Authentication stub: a real bearer-token check against "
                                      "TCBW_API_TOKEN."),
                       QStringLiteral("认证桩：针对 TCBW_API_TOKEN 的真实 bearer 令牌校验。"),
                       true);
        out << comment(request, QStringLiteral("#"),
                       QStringLiteral("It is a working check, not a placeholder -- replace it "
                                      "with your identity provider."),
                       QStringLiteral("它是可用的校验而非占位符 —— 请替换为你的身份认证服务。"),
                       true);
        out << QStringLiteral("def require_token(authorization: str | None = "
                              "Header(default=None)) -> None:");
        out << QStringLiteral("    expected = os.environ.get(\"TCBW_API_TOKEN\")");
        out << QStringLiteral("    if not expected:");
        out << QStringLiteral("        raise HTTPException(status_code=503, detail=\"server "
                              "token is not configured\")");
        out << QStringLiteral("    provided = authorization or \"\"");
        out << QStringLiteral("    token = provided[7:] if provided.startswith(\"Bearer \") "
                              "else \"\"");
        out << QStringLiteral("    if not token or not hmac.compare_digest(token, expected):");
        out << QStringLiteral("        raise HTTPException(status_code=401, detail=\"invalid "
                              "bearer token\")");
    }

    out << QString();
    out << QStringLiteral("@app.get(\"/health\")");
    out << QStringLiteral("async def health() -> dict[str, Any]:");
    out << QStringLiteral("    \"\"\"Health check -- always public. / 健康检查 —— 始终公开。\"\"\"");
    out << QStringLiteral("    return {\"status\": \"ok\", \"name\": ") + quoted(name)
               + QStringLiteral("}");

    for (int i = 0; i < endpoints.size(); ++i) {
        const Endpoint &endpoint = endpoints.at(i);
        const QString method = httpMethodOf(endpoint);
        const QString path = rewritePlaceholders(endpointRoutePath(endpoint),
                                                 QStringLiteral("brace"));
        const QStringList parameters = routeParameters(endpointRoutePath(endpoint));
        const QString handler = handlers.at(i);

        QString signature = QStringLiteral("async def ") + handler + QStringLiteral("(request: "
                                                                                   "Request");
        for (const QString &parameter : parameters)
            signature += QStringLiteral(", ") + parameter + QStringLiteral(": str");
        signature += QStringLiteral(") -> dict[str, Any]:");

        const QString verb = method.toLower();
        const bool knownVerb = verb == QStringLiteral("get") || verb == QStringLiteral("post")
            || verb == QStringLiteral("put") || verb == QStringLiteral("patch")
            || verb == QStringLiteral("delete") || verb == QStringLiteral("head")
            || verb == QStringLiteral("options");

        QString decorator;
        if (knownVerb)
            decorator = QStringLiteral("@app.") + verb + QStringLiteral("(") + quoted(path);
        else
            decorator = QStringLiteral("@app.api_route(") + quoted(path)
                      + QStringLiteral(", methods=[") + quoted(method) + QStringLiteral("]");
        if (request.includeAuthStubs)
            decorator += QStringLiteral(", dependencies=[Depends(require_token)]");
        decorator += QStringLiteral(")");

        out << QString();
        out << comment(request, QStringLiteral("#"),
                       method + QLatin1Char(' ') + path + QStringLiteral(" -- ")
                           + oneLine(endpoint.name),
                       method + QLatin1Char(' ') + path + QStringLiteral(" —— ")
                           + oneLine(endpoint.name));
        out << decorator;
        out << signature;
        out << QStringLiteral("    \"\"\"") + oneLine(endpoint.name) + QStringLiteral("\"\"\"");
        out << QStringLiteral("    # Replace this echo with your real handler.");
        out << QStringLiteral("    # 请把这段回显替换为你真正的处理逻辑。");
        out << QStringLiteral("    return {");
        out << QStringLiteral("        \"ok\": True,");
        out << QStringLiteral("        \"endpoint\": ") + quoted(oneLine(endpoint.name))
                   + QStringLiteral(",");
        out << QStringLiteral("        \"method\": ") + quoted(method) + QStringLiteral(",");
        out << QStringLiteral("        \"path\": ") + quoted(path) + QStringLiteral(",");
        out << QStringLiteral("        \"params\": dict(request.path_params),");
        out << QStringLiteral("        \"query\": dict(request.query_params),");
        out << QStringLiteral("        \"body\": await read_json(request),");
        out << QStringLiteral("    }");
    }

    return joinLines(out);
}

//
//  cloudflare-worker
//
QString workerIndexJs(const ScaffoldRequest &request, const QVector<Endpoint> &endpoints)
{
    const QString name = slugifyAscii(request.projectName, QStringLiteral("tcbw-worker"));
    QStringList out;

    out << comment(request, QStringLiteral("//"),
                   QStringLiteral("Generated by ") + kGeneratedBy + QStringLiteral("."),
                   QStringLiteral("由 ") + kGeneratedBy + QStringLiteral(" 生成。"));
    out << comment(request, QStringLiteral("//"),
                   QStringLiteral("Run: npm install && npx wrangler dev"),
                   QStringLiteral("运行：npm install && npx wrangler dev"));
    out << QString();
    if (request.includeAuthStubs)
        out << QStringLiteral("import { timingSafeEqual } from 'node:crypto';") << QString();

    out << QStringLiteral("const routes = [");
    for (const Endpoint &endpoint : endpoints) {
        const QString method = httpMethodOf(endpoint);
        const QString path = rewritePlaceholders(endpointRoutePath(endpoint),
                                                 QStringLiteral("brace"));
        QStringList parameters;
        const QString pattern = routeRegexSource(endpointRoutePath(endpoint), &parameters);
        QStringList quotedParameters;
        for (const QString &parameter : parameters)
            quotedParameters << quoted(parameter);
        out << QStringLiteral("  {");
        out << QStringLiteral("    method: ") + quoted(method) + QStringLiteral(",");
        out << QStringLiteral("    endpoint: ") + quoted(oneLine(endpoint.name))
                   + QStringLiteral(",");
        out << QStringLiteral("    path: ") + quoted(path) + QStringLiteral(",");
        out << QStringLiteral("    pattern: new RegExp(") + quoted(pattern)
                   + QStringLiteral("),");
        out << QStringLiteral("    params: [") + quotedParameters.join(QStringLiteral(", "))
                   + QStringLiteral("],");
        out << QStringLiteral("  },");
    }
    out << QStringLiteral("];");

    out << QString();
    if (request.includeCors) {
        out << comment(request, QStringLiteral("//"),
                       QStringLiteral("CORS: narrow this to the origins you really serve."),
                       QStringLiteral("跨域：请收窄为你真实服务的来源。"));
        out << QStringLiteral("const corsHeaders = {");
        out << QStringLiteral("  'access-control-allow-origin': '*',");
        out << QStringLiteral("  'access-control-allow-methods': 'GET, POST, PUT, PATCH, DELETE, "
                              "OPTIONS',");
        out << QStringLiteral("  'access-control-allow-headers': 'Content-Type, Authorization',");
        out << QStringLiteral("};");
        out << QString();
    }
    out << QStringLiteral("function json(payload, status = 200) {");
    out << QStringLiteral("  return new Response(JSON.stringify(payload), {");
    out << QStringLiteral("    status,");
    if (request.includeCors) {
        out << QStringLiteral("    headers: { 'content-type': 'application/json; charset=utf-8', "
                              "...corsHeaders },");
    } else {
        out << QStringLiteral("    headers: { 'content-type': 'application/json; charset=utf-8' "
                              "},");
    }
    out << QStringLiteral("  });");
    out << QStringLiteral("}");

    if (request.includeAuthStubs) {
        out << QString();
        out << comment(request, QStringLiteral("//"),
                       QStringLiteral("Authentication stub: a real bearer-token check against "
                                      "the TCBW_API_TOKEN secret."),
                       QStringLiteral("认证桩：针对 TCBW_API_TOKEN 机密的真实 bearer 令牌校验。"),
                       true);
        out << comment(request, QStringLiteral("//"),
                       QStringLiteral("Store it with: npx wrangler secret put TCBW_API_TOKEN"),
                       QStringLiteral("设置方式：npx wrangler secret put TCBW_API_TOKEN"),
                       true);
        out << QStringLiteral("function safeEqual(a, b) {");
        out << QStringLiteral("  const left = new TextEncoder().encode(String(a));");
        out << QStringLiteral("  const right = new TextEncoder().encode(String(b));");
        out << QStringLiteral("  if (left.length !== right.length) return false;");
        out << QStringLiteral("  return timingSafeEqual(left, right);");
        out << QStringLiteral("}");
        out << QString();
        out << QStringLiteral("function authorised(request, env) {");
        out << QStringLiteral("  const expected = env?.TCBW_API_TOKEN;");
        out << QStringLiteral("  if (!expected) return 'unconfigured';");
        out << QStringLiteral("  const header = request.headers.get('authorization') || '';");
        out << QStringLiteral("  const token = header.startsWith('Bearer ') ? header.slice(7) : "
                              "'';");
        out << QStringLiteral("  return token && safeEqual(token, expected) ? null : 'denied';");
        out << QStringLiteral("}");
    }

    out << QString();
    out << QStringLiteral("export default {");
    out << QStringLiteral("  async fetch(request, env, ctx) {");
    out << QStringLiteral("    const url = new URL(request.url);");
    if (request.includeCors) {
        out << QStringLiteral("    if (request.method === 'OPTIONS') {");
        out << QStringLiteral("      return new Response(null, { status: 204, headers: "
                              "corsHeaders });");
        out << QStringLiteral("    }");
    }
    out << QStringLiteral("    if (url.pathname === '/health') {");
    out << QStringLiteral("      return json({ status: 'ok', name: ") + quoted(name)
               + QStringLiteral(" });");
    out << QStringLiteral("    }");
    if (request.includeAuthStubs) {
        out << QString();
        out << QStringLiteral("    const verdict = authorised(request, env);");
        out << QStringLiteral("    if (verdict === 'unconfigured') {");
        out << QStringLiteral("      return json({ error: 'server token is not configured', "
                              "error_zh: '服务器令牌未配置' }, 503);");
        out << QStringLiteral("    }");
        out << QStringLiteral("    if (verdict === 'denied') {");
        out << QStringLiteral("      return json({ error: 'invalid bearer token', error_zh: "
                              "'bearer 令牌无效' }, 401);");
        out << QStringLiteral("    }");
    }
    out << QString();
    out << QStringLiteral("    for (const route of routes) {");
    out << QStringLiteral("      const match = route.pattern.exec(url.pathname);");
    out << QStringLiteral("      if (!match || route.method !== request.method) continue;");
    out << QStringLiteral("      const params = {};");
    out << QStringLiteral("      route.params.forEach((key, index) => {");
    out << QStringLiteral("        params[key] = decodeURIComponent(match[index + 1] ?? '');");
    out << QStringLiteral("      });");
    out << QStringLiteral("      let body = null;");
    out << QStringLiteral("      if (request.method !== 'GET' && request.method !== 'HEAD') {");
    out << QStringLiteral("        body = await request.json().catch(() => null);");
    out << QStringLiteral("      }");
    out << QStringLiteral("      // Replace this echo with your real handler.");
    out << QStringLiteral("      // 请把这段回显替换为你真正的处理逻辑。");
    out << QStringLiteral("      return json({");
    out << QStringLiteral("        ok: true,");
    out << QStringLiteral("        endpoint: route.endpoint,");
    out << QStringLiteral("        method: route.method,");
    out << QStringLiteral("        path: route.path,");
    out << QStringLiteral("        params,");
    out << QStringLiteral("        query: Object.fromEntries(url.searchParams),");
    out << QStringLiteral("        body,");
    out << QStringLiteral("      });");
    out << QStringLiteral("    }");
    out << QString();
    out << QStringLiteral("    return json({ error: 'not found', error_zh: '未找到' }, 404);");
    out << QStringLiteral("  },");
    out << QStringLiteral("};");

    return joinLines(out);
}

//
//  supabase-edge
//
QString supabaseEdgeIndexTs(const ScaffoldRequest &request, const QVector<Endpoint> &endpoints,
                            const QString &functionName)
{
    QStringList out;
    out << comment(request, QStringLiteral("//"),
                   QStringLiteral("Generated by ") + kGeneratedBy + QStringLiteral("."),
                   QStringLiteral("由 ") + kGeneratedBy + QStringLiteral(" 生成。"));
    out << comment(request, QStringLiteral("//"),
                   QStringLiteral("Run: supabase functions serve ") + functionName
                       + QStringLiteral(" --env-file ./supabase/.env.local"),
                   QStringLiteral("运行：supabase functions serve ") + functionName
                       + QStringLiteral(" --env-file ./supabase/.env.local"));
    out << QString();
    if (request.includeCors) {
        out << QStringLiteral("const corsHeaders: Record<string, string> = {");
        out << QStringLiteral("  \"access-control-allow-origin\": \"*\",");
        out << QStringLiteral("  \"access-control-allow-methods\": \"GET, POST, PUT, PATCH, "
                              "DELETE, OPTIONS\",");
        out << QStringLiteral("  \"access-control-allow-headers\": \"Content-Type, "
                              "Authorization\",");
        out << QStringLiteral("};");
    } else {
        out << QStringLiteral("const corsHeaders: Record<string, string> = {};");
    }
    out << QString();
    out << QStringLiteral("type Route = {");
    out << QStringLiteral("  method: string;");
    out << QStringLiteral("  endpoint: string;");
    out << QStringLiteral("  path: string;");
    out << QStringLiteral("  pattern: RegExp;");
    out << QStringLiteral("  params: string[];");
    out << QStringLiteral("};");
    out << QString();
    out << QStringLiteral("const routes: Route[] = [");
    for (const Endpoint &endpoint : endpoints) {
        const QString method = httpMethodOf(endpoint);
        const QString path = rewritePlaceholders(endpointRoutePath(endpoint),
                                                 QStringLiteral("brace"));
        QStringList parameters;
        const QString pattern = routeRegexSource(endpointRoutePath(endpoint), &parameters);
        QStringList quotedParameters;
        for (const QString &parameter : parameters)
            quotedParameters << quoted(parameter);
        out << QStringLiteral("  { method: ") + quoted(method) + QStringLiteral(", endpoint: ")
                   + quoted(oneLine(endpoint.name)) + QStringLiteral(", path: ") + quoted(path)
                   + QStringLiteral(", pattern: new RegExp(") + quoted(pattern)
                   + QStringLiteral("), params: [")
                   + quotedParameters.join(QStringLiteral(", ")) + QStringLiteral("] },");
    }
    out << QStringLiteral("];");
    out << QString();
    out << QStringLiteral("function json(payload: unknown, status = 200): Response {");
    out << QStringLiteral("  return new Response(JSON.stringify(payload), {");
    out << QStringLiteral("    status,");
    out << QStringLiteral("    headers: { ...corsHeaders, \"content-type\": "
                          "\"application/json; charset=utf-8\" },");
    out << QStringLiteral("  });");
    out << QStringLiteral("}");

    if (request.includeAuthStubs) {
        out << QString();
        out << comment(request, QStringLiteral("//"),
                       QStringLiteral("Authentication stub: a real bearer-token check against "
                                      "the TCBW_API_TOKEN secret."),
                       QStringLiteral("认证桩：针对 TCBW_API_TOKEN 机密的真实 bearer 令牌校验。"),
                       true);
        out << comment(request, QStringLiteral("//"),
                       QStringLiteral("Set it with: supabase secrets set TCBW_API_TOKEN=..."),
                       QStringLiteral("设置方式：supabase secrets set TCBW_API_TOKEN=..."),
                       true);
        out << QStringLiteral("function constantTimeEqual(a: string, b: string): boolean {");
        out << QStringLiteral("  const left = new TextEncoder().encode(a);");
        out << QStringLiteral("  const right = new TextEncoder().encode(b);");
        out << QStringLiteral("  let diff = left.length ^ right.length;");
        out << QStringLiteral("  const max = Math.max(left.length, right.length);");
        out << QStringLiteral("  for (let i = 0; i < max; i += 1) {");
        out << QStringLiteral("    diff |= (left[i] ?? 0) ^ (right[i] ?? 0);");
        out << QStringLiteral("  }");
        out << QStringLiteral("  return diff === 0;");
        out << QStringLiteral("}");
        out << QString();
        out << QStringLiteral("function requireToken(request: Request): Response | null {");
        out << QStringLiteral("  const expected = Deno.env.get(\"TCBW_API_TOKEN\");");
        out << QStringLiteral("  if (!expected) {");
        out << QStringLiteral("    return json({ error: \"server token is not configured\", "
                              "error_zh: \"服务器令牌未配置\" }, 503);");
        out << QStringLiteral("  }");
        out << QStringLiteral("  const header = request.headers.get(\"authorization\") ?? \"\";");
        out << QStringLiteral("  const token = header.startsWith(\"Bearer \") ? header.slice(7) "
                              ": \"\";");
        out << QStringLiteral("  if (!token || !constantTimeEqual(token, expected)) {");
        out << QStringLiteral("    return json({ error: \"invalid bearer token\", error_zh: "
                              "\"bearer 令牌无效\" }, 401);");
        out << QStringLiteral("  }");
        out << QStringLiteral("  return null;");
        out << QStringLiteral("}");
    }

    out << QString();
    out << QStringLiteral("Deno.serve(async (request: Request): Promise<Response> => {");
    if (request.includeCors) {
        out << QStringLiteral("  if (request.method === \"OPTIONS\") {");
        out << QStringLiteral("    return new Response(null, { status: 204, headers: corsHeaders "
                              "});");
        out << QStringLiteral("  }");
    }
    out << QStringLiteral("  const url = new URL(request.url);");
    out << QStringLiteral("  const path = url.pathname.replace(/^\\/functions\\/v1\\/[^/]+/, "
                          "\"\") || \"/\";");
    out << QStringLiteral("  if (path === \"/health\") {");
    out << QStringLiteral("    return json({ status: \"ok\", name: ") + quoted(functionName)
               + QStringLiteral(" });");
    out << QStringLiteral("  }");
    if (request.includeAuthStubs) {
        out << QString();
        out << QStringLiteral("  const denied = requireToken(request);");
        out << QStringLiteral("  if (denied) return denied;");
    }
    out << QString();
    out << QStringLiteral("  for (const route of routes) {");
    out << QStringLiteral("    const match = route.pattern.exec(path);");
    out << QStringLiteral("    if (!match || route.method !== request.method) continue;");
    out << QStringLiteral("    const params: Record<string, string> = {};");
    out << QStringLiteral("    route.params.forEach((key, index) => {");
    out << QStringLiteral("      params[key] = decodeURIComponent(match[index + 1] ?? \"\");");
    out << QStringLiteral("    });");
    out << QStringLiteral("    let body: unknown = null;");
    out << QStringLiteral("    if (request.method !== \"GET\" && request.method !== \"HEAD\") {");
    out << QStringLiteral("      body = await request.json().catch(() => null);");
    out << QStringLiteral("    }");
    out << QStringLiteral("    // Replace this echo with your real handler.");
    out << QStringLiteral("    // 请把这段回显替换为你真正的处理逻辑。");
    out << QStringLiteral("    return json({");
    out << QStringLiteral("      ok: true,");
    out << QStringLiteral("      endpoint: route.endpoint,");
    out << QStringLiteral("      method: route.method,");
    out << QStringLiteral("      path: route.path,");
    out << QStringLiteral("      params,");
    out << QStringLiteral("      query: Object.fromEntries(url.searchParams),");
    out << QStringLiteral("      body,");
    out << QStringLiteral("    });");
    out << QStringLiteral("  }");
    out << QString();
    out << QStringLiteral("  return json({ error: \"not found\", error_zh: \"未找到\" }, 404);");
    out << QStringLiteral("});");

    return joinLines(out);
}

//
//  firebase-functions
//
QString firebaseIndexJs(const ScaffoldRequest &request, const QVector<Endpoint> &endpoints)
{
    const QString name = slugifyAscii(request.projectName, QStringLiteral("tcbw-functions"));
    QStringList out;

    out << comment(request, QStringLiteral("//"),
                   QStringLiteral("Generated by ") + kGeneratedBy + QStringLiteral("."),
                   QStringLiteral("由 ") + kGeneratedBy + QStringLiteral(" 生成。"));
    out << comment(request, QStringLiteral("//"),
                   QStringLiteral("Run: firebase emulators:start --only functions"),
                   QStringLiteral("运行：firebase emulators:start --only functions"));
    out << QString();
    out << QStringLiteral("const { onRequest } = require('firebase-functions/v2/https');");
    out << QStringLiteral("const { setGlobalOptions } = require('firebase-functions/v2');");
    if (request.includeAuthStubs)
        out << QStringLiteral("const { timingSafeEqual } = require('node:crypto');");
    out << QString();
    out << QStringLiteral("setGlobalOptions({ region: 'us-central1', maxInstances: 10 });");
    out << QString();
    out << QStringLiteral("const ROUTES = [");
    for (const Endpoint &endpoint : endpoints) {
        const QString method = httpMethodOf(endpoint);
        const QString path = rewritePlaceholders(endpointRoutePath(endpoint),
                                                 QStringLiteral("brace"));
        QStringList parameters;
        const QString pattern = routeRegexSource(endpointRoutePath(endpoint), &parameters);
        QStringList quotedParameters;
        for (const QString &parameter : parameters)
            quotedParameters << quoted(parameter);
        out << QStringLiteral("  { method: ") + quoted(method) + QStringLiteral(", endpoint: ")
                   + quoted(oneLine(endpoint.name)) + QStringLiteral(", path: ") + quoted(path)
                   + QStringLiteral(", pattern: new RegExp(") + quoted(pattern)
                   + QStringLiteral("), params: [")
                   + quotedParameters.join(QStringLiteral(", ")) + QStringLiteral("] },");
    }
    out << QStringLiteral("];");
    out << QString();
    if (request.includeCors) {
        out << comment(request, QStringLiteral("//"),
                       QStringLiteral("CORS: narrow this to the origins you really serve."),
                       QStringLiteral("跨域：请收窄为你真实服务的来源。"));
        out << QStringLiteral("function applyCors(res) {");
        out << QStringLiteral("  res.set('Access-Control-Allow-Origin', "
                              "process.env.CORS_ORIGIN || '*');");
        out << QStringLiteral("  res.set('Access-Control-Allow-Methods', 'GET, POST, PUT, PATCH, "
                              "DELETE, OPTIONS');");
        out << QStringLiteral("  res.set('Access-Control-Allow-Headers', 'Content-Type, "
                              "Authorization');");
        out << QStringLiteral("}");
    }
    if (request.includeAuthStubs) {
        out << QString();
        out << comment(request, QStringLiteral("//"),
                       QStringLiteral("Authentication stub: a real bearer-token check against "
                                      "the TCBW_API_TOKEN secret."),
                       QStringLiteral("认证桩：针对 TCBW_API_TOKEN 机密的真实 bearer 令牌校验。"),
                       true);
        out << QStringLiteral("function authorised(req) {");
        out << QStringLiteral("  const expected = process.env.TCBW_API_TOKEN;");
        out << QStringLiteral("  if (!expected) return 'unconfigured';");
        out << QStringLiteral("  const header = String(req.get('authorization') || '');");
        out << QStringLiteral("  const token = header.startsWith('Bearer ') ? header.slice(7) : "
                              "'';");
        out << QStringLiteral("  const left = Buffer.from(token, 'utf8');");
        out << QStringLiteral("  const right = Buffer.from(expected, 'utf8');");
        out << QStringLiteral("  if (!token || left.length !== right.length) return 'denied';");
        out << QStringLiteral("  return timingSafeEqual(left, right) ? null : 'denied';");
        out << QStringLiteral("}");
    }
    out << QString();
    out << QStringLiteral("exports.api = onRequest((req, res) => {");
    if (request.includeCors) {
        out << QStringLiteral("  applyCors(res);");
        out << QStringLiteral("  if (req.method === 'OPTIONS') {");
        out << QStringLiteral("    res.status(204).send('');");
        out << QStringLiteral("    return;");
        out << QStringLiteral("  }");
    }
    out << QStringLiteral("  const path = (req.path || '/').replace(/\\/+$/, '') || '/';");
    out << QStringLiteral("  if (path === '/health') {");
    out << QStringLiteral("    res.json({ status: 'ok', name: ") + quoted(name)
               + QStringLiteral(" });");
    out << QStringLiteral("    return;");
    out << QStringLiteral("  }");
    if (request.includeAuthStubs) {
        out << QString();
        out << QStringLiteral("  const verdict = authorised(req);");
        out << QStringLiteral("  if (verdict === 'unconfigured') {");
        out << QStringLiteral("    res.status(503).json({ error: 'server token is not "
                              "configured', error_zh: '服务器令牌未配置' });");
        out << QStringLiteral("    return;");
        out << QStringLiteral("  }");
        out << QStringLiteral("  if (verdict === 'denied') {");
        out << QStringLiteral("    res.status(401).json({ error: 'invalid bearer token', "
                              "error_zh: 'bearer 令牌无效' });");
        out << QStringLiteral("    return;");
        out << QStringLiteral("  }");
    }
    out << QString();
    out << QStringLiteral("  for (const route of ROUTES) {");
    out << QStringLiteral("    const match = route.pattern.exec(path);");
    out << QStringLiteral("    if (!match || route.method !== req.method) continue;");
    out << QStringLiteral("    const params = {};");
    out << QStringLiteral("    route.params.forEach((key, index) => {");
    out << QStringLiteral("      params[key] = decodeURIComponent(match[index + 1] || '');");
    out << QStringLiteral("    });");
    out << QStringLiteral("    // Replace this echo with your real handler.");
    out << QStringLiteral("    // 请把这段回显替换为你真正的处理逻辑。");
    out << QStringLiteral("    res.json({");
    out << QStringLiteral("      ok: true,");
    out << QStringLiteral("      endpoint: route.endpoint,");
    out << QStringLiteral("      method: route.method,");
    out << QStringLiteral("      path: route.path,");
    out << QStringLiteral("      params,");
    out << QStringLiteral("      query: req.query || {},");
    out << QStringLiteral("      body: req.body ?? null,");
    out << QStringLiteral("    });");
    out << QStringLiteral("    return;");
    out << QStringLiteral("  }");
    out << QString();
    out << QStringLiteral("  res.status(404).json({ error: 'not found', error_zh: '未找到' });");
    out << QStringLiteral("});");

    return joinLines(out);
}

//
//  go-nethttp
//
QString goMainGo(const ScaffoldRequest &request, const QVector<Endpoint> &endpoints)
{
    QStringList out;
    out << comment(request, QStringLiteral("//"),
                   QStringLiteral("Generated by ") + kGeneratedBy + QStringLiteral("."),
                   QStringLiteral("由 ") + kGeneratedBy + QStringLiteral(" 生成。"));
    out << comment(request, QStringLiteral("//"),
                   QStringLiteral("Run: go mod tidy && go run ."),
                   QStringLiteral("运行：go mod tidy && go run ."));
    out << QString();
    out << QStringLiteral("package main");
    out << QString();
    out << QStringLiteral("import (");
    if (request.includeAuthStubs)
        out << QStringLiteral("\t\"crypto/subtle\"");
    out << QStringLiteral("\t\"encoding/json\"");
    out << QStringLiteral("\t\"log\"");
    out << QStringLiteral("\t\"net/http\"");
    out << QStringLiteral("\t\"os\"");
    //  "strings" is only needed by the bearer-token stub; importing it
    //  otherwise would be a compile error in Go.
    //  只有 bearer 令牌桩需要 "strings"；否则导入它会导致 Go 编译错误。
    if (request.includeAuthStubs)
        out << QStringLiteral("\t\"strings\"");
    out << QStringLiteral(")");
    out << QString();
    out << QStringLiteral("func writeJSON(w http.ResponseWriter, status int, payload any) {");
    out << QStringLiteral("\tw.Header().Set(\"Content-Type\", \"application/json; charset=utf-8\")");
    out << QStringLiteral("\tw.WriteHeader(status)");
    out << QStringLiteral("\t_ = json.NewEncoder(w).Encode(payload)");
    out << QStringLiteral("}");
    out << QString();
    out << QStringLiteral("func readJSON(r *http.Request) any {");
    out << QStringLiteral("\tif r.Body == nil {");
    out << QStringLiteral("\t\treturn nil");
    out << QStringLiteral("\t}");
    out << QStringLiteral("\tvar payload any");
    out << QStringLiteral("\tif err := json.NewDecoder(r.Body).Decode(&payload); err != nil {");
    out << QStringLiteral("\t\treturn nil");
    out << QStringLiteral("\t}");
    out << QStringLiteral("\treturn payload");
    out << QStringLiteral("}");

    if (request.includeCors) {
        out << QString();
        out << comment(request, QStringLiteral("//"),
                       QStringLiteral("CORS: narrow CORS_ORIGIN to the origins you really serve."),
                       QStringLiteral("跨域：请把 CORS_ORIGIN 收窄为你真实服务的来源。"));
        out << QStringLiteral("func withCORS(next http.Handler) http.Handler {");
        out << QStringLiteral("\treturn http.HandlerFunc(func(w http.ResponseWriter, r "
                              "*http.Request) {");
        out << QStringLiteral("\t\torigin := os.Getenv(\"CORS_ORIGIN\")");
        out << QStringLiteral("\t\tif origin == \"\" {");
        out << QStringLiteral("\t\t\torigin = \"*\"");
        out << QStringLiteral("\t\t}");
        out << QStringLiteral("\t\tw.Header().Set(\"Access-Control-Allow-Origin\", origin)");
        out << QStringLiteral("\t\tw.Header().Set(\"Access-Control-Allow-Methods\", \"GET, POST, "
                              "PUT, PATCH, DELETE, OPTIONS\")");
        out << QStringLiteral("\t\tw.Header().Set(\"Access-Control-Allow-Headers\", "
                              "\"Content-Type, Authorization\")");
        out << QStringLiteral("\t\tif r.Method == http.MethodOptions {");
        out << QStringLiteral("\t\t\tw.WriteHeader(http.StatusNoContent)");
        out << QStringLiteral("\t\t\treturn");
        out << QStringLiteral("\t\t}");
        out << QStringLiteral("\t\tnext.ServeHTTP(w, r)");
        out << QStringLiteral("\t})");
        out << QStringLiteral("}");
    }

    if (request.includeAuthStubs) {
        out << QString();
        out << comment(request, QStringLiteral("//"),
                       QStringLiteral("Authentication stub: a real bearer-token check against "
                                      "TCBW_API_TOKEN."),
                       QStringLiteral("认证桩：针对 TCBW_API_TOKEN 的真实 bearer 令牌校验。"),
                       true);
        out << comment(request, QStringLiteral("//"),
                       QStringLiteral("It is a working check, not a placeholder -- replace it "
                                      "with your identity provider."),
                       QStringLiteral("它是可用的校验而非占位符 —— 请替换为你的身份认证服务。"),
                       true);
        out << QStringLiteral("func requireToken(next http.Handler) http.Handler {");
        out << QStringLiteral("\treturn http.HandlerFunc(func(w http.ResponseWriter, r "
                              "*http.Request) {");
        out << QStringLiteral("\t\tif r.URL.Path == \"/health\" {");
        out << QStringLiteral("\t\t\tnext.ServeHTTP(w, r)");
        out << QStringLiteral("\t\t\treturn");
        out << QStringLiteral("\t\t}");
        out << QStringLiteral("\t\texpected := os.Getenv(\"TCBW_API_TOKEN\")");
        out << QStringLiteral("\t\tif expected == \"\" {");
        out << QStringLiteral("\t\t\twriteJSON(w, http.StatusServiceUnavailable, map[string]any{"
                              "\"error\": \"server token is not configured\", \"error_zh\": "
                              "\"服务器令牌未配置\"})");
        out << QStringLiteral("\t\t\treturn");
        out << QStringLiteral("\t\t}");
        out << QStringLiteral("\t\tprovided := strings.TrimPrefix(r.Header.Get(\"Authorization\"), "
                              "\"Bearer \")");
        out << QStringLiteral("\t\tif provided == \"\" || subtle.ConstantTimeCompare([]byte("
                              "provided), []byte(expected)) != 1 {");
        out << QStringLiteral("\t\t\twriteJSON(w, http.StatusUnauthorized, map[string]any{"
                              "\"error\": \"invalid bearer token\", \"error_zh\": \"bearer 令牌无效\"})");
        out << QStringLiteral("\t\t\treturn");
        out << QStringLiteral("\t\t}");
        out << QStringLiteral("\t\tnext.ServeHTTP(w, r)");
        out << QStringLiteral("\t})");
        out << QStringLiteral("}");
    }

    out << QString();
    out << QStringLiteral("func main() {");
    out << QStringLiteral("\tmux := http.NewServeMux()");
    out << QString();
    out << QStringLiteral("\tmux.HandleFunc(\"GET /health\", func(w http.ResponseWriter, r "
                          "*http.Request) {");
    out << QStringLiteral("\t\twriteJSON(w, http.StatusOK, map[string]any{\"status\": \"ok\", "
                          "\"name\": ") + quoted(slugifyAscii(request.projectName,
                                                             QStringLiteral("tcbw-backend")))
               + QStringLiteral("})");
    out << QStringLiteral("\t})");

    for (const Endpoint &endpoint : endpoints) {
        const QString method = httpMethodOf(endpoint);
        const QString path = rewritePlaceholders(endpointRoutePath(endpoint),
                                                 QStringLiteral("brace"));
        const QStringList parameters = routeParameters(endpointRoutePath(endpoint));

        out << QString();
        out << comment(request, QStringLiteral("\t//"),
                       method + QLatin1Char(' ') + path + QStringLiteral(" -- ")
                           + oneLine(endpoint.name),
                       method + QLatin1Char(' ') + path + QStringLiteral(" —— ")
                           + oneLine(endpoint.name));
        out << QStringLiteral("\tmux.HandleFunc(") + quoted(method + QLatin1Char(' ') + path)
                   + QStringLiteral(", func(w http.ResponseWriter, r *http.Request) {");
        if (parameters.isEmpty()) {
            out << QStringLiteral("\t\tparams := map[string]string{}");
        } else {
            QStringList pairs;
            for (const QString &parameter : parameters)
                pairs << quoted(parameter) + QStringLiteral(": r.PathValue(")
                             + quoted(parameter) + QStringLiteral(")");
            out << QStringLiteral("\t\tparams := map[string]string{")
                       + pairs.join(QStringLiteral(", ")) + QStringLiteral("}");
        }
        out << QStringLiteral("\t\t// Replace this echo with your real handler.");
        out << QStringLiteral("\t\t// 请把这段回显替换为你真正的处理逻辑。");
        out << QStringLiteral("\t\twriteJSON(w, http.StatusOK, map[string]any{");
        out << QStringLiteral("\t\t\t\"ok\": true,");
        out << QStringLiteral("\t\t\t\"endpoint\": ") + quoted(oneLine(endpoint.name))
                   + QStringLiteral(",");
        out << QStringLiteral("\t\t\t\"method\": ") + quoted(method) + QStringLiteral(",");
        out << QStringLiteral("\t\t\t\"path\": ") + quoted(path) + QStringLiteral(",");
        out << QStringLiteral("\t\t\t\"params\": params,");
        out << QStringLiteral("\t\t\t\"query\": r.URL.Query(),");
        if (methodCarriesBody(method))
            out << QStringLiteral("\t\t\t\"body\": readJSON(r),");
        else
            out << QStringLiteral("\t\t\t\"body\": nil,");
        out << QStringLiteral("\t\t})");
        out << QStringLiteral("\t})");
    }

    out << QString();
    out << QStringLiteral("\thandler := http.Handler(mux)");
    if (request.includeAuthStubs)
        out << QStringLiteral("\thandler = requireToken(handler)");
    if (request.includeCors)
        out << QStringLiteral("\thandler = withCORS(handler)");
    out << QString();
    out << QStringLiteral("\tport := os.Getenv(\"PORT\")");
    out << QStringLiteral("\tif port == \"\" {");
    out << QStringLiteral("\t\tport = \"8080\"");
    out << QStringLiteral("\t}");
    out << QStringLiteral("\tlog.Printf(\"[tcbw] listening on http://127.0.0.1:%s\", port)");
    out << QStringLiteral("\tif err := http.ListenAndServe(\"127.0.0.1:\"+port, handler); err != "
                          "nil {");
    out << QStringLiteral("\t\tlog.Fatal(err)");
    out << QStringLiteral("\t}");
    out << QStringLiteral("}");

    return joinLines(out);
}

//
//  static-json
//
QString staticJsonServerJs(const ScaffoldRequest &request, const QVector<Endpoint> &endpoints)
{
    QStringList out;
    out << comment(request, QStringLiteral("//"),
                   QStringLiteral("Generated by ") + kGeneratedBy + QStringLiteral("."),
                   QStringLiteral("由 ") + kGeneratedBy + QStringLiteral(" 生成。"));
    out << comment(request, QStringLiteral("//"),
                   QStringLiteral("Dependency-free Node server: it serves db.json as a REST API."),
                   QStringLiteral("零依赖 Node 服务器：把 db.json 作为 REST API 提供。"));
    out << comment(request, QStringLiteral("//"),
                   QStringLiteral("Run: node server.js"),
                   QStringLiteral("运行：node server.js"));
    out << QString();
    out << QStringLiteral("import http from 'node:http';");
    out << QStringLiteral("import { readFileSync, writeFileSync } from 'node:fs';");
    out << QStringLiteral("import { dirname, join } from 'node:path';");
    out << QStringLiteral("import { fileURLToPath } from 'node:url';");
    if (request.includeAuthStubs)
        out << QStringLiteral("import { timingSafeEqual } from 'node:crypto';");
    out << QString();
    out << QStringLiteral("const here = dirname(fileURLToPath(import.meta.url));");
    out << QStringLiteral("const dbPath = join(here, 'db.json');");
    out << QStringLiteral("const db = JSON.parse(readFileSync(dbPath, 'utf8'));");
    out << QStringLiteral("const port = Number(process.env.PORT || 3001);");
    out << QString();
    out << comment(request, QStringLiteral("//"),
                   QStringLiteral("Every top level array in db.json becomes a collection; the "
                                  "routes below mirror the endpoints of the project."),
                   QStringLiteral("db.json 中每个顶层数组都会成为一个集合；下面的路由与工程"
                                  "端点一一对应。"));
    out << QStringLiteral("const collections = new Set(");
    out << QStringLiteral("  Object.entries(db)");
    out << QStringLiteral("    .filter(([, value]) => Array.isArray(value))");
    out << QStringLiteral("    .map(([key]) => key),");
    out << QStringLiteral(");");
    out << QString();
    if (request.includeCors) {
        out << QStringLiteral("const CORS_ORIGIN = process.env.CORS_ORIGIN || '*';");
        out << QString();
    }
    out << QStringLiteral("function send(res, status, payload) {");
    out << QStringLiteral("  const body = payload === undefined ? '' : JSON.stringify(payload);");
    out << QStringLiteral("  res.writeHead(status, {");
    out << QStringLiteral("    'content-type': 'application/json; charset=utf-8',");
    out << QStringLiteral("    'content-length': Buffer.byteLength(body),");
    out << QStringLiteral("    'cache-control': 'no-store',");
    if (request.includeCors)
        out << QStringLiteral("    'access-control-allow-origin': CORS_ORIGIN,");
    out << QStringLiteral("    'access-control-allow-headers': 'Content-Type, Authorization',");
    out << QStringLiteral("    'access-control-allow-methods': 'GET, POST, DELETE, OPTIONS',");
    out << QStringLiteral("  });");
    out << QStringLiteral("  res.end(body);");
    out << QStringLiteral("}");
    out << QString();
    out << QStringLiteral("function persist() {");
    out << QStringLiteral("  writeFileSync(dbPath, `${JSON.stringify(db, null, 2)}\\n`, 'utf8');");
    out << QStringLiteral("}");
    out << QString();
    out << QStringLiteral("async function readBody(req) {");
    out << QStringLiteral("  const chunks = [];");
    out << QStringLiteral("  for await (const chunk of req) chunks.push(chunk);");
    out << QStringLiteral("  if (chunks.length === 0) return null;");
    out << QStringLiteral("  try {");
    out << QStringLiteral("    return JSON.parse(Buffer.concat(chunks).toString('utf8'));");
    out << QStringLiteral("  } catch {");
    out << QStringLiteral("    return null;");
    out << QStringLiteral("  }");
    out << QStringLiteral("}");

    if (request.includeAuthStubs) {
        out << QString();
        out << comment(request, QStringLiteral("//"),
                       QStringLiteral("Authentication stub: a real bearer-token check against "
                                      "TCBW_API_TOKEN."),
                       QStringLiteral("认证桩：针对 TCBW_API_TOKEN 的真实 bearer 令牌校验。"),
                       true);
        out << QStringLiteral("function authorised(req, res) {");
        out << QStringLiteral("  const expected = process.env.TCBW_API_TOKEN;");
        out << QStringLiteral("  if (!expected) {");
        out << QStringLiteral("    send(res, 503, { error: 'server token is not configured', "
                              "error_zh: '服务器令牌未配置' });");
        out << QStringLiteral("    return false;");
        out << QStringLiteral("  }");
        out << QStringLiteral("  const header = String(req.headers.authorization || '');");
        out << QStringLiteral("  const token = header.startsWith('Bearer ') ? header.slice(7) : "
                              "'';");
        out << QStringLiteral("  const left = Buffer.from(token, 'utf8');");
        out << QStringLiteral("  const right = Buffer.from(expected, 'utf8');");
        out << QStringLiteral("  if (!token || left.length !== right.length || "
                              "!timingSafeEqual(left, right)) {");
        out << QStringLiteral("    send(res, 401, { error: 'invalid bearer token', error_zh: "
                              "'bearer 令牌无效' });");
        out << QStringLiteral("    return false;");
        out << QStringLiteral("  }");
        out << QStringLiteral("  return true;");
        out << QStringLiteral("}");
    }

    out << QString();
    out << QStringLiteral("const server = http.createServer(async (req, res) => {");
    out << QStringLiteral("  if (req.method === 'OPTIONS') {");
    out << QStringLiteral("    return send(res, 204, undefined);");
    out << QStringLiteral("  }");
    out << QStringLiteral("  const url = new URL(req.url ?? '/', `http://${req.headers.host ?? "
                          "'127.0.0.1'}`);");
    out << QStringLiteral("  const segments = url.pathname.split('/').filter(Boolean);");
    out << QStringLiteral("  if (segments.length === 0 || segments[0] === 'health') {");
    out << QStringLiteral("    return send(res, 200, { status: 'ok', collections: "
                          "[...collections] });");
    out << QStringLiteral("  }");
    if (request.includeAuthStubs) {
        out << QStringLiteral("  if (!authorised(req, res)) return undefined;");
    }
    out << QStringLiteral("  const name = segments[0];");
    out << QStringLiteral("  const collection = db[name];");
    out << QStringLiteral("  if (!collections.has(name) || !Array.isArray(collection)) {");
    out << QStringLiteral("    return send(res, 404, { error: 'not found', error_zh: '未找到', "
                          "collections: [...collections] });");
    out << QStringLiteral("  }");
    out << QStringLiteral("  if (req.method === 'GET' || req.method === 'HEAD') {");
    out << QStringLiteral("    const id = segments[1];");
    out << QStringLiteral("    if (!id) return send(res, 200, collection);");
    out << QStringLiteral("    const found = collection.find((item) => String(item?.id) === "
                          "String(id));");
    out << QStringLiteral("    return found ? send(res, 200, found) : send(res, 404, { error: 'not "
                          "found', error_zh: '未找到' });");
    out << QStringLiteral("  }");
    out << QStringLiteral("  if (req.method === 'POST') {");
    out << QStringLiteral("    const payload = await readBody(req);");
    out << QStringLiteral("    if (payload === null) {");
    out << QStringLiteral("      return send(res, 400, { error: 'invalid JSON body', error_zh: "
                          "'JSON 正文不合法' });");
    out << QStringLiteral("    }");
    out << QStringLiteral("    const nextId = collection.reduce((max, item) => Math.max(max, "
                          "Number(item?.id) || 0), 0) + 1;");
    out << QStringLiteral("    const record = { id: nextId, ...payload };");
    out << QStringLiteral("    collection.push(record);");
    out << QStringLiteral("    persist();");
    out << QStringLiteral("    return send(res, 201, record);");
    out << QStringLiteral("  }");
    out << QStringLiteral("  if (req.method === 'DELETE') {");
    out << QStringLiteral("    const id = segments[1];");
    out << QStringLiteral("    const index = collection.findIndex((item) => String(item?.id) === "
                          "String(id));");
    out << QStringLiteral("    if (index < 0) return send(res, 404, { error: 'not found', "
                          "error_zh: '未找到' });");
    out << QStringLiteral("    collection.splice(index, 1);");
    out << QStringLiteral("    persist();");
    out << QStringLiteral("    return send(res, 204, undefined);");
    out << QStringLiteral("  }");
    out << QStringLiteral("  return send(res, 405, { error: 'method not allowed', error_zh: "
                          "'方法不允许' });");
    out << QStringLiteral("});");
    out << QString();
    out << QStringLiteral("server.listen(port, '127.0.0.1', () => {");
    out << QStringLiteral("  console.log(`[tcbw] json server on http://127.0.0.1:${port}`);");
    out << QStringLiteral("  if (collections.size > 0) {");
    out << QStringLiteral("    console.log(`[tcbw] collections: ${[...collections].join(', ')}`);");
    out << QStringLiteral("  }");
    out << QStringLiteral("});");
    Q_UNUSED(endpoints);

    return joinLines(out);
}

QJsonObject staticJsonDb(const ScaffoldRequest &request, const QVector<Endpoint> &endpoints)
{
    QJsonObject db;
    QStringList keys;

    for (const Endpoint &endpoint : endpoints) {
        const QString key = slugifyAscii(endpoint.name, QStringLiteral("records"));
        if (keys.contains(key))
            continue;
        keys << key;

        QJsonObject record;
        record.insert(QStringLiteral("id"), 1);
        record.insert(QStringLiteral("endpoint"), oneLine(endpoint.name));
        record.insert(QStringLiteral("method"), httpMethodOf(endpoint));
        record.insert(QStringLiteral("path"), endpointRoutePath(endpoint));
        record.insert(QStringLiteral("note"), QStringLiteral("Replace this seed record with real "
                                                             "data. / 请用真实数据替换此种子记录。"));

        QJsonArray array;
        array.append(record);
        db.insert(key, array);
    }

    QJsonObject meta;
    meta.insert(QStringLiteral("generatedBy"), kGeneratedBy);
    meta.insert(QStringLiteral("targetId"), request.targetId);
    meta.insert(QStringLiteral("endpointCount"), static_cast<int>(endpoints.size()));
    db.insert(QStringLiteral("meta"), meta);
    return db;
}

} // namespace

// ---------------------------------------------------------------------------
//  ScaffoldRequest
//  脚手架请求
// ---------------------------------------------------------------------------
QJsonObject ScaffoldRequest::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("targetId"), targetId);
    obj.insert(QStringLiteral("projectName"), projectName);
    obj.insert(QStringLiteral("outputDirectory"), outputDirectory);
    obj.insert(QStringLiteral("endpoints"), endpoints.toJson());
    obj.insert(QStringLiteral("includeCors"), includeCors);
    obj.insert(QStringLiteral("includeAuthStubs"), includeAuthStubs);
    obj.insert(QStringLiteral("includeDocker"), includeDocker);
    obj.insert(QStringLiteral("includeReadme"), includeReadme);
    obj.insert(QStringLiteral("includeEnvExample"), includeEnvExample);
    obj.insert(QStringLiteral("nodeVersion"), nodeVersion);
    obj.insert(QStringLiteral("pythonVersion"), pythonVersion);
    obj.insert(QStringLiteral("chineseComments"), chineseComments);
    return obj;
}

// ---------------------------------------------------------------------------
//  BackendScaffold
//  脚手架生成器
// ---------------------------------------------------------------------------
EmittedFile BackendScaffold::textFile(const QString &path, const QString &contents,
                                      const QString &mime)
{
    EmittedFile file;
    file.relativePath = path;
    file.contents = contents;
    file.mimeType = mime;
    file.binary = false;
    return file;
}

EmitResult BackendScaffold::generate(const ScaffoldRequest &request) const
{
    const QString targetId = request.targetId.trimmed();

    if (targetId == QStringLiteral("node-express"))
        return generateNodeExpress(request);
    if (targetId == QStringLiteral("python-fastapi"))
        return generatePythonFastApi(request);
    if (targetId == QStringLiteral("cloudflare-workers")
        || targetId == QStringLiteral("cloudflare-worker"))
        return generateCloudflareWorker(request);
    if (targetId == QStringLiteral("supabase") || targetId == QStringLiteral("supabase-edge"))
        return generateSupabaseEdge(request);
    if (targetId == QStringLiteral("firebase-functions"))
        return generateFirebaseFunctions(request);
    if (targetId == QStringLiteral("go-nethttp"))
        return generateGoNetHttp(request);
    if (targetId == QStringLiteral("static-json"))
        return generateStaticJsonServer(request);

    //  An unimplemented target still produces a runnable server; it is reported
    //  as a warning, never as a failure.
    //  未实现的目标仍然会生成可运行的服务器；它只作为警告上报，绝不作为失败。
    EmitResult result = generateStaticJsonServer(request);
    result.ok = true;
    result.warnings.append(
        QStringLiteral("No dedicated generator for backend target \"%1\"; a "
                       "dependency-free static JSON server was generated instead. / "
                       "后端目标 \"%1\" 没有专用生成器；已改为生成零依赖的静态 JSON 服务器。")
            .arg(targetId.isEmpty() ? QStringLiteral("(empty)") : targetId));
    return result;
}

QStringList BackendScaffold::plannedFiles(const ScaffoldRequest &request) const
{
    //  Derived from the real generator output so the two can never disagree.
    //  由真实的生成结果推导，因此两者永远不会不一致。
    const EmitResult result = generate(request);
    QStringList paths;
    paths.reserve(result.files.size());
    for (const EmittedFile &file : result.files)
        paths.append(file.relativePath);
    return paths;
}

QString BackendScaffold::describe(const BackendTarget &target, bool chinese)
{
    const QString id = target.id;
    if (id == QStringLiteral("node-express")) {
        return chinese ? QStringLiteral("Express 服务器：每个端点一条路由，含 CORS、/health、"
                                        "bearer 令牌校验桩与 Dockerfile。")
                       : QStringLiteral("Express server: one route per endpoint, CORS, /health, "
                                        "a bearer-token check and a Dockerfile.");
    }
    if (id == QStringLiteral("python-fastapi")) {
        return chinese ? QStringLiteral("FastAPI 应用：异步路由、CORS、/health，并自动生成 "
                                        "OpenAPI 文档。")
                       : QStringLiteral("FastAPI app: async routes, CORS, /health and automatic "
                                        "OpenAPI docs.");
    }
    if (id == QStringLiteral("cloudflare-workers")) {
        return chinese ? QStringLiteral("Cloudflare Worker：wrangler 配置、fetch 路由表与 "
                                        "CORS 响应头。")
                       : QStringLiteral("Cloudflare Worker: wrangler config, a fetch router and "
                                        "CORS headers.");
    }
    if (id == QStringLiteral("supabase") || id == QStringLiteral("supabase-edge")) {
        return chinese ? QStringLiteral("Supabase Edge Function：TypeScript 路由与 "
                                        "supabase/config.toml。")
                       : QStringLiteral("Supabase Edge Function: a TypeScript router plus "
                                        "supabase/config.toml.");
    }
    if (id == QStringLiteral("firebase-functions")) {
        return chinese ? QStringLiteral("Firebase Functions：onRequest 路由、firebase.json 与 "
                                        "模拟器配置。")
                       : QStringLiteral("Firebase Functions: an onRequest router, firebase.json "
                                        "and emulator config.");
    }
    if (id == QStringLiteral("go-nethttp")) {
        return chinese ? QStringLiteral("Go 标准库服务器：ServeMux 路由、CORS 中间件与 "
                                        "/health。")
                       : QStringLiteral("Go standard-library server: ServeMux routes, CORS "
                                        "middleware and /health.");
    }
    if (id == QStringLiteral("static-json")) {
        return chinese ? QStringLiteral("零依赖 Node 服务器：以 db.json 作为 REST API，"
                                        "无需安装任何包。")
                       : QStringLiteral("Dependency-free Node server: db.json served as a REST "
                                        "API, no packages to install.");
    }
    return chinese ? (QStringLiteral("暂无专用生成器，将改为生成零依赖的静态 JSON 服务器。"))
                   : (QStringLiteral("No dedicated generator yet; a dependency-free static JSON "
                                     "server is generated instead."));
}

// ---------------------------------------------------------------------------
//  Generators
//  生成器
// ---------------------------------------------------------------------------
EmitResult BackendScaffold::generateNodeExpress(const ScaffoldRequest &request) const
{
    EmitResult result;
    result.ok = true;

    const QString name = slugifyAscii(request.projectName, QStringLiteral("tcbw-backend"));
    const QVector<Endpoint> endpoints = activeEndpoints(request);

    QJsonObject pkg;
    pkg.insert(QStringLiteral("name"), name);
    pkg.insert(QStringLiteral("version"), QStringLiteral("0.1.0"));
    pkg.insert(QStringLiteral("private"), true);
    pkg.insert(QStringLiteral("type"), QStringLiteral("module"));
    pkg.insert(QStringLiteral("description"),
               QStringLiteral("Generated by ") + kGeneratedBy);
    pkg.insert(QStringLiteral("main"), QStringLiteral("server.js"));
    QJsonObject engines;
    engines.insert(QStringLiteral("node"), QStringLiteral(">=") + nodeMajorVersion(request));
    pkg.insert(QStringLiteral("engines"), engines);
    QJsonObject scripts;
    scripts.insert(QStringLiteral("start"), QStringLiteral("node server.js"));
    scripts.insert(QStringLiteral("dev"), QStringLiteral("node --watch server.js"));
    pkg.insert(QStringLiteral("scripts"), scripts);
    QJsonObject dependencies;
    dependencies.insert(QStringLiteral("cors"), QStringLiteral("^2.8.5"));
    dependencies.insert(QStringLiteral("express"), QStringLiteral("^4.21.2"));
    pkg.insert(QStringLiteral("dependencies"), dependencies);

    result.files.append(textFile(QStringLiteral("package.json"), jsonText(pkg),
                                 QStringLiteral("application/json")));
    result.files.append(textFile(QStringLiteral("server.js"),
                                 expressServerJs(request, endpoints),
                                 QStringLiteral("text/javascript")));

    if (request.includeEnvExample) {
        result.files.append(textFile(QStringLiteral(".env.example"),
                                     envExampleText(request, {}),
                                     QStringLiteral("text/plain")));
    }
    if (request.includeReadme) {
        QStringList notes;
        notes << QStringLiteral("Start command: `npm start` (or `npm run dev` with file watching).");
        notes << QStringLiteral("The server listens on 127.0.0.1 by default; change the host in "
                                "`server.js` when you deploy.");
        result.files.append(textFile(
            QStringLiteral("README.md"),
            readmeText(request, BackendConnector::findTarget(request.targetId), endpoints,
                       {QStringLiteral("npm install"),
                        QStringLiteral("cp .env.example .env"),
                        QStringLiteral("npm start")},
                       notes),
            QStringLiteral("text/markdown")));
    }
    if (request.includeDocker) {
        result.files.append(textFile(QStringLiteral("Dockerfile"),
                                     dockerfileForNode(request,
                                                       QStringLiteral("[\"node\", \"server.js\"]")),
                                     QStringLiteral("text/plain")));
        QStringList ignore;
        ignore << QStringLiteral("node_modules");
        ignore << QStringLiteral(".env");
        ignore << QStringLiteral("npm-debug.log*");
        result.files.append(textFile(QStringLiteral(".dockerignore"),
                                     joinLines(ignore), QStringLiteral("text/plain")));
    }

    return result;
}

EmitResult BackendScaffold::generatePythonFastApi(const ScaffoldRequest &request) const
{
    EmitResult result;
    result.ok = true;

    const QVector<Endpoint> endpoints = activeEndpoints(request);

    QStringList requirements;
    requirements << QStringLiteral("# Generated by ") + kGeneratedBy;
    requirements << QStringLiteral("fastapi==0.115.6");
    requirements << QStringLiteral("uvicorn[standard]==0.34.0");
    result.files.append(textFile(QStringLiteral("requirements.txt"), joinLines(requirements),
                                 QStringLiteral("text/plain")));

    result.files.append(textFile(QStringLiteral("main.py"),
                                 fastApiMainPy(request, endpoints),
                                 QStringLiteral("text/x-python")));

    if (request.includeEnvExample) {
        result.files.append(textFile(QStringLiteral(".env.example"),
                                     envExampleText(request, {}),
                                     QStringLiteral("text/plain")));
    }
    if (request.includeReadme) {
        QStringList notes;
        notes << QStringLiteral("Interactive API docs: http://127.0.0.1:8000/docs");
        notes << QStringLiteral("Python version used for the Dockerfile: ")
                     + pythonVersionOr(request, QStringLiteral("3.12"));
        result.files.append(textFile(
            QStringLiteral("README.md"),
            readmeText(request, BackendConnector::findTarget(request.targetId), endpoints,
                       {QStringLiteral("python -m venv .venv"),
                        QStringLiteral(". .venv/bin/activate   # Windows: .venv\\Scripts\\activate"),
                        QStringLiteral("pip install -r requirements.txt"),
                        QStringLiteral("uvicorn main:app --reload --host 127.0.0.1 --port 8000")},
                       notes),
            QStringLiteral("text/markdown")));
    }
    if (request.includeDocker) {
        const QString version = pythonVersionOr(request, QStringLiteral("3.12"));
        QStringList lines;
        lines << comment(request, QStringLiteral("#"),
                         QStringLiteral("Generated by ") + kGeneratedBy,
                         QStringLiteral("由 ") + kGeneratedBy + QStringLiteral(" 生成"));
        lines << QStringLiteral("FROM python:") + version + QStringLiteral("-slim");
        lines << QStringLiteral("WORKDIR /app");
        lines << QStringLiteral("COPY requirements.txt ./");
        lines << QStringLiteral("RUN pip install --no-cache-dir -r requirements.txt");
        lines << QStringLiteral("COPY . .");
        lines << QStringLiteral("ENV PORT=8000");
        lines << QStringLiteral("EXPOSE 8000");
        lines << QStringLiteral("CMD [\"uvicorn\", \"main:app\", \"--host\", \"0.0.0.0\", "
                                "\"--port\", \"8000\"]");
        result.files.append(textFile(QStringLiteral("Dockerfile"), joinLines(lines),
                                     QStringLiteral("text/plain")));
    }

    return result;
}

EmitResult BackendScaffold::generateCloudflareWorker(const ScaffoldRequest &request) const
{
    EmitResult result;
    result.ok = true;

    const QString name = slugifyAscii(request.projectName, QStringLiteral("tcbw-worker"));
    const QVector<Endpoint> endpoints = activeEndpoints(request);

    QStringList wrangler;
    wrangler << comment(request, QStringLiteral("#"),
                        QStringLiteral("Generated by ") + kGeneratedBy,
                        QStringLiteral("由 ") + kGeneratedBy + QStringLiteral(" 生成"));
    wrangler << QStringLiteral("name = ") + quoted(name);
    wrangler << QStringLiteral("main = \"src/index.js\"");
    wrangler << QStringLiteral("compatibility_date = \"2025-06-01\"");
    wrangler << QStringLiteral("workers_dev = true");
    wrangler << QString();
    wrangler << comment(request, QStringLiteral("#"),
                        QStringLiteral("Secrets are never written here: use "
                                       "`npx wrangler secret put TCBW_API_TOKEN`."),
                        QStringLiteral("机密绝不写在此处：请使用 "
                                       "`npx wrangler secret put TCBW_API_TOKEN`。"));
    result.files.append(textFile(QStringLiteral("wrangler.toml"), joinLines(wrangler),
                                 QStringLiteral("text/plain")));

    result.files.append(textFile(QStringLiteral("src/index.js"),
                                 workerIndexJs(request, endpoints),
                                 QStringLiteral("text/javascript")));

    QJsonObject pkg;
    pkg.insert(QStringLiteral("name"), name);
    pkg.insert(QStringLiteral("version"), QStringLiteral("0.1.0"));
    pkg.insert(QStringLiteral("private"), true);
    pkg.insert(QStringLiteral("type"), QStringLiteral("module"));
    pkg.insert(QStringLiteral("description"), QStringLiteral("Generated by ") + kGeneratedBy);
    QJsonObject scripts;
    scripts.insert(QStringLiteral("dev"), QStringLiteral("wrangler dev"));
    scripts.insert(QStringLiteral("deploy"), QStringLiteral("wrangler deploy"));
    pkg.insert(QStringLiteral("scripts"), scripts);
    QJsonObject devDependencies;
    devDependencies.insert(QStringLiteral("wrangler"), QStringLiteral("^3.99.0"));
    pkg.insert(QStringLiteral("devDependencies"), devDependencies);
    result.files.append(textFile(QStringLiteral("package.json"), jsonText(pkg),
                                 QStringLiteral("application/json")));

    if (request.includeEnvExample) {
        result.files.append(textFile(QStringLiteral(".env.example"),
                                     envExampleText(request, {}),
                                     QStringLiteral("text/plain")));
    }
    if (request.includeReadme) {
        QStringList notes;
        notes << QStringLiteral("Local dev server: http://127.0.0.1:8787");
        notes << QStringLiteral("Deploy with `npx wrangler deploy`.");
        result.files.append(textFile(
            QStringLiteral("README.md"),
            readmeText(request, BackendConnector::findTarget(request.targetId), endpoints,
                       {QStringLiteral("npm install"),
                        QStringLiteral("npx wrangler dev --ip 127.0.0.1 --port 8787"),
                        QStringLiteral("npx wrangler deploy")},
                       notes),
            QStringLiteral("text/markdown")));
    }
    if (request.includeDocker) {
        QStringList lines;
        lines << comment(request, QStringLiteral("#"),
                         QStringLiteral("Generated by ") + kGeneratedBy,
                         QStringLiteral("由 ") + kGeneratedBy + QStringLiteral(" 生成"));
        lines << QStringLiteral("FROM node:") + nodeMajorVersion(request) + QStringLiteral("-alpine");
        lines << QStringLiteral("WORKDIR /app");
        lines << QStringLiteral("COPY package*.json ./");
        lines << QStringLiteral("RUN npm install");
        lines << QStringLiteral("COPY . .");
        lines << QStringLiteral("EXPOSE 8787");
        lines << QStringLiteral("CMD [\"npx\", \"wrangler\", \"dev\", \"--ip\", \"0.0.0.0\", "
                                "\"--port\", \"8787\"]");
        result.files.append(textFile(QStringLiteral("Dockerfile"), joinLines(lines),
                                     QStringLiteral("text/plain")));
    }

    return result;
}

EmitResult BackendScaffold::generateSupabaseEdge(const ScaffoldRequest &request) const
{
    EmitResult result;
    result.ok = true;

    const QString functionName = slugifyAscii(request.projectName, QStringLiteral("tcbw-api"));
    const QVector<Endpoint> endpoints = activeEndpoints(request);

    result.files.append(textFile(
        QStringLiteral("supabase/functions/") + functionName + QStringLiteral("/index.ts"),
        supabaseEdgeIndexTs(request, endpoints, functionName),
        QStringLiteral("text/typescript")));

    QStringList config;
    config << comment(request, QStringLiteral("#"),
                      QStringLiteral("Generated by ") + kGeneratedBy,
                      QStringLiteral("由 ") + kGeneratedBy + QStringLiteral(" 生成"));
    config << QStringLiteral("project_id = ") + quoted(functionName);
    config << QString();
    config << QStringLiteral("[functions.") + functionName + QStringLiteral("]");
    config << comment(request, QStringLiteral("#"),
                      QStringLiteral("The function performs its own bearer-token check against "
                                     "TCBW_API_TOKEN."),
                      QStringLiteral("该函数使用 TCBW_API_TOKEN 自行完成 bearer 令牌校验。"));
    config << QStringLiteral("verify_jwt = false");
    result.files.append(textFile(QStringLiteral("supabase/config.toml"), joinLines(config),
                                 QStringLiteral("text/plain")));

    if (request.includeEnvExample) {
        result.files.append(textFile(QStringLiteral(".env.example"),
                                     envExampleText(request, {}),
                                     QStringLiteral("text/plain")));
    }
    if (request.includeReadme) {
        QStringList notes;
        notes << QStringLiteral("Function path: `/functions/v1/") + functionName
                     + QStringLiteral("/<endpoint>`.");
        notes << QStringLiteral("Set secrets with `supabase secrets set TCBW_API_TOKEN=...`.");
        result.files.append(textFile(
            QStringLiteral("README.md"),
            readmeText(request, BackendConnector::findTarget(request.targetId), endpoints,
                       {QStringLiteral("supabase start"),
                        QStringLiteral("supabase functions serve ") + functionName,
                        QStringLiteral("supabase functions deploy ") + functionName},
                       notes),
            QStringLiteral("text/markdown")));
    }
    if (request.includeDocker) {
        QStringList lines;
        lines << comment(request, QStringLiteral("#"),
                         QStringLiteral("Generated by ") + kGeneratedBy,
                         QStringLiteral("由 ") + kGeneratedBy + QStringLiteral(" 生成"));
        lines << QStringLiteral("FROM denoland/deno:2.1.4");
        lines << QStringLiteral("WORKDIR /app");
        lines << QStringLiteral("COPY . .");
        lines << QStringLiteral("EXPOSE 8000");
        lines << QStringLiteral("CMD [\"deno\", \"run\", \"--allow-net\", \"--allow-env\", "
                                "\"supabase/functions/") + functionName
                     + QStringLiteral("/index.ts\"]");
        result.files.append(textFile(QStringLiteral("Dockerfile"), joinLines(lines),
                                     QStringLiteral("text/plain")));
    }

    return result;
}

EmitResult BackendScaffold::generateFirebaseFunctions(const ScaffoldRequest &request) const
{
    EmitResult result;
    result.ok = true;

    const QVector<Endpoint> endpoints = activeEndpoints(request);
    const QString name = slugifyAscii(request.projectName, QStringLiteral("tcbw-functions"));

    QJsonObject pkg;
    pkg.insert(QStringLiteral("name"), QStringLiteral("functions"));
    pkg.insert(QStringLiteral("private"), true);
    pkg.insert(QStringLiteral("description"), QStringLiteral("Generated by ") + kGeneratedBy);
    pkg.insert(QStringLiteral("main"), QStringLiteral("index.js"));
    QJsonObject engines;
    engines.insert(QStringLiteral("node"), nodeMajorVersion(request));
    pkg.insert(QStringLiteral("engines"), engines);
    QJsonObject dependencies;
    dependencies.insert(QStringLiteral("firebase-admin"), QStringLiteral("^13.0.2"));
    dependencies.insert(QStringLiteral("firebase-functions"), QStringLiteral("^6.1.2"));
    pkg.insert(QStringLiteral("dependencies"), dependencies);
    result.files.append(textFile(QStringLiteral("functions/package.json"), jsonText(pkg),
                                 QStringLiteral("application/json")));

    result.files.append(textFile(QStringLiteral("functions/index.js"),
                                 firebaseIndexJs(request, endpoints),
                                 QStringLiteral("text/javascript")));

    QJsonObject hosting;
    hosting.insert(QStringLiteral("public"), QStringLiteral("public"));
    QJsonArray ignore;
    ignore.append(QStringLiteral("firebase.json"));
    ignore.append(QStringLiteral("**/.*"));
    ignore.append(QStringLiteral("**/node_modules/**"));
    hosting.insert(QStringLiteral("ignore"), ignore);
    QJsonArray rewrites;
    QJsonObject rewrite;
    rewrite.insert(QStringLiteral("source"), QStringLiteral("/api/**"));
    rewrite.insert(QStringLiteral("function"), QStringLiteral("api"));
    rewrites.append(rewrite);
    hosting.insert(QStringLiteral("rewrites"), rewrites);

    QJsonArray functions;
    QJsonObject functionEntry;
    functionEntry.insert(QStringLiteral("source"), QStringLiteral("functions"));
    functionEntry.insert(QStringLiteral("codebase"), QStringLiteral("default"));
    functionEntry.insert(QStringLiteral("runtime"),
                         QStringLiteral("nodejs") + nodeMajorVersion(request));
    functions.append(functionEntry);

    QJsonObject firebase;
    firebase.insert(QStringLiteral("functions"), functions);
    firebase.insert(QStringLiteral("hosting"), hosting);
    result.files.append(textFile(QStringLiteral("firebase.json"), jsonText(firebase),
                                 QStringLiteral("application/json")));

    if (request.includeEnvExample) {
        result.files.append(textFile(QStringLiteral("functions/.env.example"),
                                     envExampleText(request, {}),
                                     QStringLiteral("text/plain")));
    }
    if (request.includeReadme) {
        QStringList notes;
        notes << QStringLiteral("Emulator URL: http://127.0.0.1:5001/") + name
                     + QStringLiteral("/us-central1/api");
        notes << QStringLiteral("Set secrets with `firebase functions:secrets:set "
                                "TCBW_API_TOKEN`.");
        result.files.append(textFile(
            QStringLiteral("README.md"),
            readmeText(request, BackendConnector::findTarget(request.targetId), endpoints,
                       {QStringLiteral("cd functions && npm install && cd .."),
                        QStringLiteral("firebase emulators:start --only functions"),
                        QStringLiteral("firebase deploy --only functions,hosting")},
                       notes),
            QStringLiteral("text/markdown")));
    }
    if (request.includeDocker) {
        QStringList lines;
        lines << comment(request, QStringLiteral("#"),
                         QStringLiteral("Generated by ") + kGeneratedBy,
                         QStringLiteral("由 ") + kGeneratedBy + QStringLiteral(" 生成"));
        lines << QStringLiteral("FROM node:") + nodeMajorVersion(request) + QStringLiteral("-alpine");
        lines << QStringLiteral("WORKDIR /app");
        lines << QStringLiteral("COPY functions/package*.json ./functions/");
        lines << QStringLiteral("RUN cd functions && npm install");
        lines << QStringLiteral("COPY . .");
        lines << QStringLiteral("EXPOSE 5001");
        lines << QStringLiteral("CMD [\"npx\", \"firebase-tools\", \"emulators:start\", "
                                "\"--only\", \"functions\"]");
        result.files.append(textFile(QStringLiteral("Dockerfile"), joinLines(lines),
                                     QStringLiteral("text/plain")));
    }

    return result;
}

EmitResult BackendScaffold::generateGoNetHttp(const ScaffoldRequest &request) const
{
    EmitResult result;
    result.ok = true;

    const QString name = slugifyAscii(request.projectName, QStringLiteral("tcbw-backend"));
    const QVector<Endpoint> endpoints = activeEndpoints(request);

    QStringList mod;
    mod << comment(request, QStringLiteral("//"),
                   QStringLiteral("Generated by ") + kGeneratedBy,
                   QStringLiteral("由 ") + kGeneratedBy + QStringLiteral(" 生成"));
    mod << QStringLiteral("module example.com/") + name;
    mod << QString();
    mod << QStringLiteral("go 1.22");
    result.files.append(textFile(QStringLiteral("go.mod"), joinLines(mod),
                                 QStringLiteral("text/plain")));

    result.files.append(textFile(QStringLiteral("main.go"), goMainGo(request, endpoints),
                                 QStringLiteral("text/x-go")));

    if (request.includeEnvExample) {
        result.files.append(textFile(QStringLiteral(".env.example"),
                                     envExampleText(request, {}),
                                     QStringLiteral("text/plain")));
    }
    if (request.includeReadme) {
        QStringList notes;
        notes << QStringLiteral("Go 1.22 or newer is required for method-aware ServeMux "
                                "patterns.");
        notes << QStringLiteral("The server binds 127.0.0.1 by default; change the address in "
                                "`main.go` when you deploy.");
        result.files.append(textFile(
            QStringLiteral("README.md"),
            readmeText(request, BackendConnector::findTarget(request.targetId), endpoints,
                       {QStringLiteral("go mod tidy"),
                        QStringLiteral("go run ."),
                        QStringLiteral("go build -o server .")},
                       notes),
            QStringLiteral("text/markdown")));
    }
    if (request.includeDocker) {
        QStringList lines;
        lines << comment(request, QStringLiteral("#"),
                         QStringLiteral("Generated by ") + kGeneratedBy,
                         QStringLiteral("由 ") + kGeneratedBy + QStringLiteral(" 生成"));
        lines << QStringLiteral("FROM golang:1.22-alpine AS build");
        lines << QStringLiteral("WORKDIR /src");
        lines << QStringLiteral("COPY go.mod ./");
        lines << QStringLiteral("COPY main.go ./");
        lines << QStringLiteral("RUN go build -o /out/server .");
        lines << QString();
        lines << QStringLiteral("FROM alpine:3.20");
        lines << QStringLiteral("COPY --from=build /out/server /usr/local/bin/server");
        lines << QStringLiteral("ENV PORT=8080");
        lines << QStringLiteral("EXPOSE 8080");
        lines << QStringLiteral("ENTRYPOINT [\"/usr/local/bin/server\"]");
        result.files.append(textFile(QStringLiteral("Dockerfile"), joinLines(lines),
                                     QStringLiteral("text/plain")));
    }

    return result;
}

EmitResult BackendScaffold::generateStaticJsonServer(const ScaffoldRequest &request) const
{
    EmitResult result;
    result.ok = true;

    const QString name = slugifyAscii(request.projectName, QStringLiteral("tcbw-json"));
    const QVector<Endpoint> endpoints = activeEndpoints(request);

    result.files.append(textFile(QStringLiteral("db.json"),
                                 jsonText(staticJsonDb(request, endpoints)),
                                 QStringLiteral("application/json")));
    result.files.append(textFile(QStringLiteral("server.js"),
                                 staticJsonServerJs(request, endpoints),
                                 QStringLiteral("text/javascript")));

    QJsonObject pkg;
    pkg.insert(QStringLiteral("name"), name);
    pkg.insert(QStringLiteral("version"), QStringLiteral("0.1.0"));
    pkg.insert(QStringLiteral("private"), true);
    pkg.insert(QStringLiteral("type"), QStringLiteral("module"));
    pkg.insert(QStringLiteral("description"), QStringLiteral("Generated by ") + kGeneratedBy);
    QJsonObject scripts;
    scripts.insert(QStringLiteral("start"), QStringLiteral("node server.js"));
    pkg.insert(QStringLiteral("scripts"), scripts);
    QJsonObject engines;
    engines.insert(QStringLiteral("node"), QStringLiteral(">=") + nodeMajorVersion(request));
    pkg.insert(QStringLiteral("engines"), engines);
    result.files.append(textFile(QStringLiteral("package.json"), jsonText(pkg),
                                 QStringLiteral("application/json")));

    if (request.includeEnvExample) {
        result.files.append(textFile(QStringLiteral(".env.example"),
                                     envExampleText(request, {}),
                                     QStringLiteral("text/plain")));
    }
    if (request.includeReadme) {
        QStringList notes;
        notes << QStringLiteral("No dependencies: `node server.js` is enough.");
        notes << QStringLiteral("Collections are the top level arrays of `db.json`; POST and "
                                "DELETE persist back to that file.");
        result.files.append(textFile(
            QStringLiteral("README.md"),
            readmeText(request, BackendConnector::findTarget(request.targetId), endpoints,
                       {QStringLiteral("node server.js"),
                        QStringLiteral("curl http://127.0.0.1:3001/health")},
                       notes),
            QStringLiteral("text/markdown")));
    }
    if (request.includeDocker) {
        QStringList lines;
        lines << comment(request, QStringLiteral("#"),
                         QStringLiteral("Generated by ") + kGeneratedBy,
                         QStringLiteral("由 ") + kGeneratedBy + QStringLiteral(" 生成"));
        lines << QStringLiteral("FROM node:") + nodeMajorVersion(request) + QStringLiteral("-alpine");
        lines << QStringLiteral("WORKDIR /app");
        lines << QStringLiteral("COPY . .");
        lines << QStringLiteral("ENV PORT=3001");
        lines << QStringLiteral("EXPOSE 3001");
        lines << QStringLiteral("CMD [\"node\", \"server.js\"]");
        result.files.append(textFile(QStringLiteral("Dockerfile"), joinLines(lines),
                                     QStringLiteral("text/plain")));
    }

    return result;
}

} // namespace tcbw
