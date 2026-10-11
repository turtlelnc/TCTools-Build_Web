# Documentation — TCTools-Build_Web / 文档 — TCTools-Build_Web

Bilingual end-user documentation for **TCTools-Build_Web (TC-BuildWeb)** v0.1.0-rc1.

**TCTools-Build_Web（TC-BuildWeb）v0.1.0-rc1 的中英双语文档。**

*Generated with DeepSeek V4.1 Flash on DeepSeek Harness.*
*本项目由 DeepSeek V4.1 Flash 在 DeepSeek Harness 上生成。*

---

## What is in this folder / 本目录内容

Every folder is named English first, then Chinese, matching the convention used by the
rest of the repository.

每个文件夹都按“先英文、后中文”的方式命名，与仓库其余部分保持一致。

| Path / 路径 | Language / 语言 | What it is / 说明 |
|---|---|---|
| [How_to_Use_EN_英文使用手册/how_to_use.md](How_to_Use_EN_英文使用手册/how_to_use.md) | English | The complete beginner's manual, in Markdown |
| [How_to_Use_EN_英文使用手册/how_to_use.pdf](How_to_Use_EN_英文使用手册/how_to_use.pdf) | English | The same manual, print-ready A4 PDF |
| [用户手册_ZH_中文使用手册/用户手册.md](用户手册_ZH_中文使用手册/用户手册.md) | 简体中文 | 完整的新手使用手册（Markdown） |
| [用户手册_ZH_中文使用手册/用户手册.pdf](用户手册_ZH_中文使用手册/用户手册.pdf) | 简体中文 | 同一手册的 A4 打印版 PDF |
| [For_AI_Agents_EN_供AI智能体/AI_AGENT_GUIDE.md](For_AI_Agents_EN_供AI智能体/AI_AGENT_GUIDE.md) | English | Dense guide for an LLM or agent working in this repository |
| `images_Images_图片/` | — | Screenshots, published per release / 截图，随版本发布 |

### The two manuals cover the same ground / 两本手册内容对等

1. What this is, and what you can make with it / 这是什么，能做出什么
2. Installing and starting the app, plus `--doctor` / 安装、启动，以及 `--doctor`
3. Your first page in 10 minutes / 十分钟做出第一个页面
4. The window at a glance / 界面速览
5. Designing / 设计页面
6. Styling, with a "CSS units without the jargon" sidebar / 设置样式（附 CSS 单位简说）
7. Programming with blocks / 用积木编程
8. Connecting a backend / 连接后端
9. Using the AI (TI-UseAgent) / 使用 AI（TI-UseAgent）
10. MCP servers / MCP 服务器
11. Exporting and publishing / 导出与发布
12. Keyboard shortcuts / 键盘快捷键
13. Troubleshooting / 疑难解答
14. Glossary / 术语表
15. Getting help and reporting a problem / 获取帮助与反馈问题

Section 12 of each manual is a shortcut table verified against the application source
(`resources_Resources_资源/qml_QML_界面/Main.qml`); the CLI flags documented in section
2 and in the agent guide were verified against
`src_Source_源代码/app_应用/main.cpp`.

两本手册的第 12 节都是对照应用源码（`resources_Resources_资源/qml_QML_界面/Main.qml`）
核对过的快捷键表；第 2 节与智能体指南中的命令行参数则对照
`src_Source_源代码/app_应用/main.cpp` 核对。

---

## Screenshots / 截图

**No image files are committed with the documentation source.**
Screenshots are published **per release**, so that the pictures always match a shipped
build rather than a moving checkout. The manuals reference them by relative path, and
every reference is currently rendered as a clearly labelled placeholder.

**文档源码中不包含任何图片文件。** 截图随**每个发行版本**一同发布，这样图片永远对应
一个已经发布的构建，而不是一个随时变化的代码库。手册以相对路径引用图片，目前每一处
引用都显示为标签清晰的占位符。

Intended paths / 预期的路径:

```text
docs_Documentation_文档/images_Images_图片/
├── 01-design-mode.png              Design mode / 设计模式
├── 02-elements-panel.png           Elements panel / 元素面板
├── 03-inspector-tabs.png           Inspector tabs / 属性面板标签
├── 04-blocks-editor.png            Blocks editor / 积木编辑器
├── 05-compiled-code.png            Compiled JavaScript panel / 编译后的 JavaScript 面板
├── 06-ai-provider-panel.png        AI provider panel / AI 厂商面板
├── 07-approval-prompt.png          Approval prompt / 审批提示
├── 08-mcp-panel.png                MCP servers panel / MCP 服务器面板
├── 09-backend-endpoints.png        Backend endpoints / 后端端点
├── 10-scaffold-targets.png         Starter server targets / 起始服务器目标
├── 11-export-dialog.png            Export dialog / 导出对话框
├── 12-preview-browser.png          Browser preview / 浏览器预览
├── 13-welcome-screen.png           Welcome screen / 欢迎界面
├── 14-troubleshooting-doctor.png   --doctor output / --doctor 输出
├── 15-breakpoints.png              Breakpoints / 断点
└── 16-mode-rail.png                Mode rail / 模式侧栏
```

---

## Rebuilding the PDFs / 重新生成 PDF

The manuals are converted to print HTML and then to PDF by a repository helper. It has
no npm dependencies and requires **Google Chrome** (headless `--print-to-pdf` is the
PDF engine).

手册由仓库内的辅助脚本先生成打印用 HTML，再转为 PDF。该脚本不依赖任何 npm 包，
但**需要安装 Google Chrome**（无头模式的 `--print-to-pdf` 就是 PDF 引擎）。

```bash
# Chinese manual / 中文手册
node scripts_Scripts_脚本/md_to_pdf.js \
  "docs_Documentation_文档/用户手册_ZH_中文使用手册/用户手册.md" \
  --pdf "docs_Documentation_文档/用户手册_ZH_中文使用手册/用户手册.pdf" --lang zh

# English manual / 英文手册
node scripts_Scripts_脚本/md_to_pdf.js \
  "docs_Documentation_文档/How_to_Use_EN_英文使用手册/how_to_use.md" \
  --pdf "docs_Documentation_文档/How_to_Use_EN_英文使用手册/how_to_use.pdf" --lang en
```

The script writes a temporary `.build.html` next to the input and deletes it on
success (that name is already ignored by `.gitignore`); pass `--keep-html` to keep it,
or `--html-only` to stop before printing.

脚本会在输入文件旁写出一份临时的 `.build.html`，成功后在结束时删除（该文件名已由
`.gitignore` 忽略）；加 `--keep-html` 可以保留它，加 `--html-only` 则只生成 HTML、
不打印 PDF。

---

## See also / 另见

- [../README.md](../README.md) — the project README, including build,
  packaging, security summary and known limitations
- [../LICENSE](../LICENSE) — MIT License
- [For_AI_Agents_EN_供AI智能体/AI_AGENT_GUIDE.md](For_AI_Agents_EN_供AI智能体/AI_AGENT_GUIDE.md)
  — repository map, architecture, extension recipes, testing and safety constraints
