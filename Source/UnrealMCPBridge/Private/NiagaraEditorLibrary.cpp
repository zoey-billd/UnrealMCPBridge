// Copyright Omar Abdelwahed 2026. All Rights Reserved.

#include "NiagaraEditorLibrary.h"

#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraNodeInput.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraConstants.h"
#include "NiagaraSettings.h"
#include "NiagaraEditorSettings.h"
#include "NiagaraEmitterEditorData.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraRibbonRendererProperties.h"

#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"
#include "NiagaraEditorUtilities.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "EdGraph/EdGraph.h"

// =============================================================================
// FILE-LOCAL HELPERS (replacing unexported NiagaraEditor functions)
// =============================================================================

/**
 * Creates an output node + input node for a Niagara graph stage and connects them.
 * Replaces FNiagaraStackGraphUtilities::ResetGraphForOutput which is not exported.
 * Uses FGraphNodeCreator (engine template) which handles NewObject, AddNode, AllocateDefaultPins.
 */
static UNiagaraNodeOutput* CreateOutputNodeForStage(
	UNiagaraGraph* Graph,
	ENiagaraScriptUsage ScriptUsage,
	FGuid ScriptUsageId)
{
	// Create output node
	FGraphNodeCreator<UNiagaraNodeOutput> OutputNodeCreator(*Graph);
	UNiagaraNodeOutput* OutputNode = OutputNodeCreator.CreateNode();
	OutputNode->SetUsage(ScriptUsage);
	OutputNode->SetUsageId(ScriptUsageId);
	OutputNode->Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetParameterMapDef(), TEXT("Out")));
	OutputNodeCreator.Finalize();

	// Create input node
	FGraphNodeCreator<UNiagaraNodeInput> InputNodeCreator(*Graph);
	UNiagaraNodeInput* InputNode = InputNodeCreator.CreateNode();
	InputNode->Input = FNiagaraVariable(FNiagaraTypeDefinition::GetParameterMapDef(), TEXT("InputMap"));
	InputNode->Usage = ENiagaraInputNodeUsage::Parameter;
	InputNodeCreator.Finalize();

	// Connect: input node output pin -> output node input pin
	UEdGraphPin* OutputInputPin = nullptr;
	for (UEdGraphPin* Pin : OutputNode->Pins)
	{
		if (Pin->Direction == EGPD_Input)
		{
			OutputInputPin = Pin;
			break;
		}
	}

	UEdGraphPin* InputOutputPin = nullptr;
	for (UEdGraphPin* Pin : InputNode->Pins)
	{
		if (Pin->Direction == EGPD_Output)
		{
			InputOutputPin = Pin;
			break;
		}
	}

	if (OutputInputPin && InputOutputPin)
	{
		InputOutputPin->MakeLinkTo(OutputInputPin);
	}

	return OutputNode;
}

/**
 * Finds an output node in a graph by usage type.
 * Replaces UNiagaraGraph::FindOutputNode which is not exported.
 */
static UNiagaraNodeOutput* FindOutputNodeManual(
	UNiagaraGraph* Graph,
	ENiagaraScriptUsage TargetUsage,
	FGuid TargetUsageId = FGuid())
{
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		UNiagaraNodeOutput* OutputNode = Cast<UNiagaraNodeOutput>(Node);
		if (OutputNode && OutputNode->GetUsage() == TargetUsage)
		{
			if (!TargetUsageId.IsValid() || OutputNode->GetUsageId() == TargetUsageId)
			{
				return OutputNode;
			}
		}
	}
	return nullptr;
}

/**
 * Finds the target script for a module node based on its stage usage.
 */
static UNiagaraScript* FindTargetScriptForModule(
	FVersionedNiagaraEmitterData* EmitterData,
	UNiagaraNodeFunctionCall* ModuleNode)
{
	ENiagaraScriptUsage Usage = FNiagaraStackGraphUtilities::GetOutputNodeUsage(*ModuleNode);

	if (Usage == ENiagaraScriptUsage::EmitterSpawnScript)
	{
		return EmitterData->EmitterSpawnScriptProps.Script;
	}
	if (Usage == ENiagaraScriptUsage::EmitterUpdateScript)
	{
		return EmitterData->EmitterUpdateScriptProps.Script;
	}
	if (Usage == ENiagaraScriptUsage::ParticleSpawnScript ||
		Usage == ENiagaraScriptUsage::ParticleSpawnScriptInterpolated)
	{
		return EmitterData->SpawnScriptProps.Script;
	}
	if (Usage == ENiagaraScriptUsage::ParticleUpdateScript)
	{
		return EmitterData->UpdateScriptProps.Script;
	}

	return nullptr;
}

// =============================================================================
// PRIVATE CLASS HELPERS
// =============================================================================

bool UNiagaraEditorLibrary::CategoryToScriptUsage(
	const FString& Category,
	ENiagaraScriptUsage& OutUsage)
{
	if (Category.Equals(TEXT("EmitterSpawn"), ESearchCase::IgnoreCase))
	{
		OutUsage = ENiagaraScriptUsage::EmitterSpawnScript;
	}
	else if (Category.Equals(TEXT("EmitterUpdate"), ESearchCase::IgnoreCase))
	{
		OutUsage = ENiagaraScriptUsage::EmitterUpdateScript;
	}
	else if (Category.Equals(TEXT("ParticleSpawn"), ESearchCase::IgnoreCase))
	{
		OutUsage = ENiagaraScriptUsage::ParticleSpawnScript;
	}
	else if (Category.Equals(TEXT("ParticleUpdate"), ESearchCase::IgnoreCase))
	{
		OutUsage = ENiagaraScriptUsage::ParticleUpdateScript;
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[NiagaraEditorLib] Unknown execution category: %s. Use EmitterSpawn, EmitterUpdate, ParticleSpawn, or ParticleUpdate."), *Category);
		return false;
	}
	return true;
}

bool UNiagaraEditorLibrary::GetEmitterAndData(
	UNiagaraSystem* System,
	int32 EmitterIndex,
	FNiagaraEmitterHandle*& OutHandle,
	FVersionedNiagaraEmitterData*& OutData)
{
	if (!System)
	{
		UE_LOG(LogTemp, Error, TEXT("[NiagaraEditorLib] System is null"));
		return false;
	}

	TArray<FNiagaraEmitterHandle>& Handles = System->GetEmitterHandles();
	if (EmitterIndex < 0 || EmitterIndex >= Handles.Num())
	{
		UE_LOG(LogTemp, Error, TEXT("[NiagaraEditorLib] Emitter index %d out of range (0-%d)"), EmitterIndex, Handles.Num() - 1);
		return false;
	}

	OutHandle = &Handles[EmitterIndex];
	OutData = OutHandle->GetEmitterData();
	if (!OutData)
	{
		UE_LOG(LogTemp, Error, TEXT("[NiagaraEditorLib] No emitter data for index %d"), EmitterIndex);
		return false;
	}

	return true;
}

UNiagaraScript* UNiagaraEditorLibrary::GetScriptForCategory(
	FVersionedNiagaraEmitterData* EmitterData,
	const FString& ExecutionCategory)
{
	if (!EmitterData)
	{
		return nullptr;
	}

	if (ExecutionCategory.Equals(TEXT("EmitterSpawn"), ESearchCase::IgnoreCase))
	{
		return EmitterData->EmitterSpawnScriptProps.Script;
	}
	else if (ExecutionCategory.Equals(TEXT("EmitterUpdate"), ESearchCase::IgnoreCase))
	{
		return EmitterData->EmitterUpdateScriptProps.Script;
	}
	else if (ExecutionCategory.Equals(TEXT("ParticleSpawn"), ESearchCase::IgnoreCase))
	{
		return EmitterData->SpawnScriptProps.Script;
	}
	else if (ExecutionCategory.Equals(TEXT("ParticleUpdate"), ESearchCase::IgnoreCase))
	{
		return EmitterData->UpdateScriptProps.Script;
	}
	return nullptr;
}

