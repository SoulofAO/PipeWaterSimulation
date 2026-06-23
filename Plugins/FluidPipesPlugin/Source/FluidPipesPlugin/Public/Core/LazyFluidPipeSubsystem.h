#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LazyFluidPipeSubsystem.generated.h"

class ULazyFluidPipesDeveloperSettings;

UCLASS()
class FLUIDPIPESPLUGIN_API ULazyFluidPipeSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	const ULazyFluidPipesDeveloperSettings& GetSimulationSettings() const;
	void SetRuntimeSimulationSettingsOverride(ULazyFluidPipesDeveloperSettings* SimulationSettings);
	void ClearRuntimeSimulationSettingsOverride();
	bool HasRuntimeSimulationSettingsOverride() const;

	void SetBenchmarkFrameStatsRecordingEnabled(bool bEnabled);
	bool IsBenchmarkFrameStatsRecordingEnabled() const;
	void ResetBenchmarkFrameStats();
	void RecordBenchmarkOneDFrameSimulationDurationSeconds(double SimulationDurationSeconds);
	void RecordBenchmarkZeroDFrameSimulationDurationSeconds(double SimulationDurationSeconds);
	bool ConsumeBenchmarkFrameStats(double& OutAverageOneDFrameSimulationMilliseconds, double& OutAverageZeroDFrameSimulationMilliseconds, double& OutAverageCombinedFrameSimulationMilliseconds, int32& OutSampleCount);

private:
	UPROPERTY(Transient)
	TObjectPtr<ULazyFluidPipesDeveloperSettings> RuntimeSimulationSettingsOverride;

	bool bBenchmarkFrameStatsRecordingEnabled = false;
	double BenchmarkAccumulatedOneDFrameSimulationSeconds = 0.0;
	double BenchmarkAccumulatedZeroDFrameSimulationSeconds = 0.0;
	double BenchmarkAccumulatedCombinedFrameSimulationSeconds = 0.0;
	int32 BenchmarkFrameSampleCount = 0;
	uint64 BenchmarkStatsLastFrameNumber = UINT64_MAX;

	void RegisterBenchmarkFrameSampleIfNeeded();
};
