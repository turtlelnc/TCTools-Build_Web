// ============================================================================
//  JsEmitter.cpp -- renders block programs and interactions as JavaScript
//  JsEmitter.cpp —— 将积木程序与交互渲染为 JavaScript
//  Part of TCTools-Build_Web (TC-BuildWeb)
// ============================================================================
#include "JsEmitter.h"

#include "BlockCompiler.h"
#include "BlockDef.h"
#include "CssEmitter.h"
#include "DocumentNode.h"
#include "EmitOptions.h"
#include "Page.h"

#include <QChar>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QMap>
#include <QPair>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>

#include <functional>

namespace tcbw {

namespace {

//  Declarative interaction keys understood on a node's `data` object.
//  节点 `data` 对象上可识别的声明式交互键。
const QStringList &interactionKeys()
{
    static const QStringList s = {
        QStringLiteral("onClick"),      QStringLiteral("onSubmit"),
        QStringLiteral("onChange"),     QStringLiteral("onInput"),
        QStringLiteral("onLoad"),       QStringLiteral("onMouseEnter"),
        QStringLiteral("onMouseLeave")
    };
    return s;
}

//  Maps a declarative key to the DOM event name it binds.
//  将声明式键映射到它绑定的 DOM 事件名。
QString eventNameFor(const QString &key)
{
    if (key == QLatin1String("onClick"))
        return QStringLiteral("click");
    if (key == QLatin1String("onSubmit"))
        return QStringLiteral("submit");
    if (key == QLatin1String("onChange"))
        return QStringLiteral("change");
    if (key == QLatin1String("onInput"))
        return QStringLiteral("input");
    if (key == QLatin1String("onLoad"))
        return QStringLiteral("load");
    if (key == QLatin1String("onMouseEnter"))
        return QStringLiteral("mouseenter");
    if (key == QLatin1String("onMouseLeave"))
        return QStringLiteral("mouseleave");
    return QString();
}

//  Escapes a JSON text island so it can never terminate the surrounding
//  <script> element and never introduces a raw line terminator.
//  转义 JSON 数据岛，使其绝不会结束所在的 <script> 元素，也不会引入原始行终止符。
QString escapeJsonIsland(const QString &json)
{
    QString out;
    out.reserve(json.size() + 16);

    const int n = json.size();
    for (int i = 0; i < n; ++i) {
        const QChar c = json.at(i);
        if (c == QLatin1Char('<')) {
            const QString tail = json.mid(i);
            if (tail.startsWith(QLatin1String("</script"), Qt::CaseInsensitive)
                || tail.startsWith(QLatin1String("<!--"))) {
                out += QLatin1String("<\\/");
                i += 1;                            // consumed "</" or "<!" / 已消费
                continue;
            }
            out += c;
            continue;
        }
        if (c.unicode() == 0x2028) {
            out += QLatin1String("\\u2028");
            continue;
        }
        if (c.unicode() == 0x2029) {
            out += QLatin1String("\\u2029");
            continue;
        }
        out += c;
    }
    return out;
}

//  Conservative whitelist for a raw `onXxx` statement coming from node data.
//  Everything that could open a new syntactic position is refused.
//  针对来自节点 data 的原始 `onXxx` 语句的保守白名单。
//  任何可能开启新语法结构的内容都会被拒绝。
bool isSafeRawStatement(const QString &value)
{
    if (value.isEmpty() || value.size() >= 200)
        return false;
    if (value.contains(QLatin1Char(';')) || value.contains(QLatin1Char('{'))
        || value.contains(QLatin1Char('}')) || value.contains(QLatin1Char('('))
        || value.contains(QLatin1Char(')')))
        return false;
    if (value.contains(QLatin1Char('\n')) || value.contains(QLatin1Char('\r')))
        return false;

    static const QRegularExpression allowed(
        QStringLiteral("^[A-Za-z0-9_$.;\\-\\[\\]'\" ]+$"));
    return allowed.match(value).hasMatch();
}

//  Serialises the page tree for the DOM builder.
//  为 DOM 构建器序列化页面树。
QJsonObject nodeToJson(const DocumentNode &node)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("tag"), node.tag.trimmed().toLower());

    if (!node.name.trimmed().isEmpty())
        obj.insert(QStringLiteral("name"), node.name.trimmed());
    if (!node.text.isEmpty())
        obj.insert(QStringLiteral("text"), node.text);
    if (node.textIsHtml)
        obj.insert(QStringLiteral("textIsHtml"), true);
    if (node.kind == NodeKind::Text)
        obj.insert(QStringLiteral("kind"), QStringLiteral("text"));

    if (!node.classes.isEmpty())
        obj.insert(QStringLiteral("classes"), QJsonArray::fromStringList(node.classes));

    const QMap<QString, QString> attrs = node.effectiveAttributes();
    if (!attrs.isEmpty()) {
        QJsonObject attrObj;
        for (auto it = attrs.constBegin(); it != attrs.constEnd(); ++it)
            attrObj.insert(it.key(), it.value());
        obj.insert(QStringLiteral("attributes"), attrObj);
    }

    //  Only the inline-style mode needs declarations in the data island.
    if (!node.style.isEmpty()) {
        const QMap<QString, QString> decls = node.style.resolved(Breakpoint::Desktop);
        if (!decls.isEmpty()) {
            QJsonObject styleObj;
            for (auto it = decls.constBegin(); it != decls.constEnd(); ++it)
                styleObj.insert(it.key(), it.value());
            obj.insert(QStringLiteral("style"), styleObj);
        }
    }

    if (node.layoutMode == LayoutMode::Free) {
        obj.insert(QStringLiteral("free"), true);
        obj.insert(QStringLiteral("x"), node.freeRect.x());
        obj.insert(QStringLiteral("y"), node.freeRect.y());
        obj.insert(QStringLiteral("w"), node.freeRect.width());
        obj.insert(QStringLiteral("h"), node.freeRect.height());
    }

    if (node.hidden)
        obj.insert(QStringLiteral("hidden"), true);

    QJsonArray children;
    for (const auto &child : node.children()) {
        if (child->kind == NodeKind::Slot)
            continue;
        children.append(nodeToJson(*child));
    }
    if (!children.isEmpty())
        obj.insert(QStringLiteral("children"), children);

    return obj;
}

} // namespace

// ---------------------------------------------------------------------------
//  Construction
//  构造
// ---------------------------------------------------------------------------
JsEmitter::JsEmitter(const EmitOptions &options)
    : m_options(options)
{
}

