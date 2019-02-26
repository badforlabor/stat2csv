// Fill out your copyright notice in the Description page of Project Settings.
#pragma optimize( "", off )

#include "Stat2CsvComp.h"

#include "Engine/Engine.h"
#include "RenderCore.h"
#include "Misc/App.h"
#include "RendererInterface.h"
#include "FileManager.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"

FRunnable* NewRecordMgr2();

// Sets default values for this component's properties
UStat2CsvComp::UStat2CsvComp()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;
	Saver = NULL;
	// ...
}

extern FRunnable* NewRecordMgr2();
// Called when the game starts
void UStat2CsvComp::BeginPlay()
{
	Super::BeginPlay();

	// ...
	if (Saver != NULL)
	{
		Saver->Exit();

		delete Saver;
		Saver = NULL;
	}
	Saver = NewRecordMgr2();
}


// Called every frame
void UStat2CsvComp::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// ...
}

void UStat2CsvComp::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	if (Saver != NULL)
	{
		Saver->Exit();

		delete Saver;
		Saver = NULL;
	}
}


class FRecordOne
{
public:

	FRecordOne()
		: MinValue(65536), MaxValue(0), AverageValue(0)
	{
	}

	float MinValue;
	float MaxValue;
	float AverageValue;

	void Record(float v)
	{
		if (v < MinValue)
		{
			MinValue = v;
		}
		if (v > MaxValue)
		{
			MaxValue = v;
		}
	}

};
class FRecordTime
{
public:
	FRecordTime(const char* InName)
		: LastTime(0), Name(InName)
	{}

	// µ•Œª «∫¡√Î
	void PushMilliSecond(bool newLine, float v)
	{
		auto now = 1.0 * FDateTime::UtcNow().GetTicks() / (ETimespan::TicksPerSecond);

		if (Datas.Num() == 0)
		{
			Values.Empty();
			LastTime = now;
			Datas.Add(FRecordOne());
		}
		FRecordOne& last = Datas.Last();
		last.Record(v);
		Values.Add(v);

		if (newLine)
		{
			float Total = 0;
			for (int i = 0; i < Values.Num(); i++)
			{
				Total += Values[i];
			}
			last.AverageValue = Total > 0 ? Total / Values.Num() : 0;

			Values.Empty();
			LastTime = now;
			Datas.Add(FRecordOne());
		}
	}

	TArray<float> GetOneLine(int Line)
	{
		TArray<float> ret;
		if (Line >= Datas.Num())
		{
			return ret;
		}

		auto one = Datas[Line];
		ret.Add(one.AverageValue);
		ret.Add(one.MinValue);
		ret.Add(one.MaxValue);
		return ret;
	}
	FString GetCsvTitle()
	{
		return FString::Printf(TEXT("Avg-%s, Min-%s, Max-%s"), *Name, *Name, *Name);
	}

private:
	TArray<float> Values;
	double LastTime;
	FString Name;
	TArray<FRecordOne> Datas;
};

class FRecordMgr : public FRunnable
{
public:
	FRecordMgr()
		:FPS("FPS"),
		FrameTime("FrameTime"),
		RenderTime("RenderTime"),
		GameTime("GameTime"),
		RHITime("RHITime"),
		GPUTime("GPUTime"),
		UsedMemory("Memory"),
		PeakUsedMemory("PreAllocMemory"),
		ReadLineCount(0),
		LineCount(0),
		LastTime(0),
		BeginTime(0),
		Thread(NULL)
	{
		BeginTime = 1.0 * FDateTime::UtcNow().GetTicks() / (ETimespan::TicksPerSecond);
		StopTaskCounter.Reset();
		bStopped = false;
		Thread = FRunnableThread::Create(this, TEXT("Stat2Csv"));
	}
	~FRecordMgr()
	{
		delete Thread;
		Thread = NULL;
	}

	FRecordTime FPS;
	FRecordTime FrameTime;
	FRecordTime RenderTime;
	FRecordTime GameTime;
	FRecordTime RHITime;
	FRecordTime GPUTime;
	FRecordTime UsedMemory;
	FRecordTime PeakUsedMemory;

	double LastTime;
	double BeginTime = 0;
	int LineCount;
	int ReadLineCount;
	FThreadSafeCounter StopTaskCounter;
	bool bStopped;
	FRunnableThread* Thread = NULL;

