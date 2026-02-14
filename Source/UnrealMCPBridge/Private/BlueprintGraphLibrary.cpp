// Copyright Omar Abdelwahed 2026. All Rights Reserved.

#include "BlueprintGraphLibrary.h"

#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"

#include "EdGraphSchema_K2.h"
#include "EdGraphSchema_K2_Actions.h"
#include "K2Node_Event.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_CustomEvent.h"

#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "BlueprintFunctionNodeSpawner.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"

// UE 5.5 PerformAction takes FVector2D; UE 5.6+ uses FVector2f
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
using FGraphNodePosition = FVector2f;
#else
using FGraphNodePosition = FVector2D;
#endif

// =============================================================================
// PRIVATE HELPERS
// =============================================================================

UEdGraph* UBlueprintGraphLibrary::GetEventGraph(UBlueprint* Blueprint)
{
	if (!Blueprint)
	{
		return nullptr;
	}

	UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	if (!EventGraph && Blueprint->UbergraphPages.Num() > 0)
	{
		EventGraph = Blueprint->UbergraphPages[0];
	}
	return EventGraph;
}

UEdGraphNode* UBlueprintGraphLibrary::FindNodeById(UEdGraph* Graph, const FString& NodeId)
{
	if (!Graph)
	{
		return nullptr;
	}

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node && Node->GetName() == NodeId)
		{
			return Node;
		}
	}
	return nullptr;
}

