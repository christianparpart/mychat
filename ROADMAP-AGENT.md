# Agentic Coding Assistant Roadmap

> Extending **mychat** from a local-model chat application into a full agentic coding assistant,
> comparable to Claude Code / OpenCode.

---

## Current Foundation

mychat already provides a solid base for agentic operation:

| Component | Location | Status |
|-----------|----------|--------|
| Agent loop with multi-step tool calling | `src/agent/AgentLoop` | Working (max 10 steps, sequential tool execution) |
| MCP client/server infrastructure | `src/mcp/` | Working (stdio transport, multi-server routing, MCP 2024-11-05) |
| LLM inference engine | `src/llm/LlmEngine` | Working (llama.cpp, streaming, `<tool_call>` XML parsing) |
| Tool type definitions | `src/core/Types.hpp` | `ToolCall`, `ToolResult`, `ToolDefinition` with JSON Schema |
| Streaming markdown renderer | `src/tui/MarkdownRenderer` | Working (headings, code blocks, bold/italic, `<think>` blocks) |
| Input field with Emacs keybindings | `src/tui/InputField` | Working (grapheme-aware, history, kill ring, multiline) |
| Chat session management | `src/llm/ChatSession` | Working (system/user/assistant/tool message roles) |
| Configuration system | `src/mychat/Config` | Working (TOML, XDG paths, MCP server configs) |
| Log panel | `src/tui/LogPanel` | Working (collapsible, scrollable, thread-safe) |

---

## Phase 1: LLM Provider Abstraction

**Goal:** Replace the monolithic `LlmEngine` (llama.cpp-only) with a provider interface that supports
both local and remote LLM backends.

### 1.1 Provider Interface

Create an abstract base class that all providers implement:

```
src/llm/
├── LlmProvider.hpp          # Abstract interface
├── LlamaProvider.hpp/.cpp   # Existing llama.cpp backend (refactored from LlmEngine)
├── HttpProvider.hpp/.cpp     # Base for HTTP-based API providers
├── ClaudeProvider.hpp/.cpp   # Anthropic Claude API
├── OpenAiProvider.hpp/.cpp   # OpenAI API (and compatible endpoints)
└── LlmEngine.hpp/.cpp       # Thin facade over the active provider
```

**Interface contract:**

- `generate(messages, tools, sampler, streamCb) → Result<GenerateResult>` — same signature as current `LlmEngine::generate`
- `supportsToolUse() → bool` — whether the provider handles tool calls natively
- `contextSize() → int` — effective context window
- `modelInfo() → ProviderModelInfo` — model name, provider name, capabilities

### 1.2 Tool Call Wire Formats

Each provider has its own tool-calling convention:

| Provider | Format |
|----------|--------|
| llama.cpp (current) | `<tool_call>{"name":...,"arguments":...}</tool_call>` XML in text |
| Claude API | Structured `tool_use` content blocks in the response |
| OpenAI API | `tool_calls` array in the assistant message with function name + JSON args |
| OpenAI-compatible | Same as OpenAI (Ollama, vLLM, LM Studio, etc.) |

The provider layer normalizes all formats into the existing `GenerateResult { text, vector<ToolCall> }` structure.

### 1.3 HTTP Client Infrastructure

- Add a lightweight HTTP client (libcurl or cpp-httplib) for API calls
- Support streaming via SSE (Server-Sent Events) for Claude and OpenAI APIs
- Map streaming chunks to the existing `StreamCallback` so the TUI renderer works unchanged

### 1.4 API Key Management

- Store API keys in the existing config file (`~/.config/mychat/config.toml`) under `[providers]`
- Support `$ANTHROPIC_API_KEY`, `$OPENAI_API_KEY` environment variable overrides
- Never log or display API keys — mask in config dump/debug output

### 1.5 Provider Selection

Extend `AppConfig` with provider configuration:

