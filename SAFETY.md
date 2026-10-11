# Safety and Security — TCTools-Build_Web

**English** | [中文](SAFETY.zh.md)

Applies to **TCTools-Build_Web (`TC-BuildWeb`) v0.1.0-rc1** on branch `main`.

---

## Release-candidate status

TCTools-Build_Web is a **release candidate**, not finished software. It has **not
been security-audited**. It must not be treated as secure, hardened, or
production-ready, and it must not be relied on as the only control protecting
anything you care about.

The application stores credentials, makes authenticated network requests on your
behalf, runs programs you configure, writes files to disk, and generates markup
that you may publish to the internet. Incorrect model output, a defect, a
misconfiguration, a hostile project file, a malicious MCP server, or prompt
injection arriving through AI context can each cause data loss, credential
exposure, or the publication of content you did not intend. The mitigations
described below are real and are implemented in the code, but they reduce risk;
they do not eliminate it.

---

## 1. Purpose and scope

This document describes the security-relevant behaviour of TCTools-Build_Web and
the risks a user accepts by running it. It covers:

| Surface | What is in scope |
|---|---|
| **The desktop application** | The C++20 / Qt 6 process, its QML user interface, its local files, and the credentials it stores. |
| **The code it generates** | HTML, CSS and JavaScript emitted by the `codegen_CodeGen_代码生成` module, in all four export modes (`SingleHtml`, `HtmlWithAssets`, `ScriptOnly`, `HeadlessBundle`). |
| **The preview server** | The loopback HTTP server in `backend_Backend_后端/PreviewServer.cpp` used for real-browser preview. |
| **The AI integration** | The vendor registry, `SecretStore`, `AuthManager`, the HTTP client (`AiProvider`), the agent tool registry (`AgentTools`) and the agent runtime (`AgentRuntime`). |
| **The MCP client** | `ai_AI_智能/McpClient.cpp` and the `mcp_<server>_<tool>` tool surface it contributes. |
| **The packaging and CI pipeline** | `packaging_Packaging_打包/`, `scripts_Scripts_脚本/` and `.github/workflows/`, including what those workflows are permitted to do. |

**Out of scope.** This document does not describe the security of any third-party
AI vendor, MCP server, backend platform or hosting provider you choose to
connect to. Those are separate systems with their own security properties and
their own terms. It also does not describe the security of the operating system
or the Qt runtime — a compromised machine or a compromised Qt installation is
assumed to be outside the application's ability to defend against.

---

## 2. Intended use and non-goals

**Intended use.** TCTools-Build_Web is a desktop page builder for people who do
not write code. It lets a user compose a page visually, add behaviour with
block programming, optionally connect a backend and optionally use an AI
assistant, then export ordinary `.html` / `.js` files that the user hosts
somewhere else.

**Non-goals.** The application is explicitly *not*:

- **A sandbox.** It runs with your user account's privileges. It does not confine
  the AI, the MCP servers you configure, or the code it generates. There is no
  container, no seccomp filter, no OS-level isolation of any kind.
- **A hosting provider.** It publishes nothing. Exporting writes files to a
  directory you chose. Where those files go afterwards is entirely your decision
  and your responsibility.
- **A secret manager of record.** `SecretStore` is a convenience store for
  credentials the application needs in order to work. It is not an audited
  vault, it has no access-control model of its own, and on Linux it does not
  even provide confidentiality against a local attacker. Keep your authoritative
  credentials with the vendor or in a real password manager.
- **A substitute for reviewing what you publish.** Generated markup is escaped
  and filtered, but the application has no idea what your site is supposed to
  say, and no tool can tell you whether what it generated is what you meant. Read
  the exported page before you publish it.
- **A content-safety or compliance tool.** It does not moderate AI output, filter
  for illegal content, check licenses of AI-generated text or images, or verify
  factual claims.

---

## 3. Threat model

### 3.1 Assets

| Asset | Where it lives | Why it matters |
|---|---|---|
| **User projects** | `.tcbw` files on disk, plus their autosave and crash-recovery copies under the per-user application data location | Represents hours of work; may contain unpublished or confidential content. |
| **API keys and OAuth tokens** | The `SecretStore` vault | Bearer credentials that bill against the user's account and act with the user's authority at the vendor. |
| **The user's machine** | Files, processes, network | The application runs with full user privileges. |
| **Generated sites** | The export directory, then wherever the user hosts it | Published under the user's name to an audience the user does not control. |
| **The user's browser session** | The browser the user opens the loopback preview in | A page served by the preview server executes in the browser's origin context. |

