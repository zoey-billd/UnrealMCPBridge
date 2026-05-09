// Copyright Omar Abdelwahed 2025. All Rights Reserved.

#include "UnrealMCPBridgeStyle.h"
#include "UnrealMCPBridge.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/SlateStyleRegistry.h"
#include "Slate/SlateGameResources.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/SlateStyleMacros.h"

#define RootToContentDir Style->RootToContentDir

TSharedPtr<FSlateStyleSet> FUnrealMCPBridgeStyle::StyleInstance = nullptr;

void FUnrealMCPBridgeStyle::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

void FUnrealMCPBridgeStyle::Shutdown()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
	ensure(StyleInstance.IsUnique());
	StyleInstance.Reset();
}

FName FUnrealMCPBridgeStyle::GetStyleSetName()
{
	static FName StyleSetName(TEXT("UnrealMCPBridgeStyle"));
	return StyleSetName;
}


const FVector2D Icon16x16(16.0f, 16.0f);
const FVector2D Icon20x20(20.0f, 20.0f);

TSharedRef< FSlateStyleSet > FUnrealMCPBridgeStyle::Create()
{
	TSharedRef< FSlateStyleSet > Style = MakeShareable(new FSlateStyleSet("UnrealMCPBridgeStyle"));
	Style->SetContentRoot(IPluginManager::Get().FindPlugin("UnrealMCPBridge")->GetBaseDir() / TEXT("Resources"));

	//Style->Set("UnrealMCPBridge.PluginAction", new IMAGE_BRUSH_SVG(TEXT("PlaceholderButtonIcon"), Icon20x20));
	Style->Set("UnrealMCPBridge.PluginAction", new IMAGE_BRUSH(TEXT("MCPBridgeIcon"), Icon20x20));
	return Style;
}

void FUnrealMCPBridgeStyle::ReloadTextures()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetRenderer()->ReloadTextureResources();
	}
}

const ISlateStyle& FUnrealMCPBridgeStyle::Get()
{
	return *StyleInstance;
}
