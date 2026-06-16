#include "Core/Actors/PipeFluidSimulationSettingsBenchmarkActor.h"

#include "Core/Actors/PipeFluidBenchmarkNetworkActor.h"
#include "Core/LazyFluidPipeSubsystem.h"
#include "Core/LevelImport/FluidPipeLevelImportSubsystem.h"
#include "Core/Simulation/FluidPipeZeroDOneDPhysicsComparison.h"
#include "Core/Simulation0D/FluidNetwork0DSubsystem.h"
#include "Engine/World.h"
#include "Other/LazyFluidPipesDeveloperSettings.h"

APipeFluidSimulationSettingsBenchmarkActor::APipeFluidSimulationSettingsBenchmarkActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
}

void APipeFluidSimulationSettingsBenchmarkActor::BeginPlay()
{
	Super::BeginPlay();

	if (bBenchmarkRunning)
	{
		return;
	}

	if (SimulationSettingsProfiles.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("Fluid simulation settings benchmark: no SimulationSettingsProfiles configured."));
		return;
	}

	if (BenchmarkNetworkActors.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("Fluid simulation settings benchmark: no BenchmarkNetworkActors configured."));
		return;
	}

	StartBenchmarkRun();
}

void APipeFluidSimulationSettingsBenchmarkActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopSimulationSettingsBenchmark();
	Super::EndPlay(EndPlayReason);
}

void APipeFluidSimulationSettingsBenchmarkActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bBenchmarkRunning)
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		StopSimulationSettingsBenchmark();
		return;
	}

	const double ElapsedPhaseSeconds = World->GetTimeSeconds() - PhaseStartWorldSeconds;
	if (BenchmarkPhase == EFluidSimulationSettingsBenchmarkPhase::Warmup)
	{
		if (ElapsedPhaseSeconds >= static_cast<double>(WarmupDurationSeconds))
		{
			AdvanceBenchmarkPhase();
		}
		return;
	}

	if (BenchmarkPhase == EFluidSimulationSettingsBenchmarkPhase::Measure)
	{
		if (ElapsedPhaseSeconds >= static_cast<double>(MeasurementDurationSeconds))
		{
			FinishCurrentBenchmarkPairMeasurement();
		}
	}
}

void APipeFluidSimulationSettingsBenchmarkActor::StopSimulationSettingsBenchmark()
{
	if (!bBenchmarkRunning)
	{
		return;
	}

	bBenchmarkRunning = false;
	SetActorTickEnabled(false);
	ClearAllBenchmarkNetworks();
	RestoreDefaultSimulationSettingsOverride();
}

void APipeFluidSimulationSettingsBenchmarkActor::StartBenchmarkRun()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	BenchmarkResults.Reset();
	CurrentNetworkIndex = 0;
	CurrentProfileIndex = 0;
	LastBuiltNetworkIndex = INDEX_NONE;
	bBenchmarkRunning = true;
	SetActorTickEnabled(true);

	if (ULazyFluidPipeSubsystem* FluidPipeSubsystem = World->GetSubsystem<ULazyFluidPipeSubsystem>())
	{
		FluidPipeSubsystem->SetBenchmarkFrameStatsRecordingEnabled(true);
		FluidPipeSubsystem->ResetBenchmarkFrameStats();
	}

	ApplyCurrentBenchmarkPair();
	BenchmarkPhase = EFluidSimulationSettingsBenchmarkPhase::Warmup;
	PhaseStartWorldSeconds = World->GetTimeSeconds();
}

void APipeFluidSimulationSettingsBenchmarkActor::ApplyCurrentBenchmarkPair()
{
	UWorld* World = GetWorld();
	if (!World || !BenchmarkNetworkActors.IsValidIndex(CurrentNetworkIndex) || !SimulationSettingsProfiles.IsValidIndex(CurrentProfileIndex))
	{
		return;
	}

	if (CurrentNetworkIndex != LastBuiltNetworkIndex)
	{
		ClearAllBenchmarkNetworks();
		if (APipeFluidBenchmarkNetworkActor* ActiveNetwork = BenchmarkNetworkActors[CurrentNetworkIndex])
		{
			ActiveNetwork->BuildBenchmarkNetwork();
		}
		LastBuiltNetworkIndex = CurrentNetworkIndex;
	}

	ULazyFluidPipesDeveloperSettings* ActiveProfile = SimulationSettingsProfiles[CurrentProfileIndex];
	if (!ActiveProfile)
	{
		return;
	}

	if (ULazyFluidPipeSubsystem* FluidPipeSubsystem = World->GetSubsystem<ULazyFluidPipeSubsystem>())
	{
		FluidPipeSubsystem->SetRuntimeSimulationSettingsOverride(ActiveProfile);
		FluidPipeSubsystem->ResetBenchmarkFrameStats();
	}

	if (UFluidNetwork0DSubsystem* ZeroDSubsystem = World->GetSubsystem<UFluidNetwork0DSubsystem>())
	{
		ZeroDSubsystem->ResetSimulationState();
	}

	if (UFluidPipeLevelImportSubsystem* ImportSubsystem = World->GetSubsystem<UFluidPipeLevelImportSubsystem>())
	{
		ImportSubsystem->RunLevelPipeImportNow();
	}
}

void APipeFluidSimulationSettingsBenchmarkActor::AdvanceBenchmarkPhase()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (ULazyFluidPipeSubsystem* FluidPipeSubsystem = World->GetSubsystem<ULazyFluidPipeSubsystem>())
	{
		FluidPipeSubsystem->ResetBenchmarkFrameStats();
	}

	BenchmarkPhase = EFluidSimulationSettingsBenchmarkPhase::Measure;
	PhaseStartWorldSeconds = World->GetTimeSeconds();
}