### 3.2 Adversaries, and what actually mitigates them

| Adversary | Attack path | Mitigations that exist in the code | Residual risk |
|---|---|---|---|
| **A malicious `.tcbw` project received from someone else** | Project JSON is opened by the user; embedded text, URLs, attribute names, style values, endpoints, headers or scripts could be crafted to escape or exfiltrate. Project files are JSON and can contain arbitrary user strings. | Every user-controlled string is escaped **by syntactic position** when emitted (text, attribute value, script context, CSS identifier), unsafe URL and attribute values are dropped, and `writeToDirectory` refuses output paths that escape the target directory (absolute paths, `..`, backslashes, NUL bytes, symlinked destinations, canonical-path escapes). A project cannot itself cause an HTTP request to be sent; the user must press the button that does it. | Opening a hostile project is not treated as a hard security boundary. Treat an untrusted `.tcbw` as untrusted input: open it, inspect what it contains, and only then use features that contact a network or run a scaffold. |
| **A malicious or compromised MCP server** | The server is a program the user configures. It can lie about its tool list, return hostile content, or attempt to influence the model through its output. | An MCP server is **not started at all** unless the user has explicitly marked it trusted. Its tools are exposed under the namespaced `mcp_<server>_<tool>`, so it cannot shadow a built-in tool name. Its secrets travel in the process environment, never in `argv`. Its stderr is treated as diagnostics only — redacted and truncated, never parsed. Every resulting tool call still passes the approval gate. | A trusted MCP server is trusted. Trusting one is equivalent to running that program. Its returned text is untrusted input to the model, so prompt injection from a server is possible (see §5). |
| **Prompt injection arriving through AI context** | Text the model reads — project content, a fetched description, an MCP tool result, a transcript — contains instructions that the model follows, causing the agent to attempt an action the user did not ask for. | Mutating actions are not model output alone: each is a **declared tool with a JSON-Schema input** (`additionalProperties: false`), it is classified as mutating or destructive, and it must pass the approval gate unless the configured policy auto-approves that class. The model can never write the document directly. | A model can still be persuaded to *propose* a harmful-but-well-formed call, and an auto-approving policy will run it. The gate is a human decision point; its value depends entirely on the human reading it. |
| **A malicious web page reaching the loopback preview server** | A page in the user's browser (or a DNS rebinding attack that points an attacker-controlled name at `127.0.0.1`) issues requests to the preview port to read generated content or reach anything else the server does. | The preview server binds `127.0.0.1` only — never `QHostAddress::Any`. It requires a loopback `Host` header, so a request whose `Host` is `evil.example` (or `localhost.attacker.example`) is refused even though the TCP connection arrived on loopback. A duplicated `Host` header is treated as fatal. It serves only in-memory generated files: there is no filesystem traversal surface and no project on disk is exposed. | Any local process, and any browser page that can reach the port *and* present a loopback `Host`, can read the previewed page. Preview content is by definition content the user is working on, so this is a small exposure — but it is not zero, and the preview server should not be left running on a shared machine. |
| **A network attacker on plain HTTP** | Interception or modification of an authenticated request, or credential theft from a plaintext request. | No credential is sent over plaintext HTTP except to a loopback address. OAuth authorize, token, device-code and refresh endpoints are required to be `https` (loopback may use `http`). Authenticated requests do not follow cross-origin redirects. | Local inference servers on `http://127.0.0.1` are allowed by design. `/etc/hosts` manipulation or a hostile local proxy on a plain-`http` loopback configuration is outside the model. |
| **A local attacker with filesystem access** | Reads or replaces the credential vault, the settings file, autosave records, or the generated output. | On Windows the vault is sealed with DPAPI and bound to the user's account on that machine, so copying the blob to another machine or account does not yield the plaintext. On macOS, values live in the login Keychain. Generated output is written atomically (`QSaveFile`) so an interrupted write cannot leave a half-written page. | **On Linux the vault is obfuscated, not encrypted: the key sits beside the ciphertext, so any attacker who can read the files can recover every secret.** This is a genuine limitation, not a nuance — see §4. On every platform, an attacker who already has the user's account has the user's credentials. |

---