```toml
[llm]
provider = "claude"            # or "openai", "openai-compat", "local"
model = "claude-sonnet-4-5-20250514"

[llm.local]
model_path = "~/.local/share/mychat/models/..."
context_size = 8192

[llm.claude]
api_key_env = "ANTHROPIC_API_KEY"

[llm.openai]
api_key_env = "OPENAI_API_KEY"

[llm.openai-compat]
base_url = "http://localhost:11434/v1"
api_key_env = "OLLAMA_API_KEY"   # optional
model = "qwen2.5-coder:32b"
```

**Touches:** `LlmEngine`, `Config`, `App::initialize()`

---

## Phase 2: Built-in Coding Tools

**Goal:** Provide a core set of local tools that the agent can call without any external MCP server,
making mychat a self-contained coding assistant.

### 2.1 Tool Registry

Create a local tool registry alongside the MCP `ServerManager`:

```
src/tools/
├── ToolRegistry.hpp/.cpp      # Registers and dispatches built-in tools
├── ReadTool.hpp/.cpp          # Read file contents
├── WriteTool.hpp/.cpp         # Write/create files
├── EditTool.hpp/.cpp          # Exact string replacement in files
├── GlobTool.hpp/.cpp          # File pattern matching
├── GrepTool.hpp/.cpp          # Content search (ripgrep-style)
├── BashTool.hpp/.cpp          # Shell command execution
└── GitTool.hpp/.cpp           # Git operations (status, diff, log, commit)
```

Each tool implements:

- `name() → string` — Tool name (e.g., `"read_file"`)
- `definition() → ToolDefinition` — JSON Schema for `inputSchema`
- `execute(json arguments) → Result<ToolResult>` — Run the tool and return output

The `AgentLoop` queries both `ToolRegistry::allTools()` and `ServerManager::allTools()` to build the
combined tool list passed to `LlmEngine::generate()`.

### 2.2 Tool Specifications

#### `read_file`
- **Input:** `{ path: string, offset?: int, limit?: int }`
- **Behavior:** Read file contents with optional line range. Return with line numbers.
- **Output:** File content as text, or error if file doesn't exist.

#### `write_file`
- **Input:** `{ path: string, content: string }`
- **Behavior:** Write content to file, creating directories as needed.
- **Output:** Confirmation with bytes written.

#### `edit_file`
- **Input:** `{ path: string, old_string: string, new_string: string, replace_all?: bool }`
- **Behavior:** Exact string replacement. Fail if `old_string` is not found or not unique (unless `replace_all`).
- **Output:** Confirmation with match count.

#### `glob`
- **Input:** `{ pattern: string, path?: string }`
- **Behavior:** Find files matching glob pattern. Return sorted by modification time.
- **Output:** List of matching file paths.

#### `grep`
- **Input:** `{ pattern: string, path?: string, glob?: string, context?: int }`
- **Behavior:** Regex search across files. Support context lines.
- **Output:** Matching lines with file paths and line numbers.

#### `bash`
- **Input:** `{ command: string, timeout_ms?: int }`
- **Behavior:** Execute shell command with timeout (default 120s, max 600s). Capture stdout+stderr.
- **Output:** Command output (truncated if >30KB), exit code.

#### `git`
- **Input:** `{ subcommand: string, args?: [string] }`
- **Behavior:** Run git commands in the working directory. Block destructive operations by default.
- **Output:** Git command output.

### 2.3 Integration with AgentLoop

Modify `AgentLoop::executeToolCalls()`:

1. Check `ToolRegistry` first for built-in tools
2. Fall back to `ServerManager` for MCP tools
3. Return `ToolResult` with `isError=true` for unknown tool names

**Touches:** `AgentLoop`, new `src/tools/` directory, `CMakeLists.txt`

---

## Phase 3: Permission & Safety System

**Goal:** Classify tools by risk and gate dangerous operations on user approval.

### 3.1 Risk Classification

```cpp
enum class ToolRisk {
    ReadOnly,    // read_file, glob, grep, git status/log/diff — auto-approved
    Mutating,    // write_file, edit_file, git add/commit — prompt once per session
    Destructive, // bash, git push/reset --hard, rm — always prompt
};
```

