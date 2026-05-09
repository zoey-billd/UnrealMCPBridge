// Copyright Omar Abdelwahed 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Define WITH_PYTHON if not already defined
#ifndef WITH_PYTHON
#define WITH_PYTHON 0
#endif

#include "JsonGlobals.h"

/**
 * Bridge for executing Python commands within Unreal Engine
 */
class FPythonBridge
{
public:

    inline static bool bIsInitialized = false;

    /** Initialize the Python bridge */
    static void Initialize();

    /** Shut down the Python bridge */
    static void Shutdown();

    /**
     * Execute a command through Python
     * @param Command - The command to execute
     * @param Params - Parameters for the command
     * @return JSON string with the result
     */
    static FString ExecuteCommand(const FString& Command, TSharedPtr<FJsonObject> Params);

private:
    /** Execute Python script and return the result */
    static FString ExecutePythonScript(const FString& PythonScript);

    /** Convert params to a Python dictionary string */
    static FString ParamsToPythonDict(TSharedPtr<FJsonObject> Params);

    static FString LoadFileToString(FString AbsolutePath);

};