## 4. Credential handling

`SecretStore` (`ai_AI_智能/SecretStore.cpp`) is the single place where credentials
live. Its behaviour is platform-specific, and the differences are material.

| Platform | Backend | What it actually provides |
|---|---|---|
| **Windows** | DPAPI (`CryptProtectData` / `CryptUnprotectData`) | The vault blob is encrypted and integrity-protected, bound to the current Windows user account on that machine. Copying the file to another machine or another user does not decrypt it. The application reports this backend as **OS-backed**. |
| **macOS** | Login Keychain, driven through `/usr/bin/security` with the service name `com.turtlelnc.tctools.buildweb` | Secret **values** live in the Keychain and are protected by the operating system. The on-disk file holds a key **index** (names only), never the values. The application reports this backend as **OS-backed**. |
| **Linux (and anything else)** | An obfuscated file with a per-install key file stored beside it | **This is obfuscation and not a security boundary.** The ciphertext is an XOR of the JSON payload with a keystream derived from a key file that is stored next to the vault. Anyone who can read both files can recover every secret. It defeats casual inspection of a file listing or a backup; it does not defeat an attacker. The application reports this backend as **obfuscated**, and the in-app description says so plainly rather than implying protection. |

**What this means in practice.**

- On Windows and macOS, the credential store inherits the protection of the
  operating system's user-bound secret facility, with the usual caveats: it
  protects against another user on the same machine and against a stolen disk
  image, not against malware running as you.
- **On Linux, do not store a credential in this application if a local attacker
  reading your files is in your threat model.** Use a short-lived or
  narrowly-scoped key instead, rotate it, and prefer the vendor's own console for
  anything long-lived.
- The vault file's permissions are set owner-read/write only. That matters — but
  it does not change the point above, because the key file is subject to the same
  permissions and an attacker who can read one can read the other.
- The reported backend and whether it is OS-backed are both visible in
  `--doctor` output and in the settings interface, so you never have to guess
  which situation you are in.
- Credentials are never written into a project file, never sent to the model, and
  never included in an exported site. The agent surface exposes no tool that
  returns a secret value.

---

## 5. AI-specific risks

The AI assistant (TI-UseAgent) is the part of the application that takes the most
care to constrain, because it is the part that consumes untrusted text and can
change your project.

**Prompt injection is expected, not hypothetical.** The model reads your project
content, tool results, and — if you configure one — MCP server output. Any of
that text can contain instructions. The application does not attempt to detect
injection heuristically; it constrains what a successful injection can *do*:

- **Model output is untrusted input.** A model response is parsed, validated
  against the declared schema of a known tool, and either rejected or turned into
  a proposed tool call. It is never executed as code, never evaluated, and never
  used as a path or a command line.
- **AI-driven edits pass an approval gate.** Tools that mutate the project are
  flagged as such; destructive tools are flagged separately. The approval policy
  determines which classes are auto-approved and which require an explicit
  decision, and the UI shows the pending call for review before it runs. The
  default posture is to ask.
- **A declared JSON-Schema tool surface.** The agent's capabilities are an
  enumerable list of named tools, each with an object schema whose
  `additionalProperties` is `false`. There is no "run this script" tool and no
  arbitrary-code tool. If an action is not a declared tool, the model cannot take
  it.
- **The AI never receives a raw credential.** Requests are assembled by the HTTP
  client from the stored credential at the moment of the request; the secret is
  placed in the appropriate header or query parameter and is not part of the
  conversation, the tool result, or the transcript.
- **The AI never touches the document directly.** It acts through the same
  command layer the UI uses. Agent edits therefore appear on the same undo stack
  as manual edits and can be undone like any other change.
- **Transcripts are redacted before export.** Error details, log lines and any
  vendor-supplied text pass through `redactSecrets` / `redactForPrompt` before
  they reach the UI, the log, or an exported transcript. This is a filter for the
  common shapes of credential, not a guarantee that no sensitive string can ever
  appear in a transcript you choose to share.
- **The request body is not logged in full**, because it may contain your project
  text.
- **A model can still be wrong.** It will produce plausible, well-formed, and
  incorrect edits. It may misread your intent, invent an element, delete the
  wrong node, or write copy that is factually false. **You own the final
  artifact.** Review the diff, use undo, and read the export before publishing.

