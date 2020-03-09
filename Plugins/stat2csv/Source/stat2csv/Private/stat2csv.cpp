// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "stat2csv.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Core.h"
#include "CoreUObject.h"
#if WITH_EDITOR
#include "Editor.h"
#endif
#include "Kismet/KismetSystemLibrary.h"

#define LOCTEXT_NAMESPACE "Fstat2csvModule"

void Fstat2csvModule::StartupModule()
{
	LastIdx = 0;

	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
#if WITH_EDITOR
	if (GIsEditor)
	{
		FEditorDelegates::EndPIE.AddRaw(this, &Fstat2csvModule::EndPIE);
	}
#endif
	FCoreDelegates::OnPreExit.AddRaw(this, &Fstat2csvModule::OnPreExit);
	FCoreUObjectDelegates::PostLoadMapWithWorld.AddRaw(this, &Fstat2csvModule::OnPostLoadMap);

}

void Fstat2csvModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
#if WITH_EDITOR
	if (GIsEditor)
	{
		FEditorDelegates::EndPIE.RemoveAll(this);
	}
#endif
	FCoreDelegates::OnPreExit.RemoveAll(this);
	FCoreUObjectDelegates::PostLoadMapWithWorld.RemoveAll(this);
}
#ifdef TEST_STAT
static FRunnable* Saver = NULL;
void StopStat2Csv()
{
	if (Saver != NULL)
	{
		Saver->Exit();

		delete Saver;
		Saver = NULL;
	}
}
void StartStat2Csv()
{
	StopStat2Csv();
	Saver = Fstat2csvTools::NewRecordMgr2();
}
#else
void StopStat2Csv()
{}
void StartStat2Csv()
{}
#endif
void Fstat2csvModule::EndPIE(bool bIsSimulating)
{
	EndDump();
	StopStat2Csv();
}
void Fstat2csvModule::OnPreExit()
{
	EndDump();
	StopStat2Csv();
}
void Fstat2csvModule::OnPostLoadMap(UWorld* World)
{
	StartStat2Csv();
	BeginDump(World);
}
void Fstat2csvModule::BeginDump(UWorld* World)
{
	if (World->WorldType == EWorldType::PIE
		|| World->WorldType == EWorldType::Game)
	{
	}
	else
	{
		return;
	}

	if (LastWorld != nullptr)
	{
		EndDump();
	}

	const FDateTime CaptureStartTime = FDateTime::Now();

	LastWorld = World;
	LastIdx = 0;
	OutputFilename = CaptureStartTime.ToString(TEXT("stat2csv-%y%m%d_%H%M%S"));
	OutputFilename = FPaths::Combine(*FPaths::ProjectDir(), TEXT("Saved"), TEXT("stat2csv"), *OutputFilename);

	ActiveFrameTimesChart = MakeShareable(new FFineGrainedPerformanceTracker(CaptureStartTime));
	GEngine->AddPerformanceDataConsumer(ActiveFrameTimesChart);

	auto Delegate = FTimerDelegate::CreateRaw(this, &Fstat2csvModule::DoDumpTimer);
	LastWorld->GetTimerManager().SetTimer(DelayStartRoomHandler, Delegate, 1, true, 1);
}
void Fstat2csvModule::EndDump()
{
	if (!ActiveFrameTimesChart.IsValid())
	{
		return;
	}
	if (!LastWorld)
	{
		return;
	}

	if (OutputFile != nullptr)
	{
		DoDumpTimer();

		delete OutputFile;
		OutputFile = nullptr;
	}

	// 输出一个平均值
	if (ActiveFrameTimesChart.IsValid())
	{
		OutputFile = IFileManager::Get().CreateFileWriter(*FString::Printf(TEXT("%s-avg.csv"), *OutputFilename));
		OutputFile->Logf(TEXT("Percentile,Frame (ms), GT (ms), RT (ms), GPU (ms),DynRes,Context"));
		TArray<float> FrameTimesCopy = ActiveFrameTimesChart->FrameTimes;
		TArray<float> GameThreadFrameTimesCopy = ActiveFrameTimesChart->GameThreadFrameTimes;
		TArray<float> RenderThreadFrameTimesCopy = ActiveFrameTimesChart->RenderThreadFrameTimes;
		TArray<float> GPUFrameTimesCopy = ActiveFrameTimesChart->GPUFrameTimes;
		TArray<float> DynamicResolutionScreenPercentagesCopy = ActiveFrameTimesChart->DynamicResolutionScreenPercentages;
		// using selection a few times should still be faster than full sort once,
		// since it's linear vs non-linear (O(n) vs O(n log n) for quickselect vs quicksort)
		for (int32 Percentile = 25; Percentile <= 75; Percentile += 25)
		{
			OutputFile->Logf(TEXT("%d,%.2f,%.2f,%.2f,%.2f,%.2f,%d"), Percentile,
				ActiveFrameTimesChart->GetPercentileValue(FrameTimesCopy, Percentile) * 1000,
				ActiveFrameTimesChart->GetPercentileValue(GameThreadFrameTimesCopy, Percentile) * 1000,
				ActiveFrameTimesChart->GetPercentileValue(RenderThreadFrameTimesCopy, Percentile) * 1000,
				ActiveFrameTimesChart->GetPercentileValue(GPUFrameTimesCopy, Percentile) * 1000,
				ActiveFrameTimesChart->GetPercentileValue(DynamicResolutionScreenPercentagesCopy, Percentile),
				0
			);
		}

		delete OutputFile;
		OutputFile = nullptr;
	}

	LastWorld->GetTimerManager().ClearTimer(DelayStartRoomHandler);
	LastWorld = nullptr;
	ActiveFrameTimesChart = nullptr;
}
void Fstat2csvModule::DoDumpTimer()
{
	if (!ActiveFrameTimesChart.IsValid())
	{
		return;
	}
	if (ActiveFrameTimesChart.IsValid())
	{
		if (OutputFile == nullptr)
		{
			OutputFile = IFileManager::Get().CreateFileWriter(*FString::Printf(TEXT("%s.csv"), *OutputFilename), EFileWrite::FILEWRITE_AllowRead);
			OutputFile->Logf(TEXT("Time (sec),Frame (ms), GT (ms), RT (ms), GPU (ms),DynRes,Context"));
		}

		if (OutputFile)
		{
			double ElapsedTime = 0.0;
			for (int32 i = LastIdx; i < ActiveFrameTimesChart->FrameTimes.Num(); i++)
			{
				OutputFile->Logf(TEXT("%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%d"), ElapsedTime,
					ActiveFrameTimesChart->FrameTimes[i] * 1000,
					ActiveFrameTimesChart->GameThreadFrameTimes[i] * 1000,
					ActiveFrameTimesChart->RenderThreadFrameTimes[i] * 1000,
					ActiveFrameTimesChart->GPUFrameTimes[i] * 1000,
					ActiveFrameTimesChart->DynamicResolutionScreenPercentages[i],
					ActiveFrameTimesChart->ActiveModes[i]);
				ElapsedTime += ActiveFrameTimesChart->FrameTimes[i];
			}
		}

		LastIdx = ActiveFrameTimesChart->FrameTimes.Num();
	}
}


#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(Fstat2csvModule, stat2csv)