// ---------------------------------------------------------------------------
//  Runtime
//  运行时
// ---------------------------------------------------------------------------
QString JsEmitter::emitRuntime() const
{
    //  The runtime is a single, dependency-free ES2017 string. It is written with
    //  plain string concatenation so no brace inside a JavaScript template
    //  literal can ever be mistaken for a Qt argument placeholder.
    //  运行时代码是一段无依赖的 ES2017 字符串。这里使用普通字符串拼接书写，
    //  以免 JavaScript 模板字面量中的花括号被误认为 Qt 的参数占位符。
    static const QString runtime = QStringLiteral(
        "/* ------------------------------------------------------------------ */\n"
        "/* TCBW runtime -- dependency free, ES2017.                            */\n"
        "/* TCBW 运行时 —— 无依赖，ES2017。                                     */\n"
        "/* ------------------------------------------------------------------ */\n"
        "(function (global) {\n"
        "  'use strict';\n"
        "  var TCBW = global.TCBW || (global.TCBW = {});\n"
        "\n"
        "  /* -- version and configuration / 版本与配置 ------------------------- */\n"
        "  TCBW.version = '0.1.0-rc1';\n"
        "  TCBW.scripts = TCBW.scripts || {};\n"
        "  TCBW.config = TCBW.config || {\n"
        "    mount: '[data-tcbw-root]',          /* page root selector / 页面根选择器 */\n"
        "    pageId: '',                          /* current page id / 当前页面 id */\n"
        "    endpoints: {},                       /* endpointId -> request spec / 端点表 */\n"
        "    aiProxyUrl: '',                      /* user-configured proxy / 用户配置的代理 */\n"
        "    aiProxyHeaders: {},                  /* extra headers / 额外请求头 */\n"
        "    mcpBaseUrl: '',                      /* MCP gateway base URL / MCP 网关地址 */\n"
        "    mcpHeaders: {},\n"
        "    requestTimeoutMs: 30000,\n"
        "    debug: false\n"
        "  };\n"
        "\n"
        "  function warn(msg) { if (TCBW.config.debug && global.console) "
        "global.console.warn('[TCBW] ' + msg); }\n"
        "\n"
        "  /* -- variables / 变量 ---------------------------------------------- */\n"
        "  TCBW.vars = TCBW.vars || {};\n"
        "  TCBW.setVar = function (name, value) { TCBW.vars[name] = value; return value; };\n"
        "  TCBW.getVar = function (name, fallback) {\n"
        "    return Object.prototype.hasOwnProperty.call(TCBW.vars, name) "
        "? TCBW.vars[name] : fallback;\n"
        "  };\n"
        "\n"
        "  /* -- element resolution / 元素解析 ---------------------------------- */\n"
        "  function el(target) {\n"
        "    if (!target) return null;\n"
        "    if (typeof target === 'string') {\n"
        "      var found = null;\n"
        "      try { found = global.document.querySelector(target); } catch (e) { found = null; }\n"
        "      return found;\n"
        "    }\n"
        "    if (target.nodeType === 1) return target;\n"
        "    return null;\n"
        "  }\n"
        "  function each(target, fn) {\n"
        "    if (typeof target === 'string') {\n"
        "      var list = [];\n"
        "      try { list = global.document.querySelectorAll(target); } "
        "catch (e) { list = []; }\n"
        "      for (var i = 0; i < list.length; i++) fn(list[i]);\n"
        "      return;\n"
        "    }\n"
        "    var one = el(target);\n"
        "    if (one) fn(one);\n"
        "  }\n"
        "  function str(value) { return value === null || value === undefined ? '' : String(value); }\n"
        "  function hasClass(node, name) {\n"
        "    if (!node || !node.classList) return false;\n"
        "    return node.classList.contains(name);\n"
        "  }\n"
        "  TCBW.el = el;\n"
        "  TCBW.each = each;\n"
        "\n"
        "  /* -- DOM helpers / DOM 辅助函数 ------------------------------------- */\n"
        "  TCBW.dom = {\n"
        "    el: el,\n"
        "    show: function (t, mode) { each(t, function (n) "
        "{ n.style.display = mode || ''; n.removeAttribute('hidden'); }); },\n"
        "    hide: function (t) { each(t, function (n) { n.setAttribute('hidden', 'hidden'); "
        "n.style.display = 'none'; }); },\n"
        "    toggle: function (t, force) {\n"
        "      each(t, function (n) {\n"
        "        var hidden = n.hasAttribute('hidden') || n.style.display === 'none';\n"
        "        var next = (force === undefined) ? hidden : !!force;\n"
        "        if (next) { n.removeAttribute('hidden'); n.style.display = ''; } "
        "else { n.setAttribute('hidden', 'hidden'); n.style.display = 'none'; }\n"
        "      });\n"
        "    },\n"
        "    text: function (t, value) { each(t, function (n) "
        "{ n.textContent = str(value); }); },\n"
        "    html: function (t, value) { each(t, function (n) { n.innerHTML = str(value); }); },\n"
        "    /* Alias kept because block programs use both spellings.            */\n"
        "    /* 两种拼写都有积木在使用，因此保留别名。                          */\n"
        "    setHtml: function (t, value) { TCBW.dom.html(t, value); },\n"
        "    style: function (t, property, value) {\n"
        "      each(t, function (n) {\n"
        "        if (property === null || property === undefined) return;\n"
        "        if (typeof property === 'object') {\n"
        "          for (var k in property) "
        "{ if (Object.prototype.hasOwnProperty.call(property, k)) "
        "n.style.setProperty(k, str(property[k])); }\n"
        "        } else if (value === undefined || value === null) {\n"
        "          n.style.removeProperty(str(property));\n"
        "        } else {\n"
        "          n.style.setProperty(str(property), str(value));\n"
        "        }\n"
        "      });\n"
        "    },\n"
        "    addClass: function (t, name) { each(t, function (n) "
        "{ if (n.classList) n.classList.add(str(name)); }); },\n"
        "    removeClass: function (t, name) { each(t, function (n) "
        "{ if (n.classList) n.classList.remove(str(name)); }); },\n"
        "    toggleClass: function (t, name) { each(t, function (n) "
        "{ if (n.classList) n.classList.toggle(str(name)); }); },\n"
        "    hasClass: function (t, name) { var node = el(t); return hasClass(node, str(name)); },\n"
        "    attr: function (t, name, value) {\n"
        "      each(t, function (n) {\n"
        "        if (value === undefined) { n.removeAttribute(str(name)); return; }\n"
        "        if (value === null) { return; }\n"
        "        n.setAttribute(str(name), str(value));\n"
        "      });\n"
        "    },\n"
        "    scrollTo: function (t, options) {\n"
        "      each(t, function (n) {\n"
        "        var opts = (options && typeof options === 'object') "
        "? options : { behavior: 'smooth', block: 'start' };\n"
        "        if (typeof n.scrollIntoView === 'function') n.scrollIntoView(opts);\n"
        "      });\n"
        "    },\n"
        "    focus: function (t) { each(t, function (n) "
        "{ if (typeof n.focus === 'function') n.focus(); }); },\n"
        "    animate: function (t, keyframes, options) {\n"
        "      var done = 0;\n"
        "      each(t, function (n) {\n"
        "        done++;\n"
        "        if (typeof n.animate !== 'function') return;\n"
        "        n.animate(keyframes || [{ opacity: 0 }, { opacity: 1 }], "
        "options || { duration: 240, easing: 'ease-out' });\n"
        "      });\n"
        "      return done;\n"
        "    },\n"
        "    create: function (tag, attrs, children) {\n"
        "      var node = global.document.createElement(str(tag || 'div'));\n"
        "      if (attrs && typeof attrs === 'object') {\n"
        "        for (var k in attrs) {\n"
        "          if (!Object.prototype.hasOwnProperty.call(attrs, k)) continue;\n"
        "          if (k === 'text') { node.textContent = str(attrs[k]); continue; }\n"
        "          if (k === 'html') { node.innerHTML = str(attrs[k]); continue; }\n"
        "          if (k === 'class' || k === 'className') "
        "{ node.setAttribute('class', str(attrs[k])); continue; }\n"
        "          if (k === 'style' && attrs[k] && typeof attrs[k] === 'object') "
        "{ TCBW.dom.style(node, attrs[k]); continue; }\n"
        "          node.setAttribute(k, str(attrs[k]));\n"
        "        }\n"
        "      }\n"
        "      TCBW.dom.append(node, children);\n"
        "      return node;\n"
        "    },\n"
        "    append: function (parent, children) {\n"
        "      var host = el(parent);\n"
        "      if (!host || children === null || children === undefined) return host;\n"
        "      var list = (children instanceof Array) ? children : [children];\n"
        "      for (var i = 0; i < list.length; i++) {\n"
        "        var item = list[i];\n"
        "        if (item === null || item === undefined) continue;\n"
        "        host.appendChild((item.nodeType === 1 || item.nodeType === 3) "
        "? item : global.document.createTextNode(str(item)));\n"
        "      }\n"
        "      return host;\n"
        "    },\n"
        "    remove: function (t) { each(t, function (n) "
        "{ if (n.parentNode) n.parentNode.removeChild(n); }); },\n"
        "    setImage: function (t, src, alt) {\n"
        "      each(t, function (n) {\n"
        "        if (n.tagName && n.tagName.toLowerCase() === 'img') "
        "{ n.src = str(src); if (alt !== undefined) n.alt = str(alt); return; }\n"
        "        n.style.backgroundImage = 'url(\"' + str(src).replace(/\"/g, '%22') + '\")';\n"
        "      });\n"
        "    }\n"
        "  };\n"
        "\n"
        "  /* -- events and readiness / 事件与就绪 ------------------------------ */\n"
        "  TCBW.on = function (target, event, handler) {\n"
        "    if (typeof handler !== 'function') return function () {};\n"
        "    var bound = [];\n"
        "    each(target, function (n) {\n"
        "      n.addEventListener(str(event), handler, false);\n"
        "      bound.push(n);\n"
        "    });\n"
        "    return function () {\n"
        "      for (var i = 0; i < bound.length; i++) "
        "bound[i].removeEventListener(str(event), handler, false);\n"
        "    };\n"
        "  };\n"
        "  TCBW.ready = function (fn) {\n"
        "    if (typeof fn !== 'function') return;\n"
        "    if (global.document.readyState === 'loading') "
        "global.document.addEventListener('DOMContentLoaded', fn, { once: true });\n"
        "    else fn();\n"
        "  };\n"
        "\n"
        "  /* -- storage / 存储（可能被浏览器策略阻止，全部包裹 try/catch） ----- */\n"
        "  TCBW.store = {\n"
        "    set: function (k, v) { try { global.localStorage.setItem(str(k), str(v)); "
        "return true; } catch (e) { warn('localStorage blocked'); return false; } },\n"
        "    get: function (k, fallback) { try { var v = "
        "global.localStorage.getItem(str(k)); return v === null ? fallback : v; } "
        "catch (e) { return fallback; } },\n"
        "    remove: function (k) { try { global.localStorage.removeItem(str(k)); return true; } "
        "catch (e) { return false; } },\n"
        "    clear: function () { try { global.localStorage.clear(); return true; } "
        "catch (e) { return false; } },\n"
        "    sessionSet: function (k, v) { try { global.sessionStorage.setItem(str(k), str(v)); "
        "return true; } catch (e) { return false; } },\n"
        "    sessionGet: function (k, fallback) { try { var v = "
        "global.sessionStorage.getItem(str(k)); return v === null ? fallback : v; } "
        "catch (e) { return fallback; } },\n"
        "    cookieSet: function (k, v, days) {\n"
        "      try {\n"
        "        var expires = '';\n"
        "        if (days) { var d = new Date(); d.setTime(d.getTime() + days * 864e5); "
        "expires = '; expires=' + d.toUTCString(); }\n"
        "        global.document.cookie = encodeURIComponent(str(k)) + '=' "
        "+ encodeURIComponent(str(v)) + expires + '; path=/; SameSite=Lax';\n"
        "        return true;\n"
        "      } catch (e) { return false; }\n"
        "    },\n"
        "    cookieGet: function (k, fallback) {\n"
        "      try {\n"
        "        var wanted = encodeURIComponent(str(k)) + '=';\n"
        "        var parts = str(global.document.cookie).split(';');\n"
        "        for (var i = 0; i < parts.length; i++) {\n"
        "          var item = parts[i].replace(/^\\s+/, '');\n"
        "          if (item.indexOf(wanted) === 0) "
        "return decodeURIComponent(item.substring(wanted.length));\n"
        "        }\n"
        "        return fallback;\n"
        "      } catch (e) { return fallback; }\n"
        "    }\n"
        "  };\n"
        "\n"
        "  /* -- http / HTTP 请求（基于 fetch，返回 Promise） ------------------- */\n"
        "  function qs(params) {\n"
        "    if (!params) return '';\n"
        "    var parts = [];\n"
        "    if (typeof params === 'string') return params.charAt(0) === '?' "
        "? params.substring(1) : params;\n"
        "    for (var k in params) {\n"
        "      if (!Object.prototype.hasOwnProperty.call(params, k)) continue;\n"
        "      var v = params[k];\n"
        "      if (v === null || v === undefined) continue;\n"
        "      if (v instanceof Array) {\n"
        "        for (var i = 0; i < v.length; i++) "
        "parts.push(encodeURIComponent(k) + '=' + encodeURIComponent(str(v[i])));\n"
        "      } else {\n"
        "        parts.push(encodeURIComponent(k) + '=' + encodeURIComponent(str(v)));\n"
        "      }\n"
        "    }\n"
        "    return parts.join('&');\n"
        "  }\n"
        "\n"
        "  TCBW.http = {\n"
        "    request: function (method, url, body, options) {\n"
        "      var opts = options || {};\n"
        "      return new Promise(function (resolve, reject) {\n"
        "        var controller = null;\n"
        "        var timer = null;\n"
        "        var fetchOpts = {\n"
        "          method: str(method || 'GET').toUpperCase(),\n"
        "          headers: Object.assign({ 'Accept': 'application/json' }, "
        "opts.headers || opts.header || {}),\n"
        "          credentials: opts.credentials || 'same-origin'\n"
        "        };\n"
        "        var target = str(url);\n"
        "        var query = qs(opts.query || opts.params);\n"
        "        if (query) target += (target.indexOf('?') >= 0 ? '&' : '?') + query;\n"
        "        if (body !== null && body !== undefined && fetchOpts.method !== 'GET' "
        "&& fetchOpts.method !== 'HEAD') {\n"
        "          if (typeof body === 'string') { fetchOpts.body = body; }\n"
        "          else if (opts.form) { fetchOpts.body = qs(body); "
        "fetchOpts.headers['Content-Type'] = 'application/x-www-form-urlencoded'; }\n"
        "          else { fetchOpts.body = JSON.stringify(body); "
        "if (!fetchOpts.headers['Content-Type']) "
        "fetchOpts.headers['Content-Type'] = 'application/json'; }\n"
        "        }\n"
        "        try { controller = new global.AbortController(); "
        "fetchOpts.signal = controller.signal; } catch (e) { controller = null; }\n"
        "        var timeoutMs = opts.timeoutMs || TCBW.config.requestTimeoutMs;\n"
        "        if (controller && timeoutMs > 0) {\n"
        "          timer = global.setTimeout(function () { "
        "try { controller.abort(); } catch (e) {} }, timeoutMs);\n"
        "        }\n"
        "        global.fetch(target, fetchOpts).then(function (response) {\n"
        "          var contentType = response.headers "
        "? (response.headers.get('content-type') || '') : '';\n"
        "          var parse = (opts.responseType === 'json' || "
        "(!opts.responseType && contentType.indexOf('json') >= 0))\n"
        "            ? response.json()\n"
        "            : response.text();\n"
        "          return parse.then(function (payload) {\n"
        "            if (timer) global.clearTimeout(timer);\n"
        "            var result = { ok: response.ok, status: response.status, "
        "statusText: response.statusText, headers: response.headers, data: payload, "
        "body: payload };\n"
        "            if (!response.ok && opts.rejectOnHttpError) "
        "{ reject(result); return; }\n"
        "            resolve(result);\n"
        "          });\n"
        "        }).catch(function (error) {\n"
        "          if (timer) global.clearTimeout(timer);\n"
        "          reject({ ok: false, status: 0, error: error, "
        "message: str(error && error.message), data: null });\n"
        "        });\n"
        "      });\n"
        "    },\n"
        "    get: function (url, options) { return TCBW.http.request('GET', url, null, options); },\n"
        "    post: function (url, body, options) "
        "{ return TCBW.http.request('POST', url, body, options); },\n"
        "    put: function (url, body, options) "
        "{ return TCBW.http.request('PUT', url, body, options); },\n"
        "    patch: function (url, body, options) "
        "{ return TCBW.http.request('PATCH', url, body, options); },\n"
        "    del: function (url, options) "
        "{ return TCBW.http.request('DELETE', url, null, options); },\n"
        "    /* JSON convenience / JSON 便捷封装 */\n"
        "    getJson: function (url, options) {\n"
        "      var opts = Object.assign({}, options || {}); opts.responseType = 'json';\n"
        "      return TCBW.http.request('GET', url, null, opts).then(function (r) "
        "{ return r.data; });\n"
        "    },\n"
        "    postJson: function (url, body, options) {\n"
        "      var opts = Object.assign({}, options || {}); opts.responseType = 'json';\n"
        "      return TCBW.http.request('POST', url, body, opts).then(function (r) "
        "{ return r.data; });\n"
        "    }\n"
        "  };\n"
        "\n"
        "  /* -- backend endpoints / 后端端点 -------------------------------- */\n"
        "  function expandTemplate(template, variables) {\n"
        "    return str(template).replace(/\\{\\{\\s*([A-Za-z0-9_.-]+)\\s*\\}\\}/g, "
        "function (match, name) {\n"
        "      var value = variables[name];\n"
        "      if (value === undefined && name.indexOf('.') > 0) {\n"
        "        var cursor = variables;\n"
        "        var parts = name.split('.');\n"
        "        for (var i = 0; i < parts.length && cursor; i++) cursor = cursor[parts[i]];\n"
        "        value = cursor;\n"
        "      }\n"
        "      return value === undefined || value === null ? '' "
        ": encodeURIComponent(str(value));\n"
        "    });\n"
        "  }\n"
        "\n"
        "  TCBW.backend = {\n"
        "    expand: expandTemplate,\n"
        "    call: function (endpointId, options) {\n"
        "      var opts = options || {};\n"
        "      var spec = TCBW.config.endpoints[endpointId];\n"
        "      if (!spec) {\n"
        "        return Promise.reject({ ok: false, status: 0, endpointId: endpointId,\n"
        "          message: 'Unknown endpoint: ' + endpointId });\n"
        "      }\n"
        "      var variables = Object.assign({}, TCBW.vars, opts.variables || {});\n"
        "      var url = expandTemplate(spec.url, variables);\n"
        "      var body = null;\n"
        "      if (spec.body) {\n"
        "        body = String(spec.body).replace(/\\{\\{\\s*([A-Za-z0-9_.-]+)\\s*\\}\\}/g,\n"
        "          function (match, name) {\n"
        "            var value = variables[name];\n"
        "            return value === undefined || value === null ? '' : String(value);\n"
        "          });\n"
        "      }\n"
        "      return TCBW.http.request(spec.method || 'GET', url, body,\n"
        "        { headers: spec.headers || {}, query: spec.query || {},\n"
        "          timeoutMs: spec.timeoutMs, responseType: 'json' });\n"
        "    }\n"
        "  };\n"
        "\n"
        "  /* -- AI proxy / AI 代理 -------------------------------------------- */\n"
        "  /* NOTE (important): the exported page never embeds an API key or any   */\n"
        "  /* other credential. The request goes to a proxy URL the user          */\n"
        "  /* configured in TCBW.config.aiProxyUrl, and that proxy owns the       */\n"
        "  /* secret. Keep it that way.                                           */\n"
        "  /* 注意（重要）：导出的页面绝不内嵌 API Key 或任何凭据。请求会发送到   */\n"
        "  /* 用户在 TCBW.config.aiProxyUrl 中配置的代理地址，由该代理持有密钥。 */\n"
        "  /* 请保持这一设计。                                                    */\n"
        "  TCBW.ai = {\n"
        "    ask: function (options) {\n"
        "      var opts = options || {};\n"
        "      var proxy = TCBW.config.aiProxyUrl;\n"
        "      if (!proxy) {\n"
        "        return Promise.reject({ ok: false, status: 0, data: null,\n"
        "          message: 'No AI proxy configured. Set TCBW.config.aiProxyUrl. / '\n"
        "                 + '\\u672a\\u914d\\u7f6e AI \\u4ee3\\u7406\\uff0c\\u8bf7\\u8bbe\\u7f6e '\n"
        "                 + 'TCBW.config.aiProxyUrl\\u3002' });\n"
        "      }\n"
        "      var payload = { prompt: str(opts.prompt), model: opts.model || '',\n"
        "        target: opts.target || '', variables: opts.variables || TCBW.vars };\n"
        "      var headers = Object.assign({ 'Content-Type': 'application/json' },\n"
        "        TCBW.config.aiProxyHeaders || {});\n"
        "      return TCBW.http.post(proxy, payload, { headers: headers,\n"
        "        timeoutMs: opts.timeoutMs || 120000, responseType: 'json' })\n"
        "        .then(function (result) {\n"
        "          var answer = result.data;\n"
        "          if (answer && typeof answer === 'object') {\n"
        "            answer = (answer.text !== undefined) ? answer.text\n"
        "                   : (answer.answer !== undefined) ? answer.answer\n"
        "                   : (answer.content !== undefined) ? answer.content : answer;\n"
        "          }\n"
        "          if (opts.target) TCBW.dom.text(opts.target, answer);\n"
        "          return answer;\n"
        "        });\n"
        "    }\n"
        "  };\n"
        "\n"
        "  /* -- MCP tool calls / MCP 工具调用 --------------------------------- */\n"
        "  TCBW.mcp = {\n"
        "    callTool: function (server, tool, args) {\n"
        "      var base = TCBW.config.mcpBaseUrl;\n"
        "      if (!base) {\n"
        "        return Promise.reject({ ok: false, status: 0,\n"
        "          message: 'No MCP gateway configured. Set TCBW.config.mcpBaseUrl.' });\n"
        "      }\n"
        "      var url = base.replace(/\\/+$/, '') + '/' + encodeURIComponent(str(server))\n"
        "        + '/tools/' + encodeURIComponent(str(tool));\n"
        "      return TCBW.http.post(url, { arguments: args || {} },\n"
        "        { headers: TCBW.config.mcpHeaders || {}, responseType: 'json' });\n"
        "    }\n"
        "  };\n"
        "\n"
        "  /* -- data helpers / 数据辅助函数 ----------------------------------- */\n"
        "  TCBW.data = {\n"
        "    parse: function (text, fallback) {\n"
        "      if (typeof text !== 'string') return text === undefined ? fallback : text;\n"
        "      try { return JSON.parse(text); } catch (e) { return fallback; }\n"
        "    },\n"
        "    stringify: function (value, pretty) {\n"
        "      try { return JSON.stringify(value, null, pretty ? 2 : 0); } catch (e) "
        "{ return ''; }\n"
        "    },\n"
        "    get: function (object, path, fallback) {\n"
        "      if (object === null || object === undefined) return fallback;\n"
        "      if (path === undefined || path === null || path === '') return object;\n"
        "      var parts = str(path).split('.');\n"
        "      var cursor = object;\n"
        "      for (var i = 0; i < parts.length; i++) {\n"
        "        if (cursor === null || cursor === undefined) return fallback;\n"
        "        cursor = cursor[parts[i]];\n"
        "      }\n"
        "      return cursor === undefined ? fallback : cursor;\n"
        "    },\n"
        "    set: function (object, path, value) {\n"
        "      if (!object || typeof object !== 'object') return object;\n"
        "      var parts = str(path).split('.');\n"
        "      var cursor = object;\n"
        "      for (var i = 0; i < parts.length - 1; i++) {\n"
        "        if (typeof cursor[parts[i]] !== 'object' || cursor[parts[i]] === null) "
        "cursor[parts[i]] = {};\n"
        "        cursor = cursor[parts[i]];\n"
        "      }\n"
        "      cursor[parts[parts.length - 1]] = value;\n"
        "      return object;\n"
        "    },\n"
        "    filter: function (list, predicate) {\n"
        "      if (!(list instanceof Array)) return [];\n"
        "      if (typeof predicate !== 'function') return list.slice();\n"
        "      return list.filter(predicate);\n"
        "    },\n"
        "    map: function (list, mapper) {\n"
        "      if (!(list instanceof Array)) return [];\n"
        "      if (typeof mapper !== 'function') return list.slice();\n"
        "      return list.map(mapper);\n"
        "    },\n"
        "    sort: function (list, key, descending) {\n"
        "      if (!(list instanceof Array)) return [];\n"
        "      var copy = list.slice();\n"
        "      copy.sort(function (a, b) {\n"
        "        var left = key ? TCBW.data.get(a, key) : a;\n"
        "        var right = key ? TCBW.data.get(b, key) : b;\n"
        "        if (left === right) return 0;\n"
        "        var result = (left === undefined || left === null) ? -1\n"
        "                   : (right === undefined || right === null) ? 1\n"
        "                   : (left < right ? -1 : 1);\n"
        "        return descending ? -result : result;\n"
        "      });\n"
        "      return copy;\n"
        "    },\n"
        "    length: function (value) {\n"
        "      if (value === null || value === undefined) return 0;\n"
        "      if (typeof value === 'string' || value instanceof Array) return value.length;\n"
        "      if (typeof value === 'object') return Object.keys(value).length;\n"
        "      return 0;\n"
        "    }\n"
        "  };\n"
        "\n"
        "  /* -- script registry / 脚本注册表 ---------------------------------- */\n"
        "  TCBW.def = function (id, fn) {\n"
        "    if (id) TCBW.scripts[str(id)] = fn;\n"
        "    return fn;\n"
        "  };\n"
        "  TCBW.runScript = function (id, payload) {\n"
        "    var fn = TCBW.scripts[str(id)];\n"
        "    if (typeof fn !== 'function') { warn('unknown script ' + id); return undefined; }\n"
        "    try { return fn(payload); } catch (e) { warn('script ' + id + ' failed: ' + e); "
        "return undefined; }\n"
        "  };\n"
        "  TCBW.defs = TCBW.scripts;\n"
        "})(typeof window !== 'undefined' ? window : this);\n");

    if (!m_options.minifyJs)
        return runtime;

    //  The runtime is already concise; the minifier only removes its comments.
    //  运行时代码本身已经很紧凑，压缩器只负责去掉其中的注释。
    return minify(runtime);
}

