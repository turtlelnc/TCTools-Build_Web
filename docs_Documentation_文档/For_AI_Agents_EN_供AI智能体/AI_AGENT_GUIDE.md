# AI Agent Guide — TCTools-Build_Web (`TC-BuildWeb`)

**Audience:** an LLM or autonomous agent that has been pointed at this repository and
asked to help a user with it.

**Read this first.** Every fact below was verified against the sources at the time of
writing. Where a number or path could drift, this guide says how to re-derive it
rather than asking you to trust it. Do not invent flags, tools, formats or counts —
re-read the named file.

---

## 1. What this project is

TCTools-Build_Web (short name **TC-BuildWeb**) is a cross-platform C++20 / Qt 6
desktop application that lets people who have never written code build web pages by
dragging HTML elements onto a structural canvas and snapping Scratch-style blocks
together to add behaviour.

It compiles the result to ordinary, unencumbered HTML, CSS and JavaScript, in four
export shapes, with no runtime and no lock-in format at the destination; projects are
stored as `.tcbw` JSON files.

It also embeds **TI-UseAgent**, an AI assistant that reaches the document *only*
through declared, JSON-Schema-typed tools behind an approval gate, across 52+ vendor
presets and MCP servers that are untrusted until explicitly trusted.

- Version: `0.1.0` with release channel `rc1` ⇒ `0.1.0-rc1` (`CMakeLists.txt`
  `project(... VERSION 0.1.0)`, `TCBW_VERSION_CHANNEL "rc1"`).
- Licence: MIT (`LICENSE`).
- Remote: <https://github.com/turtlelnc/TCTools-Build_Web>.

---

## 2. Repository map

Every project folder is bilingual, **English first then Chinese**, in the form
`name_Name_名称`. `README.md` documents why `.github/workflows/` is the single
deliberate exception (GitHub requires that literal path).

```text
TCTools-Build_Web/
├── CMakeLists.txt                     top-level build; tcbw_core + the QML app + tests
├── LICENSE                            MIT
├── README.md                          the authoritative long-form project README
├── src_Source_源代码/
│   ├── core_Core_核心/                types, element catalogue, document tree, pages,
│   │                                  undo commands, project IO, settings, id gen
│   ├── codegen_CodeGen_代码生成/      HTML / CSS / JS emitters + export orchestrator
│   ├── blocks_Blocks_积木/            block defs, registry, model, JavaScript compiler
│   ├── ai_AI_智能/                    vendor presets, SecretStore, AuthManager,
│   │                                  AiProvider, McpClient, AgentTools, AgentRuntime
│   ├── backend_Backend_后端/          backend targets, endpoints, connector, scaffold,
│   │                                  loopback PreviewServer
│   └── app_应用/                      main.cpp + the five QML-singleton controllers
├── resources_Resources_资源/
│   ├── qml_QML_界面/                  the whole UI + Tr.qml (bilingual strings) + Theme.qml
│   └── icons_Icons_图标/              app/tray icons + app.qrc
├── tests_Tests_测试/                  five Qt Test suites + their CMakeLists.txt
├── scripts_Scripts_脚本/              build.ps1, build.sh, check_syntax.ps1,
│                                      md_to_pdf.js (documentation build helper)
├── packaging_Packaging_打包/          windows_Windows/, macos_macOS/, linux_Linux/
├── docs_Documentation_文档/           end-user documentation (this tree)
│   ├── README.md                      bilingual index of the docs tree
│   ├── How_to_Use_EN_英文使用手册/     how_to_use.md + how_to_use.pdf
│   ├── 用户手册_ZH_中文使用手册/       用户手册.md + 用户手册.pdf
│   ├── For_AI_Agents_EN_供AI智能体/    AI_AGENT_GUIDE.md (this file)
│   └── images_Images_图片/            screenshots, published per release (see §11)
├── build_构建/                        out-of-source CMake binary dir (git-ignored)
└── .github/workflows/                 build_构建.yml, release_发布.yml
```

---

## 3. Build and run commands

Requirements: C++20 compiler, CMake ≥ 3.24, Qt ≥ 6.5 with
`Core Gui Qml Quick QuickControls2 QuickLayouts Network Sql Concurrent Svg Test`,
Ninja recommended. Node.js is **not** needed to build or run the app.

CMake options (all in `CMakeLists.txt`):