UNiagaraNodeOutput* UNiagaraEditorLibrary::FindOutputNodeForCategory(
	FVersionedNiagaraEmitterData* EmitterData,
	const FString& ExecutionCategory)
{
	if (!EmitterData || !EmitterData->GraphSource)
	{
		return nullptr;
	}

	UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(EmitterData->GraphSource);
	if (!Source || !Source->NodeGraph)
	{
		return nullptr;
	}

	ENiagaraScriptUsage Usage;
	if (!CategoryToScriptUsage(ExecutionCategory, Usage))
	{
		return nullptr;
	}

	UNiagaraScript* Script = GetScriptForCategory(EmitterData, ExecutionCategory);
	if (!Script)
	{
		return nullptr;
	}

	// Use manual iteration instead of unexported UNiagaraGraph::FindOutputNode
	return FindOutputNodeManual(Source->NodeGraph, Usage, Script->GetUsageId());
}

UNiagaraNodeFunctionCall* UNiagaraEditorLibrary::FindModuleNode(
	FVersionedNiagaraEmitterData* EmitterData,
	const FString& ModuleNodeName)
{
	if (!EmitterData || !EmitterData->GraphSource)
	{
		return nullptr;
	}

	UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(EmitterData->GraphSource);
	if (!Source || !Source->NodeGraph)
	{
		return nullptr;
	}

	for (UEdGraphNode* Node : Source->NodeGraph->Nodes)
	{
		UNiagaraNodeFunctionCall* FuncNode = Cast<UNiagaraNodeFunctionCall>(Node);
		if (FuncNode && FuncNode->GetName() == ModuleNodeName)
		{
			return FuncNode;
		}
	}
	return nullptr;
}

// =============================================================================
// SYSTEM MANAGEMENT
// =============================================================================

UNiagaraSystem* UNiagaraEditorLibrary::CreateNiagaraSystem(
	const FString& Path,
	const FString& Name)
{
	FString PackagePath = Path / Name;
	UPackage* Package = CreatePackage(*PackagePath);
	if (!Package)
	{
		UE_LOG(LogTemp, Error, TEXT("[NiagaraEditorLib] Failed to create package at %s"), *PackagePath);
		return nullptr;
	}

	UNiagaraSystem* System = NewObject<UNiagaraSystem>(Package, FName(*Name), RF_Public | RF_Standalone | RF_Transactional);
	if (!System)
	{
		UE_LOG(LogTemp, Error, TEXT("[NiagaraEditorLib] Failed to create NiagaraSystem"));
		return nullptr;
	}

	// Initialize system scripts (following NiagaraSystemFactoryNew::InitializeSystem)
	UNiagaraScript* SysSpawnScript = System->GetSystemSpawnScript();
	UNiagaraScript* SysUpdateScript = System->GetSystemUpdateScript();

	UNiagaraScriptSource* SystemScriptSource = NewObject<UNiagaraScriptSource>(SysSpawnScript, TEXT("SystemScriptSource"), RF_Transactional);
	if (SystemScriptSource)
	{
		SystemScriptSource->NodeGraph = NewObject<UNiagaraGraph>(SystemScriptSource, TEXT("SystemScriptGraph"), RF_Transactional);
	}

	SysSpawnScript->SetLatestSource(SystemScriptSource);
	SysUpdateScript->SetLatestSource(SystemScriptSource);

	// Create system output nodes using FGraphNodeCreator (replaces unexported ResetGraphForOutput)
	if (SystemScriptSource && SystemScriptSource->NodeGraph)
	{
		CreateOutputNodeForStage(SystemScriptSource->NodeGraph, ENiagaraScriptUsage::SystemSpawnScript, SysSpawnScript->GetUsageId());
		UNiagaraNodeOutput* UpdateOutputNode = CreateOutputNodeForStage(SystemScriptSource->NodeGraph, ENiagaraScriptUsage::SystemUpdateScript, SysUpdateScript->GetUsageId());

		// Add required system update script
		FSoftObjectPath SystemUpdateScriptRef = GetDefault<UNiagaraEditorSettings>()->RequiredSystemUpdateScript;
		UObject* LoadedScript = SystemUpdateScriptRef.TryLoad();
		UNiagaraScript* RequiredScript = LoadedScript ? Cast<UNiagaraScript>(LoadedScript) : nullptr;
		if (RequiredScript && UpdateOutputNode)
		{
			// Use exported UNiagaraScript* overload (NIAGARAEDITOR_API)
			FNiagaraStackGraphUtilities::AddScriptModuleToStack(RequiredScript, *UpdateOutputNode);
		}
	}

	// Assign default effect type
	const UNiagaraSettings* NiagaraConf = GetDefault<UNiagaraSettings>();
	if (NiagaraConf && System->GetEffectType() == nullptr && NiagaraConf->GetDefaultEffectType())
	{
		System->SetEffectType(NiagaraConf->GetDefaultEffectType());
	}

	FAssetRegistryModule::AssetCreated(System);
	System->MarkPackageDirty();

	UE_LOG(LogTemp, Display, TEXT("[NiagaraEditorLib] Created Niagara System: %s"), *PackagePath);
	return System;
}

bool UNiagaraEditorLibrary::CompileNiagaraSystem(UNiagaraSystem* System)
{
	if (!System)
	{
		UE_LOG(LogTemp, Error, TEXT("[NiagaraEditorLib] CompileNiagaraSystem: System is null"));
		return false;
	}

	System->RequestCompile(false);
	System->PollForCompilationComplete();

	// Save
	UPackage* Package = System->GetOutermost();
	FString PackageFilename;
	if (FPackageName::TryConvertLongPackageNameToFilename(
		Package->GetName(), PackageFilename, FPackageName::GetAssetPackageExtension()))
	{
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		UPackage::SavePackage(Package, System, *PackageFilename, SaveArgs);
	}

	UE_LOG(LogTemp, Display, TEXT("[NiagaraEditorLib] Compiled and saved: %s"), *System->GetName());
	return true;
}

// =============================================================================
// EMITTER MANAGEMENT
// =============================================================================