### 3.2 Permission Manager

```
src/core/
└── PermissionManager.hpp/.cpp
```

- Maintain a per-session approval set: `set<string> approvedTools`
- For `Mutating` tools: prompt user on first use, remember approval for session
- For `Destructive` tools: always prompt with command preview
- Display approval prompts in the TUI (modal dialog via `src/tui/Dialog`)
- Configuration option for "auto-approve all" mode (`--dangerously-skip-permissions`)

### 3.3 Bash Command Safety

Special handling for the `bash` tool:

- Parse the command string for known-dangerous patterns (`rm -rf`, `git push --force`, etc.)
- Block interactive commands (`vim`, `less`, `git rebase -i`)
- Enforce timeout to prevent runaway processes
- Kill child process on timeout

### 3.4 Sandboxing (Future)

- Optional filesystem sandboxing (restrict tool operations to project directory)
- Network access controls for bash commands
- Resource limits (CPU time, memory) for spawned processes

**Touches:** `AgentLoop`, `ToolRegistry`, new `PermissionManager`, `Dialog`

---

## Phase 4: Context Window Management

**Goal:** Keep conversations within the LLM's context window during long coding sessions.

### 4.1 Token Counting

- For local models: use llama.cpp tokenizer directly (`llama_tokenize`)
- For API providers: use tiktoken-compatible counting (cl100k_base for OpenAI, Claude token estimation)
- Track token usage per message in `ChatSession`
- Expose `ChatSession::estimatedTokenCount() → int`

### 4.2 Conversation Compaction

When approaching the context limit (e.g., 80% of `contextSize`):

1. **Summarize old messages** — Send a summarization request to the LLM: "Summarize the conversation so far, preserving key decisions, file changes, and current task state."
2. **Replace old messages** — Replace messages before the summary with a single system message containing the summary.
3. **Preserve recent context** — Keep the last N messages (configurable) plus all pending tool results.

### 4.3 Sliding Window Strategy

- Track cumulative token count as messages are added
- Trigger compaction when `estimatedTokenCount() > contextSize * 0.8`
- Keep system prompt, last compaction summary, and recent messages
- Log compaction events to `LogPanel`

### 4.4 Tool Result Truncation

- Truncate large tool results (file reads, bash output) before adding to conversation
- Configurable max result size (default 30KB)
- Add `[truncated — X bytes omitted]` marker

**Touches:** `ChatSession`, `LlmEngine`, `AgentLoop`, `Config`

---

## Phase 5: Enhanced MCP Support

**Goal:** Bring MCP support up to the latest spec features beyond the current stdio-only implementation.

### 5.1 Streamable HTTP Transport

Add a new transport alongside `StdioTransport`:

```
src/mcp/
├── Transport.hpp              # (existing) Abstract base
├── StdioTransport.hpp/.cpp    # (existing) Stdio pipes
└── HttpTransport.hpp/.cpp     # NEW: Streamable HTTP (SSE-based)
```

- Implement the MCP Streamable HTTP transport (POST for requests, SSE for server-initiated messages)
- Support session management via `Mcp-Session-Id` header
- Connection resumability

### 5.2 Dynamic Tool Discovery

Currently tools are loaded once at startup via `McpClient::listTools()`. Enhance to support:

- `notifications/tools/list_changed` — server notifies client that tools have changed
- Re-fetch tool list on notification
- Update `ServerManager` tool routing map dynamically
- Notify `AgentLoop` of tool set changes

### 5.3 MCP Resources

Implement the MCP resources primitive:

- `resources/list` — discover available resources from servers
- `resources/read` — fetch resource content
- Inject resource content into conversation context when referenced
- Resource templates with URI patterns

### 5.4 MCP Prompts

Implement the MCP prompts primitive:

- `prompts/list` — discover available prompt templates
- `prompts/get` — retrieve a prompt with arguments
- Expose as slash commands in the TUI (e.g., `/mcp:prompt-name`)