| Option | Default | Meaning |
|---|---|---|
| `TCBW_BUILD_TESTS` | `ON` | build the Qt Test suites and register them with CTest |
| `TCBW_ENABLE_WEBENGINE` | `ON` | *probe* for Qt WebEngine; a `QUIET` find, so absence is never an error |
| `TCBW_WARNINGS_AS_ERRORS` | `OFF` | `-Werror` / `/WX` on `tcbw_core` |

### Windows — MinGW-w64 + Qt

```powershell
$env:PATH = 'C:\Qt\6.11.1\mingw_64\bin;C:\Mingw-w64\mingw64\bin;' + $env:PATH

cmake -S . -B build_构建 -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH='C:\Qt\6.11.1\mingw_64' `
  -DTCBW_BUILD_TESTS=ON `
  -DTCBW_ENABLE_WEBENGINE=ON `
  -DTCBW_WARNINGS_AS_ERRORS=OFF

cmake --build build_构建 --parallel
ctest --test-dir build_构建 --output-on-failure
```

Helper: `pwsh -File scripts_Scripts_脚本/build.ps1 -BuildType Release -Tests -QtDir <kit> -MingwBin <mingw bin>`

### macOS

```bash
export QT_ROOT_DIR="$(brew --prefix qt)"

cmake -S . -B build_构建 -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$QT_ROOT_DIR" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
  -DTCBW_BUILD_TESTS=ON \
  -DTCBW_ENABLE_WEBENGINE=ON \
  -DTCBW_WARNINGS_AS_ERRORS=OFF

cmake --build build_构建 --parallel
ctest --test-dir build_构建 --output-on-failure
```

Add `-DCMAKE_OSX_ARCHITECTURES='arm64;x86_64'` for a universal binary. Helper:
`QT_ROOT_DIR="$QT_ROOT_DIR" ./scripts_Scripts_脚本/build.sh --build-type Release --arch arm64`

### Linux — Debian / Ubuntu

```bash
sudo apt-get update
sudo apt-get install -y ninja-build ccache \
  libgl1-mesa-dev libglu1-mesa-dev libxkbcommon-x11-dev \
  libxcb-cursor0 libxcb-icccm4-dev libxcb-keysyms1-dev \
  libxcb-shape0-dev libxcb-xinerama0-dev libxcb-randr0-dev \
  libxcb-render-util0-dev libxcb-xkb-dev libxkbcommon-dev \
  libfontconfig1-dev libfreetype6-dev

export QT_ROOT_DIR=/opt/Qt/6.8.3/gcc_64

cmake -S . -B build_构建 -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$QT_ROOT_DIR" \
  -DTCBW_BUILD_TESTS=ON \
  -DTCBW_ENABLE_WEBENGINE=OFF \
  -DTCBW_WARNINGS_AS_ERRORS=OFF

cmake --build build_构建 --parallel
QT_QPA_PLATFORM=offscreen ctest --test-dir build_构建 --output-on-failure
```

Multi-config generators (Visual Studio, Xcode) need the configuration on CTest too:
`ctest --test-dir build_构建 -C Release --output-on-failure`.

### Command-line interface

**Verified against `src_Source_源代码/app_应用/main.cpp`** (the `QCommandLineParser`
block near the top of `main()`). Do not document a flag without re-reading it.

| Flag | Behaviour | Exit status |
|---|---|---|
| `--doctor` | print environment diagnostics and exit | 0 |
| `--open <file>` / `-o <file>` | open a `.tcbw` project at start-up; a failure prints a warning and continues | 0 |
| `--export <dir>` / `-e <dir>` | export to a directory; without `--headless` the GUI still opens | 0 (4 on export failure) |
| `--headless` | run without a window; **implies** export | 0 |
| `--help`, `--version` | standard Qt parser output | 0 |

Contract notes worth stating precisely:

- **`--headless` requires `--export <dir>`.** `--headless` alone prints
  `Error: --headless requires --export <directory>.` and returns **3**.
- `--export` without `--headless` returns **4** if the export fails.
- The UI failing to load returns **2** (`engine.rootObjects().isEmpty()`).
- `--doctor` prints: version and channel, Qt runtime vs. build version, platform and
  pretty product name, CPU architecture, kernel type and version, system locale, the
  settings file path, the secret backend name plus `OS-backed`/`obfuscated`, vendor
  count, model count, HTML element count, block type count, and WebEngine availability.

```powershell
.\build_构建\TCTools-Build_Web.exe --doctor
.\build_构建\TCTools-Build_Web.exe --headless --export .\out_site
```

```bash
./build_构建/TCTools-Build_Web --doctor
./build_构建/TCTools-Build_Web --headless --export ./out_site
```

### Packaging

Packaging runs **after** `cmake --install`, because `CMakeLists.txt` calls
`qt_generate_deploy_qml_app_script`:

```bash
cmake --install build_构建 --prefix "$PWD/dist/stage"
```

Then see `packaging_Packaging_打包/README.md` and `PACKAGING_SPECS.md` (NSIS, `.dmg`,
`.deb`, AppImage).

---

## 4. Architecture summary

Three layers, strictly downward. Nothing in `core` knows about emitters, blocks, AI
or backends.

```text
QML shell (resources_Resources_资源/qml_QML_界面)
    Main.qml · Theme.qml · Tr.qml · WelcomeScreen · DesignView · BlocksView
    AiView · BackendView · CodeView · PreviewView · InspectorPanel · SidebarPanel
    ExportDialog · SettingsDialog · McpPanel · EndpointEditor · AiProviderPanel
    + the App* design-system controls
        │
        ▼
