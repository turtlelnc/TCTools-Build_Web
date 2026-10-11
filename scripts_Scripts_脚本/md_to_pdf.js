// ============================================================================
//  md_to_pdf.js -- documentation build helper (Markdown -> print HTML -> PDF)
//  md_to_pdf.js —— 文档构建助手（Markdown → 打印 HTML → PDF）
//
//  THIS IS A DOCUMENTATION BUILD HELPER, NOT PART OF THE APPLICATION.
//  It is dependency-free Node.js (no npm install) and it REQUIRES GOOGLE CHROME
//  to be installed, because Chrome's headless "--print-to-pdf" is the PDF
//  engine. Nothing in the shipped application imports this file.
//  这是文档构建助手，不属于应用程序本体。它是不依赖任何 npm 包的纯 Node.js
//  脚本，并且**必须安装 Google Chrome** —— PDF 引擎就是 Chrome 无头模式的
//  "--print-to-pdf"。发布的应用不会引用本文件。
//
//  Usage / 用法:
//    node md_to_pdf.js <input.md> [--pdf <out.pdf>] [--html <out.html>]
//                      [--lang en|zh|auto] [--chrome <path>] [--html-only]
//                      [--keep-html]
//
//  Markdown features handled / 支持的 Markdown 语法:
//    headings (# .. ######), paragraphs, **bold**, *italic* / _italic_,
//    `inline code`, fenced code blocks (``` and ~~~), bullet lists (nested),
//    numbered lists (nested), tables (with :---: alignment), blockquotes,
//    horizontal rules, links [text](url), and image placeholders ![alt](src).
//  处理：标题、段落、粗体、斜体、行内代码、围栏代码块、有序/无序列表（可嵌套）、
//  表格（含对齐）、引用块、分隔线、链接、图片占位符。
//
//  Source text is HTML-escaped BEFORE conversion, so a "<" inside a code sample
//  can never break the generated page. / 源码在转换前先做 HTML 转义，因此代码
//  示例里的 "<" 永远不会破坏生成的页面。
// ============================================================================

'use strict';

const fs = require('fs');
const path = require('path');
const { spawnSync } = require('child_process');

// ---------------------------------------------------------------------------
//  Chrome discovery / Chrome 查找
// ---------------------------------------------------------------------------
const CHROME_CANDIDATES = [
    'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe',
    'C:\\Program Files (x86)\\Google\\Chrome\\Application\\chrome.exe',
    '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    '/usr/bin/google-chrome',
    '/usr/bin/google-chrome-stable',
    '/usr/bin/chromium',
    '/usr/bin/chromium-browser',
    '/snap/bin/chromium',
];

function findChrome(explicit) {
    if (explicit) {
        return fs.existsSync(explicit) ? explicit : null;
    }
    for (const candidate of CHROME_CANDIDATES) {
        if (fs.existsSync(candidate)) return candidate;
    }
    return null;
}

// ---------------------------------------------------------------------------
//  Escaping / 转义
// ---------------------------------------------------------------------------
function escapeHtml(text) {
    return String(text)
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;');
}