int32 UNiagaraEditorLibrary::AddEmptyEmitter(
	UNiagaraSystem* System,
	const FString& EmitterName)
{
	if (!System)
	{
		UE_LOG(LogTemp, Error, TEXT("[NiagaraEditorLib] AddEmptyEmitter: System is null"));
		return -1;
	}

	UNiagaraEmitter* SourceEmitter = nullptr;
	FGuid EmitterVersion;

	// Try loading the default empty emitter template from editor settings
	// (same pattern as FNiagaraSystemViewModel::AddMinimalEmitter)
	FSoftObjectPath DefaultEmitterPath = GetDefault<UNiagaraEditorSettings>()->DefaultEmptyEmitter;
	if (DefaultEmitterPath.IsValid() && DefaultEmitterPath.IsAsset())
	{
		UNiagaraEmitter* TemplateEmitter = Cast<UNiagaraEmitter>(DefaultEmitterPath.TryLoad());
		if (TemplateEmitter)
		{
			EmitterVersion = TemplateEmitter->GetExposedVersion().VersionGuid;
			// Duplicate template to transient package with the desired name.
			// AddEmitterToSystem derives the handle name from GetFName(), so the
			// duplicate must have the user's EmitterName as its FName.
			SourceEmitter = Cast<UNiagaraEmitter>(
				StaticDuplicateObject(TemplateEmitter, GetTransientPackage(), FName(*EmitterName)));
			if (SourceEmitter)
			{
				SourceEmitter->SetUniqueEmitterName(EmitterName);
			}
			UE_LOG(LogTemp, Display, TEXT("[NiagaraEditorLib] Using default empty emitter template: %s"), *DefaultEmitterPath.ToString());
		}
	}

	// Fallback: create emitter from scratch if no template available
	if (!SourceEmitter)
	{
		SourceEmitter = NewObject<UNiagaraEmitter>(GetTransientPackage(), FName(*EmitterName), RF_Transactional);
		if (!SourceEmitter)
		{
			UE_LOG(LogTemp, Error, TEXT("[NiagaraEditorLib] Failed to create UNiagaraEmitter"));
			return -1;
		}

		SourceEmitter->SetUniqueEmitterName(EmitterName);
		SourceEmitter->CheckVersionDataAvailable();
		FVersionedNiagaraEmitterData* EmitterData = SourceEmitter->GetLatestEmitterData();
		if (!EmitterData)
		{
			UE_LOG(LogTemp, Error, TEXT("[NiagaraEditorLib] Failed to get emitter data"));
			return -1;
		}

		EmitterData->SimTarget = ENiagaraSimTarget::CPUSim;

		// Create graph source
		UNiagaraScriptSource* Source = NewObject<UNiagaraScriptSource>(SourceEmitter, NAME_None, RF_Transactional);
		UNiagaraGraph* CreatedGraph = NewObject<UNiagaraGraph>(Source, NAME_None, RF_Transactional);
		Source->NodeGraph = CreatedGraph;

		EmitterData->GraphSource = Source;
		EmitterData->SpawnScriptProps.Script->SetLatestSource(Source);
		EmitterData->UpdateScriptProps.Script->SetLatestSource(Source);
		EmitterData->EmitterSpawnScriptProps.Script->SetLatestSource(Source);
		EmitterData->EmitterUpdateScriptProps.Script->SetLatestSource(Source);
		EmitterData->GetGPUComputeScript()->SetLatestSource(Source);

		CreateOutputNodeForStage(CreatedGraph, ENiagaraScriptUsage::EmitterSpawnScript, EmitterData->EmitterSpawnScriptProps.Script->GetUsageId());
		CreateOutputNodeForStage(CreatedGraph, ENiagaraScriptUsage::EmitterUpdateScript, EmitterData->EmitterUpdateScriptProps.Script->GetUsageId());
		CreateOutputNodeForStage(CreatedGraph, ENiagaraScriptUsage::ParticleSpawnScript, EmitterData->SpawnScriptProps.Script->GetUsageId());
		CreateOutputNodeForStage(CreatedGraph, ENiagaraScriptUsage::ParticleUpdateScript, EmitterData->UpdateScriptProps.Script->GetUsageId());

		SourceEmitter->AddRenderer(NewObject<UNiagaraSpriteRendererProperties>(SourceEmitter, TEXT("Renderer")), EmitterData->Version.VersionGuid);

		EmitterData->bDeterminism = false;
		EmitterData->SpawnScriptProps.Script->SetUsage(ENiagaraScriptUsage::ParticleSpawnScriptInterpolated);

		EmitterVersion = EmitterData->Version.VersionGuid;
		UE_LOG(LogTemp, Display, TEXT("[NiagaraEditorLib] Created emitter from scratch (no template available)"));
	}

	// Use the proper editor utility instead of raw System->AddEmitterHandle().
	// AddEmitterToSystem handles: KillSystemInstances, Modify(), unique naming,
	// RebuildEmitterNodes, and SynchronizeOverviewGraphWithSystem — all required
	// for the emitter to appear in the Niagara editor.
	FNiagaraEditorUtilities::AddEmitterToSystem(*System, *SourceEmitter, EmitterVersion);

	int32 EmitterIndex = System->GetNumEmitters() - 1;

	UE_LOG(LogTemp, Display, TEXT("[NiagaraEditorLib] Added emitter '%s' at index %d"), *EmitterName, EmitterIndex);
	return EmitterIndex;
}

int32 UNiagaraEditorLibrary::GetEmitterCount(UNiagaraSystem* System)
{
	if (!System)
	{
		return 0;
	}
	return System->GetNumEmitters();
}

TArray<FString> UNiagaraEditorLibrary::GetEmitterNames(UNiagaraSystem* System)
{
	TArray<FString> Result;
	if (!System)
	{
		return Result;
	}

	const TArray<FNiagaraEmitterHandle>& Handles = System->GetEmitterHandles();
	for (const FNiagaraEmitterHandle& Handle : Handles)
	{
		Result.Add(Handle.GetName().ToString());
	}
	return Result;
}

// =============================================================================
// MODULE MANAGEMENT
// =============================================================================

FString UNiagaraEditorLibrary::AddModule(
	UNiagaraSystem* System,
	int32 EmitterIndex,
	const FString& ScriptAssetPath,
	const FString& ExecutionCategory)
{
	FNiagaraEmitterHandle* Handle = nullptr;
	FVersionedNiagaraEmitterData* EmitterData = nullptr;
	if (!GetEmitterAndData(System, EmitterIndex, Handle, EmitterData))
	{
		return FString();
	}

	// Load the module script
	FSoftObjectPath AssetRef(ScriptAssetPath);
	UObject* LoadedObj = AssetRef.TryLoad();
	UNiagaraScript* AssetScript = LoadedObj ? Cast<UNiagaraScript>(LoadedObj) : nullptr;
	if (!AssetScript)
	{
		UE_LOG(LogTemp, Error, TEXT("[NiagaraEditorLib] Failed to load module script: %s"), *ScriptAssetPath);
		return FString();
	}

	// Find the output node for the target stage
	UNiagaraNodeOutput* OutputNode = FindOutputNodeForCategory(EmitterData, ExecutionCategory);
	if (!OutputNode)
	{
		UE_LOG(LogTemp, Error, TEXT("[NiagaraEditorLib] No output node for category '%s'"), *ExecutionCategory);
		return FString();
	}

	// Add module to stack (using exported UNiagaraScript* overload, not unexported FAssetData overload)
	UNiagaraNodeFunctionCall* NewModule = FNiagaraStackGraphUtilities::AddScriptModuleToStack(AssetScript, *OutputNode);
	if (!NewModule)
	{
		UE_LOG(LogTemp, Error, TEXT("[NiagaraEditorLib] AddScriptModuleToStack failed for %s"), *ScriptAssetPath);
		return FString();
	}

	UE_LOG(LogTemp, Display, TEXT("[NiagaraEditorLib] Added module %s to %s -> %s"),
		*ScriptAssetPath, *ExecutionCategory, *NewModule->GetName());
	return NewModule->GetName();
}

