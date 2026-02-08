# unreal_mcp_client.py
import socket
import json
import re
from mcp.server.fastmcp import FastMCP, Context

# Add a startup handler to connect to Unreal
from contextlib import asynccontextmanager
from collections.abc import AsyncIterator

@asynccontextmanager
async def app_lifespan(server: FastMCP) -> AsyncIterator[str]:
    """Connect to Unreal Engine when the MCP server starts"""
    connected = connect_to_unreal()
    if connected:
        yield "Connected to Unreal Engine"
    else:
        yield "Warning: Not connected to Unreal Engine. Make sure the plugin is running."

# Use the lifespan with the FastMCP instance
mcp = FastMCP("Unreal Engine", lifespan=app_lifespan)

# Configure socket connection
HOST = '127.0.0.1'
PORT = 9000
socket_client = None

# Connect to the Unreal Engine socket server
def connect_to_unreal():
    global socket_client
    try:
        socket_client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        socket_client.connect((HOST, PORT))
        print(f"Connected to Unreal Engine on {HOST}:{PORT}")
        return True
    except Exception as e:
        print(f"Failed to connect to Unreal Engine: {e}")
        socket_client = None
        return False

# Send a command to Unreal Engine and get the response
def send_command(command, params=None):

    if socket_client is None:
        if not connect_to_unreal():
            return {"status": "error", "message": "Not connected to Unreal Engine"}
    
    if params is None:
        params = {}
    
    message = {
        "command": command,
        "params": params
    }
    
    try:
        # Send the message
        socket_client.sendall(json.dumps(message).encode('utf-8'))

        # Receive the response - accumulate chunks until valid JSON
        socket_client.settimeout(60.0)
        buffer = b''
        try:
            while True:
                chunk = socket_client.recv(65536)
                if not chunk:
                    break
                buffer += chunk

                # Try to parse accumulated buffer as JSON
                try:
                    decoded = buffer.decode('utf-8', 'replace')
                    stripped = decoded.strip('\'"\n\r')
                    replaced = stripped.replace('\\\'', '')
                    response = json.loads(replaced)
                    socket_client.settimeout(None)
                    return response
                except json.JSONDecodeError:
                    continue
        except socket.timeout:
            socket_client.settimeout(None)
            if buffer:
                decoded = buffer.decode('utf-8', 'replace')
                return {"status": "error", "message": f"Timeout - partial response: {decoded[:500]}"}
            return {"status": "error", "message": "Timeout waiting for response from Unreal Engine"}

        socket_client.settimeout(None)
        if buffer:
            decoded = buffer.decode('utf-8', 'replace')
            return {"status": "error", "message": f"Connection closed - partial: {decoded[:500]}"}
        return {"status": "error", "message": "Connection closed by Unreal Engine"}

    except Exception as e:
        print(f"Error sending command to Unreal: {e}")
        # Try to reconnect
        if connect_to_unreal():
            return send_command(command, params)
        return {"status": "error", "message": f"Communication error: {e}"}

# Tools
#@mcp.tool()
#def get_project_name() -> str:
#    """
#    Get the current Unreal Engine project name.
#    """
#    result = send_command("get_project_name")
#    if result.get("status") == "success":
#        return result.get("result", "Unknown Project")
#    else:
#        return f"Error: {result.get('message', 'Unknown error')}"

@mcp.tool()
def get_actors() -> str:
    """List all actors in the current level"""
    print(f"get_actors")
    result = send_command("get_actors")
    if result.get("status") == "success":
        actors = result.get("result", [])
        
        response = "# Actors in the current level\n\n"
        for actor in actors:
            response += f"- {actor.get('name')} ({actor.get('class')})\n"
            response += f"  Location: {actor.get('location')}\n"
        
        return response
    else:
        return f"get_actors error: " + json.dumps(result) #f"Error: {result.get('message', 'Unknown error')}"

@mcp.tool()
def get_actor_details(actor_name: str) -> str:
    """
    Get details for a specific actor by name.

    Args:
        actor_name: Name of the actor to retrieve details
    """
    result = send_command("get_actor_details", {"actor_name": actor_name})
    if result.get("status") == "success":
        actor = result.get("result", {})
        
        response = f"# Actor: {actor_name}\n\n"
        for key, value in actor.items():
            response += f"- {key}: {value}\n"
        
        return response
    else:
        return f"Error: {result.get('message', 'Unknown error')}"