// ---------------------------------------------------------------------------
//  Inline formatting / 行内格式
//  Code spans are tokenised out first so that * and _ inside them survive.
//  先取出代码片段，使其中的 * 与 _ 不被误当作强调标记。
// ---------------------------------------------------------------------------
function renderInline(raw) {
    const spans = [];
    let text = String(raw).replace(/`([^`]+)`/g, (match, code) => {
        spans.push(code);
        return '\u0000CODE' + (spans.length - 1) + '\u0000';
    });

    //  Images then links (images first: "![a](b)" also matches the link rule).
    //  先图片后链接（否则 "![a](b)" 会被链接规则抢先匹配）。
    const images = [];
    text = text.replace(/!\[([^\]]*)\]\(([^)\s]+)(?:\s+"[^"]*")?\)/g, (match, alt, src) => {
        images.push({ alt: alt, src: src });
        return '\u0000IMG' + (images.length - 1) + '\u0000';
    });

    text = text.replace(/\[([^\]]+)\]\(([^)\s]+)(?:\s+"[^"]*")?\)/g, (match, label, href) => {
        const safeHref = escapeHtml(href);
        return '<a href="' + safeHref + '">' + label + '</a>';
    });

    //  Bold before italic. / 先处理粗体，再处理斜体。
    text = text.replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>');
    text = text.replace(/__([^_]+)__/g, '<strong>$1</strong>');

    //  Emphasis: the marked run must not start or end with a space. The
    //  surrounding characters are deliberately unconstrained, because Chinese
    //  text writes *emphasis* directly after a Han character and the usual
    //  "must follow whitespace" rule would leave the asterisks visible.
    //  强调：被标记的内容首尾不能是空格。前后字符刻意不做限制，因为中文会在汉字
    //  后面直接写 *强调*，若按"前面必须是空白"的常见规则，星号就会留在正文里。
    const emphasise = (pattern, tag) => {
        text = text.replace(pattern, (match, inner) => {
            if (inner === undefined || /^\s/.test(inner) || /\s$/.test(inner)) return match;
            return '<' + tag + '>' + inner + '</' + tag + '>';
        });
    };
    emphasise(/\*([^*\n]+)\*/g, 'em');
    emphasise(/_([^_\n]+)_/g, 'em');
    text = text.replace(/~~([^~]+)~~/g, '<del>$1</del>');

    //  Restore code spans and images. / 还原代码片段与图片占位符。
    text = text.replace(/\u0000CODE(\d+)\u0000/g, (match, index) => {
        return '<code>' + escapeHtml(spans[Number(index)]) + '</code>';
    });
    text = text.replace(/\u0000IMG(\d+)\u0000/g, (match, index) => {
        const image = images[Number(index)];
        return renderImage(image.alt, image.src);
    });

    return text;
}

// ---------------------------------------------------------------------------
//  Image placeholders / 图片占位符
//  The documentation ships no image files yet. When the referenced file does
//  exist next to the Markdown it is embedded normally; otherwise a clearly
//  labelled placeholder box is printed instead of a broken-image icon.
//  文档目前不附带图片文件。若被引用的文件确实存在于 Markdown 旁，则正常嵌入；
//  否则打印一个标签清晰的占位框，而不是一个"图片已损坏"图标。
// ---------------------------------------------------------------------------
let imageBaseDirectory = process.cwd();

function renderImage(alt, src) {
    if (!/^[a-z]+:/i.test(src) && !src.startsWith('/')) {
        const resolved = path.resolve(imageBaseDirectory, decodeURIComponent(src));
        if (fs.existsSync(resolved)) {
            return (
                '<figure class="doc-figure"><img src="' +
                escapeHtml(src) +
                '" alt="' +
                escapeHtml(alt) +
                '"><figcaption>' +
                escapeHtml(alt) +
                '</figcaption></figure>'
            );
        }
    }
    return (
        '<div class="img-placeholder">' +
        '<div class="img-placeholder-label">' +
        escapeHtml(alt) +
        '</div>' +
        '<div class="img-placeholder-path">' +
        escapeHtml(src) +
        '</div>' +
        '<div class="img-placeholder-note">Screenshot placeholder · 截图占位符</div>' +
        '</div>'
    );
}

// ---------------------------------------------------------------------------
//  Block parsing / 块级解析
// ---------------------------------------------------------------------------
function isBlank(line) {
    return line.trim() === '';
}

function isFence(line) {
    const match = /^\s*(`{3,}|~{3,})\s*([A-Za-z0-9_+-]*)\s*$/.exec(line);
    return match ? { marker: match[1][0], length: match[1].length, info: match[2] } : null;
}

function isHeading(line) {
    const match = /^(#{1,6})\s+(.*?)\s*#*\s*$/.exec(line);
    return match ? { level: match[1].length, text: match[2] } : null;
}

function isHorizontalRule(line) {
    const trimmed = line.trim();
    if (trimmed.length < 3) return false;
    return /^(-{3,}|\*{3,}|_{3,})$/.test(trimmed.replace(/[ \t]/g, ''));
}

//  A line that is nothing but an image reference becomes a block of its own, so
//  the placeholder box is never nested inside a paragraph.
//  整行只有一个图片引用的行会独立成块，占位框因此不会被塞进段落里。
function imageOnlyMatch(line) {
    return /^\s*!\[([^\]]*)\]\(([^)\s]+)(?:\s+"[^"]*")?\)\s*$/.exec(line);
}

function isBlockquote(line) {
    return /^\s{0,3}>/.test(line);
}