QML-singleton controllers (src_Source_源代码/app_应用) — the ONLY QML↔C++ bridge
    App      → AppController.h/.cpp
    Builder  → BuilderController.h/.cpp
    Blocks   → BlocksController.h/.cpp
    Agent    → AiController.h/.cpp
    Backend  → BackendController.h/.cpp
        │
        ▼
tcbw_core  (static library, CMake target `tcbw_core`, alias `TCTools::BuildWeb::Core`)
    core_Core_核心       Types.h · DocumentNode.h · Page.h · ProjectDocument.h
                        ElementCatalog.h · ProjectIO.h · Commands.h · StyleSheet.h
                        AppSettings.h · IdGenerator.h
    codegen_CodeGen_代码生成  EmitOptions.h · HtmlEmitter.h · CssEmitter.h
                        JsEmitter.h · CodeGenerator.h
    blocks_Blocks_积木    BlockDef.h · BlockRegistry.h · BlockModel.h · BlockCompiler.h
    ai_AI_智能            AiTypes.h · ProviderRegistry.h · SecretStore.h · AuthManager.h
                        AiProvider.h · McpClient.h · AgentTools.h · AgentRuntime.h
    backend_Backend_后端  Endpoint.h · BackendConnector.h · BackendScaffold.h
                        PreviewServer.h
```

Registration happens in `main.cpp`, before `engine.loadFromModule(uri, "Main")`:

```cpp
const char *uri = "TCTools.BuildWeb";
qmlRegisterSingletonInstance(uri, 1, 0, "App", appController);
qmlRegisterSingletonInstance(uri, 1, 0, "Builder", builderController);
qmlRegisterSingletonInstance(uri, 1, 0, "Blocks", blocksController);
qmlRegisterSingletonInstance(uri, 1, 0, "Agent", aiController);
qmlRegisterSingletonInstance(uri, 1, 0, "Backend", backendController);
```

`aiController->setBuilderController(builderController)` is what makes agent-driven
edits go through the same code paths as manual ones — and therefore onto the same
undo stack.

### Include-path rule

Each module directory is on `tcbw_core`'s **public** include path
(`target_include_directories(tcbw_core PUBLIC ...)` in `CMakeLists.txt`). Includes are
therefore **bare**: `#include "DocumentNode.h"`, never a path.

### Adding a source file

`CMakeLists.txt` enumerates sources explicitly (`TCBW_CORE_SOURCES`, and the app's
source list). Only QML is globbed. A new `.cpp` must be added to the list or it will
not be compiled.

---

## 5. The project file format

- **Extension:** `.tcbw`.
- **Encoding:** JSON, written as UTF-8.
- **Version field:** `schemaVersion`, an integer at the top level.
- **Current version:** `ProjectDocument::kSchemaVersion` in
  `src_Source_源代码/core_Core_核心/ProjectDocument.h` — currently `1`.

Where validation lives: **`src_Source_源代码/core_Core_核心/ProjectIO.cpp`**.

- Serialisation writes `schemaVersion` first (see the hand-built prefix near the top
  of the file) and also inserts it into the root object.
- Reading has two guards: a fast textual pre-scan that extracts
  `"schemaVersion":` before full parsing (so a corrupt or truncated file is rejected
  early), and then a structural check that the root contains a numeric
  `schemaVersion`.
- **A file whose version is greater than `kSchemaVersion` is refused**, with a
  bilingual error naming both versions. Older files are accepted; migrate by
  interpreting the older shape, not by rewriting the constant.
- A missing or non-numeric `schemaVersion` is an error, not a silent default.

Practical consequences for an agent:

