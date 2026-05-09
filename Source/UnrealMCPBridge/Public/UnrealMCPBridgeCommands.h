// Copyright Omar Abdelwahed 2025. All Rights Reserved.

#pragma once

#include "Framework/Commands/Commands.h"
#include "UnrealMCPBridgeStyle.h"

class FUnrealMCPBridgeCommands : public TCommands<FUnrealMCPBridgeCommands>
{
public:

	FUnrealMCPBridgeCommands()
		: TCommands<FUnrealMCPBridgeCommands>(TEXT("UnrealMCPBridge"), NSLOCTEXT("Contexts", "UnrealMCPBridge", "UnrealMCPBridge Plugin"), NAME_None, FUnrealMCPBridgeStyle::GetStyleSetName())
	{
	}

	// TCommands<> interface
	virtual void RegisterCommands() override;

public:
	TSharedPtr< FUICommandInfo > PluginAction;
};
