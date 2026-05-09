// Copyright Omar Abdelwahed 2025. All Rights Reserved.

#include "UnrealMCPBridgeCommands.h"

#define LOCTEXT_NAMESPACE "FUnrealMCPBridgeModule"

void FUnrealMCPBridgeCommands::RegisterCommands()
{
	UI_COMMAND(PluginAction, "UnrealMCPBridge", "Start MCP Bridge", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