TArray<FString> UNiagaraEditorLibrary::GetModules(
	UNiagaraSystem* System,
	int32 EmitterIndex,
	const FString& ExecutionCategory)
{
	TArray<FString> Result;

	FNiagaraEmitterHandle* Handle = nullptr;
	FVersionedNiagaraEmitterData* EmitterData = nullptr;
	if (!GetEmitterAndData(System, EmitterIndex, Handle, EmitterData))
	{
		return Result;
	}

	UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(EmitterData->GraphSource);
	if (!Source || !Source->NodeGraph)
	{
		return Result;
	}

	ENiagaraScriptUsage TargetUsage;
	if (!CategoryToScriptUsage(ExecutionCategory, TargetUsage))
	{
		return Result;
	}

	for (UEdGraphNode* Node : Source->NodeGraph->Nodes)
	{
		UNiagaraNodeFunctionCall* FuncNode = Cast<UNiagaraNodeFunctionCall>(Node);
		if (FuncNode)
		{
			ENiagaraScriptUsage NodeUsage = FNiagaraStackGraphUtilities::GetOutputNodeUsage(*FuncNode);
			if (NodeUsage == TargetUsage)
			{
				Result.Add(FuncNode->GetName());
			}
		}
	}

	return Result;
}

// =============================================================================
// PARAMETER SETTING (Rapid Iteration Parameters)
// =============================================================================

/**
 * Sets a rapid iteration parameter on a module.
 * Replaces unexported FNiagaraStackGraphUtilities::CreateRapidIterationParameter
 * with exported FNiagaraUtilities::CreateRapidIterationConstantName + manual FNiagaraVariable construction.
 */
static bool SetRapidIterationParameterHelper(
	FNiagaraEmitterHandle* Handle,
	FVersionedNiagaraEmitterData* EmitterData,
	UNiagaraNodeFunctionCall* ModuleNode,
	const FString& ParameterName,
	const FNiagaraTypeDefinition& TypeDef,
	const void* ValueData,
	int32 ValueSize)
{
	UNiagaraScript* TargetScript = FindTargetScriptForModule(EmitterData, ModuleNode);
	if (!TargetScript)
	{
		UE_LOG(LogTemp, Error, TEXT("[NiagaraEditorLib] Could not find target script for module '%s'"), *ModuleNode->GetName());
		return false;
	}

	UNiagaraEmitter* Emitter = Handle->GetInstance().Emitter;
	FString UniqueEmitterName = Emitter ? Emitter->GetUniqueEmitterName() : FString();

	FNiagaraParameterHandle InputHandle = FNiagaraParameterHandle::CreateModuleParameterHandle(FName(*ParameterName));
	FNiagaraParameterHandle AliasedInputHandle = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(InputHandle, ModuleNode);

	// Use exported FNiagaraUtilities::CreateRapidIterationConstantName (NIAGARA_API)
	// instead of unexported FNiagaraStackGraphUtilities::CreateRapidIterationParameter
	FString RapidIterParamName = FNiagaraUtilities::CreateRapidIterationConstantName(
		AliasedInputHandle.GetParameterHandleString(),
		*UniqueEmitterName,
		TargetScript->GetUsage());

	FNiagaraVariable RapidIterationParameter;
	RapidIterationParameter.SetName(FName(*RapidIterParamName));
	RapidIterationParameter.SetType(TypeDef);

	RapidIterationParameter.AllocateData();
	FMemory::Memcpy(RapidIterationParameter.GetData(), ValueData, ValueSize);

	bool bAddParameterIfMissing = true;
	TargetScript->RapidIterationParameters.SetParameterData(RapidIterationParameter.GetData(), RapidIterationParameter, bAddParameterIfMissing);

	return true;
}

bool UNiagaraEditorLibrary::SetFloatParameter(
	UNiagaraSystem* System,
	int32 EmitterIndex,
	const FString& ModuleNodeName,
	const FString& ParameterName,
	float Value)
{
	FNiagaraEmitterHandle* Handle = nullptr;
	FVersionedNiagaraEmitterData* EmitterData = nullptr;
	if (!GetEmitterAndData(System, EmitterIndex, Handle, EmitterData))
	{
		return false;
	}

	UNiagaraNodeFunctionCall* ModuleNode = FindModuleNode(EmitterData, ModuleNodeName);
	if (!ModuleNode)
	{
		UE_LOG(LogTemp, Error, TEXT("[NiagaraEditorLib] Module node '%s' not found"), *ModuleNodeName);
		return false;
	}

	bool bResult = SetRapidIterationParameterHelper(Handle, EmitterData, ModuleNode, ParameterName,
		FNiagaraTypeDefinition::GetFloatDef(), &Value, sizeof(float));

	if (bResult)
	{
		UE_LOG(LogTemp, Display, TEXT("[NiagaraEditorLib] Set float %s.%s = %f"), *ModuleNodeName, *ParameterName, Value);
	}
	return bResult;
}

bool UNiagaraEditorLibrary::SetVectorParameter(
	UNiagaraSystem* System,
	int32 EmitterIndex,
	const FString& ModuleNodeName,
	const FString& ParameterName,
	float X, float Y, float Z)
{
	FNiagaraEmitterHandle* Handle = nullptr;
	FVersionedNiagaraEmitterData* EmitterData = nullptr;
	if (!GetEmitterAndData(System, EmitterIndex, Handle, EmitterData))
	{
		return false;
	}

	UNiagaraNodeFunctionCall* ModuleNode = FindModuleNode(EmitterData, ModuleNodeName);
	if (!ModuleNode)
	{
		UE_LOG(LogTemp, Error, TEXT("[NiagaraEditorLib] Module node '%s' not found"), *ModuleNodeName);
		return false;
	}

	FVector3f VecValue(X, Y, Z);
	bool bResult = SetRapidIterationParameterHelper(Handle, EmitterData, ModuleNode, ParameterName,
		FNiagaraTypeDefinition::GetVec3Def(), &VecValue, sizeof(FVector3f));

	if (bResult)
	{
		UE_LOG(LogTemp, Display, TEXT("[NiagaraEditorLib] Set vector %s.%s = (%f, %f, %f)"), *ModuleNodeName, *ParameterName, X, Y, Z);
	}
	return bResult;
}

bool UNiagaraEditorLibrary::SetIntParameter(
	UNiagaraSystem* System,
	int32 EmitterIndex,
	const FString& ModuleNodeName,
	const FString& ParameterName,
	int32 Value)
{
	FNiagaraEmitterHandle* Handle = nullptr;
	FVersionedNiagaraEmitterData* EmitterData = nullptr;
	if (!GetEmitterAndData(System, EmitterIndex, Handle, EmitterData))
	{
		return false;
	}

	UNiagaraNodeFunctionCall* ModuleNode = FindModuleNode(EmitterData, ModuleNodeName);
	if (!ModuleNode)
	{
		UE_LOG(LogTemp, Error, TEXT("[NiagaraEditorLib] Module node '%s' not found"), *ModuleNodeName);
		return false;
	}

	bool bResult = SetRapidIterationParameterHelper(Handle, EmitterData, ModuleNode, ParameterName,
		FNiagaraTypeDefinition::GetIntDef(), &Value, sizeof(int32));

	if (bResult)
	{
		UE_LOG(LogTemp, Display, TEXT("[NiagaraEditorLib] Set int %s.%s = %d"), *ModuleNodeName, *ParameterName, Value);
	}
	return bResult;
}

