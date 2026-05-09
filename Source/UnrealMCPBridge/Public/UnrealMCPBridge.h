// Copyright Omar Abdelwahed 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FToolBarBuilder;
class FMenuBuilder;

class FUnrealMCPBridgeModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	
	/** This function will be bound to Command. */
	void PluginButtonClicked();

	void RestartSocketServer();
	
private:
	class FMCPSocketServer* SocketServer;

	void RegisterMenus();


private:
	TSharedPtr<class FUICommandList> PluginCommands;
};