void APipeFluidSimulationSettingsBenchmarkActor::FinishCurrentBenchmarkPairMeasurement()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		StopSimulationSettingsBenchmark();
		return;
	}

	FFluidSimulationSettingsBenchmarkResult Result;
	if (BenchmarkNetworkActors.IsValidIndex(CurrentNetworkIndex) && BenchmarkNetworkActors[CurrentNetworkIndex])
	{
		Result.NetworkDescription = BenchmarkNetworkActors[CurrentNetworkIndex]->BuildNetworkDescription();
	}
	if (SimulationSettingsProfiles.IsValidIndex(CurrentProfileIndex) && SimulationSettingsProfiles[CurrentProfileIndex])
	{
		Result.ProfileDescription = SimulationSettingsProfiles[CurrentProfileIndex]->BuildProfileDescription();
	}

	double AverageOneDMilliseconds = 0.0;
	double AverageZeroDMilliseconds = 0.0;
	double AverageCombinedMilliseconds = 0.0;
	int32 SampleCount = 0;
	if (ULazyFluidPipeSubsystem* FluidPipeSubsystem = World->GetSubsystem<ULazyFluidPipeSubsystem>())
	{
		FluidPipeSubsystem->ConsumeBenchmarkFrameStats(AverageOneDMilliseconds, AverageZeroDMilliseconds, AverageCombinedMilliseconds, SampleCount);
	}

	Result.AverageOneDSimulationMilliseconds = AverageOneDMilliseconds;
	Result.AverageZeroDSimulationMilliseconds = AverageZeroDMilliseconds;
	Result.AverageCombinedSimulationMilliseconds = AverageCombinedMilliseconds;
	Result.FrameSampleCount = SampleCount;

	BenchmarkResults.Add(Result);

	if (bLogZeroDOneDPhysicsComparisonAfterEachPair)
	{
		FFluidPipeZeroDOneDPhysicsComparison::LogComparisonReport(World);
	}

	++CurrentProfileIndex;
	if (CurrentProfileIndex >= SimulationSettingsProfiles.Num())
	{
		CurrentProfileIndex = 0;
		++CurrentNetworkIndex;
	}

	if (!HasAnotherBenchmarkPairAfterCurrent())
	{
		CompleteBenchmarkAndLogResults();
		return;
	}

	ApplyCurrentBenchmarkPair();
	BenchmarkPhase = EFluidSimulationSettingsBenchmarkPhase::Warmup;
	PhaseStartWorldSeconds = World->GetTimeSeconds();
}

void APipeFluidSimulationSettingsBenchmarkActor::CompleteBenchmarkAndLogResults()
{
	UE_LOG(LogTemp, Log, TEXT("Fluid simulation settings benchmark complete. Pairs=%d"), BenchmarkResults.Num());
	for (int32 ResultIndex = 0; ResultIndex < BenchmarkResults.Num(); ++ResultIndex)
	{
		const FFluidSimulationSettingsBenchmarkResult& Result = BenchmarkResults[ResultIndex];
		const FString ResultLogMessage = FString::Format(
			TEXT("[{0}] network={1} | profile={2} | 1D={3} ms | 0D={4} ms | combined={5} ms | samples={6}"),
			{
				FString::FromInt(ResultIndex),
				Result.NetworkDescription,
				Result.ProfileDescription,
				FString::SanitizeFloat(Result.AverageOneDSimulationMilliseconds),
				FString::SanitizeFloat(Result.AverageZeroDSimulationMilliseconds),
				FString::SanitizeFloat(Result.AverageCombinedSimulationMilliseconds),
				FString::FromInt(Result.FrameSampleCount)
			});
		UE_LOG(LogTemp, Log, TEXT("%s"), *ResultLogMessage);
	}

	StopSimulationSettingsBenchmark();
}

void APipeFluidSimulationSettingsBenchmarkActor::RestoreDefaultSimulationSettingsOverride()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (ULazyFluidPipeSubsystem* FluidPipeSubsystem = World->GetSubsystem<ULazyFluidPipeSubsystem>())
	{
		FluidPipeSubsystem->SetBenchmarkFrameStatsRecordingEnabled(false);
		FluidPipeSubsystem->ClearRuntimeSimulationSettingsOverride();
	}
}

void APipeFluidSimulationSettingsBenchmarkActor::ClearAllBenchmarkNetworks()
{
	for (APipeFluidBenchmarkNetworkActor* BenchmarkNetwork : BenchmarkNetworkActors)
	{
		if (BenchmarkNetwork)
		{
			BenchmarkNetwork->ClearBenchmarkNetwork();
		}
	}
	LastBuiltNetworkIndex = INDEX_NONE;
}

int32 APipeFluidSimulationSettingsBenchmarkActor::GetTotalBenchmarkPairCount() const
{
	return BenchmarkNetworkActors.Num() * SimulationSettingsProfiles.Num();
}

bool APipeFluidSimulationSettingsBenchmarkActor::HasAnotherBenchmarkPairAfterCurrent() const
{
	if (SimulationSettingsProfiles.Num() == 0 || BenchmarkNetworkActors.Num() == 0)
	{
		return false;
	}

	const int32 NextLinearIndex = (CurrentNetworkIndex * SimulationSettingsProfiles.Num()) + CurrentProfileIndex;
	return NextLinearIndex < GetTotalBenchmarkPairCount();
}