// ---------------------------------------------------------------------------
//  Compiled block programs
//  编译后的积木程序
// ---------------------------------------------------------------------------
QString JsEmitter::emitProgram(const QVector<BlockScript *> &program) const
{
    if (program.isEmpty())
        return QString();

    BlockCompiler compiler(m_options.compileOptions());
    const CompileResult result = compiler.compileProgram(program);
    return result.code;
}

// ---------------------------------------------------------------------------
//  Declarative interactions
//  声明式交互
// ---------------------------------------------------------------------------
QString JsEmitter::emitInteractions(const Page &page) const
{
    if (!page.root)
        return QString();

    //  Safety decision / 安全决策:
    //    A value that starts with `script:` is a reference to a compiled block
    //    script and becomes `TCBW.runScript('<id>')`. Any other value would have
    //    to be emitted as a raw JavaScript statement, which is exactly the
    //    injection risk this product must avoid, so it is emitted only when it
    //    passes a deliberately narrow whitelist: no `;`, no braces, no
    //    parentheses, shorter than 200 characters, and only identifier-ish,
    //    dot, bracket, dash, quote and space characters. Anything else is
    //    skipped with a bilingual comment explaining why.
    //  以 `script:` 开头的值是已编译积木脚本的引用，会变成
    //  `TCBW.runScript('<id>')`。其他值只能作为原始 JavaScript 语句输出，
    //  而这正是本产品必须避免的注入风险，因此仅当它通过刻意收窄的白名单时才输出：
    //  不含 `;`、不含花括号、不含圆括号、长度小于 200，且只包含标识符类字符、
    //  点、方括号、短横线、引号与空格。其余一律跳过，并附上说明原因的双语注释。

    QStringList bindings;

    std::function<void(const DocumentNode &)> walk = [&](const DocumentNode &node) {
        for (const QString &key : interactionKeys()) {
            const QJsonValue value = node.data.value(key);
            if (!value.isString())
                continue;
            const QString raw = value.toString().trimmed();
            if (raw.isEmpty())
                continue;

            const QString event = eventNameFor(key);
            if (event.isEmpty())
                continue;

            if (raw.startsWith(QLatin1String("script:"))) {
                const QString scriptId = raw.mid(7).trimmed();
                if (scriptId.isEmpty())
                    continue;
                bindings << QStringLiteral("TCBW.on('%1', '%2', function (event) { "
                                           "TCBW.runScript('%3', { event: event }); });")
                                .arg(BlockCompiler::escapeJsString(CssEmitter::selectorFor(node)),
                                     BlockCompiler::escapeJsString(event),
                                     BlockCompiler::escapeJsString(scriptId));
                continue;
            }

            if (isSafeRawStatement(raw)) {
                bindings << QStringLiteral("TCBW.on('%1', '%2', function (event) { %3; });")
                                .arg(BlockCompiler::escapeJsString(CssEmitter::selectorFor(node)),
                                     BlockCompiler::escapeJsString(event), raw);
                continue;
            }

            bindings << QStringLiteral("/* TCBW: skipped %1 on %2 -- the value is not a "
                                       "`script:` reference and failed the conservative "
                                       "statement whitelist. */")
                            .arg(key, node.id);
            bindings << QStringLiteral("/* TCBW: 已跳过 %1（节点 %2）—— 该值不是 `script:` "
                                       "引用，且未通过保守的语句白名单。 */")
                            .arg(key, node.id);
        }

        for (const auto &child : node.children())
            walk(*child);
    };

    walk(*page.root);

    if (bindings.isEmpty())
        return QString();

    QString out = QStringLiteral("/* Declarative interactions collected from node data. */\n")
                  + QStringLiteral("/* 从节点 data 收集到的声明式交互。 */\n");
    for (const QString &line : bindings)
        out += line + QLatin1Char('\n');
    return out;
}