@mcp.tool()
def spawn_actor(asset_path: str, location_x: float = 0, location_y: float = 0, location_z: float = 0, rotation_x: float = 0, rotation_y: float = 0, rotation_z: float = 0, scale_x: float = 0, scale_y: float = 0, scale_z: float = 0) -> str:
    """
    Spawn an actor in the current level.
    
    Args:
        asset_path: Path to the asset on disk
        location_x: X coordinate for actor placement
        location_y: Y coordinate for actor placement
        location_z: Z coordinate for actor placement
        rotation_x: Rotation around X-axis
        rotation_y: Rotation around Y-axis
        rotation_z: Rotation around Z-axis
        scale_x: Scale of actor in X-direction
        scale_y: Scale of actor in Y-direction
        scale_z: Scale of actor in Z-direction
    """
    result = send_command("spawn_actor", {
        "asset_path": asset_path,
        "location_x": location_x,
        "location_y": location_y,
        "location_z": location_z,
        "rotation_x": rotation_x,
        "rotation_y": rotation_y,
        "rotation_z": rotation_z,
        "scale_x": scale_x,
        "scale_y": scale_y,
        "scale_z": scale_z
    })
    
    if result.get("status") == "success":
        return result.get("result", "Actor created successfully")
    else:
        return f"Error: {result.get('message', 'Unknown error')}"

@mcp.tool()
def modify_actor(actor_name: str, property_name: str, property_value: str) -> str:
    """
    Modify a property of an existing actor.
    
    Args:
        actor_name: Name of the actor to modify
        property_name: Name of the property to change
        property_value: New value for the property (will be converted to appropriate type)
    """
    result = send_command("modify_actor", {
        "actor_name": actor_name,
        "property_name": property_name,
        "property_value": property_value
    })
    
    if result.get("status") == "success":
        return result.get("result", "Actor modified successfully")
    else:
        return f"Error: {result.get('message', 'Unknown error')}"

@mcp.tool()
def get_selected_actors() -> str:
    """Get the currently selected actors in the editor"""
    result = send_command("get_selected_actors")
    
    if result.get("status") == "success":
        actors = result.get("result", [])
        
        if not actors:
            return "No actors are currently selected."
        
        response = "# Currently Selected Actors\n\n"
        for actor in actors:
            response += f"- {actor.get('name')} ({actor.get('class')})\n"
            response += f"  Location: {actor.get('location')}\n"
        
        return response
    else:
        return f"Error: {result.get('message', 'Unknown error')}"

@mcp.tool()
def set_material(actor_name: str, material_path: str) -> str:
    """
    Apply a material to a static mesh actor.
    
    Args:
        actor_name: Name of the actor to modify
        material_path: Path to the material asset (e.g., '/Game/Materials/M_Basic')
    """
    result = send_command("set_material", {
        "actor_name": actor_name,
        "material_path": material_path
    })
    
    if result.get("status") == "success":
        return result.get("result", "Material applied successfully")
    else:
        return f"Error: {result.get('message', 'Unknown error')}"

@mcp.tool()
def delete_all_static_mesh_actors() -> str:
    """Delete all static mesh actors in the scene"""
    result = send_command("delete_all_static_mesh_actors")
    if result.get("status") == "success":
        response = result.get("result")
        return response
    else:
        return json.dumps(result)

@mcp.tool()
def get_project_dir() -> str:
    """Get the top level project directory"""
    result = send_command("get_project_dir")
    if result.get("status") == "success":
        response = result.get("result")
        return response
    else:
        return json.dumps(result)

@mcp.tool()
def get_content_dir() -> str:
    """Get the content directory"""
    result = send_command("get_content_dir")
    if result.get("status") == "success":
        response = result.get("result")
        return response
    else:
        return json.dumps(result)

@mcp.tool()
def find_basic_shapes():
    """Search for basic shapes for building"""
    result = send_command("find_basic_shapes")
    if result.get("status") == "success":
        response = result.get("result")
        return json.dumps(response)
    else:
        return json.dumps(result)

