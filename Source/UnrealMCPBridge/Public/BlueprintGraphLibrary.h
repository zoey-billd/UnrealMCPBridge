// Copyright Omar Abdelwahed 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BlueprintGraphLibrary.generated.h"

class UBlueprint;
class UEdGraph;
class UEdGraphNode;
struct FEdGraphPinType;

/**
 * Blueprint function library for programmatic Blueprint graph manipulation.
 * Wraps C++ graph APIs (AddNode, ConnectPins, AllocateDefaultPins) that are
 * not UFUNCTIONs and therefore not accessible from the Python API.
 *
 * All functions are static and BlueprintCallable, auto-reflecting to Python via:
 *   import unreal
 *   lib = unreal.BlueprintGraphLibrary
 *
 * Node IDs are UEdGraphNode::GetName() strings. Pin names are internal names
 * ("execute", "then", "ReturnValue"), not display names.
 */
UCLASS()
class UBlueprintGraphLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// =========================================================================
	// ASSET CREATION
	// =========================================================================

	/** Create a new Blueprint asset.
	 * @param Path Content path (e.g., "/Game/Blueprints")
	 * @param Name Blueprint name (e.g., "BP_MyActor")
	 * @param ParentClass Parent class (e.g., AActor::StaticClass())
	 * @return The created Blueprint, or nullptr on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Blueprint Graph|Asset")
	static UBlueprint* CreateNewBlueprint(
		const FString& Path,
		const FString& Name,
		UClass* ParentClass);

	// =========================================================================
	// NODE CREATION (returns node ID string for pin connections)
	// =========================================================================

	/** Add an event node (BeginPlay, Tick, etc.).
	 * @param Blueprint Target Blueprint
	 * @param EventName Event function name (e.g., "ReceiveBeginPlay")
	 * @param PosX Node X position in graph
	 * @param PosY Node Y position in graph
	 * @return Node ID string, or empty on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Blueprint Graph|Nodes")
	static FString AddEventNode(
		UBlueprint* Blueprint,
		const FString& EventName,
		float PosX = 0.f,
		float PosY = 0.f);

	/** Add a function call node.
	 * @param Blueprint Target Blueprint
	 * @param TargetClass Class that owns the function
	 * @param FunctionName Function name (e.g., "PrintString")
	 * @param PosX Node X position in graph
	 * @param PosY Node Y position in graph
	 * @return Node ID string, or empty on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Blueprint Graph|Nodes")
	static FString AddCallFunctionNode(
		UBlueprint* Blueprint,
		UClass* TargetClass,
		const FString& FunctionName,
		float PosX = 0.f,
		float PosY = 0.f);

	/** Add a variable Get node.
	 * @param Blueprint Target Blueprint
	 * @param VariableName Name of the Blueprint variable
	 * @param PosX Node X position in graph
	 * @param PosY Node Y position in graph
	 * @return Node ID string, or empty on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Blueprint Graph|Nodes")
	static FString AddVariableGetNode(
		UBlueprint* Blueprint,
		const FString& VariableName,
		float PosX = 0.f,
		float PosY = 0.f);

	/** Add a variable Set node.
	 * @param Blueprint Target Blueprint
	 * @param VariableName Name of the Blueprint variable
	 * @param PosX Node X position in graph
	 * @param PosY Node Y position in graph
	 * @return Node ID string, or empty on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Blueprint Graph|Nodes")
	static FString AddVariableSetNode(
		UBlueprint* Blueprint,
		const FString& VariableName,
		float PosX = 0.f,
		float PosY = 0.f);

	/** Add a Branch (if/else) node.
	 * @param Blueprint Target Blueprint
	 * @param PosX Node X position in graph
	 * @param PosY Node Y position in graph
	 * @return Node ID string, or empty on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Blueprint Graph|Nodes")
	static FString AddBranchNode(
		UBlueprint* Blueprint,
		float PosX = 0.f,
		float PosY = 0.f);

	/** Add a Custom Event node.
	 * @param Blueprint Target Blueprint
	 * @param EventName Custom event name
	 * @param PosX Node X position in graph
	 * @param PosY Node Y position in graph
	 * @return Node ID string, or empty on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Blueprint Graph|Nodes")
	static FString AddCustomEventNode(
		UBlueprint* Blueprint,
		const FString& EventName,
		float PosX = 0.f,
		float PosY = 0.f);

	// =========================================================================
	// PIN OPERATIONS
	// =========================================================================

	/** Connect two pins between nodes.
	 * @param Blueprint Target Blueprint
	 * @param SourceNodeId Source node ID (from Add*Node return value)
	 * @param SourcePinName Source pin internal name (e.g., "then")
	 * @param TargetNodeId Target node ID
	 * @param TargetPinName Target pin internal name (e.g., "execute")
	 * @return true if connection succeeded
	 */
	UFUNCTION(BlueprintCallable, Category = "Blueprint Graph|Pins")
	static bool ConnectPins(
		UBlueprint* Blueprint,
		const FString& SourceNodeId,
		const FString& SourcePinName,
		const FString& TargetNodeId,
		const FString& TargetPinName);

	/** Set the default value of a pin.
	 * @param Blueprint Target Blueprint
	 * @param NodeId Node ID
	 * @param PinName Pin internal name
	 * @param Value String representation of the value
	 * @return true if value was set
	 */
	UFUNCTION(BlueprintCallable, Category = "Blueprint Graph|Pins")
	static bool SetPinDefaultValue(
		UBlueprint* Blueprint,
		const FString& NodeId,
		const FString& PinName,
		const FString& Value);

	// =========================================================================
	// VARIABLES
	// =========================================================================

	/** Add a variable to a Blueprint.
	 * @param Blueprint Target Blueprint
	 * @param Name Variable name
	 * @param TypeName Type: "bool", "int", "float", "string", "Vector", "Rotator", or a class path
	 * @param bInstanceEditable If true, variable is editable per instance
	 * @return true if variable was added
	 */
	UFUNCTION(BlueprintCallable, Category = "Blueprint Graph|Variables")
	static bool AddVariable(
		UBlueprint* Blueprint,
		const FString& Name,
		const FString& TypeName,
		bool bInstanceEditable = true);

	// =========================================================================
	// COMPILE / SAVE
	// =========================================================================

	/** Compile and save a Blueprint.
	 * @param Blueprint The Blueprint to compile and save
	 * @return true if compilation succeeded without errors
	 */
	UFUNCTION(BlueprintCallable, Category = "Blueprint Graph|Compile")
	static bool CompileAndSaveBlueprint(UBlueprint* Blueprint);

	// =========================================================================
	// INTROSPECTION
	// =========================================================================

	/** Get all node IDs in a Blueprint's event graph.
	 * @param Blueprint Target Blueprint
	 * @return Array of node ID strings
	 */
	UFUNCTION(BlueprintCallable, Category = "Blueprint Graph|Introspection")
	static TArray<FString> GetNodeIds(UBlueprint* Blueprint);

	/** Get all pin names on a node.
	 * @param Blueprint Target Blueprint
	 * @param NodeId Node ID
	 * @return Array of pin internal names
	 */
	UFUNCTION(BlueprintCallable, Category = "Blueprint Graph|Introspection")
	static TArray<FString> GetPinNames(
		UBlueprint* Blueprint,
		const FString& NodeId);

	/** Get detailed pin info for a node.
	 * @param Blueprint Target Blueprint
	 * @param NodeId Node ID
	 * @return Array of strings in format "PinName|Direction|Type"
	 */
	UFUNCTION(BlueprintCallable, Category = "Blueprint Graph|Introspection")
	static TArray<FString> GetPinDetails(
		UBlueprint* Blueprint,
		const FString& NodeId);

private:
	static UEdGraph* GetEventGraph(UBlueprint* Blueprint);
	static UEdGraphNode* FindNodeById(UEdGraph* Graph, const FString& NodeId);
	static bool TypeNameToPinType(const FString& TypeName, FEdGraphPinType& OutPinType);
};