// ---------------------------------------------------------------------------
//  Page data island and DOM builder
//  页面数据岛与 DOM 构建器
// ---------------------------------------------------------------------------
QString JsEmitter::emitPageData(const Page &page) const
{
    QJsonObject payload;
    payload.insert(QStringLiteral("id"), page.id);
    payload.insert(QStringLiteral("name"), page.name);
    payload.insert(QStringLiteral("title"), page.title);
    payload.insert(QStringLiteral("generator"),
                   QStringLiteral("TCTools-Build_Web ") + versionString());

    if (page.root)
        payload.insert(QStringLiteral("root"), nodeToJson(*page.root));

    const QString json = QString::fromUtf8(
        QJsonDocument(payload).toJson(QJsonDocument::Compact));

    //  The island is written as a JSON *string* so the DOM builder can parse it
    //  without any risk of the surrounding <script> being terminated early.
    //  数据岛以 JSON *字符串* 形式写出，使 DOM 构建器解析它时不会提前结束
    //  所在的 <script>。
    const QString inner = escapeJsonIsland(BlockCompiler::escapeJsString(json));

    QString out = QStringLiteral("/* Page data island. */\n");
    out += QStringLiteral("/* 页面数据岛。 */\n");
    out += QStringLiteral("TCBW.pageData = JSON.parse('") + inner + QStringLiteral("');\n");
    return out;
}

