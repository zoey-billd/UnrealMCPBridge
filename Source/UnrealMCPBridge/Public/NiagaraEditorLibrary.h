// Copyright Omar Abdelwahed 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "NiagaraEditorLibrary.generated.h"

class UNiagaraSystem;
class UNiagaraEmitter;
class UNiagaraScript;
class UNiagaraNodeFunctionCall;
class UNiagaraNodeOutput;
struct FVersionedNiagaraEmitterData;
struct FNiagaraEmitterHandle;
enum class ENiagaraScriptUsage : uint8;

/**
 * Blueprint function library for programmatic Niagara system creation.
 * Wraps C++ Niagara editor APIs that are not UFUNCTIONs and therefore
 * not accessible from the Python API.
 *
 * All functions are static and BlueprintCallable, auto-reflecting to Python via:
 *   import unreal
 *   lib = unreal.NiagaraEditorLibrary
 *
 * Execution categories: "EmitterSpawn", "EmitterUpdate", "ParticleSpawn", "ParticleUpdate"
 * Module node names are returned from AddModule() for use in parameter setting.
 */
UCLASS()
class UNiagaraEditorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// =========================================================================
	// SYSTEM MANAGEMENT
	// =========================================================================

	/** Create a new empty Niagara System asset.
	 * @param Path Content path (e.g., "/Game/VFX")
	 * @param Name System name (e.g., "NS_Rain")
	 * @return The created system, or nullptr on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Niagara Editor|System")
	static UNiagaraSystem* CreateNiagaraSystem(
		const FString& Path,
		const FString& Name);

	/** Compile and save a Niagara System.
	 * @param System The system to compile
	 * @return true if compilation succeeded
	 */
	UFUNCTION(BlueprintCallable, Category = "Niagara Editor|System")
	static bool CompileNiagaraSystem(UNiagaraSystem* System);

	// =========================================================================
	// EMITTER MANAGEMENT
	// =========================================================================

	/** Add an empty emitter to a system with default sprite renderer.
	 * Sets up graph source, output nodes for all 4 stages.
	 * @param System Target system
	 * @param EmitterName Display name for the emitter
	 * @return Emitter index (0-based), or -1 on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Niagara Editor|Emitter")
	static int32 AddEmptyEmitter(
		UNiagaraSystem* System,
		const FString& EmitterName);

	/** Get the number of emitters in a system.
	 * @param System Target system
	 * @return Number of emitters
	 */
	UFUNCTION(BlueprintCallable, Category = "Niagara Editor|Emitter")
	static int32 GetEmitterCount(UNiagaraSystem* System);

	/** Get names of all emitters in a system.
	 * @param System Target system
	 * @return Array of emitter display names
	 */
	UFUNCTION(BlueprintCallable, Category = "Niagara Editor|Emitter")
	static TArray<FString> GetEmitterNames(UNiagaraSystem* System);

	// =========================================================================
	// MODULE MANAGEMENT
	// =========================================================================

	/** Add a module to an emitter stage.
	 * @param System Target system
	 * @param EmitterIndex Emitter index (from AddEmptyEmitter)
	 * @param ScriptAssetPath Full asset path (e.g., "/Niagara/Modules/Emitter/SpawnRate.SpawnRate")
	 * @param ExecutionCategory Stage: "EmitterSpawn", "EmitterUpdate", "ParticleSpawn", "ParticleUpdate"
	 * @return Module node name for parameter setting, or empty on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Niagara Editor|Modules")
	static FString AddModule(
		UNiagaraSystem* System,
		int32 EmitterIndex,
		const FString& ScriptAssetPath,
		const FString& ExecutionCategory);

	/** List module node names in an emitter stage.
	 * @param System Target system
	 * @param EmitterIndex Emitter index
	 * @param ExecutionCategory Stage name
	 * @return Array of module node names
	 */
	UFUNCTION(BlueprintCallable, Category = "Niagara Editor|Modules")
	static TArray<FString> GetModules(
		UNiagaraSystem* System,
		int32 EmitterIndex,
		const FString& ExecutionCategory);

	// =========================================================================
	// PARAMETER SETTING (Rapid Iteration Parameters)
	// =========================================================================

	/** Set a float parameter on a module.
	 * @param System Target system
	 * @param EmitterIndex Emitter index
	 * @param ModuleNodeName Module node name (from AddModule)
	 * @param ParameterName Parameter name (e.g., "SpawnRate")
	 * @param Value Float value
	 * @return true if parameter was set
	 */
	UFUNCTION(BlueprintCallable, Category = "Niagara Editor|Parameters")
	static bool SetFloatParameter(
		UNiagaraSystem* System,
		int32 EmitterIndex,
		const FString& ModuleNodeName,
		const FString& ParameterName,
		float Value);

	/** Set a vector parameter on a module.
	 * @param System Target system
	 * @param EmitterIndex Emitter index
	 * @param ModuleNodeName Module node name (from AddModule)
	 * @param ParameterName Parameter name (e.g., "Velocity")
	 * @param X X component
	 * @param Y Y component
	 * @param Z Z component
	 * @return true if parameter was set
	 */
	UFUNCTION(BlueprintCallable, Category = "Niagara Editor|Parameters")
	static bool SetVectorParameter(
		UNiagaraSystem* System,
		int32 EmitterIndex,
		const FString& ModuleNodeName,
		const FString& ParameterName,
		float X, float Y, float Z);

	/** Set an int parameter on a module.
	 * @param System Target system
	 * @param EmitterIndex Emitter index
	 * @param ModuleNodeName Module node name (from AddModule)
	 * @param ParameterName Parameter name
	 * @param Value Int value
	 * @return true if parameter was set
	 */
	UFUNCTION(BlueprintCallable, Category = "Niagara Editor|Parameters")
	static bool SetIntParameter(
		UNiagaraSystem* System,
		int32 EmitterIndex,
		const FString& ModuleNodeName,
		const FString& ParameterName,
		int32 Value);

	/** Set a bool parameter on a module.
	 * @param System Target system
	 * @param EmitterIndex Emitter index
	 * @param ModuleNodeName Module node name (from AddModule)
	 * @param ParameterName Parameter name
	 * @param Value Bool value
	 * @return true if parameter was set
	 */
	UFUNCTION(BlueprintCallable, Category = "Niagara Editor|Parameters")
	static bool SetBoolParameter(
		UNiagaraSystem* System,
		int32 EmitterIndex,
		const FString& ModuleNodeName,
		const FString& ParameterName,
		bool Value);

	/** Set a Vector2D parameter on a module (e.g., Sprite Size).
	 * @param System Target system
	 * @param EmitterIndex Emitter index
	 * @param ModuleNodeName Module node name (from AddModule)
	 * @param ParameterName Parameter name (e.g., "Sprite Size")
	 * @param X X component
	 * @param Y Y component
	 * @return true if parameter was set
	 */
	UFUNCTION(BlueprintCallable, Category = "Niagara Editor|Parameters")
	static bool SetVector2Parameter(
		UNiagaraSystem* System,
		int32 EmitterIndex,
		const FString& ModuleNodeName,
		const FString& ParameterName,
		float X, float Y);

	/** Set a position parameter on a module (e.g., Beam Start, Beam End).
	 * Niagara has a special "Position" type distinct from Vector3 for world-space positions.
	 * @param System Target system
	 * @param EmitterIndex Emitter index
	 * @param ModuleNodeName Module node name (from AddModule)
	 * @param ParameterName Parameter name (e.g., "Beam End")
	 * @param X X component
	 * @param Y Y component
	 * @param Z Z component
	 * @return true if parameter was set
	 */
	UFUNCTION(BlueprintCallable, Category = "Niagara Editor|Parameters")
	static bool SetPositionParameter(
		UNiagaraSystem* System,
		int32 EmitterIndex,
		const FString& ModuleNodeName,
		const FString& ParameterName,
		float X, float Y, float Z);

	/** Set a linear color parameter on a module (e.g., Color).
	 * @param System Target system
	 * @param EmitterIndex Emitter index
	 * @param ModuleNodeName Module node name (from AddModule)
	 * @param ParameterName Parameter name (e.g., "Color")
	 * @param R Red (0-1)
	 * @param G Green (0-1)
	 * @param B Blue (0-1)
	 * @param A Alpha (0-1)
	 * @return true if parameter was set
	 */
	UFUNCTION(BlueprintCallable, Category = "Niagara Editor|Parameters")
	static bool SetColorParameter(
		UNiagaraSystem* System,
		int32 EmitterIndex,
		const FString& ModuleNodeName,
		const FString& ParameterName,
		float R, float G, float B, float A);

	// =========================================================================
	// STATIC SWITCH PARAMETERS
	// =========================================================================

	/** Set a static switch value on a module (e.g., Sprite Size Mode, Color Mode).
	 * Static switches are dropdown/enum inputs that control which code paths are
	 * active in a module. They cannot be set via rapid iteration parameters.
	 * @param System Target system
	 * @param EmitterIndex Emitter index
	 * @param ModuleNodeName Module node name (from AddModule or GetModules)
	 * @param SwitchName Name of the static switch (e.g., "Sprite Size Mode")
	 * @param Value Integer value for the switch (enum index)
	 * @return true if the switch was found and set
	 */
	UFUNCTION(BlueprintCallable, Category = "Niagara Editor|Parameters")
	static bool SetStaticSwitchValue(
		UNiagaraSystem* System,
		int32 EmitterIndex,
		const FString& ModuleNodeName,
		const FString& SwitchName,
		int32 Value);

	/** List all static switch pins on a module with their current values.
	 * @param System Target system
	 * @param EmitterIndex Emitter index
	 * @param ModuleNodeName Module node name
	 * @return Array of strings in format "PinName=CurrentValue"
	 */
	UFUNCTION(BlueprintCallable, Category = "Niagara Editor|Introspection")
	static TArray<FString> GetStaticSwitchValues(
		UNiagaraSystem* System,
		int32 EmitterIndex,
		const FString& ModuleNodeName);

	// =========================================================================
	// RENDERER MANAGEMENT
	// =========================================================================

	/** Add a sprite renderer to an emitter.
	 * @param System Target system
	 * @param EmitterIndex Emitter index
	 * @return true if renderer was added
	 */
	UFUNCTION(BlueprintCallable, Category = "Niagara Editor|Renderers")
	static bool AddSpriteRenderer(
		UNiagaraSystem* System,
		int32 EmitterIndex);

	/** Add a ribbon renderer to an emitter.
	 * @param System Target system
	 * @param EmitterIndex Emitter index
	 * @return true if renderer was added
	 */
	UFUNCTION(BlueprintCallable, Category = "Niagara Editor|Renderers")
	static bool AddRibbonRenderer(
		UNiagaraSystem* System,
		int32 EmitterIndex);

	/** Remove all sprite renderers from an emitter.
	 * Useful when switching to ribbon/mesh rendering (e.g., beam effects).
	 * @param System Target system
	 * @param EmitterIndex Emitter index
	 * @return true if any sprite renderers were removed
	 */
	UFUNCTION(BlueprintCallable, Category = "Niagara Editor|Renderers")
	static bool RemoveSpriteRenderer(
		UNiagaraSystem* System,
		int32 EmitterIndex);

	/** Set sprite renderer alignment mode.
	 * @param System Target system
	 * @param EmitterIndex Emitter index
	 * @param Alignment "Unaligned", "VelocityAligned", "CustomAlignment", or "Automatic"
	 * @return true if set successfully
	 */
	UFUNCTION(BlueprintCallable, Category = "Niagara Editor|Renderers")
	static bool SetSpriteRendererAlignment(
		UNiagaraSystem* System,
		int32 EmitterIndex,
		const FString& Alignment);

	/** Set sprite renderer facing mode.
	 * @param System Target system
	 * @param EmitterIndex Emitter index
	 * @param FacingMode "FaceCamera", "FaceCameraPlane", "CustomFacingVector",
	 *                   "FaceCameraPosition", "FaceCameraDistanceBlend", or "Automatic"
	 * @return true if set successfully
	 */
	UFUNCTION(BlueprintCallable, Category = "Niagara Editor|Renderers")
	static bool SetSpriteRendererFacingMode(
		UNiagaraSystem* System,
		int32 EmitterIndex,
		const FString& FacingMode);

	/** Set ribbon renderer material.
	 * @param System Target system
	 * @param EmitterIndex Emitter index
	 * @param MaterialPath Asset path to the material (e.g., "/Niagara/DefaultAssets/DefaultRibbonMaterial")
	 * @return true if set successfully
	 */
	UFUNCTION(BlueprintCallable, Category = "Niagara Editor|Renderers")
	static bool SetRibbonRendererMaterial(
		UNiagaraSystem* System,
		int32 EmitterIndex,
		const FString& MaterialPath);

	/** Set ribbon renderer tessellation for angular/spiky effects (e.g., lightning).
	 * Sets TessellationMode to Custom and applies CurveTension.
	 * @param System Target system
	 * @param EmitterIndex Emitter index
	 * @param CurveTension Curve tension (0-0.99). Higher = sharper/more angular. 0.5 is good for lightning.
	 * @return true if set successfully
	 */
	UFUNCTION(BlueprintCallable, Category = "Niagara Editor|Renderers")
	static bool SetRibbonRendererTessellation(
		UNiagaraSystem* System,
		int32 EmitterIndex,
		float CurveTension);

	// =========================================================================
	// EMITTER PROPERTIES
	// =========================================================================

	/** Set the simulation target for an emitter (CPU or GPU).
	 * @param System Target system
	 * @param EmitterIndex Emitter index
	 * @param SimTarget "CPUSim" or "GPUComputeSim"
	 * @return true if set successfully
	 */
	UFUNCTION(BlueprintCallable, Category = "Niagara Editor|Emitter")
	static bool SetEmitterSimTarget(
		UNiagaraSystem* System,
		int32 EmitterIndex,
		const FString& SimTarget);

	/** Set fixed bounding box for an emitter. Also sets CalculateBoundsMode to Fixed.
	 * Required for GPU simulation.
	 * @param System Target system
	 * @param EmitterIndex Emitter index
	 * @param MinX/MinY/MinZ Minimum bounds corner
	 * @param MaxX/MaxY/MaxZ Maximum bounds corner
	 * @return true if set successfully
	 */
	UFUNCTION(BlueprintCallable, Category = "Niagara Editor|Emitter")
	static bool SetEmitterFixedBounds(
		UNiagaraSystem* System,
		int32 EmitterIndex,
		float MinX, float MinY, float MinZ,
		float MaxX, float MaxY, float MaxZ);

	// =========================================================================
	// CURVE DATA INTERFACES
	// =========================================================================

	/** Set keys on a float curve data interface (e.g., ScaleSpriteSize uniform curve).
	 * Requires a prior compile so CachedDefaultDataInterfaces is populated.
	 * @param System Target system
	 * @param EmitterIndex Emitter index
	 * @param ModuleNodeName Module node name containing the curve
	 * @param ParameterName Parameter name of the curve input
	 * @param Times Comma-separated time values (e.g., "0.0,0.5,1.0")
	 * @param Values Comma-separated float values (e.g., "0.0,1.0,0.0")
	 * @return true if curve keys were set
	 */
	UFUNCTION(BlueprintCallable, Category = "Niagara Editor|Curves")
	static bool SetFloatCurveKeys(
		UNiagaraSystem* System,
		int32 EmitterIndex,
		const FString& ModuleNodeName,
		const FString& ParameterName,
		const FString& Times,
		const FString& Values);

	/** Set keys on a color curve data interface (e.g., ScaleColor gradient).
	 * Requires a prior compile so CachedDefaultDataInterfaces is populated.
	 * @param System Target system
	 * @param EmitterIndex Emitter index
	 * @param ModuleNodeName Module node name containing the curve
	 * @param ParameterName Parameter name of the curve input
	 * @param Times Comma-separated time values
	 * @param RedValues Comma-separated red channel values (0-1)
	 * @param GreenValues Comma-separated green channel values (0-1)
	 * @param BlueValues Comma-separated blue channel values (0-1)
	 * @param AlphaValues Comma-separated alpha channel values (0-1)
	 * @return true if curve keys were set
	 */
	UFUNCTION(BlueprintCallable, Category = "Niagara Editor|Curves")
	static bool SetColorCurveKeys(
		UNiagaraSystem* System,
		int32 EmitterIndex,
		const FString& ModuleNodeName,
		const FString& ParameterName,
		const FString& Times,
		const FString& RedValues,
		const FString& GreenValues,
		const FString& BlueValues,
		const FString& AlphaValues);

	/** Set keys on a Vector2D curve data interface (e.g., non-uniform sprite size curve).
	 * Requires a prior compile so CachedDefaultDataInterfaces is populated.
	 * @param System Target system
	 * @param EmitterIndex Emitter index
	 * @param ModuleNodeName Module node name containing the curve
	 * @param ParameterName Parameter name of the curve input
	 * @param Times Comma-separated time values
	 * @param XValues Comma-separated X channel values
	 * @param YValues Comma-separated Y channel values
	 * @return true if curve keys were set
	 */
	UFUNCTION(BlueprintCallable, Category = "Niagara Editor|Curves")
	static bool SetVector2DCurveKeys(
		UNiagaraSystem* System,
		int32 EmitterIndex,
		const FString& ModuleNodeName,
		const FString& ParameterName,
		const FString& Times,
		const FString& XValues,
		const FString& YValues);

	// =========================================================================
	// INTROSPECTION
	// =========================================================================

	/** Get parameter info for a module.
	 * @param System Target system
	 * @param EmitterIndex Emitter index
	 * @param ModuleNodeName Module node name
	 * @return Array of strings in format "Name|Type"
	 */
	UFUNCTION(BlueprintCallable, Category = "Niagara Editor|Introspection")
	static TArray<FString> GetModuleParameters(
		UNiagaraSystem* System,
		int32 EmitterIndex,
		const FString& ModuleNodeName);

	/** Get all renderer properties on an emitter.
	 * Returns key properties for each renderer (type, material, alignment, tessellation, etc.)
	 * @param System Target system
	 * @param EmitterIndex Emitter index
	 * @return Array of strings in format "RendererType|Property=Value"
	 */
	UFUNCTION(BlueprintCallable, Category = "Niagara Editor|Introspection")
	static TArray<FString> GetRendererProperties(
		UNiagaraSystem* System,
		int32 EmitterIndex);

	/** List all rapid iteration parameters currently set on an emitter's scripts.
	 * Useful for debugging whether SetFloatParameter/SetVectorParameter actually applied.
	 * @param System Target system
	 * @param EmitterIndex Emitter index
	 * @return Array of strings in format "ScriptUsage|ParamName|Type|Value"
	 */
	UFUNCTION(BlueprintCallable, Category = "Niagara Editor|Introspection")
	static TArray<FString> ListRapidIterationParameters(
		UNiagaraSystem* System,
		int32 EmitterIndex);

	/** Get the actual input variable names from a module's script graph.
	 * Walks the called graph's MapGet nodes to find real parameter names.
	 * These are the names you should use with SetFloatParameter, etc.
	 * @param System Target system
	 * @param EmitterIndex Emitter index
	 * @param ModuleNodeName Module node name
	 * @return Array of strings in format "Name|Type"
	 */
	UFUNCTION(BlueprintCallable, Category = "Niagara Editor|Introspection")
	static TArray<FString> GetModuleInputs(
		UNiagaraSystem* System,
		int32 EmitterIndex,
		const FString& ModuleNodeName);

	/** List all cached data interface objects in an emitter's compiled scripts.
	 * Useful for debugging curve DI discovery. Requires a prior compile.
	 * @param System Target system
	 * @param EmitterIndex Emitter index
	 * @return Array of strings in format "ScriptStage|Name|CompileName|Class"
	 */
	UFUNCTION(BlueprintCallable, Category = "Niagara Editor|Introspection")
	static TArray<FString> ListCachedDataInterfaces(
		UNiagaraSystem* System,
		int32 EmitterIndex);

private:
	static bool GetEmitterAndData(
		UNiagaraSystem* System,
		int32 EmitterIndex,
		FNiagaraEmitterHandle*& OutHandle,
		FVersionedNiagaraEmitterData*& OutData);

	static UNiagaraNodeOutput* FindOutputNodeForCategory(
		FVersionedNiagaraEmitterData* EmitterData,
		const FString& ExecutionCategory);

	static UNiagaraNodeFunctionCall* FindModuleNode(
		FVersionedNiagaraEmitterData* EmitterData,
		const FString& ModuleNodeName);

	static bool CategoryToScriptUsage(
		const FString& Category,
		ENiagaraScriptUsage& OutUsage);

	static UNiagaraScript* GetScriptForCategory(
		FVersionedNiagaraEmitterData* EmitterData,
		const FString& ExecutionCategory);
};