- Never hand-edit `schemaVersion` upward to "make it load".
- Never claim a `.tcbw` file is forward-compatible with a newer build.
- Credentials are **not** in the project file; they live in `SecretStore`
  (see §10). Endpoint definitions may reference a credential, but the value is not
  serialised into the project.

---

## 6. How to extend the product

Each recipe is the complete change set. Resist adding abstractions.

### 6.1 Add a palette element — one row in `ElementCatalog.cpp`

File: `src_Source_源代码/core_Core_核心/ElementCatalog.cpp`, in
`ElementCatalog::registerElements()`.

The local `add` lambda takes, in order: `tag`, `labelEn`, `labelZh`, `groupEn`,
`groupZh`, `descEn`, `descZh`, `icon`, `ElementCategory category`, `textEn`,
`textZh`, `defaultAttributes`, `defaultStyle`, `allowsChildren`, `isVoid`, `order`.

```cpp
add(QStringLiteral("mark"), QStringLiteral("Highlight"), QStringLiteral("高亮"),
    QStringLiteral("Inline"), QStringLiteral("行内"),
    QStringLiteral("Highlighted run of text."), QStringLiteral("高亮的一段文字。"),
    QStringLiteral("highlighter"), ElementCategory::Text,
    QStringLiteral("Highlighted"), QStringLiteral("高亮文字"),
    {}, {{QStringLiteral("background-color"), QStringLiteral("#fff3b0")}},
    true, false, 120);
```

Two rules that surprise people:

- **The first registration of a tag wins.** The `add` lambda returns early if
  `m_byTag` already contains the tag, so a palette variant never shadows the plain
  entry. Register variants *after* the base tag, or use a distinct tag.
- `ElementCategory` (in `Types.h`) has the values `Layout`, `Text`, `Media`, `Form`,
  `Data`, `Navigation`, `Advanced`, `Component` and `Custom`. `Component` is what the
  UI labels "Ready-made sections".

Ready-made multi-element widgets are separate: add a `CompositeDef` in
`registerComposites()` and build its subtree in `ElementCatalog::createComposite()`.

### 6.2 Add a block — one `BlockDef` plus a compiler branch

Two files, both mandatory. A block that exists in the registry but has no compiler
branch compiles to `undefined` and appends a bilingual
`"No expression generator for block '%1'."` error.

**Step 1 — register it.** `src_Source_源代码/blocks_Blocks_积木/BlockRegistry.cpp`,
in the registration function that calls `BlockRegistry::add(BlockDef)`.

```cpp
BlockDef def;
def.type = QStringLiteral("math.cube");          // stable, dotted, never renamed
def.category = QStringLiteral("math");           // a key from registerCategories()
def.shape = BlockShape::Reporter;                // Hat/Stack/CShape/EShape/Cap/Reporter/Boolean/Round
def.labelEn = QStringLiteral("cube of %1");
def.labelZh = QStringLiteral("%1 的立方");
def.inputs.append(input(QStringLiteral("value"), QStringLiteral("number"),
                        QStringLiteral("数字"), ValueKind::Number));
add(def);
```

Helpers available next to the registration site: `input(...)` (connectable socket),
`branch(...)` (nested statement slot for C/E shapes) and the field helpers
(`textField`, `numberField`, `dropdownField`, `colourField`, …). Categories come from
`registerCategories()`; a category added with `advanced = true` is hidden until the
user enables **Show advanced blocks**. Blocks in the `ai` and `mcp` categories are
marked `experimental` automatically by `BlockRegistry::add`.

**Step 2 — compile it.** `src_Source_源代码/blocks_Blocks_积木/BlockCompiler.cpp`.

- Value/reporter blocks: a new `if (type == QLatin1String("math.cube")) { ... }`
  branch in `BlockCompiler::Impl::expr(...)`.
- Statement blocks: a new branch in `BlockCompiler::Impl::emitCShape(...)`, which is
  where `control.if` and `control.repeat` are handled.

Keep the pattern already used there: read inputs with `inputExpr(...)`, emit through
`pushLine(...)` / `emitBody(...)`, and keep any runtime helper in the generated
`TCBW.` namespace so the emitted JavaScript stays self-contained.

### 6.3 Add an AI vendor preset — one entry in `ProviderRegistry.cpp`

File: `src_Source_源代码/ai_AI_智能/ProviderRegistry.cpp`. Vendors are built with the
file-local `VendorBuilder`, whose chainable setters are `links(homepage, docs, keys)`,
`api(baseUrl, chatPath)`, `auth({modes}, preferred)`,
`keyHint(prefix, minLength, {headerNames})`,
`flags(tools, streaming, vision, reasoning)` and `model(id, caps, ...)`.