bool UNiagaraEditorLibrary::SetBoolParameter(
	UNiagaraSystem* System,
	int32 EmitterIndex,
	const FString& ModuleNodeName,
	const FString& ParameterName,
	bool Value)
{
	FNiagaraEmitterHandle* Handle = nullptr;
	FVersionedNiagaraEmitterData* EmitterData = nullptr;
	if (!GetEmitterAndData(System, EmitterIndex, Handle, EmitterData))
	{
		return false;
	}

	UNiagaraNodeFunctionCall* ModuleNode = FindModuleNode(EmitterData, ModuleNodeName);
	if (!ModuleNode)
	{
		UE_LOG(LogTemp, Error, TEXT("[NiagaraEditorLib] Module node '%s' not found"), *ModuleNodeName);
		return false;
	}

	FNiagaraBool NiagaraBoolValue;
	NiagaraBoolValue.SetValue(Value);
	bool bResult = SetRapidIterationParameterHelper(Handle, EmitterData, ModuleNode, ParameterName,
		FNiagaraTypeDefinition::GetBoolDef(), &NiagaraBoolValue, sizeof(FNiagaraBool));

	if (bResult)
	{
		UE_LOG(LogTemp, Display, TEXT("[NiagaraEditorLib] Set bool %s.%s = %s"), *ModuleNodeName, *ParameterName, Value ? TEXT("true") : TEXT("false"));
	}
	return bResult;
}

bool UNiagaraEditorLibrary::SetVector2Parameter(
	UNiagaraSystem* System,
	int32 EmitterIndex,
	const FString& ModuleNodeName,
	const FString& ParameterName,
	float X, float Y)
{
	FNiagaraEmitterHandle* Handle = nullptr;
	FVersionedNiagaraEmitterData* EmitterData = nullptr;
	if (!GetEmitterAndData(System, EmitterIndex, Handle, EmitterData))
	{
		return false;
	}

	UNiagaraNodeFunctionCall* ModuleNode = FindModuleNode(EmitterData, ModuleNodeName);
	if (!ModuleNode)
	{
		UE_LOG(LogTemp, Error, TEXT("[NiagaraEditorLib] Module node '%s' not found"), *ModuleNodeName);
		return false;
	}

	FVector2f Vec2Value(X, Y);
	bool bResult = SetRapidIterationParameterHelper(Handle, EmitterData, ModuleNode, ParameterName,
		FNiagaraTypeDefinition::GetVec2Def(), &Vec2Value, sizeof(FVector2f));

	if (bResult)
	{
		UE_LOG(LogTemp, Display, TEXT("[NiagaraEditorLib] Set vec2 %s.%s = (%f, %f)"), *ModuleNodeName, *ParameterName, X, Y);
	}
	return bResult;
}

bool UNiagaraEditorLibrary::SetPositionParameter(
	UNiagaraSystem* System,
	int32 EmitterIndex,
	const FString& ModuleNodeName,
	const FString& ParameterName,
	float X, float Y, float Z)
{
	FNiagaraEmitterHandle* Handle = nullptr;
	FVersionedNiagaraEmitterData* EmitterData = nullptr;
	if (!GetEmitterAndData(System, EmitterIndex, Handle, EmitterData))
	{
		return false;
	}

	UNiagaraNodeFunctionCall* ModuleNode = FindModuleNode(EmitterData, ModuleNodeName);
	if (!ModuleNode)
	{
		UE_LOG(LogTemp, Error, TEXT("[NiagaraEditorLib] Module node '%s' not found"), *ModuleNodeName);
		return false;
	}

	FVector3f PosValue(X, Y, Z);
	bool bResult = SetRapidIterationParameterHelper(Handle, EmitterData, ModuleNode, ParameterName,
		FNiagaraTypeDefinition::GetPositionDef(), &PosValue, sizeof(FVector3f));

	if (bResult)
	{
		UE_LOG(LogTemp, Display, TEXT("[NiagaraEditorLib] Set position %s.%s = (%f, %f, %f)"), *ModuleNodeName, *ParameterName, X, Y, Z);
	}
	return bResult;
}

bool UNiagaraEditorLibrary::SetColorParameter(
	UNiagaraSystem* System,
	int32 EmitterIndex,
	const FString& ModuleNodeName,
	const FString& ParameterName,
	float R, float G, float B, float A)
{
	FNiagaraEmitterHandle* Handle = nullptr;
	FVersionedNiagaraEmitterData* EmitterData = nullptr;
	if (!GetEmitterAndData(System, EmitterIndex, Handle, EmitterData))
	{
		return false;
	}

	UNiagaraNodeFunctionCall* ModuleNode = FindModuleNode(EmitterData, ModuleNodeName);
	if (!ModuleNode)
	{
		UE_LOG(LogTemp, Error, TEXT("[NiagaraEditorLib] Module node '%s' not found"), *ModuleNodeName);
		return false;
	}

	FLinearColor ColorValue(R, G, B, A);
	bool bResult = SetRapidIterationParameterHelper(Handle, EmitterData, ModuleNode, ParameterName,
		FNiagaraTypeDefinition::GetColorDef(), &ColorValue, sizeof(FLinearColor));

	if (bResult)
	{
		UE_LOG(LogTemp, Display, TEXT("[NiagaraEditorLib] Set color %s.%s = (%f, %f, %f, %f)"), *ModuleNodeName, *ParameterName, R, G, B, A);
	}
	return bResult;
}

// =============================================================================
// STATIC SWITCH PARAMETERS
// =============================================================================

bool UNiagaraEditorLibrary::SetStaticSwitchValue(
	UNiagaraSystem* System,
	int32 EmitterIndex,
	const FString& ModuleNodeName,
	const FString& SwitchName,
	int32 Value)
{
	FNiagaraEmitterHandle* Handle = nullptr;
	FVersionedNiagaraEmitterData* EmitterData = nullptr;
	if (!GetEmitterAndData(System, EmitterIndex, Handle, EmitterData))
	{
		return false;
	}

	UNiagaraNodeFunctionCall* ModuleNode = FindModuleNode(EmitterData, ModuleNodeName);
	if (!ModuleNode)
	{
		UE_LOG(LogTemp, Error, TEXT("[NiagaraEditorLib] Module node '%s' not found"), *ModuleNodeName);
		return false;
	}

	// Static switch values are stored as pin DefaultValue strings on the module node.
	// Niagara enum switches use "NewEnumeratorN" format; bool switches use "true"/"false".
	for (UEdGraphPin* Pin : ModuleNode->Pins)
	{
		if (Pin->Direction == EGPD_Input && Pin->PinName.ToString().Equals(SwitchName, ESearchCase::IgnoreCase))
		{
			// Detect format from current value
			FString ValueStr;
			if (Pin->DefaultValue.StartsWith(TEXT("NewEnumerator")) || Pin->DefaultValue.StartsWith(TEXT("newenumerator")))
			{
				ValueStr = FString::Printf(TEXT("NewEnumerator%d"), Value);
			}
			else if (Pin->DefaultValue.Equals(TEXT("true"), ESearchCase::IgnoreCase) || Pin->DefaultValue.Equals(TEXT("false"), ESearchCase::IgnoreCase))
			{
				ValueStr = Value != 0 ? TEXT("true") : TEXT("false");
			}
			else
			{
				ValueStr = FString::FromInt(Value);
			}
			Pin->DefaultValue = ValueStr;
			UE_LOG(LogTemp, Display, TEXT("[NiagaraEditorLib] Set static switch %s.%s = %s"), *ModuleNodeName, *SwitchName, *ValueStr);
			return true;
		}
	}

	UE_LOG(LogTemp, Error, TEXT("[NiagaraEditorLib] Static switch pin '%s' not found on '%s'"), *SwitchName, *ModuleNodeName);
	return false;
}