### 5.5 MCP Sampling

Implement server-initiated sampling (the MCP server requests an LLM completion from the client):

- `sampling/createMessage` — handle sampling requests from MCP servers
- Route through `LlmEngine::generate()` with server-specified parameters
- Return the generated response back to the requesting MCP server

**Touches:** `Transport`, `McpClient`, `ServerManager`, `AgentLoop`, new `HttpTransport`

---

## Phase 6: Project Context Awareness

**Goal:** Make the agent aware of project structure, conventions, and current state.

### 6.1 Project Detection

On startup or when changing working directory:

- Detect project root (walk up to find `.git/`, `CMakeLists.txt`, `package.json`, etc.)
- Load project-level configuration files (`.mychat/config.toml`, `CLAUDE.md`)
- Build initial project context for the system prompt

### 6.2 Git Status Integration

- Run `git status`, `git diff`, `git log --oneline -10` at session start
- Include branch name, dirty files, and recent commits in system prompt
- Refresh on tool calls that modify files

### 6.3 Project Structure Indexing

- Walk the file tree (respecting `.gitignore`)
- Build a file manifest with paths, sizes, and types
- Include a condensed project tree in the system prompt
- Re-index when files change (via tool calls or file watcher)

### 6.4 Rules File Loading

Support hierarchical rules/instructions:

1. Global rules: `~/.config/mychat/rules/*.md`
2. Project rules: `<project-root>/CLAUDE.md` or `<project-root>/.mychat/rules/*.md`
3. Merge into system prompt with clear section markers

### 6.5 File Watching (Optional)

- Use `inotify` (Linux) / `kqueue` (macOS) for filesystem change notifications
- Update project index when external tools modify files
- Notify the agent of changes during long sessions

**Touches:** `Config`, `ChatSession::setSystemPrompt()`, new `src/core/ProjectContext`

---

## Phase 7: TUI Enhancements for Coding

**Goal:** Enhance the terminal UI for a coding-assistant workflow.

### 7.1 Diff Rendering

Add a diff renderer to the TUI:

- Parse unified diff format
- Color-code additions (green), deletions (red), context (dim)
- Show file path headers
- Integrate with `edit_file` tool output — show diffs of changes made

### 7.2 Syntax-Highlighted Code Blocks

Enhance `MarkdownRenderer` code block rendering:

- Detect language from fenced code block info string (` ```cpp `, ` ```python `, etc.)
- Apply basic keyword/string/comment highlighting using a lightweight tokenizer
- Support common languages: C++, Python, JavaScript/TypeScript, Rust, Go, Shell

### 7.3 Tool Execution Progress

- Show a status line for each tool call: tool name, arguments summary, elapsed time
- Use `Spinner` with tool-specific labels during execution
- Show tool result summary (success/error, output size) after completion
- Animate concurrent tool execution (when Phase 8 enables it)

### 7.4 Multi-Panel Layout

Optional split-view mode:

- Main panel: conversation with the agent
- Side panel: file preview (read-only, updated when agent reads files)
- Bottom panel: log/status (existing `LogPanel`, extended)
- Toggle with keyboard shortcuts

### 7.5 Scrollback and Search

- Implement scrollback buffer for conversation history
- Ctrl+R reverse search through conversation
- Page up/down to scroll through long outputs

**Touches:** `MarkdownRenderer`, `TerminalOutput`, `LogPanel`, new diff/syntax renderers, `App::run()`

---

## Phase 8: Advanced Agent Features

**Goal:** Evolve from a single-turn tool caller into a sophisticated coding agent.

### 8.1 Parallel Tool Execution

Upgrade `AgentLoop::executeToolCalls()`:

- Identify independent tool calls (no shared state dependencies)
- Execute independent calls concurrently using `std::jthread` or `std::async`
- Collect results and return in original order
- Serial fallback for tools with side effects on shared state

### 8.2 Sub-Agent Spawning

