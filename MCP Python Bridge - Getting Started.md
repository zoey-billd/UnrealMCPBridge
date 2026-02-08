# Unreal Engine MCP Python Bridge Plugin

This is a plugin for Unreal Engine (UE) that creates a server implementation of [Model Context Protocol (MCP)](https://modelcontextprotocol.io). This allows MCP clients, like Anthropic's [Claude](https://claude.ai), to access the full UE Python API.

## Use Cases

- Developing tools and workflows in Python with an agent like Claude.
- Intelligent and dynamic automation of such workflows.
- General collaborative development with an agent.

## Prerequisites

- Visual Studio 2019 or higher.
- An AI Agent. Below, we assume Claude will be used. But any AI Agent that implements MCP should suffice.
- Unreal Engine 5 with the Python Editor Script Plugin enabled.
- Note the [Unreal Engine Python API](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/?application_version=5.5).

## Installing from GitHub

1. Create a new Unreal Engine C++ project.
2. Under the project root directory, find the `Plugins` folder.
3. Clone the UnrealMCPBridge [repo](https://github.com/odabr/UnrealMCPBridge) into the `Plugins` folder so that there is a new containing folder with all the project contents underneath.
4. Right-click your UE project file (ends with `.uproject`) and select "Generate Visual Studio project files". If you don't immediately see that option, first select "Show more options" and it should appear.
5. Open your new Visual Studio project and build.

## Installing from Fab

1. After purchasing the plugin on Fab, find the plugin in your Library in the Epic Games Launcher.
2. Click "Install to Engine" and choose the appropriate version of Unreal Engine.

## Configure Claude for Desktop

1. Copy `unreal_mcp_client.py` from the [MCPClient folder](https://github.com/odabr/UnrealMCPBridge/tree/main/MCPClient) on GitHub to a location of your choice.

2. Find your `claude_desktop_config.json` configuration file. Mac location:
   `~/Library/Application\ Support/Claude/claude_desktop_config.json`
   Windows location:
   `[path_to_your_user_account]\AppData\Claude\claude_desktop_config.json`

3. Add the `unreal-engine` server section to your config file and update the path location excluding the square brackets, below.
   - Mac path format: `/[path_from_step_1]/unreal_mcp_client.py`
   - Windows path format: `C:\\[path_from_step_1]\\unreal_mcp_client.py`

```json
{
  "mcpServers": {
    "unreal-engine": {
      "command": "python",
      "args": [
        "[mac_or_windows_format_path_to_unreal_mcp_client.py]"
      ]
    }
  }
}
```

4. Create a new Unreal Engine project or load an existing project.
5. From the "Edit" menu, select "Plugins".
6. Enable these plugins: `UnrealMCPBridge` and `Python Editor Script Plugin`.
7. Restart Unreal Engine.
8. There should be a new toolbar button that says, "Start MCP Bridge" when you hover with your mouse.
9. Click the MCP Bridge button. A pop-up will state, "MCP Python bridge running." The Output Log will note a new socket server listening on 127.0.0.1:9000.
10. Launch Claude as an administrator.
11. Click the "Attach from MCP" plug-icon. Under "Choose an integration" are two test Prompts: `create_castle` and `create_town`. You can edit their implementations in `unreal_server_init.py` under the `Content` folder of the plugin. Be sure to restart Unreal Engine after any changes.
12. Alternatively, ask Claude to build in your project using the UE Python API.
13. A list of currently implemented tools can be found by clicking the hammer-icon to the left of the plug-icon.

## Configure Claude Code

[Claude Code](https://docs.anthropic.com/en/docs/agents-and-tools/claude-code/overview) is Anthropic's CLI tool for agentic coding. It supports MCP servers via a `.mcp.json` configuration file in your project root.

### Option A: CLI Command (Recommended)

From your Unreal Engine project root directory, run:

**Mac/Linux:**
```bash
claude mcp add --transport stdio --scope project unreal-bridge -- python /path/to/unreal_mcp_client.py
```

**Windows:**
```bash
claude mcp add --transport stdio --scope project unreal-bridge -- python C:\path\to\unreal_mcp_client.py
```

This creates a `.mcp.json` file in your project root automatically.

### Option B: Manual Configuration

1. Copy `unreal_mcp_client.py` from the MCPClient folder to a location of your choice.

2. Create a `.mcp.json` file in your Unreal Engine project root directory (the folder containing your `.uproject` file) with the following contents:

**Mac/Linux:**
```json
{
  "mcpServers": {
    "unreal-bridge": {
      "command": "python",
      "args": ["/path/to/unreal_mcp_client.py"],
      "env": {}
    }
  }
}
```

**Windows** (use forward slashes or escaped backslashes):
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

### Starting a Session

1. Open your Unreal Engine project and click the MCP Bridge toolbar button to start the server.
2. Open a terminal in your project root directory and launch Claude Code:
   ```bash
   claude
   ```
3. Claude Code will detect the `.mcp.json` and prompt you to approve the MCP server on first use.
4. Once approved, all MCP tools (get_actors, spawn_actor, execute_python, etc.) are available. Claude Code can call them directly during the conversation.

### Verifying the Connection

Inside a Claude Code session, type `/mcp` to check the status of connected MCP servers. You should see the `unreal-bridge` server listed as connected.

## Developing New Tools and Prompts

Examine `unreal_mcp_client.py` and you'll see how MCP defines tools and prompts using Python decorators above functions. As an example:

```python
@mcp.tool()
def get_project_dir() -> str:
    """Get the top level project directory"""
    result = send_command("get_project_dir")
    if result.get("status") == "success":
        response = result.get("result")
        return response
    else:
        return json.dumps(result)
```

This sends the `get_project_dir` command to Unreal Engine for execution and returns the project level directory for the current project. Under the `Content` folder of the plugin, you will see the server-side implementation of this tool command:

```python
@staticmethod
def get_project_dir():
    """Get the top level project directory"""
    try:
        project_dir = unreal.Paths.project_dir()
        return json.dumps({
            "status": "success",
            "result" : f"{project_dir}"
        })
    except Exception as e:
        return json.dumps({ "status": "error", "message": str(e) })
```

Follow this pattern to create new tools that appear in the Claude Desktop interface under the hammer-icon, or as callable tools in Claude Code.

Implementing new prompts is a matter of adding them to `unreal_mcp_client.py`. As an example, here is the `create_castle` prompt from `unreal_mcp_client.py`:

```python
@mcp.prompt()
def create_castle() -> str:
    """Create a castle"""
    return f"""
Please create a castle in the current Unreal Engine project.
0. Refer to the Unreal Engine Python API when creating new python code:
   https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/?application_version=5.5
1. Clear all the StaticMeshActors in the scene.
2. Get the project directory and the content directory.
3. Find basic shapes to use for building structures.
4. Create a castle using these basic shapes.
"""
```

Be sure to maintain triple-quotes so the entire prompt is returned. A good way to iterate over creating prompts is simply iterating each step number with Claude until you get satisfactory results. Then combine them all into a numbered step-by-step prompt as shown.

You must restart Claude for any changes to `unreal_mcp_client.py` to take effect. Note for Windows, you might need to end the Claude process in the Task Manager to truly restart Claude. For Claude Code, restart the session or use `/mcp` to reconnect.