QString JsEmitter::emitDomBuilder(const Page &page) const
{
    Q_UNUSED(page)

    //  Generic builder: it walks `TCBW.pageData.root` and mirrors the tree into
    //  a real DOM subtree. Void elements are never given children.
    //  通用构建器：遍历 `TCBW.pageData.root` 并将树映射为真实的 DOM 子树。
    //  空元素绝不会被赋予子节点。
    static const QString builder = QStringLiteral(
        "/* DOM builder -- renders the exported page tree without any HTML file. */\n"
        "/* DOM 构建器 —— 不需要任何 HTML 文件即可渲染导出的页面树。 */\n"
        "TCBW.VOID_TAGS = { area: 1, base: 1, br: 1, col: 1, embed: 1, hr: 1, img: 1,\n"
        "  input: 1, link: 1, meta: 1, param: 1, source: 1, track: 1, wbr: 1 };\n"
        "TCBW.SAFE_ATTR = /^[a-zA-Z][a-zA-Z0-9-]*$/;\n"
        "TCBW.URL_ATTR = { href: 1, src: 1, action: 1, poster: 1, formaction: 1, srcset: 1 };\n"
        "TCBW.isSafeUrl = function (value) {\n"
        "  var text = String(value === undefined || value === null ? '' : value)\n"
        "    .replace(/^[\\u0000-\\u0020\\u007f\\s]+/, '');\n"
        "  var match = /^([a-zA-Z][a-zA-Z0-9+.\\-]*):/.exec(text);\n"
        "  if (!match) return true;\n"
        "  var scheme = match[1].toLowerCase();\n"
        "  if (scheme === 'http' || scheme === 'https' || scheme === 'mailto'\n"
        "      || scheme === 'tel' || scheme === 'blob') return true;\n"
        "  if (scheme === 'data') {\n"
        "    var descriptor = text.substring(5).split(',')[0].split(';')[0].trim().toLowerCase();\n"
        "    return descriptor.indexOf('image/') === 0 || descriptor.indexOf('font/') === 0;\n"
        "  }\n"
        "  return false;\n"
        "};\n"
        "TCBW.buildNode = function (spec) {\n"
        "  if (!spec || typeof spec !== 'object') return null;\n"
        "  if (spec.kind === 'text' || spec.tag === '#text') "
        "{ return document.createTextNode(String(spec.text || '')); }\n"
        "  var tag = String(spec.tag || 'div').toLowerCase();\n"
        "  if (!/^[a-zA-Z][a-zA-Z0-9-]*$/.test(tag)) tag = 'div';\n"
        "  var node = document.createElement(tag);\n"
        "  var attrs = spec.attributes || {};\n"
        "  for (var key in attrs) {\n"
        "    if (!Object.prototype.hasOwnProperty.call(attrs, key)) continue;\n"
        "    var name = String(key).toLowerCase();\n"
        "    if (!TCBW.SAFE_ATTR.test(name) || name.indexOf('on') === 0) continue;\n"
        "    var value = attrs[key];\n"
        "    if (TCBW.URL_ATTR[name] && !TCBW.isSafeUrl(value)) continue;\n"
        "    try { node.setAttribute(name, String(value === undefined ? '' : value)); }\n"
        "    catch (e) { /* invalid attribute name for this element */ }\n"
        "  }\n"
        "  if (spec.classes instanceof Array && spec.classes.length) "
        "{ node.setAttribute('class', spec.classes.join(' ')); }\n"
        "  if (spec.style && typeof spec.style === 'object') {\n"
        "    for (var property in spec.style) {\n"
        "      if (!Object.prototype.hasOwnProperty.call(spec.style, property)) continue;\n"
        "      try { node.style.setProperty(property, String(spec.style[property])); } "
        "catch (e) {}\n"
        "    }\n"
        "  }\n"
        "  if (spec.free) {\n"
        "    node.style.position = node.style.position || 'absolute';\n"
        "    if (!node.style.left) node.style.left = Number(spec.x || 0) + 'px';\n"
        "    if (!node.style.top) node.style.top = Number(spec.y || 0) + 'px';\n"
        "    if (!node.style.width) node.style.width = Number(spec.w || 0) + 'px';\n"
        "    if (!node.style.height) node.style.height = Number(spec.h || 0) + 'px';\n"
        "  }\n"
        "  if (spec.hidden) node.setAttribute('hidden', 'hidden');\n"
        "  if (spec.name) node.setAttribute('data-tcbw-name', String(spec.name));\n"
        "  if (spec.text !== undefined && spec.text !== null && spec.text !== '') {\n"
        "    if (spec.textIsHtml) node.innerHTML = String(spec.text);\n"
        "    else node.textContent = String(spec.text);\n"
        "  }\n"
        "  if (!TCBW.VOID_TAGS[tag] && spec.children instanceof Array) {\n"
        "    for (var i = 0; i < spec.children.length; i++) {\n"
        "      var child = TCBW.buildNode(spec.children[i]);\n"
        "      if (child) node.appendChild(child);\n"
        "    }\n"
        "  }\n"
        "  return node;\n"
        "};\n"
        "TCBW.mount = function (selector) {\n"
        "  if (!TCBW.pageData || !TCBW.pageData.root) return null;\n"
        "  var host = TCBW.el(selector || TCBW.config.mount)\n"
        "    || document.body;\n"
        "  if (!host) return null;\n"
        "  /* Build into a fragment first so a failure leaves the page untouched. */\n"
        "  /* 先构建到片段中，这样即使失败也不会影响现有页面。 */\n"
        "  var fragment = document.createDocumentFragment();\n"
        "  var built = TCBW.buildNode(TCBW.pageData.root);\n"
        "  if (!built) return null;\n"
        "  fragment.appendChild(built);\n"
        "  var kids = built.childNodes ? Array.prototype.slice.call(built.childNodes) : [];\n"
        "  for (var i = 0; i < kids.length; i++) host.appendChild(kids[i]);\n"
        "  if (built.getAttribute && built.getAttribute('class')) {\n"
        "    host.className = (host.className ? host.className + ' ' : '')\n"
        "      + built.getAttribute('class');\n"
        "  }\n"
        "  document.documentElement.setAttribute('data-tcbw-built', 'true');\n"
        "  return host;\n"
        "};\n"
        "TCBW.ready(function () {\n"
        "  try { TCBW.mount(); } catch (e) { TCBW.config.debug && console.error(e); }\n"
        "  TCBW.on(window, 'load', function () {\n"
        "    TCBW.runScript('page:load', {});\n"
        "  });\n"
        "});\n");

    return m_options.minifyJs ? minify(builder) : builder;
}