bool UBlueprintGraphLibrary::TypeNameToPinType(const FString& TypeName, FEdGraphPinType& OutPinType)
{
	if (TypeName.Equals(TEXT("bool"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
	}
	else if (TypeName.Equals(TEXT("int"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int;
	}
	else if (TypeName.Equals(TEXT("float"), ESearchCase::IgnoreCase) ||
			 TypeName.Equals(TEXT("double"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Real;
		OutPinType.PinSubCategory = TEXT("double");
	}
	else if (TypeName.Equals(TEXT("string"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_String;
	}
	else if (TypeName.Equals(TEXT("name"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Name;
	}
	else if (TypeName.Equals(TEXT("text"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Text;
	}
	else if (TypeName.Equals(TEXT("Vector"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		OutPinType.PinSubCategoryObject = TBaseStructure<FVector>::Get();
	}
	else if (TypeName.Equals(TEXT("Rotator"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		OutPinType.PinSubCategoryObject = TBaseStructure<FRotator>::Get();
	}
	else if (TypeName.Equals(TEXT("Transform"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		OutPinType.PinSubCategoryObject = TBaseStructure<FTransform>::Get();
	}
	else if (TypeName.StartsWith(TEXT("/")))
	{
		// Class path (e.g., "/Script/Engine.Actor")
		UClass* Class = FindObject<UClass>(nullptr, *TypeName);
		if (!Class)
		{
			Class = LoadObject<UClass>(nullptr, *TypeName);
		}
		if (Class)
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Object;
			OutPinType.PinSubCategoryObject = Class;
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[BlueprintGraphLib] Could not find class: %s"), *TypeName);
			return false;
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[BlueprintGraphLib] Unknown type name: %s"), *TypeName);
		return false;
	}

	return true;
}

// =============================================================================
// ASSET CREATION
// =============================================================================

UBlueprint* UBlueprintGraphLibrary::CreateNewBlueprint(
	const FString& Path,
	const FString& Name,
	UClass* ParentClass)
{
	if (!ParentClass)
	{
		UE_LOG(LogTemp, Error, TEXT("[BlueprintGraphLib] CreateNewBlueprint: ParentClass is null"));
		return nullptr;
	}

	FString PackagePath = Path / Name;
	UPackage* Package = CreatePackage(*PackagePath);
	if (!Package)
	{
		UE_LOG(LogTemp, Error, TEXT("[BlueprintGraphLib] CreateNewBlueprint: Failed to create package at %s"), *PackagePath);
		return nullptr;
	}

	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
		ParentClass,
		Package,
		FName(*Name),
		BPTYPE_Normal,
		UBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass(),
		NAME_None
	);

	if (Blueprint)
	{
		FAssetRegistryModule::AssetCreated(Blueprint);
		Blueprint->MarkPackageDirty();
		UE_LOG(LogTemp, Display, TEXT("[BlueprintGraphLib] Created Blueprint: %s"), *PackagePath);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[BlueprintGraphLib] CreateNewBlueprint: FKismetEditorUtilities::CreateBlueprint failed for %s"), *PackagePath);
	}

	return Blueprint;
}

// =============================================================================
// NODE CREATION
// =============================================================================

FString UBlueprintGraphLibrary::AddEventNode(
	UBlueprint* Blueprint,
	const FString& EventName,
	float PosX,
	float PosY)
{
	if (!Blueprint)
	{
		UE_LOG(LogTemp, Error, TEXT("[BlueprintGraphLib] AddEventNode: Blueprint is null"));
		return FString();
	}

	UEdGraph* EventGraph = GetEventGraph(Blueprint);
	if (!EventGraph)
	{
		UE_LOG(LogTemp, Error, TEXT("[BlueprintGraphLib] AddEventNode: No event graph found"));
		return FString();
	}

	UK2Node_Event* EventNode = NewObject<UK2Node_Event>(EventGraph);
	EventNode->EventReference.SetExternalMember(FName(*EventName), Blueprint->ParentClass);

	FEdGraphSchemaAction_K2NewNode Action;
	Action.NodeTemplate = EventNode;
	UEdGraphPin* NullPin = nullptr;
	Action.PerformAction(EventGraph, NullPin, FGraphNodePosition(PosX, PosY));

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	UE_LOG(LogTemp, Display, TEXT("[BlueprintGraphLib] Added event node: %s -> %s"), *EventName, *EventNode->GetName());
	return EventNode->GetName();
}

FString UBlueprintGraphLibrary::AddCallFunctionNode(
	UBlueprint* Blueprint,
	UClass* TargetClass,
	const FString& FunctionName,
	float PosX,
	float PosY)
{
	if (!Blueprint)
	{
		UE_LOG(LogTemp, Error, TEXT("[BlueprintGraphLib] AddCallFunctionNode: Blueprint is null"));
		return FString();
	}
	if (!TargetClass)
	{
		UE_LOG(LogTemp, Error, TEXT("[BlueprintGraphLib] AddCallFunctionNode: TargetClass is null"));
		return FString();
	}

	UEdGraph* EventGraph = GetEventGraph(Blueprint);
	if (!EventGraph)
	{
		UE_LOG(LogTemp, Error, TEXT("[BlueprintGraphLib] AddCallFunctionNode: No event graph found"));
		return FString();
	}

	UFunction* Function = TargetClass->FindFunctionByName(FName(*FunctionName));
	if (!Function)
	{
		UE_LOG(LogTemp, Error, TEXT("[BlueprintGraphLib] AddCallFunctionNode: Function '%s' not found on class '%s'"),
			*FunctionName, *TargetClass->GetName());
		return FString();
	}

	// Method A: UBlueprintFunctionNodeSpawner (recommended for function calls)
	UBlueprintFunctionNodeSpawner* Spawner = UBlueprintFunctionNodeSpawner::Create(Function);
	if (!Spawner)
	{
		UE_LOG(LogTemp, Error, TEXT("[BlueprintGraphLib] AddCallFunctionNode: Failed to create node spawner"));
		return FString();
	}

	UEdGraphNode* Node = Spawner->Invoke(EventGraph, IBlueprintNodeBinder::FBindingSet(), FVector2D(PosX, PosY));
	if (!Node)
	{
		// Fallback: Method C (direct creation)
		UE_LOG(LogTemp, Warning, TEXT("[BlueprintGraphLib] Spawner->Invoke failed, using direct creation fallback"));

		UK2Node_CallFunction* FuncNode = NewObject<UK2Node_CallFunction>(EventGraph);
		FuncNode->FunctionReference.SetExternalMember(FName(*FunctionName), TargetClass);
		EventGraph->AddNode(FuncNode, true, false);
		FuncNode->AllocateDefaultPins();
		FuncNode->NodePosX = static_cast<int32>(PosX);
		FuncNode->NodePosY = static_cast<int32>(PosY);
		Node = FuncNode;
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	UE_LOG(LogTemp, Display, TEXT("[BlueprintGraphLib] Added function call node: %s::%s -> %s"),
		*TargetClass->GetName(), *FunctionName, *Node->GetName());
	return Node->GetName();
}

FString UBlueprintGraphLibrary::AddVariableGetNode(
	UBlueprint* Blueprint,
	const FString& VariableName,
	float PosX,
	float PosY)
{
	if (!Blueprint)
	{
		UE_LOG(LogTemp, Error, TEXT("[BlueprintGraphLib] AddVariableGetNode: Blueprint is null"));
		return FString();
	}

	UEdGraph* EventGraph = GetEventGraph(Blueprint);
	if (!EventGraph)
	{
		UE_LOG(LogTemp, Error, TEXT("[BlueprintGraphLib] AddVariableGetNode: No event graph found"));
		return FString();
	}

	UK2Node_VariableGet* Node = NewObject<UK2Node_VariableGet>(EventGraph);
	Node->VariableReference.SetSelfMember(FName(*VariableName));

	FEdGraphSchemaAction_K2NewNode Action;
	Action.NodeTemplate = Node;
	UEdGraphPin* NullPin = nullptr;
	Action.PerformAction(EventGraph, NullPin, FGraphNodePosition(PosX, PosY));

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	UE_LOG(LogTemp, Display, TEXT("[BlueprintGraphLib] Added variable get node: %s -> %s"), *VariableName, *Node->GetName());
	return Node->GetName();
}

FString UBlueprintGraphLibrary::AddVariableSetNode(
	UBlueprint* Blueprint,
	const FString& VariableName,
	float PosX,
	float PosY)
{
	if (!Blueprint)
	{
		UE_LOG(LogTemp, Error, TEXT("[BlueprintGraphLib] AddVariableSetNode: Blueprint is null"));
		return FString();
	}

	UEdGraph* EventGraph = GetEventGraph(Blueprint);
	if (!EventGraph)
	{
		UE_LOG(LogTemp, Error, TEXT("[BlueprintGraphLib] AddVariableSetNode: No event graph found"));
		return FString();
	}

	UK2Node_VariableSet* Node = NewObject<UK2Node_VariableSet>(EventGraph);
	Node->VariableReference.SetSelfMember(FName(*VariableName));

	FEdGraphSchemaAction_K2NewNode Action;
	Action.NodeTemplate = Node;
	UEdGraphPin* NullPin = nullptr;
	Action.PerformAction(EventGraph, NullPin, FGraphNodePosition(PosX, PosY));

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	UE_LOG(LogTemp, Display, TEXT("[BlueprintGraphLib] Added variable set node: %s -> %s"), *VariableName, *Node->GetName());
	return Node->GetName();
}

FString UBlueprintGraphLibrary::AddBranchNode(
	UBlueprint* Blueprint,
	float PosX,
	float PosY)
{
	if (!Blueprint)
	{
		UE_LOG(LogTemp, Error, TEXT("[BlueprintGraphLib] AddBranchNode: Blueprint is null"));
		return FString();
	}

	UEdGraph* EventGraph = GetEventGraph(Blueprint);
	if (!EventGraph)
	{
		UE_LOG(LogTemp, Error, TEXT("[BlueprintGraphLib] AddBranchNode: No event graph found"));
		return FString();
	}

	UK2Node_IfThenElse* Node = NewObject<UK2Node_IfThenElse>(EventGraph);

	FEdGraphSchemaAction_K2NewNode Action;
	Action.NodeTemplate = Node;
	UEdGraphPin* NullPin = nullptr;
	Action.PerformAction(EventGraph, NullPin, FGraphNodePosition(PosX, PosY));

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	UE_LOG(LogTemp, Display, TEXT("[BlueprintGraphLib] Added branch node -> %s"), *Node->GetName());
	return Node->GetName();
}

FString UBlueprintGraphLibrary::AddCustomEventNode(
	UBlueprint* Blueprint,
	const FString& EventName,
	float PosX,
	float PosY)
{
	if (!Blueprint)
	{
		UE_LOG(LogTemp, Error, TEXT("[BlueprintGraphLib] AddCustomEventNode: Blueprint is null"));
		return FString();
	}

	UEdGraph* EventGraph = GetEventGraph(Blueprint);
	if (!EventGraph)
	{
		UE_LOG(LogTemp, Error, TEXT("[BlueprintGraphLib] AddCustomEventNode: No event graph found"));
		return FString();
	}

	UK2Node_CustomEvent* Node = NewObject<UK2Node_CustomEvent>(EventGraph);
	Node->CustomFunctionName = FName(*EventName);

	FEdGraphSchemaAction_K2NewNode Action;
	Action.NodeTemplate = Node;
	UEdGraphPin* NullPin = nullptr;
	Action.PerformAction(EventGraph, NullPin, FGraphNodePosition(PosX, PosY));

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	UE_LOG(LogTemp, Display, TEXT("[BlueprintGraphLib] Added custom event node: %s -> %s"), *EventName, *Node->GetName());
	return Node->GetName();
}

// =============================================================================
// PIN OPERATIONS
// =============================================================================

bool UBlueprintGraphLibrary::ConnectPins(
	UBlueprint* Blueprint,
	const FString& SourceNodeId,
	const FString& SourcePinName,
	const FString& TargetNodeId,
	const FString& TargetPinName)
{
	if (!Blueprint)
	{
		UE_LOG(LogTemp, Error, TEXT("[BlueprintGraphLib] ConnectPins: Blueprint is null"));
		return false;
	}

	UEdGraph* EventGraph = GetEventGraph(Blueprint);
	if (!EventGraph)
	{
		UE_LOG(LogTemp, Error, TEXT("[BlueprintGraphLib] ConnectPins: No event graph found"));
		return false;
	}

	UEdGraphNode* SourceNode = FindNodeById(EventGraph, SourceNodeId);
	if (!SourceNode)
	{
		UE_LOG(LogTemp, Error, TEXT("[BlueprintGraphLib] ConnectPins: Source node '%s' not found"), *SourceNodeId);
		return false;
	}

	UEdGraphNode* TargetNode = FindNodeById(EventGraph, TargetNodeId);
	if (!TargetNode)
	{
		UE_LOG(LogTemp, Error, TEXT("[BlueprintGraphLib] ConnectPins: Target node '%s' not found"), *TargetNodeId);
		return false;
	}

	UEdGraphPin* SourcePin = SourceNode->FindPin(FName(*SourcePinName));
	if (!SourcePin)
	{
		UE_LOG(LogTemp, Error, TEXT("[BlueprintGraphLib] ConnectPins: Pin '%s' not found on node '%s'"),
			*SourcePinName, *SourceNodeId);
		return false;
	}

	UEdGraphPin* TargetPin = TargetNode->FindPin(FName(*TargetPinName));
	if (!TargetPin)
	{
		UE_LOG(LogTemp, Error, TEXT("[BlueprintGraphLib] ConnectPins: Pin '%s' not found on node '%s'"),
			*TargetPinName, *TargetNodeId);
		return false;
	}

	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	bool bResult = Schema->TryCreateConnection(SourcePin, TargetPin);

	if (bResult)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		UE_LOG(LogTemp, Display, TEXT("[BlueprintGraphLib] Connected %s.%s -> %s.%s"),
			*SourceNodeId, *SourcePinName, *TargetNodeId, *TargetPinName);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[BlueprintGraphLib] ConnectPins: TryCreateConnection failed for %s.%s -> %s.%s"),
			*SourceNodeId, *SourcePinName, *TargetNodeId, *TargetPinName);
	}

	return bResult;
}

bool UBlueprintGraphLibrary::SetPinDefaultValue(
	UBlueprint* Blueprint,
	const FString& NodeId,
	const FString& PinName,
	const FString& Value)
{
	if (!Blueprint)
	{
		UE_LOG(LogTemp, Error, TEXT("[BlueprintGraphLib] SetPinDefaultValue: Blueprint is null"));
		return false;
	}

	UEdGraph* EventGraph = GetEventGraph(Blueprint);
	if (!EventGraph)
	{
		UE_LOG(LogTemp, Error, TEXT("[BlueprintGraphLib] SetPinDefaultValue: No event graph found"));
		return false;
	}

	UEdGraphNode* Node = FindNodeById(EventGraph, NodeId);
	if (!Node)
	{
		UE_LOG(LogTemp, Error, TEXT("[BlueprintGraphLib] SetPinDefaultValue: Node '%s' not found"), *NodeId);
		return false;
	}

	UEdGraphPin* Pin = Node->FindPin(FName(*PinName));
	if (!Pin)
	{
		UE_LOG(LogTemp, Error, TEXT("[BlueprintGraphLib] SetPinDefaultValue: Pin '%s' not found on node '%s'"),
			*PinName, *NodeId);
		return false;
	}

	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	Schema->TrySetDefaultValue(*Pin, Value);

	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

	UE_LOG(LogTemp, Display, TEXT("[BlueprintGraphLib] Set pin default: %s.%s = %s"), *NodeId, *PinName, *Value);
	return true;
}

// =============================================================================
// VARIABLES
// =============================================================================

bool UBlueprintGraphLibrary::AddVariable(
	UBlueprint* Blueprint,
	const FString& Name,
	const FString& TypeName,
	bool bInstanceEditable)
{
	if (!Blueprint)
	{
		UE_LOG(LogTemp, Error, TEXT("[BlueprintGraphLib] AddVariable: Blueprint is null"));
		return false;
	}

	FEdGraphPinType PinType;
	if (!TypeNameToPinType(TypeName, PinType))
	{
		return false;
	}

	bool bResult = FBlueprintEditorUtils::AddMemberVariable(Blueprint, FName(*Name), PinType);
	if (!bResult)
	{
		UE_LOG(LogTemp, Error, TEXT("[BlueprintGraphLib] AddVariable: Failed to add variable '%s' of type '%s'"),
			*Name, *TypeName);
		return false;
	}

	if (bInstanceEditable)
	{
		FBlueprintEditorUtils::SetBlueprintOnlyEditableFlag(Blueprint, FName(*Name), false);
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	UE_LOG(LogTemp, Display, TEXT("[BlueprintGraphLib] Added variable: %s (%s, editable=%d)"),
		*Name, *TypeName, bInstanceEditable);
	return true;
}

// =============================================================================
// COMPILE / SAVE
// =============================================================================

bool UBlueprintGraphLibrary::CompileAndSaveBlueprint(UBlueprint* Blueprint)
{
	if (!Blueprint)
	{
		UE_LOG(LogTemp, Error, TEXT("[BlueprintGraphLib] CompileAndSaveBlueprint: Blueprint is null"));
		return false;
	}

	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	bool bSuccess = (Blueprint->Status != BS_Error);
	if (!bSuccess)
	{
		UE_LOG(LogTemp, Error, TEXT("[BlueprintGraphLib] CompileAndSaveBlueprint: Compilation failed for %s"),
			*Blueprint->GetName());
		return false;
	}

	UPackage* Package = Blueprint->GetOutermost();
	FString PackageFilename;
	if (FPackageName::TryConvertLongPackageNameToFilename(
		Package->GetName(), PackageFilename, FPackageName::GetAssetPackageExtension()))
	{
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		UPackage::SavePackage(Package, Blueprint, *PackageFilename, SaveArgs);
	}

	UE_LOG(LogTemp, Display, TEXT("[BlueprintGraphLib] Compiled and saved: %s"), *Blueprint->GetName());
	return true;
}

// =============================================================================
// INTROSPECTION
// =============================================================================

TArray<FString> UBlueprintGraphLibrary::GetNodeIds(UBlueprint* Blueprint)
{
	TArray<FString> Result;

	if (!Blueprint)
	{
		UE_LOG(LogTemp, Error, TEXT("[BlueprintGraphLib] GetNodeIds: Blueprint is null"));
		return Result;
	}

	UEdGraph* EventGraph = GetEventGraph(Blueprint);
	if (!EventGraph)
	{
		return Result;
	}

	for (UEdGraphNode* Node : EventGraph->Nodes)
	{
		if (Node)
		{
			Result.Add(Node->GetName());
		}
	}

	return Result;
}

TArray<FString> UBlueprintGraphLibrary::GetPinNames(
	UBlueprint* Blueprint,
	const FString& NodeId)
{
	TArray<FString> Result;

	if (!Blueprint)
	{
		UE_LOG(LogTemp, Error, TEXT("[BlueprintGraphLib] GetPinNames: Blueprint is null"));
		return Result;
	}

	UEdGraph* EventGraph = GetEventGraph(Blueprint);
	if (!EventGraph)
	{
		return Result;
	}

	UEdGraphNode* Node = FindNodeById(EventGraph, NodeId);
	if (!Node)
	{
		UE_LOG(LogTemp, Error, TEXT("[BlueprintGraphLib] GetPinNames: Node '%s' not found"), *NodeId);
		return Result;
	}

	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin)
		{
			Result.Add(Pin->GetName());
		}
	}

	return Result;
}

TArray<FString> UBlueprintGraphLibrary::GetPinDetails(
	UBlueprint* Blueprint,
	const FString& NodeId)
{
	TArray<FString> Result;

	if (!Blueprint)
	{
		UE_LOG(LogTemp, Error, TEXT("[BlueprintGraphLib] GetPinDetails: Blueprint is null"));
		return Result;
	}

	UEdGraph* EventGraph = GetEventGraph(Blueprint);
	if (!EventGraph)
	{
		return Result;
	}

	UEdGraphNode* Node = FindNodeById(EventGraph, NodeId);
	if (!Node)
	{
		UE_LOG(LogTemp, Error, TEXT("[BlueprintGraphLib] GetPinDetails: Node '%s' not found"), *NodeId);
		return Result;
	}

	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin)
		{
			FString Direction = (Pin->Direction == EGPD_Input) ? TEXT("Input") : TEXT("Output");
			FString Category = Pin->PinType.PinCategory.ToString();
			Result.Add(FString::Printf(TEXT("%s|%s|%s"), *Pin->GetName(), *Direction, *Category));
		}
	}

	return Result;
}