**Prompt-injection guidance.** Treat any text the model has not written itself as
hostile: an imported project, a description pasted from the web, an MCP tool
result. If you see the agent proposing a call you did not ask for, decline it —
and consider whether the context that produced it is worth keeping.

---

## 6. MCP risks

The Model Context Protocol client lets you extend the assistant with external
tool servers. It is the most powerful extension point in the application, and it
is treated accordingly.

- **An MCP server is a program you choose to run.** It executes with your
  privileges, on your machine. Configuring one is the same class of decision as
  running any downloaded binary, and it deserves the same scrutiny.
- **Untrusted until explicitly trusted.** A stdio server is **not launched** at
  all unless its configuration is marked trusted. Launching an untrusted program
  is never acceptable, and the client refuses it with a message telling you to
  review and mark it trusted in settings. There is no "just try it" path.
- **Tool names are namespaced.** A server's tools are exposed to the model as
  `mcp_<server>_<tool>`, with each fragment sanitised. A server therefore cannot
  present a tool named like a built-in one and shadow it — the built-in name and
  the namespaced name are distinct, and the server's own tool name is only used
  when calling back into that server.
- **Secrets go through the environment, never `argv`.** Command lines are visible
  to other processes on the machine and are routinely captured in logs, crash
  reports and process listings. Server secrets are therefore injected into the
  child process environment. Environment values that look like credentials are
  additionally kept out of error messages and logs.
- **Server output is diagnostics, not instructions.** A server's stderr is
  redacted, truncated, and never parsed for control information.
- **Approval still applies.** Tools that a trusted MCP server contributes remain
  subject to the same mutating/destructive classification and the same approval
  gate as built-in tools.

**What MCP trust does *not* buy you.** Trusting a server says "I have reviewed
this program and I am willing to run it". It does not sandbox it, does not
restrict which files it reads, does not stop it from making network requests, and
does not make its output truthful.

---

## 7. Generated content

The code generator is written on the assumption that every user-supplied string
is hostile and every user-supplied URL is wrong until proven otherwise.

- **Escaping by syntactic position.** HTML text nodes, attribute values, script
  contexts and CSS identifiers each require different escaping, and the emitter
  applies the escaping that matches the position it is writing into. A string
  that is safe in a text node is not safe in an inline script, and the generator
  does not pretend otherwise.
- **URL and attribute filtering.** Values that are not safe URLs are dropped
  rather than emitted, and attribute names and tag names that are not safe are
  rejected. This closes the common `javascript:`-URL and event-handler-attribute
  injection paths.
- **Safe output paths.** `writeToDirectory` refuses a file whose relative path is
  empty, contains a NUL byte, contains a backslash, is absolute, traverses with
  `..`, resolves outside the export directory after canonicalisation, or targets
  an existing symlink. The export directory itself may not be a symlink, and the
  canonical parent is re-checked after directories are created, so a symlink
  introduced during the write cannot redirect output.
- **Atomic writes.** Files are written through `QSaveFile`, so an interrupted or
  failed export cannot leave a partially written page where a complete one used
  to be.

**Why exported sites still need your own review.** Escaping prevents injection
through *markup structure*. It does not make the *content* correct, safe or
appropriate. The generator cannot know that a link points somewhere you did not
intend, that a backend endpoint you configured is not yours, that AI-written copy
is false, that an embedded image is licensed for your use, or that third-party
code you pre-linked has its own behaviour. Those are editorial and legal
decisions that belong to the person publishing the page.

---

## 8. Network exposure

The application opens exactly one listening socket, and only while a preview is
running.

| Property | Implementation |
|---|---|
| **Bind address** | `127.0.0.1` only (`QHostAddress::LocalHost`). The code deliberately never binds `QHostAddress::Any`, because a preview server reachable from the network would publish the user's in-progress work to it. |
| **Host header** | A request must carry a loopback `Host` header. This is what stops a malicious page, or a DNS-rebinding name pointed at `127.0.0.1`, from reaching the preview: the browser will send the attacker's hostname in `Host`, and the request is refused. Bare `localhost` and `::1` are accepted; lookalikes such as `localhost.attacker.example` are not. |
| **Duplicate Host** | A duplicated `Host` header is treated as fatal rather than resolved arbitrarily. |
| **Served content** | Only in-memory generated files. The server has no filesystem root, so there is no directory-traversal surface and no route to the project file or any other file on disk. |
| **Port** | The caller supplies a preferred port and the server binds loopback on it; if that port is taken the start fails and reports the reason rather than silently falling back to another port. The resulting `http://127.0.0.1:<port>/` URL is what the application hands to the browser. |
| **Lifetime** | The socket exists only for the preview session. Stop the preview (or exit the application) and nothing is listening. |