TArray<FString> UNiagaraEditorLibrary::GetStaticSwitchValues(
	UNiagaraSystem* System,
	int32 EmitterIndex,
	const FString& ModuleNodeName)
{
	TArray<FString> Result;

	FNiagaraEmitterHandle* Handle = nullptr;
	FVersionedNiagaraEmitterData* EmitterData = nullptr;
	if (!GetEmitterAndData(System, EmitterIndex, Handle, EmitterData))
	{
		return Result;
	}

	UNiagaraNodeFunctionCall* ModuleNode = FindModuleNode(EmitterData, ModuleNodeName);
	if (!ModuleNode)
	{
		UE_LOG(LogTemp, Error, TEXT("[NiagaraEditorLib] GetStaticSwitchValues: Module '%s' not found"), *ModuleNodeName);
		return Result;
	}

	for (UEdGraphPin* Pin : ModuleNode->Pins)
	{
		if (Pin->Direction == EGPD_Input)
		{
			Result.Add(FString::Printf(TEXT("%s=%s"), *Pin->PinName.ToString(), *Pin->DefaultValue));
		}
	}

	return Result;
}

// =============================================================================
// RENDERER MANAGEMENT
// =============================================================================

bool UNiagaraEditorLibrary::AddSpriteRenderer(
	UNiagaraSystem* System,
	int32 EmitterIndex)
{
	FNiagaraEmitterHandle* Handle = nullptr;
	FVersionedNiagaraEmitterData* EmitterData = nullptr;
	if (!GetEmitterAndData(System, EmitterIndex, Handle, EmitterData))
	{
		return false;
	}

	UNiagaraEmitter* Emitter = Handle->GetInstance().Emitter;
	if (!Emitter)
	{
		UE_LOG(LogTemp, Error, TEXT("[NiagaraEditorLib] AddSpriteRenderer: Emitter is null"));
		return false;
	}

	UNiagaraSpriteRendererProperties* Renderer = NewObject<UNiagaraSpriteRendererProperties>(Emitter, TEXT("SpriteRenderer"));
	Emitter->AddRenderer(Renderer, EmitterData->Version.VersionGuid);

	UE_LOG(LogTemp, Display, TEXT("[NiagaraEditorLib] Added sprite renderer to emitter %d"), EmitterIndex);
	return true;
}

bool UNiagaraEditorLibrary::AddRibbonRenderer(
	UNiagaraSystem* System,
	int32 EmitterIndex)
{
	FNiagaraEmitterHandle* Handle = nullptr;
	FVersionedNiagaraEmitterData* EmitterData = nullptr;
	if (!GetEmitterAndData(System, EmitterIndex, Handle, EmitterData))
	{
		return false;
	}

	UNiagaraEmitter* Emitter = Handle->GetInstance().Emitter;
	if (!Emitter)
	{
		UE_LOG(LogTemp, Error, TEXT("[NiagaraEditorLib] AddRibbonRenderer: Emitter is null"));
		return false;
	}

	UNiagaraRibbonRendererProperties* Renderer = NewObject<UNiagaraRibbonRendererProperties>(Emitter, TEXT("RibbonRenderer"));
	Emitter->AddRenderer(Renderer, EmitterData->Version.VersionGuid);

	UE_LOG(LogTemp, Display, TEXT("[NiagaraEditorLib] Added ribbon renderer to emitter %d"), EmitterIndex);
	return true;
}

bool UNiagaraEditorLibrary::RemoveSpriteRenderer(
	UNiagaraSystem* System,
	int32 EmitterIndex)
{
	FNiagaraEmitterHandle* Handle = nullptr;
	FVersionedNiagaraEmitterData* EmitterData = nullptr;
	if (!GetEmitterAndData(System, EmitterIndex, Handle, EmitterData))
	{
		return false;
	}

	UNiagaraEmitter* Emitter = Handle->GetInstance().Emitter;
	if (!Emitter)
	{
		return false;
	}

	bool bRemoved = false;
	TArray<UNiagaraRendererProperties*> ToRemove;
	for (UNiagaraRendererProperties* Renderer : EmitterData->GetRenderers())
	{
		if (Cast<UNiagaraSpriteRendererProperties>(Renderer))
		{
			ToRemove.Add(Renderer);
		}
	}

	for (UNiagaraRendererProperties* Renderer : ToRemove)
	{
		Emitter->RemoveRenderer(Renderer, EmitterData->Version.VersionGuid);
		bRemoved = true;
	}

	if (bRemoved)
	{
		UE_LOG(LogTemp, Display, TEXT("[NiagaraEditorLib] Removed sprite renderer(s) from emitter %d"), EmitterIndex);
	}
	return bRemoved;
}

bool UNiagaraEditorLibrary::SetSpriteRendererAlignment(
	UNiagaraSystem* System,
	int32 EmitterIndex,
	const FString& Alignment)
{
	FNiagaraEmitterHandle* Handle = nullptr;
	FVersionedNiagaraEmitterData* EmitterData = nullptr;
	if (!GetEmitterAndData(System, EmitterIndex, Handle, EmitterData))
	{
		return false;
	}

	ENiagaraSpriteAlignment AlignmentEnum;
	if (Alignment.Equals(TEXT("Unaligned"), ESearchCase::IgnoreCase))
	{
		AlignmentEnum = ENiagaraSpriteAlignment::Unaligned;
	}
	else if (Alignment.Equals(TEXT("VelocityAligned"), ESearchCase::IgnoreCase))
	{
		AlignmentEnum = ENiagaraSpriteAlignment::VelocityAligned;
	}
	else if (Alignment.Equals(TEXT("CustomAlignment"), ESearchCase::IgnoreCase))
	{
		AlignmentEnum = ENiagaraSpriteAlignment::CustomAlignment;
	}
	else if (Alignment.Equals(TEXT("Automatic"), ESearchCase::IgnoreCase))
	{
		AlignmentEnum = ENiagaraSpriteAlignment::Automatic;
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[NiagaraEditorLib] Unknown alignment: %s. Use Unaligned, VelocityAligned, CustomAlignment, or Automatic."), *Alignment);
		return false;
	}

	const TArray<UNiagaraRendererProperties*>& Renderers = EmitterData->GetRenderers();
	for (UNiagaraRendererProperties* Renderer : Renderers)
	{
		UNiagaraSpriteRendererProperties* SpriteRenderer = Cast<UNiagaraSpriteRendererProperties>(Renderer);
		if (SpriteRenderer)
		{
			SpriteRenderer->Alignment = AlignmentEnum;
			UE_LOG(LogTemp, Display, TEXT("[NiagaraEditorLib] Set sprite alignment to %s"), *Alignment);
			return true;
		}
	}

	UE_LOG(LogTemp, Error, TEXT("[NiagaraEditorLib] No sprite renderer found on emitter %d"), EmitterIndex);
	return false;
}