	void Tick()
	{
		if (!GEngine)
		{
			return;
		}
		auto now = 1.0 * FDateTime::UtcNow().GetTicks() / (ETimespan::TicksPerSecond);
		auto newLine = false;
		if (now - LastTime > 1)
		{
			newLine = true;
			LineCount++;
			LastTime = now;
		}

		FrameTime.PushMilliSecond(newLine, (FApp::GetCurrentTime() - FApp::GetLastTime()) * 1000);
		RenderTime.PushMilliSecond(newLine, FPlatformTime::ToMilliseconds(GRenderThreadTime));
		GameTime.PushMilliSecond(newLine, FPlatformTime::ToMilliseconds(GGameThreadTime));
		RHITime.PushMilliSecond(newLine, FPlatformTime::ToMilliseconds(GRHIThreadTime));
		GPUTime.PushMilliSecond(newLine, FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()));
		
		FPlatformMemoryStats MemoryStats = FPlatformMemory::GetStats();
		UsedMemory.PushMilliSecond(newLine, MemoryStats.UsedPhysical / 1000 / 1000);
		PeakUsedMemory.PushMilliSecond(newLine, MemoryStats.PeakUsedPhysical / 1000 / 1000);

		auto MaxTime = FMath::Max(RHIGetGPUFrameCycles(), FMath::Max(GRenderThreadTime, GGameThreadTime));
		
		FPS.PushMilliSecond(newLine, MaxTime > 0 ? 1000 / FPlatformTime::ToMilliseconds(MaxTime) : 200);
	}

	virtual uint32 Run() override
	{
		while (StopTaskCounter.GetValue() == 0)
		{
			FPlatformProcess::Sleep(0.03f);
			Tick();
			Dump();
		}
		bStopped = true;
		return 0;
	}
	virtual void Stop() override
	{
		StopTaskCounter.Increment();
	}
	virtual void Exit() override
	{
		Stop();
	}

	void Dump()
	{
		if (ReadLineCount >= LineCount)
		{
			return;
		}

		TArray<FRecordTime*> DumpTime;
		DumpTime.Add(&FPS);
		DumpTime.Add(&FrameTime);
		DumpTime.Add(&RenderTime);
		DumpTime.Add(&GameTime);
		DumpTime.Add(&RHITime);
		DumpTime.Add(&GPUTime);
		DumpTime.Add(&UsedMemory);
		DumpTime.Add(&PeakUsedMemory);

		int OldRead = ReadLineCount;
		ReadLineCount = LineCount;

		FArchive* ar = NULL;
		FString LineEnd("\r\n");
		if (OldRead == 0)
		{
			ar = IFileManager::Get().CreateFileWriter(*FString::Printf(TEXT("%s/%s"), *FPaths::ProjectSavedDir(), TEXT("fps.csv")));

			FString OneLine = "time";
			for (int i = 0; i < DumpTime.Num(); i++)
			{
				auto t = DumpTime[i];

				if (!OneLine.IsEmpty())
				{
					OneLine += ",";
				}
				OneLine += t->GetCsvTitle();
			}
			OneLine += LineEnd;

			auto Src = StringCast<ANSICHAR>(*OneLine, OneLine.Len());
			ar->Serialize((ANSICHAR*)Src.Get(), Src.Length() * sizeof(ANSICHAR));
		}
		else
		{
			ar = IFileManager::Get().CreateFileWriter(*FString::Printf(TEXT("%s/%s"), *FPaths::ProjectSavedDir(), TEXT("fps.csv")), FILEWRITE_Append);
		}

		for (int line = OldRead; line < ReadLineCount; line++)
		{
			FString OneLine = FString::Printf(TEXT("%.2f"), 1.0 * FDateTime::UtcNow().GetTicks() / (ETimespan::TicksPerSecond) - BeginTime);
			for (int i = 0; i < DumpTime.Num(); i++)
			{
				auto t = DumpTime[i];
				auto Datas = t->GetOneLine(line);
				for (int k = 0; k < Datas.Num(); k++)
				{
					if (!OneLine.IsEmpty())
					{
						OneLine += ",";
					}
					OneLine += FString::Printf(TEXT("%.1f"), Datas[k]);
				}
			}
			OneLine += LineEnd;

			auto Src = StringCast<ANSICHAR>(*OneLine, OneLine.Len());
			ar->Serialize((ANSICHAR*)Src.Get(), Src.Length() * sizeof(ANSICHAR));
		}
		ar->Flush();
		ar->Close();
	}
};

FRunnable* NewRecordMgr2()
{
	return new FRecordMgr();
}

#pragma optimize( "", on )