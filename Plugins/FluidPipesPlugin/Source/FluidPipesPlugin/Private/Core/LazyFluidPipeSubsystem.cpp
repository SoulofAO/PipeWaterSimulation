#include "Core/LazyFluidPipeSubsystem.h"

#include "CoreGlobals.h"
#include "Other/LazyFluidPipesDeveloperSettings.h"

const ULazyFluidPipesDeveloperSettings& ULazyFluidPipeSubsystem::GetSimulationSettings() const
{
	if (RuntimeSimulationSettingsOverride)
	{
		return *RuntimeSimulationSettingsOverride;
	}

	return *GetDefault<ULazyFluidPipesDeveloperSettings>();
}

void ULazyFluidPipeSubsystem::SetRuntimeSimulationSettingsOverride(ULazyFluidPipesDeveloperSettings* SimulationSettings)
{
	RuntimeSimulationSettingsOverride = SimulationSettings;
}

void ULazyFluidPipeSubsystem::ClearRuntimeSimulationSettingsOverride()
{
	RuntimeSimulationSettingsOverride = nullptr;
}

bool ULazyFluidPipeSubsystem::HasRuntimeSimulationSettingsOverride() const
{
	return RuntimeSimulationSettingsOverride != nullptr;
}

void ULazyFluidPipeSubsystem::SetBenchmarkFrameStatsRecordingEnabled(bool bEnabled)
{
	bBenchmarkFrameStatsRecordingEnabled = bEnabled;
	if (!bEnabled)
	{
		ResetBenchmarkFrameStats();
	}
}

bool ULazyFluidPipeSubsystem::IsBenchmarkFrameStatsRecordingEnabled() const
{
	return bBenchmarkFrameStatsRecordingEnabled;
}

void ULazyFluidPipeSubsystem::ResetBenchmarkFrameStats()
{
	BenchmarkAccumulatedOneDFrameSimulationSeconds = 0.0;
	BenchmarkAccumulatedZeroDFrameSimulationSeconds = 0.0;
	BenchmarkAccumulatedCombinedFrameSimulationSeconds = 0.0;
	BenchmarkFrameSampleCount = 0;
	BenchmarkStatsLastFrameNumber = UINT64_MAX;
}

void ULazyFluidPipeSubsystem::RegisterBenchmarkFrameSampleIfNeeded()
{
	if (GFrameCounter != BenchmarkStatsLastFrameNumber)
	{
		BenchmarkStatsLastFrameNumber = GFrameCounter;
		++BenchmarkFrameSampleCount;
	}
}

void ULazyFluidPipeSubsystem::RecordBenchmarkOneDFrameSimulationDurationSeconds(double SimulationDurationSeconds)
{
	if (!bBenchmarkFrameStatsRecordingEnabled)
	{
		return;
	}
	RegisterBenchmarkFrameSampleIfNeeded();
	BenchmarkAccumulatedOneDFrameSimulationSeconds += SimulationDurationSeconds;
	BenchmarkAccumulatedCombinedFrameSimulationSeconds += SimulationDurationSeconds;
}

void ULazyFluidPipeSubsystem::RecordBenchmarkZeroDFrameSimulationDurationSeconds(double SimulationDurationSeconds)
{
	if (!bBenchmarkFrameStatsRecordingEnabled)
	{
		return;
	}
	RegisterBenchmarkFrameSampleIfNeeded();
	BenchmarkAccumulatedZeroDFrameSimulationSeconds += SimulationDurationSeconds;
	BenchmarkAccumulatedCombinedFrameSimulationSeconds += SimulationDurationSeconds;
}

bool ULazyFluidPipeSubsystem::ConsumeBenchmarkFrameStats(double& OutAverageOneDFrameSimulationMilliseconds, double& OutAverageZeroDFrameSimulationMilliseconds, double& OutAverageCombinedFrameSimulationMilliseconds, int32& OutSampleCount)
{
	OutSampleCount = BenchmarkFrameSampleCount;
	if (BenchmarkFrameSampleCount <= 0)
	{
		OutAverageOneDFrameSimulationMilliseconds = 0.0;
		OutAverageZeroDFrameSimulationMilliseconds = 0.0;
		OutAverageCombinedFrameSimulationMilliseconds = 0.0;
		ResetBenchmarkFrameStats();
		return false;
	}

	const double SampleCountAsDouble = static_cast<double>(BenchmarkFrameSampleCount);
	OutAverageOneDFrameSimulationMilliseconds = (BenchmarkAccumulatedOneDFrameSimulationSeconds / SampleCountAsDouble) * 1000.0;
	OutAverageZeroDFrameSimulationMilliseconds = (BenchmarkAccumulatedZeroDFrameSimulationSeconds / SampleCountAsDouble) * 1000.0;
	OutAverageCombinedFrameSimulationMilliseconds = (BenchmarkAccumulatedCombinedFrameSimulationSeconds / SampleCountAsDouble) * 1000.0;
	ResetBenchmarkFrameStats();
	return true;
}