bool UNiagaraEditorLibrary::SetSpriteRendererFacingMode(
	UNiagaraSystem* System,
	int32 EmitterIndex,
	const FString& FacingMode)
{
	FNiagaraEmitterHandle* Handle = nullptr;
	FVersionedNiagaraEmitterData* EmitterData = nullptr;
	if (!GetEmitterAndData(System, EmitterIndex, Handle, EmitterData))
	{
		return false;
	}

	ENiagaraSpriteFacingMode FacingEnum;
	if (FacingMode.Equals(TEXT("FaceCamera"), ESearchCase::IgnoreCase))
	{
		FacingEnum = ENiagaraSpriteFacingMode::FaceCamera;
	}
	else if (FacingMode.Equals(TEXT("FaceCameraPlane"), ESearchCase::IgnoreCase))
	{
		FacingEnum = ENiagaraSpriteFacingMode::FaceCameraPlane;
	}
	else if (FacingMode.Equals(TEXT("CustomFacingVector"), ESearchCase::IgnoreCase))
	{
		FacingEnum = ENiagaraSpriteFacingMode::CustomFacingVector;
	}
	else if (FacingMode.Equals(TEXT("FaceCameraPosition"), ESearchCase::IgnoreCase))
	{
		FacingEnum = ENiagaraSpriteFacingMode::FaceCameraPosition;
	}
	else if (FacingMode.Equals(TEXT("FaceCameraDistanceBlend"), ESearchCase::IgnoreCase))
	{
		FacingEnum = ENiagaraSpriteFacingMode::FaceCameraDistanceBlend;
	}
	else if (FacingMode.Equals(TEXT("Automatic"), ESearchCase::IgnoreCase))
	{
		FacingEnum = ENiagaraSpriteFacingMode::Automatic;
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[NiagaraEditorLib] Unknown facing mode: %s. Use FaceCamera, FaceCameraPlane, CustomFacingVector, FaceCameraPosition, FaceCameraDistanceBlend, or Automatic."), *FacingMode);
		return false;
	}

	const TArray<UNiagaraRendererProperties*>& Renderers = EmitterData->GetRenderers();
	for (UNiagaraRendererProperties* Renderer : Renderers)
	{
		UNiagaraSpriteRendererProperties* SpriteRenderer = Cast<UNiagaraSpriteRendererProperties>(Renderer);
		if (SpriteRenderer)
		{
			SpriteRenderer->FacingMode = FacingEnum;
			UE_LOG(LogTemp, Display, TEXT("[NiagaraEditorLib] Set sprite facing mode to %s"), *FacingMode);
			return true;
		}
	}

	UE_LOG(LogTemp, Error, TEXT("[NiagaraEditorLib] No sprite renderer found on emitter %d"), EmitterIndex);
	return false;
}

bool UNiagaraEditorLibrary::SetRibbonRendererMaterial(
	UNiagaraSystem* System,
	int32 EmitterIndex,
	const FString& MaterialPath)
{
	FNiagaraEmitterHandle* Handle = nullptr;
	FVersionedNiagaraEmitterData* EmitterData = nullptr;
	if (!GetEmitterAndData(System, EmitterIndex, Handle, EmitterData))
	{
		return false;
	}

	UMaterialInterface* Material = Cast<UMaterialInterface>(
		FSoftObjectPath(MaterialPath).TryLoad());
	if (!Material)
	{
		UE_LOG(LogTemp, Error, TEXT("[NiagaraEditorLib] Failed to load material: %s"), *MaterialPath);
		return false;
	}

	const TArray<UNiagaraRendererProperties*>& Renderers = EmitterData->GetRenderers();
	for (UNiagaraRendererProperties* Renderer : Renderers)
	{
		UNiagaraRibbonRendererProperties* RibbonRenderer = Cast<UNiagaraRibbonRendererProperties>(Renderer);
		if (RibbonRenderer)
		{
			RibbonRenderer->Material = Material;
			UE_LOG(LogTemp, Display, TEXT("[NiagaraEditorLib] Set ribbon material to %s"), *MaterialPath);
			return true;
		}
	}

	UE_LOG(LogTemp, Error, TEXT("[NiagaraEditorLib] No ribbon renderer found on emitter %d"), EmitterIndex);
	return false;
}

bool UNiagaraEditorLibrary::SetRibbonRendererTessellation(
	UNiagaraSystem* System,
	int32 EmitterIndex,
	float CurveTension)
{
	FNiagaraEmitterHandle* Handle = nullptr;
	FVersionedNiagaraEmitterData* EmitterData = nullptr;
	if (!GetEmitterAndData(System, EmitterIndex, Handle, EmitterData))
	{
		return false;
	}

	CurveTension = FMath::Clamp(CurveTension, 0.0f, 0.99f);

	const TArray<UNiagaraRendererProperties*>& Renderers = EmitterData->GetRenderers();
	for (UNiagaraRendererProperties* Renderer : Renderers)
	{
		UNiagaraRibbonRendererProperties* RibbonRenderer = Cast<UNiagaraRibbonRendererProperties>(Renderer);
		if (RibbonRenderer)
		{
			RibbonRenderer->TessellationMode = ENiagaraRibbonTessellationMode::Custom;
			RibbonRenderer->CurveTension = CurveTension;
			UE_LOG(LogTemp, Display, TEXT("[NiagaraEditorLib] Set ribbon tessellation: Custom mode, CurveTension=%f"), CurveTension);
			return true;
		}
	}

	UE_LOG(LogTemp, Error, TEXT("[NiagaraEditorLib] No ribbon renderer found on emitter %d"), EmitterIndex);
	return false;
}

// =============================================================================
// INTROSPECTION
// =============================================================================

TArray<FString> UNiagaraEditorLibrary::GetModuleParameters(
	UNiagaraSystem* System,
	int32 EmitterIndex,
	const FString& ModuleNodeName)
{
	TArray<FString> Result;

	FNiagaraEmitterHandle* Handle = nullptr;
	FVersionedNiagaraEmitterData* EmitterData = nullptr;
	if (!GetEmitterAndData(System, EmitterIndex, Handle, EmitterData))
	{
		return Result;
	}

	UNiagaraNodeFunctionCall* ModuleNode = FindModuleNode(EmitterData, ModuleNodeName);
	if (!ModuleNode)
	{
		UE_LOG(LogTemp, Error, TEXT("[NiagaraEditorLib] GetModuleParameters: Module '%s' not found"), *ModuleNodeName);
		return Result;
	}

	// Use exported GetCalledGraph() to introspect module parameters
	// instead of unexported GetStackFunctionInputs
	UNiagaraGraph* CalledGraph = ModuleNode->GetCalledGraph();
	if (!CalledGraph)
	{
		return Result;
	}

	for (UEdGraphNode* Node : CalledGraph->Nodes)
	{
		UNiagaraNodeInput* InputNode = Cast<UNiagaraNodeInput>(Node);
		if (InputNode && InputNode->Usage == ENiagaraInputNodeUsage::Parameter)
		{
			FString TypeName = InputNode->Input.GetType().GetName();
			Result.Add(FString::Printf(TEXT("%s|%s"), *InputNode->Input.GetName().ToString(), *TypeName));
		}
	}

	return Result;
}