function listMatch(line) {
    const bullet = /^(\s*)([-*+])\s+(.*)$/.exec(line);
    if (bullet) {
        return { indent: bullet[1].replace(/\t/g, '    ').length, ordered: false, text: bullet[3] };
    }
    const ordered = /^(\s*)(\d{1,9})[.)]\s+(.*)$/.exec(line);
    if (ordered) {
        return { indent: ordered[1].replace(/\t/g, '    ').length, ordered: true, text: ordered[3] };
    }
    return null;
}

function isTableSeparator(line) {
    return /^\s*\|?\s*:?-{2,}:?\s*(\|\s*:?-{2,}:?\s*)*\|?\s*$/.test(line) && line.includes('-');
}

function splitTableRow(line) {
    let text = line.trim();
    if (text.startsWith('|')) text = text.slice(1);
    if (text.endsWith('|')) text = text.slice(0, -1);
    const cells = [];
    let current = '';
    for (let i = 0; i < text.length; i += 1) {
        const ch = text[i];
        if (ch === '\\' && text[i + 1] === '|') {
            current += '|';
            i += 1;
            continue;
        }
        if (ch === '|') {
            cells.push(current.trim());
            current = '';
            continue;
        }
        current += ch;
    }
    cells.push(current.trim());
    return cells;
}

function isTableStart(lines, index) {
    if (index + 1 >= lines.length) return false;
    if (!lines[index].includes('|')) return false;
    return isTableSeparator(lines[index + 1]);
}

function startsNewBlock(lines, index) {
    const line = lines[index];
    if (isBlank(line)) return true;
    if (isFence(line)) return true;
    if (isHeading(line)) return true;
    if (isHorizontalRule(line)) return true;
    if (isBlockquote(line)) return true;
    if (listMatch(line)) return true;
    if (isTableStart(lines, index)) return true;
    return false;
}

//  parseBlocks returns HTML for a run of lines. / parseBlocks 返回一段行的 HTML。
function parseBlocks(lines) {
    const out = [];
    let i = 0;

    while (i < lines.length) {
        const line = lines[i];

        if (isBlank(line)) {
            i += 1;
            continue;
        }

        const fence = isFence(line);
        if (fence) {
            const body = [];
            i += 1;
            while (i < lines.length) {
                const closing = isFence(lines[i]);
                if (closing && closing.marker === fence.marker && closing.length >= fence.length
                    && closing.info === '') {
                    i += 1;
                    break;
                }
                body.push(lines[i]);
                i += 1;
            }
            const language = fence.info ? ' class="language-' + escapeHtml(fence.info) + '"' : '';
            out.push('<pre class="code-block"><code' + language + '>'
                + escapeHtml(body.join('\n')) + '</code></pre>');
            continue;
        }

        const heading = isHeading(line);
        if (heading) {
            out.push('<h' + heading.level + '>' + renderInline(heading.text)
                + '</h' + heading.level + '>');
            i += 1;
            continue;
        }

        if (isHorizontalRule(line)) {
            out.push('<hr>');
            i += 1;
            continue;
        }

        const standaloneImage = imageOnlyMatch(line);
        if (standaloneImage) {
            out.push(renderImage(standaloneImage[1], standaloneImage[2]));
            i += 1;
            continue;
        }

        if (isBlockquote(line)) {
            const body = [];
            while (i < lines.length && (isBlockquote(lines[i]) || (!isBlank(lines[i]) && body.length > 0
                && !startsNewBlock(lines, i)))) {
                body.push(lines[i].replace(/^\s{0,3}>\s?/, ''));
                i += 1;
            }
            out.push('<blockquote>' + parseBlocks(body) + '</blockquote>');
            continue;
        }

        if (isTableStart(lines, i)) {
            const header = splitTableRow(lines[i]);
            const align = splitTableRow(lines[i + 1]).map((cell) => {
                const left = cell.startsWith(':');
                const right = cell.endsWith(':');
                if (left && right) return ' style="text-align:center"';
                if (right) return ' style="text-align:right"';
                if (left) return ' style="text-align:left"';
                return '';
            });
            i += 2;
            const rows = [];
            while (i < lines.length && lines[i].includes('|') && !isBlank(lines[i])) {
                rows.push(splitTableRow(lines[i]));
                i += 1;
            }
            let table = '<table><thead><tr>';
            header.forEach((cell, index) => {
                table += '<th' + (align[index] || '') + '>' + renderInline(cell) + '</th>';
            });
            table += '</tr></thead><tbody>';
            for (const row of rows) {
                table += '<tr>';
                for (let c = 0; c < header.length; c += 1) {
                    table += '<td' + (align[c] || '') + '>' + renderInline(row[c] === undefined ? '' : row[c])
                        + '</td>';
                }
                table += '</tr>';
            }
            table += '</tbody></table>';
            out.push(table);
            continue;
        }

        const item = listMatch(line);
        if (item) {
            const html = parseList(lines, i);
            out.push(html.html);
            i = html.next;
            continue;
        }

        //  Paragraph: consume until a blank line or a new block starts.
        //  段落：一直读到空行或新的块级结构开始。
        const paragraph = [];
        while (i < lines.length && !isBlank(lines[i])) {
            if (paragraph.length > 0 && startsNewBlock(lines, i)) break;
            paragraph.push(lines[i]);
            i += 1;
        }
        //  Soft line breaks inside a paragraph become spaces (standard
        //  Markdown behaviour), after honouring an explicit hard break, which
        //  is written as two or more trailing spaces.
        //  段落内的软换行变成空格（Markdown 的标准行为）；在此之前先处理用
        //  两个以上行尾空格写成的显式换行。
        const joined = paragraph.join('\n')
            .replace(/ {2,}\n/g, '<br>\n')
            .replace(/\n/g, ' ');
        out.push('<p>' + renderInline(joined) + '</p>');
    }

    return out.join('\n');
}