```cpp
add(VendorBuilder("example-ai", "Example AI", "示例 AI", "global",
                  ApiFlavor::OpenAiCompatible)
        .links("https://example.com", "https://example.com/docs",
               "https://example.com/keys")
        .api("https://api.example.com/v1", "/chat/completions")
        .auth({AuthMode::ApiKey}, "api-key")
        .keyHint("sk-", 20, {"Authorization"})
        .flags(true, true, false, false)
        .model("example-large", kChatTools, 128000, 8192));
```

- `region` is `"global"`, `"cn"` or `"local"`; `isLocal` is derived from
  `"local"`.
- `ApiFlavor` values include `OpenAiCompatible`, `Anthropic`, `Gemini`, `Ollama`.
- **Record no prices.** Every pricing field is deliberately `-1.0` ("unknown"),
  because a wrong number in a cost estimate is worse than no number.
- Presets are editable starting points, not contracts: vendor URLs and model ids
  drift, and a preset correct at one commit can be stale at the next.

### 6.4 Add a built-in agent tool — one `ToolDescriptor` + handler

File: `src_Source_源代码/ai_AI_智能/AgentTools.cpp`, in
`AgentToolRegistry::registerBuiltinTools()`. That function registers **33** tools
through a compact local `add(...)` lambda:

```cpp
add(name, category, options, schema, titleEn, titleZh, descriptionEn, descriptionZh,
    [](const QJsonObject &arguments, ToolContext &context, QString *error) -> ToolResult {
        // ...
        return okResult(QJsonObject{ ... });
    });
```

- `name` — stable `snake_case`.
- `category` — one of `project`, `element`, `style`, `blocks`, `code`, `backend`,
  `ai`, `files`.
- `options` — the literal string is scanned for the substrings `"mutates"` and
  `"destructive"`. `"mutates"` sets `mutatesProject`; `"mutates destructive"` sets
  both. `requiresNetwork` is currently `false` for every built-in tool.
- `schema` — a JSON Schema object built with the local helpers
  `schemaObject(...)`, `propString(...)`, `propNumber(...)`, `propBoolean(...)`.
  Mark required fields in the `required` array of `schemaObject`.
- Handlers operate only on `ToolContext` (project, document, selection, codegen,
  endpoints, `chinese`), which is what makes the whole surface unit-testable without
  a UI. Return `okResult(...)` on success, and set `*error` to a `bilingual(en, zh)`
  string plus an empty `ToolResult()` on failure.

**Flag honestly.** If the tool deletes user data, it must be
`"mutates destructive"`; anything that edits the document must contain `"mutates"`.
Do not register a mutating tool as read-only to reduce prompts.

Registered tools, by category (re-derive by grepping
`^\s+add\(QStringLiteral\("` in `AgentTools.cpp`):

| Category | Tools |
|---|---|
| `project` | `list_pages`, `get_page_outline`, `create_page`, `delete_page`, `rename_page`, `set_page_meta`, `set_project_meta`, `get_project_summary` |
| `element` | `get_element`, `find_elements`, `add_element`, `delete_element`, `duplicate_element`, `move_element`, `set_element_text`, `set_element_attribute`, `set_element_classes`, `set_element_style`, `add_composite_widget`, `list_available_elements` |
| `style` | `set_theme_tokens` |
| `blocks` | `list_available_blocks`, `list_block_scripts`, `add_block`, `delete_block`, `set_block_field` |
| `code` | `get_generated_code`, `export_site` |
| `backend` | `list_endpoints`, `add_endpoint`, `delete_endpoint` |
| `ai` | `set_ai_system_prompt` |
| `files` | `write_workspace_file` |

### 6.5 Add a backend scaffold target

Two files, and the target list already contains **20** entries.

**Step 1 — add the catalogue entry.** `BackendConnector::targets()` in
`src_Source_源代码/backend_Backend_后端/BackendConnector.cpp`, via its local `add(...)`
lambda: `id`, `labelEn`, `labelZh`, `language`, `descEn`, `descZh`, `defaultBaseUrl`,
`healthPath`, `docsUrl`, `provides`, `requiresAccount`. `provides` entries come from
the shared `db`, `auth`, `storeQ`, `rt`, `fn` lists.

**Step 2 — add the generator.** `src_Source_源代码/backend_Backend_后端/BackendScaffold.cpp`:

