# UnrealMCPBridge

[![UE Version](https://img.shields.io/badge/Unreal%20Engine-5.6%2B-%23313131?logo=unrealengine)](https://www.unrealengine.com/)
[![Python](https://img.shields.io/badge/Python-3.11%2B-3776AB?logo=python)](https://www.python.org/)
[![MCP](https://img.shields.io/badge/Model%20Context%20Protocol-2025--03--26-00cc88)](https://modelcontextprotocol.io)
[![License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

Model Context Protocol (MCP) bridge plugin for Unreal Engine 5.6+. Gives AI agents — like [Claude](https://claude.ai), [Claude Code](https://docs.anthropic.com/en/docs/agents-and-tools/claude-code/overview), and other MCP-compatible clients — direct access to the full Unreal Engine Python API through a local TCP socket server.

> **This fork:** adapted for **UE 5.6.0**, and extended with **[`PCGMCP/`](PCGMCP/)** — a second, Python-native MCP path purpose-built for **PCG (Procedural Content Generation) work**, added after real production use surfaced reliability issues in the C++ path under 5.6. See [What's New in This Fork](#whats-new-in-this-fork) below.

## What's New in This Fork

### 1. UE 5.6 adaptation

`EngineVersion` bumped to **5.6.0**. The C++ module compiles and loads unchanged on 5.6 (Win64, Development Editor).

### 2. Reliability findings on the C++ path — why a second path exists

Driving real PCG production graphs through the C++ bridge on 5.6 surfaced structural issues:

- **Blocking synchronous model.** The socket thread waits on the game-thread task (`WaitUntilTaskCompletes`). When the editor is busy — PCG regen, PIE, a modal dialog — tool calls stall or time out.
- **PCG generation is asynchronous.** A tool call returns long before generation finishes, so instance counts read as 0 or stale. Without a polling discipline this is trivially misdiagnosed ("the graph generates nothing").
- Manual toolbar start, single client, and it requires a C++ project + Visual Studio.

### 3. NEW — `PCGMCP/`: a Python-native MCP path for PCG (recommended)

No C++ compilation, no editor changes — works with Blueprint-only projects and Launcher (binary) engine builds.

```
AI agent (any MCP client)
   ↕  MCP stdio
pcg_mcp_server.py                 ← adapter process, outside the editor
   ↕  one-shot TCP: {"code": "<python>"} → {"result": "<stdout>"}
Editor bridge (auto-started)      ← ~60 lines in Content/Python, executes
                                     payloads on the game thread via a
                                     Slate post-tick callback
```

**Install**

1. Paste the bridge block from [`PCGMCP/pcg_bridge_server.py`](PCGMCP/pcg_bridge_server.py) into your project's `Content/Python/init_unreal.py` — the Python Editor Script Plugin auto-executes it at editor startup.
2. Install the MCP package on the machine where your AI client runs:
   ```bash
   pip install mcp
   ```
3. Register the server with any MCP-compatible client (Claude Desktop / Claude Code / Cursor / Trae …):
   ```json
   {
     "mcpServers": {
       "pcg-bridge": {
         "command": "python",
         "args": ["C:/path/to/PCGMCP/pcg_mcp_server.py"],
         "env": { "PCG_BRIDGE_PORT": "9877" }
       }
     }
   }
   ```

**Tools (12)**

| Tool | Purpose |
|------|---------|
| `ping` | Editor alive? Current map + PCG volume count |
| `list_pcg_volumes` | Every PCGVolume: exact actor name, graph path, trigger mode, instance totals |
| `dump_graph` | Node topology of a graph asset — type / enabled / per-pin edge counts |
| `get_node_settings` | Read specific editor properties off a node's settings object |
| `get_volume_status` | Generation state + per-mesh instance counts of a volume |
| `backup_asset` | Timestamped duplicate of an asset — do this before every edit |
| `save_asset` | Clear the read-only bit → save → verify the on-disk mtime actually changed |
| `set_node_property` | `set_editor_property` + immediate read-back verification |
| `regen_volume` | Submit exactly one `generate(force=True)`, then poll until instance counts stabilize |
| `run_python` | Escape hatch — arbitrary Python, wrapped in the same discipline |
| `snapshot_instances` | Label and store a volume's per-mesh instance counts |
| `diff_instances` | A/B diff between two snapshots (area-based acceptance) |

**Production discipline baked in** — not conventions to remember; the tools enforce them:

- `regen_volume` submits **one** generate request, then the adapter polls instance counts until stable. PCG editor generation is async — reading immediately gives false zeros, and stacking multiple regen requests pegs a core in a regenerate loop.
- `backup_asset` before edits; `save_asset` clears the read-only bit (Perforce) and **verifies the on-disk mtime changed** — an API "success" without a disk write is a real failure mode.
- Every payload is exception-guarded, prints state **before** risky calls (the bridge returns stdout only — stderr tracebacks are invisible), and ends with `gc.collect()` (delayed GC of stale struct wrappers has crashed editors).
- One-shot connections; one graph loaded per call (bulk-loading PCG graphs overflows editor memory).

Developed against a real PCG production pipeline — a 43-node forest-ecosystem graph family with river-avoidance, understory and riverbank subsystems (40k+ instanced meshes) — so the discipline list above is earned, not aspirational.

---

## 中文说明

本 fork 做了两件事：

1. **UE 5.6 适配** —— `.uplugin` 引擎版本升至 5.6.0，C++ 模块零改动，5.6 下正常编译加载。
2. **新增 `PCGMCP/` 纯 Python MCP 路线** —— 用 C++ 路线驱动真实 PCG 生产图时发现结构性问题：同步阻塞模型在编辑器繁忙时挂起；PCG 异步生成导致立即读数误判为 0；且需要 C++ 工程与手动启动。新路线改为：编辑器内约 60 行 TCP 桥（随编辑器自动启动，负载经 Slate post-tick 在主线程执行，socket 线程不阻塞）+ 编辑器外 MCP 适配层，暴露 12 个 PCG 专用工具（拓扑审计 / 参数读写 / 备份保存 / 异步重生成轮询 / 实例快照与 A/B 对比），并把生产纪律直接内置在工具里。无需编译 C++，蓝图工程与 Launcher 版引擎均可使用。

安装步骤与工具表见上方英文章节。

---

The rest of this README documents the original C++ plugin path.

## Use Cases

- AI-assisted level design, asset management, and scene construction
- Automated Blueprint and Niagara creation via Python
- Intelligent performance profiling with CSV Profiler and Unreal Insights traces
- Rapid prototyping of editor workflows — no manual clicking
- Live coding: iterate on scene layout, materials, and gameplay logic with an agent

## Prerequisites

- **Unreal Engine 5.6+** (source build or Epic Games Launcher)
- **Python Editor Script Plugin** enabled in your project (auto-enabled by this plugin)
- **Visual Studio 2019+** (to compile the C++ plugin)
- **Python 3.11+** with the `mcp` package for the client side:
  ```bash
  pip install mcp
  ```
- An MCP-compatible AI agent (Claude Desktop, Claude Code, etc.)

## Installation

### From GitHub

1. Create or open a C++ Unreal Engine project.
2. Clone this repo into your project's `Plugins/` folder:
   ```bash
   cd YourProject/Plugins
   git clone https://github.com/zoey-billd/UnrealMCPBridge.git
   ```
3. Right-click your `.uproject` file → **Generate Visual Studio project files**.
4. Open the generated `.sln` in Visual Studio and build (Development Editor, Win64).
5. Launch your project. The plugin will appear as a toolbar button labeled **"Start MCP Bridge"**.

### From Fab

1. Purchase the plugin on [Fab](https://www.fab.com/).
2. In the Epic Games Launcher, go to **Library** → find **UnrealMCPBridge**.
3. Click **Install to Engine** and choose your UE 5.6+ installation.

## Plugin Architecture

```
UnrealMCPBridge/
├── Source/                          # C++ plugin source
│   └── UnrealMCPBridge/
│       ├── Private/
│       │   ├── MCPSocketServer.cpp       # TCP socket server on port 9000
│       │   ├── PythonBridge.cpp          # Python execution bridge
│       │   ├── BlueprintGraphLibrary.cpp # Blueprint creation C++ library
│       │   ├── NiagaraEditorLibrary.cpp  # Niagara creation C++ library
│       │   ├── ViewportCaptureLibrary.cpp# Screenshot capture
│       │   └── TraceAnalysisLibrary.cpp  # Insights .utrace parser
│       └── Public/                       # Corresponding headers
├── Content/
│   └── unreal_server_init.py            # Server-side tool implementations
├── MCPClient/
│   └── unreal_mcp_client.py             # MCP client (Claude Desktop / Claude Code)
├── PCGMCP/                              # NEW — Python-native path for PCG work
│   ├── pcg_bridge_server.py             # Editor-side TCP bridge (auto-start, port 9877)
│   └── pcg_mcp_server.py                # MCP adapter with 12 PCG-focused tools
└── UnrealMCPBridge.uplugin
```

**How it works:** The C++ plugin starts a TCP socket server on `127.0.0.1:9000` inside Unreal Engine. The Python MCP client (`unreal_mcp_client.py`) connects to this socket and exposes tools via the MCP stdio transport. AI agents invoke tools → client sends JSON commands over TCP → UE executes Python and returns results.

## Configuration

### Claude Desktop

1. Copy `MCPClient/unreal_mcp_client.py` to a location of your choice.
2. Open your Claude Desktop config file:
   - **Windows:** `%AppData%\Claude\claude_desktop_config.json`
   - **macOS:** `~/Library/Application Support/Claude/claude_desktop_config.json`
3. Add the server entry:
   ```json
   {
     "mcpServers": {
       "unreal-engine": {
         "command": "python",
         "args": ["C:/path/to/unreal_mcp_client.py"]
       }
     }
   }
   ```
4. Restart Claude Desktop. Use the **plug icon** (MCP attach) or **hammer icon** (tools) to interact with UE.

### Claude Code

**Option A — CLI (recommended):**

```bash
claude mcp add --transport stdio --scope project unreal-bridge -- python C:/path/to/unreal_mcp_client.py
```

**Option B — Manual `.mcp.json`:**

Create `.mcp.json` in your UE project root (next to your `.uproject`):
```json
{
  "mcpServers": {
    "unreal-bridge": {
      "command": "python",
      "args": ["C:/path/to/unreal_mcp_client.py"],
      "env": {}
    }
  }
}
```

Start Claude Code with `claude` in the project directory. On first use, approve the MCP server. Type `/mcp` to verify the connection.

### Starting the Bridge

1. Open your Unreal Engine project.
2. Click the **"Start MCP Bridge"** toolbar button (added by the plugin).
3. A pop-up confirms: *"MCP Python bridge running."* The Output Log will show a socket server listening on `127.0.0.1:9000`.
4. The bridge is now ready to accept MCP client connections.

---

## API Reference

### Project & Asset Discovery

| Tool | Parameters | Description |
|------|-----------|-------------|
| `get_project_dir` | — | Returns the top-level project directory path (e.g., `D:/MyProject/`). |
| `get_content_dir` | — | Returns the Content directory path (e.g., `D:/MyProject/Content/`). |
| `find_basic_shapes` | — | Lists built-in basic shapes (cube, sphere, cylinder, etc.) available for building. |
| `find_assets` | `asset_name: str` | Searches the asset registry for assets matching a name. Use keywords like `Floor`, `Wall`, `Door`. |
| `get_asset` | `asset_path: str` | Returns the bounding box dimensions of a static mesh asset. |

### Actor Management

| Tool | Parameters | Description |
|------|-----------|-------------|
| `get_actors` | — | Lists all actors in the current level with name, class, and location. |
| `get_actor_details` | `actor_name: str` | Returns all properties and details for a specific actor. |
| `get_selected_actors` | — | Returns the actors currently selected in the editor viewport. |
| `spawn_actor` | `asset_path, location_x/y/z, rotation_x/y/z, scale_x/y/z` | Spawns a static mesh actor. All position/rotation/scale default to 0. |
| `modify_actor` | `actor_name, property_name, property_value` | Modifies a property of an existing actor. Value string auto-converts to appropriate type. |
| `set_material` | `actor_name, material_path` | Applies a material to a static mesh actor (e.g., `/Game/Materials/M_MyMaterial`). |
| `delete_all_static_mesh_actors` | — | **Destructive.** Deletes all static mesh actors in the scene. |

### Building Utilities

| Tool | Parameters | Description |
|------|-----------|-------------|
| `create_grid` | `asset_path, grid_width, grid_length` | Creates a grid of tiles auto-spaced by the asset's bounding box. |
| `create_town` | `town_center_x/y, town_width, town_height` | Procedural town layout centered at the given position. |

### Blueprint Execution

| Tool | Parameters | Description |
|------|-----------|-------------|
| `run_blueprint_function` | `blueprint_name, function_name, arguments` | Executes a function defined in a Blueprint. Arguments passed as comma-separated string. |

### Viewport & Screenshots

| Tool | Parameters | Description |
|------|-----------|-------------|
| `take_screenshot` | — | Captures the active editor viewport to PNG and returns the file path. |
| `set_viewport_camera` | `location_x/y/z, rotation_pitch/yaw/roll` | Moves the editor camera to a specific position and rotation. |
| `focus_viewport_on_actor` | `actor_name, distance` | Points camera at a named actor from a 45° overhead angle. `distance=0` auto-calculates. |

### Python Execution

| Tool | Parameters | Description |
|------|-----------|-------------|
| `execute_python` | `code: str` | Executes arbitrary Python inside Unreal Engine. **Most powerful tool** — full access to the [UE Python API](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/?application_version=5.7). Returns printed output and error tracebacks. |

The other tools are convenience wrappers. `execute_python` is the escape hatch — it can call any editor subsystem, manipulate assets, modify materials, create Blueprints, run Niagara, and anything else the UE Python API supports.

### CSV Profiler — High-Level Performance

The CSV profiler captures per-frame aggregate stats (frame time, thread times, GPU time, draw calls, memory) without the overhead of a full Insights trace.

| Tool | Description |
|------|-------------|
| `start_csv_profile` | Starts CSV frame-stat capture. |
| `stop_csv_profile` | Stops capture. CSV file needs a few seconds to finalize. |
| `get_csv_profile` | Parses the most recent CSV into JSON summary. |

**Workflow:** `start_csv_profile` → perform actions (PIE, stress test) → `stop_csv_profile` → wait 2–3s → `get_csv_profile`

The returned summary includes:

| Section | Contents |
|---------|----------|
| **timing** | avg/min/max/p50/p95/p99 for FrameTime, GameThreadTime, RenderThreadTime, GPUTime, RHIThreadTime |
| **counters** | DrawCalls, PrimitivesDrawn, MemoryFreeMB, PhysicalUsedMB |
| **budget** | Frames over 60fps/30fps budgets (count + %), average FPS |
| **trend** | First-half vs second-half comparison (stable/degrading/improving) |

**Interpreting results:** Whichever thread time is closest to FrameTime is the bottleneck. GameThreadTime dominant → CPU game-bound (too many ticking actors, expensive Blueprint logic). RenderThreadTime dominant → CPU render-bound (too many draw calls). GPUTime dominant → GPU-bound (heavy shaders, too many triangles).

### Unreal Insights Tracing — Function-Level Detail

Insights tracing captures full call stacks — individual function names, source file/line numbers, per-scope timing with nesting.

| Tool | Parameters | Description |
|------|-----------|-------------|
| `start_trace` | — | Starts capturing CPU, GPU, Frame, Counters, and Region channels. |
| `stop_trace` | — | Stops capture. Returns absolute path to the `.utrace` file. |
| `analyze_trace` | `trace_path, top_n=50` | Parses `.utrace` → JSON with duration, threads, frame stats, and top N expensive scopes (name, file/line, call count, total/avg/max inclusive time). |
| `get_trace_spikes` | `trace_path, budget_ms=33.33, max_frames=20, top_scopes_per_frame=10` | Finds frames exceeding time budget with per-frame hotspot breakdowns. |
| `get_trace_frame_summary` | `trace_path` | Frame count, avg/min/max frame time, FPS, p50/p90/p95/p99, frames over 60/30fps. |

**Recommended performance workflow:**

```
CSV Profiler (triage):
  1. start_csv_profile       → begin capture
  2. [perform actions]       → PIE, stress test
  3. stop_csv_profile        → stop capture
  4. [wait 2-3 seconds]      → allow file flush
  5. get_csv_profile         → which thread is the bottleneck?

Unreal Insights (drill down):
  6. start_trace             → detailed capture
  7. [reproduce the problem]
  8. stop_trace              → get .utrace path
  9. analyze_trace(path, 20) → top 20 expensive functions
 10. get_trace_spikes(path)  → per-frame hotspot breakdowns
```

### Built-in Prompts

Prompts are pre-written multi-step instructions for the AI agent.

| Prompt | Description |
|--------|-------------|
| `create_castle` | Clears the scene, finds basic shapes, builds a castle from primitives. |
| `create_town` | Clears the scene, finds a floor tile, builds a grid, generates a town layout. |

---

## C++ Libraries (via `execute_python`)

The plugin bundles four `BlueprintFunctionLibrary` classes that auto-reflect to Python:

| Library | Python Access | Purpose |
|---------|--------------|---------|
| `BlueprintGraphLibrary` | `unreal.BlueprintGraphLibrary` | Create Blueprint assets, add event/function/variable nodes, wire exec and data pins, compile and save. |
| `NiagaraEditorLibrary` | `unreal.NiagaraEditorLibrary` | Create Niagara systems, add emitters and modules, set parameters, configure renderers (Sprite/Ribbon/Light/Mesh/Decal), compile. |
| `ViewportCaptureLibrary` | `unreal.ViewportCaptureLibrary` | Synchronous viewport screenshot via ReadPixels. Used internally by `take_screenshot`. |
| `TraceAnalysisLibrary` | `unreal.TraceAnalysisLibrary` | Parse Unreal Insights `.utrace` files — extract timing scopes, frame stats, thread info. |

These are not exposed as standalone MCP tools but are callable through `execute_python`.

---

## Creating Blueprints and Niagara Systems

### Creating Blueprints

Use `execute_python` with `BlueprintGraphLibrary`:

```python
import unreal
bgl = unreal.BlueprintGraphLibrary
# Create Actor Blueprint, add BeginPlay event, add variable, wire pins, compile
bgl.create_blueprint("/Game/Blueprints/BP_MyActor", "Actor")
bgl.add_event_node("ReceiveBeginPlay")
bgl.add_variable("Speed", "float", 100.0, True)  # instance-editable
bgl.add_function_call("SetActorTickEnabled", "true")
bgl.compile_blueprint()
bgl.save_blueprint()
```

### Creating Niagara Particle Effects

Use `execute_python` with `NiagaraEditorLibrary`:

```python
import unreal
nel = unreal.NiagaraEditorLibrary
nel.create_niagara_system("/Game/VFX/NS_Rain")
nel.add_emitter("RainDrops")
nel.add_module("SpawnRate", {"Rate": 500})
nel.add_module("AddVelocity", {"Velocity": "(200, 0, -1500)"})
nel.add_module("GravityForce", {})
nel.set_sprite_renderer("VelocityAligned")
nel.compile_niagara_system()
```

---

## Developing Custom Tools and Prompts

### Adding a Tool

**Client side** (`unreal_mcp_client.py`) — define what the agent sees:

```python
@mcp.tool()
def my_custom_tool(param1: str, param2: int = 0) -> str:
    """Description shown to the AI agent."""
    result = send_command("my_custom_tool", {"param1": param1, "param2": param2})
    if result.get("status") == "success":
        return result.get("result")
    return f"Error: {result.get('message')}"
```

**Server side** (`Content/unreal_server_init.py`) — define the actual UE logic:

```python
@staticmethod
def my_custom_tool(param1, param2=0):
    try:
        # Your Unreal Engine Python logic here
        result = f"Processed {param1} with {param2}"
        return json.dumps({"status": "success", "result": result})
    except Exception as e:
        return json.dumps({"status": "error", "message": str(e)})
```

Restart Unreal Engine and Claude after changes.

### Adding a Prompt

```python
@mcp.prompt()
def my_prompt() -> str:
    """Description visible in the prompts menu."""
    return """
Please do the following in Unreal Engine:
1. First step description.
2. Second step description.
3. Third step description.
"""
```

Restart Claude (or use `/mcp` to reconnect in Claude Code) for new prompts to appear.

---

## Tips for Best Results

- **Be specific about asset paths.** Tell the agent `/Game/VFX/NS_MyEffect` rather than making it guess.
- **Describe the visual result.** "Long thin rain streaks falling at an angle" beats "make rain."
- **Iterate.** Create → preview → adjust ("more gravity", "change color to blue", "make particles bigger").
- **Close the asset editor** before modifying Blueprints or Niagara systems — the editor caches its own copy.
- **For performance profiling,** capture under representative workloads (PIE play, complex scenes, stress tests). Idle editor traces yield little useful data.
- **`execute_python` is your Swiss Army knife.** If a built-in tool doesn't cover your use case, `execute_python` can reach anything in the UE Python API.

---

## Troubleshooting

| Problem | Solution |
|---------|----------|
| Bridge won't start | Ensure `UnrealMCPBridge` and `Python Editor Script Plugin` are enabled in Edit → Plugins. Restart UE. |
| Claude can't connect | Click the **"Start MCP Bridge"** toolbar button in UE. Check Output Log for socket server confirmation. |
| `ModuleNotFoundError: mcp` | Install the Python MCP package: `pip install mcp` |
| Timeout on tool call | The UE bridge socket timed out. Check that UE is running and the bridge is started. Restart the bridge. |
| Blueprint/Niagara changes not appearing | Close the asset editor first. The editor caches the asset and won't reflect external Python changes. |
| CSV profiler returns empty/partial data | Wait 2–3 seconds between `stop_csv_profile` and `get_csv_profile`. The CSV file writes asynchronously on the game thread. |
| PCGMCP: bridge unreachable | Check the editor Output Log for `UE bridge auto-started on port 9877`. One port per running editor — two open editors will race for connections. |
| PCGMCP: 0 instances right after a change | Expected mid-generation; PCG generation is async. `regen_volume` already polls to stable — don't call `generate` in a loop. |

---

## Compatibility

- **Unreal Engine:** 5.6+ (Editor, Win64)
- **PCGMCP path (new):** UE 5.6+, any project type (C++ **or** Blueprint-only), Launcher or source builds — no compilation required
- **Python:** 3.11+
- **MCP Protocol:** 2025-03-26

The plugin has been tested with Claude Desktop and Claude Code. It should work with any MCP-compatible client that supports the stdio transport.

## Credits

- **Original author:** Omar Abdelwahed ([agentdisco.com](https://agentdisco.com/))
- **Original repo:** [appleweed/UnrealMCPBridge](https://github.com/appleweed/UnrealMCPBridge)
- **License:** MIT
- **Fork maintenance & PCGMCP path:** [zoey-billd](https://github.com/zoey-billd)

---

*Built with the [Model Context Protocol](https://modelcontextprotocol.io) — the open standard for connecting AI agents to tools and data sources.*