// ---------------------------------------------------------------------------
//  Complete page script
//  完整页面脚本
// ---------------------------------------------------------------------------
QString JsEmitter::emitPage(const Page &page, const QVector<BlockScript *> &program) const
{
    QString out;

    //  -- 1. header, warnings and compile errors -----------------------------
    //  —— 1. 头部、警告与编译错误 ——
    out += QStringLiteral("/* ==================================================================== */\n");
    out += QStringLiteral("/* %1 -- generated by TCTools-Build_Web %2 */\n")
               .arg(page.name.trimmed().isEmpty() ? QStringLiteral("page") : page.name.trimmed(),
                    versionString());
    out += QStringLiteral("/* %1 —— 由 TCTools-Build_Web %2 生成 */\n")
               .arg(page.name.trimmed().isEmpty() ? QStringLiteral("页面") : page.name.trimmed(),
                    versionString());
    out += QStringLiteral("/* ==================================================================== */\n");

    //  The emitter API returns a QString, so compile errors cannot be reported
    //  through an out-parameter. They are surfaced as a leading comment block
    //  instead, which puts them right in front of the user in the exported file.
    //  该生成器接口返回 QString，因此编译错误无法通过出参上报。它们改为以文件开头
    //  的注释块呈现，使用户在导出的文件中立刻看到。
    if (!program.isEmpty()) {
        BlockCompiler compiler(m_options.compileOptions());
        const CompileResult result = compiler.compileProgram(program);
        if (!result.errors.isEmpty()) {
            out += QStringLiteral("/* COMPILE ERRORS -- the page still loads, but the "
                                  "following programs were not generated. */\n");
            out += QStringLiteral("/* 编译错误 —— 页面仍可加载，但下列程序未能生成。 */\n");
            for (const QString &error : result.errors)
                out += QStringLiteral("/* ! ") + error + QStringLiteral(" */\n");
        }
        if (!result.warnings.isEmpty()) {
            out += QStringLiteral("/* Compiler warnings / 编译器警告 */\n");
            for (const QString &warning : result.warnings)
                out += QStringLiteral("/* ~ ") + warning + QStringLiteral(" */\n");
        }
    }

    if (m_options.mode == ExportMode::ScriptOnly) {
        out += QLatin1Char('\n');
        out += emitPageData(page);
        out += QLatin1Char('\n');
    }

    out += QLatin1Char('\n');
    out += emitRuntime();
    out += QLatin1Char('\n');

    if (m_options.mode == ExportMode::ScriptOnly) {
        out += emitDomBuilder(page);
        out += QLatin1Char('\n');
    }

    //  -- 2. compiled block programs -----------------------------------------
    //  —— 2. 编译后的积木程序 ——
    const QString programCode = emitProgram(program);
    if (!programCode.trimmed().isEmpty()) {
        out += QStringLiteral("/* Block programs / 积木程序 */\n");
        out += programCode;
        if (!programCode.endsWith(QLatin1Char('\n')))
            out += QLatin1Char('\n');
        out += QLatin1Char('\n');
    }

    //  -- 3. declarative interactions ----------------------------------------
    //  —— 3. 声明式交互 ——
    out += emitInteractions(page);

    //  -- 4. bootstrap -------------------------------------------------------
    //  —— 4. 启动 ——
    out += QStringLiteral("\n/* Bootstrap / 启动 */\n");
    out += QStringLiteral("TCBW.config.pageId = '%1';\n")
               .arg(BlockCompiler::escapeJsString(page.id));
    out += QStringLiteral("TCBW.ready(function () {\n");
    out += QStringLiteral("  TCBW.runScript('page:load', {});\n");
    out += QStringLiteral("});\n");

    return m_options.minifyJs ? minify(out) : out;
}