The preview is intentionally a **plain-HTTP loopback** service. Traffic never
leaves the machine, which is why plain HTTP is acceptable here and nowhere else.

---

## 9. Outbound request safety

| Rule | Implementation |
|---|---|
| **No credential over plaintext HTTP, except to loopback** | OAuth authorize, token, device-code and refresh endpoints must be `https`; only a loopback host (`127.0.0.1`, `::1`, `localhost`) may use `http`. Requests that would send a credential to a non-`https` endpoint are refused before they are sent. |
| **No cross-origin redirect following for authenticated requests** | Authenticated requests use a no-less-safe redirect policy and do not follow a redirect that crosses origin, so a credential cannot be replayed to a host the user did not authorise. |
| **Cloud metadata endpoints are blocked** | Instance-metadata services are refused outright so that a request test cannot be used as an SSRF primitive against the machine's own cloud credentials: `metadata.google.internal`, `metadata.goog`, `instance-data`, `169.254.169.254`, `100.100.100.200`, and the entire `169.254.0.0/16` link-local block that AWS, Azure and OpenStack use for instance metadata. |
| **Bounded requests** | Redirects, stored response bodies and health-probe duration are all capped, so a hostile or broken server cannot make the builder hang or exhaust memory. |
| **Header hygiene** | A header name must be an RFC 7230 token or it is dropped, and CR/LF/NUL are stripped from header values, so a value coming from a project file cannot smuggle a second header or split a response. |
| **Local endpoints are allowed on purpose** | Local inference servers (Ollama, LM Studio, vLLM, llama.cpp, Jan, text-generation-webui) are presets, and loopback `http` is permitted for them. This is a deliberate exception: the traffic does not leave the machine. |

**What is outside this model.** The application does not verify that the host a
preset points at is the vendor it claims to be. A wrong or tampered preset, a
hosts-file entry, a hostile DNS resolver, or a corporate TLS-interception proxy
can each redirect a request. Verify the endpoint of any vendor you configure, and
never paste a credential into a preset you have not checked.

---

## 10. Data collection

**None.**

- **No telemetry.** The application ships no analytics, no usage reporting, no
  crash reporting service, no update check, and no phone-home of any kind. It
  contains no analytics SDK.
- **No account.** There is no sign-in to this project, no licence server, and no
  server operated by the authors that the application talks to.
- **Outbound traffic happens only when you cause it**: when you send a message to
  an AI vendor you configured, when you authenticate to that vendor, when you
  test a backend endpoint, or when a trusted MCP server you configured makes its
  own requests. You can observe every one of these in the UI.

**Exactly what is written to disk, and where.**

| What | Where | Contains |
|---|---|---|
| **Settings** | The Qt settings file for the application; its exact path is printed by `--doctor` as *Settings file* (typically under the Qt per-user config location, e.g. `%APPDATA%\turtlelnc\TCTools-Build_Web.ini` on Windows) | Preferences, theme and language choice, window state, the recent-file list, and backend endpoint definitions you created. |
| **Secret vault** | The `SecretStore` vault file (and, on macOS, the Keychain items it indexes; on Linux, the key file beside the vault) | Your API keys and OAuth tokens. Never your project content. |
| **Projects** | The `.tcbw` file you saved, wherever you chose to save it | The whole project: pages, element tree, styles, block programs, and any endpoint definitions stored with the project. |
| **Autosave** | A per-user `autosave` directory under the application's data location, with the file name derived from a SHA-1 hash of the project's canonical path | A recent copy of the project, so an interrupted session does not lose work. It is deliberately **not** written beside the project file, so a failed or partial autosave can never damage the saved project. |
| **Crash-recovery record** | The same per-user application data location | Enough state to offer recovery of unsaved work after an abnormal exit. |
| **Generated output** | Only the export directory you chose | The exported site files. |
| **CI logs and artifacts** | GitHub Actions, only for this repository's own builds | Build logs and compiled artifacts. **No user data**: CI runs on the repository's source, never on your machine or your project. |