@mcp.tool()
def find_assets(asset_name: str) -> str:
    """
    Search for specific assets by name, like Floor, Wall, Door.

    Args:
        asset_name: Name of asset file on disk
    """
    result = send_command("find_assets", {
        "asset_name": asset_name
    })
    if result.get("status") == "success":
        response = result.get("result")
        return json.dumps(response)
    else:
        return json.dumps(result)

@mcp.tool()
def get_asset(asset_path: str) -> str:
    """
    Get the dimensions of an asset.
    
    Args:
        asset_path: Path to the asset on disk
    """
    result = send_command("get_asset", {
        "asset_path": asset_path
    })
    if result.get("status") == "success":
        response = result.get("result")
        return json.dumps(response)
    else:
        return json.dumps(result)

@mcp.tool()
def create_grid(asset_path: str, grid_width: int, grid_length: int) -> str:
    """
    Create a grid evenly spaced with the provided asset.
    
    Args:
        asset_path: Path to the tile asset on disk
        grid_width: Number of tiles in the x dimension
        grid_length: Number of tiles in the y dimension
    """
    result = send_command("create_grid", {
        "asset_path": asset_path,
        "grid_width": grid_width,
        "grid_length": grid_length
    })
    if result.get("status") == "success":
        response = result.get("result")
        return json.dumps(response)
    else:
        return json.dumps(result)

@mcp.tool()
def create_town(town_center_x: int, town_center_y: int, town_width: int, town_height: int) -> str:
    """Create a town using supplied assets

    Args:
        town_center_x: Center x position
        town_center_y: Center y position
        town_width: Width of town
        town_height: Height of town
    """
    result = send_command("create_town", {
        "town_center_x": town_center_x,
        "town_center_y": town_center_y,
        "town_width": town_width,
        "town_height": town_height
    })
    if result.get("status") == "success":
        response = result.get("result")
        return response
    else:
        return json.dumps(result)

@mcp.tool()
def run_blueprint_function(blueprint_name: str, function_name: str, arguments: str = "") -> str:
    """
    Execute a function in a Blueprint.
    
    Args:
        blueprint_name: Name of the Blueprint
        function_name: Name of the function to call
        arguments: Comma-separated list of arguments (if any)
    """
    result = send_command("execute_blueprint_function", {
        "blueprint_name": blueprint_name,
        "function_name": function_name,
        "arguments": arguments
    })
    
    if result.get("status") == "success":
        return result.get("result", "Blueprint function executed successfully")
    else:
        return f"Error: {result.get('message', 'Unknown error')}"

@mcp.tool()
def execute_python(code: str) -> str:
    """
    Execute arbitrary Python code in Unreal Engine.
    
    Args:
        code: Python code to execute
    """

    # need to double all double-quotes
    #escaped = re.escape(code)

    # need to enclose in triple codes for Python exec()
    clean_code = f'""{code}""'

    result = send_command("execute_python", {
        "code": clean_code
    })
    
    if result is None:
        return f"Code executed successfully"
    elif isinstance(result, str):
        return f"Error: {result}"
    elif isinstance(result, dict):
        if result.get("status") == "success":
            return result.get("result", "Code executed successfully")
        else:
            error_msg = result.get('message', 'Unknown error')
            traceback = result.get('traceback', '')
            return f"Error: {error_msg}\n\n{traceback}"
    else:
        return f"Error: {str(result)}"

@mcp.prompt()
def create_castle() -> str:
    """Create a castle"""
    return f"""
Please create a castle in the current Unreal Engine project.
0. Refer to the Unreal Engine Python API when creating new python code: https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/?application_version=5.5
1. Clear all the StaticMeshActors in the scene.
2. Get the project directory and the content directory.
3. Find basic shapes to use for building structures.
4. Create a castle using these basic shapes.
"""

@mcp.prompt()
def create_town() -> str:
    """Create a town"""
    return f"""
Please create a town in the current Unreal Engine project.
0. Refer to the Unreal Engine Python API when creating new python code: https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/?application_version=5.5
1. Clear all the StaticMeshActors in the scene.
2. Get the project directory and the content directory.
3. Find a square static mesh asset that can be used as a floor tile. Note its dimensions.
4. Build a grid with the floor tile that is at least 7500 cm x 7500 cm.
5. Create a town centered on the grid and with the same width and height as the grid.
"""

# Run the server
if __name__ == "__main__":
    print("Starting Unreal Engine MCP Bridge")
    mcp.run(transport='stdio')