//  parseList builds a (possibly nested) list. / parseList 构建可能嵌套的列表。
function parseList(lines, start) {
    const first = listMatch(lines[start]);
    const ordered = first.ordered;
    const baseIndent = first.indent;
    const tag = ordered ? 'ol' : 'ul';
    const items = [];
    let i = start;

    while (i < lines.length) {
        const item = listMatch(lines[i]);
        if (!item || item.indent < baseIndent) break;
        if (item.indent > baseIndent) {
            //  Nested list belongs to the previous item. / 嵌套列表属于上一项。
            const nested = parseList(lines, i);
            if (items.length > 0) {
                items[items.length - 1].children.push(nested.html);
            } else {
                items.push({ text: '', children: [nested.html] });
            }
            i = nested.next;
            continue;
        }
        if (item.ordered !== ordered) break;

        //  Continuation lines of this item. / 该项的续行。
        const body = [item.text];
        i += 1;
        while (i < lines.length && !listMatch(lines[i]) && !isBlank(lines[i])
               && !startsNewBlock(lines, i)) {
            body.push(lines[i].trim());
            i += 1;
        }
        items.push({ text: body.join(' '), children: [] });

        //  Skip the blank line between loose items. / 跳过松散列表项间的空行。
        if (i < lines.length && isBlank(lines[i])) {
            let lookahead = i;
            while (lookahead < lines.length && isBlank(lines[lookahead])) lookahead += 1;
            const next = lookahead < lines.length ? listMatch(lines[lookahead]) : null;
            if (next && next.indent >= baseIndent) {
                i = lookahead;
                continue;
            }
        }
    }

    const html = '<' + tag + '>'
        + items.map((entry) => '<li>' + renderInline(entry.text) + entry.children.join('') + '</li>').join('')
        + '</' + tag + '>';
    return { html: html, next: i };
}

// ---------------------------------------------------------------------------
//  Print stylesheet / 打印样式表
// ---------------------------------------------------------------------------
const CJK_STACK = '"Segoe UI", "Microsoft YaHei", "Noto Sans CJK SC", "PingFang SC", '
    + '"Hiragino Sans GB", "Source Han Sans SC", sans-serif';
const LATIN_STACK = '"Segoe UI", "Helvetica Neue", Helvetica, Arial, "Noto Sans", '
    + '"Liberation Sans", sans-serif';
const MONO_STACK = '"Cascadia Mono", Consolas, "SFMono-Regular", Menlo, "DejaVu Sans Mono", '
    + '"Noto Sans Mono CJK SC", monospace';

