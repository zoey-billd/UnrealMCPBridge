// Copyright Omar Abdelwahed 2025. All Rights Reserved.

#include "UnrealMCPBridge.h"
#include "UnrealMCPBridgeStyle.h"
#include "UnrealMCPBridgeCommands.h"
#include "Misc/MessageDialog.h"
#include "ToolMenus.h"
#include "MCPSocketServer.h"
#include "PythonBridge.h"

static const FName UnrealMCPBridgeTabName("UnrealMCPBridge");

#define LOCTEXT_NAMESPACE "FUnrealMCPBridgeModule"

void FUnrealMCPBridgeModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	
	FUnrealMCPBridgeStyle::Initialize();
	FUnrealMCPBridgeStyle::ReloadTextures();

	FUnrealMCPBridgeCommands::Register();
	
	PluginCommands = MakeShareable(new FUICommandList);

	PluginCommands->MapAction(
		FUnrealMCPBridgeCommands::Get().PluginAction,
		FExecuteAction::CreateRaw(this, &FUnrealMCPBridgeModule::PluginButtonClicked),
		FCanExecuteAction());

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FUnrealMCPBridgeModule::RegisterMenus));

	// Start the socket server when the module loads
    //SocketServer = new FMCPSocketServer();
    //SocketServer->Start();

}

void FUnrealMCPBridgeModule::RestartSocketServer()
{
	// Restart the socket server if it is already running
	if (SocketServer)
	{
		SocketServer->Stop();
		delete SocketServer;
	}
	// Create a new socket server instance and start it
	SocketServer = new FMCPSocketServer();
	SocketServer->Start();
}

void FUnrealMCPBridgeModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.

	// Stop the socket server when the module unloads
    if (SocketServer)
    {
        SocketServer->Stop();
        delete SocketServer;
        SocketServer = nullptr;
    }

    // Clean up Python bridge
    FPythonBridge::Shutdown();

	UToolMenus::UnRegisterStartupCallback(this);

	UToolMenus::UnregisterOwner(this);

	FUnrealMCPBridgeStyle::Shutdown();

	FUnrealMCPBridgeCommands::Unregister();
}

void FUnrealMCPBridgeModule::PluginButtonClicked()
{

	if (!FPythonBridge::bIsInitialized)
	{
		// Put your "OnButtonClicked" stuff here
		FText DialogText = FText::Format(
			LOCTEXT("MCP Bridge", "{0}"),
			FText::FromString(TEXT("MCP Python bridge running."))
		);
		FMessageDialog::Open(EAppMsgType::Ok, DialogText);
		RestartSocketServer();
		FPythonBridge::Initialize();
	}
	else
	{
		FText DialogText = FText::Format(
			LOCTEXT("MCP Bridge", "{0}"),
			FText::FromString(TEXT("Restart MCP Python bridge?"))
		);
		EAppReturnType::Type ReturnType = FMessageDialog::Open(EAppMsgType::OkCancel, DialogText);
		if (ReturnType == EAppReturnType::Ok)
		{
			RestartSocketServer();
			FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("MCP Python bridge restarted.")));
		}
	}

}

void FUnrealMCPBridgeModule::RegisterMenus()
{
	// Owner will be used for cleanup in call to UToolMenus::UnregisterOwner
	FToolMenuOwnerScoped OwnerScoped(this);

	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
		{
			FToolMenuSection& Section = Menu->FindOrAddSection("WindowLayout");
			Section.AddMenuEntryWithCommandList(FUnrealMCPBridgeCommands::Get().PluginAction, PluginCommands);
		}
	}

	{
		UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar");
		{
			FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("PluginTools");
			{
				FToolMenuEntry& Entry = Section.AddEntry(FToolMenuEntry::InitToolBarButton(FUnrealMCPBridgeCommands::Get().PluginAction));
				Entry.SetCommandList(PluginCommands);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FUnrealMCPBridgeModule, UnrealMCPBridge)