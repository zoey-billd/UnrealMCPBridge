// Copyright Omar Abdelwahed 2026. All Rights Reserved.

#include "ViewportCaptureLibrary.h"
#include "Editor.h"
#include "UnrealClient.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFileManager.h"

FString UViewportCaptureLibrary::CaptureViewport(const FString& OutputPath)
{
	if (!GEditor)
	{
		return TEXT("Error: GEditor is not available");
	}

	FViewport* Viewport = GEditor->GetActiveViewport();
	if (!Viewport)
	{
		return TEXT("Error: No active viewport found");
	}

	FIntPoint ViewportSize = Viewport->GetSizeXY();
	if (ViewportSize.X <= 0 || ViewportSize.Y <= 0)
	{
		return TEXT("Error: Viewport has invalid dimensions");
	}

	// Read pixels from the viewport backbuffer (synchronous — flushes render commands)
	TArray<FColor> Bitmap;
	if (!Viewport->ReadPixels(Bitmap))
	{
		return TEXT("Error: Failed to read viewport pixels");
	}

	// Force alpha to opaque (backbuffer alpha may be undefined)
	for (FColor& Pixel : Bitmap)
	{
		Pixel.A = 255;
	}

	// Encode as PNG
	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

	if (!ImageWrapper.IsValid())
	{
		return TEXT("Error: Failed to create image wrapper");
	}

	if (!ImageWrapper->SetRaw(Bitmap.GetData(), Bitmap.Num() * sizeof(FColor),
		ViewportSize.X, ViewportSize.Y, ERGBFormat::BGRA, 8))
	{
		return TEXT("Error: Failed to set raw image data");
	}

	const TArray64<uint8> CompressedData = ImageWrapper->GetCompressed();
	if (CompressedData.Num() == 0)
	{
		return TEXT("Error: PNG compression produced no data");
	}

	// Ensure output directory exists
	FString Directory = FPaths::GetPath(OutputPath);
	if (!Directory.IsEmpty())
	{
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		PlatformFile.CreateDirectoryTree(*Directory);
	}

	// Save to disk
	if (!FFileHelper::SaveArrayToFile(CompressedData, *OutputPath))
	{
		return FString::Printf(TEXT("Error: Failed to save file to %s"), *OutputPath);
	}

	// Return the absolute path
	return FPaths::ConvertRelativePathToFull(OutputPath);
}
