// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine.h"
#include "ChartCreation.h"
#include "Modules/ModuleManager.h"

class Fstat2csvModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

protected:
	void EndPIE(bool bIsSimulating);
	void OnPreExit();
	void OnPostLoadMap(class UWorld*);
	void DoDumpTimer();
	void BeginDump(class UWorld* World);
	void EndDump();

	class UWorld* LastWorld;
	TSharedPtr<FFineGrainedPerformanceTracker> ActiveFrameTimesChart;
	FTimerHandle DelayStartRoomHandler;
	int LastIdx;
	class FArchive* OutputFile;
	FString OutputFilename;
};

class STAT2CSV_API Fstat2csvTools
{
public:
	static class FRunnable* NewRecordMgr2();
};