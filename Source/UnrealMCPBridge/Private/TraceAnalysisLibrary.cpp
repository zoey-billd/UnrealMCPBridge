// Copyright Omar Abdelwahed 2026. All Rights Reserved.

#include "TraceAnalysisLibrary.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

// TraceServices — high-level analysis API
#include "TraceServices/ITraceServicesModule.h"
#include "TraceServices/AnalysisService.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "TraceServices/Model/TimingProfiler.h"
#include "TraceServices/Model/Threads.h"
#include "TraceServices/Model/Frames.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

static TSharedPtr<const TraceServices::IAnalysisSession> RunAnalysis(
	const FString& TracePath, FString& OutError)
{
	if (!FPaths::FileExists(TracePath))
	{
		OutError = FString::Printf(TEXT("Trace file not found: %s"), *TracePath);
		return nullptr;
	}

	ITraceServicesModule* TraceServicesModule = FModuleManager::GetModulePtr<ITraceServicesModule>("TraceServices");
	if (!TraceServicesModule)
	{
		// Try loading it
		TraceServicesModule = FModuleManager::LoadModulePtr<ITraceServicesModule>("TraceServices");
		if (!TraceServicesModule)
		{
			OutError = TEXT("Failed to load TraceServices module");
			return nullptr;
		}
	}

	// Use GetAnalysisService() — the editor already has a singleton via Insights.
	// CreateAnalysisService() asserts if one already exists.
	TSharedPtr<TraceServices::IAnalysisService> AnalysisService = TraceServicesModule->GetAnalysisService();
	if (!AnalysisService.IsValid())
	{
		// Fallback: try creating if none exists (e.g., standalone/commandlet)
		AnalysisService = TraceServicesModule->CreateAnalysisService();
		if (!AnalysisService.IsValid())
		{
			OutError = TEXT("Failed to get or create analysis service");
			return nullptr;
		}
	}

	// Synchronous analysis — processes entire trace file and waits for completion
	TSharedPtr<const TraceServices::IAnalysisSession> Session = AnalysisService->Analyze(*TracePath);
	if (!Session.IsValid())
	{
		OutError = FString::Printf(TEXT("Failed to analyze trace: %s"), *TracePath);
		return nullptr;
	}

	return Session;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FString UTraceAnalysisLibrary::AnalyzeTrace(const FString& TracePath, int32 TopN)
{
	FString Error;
	TSharedPtr<const TraceServices::IAnalysisSession> Session = RunAnalysis(TracePath, Error);
	if (!Session.IsValid())
	{
		return FString::Printf(TEXT("Error: %s"), *Error);
	}

	TraceServices::FAnalysisSessionReadScope ReadScope(*Session);

	// Get timing profiler provider
	const TraceServices::ITimingProfilerProvider* TimingProvider =
		TraceServices::ReadTimingProfilerProvider(*Session);
	if (!TimingProvider)
	{
		return TEXT("Error: No timing profiler data in trace");
	}

	// Get trace duration
	double TraceDuration = Session->GetDurationSeconds();

	// Build JSON output
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("trace_file"), TracePath);
	Root->SetNumberField(TEXT("duration_seconds"), TraceDuration);

	// Thread info
	const TraceServices::IThreadProvider& ThreadProvider = TraceServices::ReadThreadProvider(*Session);
	TArray<TSharedPtr<FJsonValue>> ThreadsArray;
	ThreadProvider.EnumerateThreads([&ThreadsArray](const TraceServices::FThreadInfo& Thread)
	{
		TSharedRef<FJsonObject> ThreadObj = MakeShared<FJsonObject>();
		ThreadObj->SetNumberField(TEXT("id"), Thread.Id);
		ThreadObj->SetStringField(TEXT("name"), Thread.Name ? Thread.Name : TEXT("Unknown"));
		if (Thread.GroupName && Thread.GroupName[0] != '\0')
		{
			ThreadObj->SetStringField(TEXT("group"), Thread.GroupName);
		}
		ThreadsArray.Add(MakeShared<FJsonValueObject>(ThreadObj));
	});
	Root->SetArrayField(TEXT("threads"), ThreadsArray);

	// Frame info
	const TraceServices::IFrameProvider& FrameProvider = TraceServices::ReadFrameProvider(*Session);
	uint64 GameFrameCount = FrameProvider.GetFrameCount(ETraceFrameType::TraceFrameType_Game);
	uint64 RenderFrameCount = FrameProvider.GetFrameCount(ETraceFrameType::TraceFrameType_Rendering);

	TSharedRef<FJsonObject> FrameInfo = MakeShared<FJsonObject>();
	FrameInfo->SetNumberField(TEXT("game_frame_count"), static_cast<double>(GameFrameCount));
	FrameInfo->SetNumberField(TEXT("render_frame_count"), static_cast<double>(RenderFrameCount));

	if (GameFrameCount > 0)
	{
		double TotalFrameTime = 0.0;
		double MinFrameTime = DBL_MAX;
		double MaxFrameTime = 0.0;
		uint64 ValidFrames = 0;
		FrameProvider.EnumerateFrames(ETraceFrameType::TraceFrameType_Game, 0, GameFrameCount,
			[&](const TraceServices::FFrame& Frame)
		{
			double FrameTime = Frame.EndTime - Frame.StartTime;
			if (FMath::IsFinite(FrameTime) && FrameTime > 0.0 && FrameTime < 10.0)
			{
				TotalFrameTime += FrameTime;
				MinFrameTime = FMath::Min(MinFrameTime, FrameTime);
				MaxFrameTime = FMath::Max(MaxFrameTime, FrameTime);
				ValidFrames++;
			}
		});

		if (ValidFrames > 0)
		{
			double AvgFrameTime = TotalFrameTime / ValidFrames;
			FrameInfo->SetNumberField(TEXT("valid_frame_count"), static_cast<double>(ValidFrames));
			FrameInfo->SetNumberField(TEXT("avg_frame_time_ms"), AvgFrameTime * 1000.0);
			FrameInfo->SetNumberField(TEXT("min_frame_time_ms"), MinFrameTime * 1000.0);
			FrameInfo->SetNumberField(TEXT("max_frame_time_ms"), MaxFrameTime * 1000.0);
			FrameInfo->SetNumberField(TEXT("avg_fps"), 1.0 / AvgFrameTime);
		}
	}
	Root->SetObjectField(TEXT("frames"), FrameInfo);

	// Build aggregation manually by enumerating timelines
	// CreateAggregation returns nullptr from the shared analysis service's provider
	struct FScopeStats
	{
		const TCHAR* Name = nullptr;
		const TCHAR* File = nullptr;
		uint32 Line = 0;
		uint64 Count = 0;
		double TotalInclusiveTime = 0.0;
		double MaxInclusiveTime = 0.0;
		double TotalExclusiveTime = 0.0;
		double MaxExclusiveTime = 0.0;
	};

	TMap<uint32, FScopeStats> ScopeStatsMap;

	// Cache timer info
	TMap<uint32, const TraceServices::FTimingProfilerTimer*> TimerCache;
	TimingProvider->ReadTimers([&TimerCache](const TraceServices::ITimingProfilerTimerReader& TimerReader)
	{
		uint32 Count = TimerReader.GetTimerCount();
		for (uint32 i = 0; i < Count; i++)
		{
			const TraceServices::FTimingProfilerTimer* Timer = TimerReader.GetTimer(i);
			if (Timer)
			{
				TimerCache.Add(i, Timer);
			}
		}
	});

	Root->SetNumberField(TEXT("timer_count"), TimerCache.Num());
	uint32 TimelineCount = TimingProvider->GetTimelineCount();
	Root->SetNumberField(TEXT("timeline_count"), TimelineCount);

	// Enumerate all timelines and collect scope stats
	TimingProvider->EnumerateTimelines([&](const TraceServices::ITimingProfilerProvider::Timeline& Timeline)
	{
		Timeline.EnumerateEvents(0.0, TraceDuration,
			[&](double StartTime, double EndTime, uint32 Depth, const TraceServices::FTimingProfilerEvent& Event) -> TraceServices::EEventEnumerate
		{
			double InclusiveTime = EndTime - StartTime;
			if (!FMath::IsFinite(InclusiveTime) || InclusiveTime < 0.0)
			{
				return TraceServices::EEventEnumerate::Continue;
			}

			FScopeStats& Stats = ScopeStatsMap.FindOrAdd(Event.TimerIndex);
			if (Stats.Name == nullptr)
			{
				const TraceServices::FTimingProfilerTimer** TimerPtr = TimerCache.Find(Event.TimerIndex);
				if (TimerPtr && *TimerPtr)
				{
					Stats.Name = (*TimerPtr)->Name;
					Stats.File = (*TimerPtr)->File;
					Stats.Line = (*TimerPtr)->Line;
				}
			}
			Stats.Count++;
			Stats.TotalInclusiveTime += InclusiveTime;
			Stats.MaxInclusiveTime = FMath::Max(Stats.MaxInclusiveTime, InclusiveTime);

			return TraceServices::EEventEnumerate::Continue;
		});
	});

	// Sort by total inclusive time descending
	TArray<TPair<uint32, FScopeStats>> SortedScopes;
	for (auto& KV : ScopeStatsMap)
	{
		SortedScopes.Add(TPair<uint32, FScopeStats>(KV.Key, KV.Value));
	}
	SortedScopes.Sort([](const TPair<uint32, FScopeStats>& A, const TPair<uint32, FScopeStats>& B)
	{
		return A.Value.TotalInclusiveTime > B.Value.TotalInclusiveTime;
	});

	int32 Limit = (TopN > 0) ? FMath::Min(TopN, SortedScopes.Num()) : SortedScopes.Num();

	TArray<TSharedPtr<FJsonValue>> ScopesArray;
	for (int32 i = 0; i < Limit; i++)
	{
		const FScopeStats& Stats = SortedScopes[i].Value;
		TSharedRef<FJsonObject> ScopeObj = MakeShared<FJsonObject>();
		ScopeObj->SetStringField(TEXT("name"), Stats.Name ? Stats.Name : TEXT("Unknown"));
		if (Stats.File)
		{
			ScopeObj->SetStringField(TEXT("file"), Stats.File);
			ScopeObj->SetNumberField(TEXT("line"), Stats.Line);
		}
		ScopeObj->SetNumberField(TEXT("count"), static_cast<double>(Stats.Count));
		ScopeObj->SetNumberField(TEXT("total_inclusive_ms"), Stats.TotalInclusiveTime * 1000.0);
		ScopeObj->SetNumberField(TEXT("avg_inclusive_ms"), (Stats.Count > 0) ? (Stats.TotalInclusiveTime / Stats.Count) * 1000.0 : 0.0);
		ScopeObj->SetNumberField(TEXT("max_inclusive_ms"), Stats.MaxInclusiveTime * 1000.0);
		ScopesArray.Add(MakeShared<FJsonValueObject>(ScopeObj));
	}
	Root->SetArrayField(TEXT("top_scopes"), ScopesArray);
	Root->SetNumberField(TEXT("scope_count"), ScopesArray.Num());
	Root->SetNumberField(TEXT("total_unique_scopes"), ScopeStatsMap.Num());

	// Serialize to string
	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(Root, Writer);
	return OutputString;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FString UTraceAnalysisLibrary::GetTraceSpikes(const FString& TracePath, float BudgetMs, int32 MaxFrames, int32 TopScopesPerFrame)
{
	FString Error;
	TSharedPtr<const TraceServices::IAnalysisSession> Session = RunAnalysis(TracePath, Error);
	if (!Session.IsValid())
	{
		return FString::Printf(TEXT("Error: %s"), *Error);
	}

	TraceServices::FAnalysisSessionReadScope ReadScope(*Session);

	const TraceServices::ITimingProfilerProvider* TimingProvider =
		TraceServices::ReadTimingProfilerProvider(*Session);
	if (!TimingProvider)
	{
		return TEXT("Error: No timing profiler data in trace");
	}

	const TraceServices::IFrameProvider& FrameProvider = TraceServices::ReadFrameProvider(*Session);
	uint64 GameFrameCount = FrameProvider.GetFrameCount(ETraceFrameType::TraceFrameType_Game);

	double BudgetSeconds = BudgetMs / 1000.0;

	// Find frames that exceed budget
	struct FSpikeFrame
	{
		uint64 Index;
		double StartTime;
		double EndTime;
		double Duration;
	};
	TArray<FSpikeFrame> SpikeFrames;

	FrameProvider.EnumerateFrames(ETraceFrameType::TraceFrameType_Game, 0, GameFrameCount,
		[&](const TraceServices::FFrame& Frame)
	{
		double Duration = Frame.EndTime - Frame.StartTime;
		if (FMath::IsFinite(Duration) && Duration > BudgetSeconds && Duration < 10.0)
		{
			SpikeFrames.Add({ Frame.Index, Frame.StartTime, Frame.EndTime, Duration });
		}
	});

	// Sort by duration descending (worst spikes first)
	SpikeFrames.Sort([](const FSpikeFrame& A, const FSpikeFrame& B)
	{
		return A.Duration > B.Duration;
	});

	if (MaxFrames > 0 && SpikeFrames.Num() > MaxFrames)
	{
		SpikeFrames.SetNum(MaxFrames);
	}

	// Build JSON
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("trace_file"), TracePath);
	Root->SetNumberField(TEXT("budget_ms"), BudgetMs);
	Root->SetNumberField(TEXT("total_frames"), static_cast<double>(GameFrameCount));
	Root->SetNumberField(TEXT("spike_count"), SpikeFrames.Num());

	TArray<TSharedPtr<FJsonValue>> SpikesArray;
	for (const FSpikeFrame& Spike : SpikeFrames)
	{
		TSharedRef<FJsonObject> SpikeObj = MakeShared<FJsonObject>();
		SpikeObj->SetNumberField(TEXT("frame_index"), static_cast<double>(Spike.Index));
		SpikeObj->SetNumberField(TEXT("duration_ms"), Spike.Duration * 1000.0);
		SpikeObj->SetNumberField(TEXT("start_time"), Spike.StartTime);
		SpikeObj->SetNumberField(TEXT("end_time"), Spike.EndTime);

		// Aggregate this frame's timing scopes via manual timeline enumeration
		struct FFrameScopeStats
		{
			const TCHAR* Name = nullptr;
			uint64 Count = 0;
			double TotalInclusiveTime = 0.0;
		};
		TMap<uint32, FFrameScopeStats> FrameScopeMap;

		TimingProvider->EnumerateTimelines([&](const TraceServices::ITimingProfilerProvider::Timeline& Timeline)
		{
			Timeline.EnumerateEvents(Spike.StartTime, Spike.EndTime,
				[&](double StartTime, double EndTime, uint32 Depth, const TraceServices::FTimingProfilerEvent& Event) -> TraceServices::EEventEnumerate
			{
				double IncTime = EndTime - StartTime;
				if (!FMath::IsFinite(IncTime) || IncTime < 0.0)
				{
					return TraceServices::EEventEnumerate::Continue;
				}
				FFrameScopeStats& Stats = FrameScopeMap.FindOrAdd(Event.TimerIndex);
				if (Stats.Name == nullptr)
				{
					TimingProvider->ReadTimers([&](const TraceServices::ITimingProfilerTimerReader& TimerReader)
					{
						const TraceServices::FTimingProfilerTimer* Timer = TimerReader.GetTimer(Event.TimerIndex);
						if (Timer)
						{
							Stats.Name = Timer->Name;
						}
					});
				}
				Stats.Count++;
				Stats.TotalInclusiveTime += IncTime;
				return TraceServices::EEventEnumerate::Continue;
			});
		});

		// Sort and take top N
		TArray<TPair<uint32, FFrameScopeStats>> SortedFrameScopes;
		for (auto& KV : FrameScopeMap)
		{
			SortedFrameScopes.Add(TPair<uint32, FFrameScopeStats>(KV.Key, KV.Value));
		}
		SortedFrameScopes.Sort([](const auto& A, const auto& B)
		{
			return A.Value.TotalInclusiveTime > B.Value.TotalInclusiveTime;
		});

		int32 ScopeLimit = FMath::Min(TopScopesPerFrame, SortedFrameScopes.Num());
		TArray<TSharedPtr<FJsonValue>> FrameScopes;
		for (int32 si = 0; si < ScopeLimit; si++)
		{
			const FFrameScopeStats& Stats = SortedFrameScopes[si].Value;
			TSharedRef<FJsonObject> ScopeObj = MakeShared<FJsonObject>();
			ScopeObj->SetStringField(TEXT("name"), Stats.Name ? Stats.Name : TEXT("Unknown"));
			ScopeObj->SetNumberField(TEXT("count"), static_cast<double>(Stats.Count));
			ScopeObj->SetNumberField(TEXT("inclusive_ms"), Stats.TotalInclusiveTime * 1000.0);
			FrameScopes.Add(MakeShared<FJsonValueObject>(ScopeObj));
		}
		SpikeObj->SetArrayField(TEXT("top_scopes"), FrameScopes);

		SpikesArray.Add(MakeShared<FJsonValueObject>(SpikeObj));
	}
	Root->SetArrayField(TEXT("spikes"), SpikesArray);

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(Root, Writer);
	return OutputString;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FString UTraceAnalysisLibrary::GetTraceThreads(const FString& TracePath)
{
	FString Error;
	TSharedPtr<const TraceServices::IAnalysisSession> Session = RunAnalysis(TracePath, Error);
	if (!Session.IsValid())
	{
		return FString::Printf(TEXT("Error: %s"), *Error);
	}

	TraceServices::FAnalysisSessionReadScope ReadScope(*Session);

	const TraceServices::IThreadProvider& ThreadProvider = TraceServices::ReadThreadProvider(*Session);

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> ThreadsArray;
	ThreadProvider.EnumerateThreads([&](const TraceServices::FThreadInfo& Thread)
	{
		TSharedRef<FJsonObject> ThreadObj = MakeShared<FJsonObject>();
		ThreadObj->SetNumberField(TEXT("id"), Thread.Id);
		ThreadObj->SetStringField(TEXT("name"), Thread.Name ? Thread.Name : TEXT("Unknown"));
		if (Thread.GroupName && Thread.GroupName[0] != '\0')
		{
			ThreadObj->SetStringField(TEXT("group"), Thread.GroupName);
		}
		ThreadsArray.Add(MakeShared<FJsonValueObject>(ThreadObj));
	});
	Root->SetArrayField(TEXT("threads"), ThreadsArray);

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(Root, Writer);
	return OutputString;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FString UTraceAnalysisLibrary::GetTraceFrameSummary(const FString& TracePath)
{
	FString Error;
	TSharedPtr<const TraceServices::IAnalysisSession> Session = RunAnalysis(TracePath, Error);
	if (!Session.IsValid())
	{
		return FString::Printf(TEXT("Error: %s"), *Error);
	}

	TraceServices::FAnalysisSessionReadScope ReadScope(*Session);

	const TraceServices::IFrameProvider& FrameProvider = TraceServices::ReadFrameProvider(*Session);

	auto BuildFrameStats = [&](ETraceFrameType FrameType) -> TSharedRef<FJsonObject>
	{
		TSharedRef<FJsonObject> Stats = MakeShared<FJsonObject>();
		uint64 Count = FrameProvider.GetFrameCount(FrameType);
		Stats->SetNumberField(TEXT("count"), static_cast<double>(Count));

		if (Count > 0)
		{
			double Total = 0.0;
			double Min = DBL_MAX;
			double Max = 0.0;
			TArray<double> FrameTimes;
			FrameTimes.Reserve(Count);

			FrameProvider.EnumerateFrames(FrameType, 0, Count,
				[&](const TraceServices::FFrame& Frame)
			{
				double Dt = Frame.EndTime - Frame.StartTime;
				if (FMath::IsFinite(Dt) && Dt > 0.0 && Dt < 10.0)
				{
					Total += Dt;
					Min = FMath::Min(Min, Dt);
					Max = FMath::Max(Max, Dt);
					FrameTimes.Add(Dt);
				}
			});

			double Avg = (FrameTimes.Num() > 0) ? Total / FrameTimes.Num() : 0.0;
			Stats->SetNumberField(TEXT("avg_ms"), Avg * 1000.0);
			Stats->SetNumberField(TEXT("min_ms"), Min * 1000.0);
			Stats->SetNumberField(TEXT("max_ms"), Max * 1000.0);
			Stats->SetNumberField(TEXT("avg_fps"), 1.0 / Avg);

			// Percentiles
			FrameTimes.Sort();
			auto Percentile = [&](double P) -> double
			{
				int32 Idx = FMath::Clamp(static_cast<int32>(P * FrameTimes.Num()), 0, FrameTimes.Num() - 1);
				return FrameTimes[Idx] * 1000.0;
			};
			Stats->SetNumberField(TEXT("p50_ms"), Percentile(0.5));
			Stats->SetNumberField(TEXT("p90_ms"), Percentile(0.9));
			Stats->SetNumberField(TEXT("p95_ms"), Percentile(0.95));
			Stats->SetNumberField(TEXT("p99_ms"), Percentile(0.99));

			// Budget analysis
			int32 Over16ms = 0, Over33ms = 0;
			for (double Dt : FrameTimes)
			{
				if (Dt > 0.01667) Over16ms++;
				if (Dt > 0.03333) Over33ms++;
			}
			Stats->SetNumberField(TEXT("frames_over_60fps_budget"), Over16ms);
			Stats->SetNumberField(TEXT("frames_over_30fps_budget"), Over33ms);
		}
		return Stats;
	};

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("trace_file"), TracePath);
	Root->SetNumberField(TEXT("duration_seconds"), Session->GetDurationSeconds());
	Root->SetObjectField(TEXT("game_thread"), BuildFrameStats(ETraceFrameType::TraceFrameType_Game));
	Root->SetObjectField(TEXT("render_thread"), BuildFrameStats(ETraceFrameType::TraceFrameType_Rendering));

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(Root, Writer);
	return OutputString;
}
