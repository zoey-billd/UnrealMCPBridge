// Copyright Omar Abdelwahed 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ViewportCaptureLibrary.generated.h"

/**
 * Blueprint function library for capturing the editor viewport as an image.
 * Provides synchronous viewport screenshot capture via ReadPixels.
 *
 * Auto-reflects to Python via:
 *   import unreal
 *   path = unreal.ViewportCaptureLibrary.capture_viewport("/path/to/output.png")
 */
UCLASS()
class UViewportCaptureLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Capture the active editor viewport to a PNG file.
	 * Reads the current backbuffer synchronously via FViewport::ReadPixels().
	 *
	 * @param OutputPath Absolute file path for the output PNG (e.g., "D:/Project/Saved/Screenshots/mcp_viewport.png")
	 * @return The absolute file path on success, or a string starting with "Error:" on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "MCP|Viewport")
	static FString CaptureViewport(const FString& OutputPath);
};
