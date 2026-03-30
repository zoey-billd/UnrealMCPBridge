// Copyright Omar Abdelwahed 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "TraceAnalysisLibrary.generated.h"

/**
 * Blueprint function library for analyzing Unreal Insights .utrace files.
 * Uses the TraceServices module to parse trace data and extract timing profiler
 * information — function names, durations, call counts, thread breakdown.
 *
 * Auto-reflects to Python via:
 *   import unreal
 *   result = unreal.TraceAnalysisLibrary.analyze_trace(path, top_n)
 */
UCLASS()
class UTraceAnalysisLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Analyze a .utrace file and return a JSON summary of the top most expensive timing scopes.
	 * Uses TraceServices to process the trace with the built-in CPU profiler analyzer,
	 * then aggregates timing data across all threads and the full trace duration.
	 *
	 * @param TracePath Absolute path to the .utrace file
	 * @param TopN Number of top scopes to return (sorted by total inclusive time, descending). 0 = all.
	 * @return JSON string with trace summary, or a string starting with "Error:" on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "MCP|TraceAnalysis")
	static FString AnalyzeTrace(const FString& TracePath, int32 TopN = 50);

	/**
	 * Analyze a .utrace file and return only the frames that exceeded a time budget.
	 * For each spike frame, includes the top timing scopes contributing to that frame.
	 *
	 * @param TracePath Absolute path to the .utrace file
	 * @param BudgetMs Frame time budget in milliseconds (e.g., 16.67 for 60fps, 33.33 for 30fps)
	 * @param MaxFrames Maximum number of spike frames to return. 0 = all.
	 * @param TopScopesPerFrame Number of top scopes to include per spike frame
	 * @return JSON string with spike frame data, or a string starting with "Error:" on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "MCP|TraceAnalysis")
	static FString GetTraceSpikes(const FString& TracePath, float BudgetMs = 33.33f, int32 MaxFrames = 20, int32 TopScopesPerFrame = 10);

	/**
	 * Get thread information from a .utrace file.
	 * Returns a JSON array of threads with their names and IDs.
	 *
	 * @param TracePath Absolute path to the .utrace file
	 * @return JSON string with thread info, or a string starting with "Error:" on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "MCP|TraceAnalysis")
	static FString GetTraceThreads(const FString& TracePath);

	/**
	 * Get frame timing summary from a .utrace file.
	 * Returns frame count, average/min/max frame times, and FPS statistics.
	 *
	 * @param TracePath Absolute path to the .utrace file
	 * @return JSON string with frame summary, or a string starting with "Error:" on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "MCP|TraceAnalysis")
	static FString GetTraceFrameSummary(const FString& TracePath);

};