// ---------------------------------------------------------------------------
//  Minifier
//  压缩器
// ---------------------------------------------------------------------------
QString JsEmitter::minify(const QString &js)
{
    //  Comment stripper with a scanner that understands strings, template
    //  literals and regular-expression literals. Anything ambiguous is left
    //  untouched: a conservative minifier that changes nothing is far better
    //  than an aggressive one that breaks working code.
    //  带扫描器的注释清除器，可识别字符串、模板字面量与正则字面量。
    //  任何歧义之处都会原样保留：一个什么都不改的保守压缩器，
    //  远好于一个会破坏可用代码的激进压缩器。
    QString out;
    out.reserve(js.size());

    static const QString regexAllowedAfter = QStringLiteral("(,=:[!&|?{};+-*%<>~^");

    const int n = js.size();
    int i = 0;
    while (i < n) {
        const QChar c = js.at(i);

        //  -- line comment ---------------------------------------------------
        //  —— 行注释 ——
        if (c == QLatin1Char('/') && i + 1 < n && js.at(i + 1) == QLatin1Char('/')) {
            i += 2;
            while (i < n && js.at(i) != QLatin1Char('\n'))
                ++i;
            if (i < n) {
                out += QLatin1Char('\n');       // keep the statement boundary / 保留语句边界
                ++i;
            }
            continue;
        }

        //  -- block comment --------------------------------------------------
        //  —— 块注释 ——
        if (c == QLatin1Char('/') && i + 1 < n && js.at(i + 1) == QLatin1Char('*')) {
            i += 2;
            while (i + 1 < n && !(js.at(i) == QLatin1Char('*') && js.at(i + 1) == QLatin1Char('/')))
                ++i;
            i = (i + 1 < n) ? i + 2 : n;
            continue;                           // comments may be removed safely / 注释可安全删除
        }

        //  -- string / template literal ---------------------------------------
        //  —— 字符串 / 模板字面量 ——
        if (c == QLatin1Char('\'') || c == QLatin1Char('"') || c == QLatin1Char('`')) {
            const QChar quote = c;
            out += c;
            ++i;
            while (i < n) {
                const QChar s = js.at(i);
                out += s;
                ++i;
                if (s == QLatin1Char('\\') && i < n) {
                    out += js.at(i);            // escaped character / 转义字符
                    ++i;
                    continue;
                }
                if (s == quote)
                    break;
            }
            continue;
        }

        //  -- regular expression literal --------------------------------------
        //  —— 正则字面量 ——
        if (c == QLatin1Char('/')) {
            QChar prev;
            for (int k = out.size() - 1; k >= 0; --k) {
                if (!out.at(k).isSpace()) {
                    prev = out.at(k);
                    break;
                }
            }
            if (prev.isNull() || regexAllowedAfter.contains(prev)) {
                out += c;
                ++i;
                bool inClass = false;
                while (i < n) {
                    const QChar s = js.at(i);
                    out += s;
                    ++i;
                    if (s == QLatin1Char('\\') && i < n) {
                        out += js.at(i);
                        ++i;
                        continue;
                    }
                    if (s == QLatin1Char('[')) {
                        inClass = true;
                        continue;
                    }
                    if (s == QLatin1Char(']')) {
                        inClass = false;
                        continue;
                    }
                    if (s == QLatin1Char('/') && !inClass)
                        break;
                    if (s == QLatin1Char('\n'))
                        break;                  // not a regex after all / 并非正则
                }
                while (i < n && js.at(i).isLetter())
                    out += js.at(i++);          // flags / 修饰符
                continue;
            }
        }

        out += c;
        ++i;
    }

    //  -- collapse blank lines ------------------------------------------------
    //  —— 折叠空行 ——
    //  Indentation is preserved and only trailing whitespace is removed, so
    //  automatic semicolon insertion and string contents can never change
    //  meaning.
    //  保留缩进，仅删除行尾空白，因此自动分号插入与字符串内容都不会改变语义。
    const QStringList lines = out.split(QLatin1Char('\n'));
    QStringList kept;
    kept.reserve(lines.size());
    for (QString line : lines) {
        while (!line.isEmpty() && line.at(line.size() - 1).isSpace())
            line.chop(1);
        if (line.trimmed().isEmpty())
            continue;
        kept.append(line);
    }
    return kept.join(QLatin1Char('\n'));
}

// ---------------------------------------------------------------------------
//  IIFE wrapper
//  IIFE 包装
// ---------------------------------------------------------------------------
QString JsEmitter::wrapIife(const QString &body, const QString &moduleName, bool useStrict,
                            bool esm)
{
    Q_UNUSED(moduleName)

    QString indented = body;
    if (!indented.endsWith(QLatin1Char('\n')))
        indented += QLatin1Char('\n');
    indented = indentBlock(indented, 2);

    if (esm) {
        //  `export default` is only legal in a module, so a module wrapper is
        //  emitted verbatim with its own strict mode.
        //  `export default` 仅在模块中合法，因此模块包装会原样输出并自带严格模式。
        return QStringLiteral("export default (function () {\n")
               + QStringLiteral("  'use strict';\n") + indented
               + QStringLiteral("})();\n");
    }

    QString out = QStringLiteral("(function () {\n");
    if (useStrict)
        out += QStringLiteral("  'use strict';\n");
    out += indented;
    out += QStringLiteral("})();\n");
    return out;
}

} // namespace tcbw