The application does not write a usage log, and does not upload any of the above.
Deleting a project does not automatically delete its autosave or recovery record;
remove those files yourself if you need a clean slate, and note that on macOS the
Keychain items are removed when you clear credentials from within the
application.

---

## 11. Operator and user responsibilities

If you run this application, you are responsible for:

1. **Not treating it as a sandbox.** Run it as an ordinary user, with the least
   privilege you can, and keep backups of anything it can write to.
2. **Understanding your secret backend.** Check `--doctor`. If the secret backend
   reads *obfuscated* (expected on Linux), accept that your stored keys are
   readable by anyone who can read your files, or store keys elsewhere.
3. **Choosing keys and scopes deliberately.** Prefer narrowly-scoped, short-lived,
   spend-limited keys over full-account keys, and rotate anything you have pasted
   into any application.
4. **Vetting what you connect.** Review an MCP server's code or provenance before
   trusting it. Verify an AI vendor endpoint before storing a key against it.
   Assume any preset may be out of date.
5. **Reading the approval prompts.** The approval gate is the control that stops
   an unwanted AI edit. Approving without reading defeats it, and an
   auto-approving policy deliberately removes it.
6. **Reviewing your export.** Read the generated page — content, links,
   embedded third-party code and endpoint configuration — before you publish it.
7. **Protecting what you publish.** Do not generate and publish content that
   reveals personal data, internal hostnames, credentials, or material you have
   no right to distribute.
8. **Keeping the software current.** Track `main` or the latest `rc`. Fixes land
   there and nowhere else.

If you are the **operator** distributing builds of this software, you are
additionally responsible for building from a trustworthy revision, for the
integrity of the artifacts you distribute (signatures, hashes), and for any
legal obligations that arise from distributing it in your jurisdiction.

---

## 12. Reporting a vulnerability

Please report security issues **privately**, not in a public issue.

**Use GitHub private vulnerability reporting:**
<https://github.com/turtlelnc/TCTools-Build_Web/security/advisories/new>

**What to include.** A report that can be reproduced is worth more than a report
that sounds alarming:

- The version or channel (`v0.1.0-rc1`, or the commit hash if you built from
  source) and the platform and architecture.
- What you did, step by step, from a clean start where possible.
- What you expected to happen, and what happened instead.
- The smallest input that demonstrates it — a `.tcbw` project, a preset
  definition, an MCP server configuration, or the request/response involved.
- Why you believe it is a security issue, and what an attacker gains.
- Whether you intend to disclose publicly, and on what timeline.

**Please redact credentials, tokens and personal data** from anything you attach.
If a secret must be shown to demonstrate the issue, use an already-revoked one.

**What to expect.** This is a hobby-scale release candidate without a security
team, a guaranteed response time, or a bug-bounty programme. A realistic
expectation is an initial acknowledgement **within roughly 7 to 14 days**, and a
first assessment shortly after that. A confirmed issue will be fixed on `main`
and noted in the release notes for the next `rc`; a report that needs more
information will get a question rather than a silence. There is no paid reward,
and no commitment is made about the order in which issues are addressed.

**Please do not** open a public issue, a public pull request, or a public
discussion for a suspected vulnerability before it has been assessed.

---

## 13. Supported versions

| Version | Supported |
|---|---|
| `main` | Yes — fixes land here first |
| The latest `rc` release | Yes — best effort |
| Any earlier `rc` or pre-release | **No** |
| Forks and modified builds | **No** — report to whoever built it |

Only the current line is maintained. There is no long-term-support branch, no
back-porting to older release candidates, and no security-fix guarantee for a
build you obtained from anywhere other than this repository.

---

## 14. Change log of the document

| Date | Version | Change |
|---|---|---|
| 2026-10-11 | 1.0 | Initial document, published with `v0.1.0-rc1`. Establishes the threat model, the per-platform credential story (including the explicit Linux obfuscation statement), the AI and MCP risk sections, network and outbound-request rules, the data-collection statement, and the reporting process. |

This document is versioned with the repository. Material changes to security
behaviour should be accompanied by an entry here in the same pull request.

---

## No warranty or liability

Use TCTools-Build_Web at your own risk. It is provided without warranty under the
[MIT License](LICENSE). To the maximum extent permitted by applicable law, the
authors and copyright holders are not responsible for damage to computers, loss
or disclosure of data, loss of files, credential compromise, or content you
published, arising from use of this project.