function printStylesheet(language) {
    const bodyStack = language === 'zh' ? CJK_STACK : LATIN_STACK;

    return `
    @page { size: A4; margin: 17mm 15mm 19mm 15mm; }

    * { box-sizing: border-box; }

    html { -webkit-print-color-adjust: exact; print-color-adjust: exact; }

    body {
      font-family: ${bodyStack};
      font-size: 10.5pt;
      line-height: 1.62;
      color: #1d1d1f;
      margin: 0;
      word-wrap: break-word;
      overflow-wrap: break-word;
    }

    h1, h2, h3, h4, h5, h6 {
      font-family: ${bodyStack};
      line-height: 1.28;
      color: #111114;
      page-break-after: avoid;
      break-after: avoid;
    }
    h1 { font-size: 22pt; margin: 0 0 10mm 0; padding-bottom: 3mm;
         border-bottom: 2px solid #0a84ff; }
    h2 { font-size: 15.5pt; margin: 9mm 0 3mm 0; padding-bottom: 1.6mm;
         border-bottom: 1px solid #d9d9de; }
    h3 { font-size: 12.5pt; margin: 6.5mm 0 2mm 0; }
    h4 { font-size: 11pt; margin: 5mm 0 1.6mm 0; color: #3a3a3f; }
    h5, h6 { font-size: 10.5pt; margin: 4mm 0 1.4mm 0; color: #3a3a3f; }

    p { margin: 0 0 2.6mm 0; orphans: 2; widows: 2; }

    a { color: #0a63c9; text-decoration: none; border-bottom: 0.4pt solid #b9d4f2; }

    strong { font-weight: 600; }
    em { font-style: italic; }
    del { color: #8a8a8f; }

    code {
      font-family: ${MONO_STACK};
      font-size: 9.2pt;
      background: #f2f2f5;
      border: 0.4pt solid #e2e2e7;
      border-radius: 2pt;
      padding: 0.2mm 0.9mm;
    }

    pre.code-block {
      font-family: ${MONO_STACK};
      font-size: 9pt;
      line-height: 1.48;
      background: #f7f7f9;
      border: 0.5pt solid #dcdce2;
      border-left: 2.6pt solid #0a84ff;
      border-radius: 3pt;
      padding: 3mm 3.4mm;
      margin: 2.6mm 0 3.4mm 0;
      white-space: pre-wrap;
      word-break: break-word;
      page-break-inside: avoid;
      break-inside: avoid;
    }
    pre.code-block code { background: none; border: none; padding: 0; font-size: inherit; }

    ul, ol { margin: 0 0 2.8mm 0; padding-left: 6.4mm; }
    li { margin: 0 0 1.1mm 0; }
    li > ul, li > ol { margin: 1.1mm 0 0 0; }

    blockquote {
      margin: 2.6mm 0 3.2mm 0;
      padding: 2mm 3.4mm;
      border-left: 2.6pt solid #b8b8c0;
      background: #fafafc;
      color: #3a3a3f;
      page-break-inside: avoid;
      break-inside: avoid;
    }
    blockquote p:last-child { margin-bottom: 0; }

    table {
      width: 100%;
      border-collapse: collapse;
      margin: 2.6mm 0 4mm 0;
      font-size: 9.4pt;
      page-break-inside: avoid;
      break-inside: avoid;
    }
    thead { display: table-header-group; }
    tr { page-break-inside: avoid; break-inside: avoid; }
    th, td {
      border: 0.5pt solid #d5d5dc;
      padding: 1.5mm 2.1mm;
      vertical-align: top;
      text-align: left;
    }
    th { background: #f0f0f4; font-weight: 600; }
    tbody tr:nth-child(even) td { background: #fbfbfd; }
    td code, th code { font-size: 8.8pt; }

    hr { border: none; border-top: 0.7pt solid #d9d9de; margin: 6mm 0; }

    .doc-figure { margin: 3mm 0 4mm 0; page-break-inside: avoid; text-align: center; }
    .doc-figure img { max-width: 100%; border: 0.5pt solid #d5d5dc; border-radius: 3pt; }
    .doc-figure figcaption {
      font-size: 8.8pt; color: #6a6a70; margin-top: 1.4mm; text-align: center;
    }

    .img-placeholder {
      border: 1pt dashed #b9b9c2;
      border-radius: 3pt;
      background: #fbfbfd;
      padding: 4mm 3mm;
      margin: 3mm 0 4mm 0;
      text-align: center;
      page-break-inside: avoid;
      break-inside: avoid;
    }
    .img-placeholder-label { font-size: 10pt; font-weight: 600; color: #3a3a3f; }
    .img-placeholder-path {
      font-family: ${MONO_STACK};
      font-size: 8.4pt;
      color: #6a6a70;
      margin-top: 1.2mm;
      word-break: break-all;
    }
    .img-placeholder-note {
      font-size: 8pt; color: #9a9aa2; margin-top: 1.6mm; letter-spacing: 0.2pt;
    }

    .doc-footer-note {
      margin-top: 10mm; padding-top: 2.4mm;
      border-top: 0.5pt solid #d9d9de;
      font-size: 8.6pt; color: #6a6a70;
    }
  `;
}