TArray<FString> UNiagaraEditorLibrary::ListRapidIterationParameters(
	UNiagaraSystem* System,
	int32 EmitterIndex)
{
	TArray<FString> Result;

	FNiagaraEmitterHandle* Handle = nullptr;
	FVersionedNiagaraEmitterData* EmitterData = nullptr;
	if (!GetEmitterAndData(System, EmitterIndex, Handle, EmitterData))
	{
		return Result;
	}

	// Check all 4 stage scripts
	struct FScriptInfo
	{
		const TCHAR* Name;
		UNiagaraScript* Script;
	};

	TArray<FScriptInfo> Scripts;
	Scripts.Add({TEXT("EmitterSpawn"), EmitterData->EmitterSpawnScriptProps.Script});
	Scripts.Add({TEXT("EmitterUpdate"), EmitterData->EmitterUpdateScriptProps.Script});
	Scripts.Add({TEXT("ParticleSpawn"), EmitterData->SpawnScriptProps.Script});
	Scripts.Add({TEXT("ParticleUpdate"), EmitterData->UpdateScriptProps.Script});

	for (const FScriptInfo& Info : Scripts)
	{
		if (!Info.Script)
		{
			continue;
		}

		auto Params = Info.Script->RapidIterationParameters.ReadParameterVariables();
		for (const auto& Var : Params)
		{
			FString TypeName = Var.GetType().GetName();

			// Read value from the parameter store using the variable as key
			FString ValueStr = TEXT("?");
			FNiagaraVariable TempVar(Var.GetType(), Var.GetName());
			const int32 ParamIdx = Info.Script->RapidIterationParameters.IndexOf(TempVar);
			if (ParamIdx != INDEX_NONE)
			{
				const uint8* DataPtr = Info.Script->RapidIterationParameters.GetParameterData(TempVar);
				if (DataPtr)
				{
					if (Var.GetType() == FNiagaraTypeDefinition::GetFloatDef())
					{
						float Val;
						FMemory::Memcpy(&Val, DataPtr, sizeof(float));
						ValueStr = FString::SanitizeFloat(Val);
					}
					else if (Var.GetType() == FNiagaraTypeDefinition::GetIntDef())
					{
						int32 Val;
						FMemory::Memcpy(&Val, DataPtr, sizeof(int32));
						ValueStr = FString::FromInt(Val);
					}
					else if (Var.GetType() == FNiagaraTypeDefinition::GetVec3Def())
					{
						FVector3f V;
						FMemory::Memcpy(&V, DataPtr, sizeof(FVector3f));
						ValueStr = FString::Printf(TEXT("(%f,%f,%f)"), V.X, V.Y, V.Z);
					}
					else if (Var.GetType() == FNiagaraTypeDefinition::GetVec2Def())
					{
						FVector2f V;
						FMemory::Memcpy(&V, DataPtr, sizeof(FVector2f));
						ValueStr = FString::Printf(TEXT("(%f,%f)"), V.X, V.Y);
					}
					else if (Var.GetType() == FNiagaraTypeDefinition::GetColorDef())
					{
						FLinearColor C;
						FMemory::Memcpy(&C, DataPtr, sizeof(FLinearColor));
						ValueStr = FString::Printf(TEXT("(%f,%f,%f,%f)"), C.R, C.G, C.B, C.A);
					}
					else if (Var.GetType() == FNiagaraTypeDefinition::GetPositionDef())
					{
						FVector3f V;
						FMemory::Memcpy(&V, DataPtr, sizeof(FVector3f));
						ValueStr = FString::Printf(TEXT("(%f,%f,%f)"), V.X, V.Y, V.Z);
					}
				}
			}

			Result.Add(FString::Printf(TEXT("%s|%s|%s|%s"), Info.Name, *Var.GetName().ToString(), *TypeName, *ValueStr));
		}
	}

	return Result;
}

TArray<FString> UNiagaraEditorLibrary::GetRendererProperties(
	UNiagaraSystem* System,
	int32 EmitterIndex)
{
	TArray<FString> Result;

	FNiagaraEmitterHandle* Handle = nullptr;
	FVersionedNiagaraEmitterData* EmitterData = nullptr;
	if (!GetEmitterAndData(System, EmitterIndex, Handle, EmitterData))
	{
		return Result;
	}

	int32 RendererIdx = 0;
	for (UNiagaraRendererProperties* Renderer : EmitterData->GetRenderers())
	{
		if (UNiagaraSpriteRendererProperties* Sprite = Cast<UNiagaraSpriteRendererProperties>(Renderer))
		{
			FString MatName = Sprite->Material ? Sprite->Material->GetPathName() : TEXT("None");
			Result.Add(FString::Printf(TEXT("Sprite[%d]|Material=%s"), RendererIdx, *MatName));
			Result.Add(FString::Printf(TEXT("Sprite[%d]|Alignment=%d"), RendererIdx, (int32)Sprite->Alignment));
			Result.Add(FString::Printf(TEXT("Sprite[%d]|FacingMode=%d"), RendererIdx, (int32)Sprite->FacingMode));
		}
		else if (UNiagaraRibbonRendererProperties* Ribbon = Cast<UNiagaraRibbonRendererProperties>(Renderer))
		{
			FString MatName = Ribbon->Material ? Ribbon->Material->GetPathName() : TEXT("None");
			Result.Add(FString::Printf(TEXT("Ribbon[%d]|Material=%s"), RendererIdx, *MatName));
			Result.Add(FString::Printf(TEXT("Ribbon[%d]|TessellationMode=%d"), RendererIdx, (int32)Ribbon->TessellationMode));
			Result.Add(FString::Printf(TEXT("Ribbon[%d]|CurveTension=%f"), RendererIdx, Ribbon->CurveTension));
			Result.Add(FString::Printf(TEXT("Ribbon[%d]|TessellationFactor=%d"), RendererIdx, Ribbon->TessellationFactor));
			Result.Add(FString::Printf(TEXT("Ribbon[%d]|FacingMode=%d"), RendererIdx, (int32)Ribbon->FacingMode));
			Result.Add(FString::Printf(TEXT("Ribbon[%d]|Shape=%d"), RendererIdx, (int32)Ribbon->Shape));
			Result.Add(FString::Printf(TEXT("Ribbon[%d]|DrawDirection=%d"), RendererIdx, (int32)Ribbon->DrawDirection));
		}
		else
		{
			Result.Add(FString::Printf(TEXT("Unknown[%d]|Class=%s"), RendererIdx, *Renderer->GetClass()->GetName()));
		}
		RendererIdx++;
	}

	return Result;
}

TArray<FString> UNiagaraEditorLibrary::GetModuleInputs(
	UNiagaraSystem* System,
	int32 EmitterIndex,
	const FString& ModuleNodeName)
{
	TArray<FString> Result;

	FNiagaraEmitterHandle* Handle = nullptr;
	FVersionedNiagaraEmitterData* EmitterData = nullptr;
	if (!GetEmitterAndData(System, EmitterIndex, Handle, EmitterData))
	{
		return Result;
	}

	UNiagaraNodeFunctionCall* ModuleNode = FindModuleNode(EmitterData, ModuleNodeName);
	if (!ModuleNode)
	{
		UE_LOG(LogTemp, Error, TEXT("[NiagaraEditorLib] GetModuleInputs: Module '%s' not found"), *ModuleNodeName);
		return Result;
	}

	// Walk the module's called graph to find ParameterMapGet nodes
	// These nodes have output pins whose names are the actual parameter input names
	UNiagaraGraph* CalledGraph = ModuleNode->GetCalledGraph();
	if (!CalledGraph)
	{
		return Result;
	}

	for (UEdGraphNode* Node : CalledGraph->Nodes)
	{
		// Identify ParameterMapGet nodes by class name (header is private)
		if (Node->GetClass()->GetName().Contains(TEXT("ParameterMapGet")))
		{
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin->Direction == EGPD_Output)
				{
					FString PinName = Pin->PinName.ToString();
					// Filter for Module.* parameters (these are the module's inputs)
					if (PinName.StartsWith(TEXT("Module.")))
					{
						FString ParamName = PinName.RightChop(7); // Remove "Module."
						FString PinType = Pin->PinType.PinCategory.ToString();

						// Try to get a more useful type from the Niagara type info
						if (Pin->PinType.PinSubCategoryObject.IsValid())
						{
							PinType = Pin->PinType.PinSubCategoryObject->GetName();
						}

						Result.Add(FString::Printf(TEXT("%s|%s"), *ParamName, *PinType));
					}
				}
			}
		}
	}

	return Result;
}