Enable the main agent to delegate tasks:

- Spawn lightweight sub-agents for parallel research/exploration
- Each sub-agent gets its own `ChatSession` (isolated context)
- Share `ToolRegistry` and `ServerManager` (thread-safe access required)
- Main agent receives sub-agent results as tool outputs
- Configurable max concurrent sub-agents

### 8.3 Plan Mode

Add a structured planning workflow:

- Agent enters plan mode: explores codebase, reads files, builds understanding
- Produces a step-by-step implementation plan
- User reviews and approves the plan
- Agent executes the approved plan step by step
- Track plan progress in the TUI

### 8.4 Slash Commands / Skills

Add a command system for common workflows:

| Command | Action |
|---------|--------|
| `/commit` | Stage changes, generate commit message, create commit |
| `/review` | Review staged changes or a PR |
| `/test` | Run project tests and analyze results |
| `/explain <file>` | Explain a file's purpose and structure |
| `/fix` | Analyze and fix the last error |

- Register commands in a `CommandRegistry`
- Each command expands to a prompt + tool sequence
- Tab completion in `InputField`

### 8.5 LSP Integration

Connect to Language Server Protocol servers for rich code intelligence:

- Launch LSP servers for detected project languages
- Feed diagnostics (errors, warnings) back to the agent after file edits
- Use `textDocument/completion` for code-aware suggestions
- Use `textDocument/definition` and `textDocument/references` for navigation

### 8.6 Memory System

Persistent agent memory across sessions:

- Store key learnings, project conventions, user preferences
- Auto-save when the agent discovers patterns
- Load memory into system prompt on session start
- Organize by project (project-level memory files)

### 8.7 Retry and Error Recovery

Harden the agent loop (addresses the unused `maxRetries` in current `AgentConfig`):

- Retry failed tool calls with exponential backoff
- On LLM generation failure, retry with reduced context
- On repeated tool failures, ask the user for guidance
- Track error patterns to avoid repeating the same mistakes

**Touches:** `AgentLoop`, `ChatSession`, new `CommandRegistry`, new `SubAgent`, `InputField` (tab completion)

---

## Dependency Graph

```
Phase 1 (LLM Providers)
   │
   ├──→ Phase 2 (Built-in Tools) ──→ Phase 3 (Permissions) ──→ Phase 8 (Advanced Agent)
   │                                                                │
   │                                                                ├── 8.1 Parallel Tools
   │                                                                ├── 8.2 Sub-Agents
   │                                                                ├── 8.3 Plan Mode
   │                                                                ├── 8.4 Slash Commands
   │                                                                ├── 8.5 LSP Integration
   │                                                                ├── 8.6 Memory System
   │                                                                └── 8.7 Retry/Recovery
   │
   ├──→ Phase 4 (Context Management)
   │
   ├──→ Phase 5 (Enhanced MCP)
   │
   ├──→ Phase 6 (Project Context)
   │
   └──→ Phase 7 (TUI Enhancements)
```

- **Phase 1** is the prerequisite — external LLM support unblocks serious agentic use (local models have limited tool-calling ability).
- **Phases 2–7** can proceed in parallel after Phase 1.
- **Phase 8** depends on Phase 2 (tools) and Phase 3 (permissions) being in place.
- **Phase 5** (MCP) is independent and can start at any time since the MCP infrastructure already exists.

---

## Non-Goals (Out of Scope)

- **GUI / web interface** — mychat is a terminal application
- **Multi-user / server mode** — single-user local tool
- **Training or fine-tuning** — use pre-trained models only
- **Mobile support** — Linux/macOS/Windows desktop terminals only

---

## Success Criteria

The roadmap is complete when a user can:

1. Point mychat at a codebase and an LLM provider (local or API)
2. Ask the agent to read, understand, and modify code
3. See the agent plan changes, execute tools, and iterate
4. Review diffs, approve mutations, and commit results
5. Use MCP servers to extend the agent's capabilities
6. Work through long sessions without context window issues