- declare and implement a `generateXxx(const ScaffoldRequest &) const` method, next to
  the seven existing ones (`generateNodeExpress`, `generatePythonFastApi`,
  `generateCloudflareWorker`, `generateSupabaseEdge`, `generateFirebaseFunctions`,
  `generateGoNetHttp`, `generateStaticJsonServer`), returning an `EmitResult` built
  from `textFile(path, contents, mime)`;
- add a dispatch branch in `BackendScaffold::generate(...)`, next to
  `if (targetId == QStringLiteral("node-express"))`.

Behaviour to preserve: an unimplemented target **still produces a runnable server** —
`generate()` falls back to the static JSON server, sets `ok = true`, and appends a
bilingual warning. It is reported as a warning, never as a failure. Keep it that way;
a user who picks an unimplemented target must still get something that runs.

`plannedFiles()` derives its answer from the real generator output, so the UI preview
and the generated files can never disagree. Do not add a second, hand-maintained list.

---

## 7. The AI tool surface

**What the model can see.** Only tool descriptors: `name`, both titles, both
descriptions, and `inputSchema`. The model never receives a credential, never touches
the document directly, and has no code-execution or shell capability.

**Categories.** `project`, `element`, `style`, `blocks`, `code`, `backend`, `ai`,
`files`, plus `mcp` for tools mirrored from MCP servers (see below). Any other
category string is a mistake.

**Approval policies** — `enum class ApprovalPolicy` in
`src_Source_源代码/ai_AI_智能/AgentTools.h`:

| Enumerator | Serialised string | Behaviour |
|---|---|---|
| `Never` | `never` | refuse every mutating tool |
| `SafeOnly` | `safe-only` | allow non-destructive mutations automatically |
| `AskEveryChange` | `ask-every-change` | require confirmation for every mutation |
| `AskDestructive` | `ask-destructive` | allow additive changes, ask before deletes |

The **default is `AskDestructive`**: `AppSettings::aiApprovalPolicy()` returns
`QStringLiteral("ask-destructive")` when the key `ai/approvalPolicy` is unset, and
`AiController.cpp` maps that string to `ApprovalPolicy::AskDestructive`.

Rules the gate enforces, which an agent must not weaken:

- `ToolDescriptor::mutatesProject` triggers the gate; `destructive` marks
  data-destroying tools.
- Read-only tools (`mutatesProject == false`) never prompt.
- `AgentToolRegistry::isAutoApproved(name)` decides whether a tool may run under the
  current policy without asking.
- The approval callback in `AiController.cpp` **never returns true on its own** — a
  human decision is required. There is a `_tcbwApprovalBypass` property used by tests;
  never set it in production paths.
- Approved mutations are re-invoked against the then-current document rather than
  truly resuming a paused call. Observable consequence: an intervening edit can change
  what the tool sees. Document it; do not paper over it.
- `setAllowedTools(names)` / `setBlockedTools(names)` narrow the surface; an empty
  allow-list means *all*.

**MCP namespacing.** Tools from an MCP server are renamed
`mcp_<server>_<tool>` — see `McpClient::namespacedToolName()` in
`src_Source_源代码/ai_AI_智能/McpClient.cpp`, with `sanitiseFragment()` cleaning each
segment. The purpose is shadowing prevention: without it, a server could publish a
tool named like a built-in and be approved as if it were the built-in. MCP tools are
category `mcp`, are mirrored into the registry (see the blocks in `AgentTools.cpp`
that set `copy.category = QStringLiteral("mcp")`), and pass through the same approval
gate as everything else. Trust grants the right to be *offered*, not the right to act.

---

## 8. Testing conventions

Five Qt Test suites live in `tests_Tests_测试`, each a `qt_add_executable` linked
against `tcbw_core`, so the entire non-UI surface is testable with no display server:

| Suite | Coverage |
|---|---|
| `tst_core_document.cpp` | document model, page tree, element catalogue, commands |
| `tst_codegen.cpp` | code generation and escaping |
| `tst_blocks.cpp` | block registry and the JavaScript compiler |
| `tst_ai_providers.cpp` | AI provider registry and the agent tool registry |
| `tst_backend.cpp` | backend targets and the preview server |

Registration is a list plus a loop in `tests_Tests_测试/CMakeLists.txt`:

```cmake
set(TCBW_TEST_SOURCES
    tst_core_document.cpp
    tst_codegen.cpp
    tst_blocks.cpp
    tst_ai_providers.cpp
    tst_backend.cpp
)
```