// ---------------------------------------------------------------------------
//  Markdown -> HTML document / Markdown → HTML 文档
// ---------------------------------------------------------------------------
function convert(markdown, options) {
    const normalized = String(markdown)
        .replace(/^\uFEFF/, '')
        .replace(/\r\n?/g, '\n')
        .replace(/\t/g, '    ');
    const lines = normalized.split('\n');
    const body = parseBlocks(lines);

    const detectedChinese = /[\u4e00-\u9fff]/.test(normalized);
    const language = options.language === 'auto'
        ? (detectedChinese ? 'zh' : 'en')
        : options.language;

    return '<!DOCTYPE html>\n<html lang="' + (language === 'zh' ? 'zh-CN' : 'en') + '">\n<head>\n'
        + '<meta charset="utf-8">\n'
        + '<meta name="viewport" content="width=device-width, initial-scale=1">\n'
        + '<title>' + escapeHtml(options.title || path.basename(options.input)) + '</title>\n'
        + '<style>' + printStylesheet(language) + '</style>\n'
        + '</head>\n<body>\n' + body + '\n</body>\n</html>\n';
}

// ---------------------------------------------------------------------------
//  PDF printing via Chrome headless / 通过 Chrome 无头模式打印 PDF
// ---------------------------------------------------------------------------
function printToPdf(chrome, htmlPath, pdfPath) {
    const fileUrl = 'file:///' + htmlPath.replace(/\\/g, '/').replace(/^\//, '');
    const args = [
        '--headless',
        '--disable-gpu',
        '--no-sandbox',
        '--print-to-pdf=' + pdfPath,
        '--no-pdf-header-footer',
        '--print-to-pdf-no-header',
        fileUrl,
    ];
    const result = spawnSync(chrome, args, { stdio: 'inherit' });
    return { status: result.status, error: result.error, command: [chrome].concat(args).join(' ') };
}

// ---------------------------------------------------------------------------
//  PDF statistics / PDF 统计
// ---------------------------------------------------------------------------
function pdfStats(pdfPath) {
    const buffer = fs.readFileSync(pdfPath);
    const text = buffer.toString('latin1');
    const pagesAll = (text.match(/\/Type\s*\/Page(?![s])/g) || []).length;
    const pagesNodes = (text.match(/\/Type\s*\/Pages/g) || []).length;

    //  The page tree may be nested, so several /Count values exist. The root
    //  count is the largest one; the number of /Type /Page objects is the
    //  ground truth and the two should agree.
    //  页面树可能是嵌套的，因此会出现多个 /Count；根节点的值最大，而
    //  /Type /Page 对象的数量才是事实依据，二者应当一致。
    const counts = (text.match(/\/Count\s+(\d+)/g) || [])
        .map((entry) => Number(entry.replace(/[^0-9]/g, '')));
    return {
        bytes: buffer.length,
        magic: buffer.slice(0, 5).toString('latin1'),
        pageObjects: pagesAll,
        pageTreeNodes: pagesNodes,
        declaredCounts: counts,
        declaredCount: counts.length ? Math.max.apply(null, counts) : null,
    };
}

// ---------------------------------------------------------------------------
//  CLI / 命令行
// ---------------------------------------------------------------------------
function parseArgs(argv) {
    const options = {
        input: null,
        pdf: null,
        html: null,
        language: 'auto',
        chrome: null,
        htmlOnly: false,
        keepHtml: false,
        title: null,
    };
    for (let i = 0; i < argv.length; i += 1) {
        const arg = argv[i];
        if (arg === '--pdf') { options.pdf = argv[++i]; }
        else if (arg === '--html') { options.html = argv[++i]; }
        else if (arg === '--lang') { options.language = argv[++i]; }
        else if (arg === '--chrome') { options.chrome = argv[++i]; }
        else if (arg === '--title') { options.title = argv[++i]; }
        else if (arg === '--html-only') { options.htmlOnly = true; }
        else if (arg === '--keep-html') { options.keepHtml = true; }
        else if (!arg.startsWith('--')) { options.input = arg; }
        else { throw new Error('Unknown option / 未知选项: ' + arg); }
    }
    return options;
}

function main() {
    const options = parseArgs(process.argv.slice(2));
    if (!options.input) {
        console.error('Usage: node md_to_pdf.js <input.md> [--pdf out.pdf] [--html out.html] '
            + '[--lang en|zh|auto] [--chrome <path>] [--html-only] [--keep-html]');
        process.exit(2);
    }
    if (!fs.existsSync(options.input)) {
        console.error('Error: input not found / 错误：找不到输入文件: ' + options.input);
        process.exit(2);
    }

    imageBaseDirectory = path.dirname(path.resolve(options.input));

    const markdown = fs.readFileSync(options.input, 'utf8');
    const html = convert(markdown, {
        input: options.input,
        language: options.language,
        title: options.title || path.basename(options.input, path.extname(options.input)),
    });

    const htmlPath = options.html
        || path.join(path.dirname(options.input),
            path.basename(options.input, path.extname(options.input)) + '.build.html');
    fs.writeFileSync(htmlPath, html, 'utf8');
    console.log('HTML written / 已写出 HTML: ' + htmlPath + ' (' + Buffer.byteLength(html, 'utf8') + ' bytes)');

    if (options.htmlOnly) {
        process.exit(0);
    }

    const chrome = findChrome(options.chrome);
    if (!chrome) {
        console.error('Error: Google Chrome was not found. Pass --chrome <path>. / '
            + '错误：未找到 Google Chrome，请用 --chrome <路径> 指定。');
        process.exit(5);
    }

    const pdfPath = options.pdf
        || path.join(path.dirname(options.input),
            path.basename(options.input, path.extname(options.input)) + '.pdf');

    //  Chrome's --print-to-pdf requires the output directory to exist.
    //  Chrome 的 --print-to-pdf 要求输出目录已存在。
    fs.mkdirSync(path.dirname(path.resolve(pdfPath)), { recursive: true });

    const printed = printToPdf(chrome, path.resolve(htmlPath), path.resolve(pdfPath));
    console.log('Chrome command / Chrome 命令:\n  ' + printed.command);

    if (printed.error) {
        console.error('Error: could not launch Chrome / 错误：无法启动 Chrome: ' + printed.error.message);
        process.exit(6);
    }
    if (printed.status !== 0) {
        console.error('Error: Chrome exited with status ' + printed.status
            + ' / 错误：Chrome 退出码 ' + printed.status);
        process.exit(6);
    }
    if (!fs.existsSync(pdfPath)) {
        console.error('Error: Chrome produced no PDF / 错误：Chrome 未生成 PDF');
        process.exit(6);
    }

    const stats = pdfStats(pdfPath);
    console.log('PDF written / 已写出 PDF: ' + path.resolve(pdfPath));
    console.log('  bytes        : ' + stats.bytes);
    console.log('  magic        : ' + JSON.stringify(stats.magic));
    console.log('  pages        : ' + stats.pageObjects + ' (/Type /Page objects)');
    console.log('  page tree    : ' + stats.pageTreeNodes + ' /Pages node(s); /Count values = '
        + stats.declaredCounts.join(', ') + ' (root = '
        + (stats.declaredCount === null ? 'unknown' : stats.declaredCount) + ')');

    if (!options.keepHtml) {
        try {
            fs.unlinkSync(htmlPath);
            console.log('Intermediate HTML removed / 已删除中间 HTML: ' + htmlPath);
        } catch (error) {
            console.warn('Warning: could not remove intermediate HTML / 警告：无法删除中间 HTML: '
                + error.message);
        }
    }

    if (stats.magic !== '%PDF-') {
        console.error('Error: the output does not start with the %PDF- magic bytes. / '
            + '错误：输出文件不是以 %PDF- 魔数字节开头。');
        process.exit(7);
    }
    if (stats.bytes < 40960) {
        console.warn('Warning: the PDF is smaller than 40 KB; verify the conversion. / '
            + '警告：PDF 小于 40 KB，请检查转换结果。');
    }
    if (stats.pageObjects < 2) {
        console.warn('Warning: fewer than two /Type /Page objects; the manual may be one page. / '
            + '警告：/Type /Page 对象少于两个，手册可能只有一页。');
    }

    process.exit(0);
}

if (require.main === module) {
    main();
}

module.exports = { convert: convert, escapeHtml: escapeHtml, findChrome: findChrome, pdfStats: pdfStats };