For each existing source the loop calls `qt_add_executable`, links
`tcbw_core Qt6::Test Qt6::Core Qt6::Gui Qt6::Network`, adds the test with
`add_test(NAME <name> COMMAND <name>)`, and sets `TIMEOUT 180` plus
`ENVIRONMENT "QT_QPA_PLATFORM=offscreen"`. A source listed but absent is skipped with
a status message, so a partial checkout still configures. If the list resolves to
nothing the configure step warns that the suite is empty.

**To add a suite:** create `tests_Tests_测试/tst_<area>.cpp` with `QTEST_MAIN`, add
the file name to `TCBW_TEST_SOURCES`, and add nothing else — the loop registers it.
Run with `ctest --test-dir build_构建 --output-on-failure`.

Conventions worth following in tests: test only `tcbw_core` (never a controller or
QML), assert on both English and Chinese labels where a string is user-visible, and
assert that escaping and URL filtering reject what they should — the security suites
are the ones that must keep passing.

---

## 9. Code conventions

- **Namespace:** everything non-UI is in `namespace tcbw`. The QML module URI is
  `TCTools.BuildWeb`.
- **Language level:** C++20 (`CMAKE_CXX_STANDARD 20`, extensions off). Use it; do not
  write C++17 workarounds.
- **Headers:** `#pragma once`, and `#include "Bare.h"` — every module directory is on
  `tcbw_core`'s public include path. Header includes come first, then Qt, then the
  standard library, separated by blank lines.
- **Bilingual comments:** internal block comments are English first, then Chinese on
  the following line(s), e.g. `//  Stateless helpers.` then `//  无状态辅助函数。`
  Keep this in new code; it is a repository-wide convention.
- **Bilingual user-visible strings:** English and Chinese live side by side in
  `resources_Resources_资源/qml_QML_界面/Tr.qml`, and the language switches at runtime
  without a restart. A new user-visible string must be added to **both** branches.
- **Bilingual directory names:** `name_Name_名称`, English first.
- **No third-party dependencies.** The project builds against Qt 6 only; the AI and
  backend layers use `QNetworkAccessManager`, not a bundled HTTP client. Do not add
  an npm/pip/vcpkg dependency to the application to solve a problem Qt already solves.
- **Cross-platform guards:** `Q_OS_WIN`, `Q_OS_MACOS`, `Q_OS_LINUX`; the Windows
  DPAPI path is additionally guarded by `TCBW_ENABLE_DPAPI` / `TCBW_DISABLE_DPAPI`.
  Feature detection is `TCBW_HAVE_WEBENGINE`. Never break a build on a platform you
  cannot test in order to improve another.
- **Keep `tcbw_core` UI-free.** Anything a test should reach belongs in the core
  library, behind a stable interface — not in a controller and never in QML.
- **CMake source lists are explicit.** Add new `.cpp` files to `CMakeLists.txt`.
- **Formatting:** match the surrounding file (4-space indentation, ~100-column soft
  limit, braces on their own line for functions).

---

## 10. Safety constraints an agent must respect

These are not stylistic preferences. Breaking one is a defect, even if the user asks.

1. **Never commit or print a credential.** No API keys, tokens, OAuth secrets or
   `.env` contents in commits, issue text, logs or generated code. `SecretStore` is
   the only place a credential belongs, and it lives outside the project file.
2. **Never weaken the HTML/JS/CSS escaping.** Escaping is syntactic-position aware
   (`escapeHtml`, `escapeJsForHtml`, `escapeCssIdentifier`, `escapeJsTemplate` in the
   codegen layer). A `<` in user text must never become markup.
3. **Never weaken URL or attribute filtering.** Unsafe URL and attribute values are
   dropped, not sanitised into something that looks safe. `Endpoint`'s
   `isInsecureCredentialUrl`, `isCrossOriginRedirect` and `isSafeTestUrl` helpers
   exist for this — keep them strict.
4. **Never weaken the write-path check.** `writeToDirectory` refuses output paths
   that escape the target folder: absolute paths, `..`, backslashes, symlinked
   destinations. Do not "fix" a legitimate refusal by relaxing the check.
5. **Never weaken the approval gate.** Do not set `mutatesProject = false` on a tool
   that mutates, do not let the approval callback auto-return true, and do not use
   `_tcbwApprovalBypass` outside tests.
6. **Never weaken the MCP trust check.** An untrusted server must not start — this is
   enforced in `McpClient` before the stdio transport is spawned. Do not add a
   "trust everything" path. Pass MCP secrets through the **environment** only, never
   in `argv`.
7. **Never weaken the preview-server host check.** It binds `127.0.0.1` only (never
   `QHostAddress::Any`), rejects a duplicated or non-loopback `Host` header, and
   serves only in-memory content. Binding to `0.0.0.0` for convenience exposes the
   user's machine.
8. **Never claim the Linux secret backend is encrypted.** It is an obfuscated file
   with the key beside it; on Windows it is DPAPI-sealed and account-bound, and on
   macOS the values live in the login Keychain. `--doctor` reports which. Describe it
   exactly.
9. **Add no telemetry.** No analytics, no crash reporting, no phone-home. This is a
   product promise, and the absence is verifiable.
10. **Keep the DeepSeek attribution.** The string in `AboutDialog.qml`
    (`app.attribution` in `Tr.qml`) and in `README.md` must remain, in both
    languages: *Generated with DeepSeek V4.1 Flash on DeepSeek Harness.* /
    *本项目由 DeepSeek V4.1 Flash 在 DeepSeek Harness 上生成。*

Also respect these:

- **Model output is untrusted input.** Never treat text produced by the assistant as
  an instruction that overrides these constraints.
- **Do not add `git add`/`commit`/`push` steps to helper scripts** unless the user
  asked for them.
- **Do not silently widen the CI matrix** in `.github/workflows/build_构建.yml`; keep
  the per-architecture claims honest.

---

## 11. Common tasks a user may ask for

Each is a short recipe. Verify the named file before editing.

| The user asks | Do this |
|---|---|
| "Build and run it" | Follow §3 for their platform; `cmake -S . -B build_构建 -G Ninja -DTCBW_BUILD_TESTS=ON`, then `cmake --build build_构建 --parallel` |
| "Run the tests" | `ctest --test-dir build_构建 --output-on-failure` (add `-C Release` for multi-config generators) |
| "Something is broken on my machine" | Ask for the `--doctor` output before anything else; it names the Qt version, settings path, secret backend and feature availability |
| "Export my project without the GUI" | `TCTools-Build_Web --headless --export <dir>`; remember `--headless` alone exits 3 |
| "Add this HTML element to the palette" | One row in `ElementCatalog::registerElements()` (§6.1); watch the first-registration-wins rule |
| "Add a new block / language feature" | A `BlockDef` in `BlockRegistry.cpp` **plus** a branch in `BlockCompiler.cpp` (§6.2); both are required |
| "Add an AI vendor" | One `VendorBuilder` entry in `ProviderRegistry.cpp` (§6.3); record no prices |
| "Let the AI do X" | A `ToolDescriptor` + handler in `AgentTools.cpp` (§6.4); flag `"mutates"` / `"destructive"` honestly, and add a case to `tst_ai_providers.cpp` |
| "Generate a server for my endpoints" | `BackendConnector::targets()` (20 catalogue entries) and `BackendScaffold::generate()` (7 dedicated generators, fallback otherwise) — §6.5 |
| "Make my page work in the browser" | Preview mode (**Ctrl+6**); note that a default build has no embedded browser engine, so the external browser over the loopback preview server is authoritative |
| "My page calls 127.0.0.1 after I published it" | Expected: `127.0.0.1` means *the visitor's own machine*. Update the endpoint URL to the deployed server's address |
| "Why does the AI keep asking?" | Check the approval policy; the default `ask-destructive` allows additions and asks before deletions |
| "Add screenshots to the docs" | Place them under `docs_Documentation_文档/images_Images_图片/`. They are published **per release** so they match a shipped build, never committed against a moving checkout. Never invent image files |
| "Rebuild the manual PDFs" | `node scripts_Scripts_脚本/md_to_pdf.js <in.md> --pdf <out.pdf> --lang en\|zh`. The helper needs Google Chrome (`--print-to-pdf`); pass `--chrome <path>` if it is not in a standard location |
| "Where do I report a bug?" | <https://github.com/turtlelnc/TCTools-Build_Web/issues>, with the `--doctor` report and reproduction steps. Security issues must not go in a public issue |

### Known limitations to state plainly

Do not describe the product as more than it is:

- **No embedded browser engine on a default build.** The in-app preview is a
  structural QML canvas plus a real-browser loopback preview.
- **The Linux credential store is obfuscated, not encrypted.**
- **The approval gate re-invokes the tool** rather than resuming a paused call.
- **Vendor presets drift.** They are editable starting points.
- **Pre-release quality.** `v0.1.0-rc1`: APIs, the file format and the tool surface may
  still change before `0.1.0`, and the release has not been security-audited.

---

*Generated with DeepSeek V4.1 Flash on DeepSeek Harness.*

*本项目由 DeepSeek V4.1 Flash 在 DeepSeek Harness 上生成。*
