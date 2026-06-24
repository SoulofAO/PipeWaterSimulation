#include "Other/FluidPipesSimulationSettingsLibrary.h"

#include "Core/LazyFluidPipeSubsystem.h"
#include "Engine/World.h"
#include "Other/LazyFluidPipesDeveloperSettings.h"

const ULazyFluidPipesDeveloperSettings& FFluidPipesSimulationSettingsLibrary::ResolveSimulationSettings(const UObject* WorldContextObject)
{
	if (WorldContextObject)
	{
		if (const UWorld* World = WorldContextObject->GetWorld())
		{
			if (const ULazyFluidPipeSubsystem* FluidPipeSubsystem = World->GetSubsystem<ULazyFluidPipeSubsystem>())
			{
				return FluidPipeSubsystem->GetSimulationSettings();
			}
		}
	}

	return *GetDefault<ULazyFluidPipesDeveloperSettings>();
}

bool FFluidPipesSimulationSettingsLibrary::IsFluidHybridSimulationActive(const ULazyFluidPipesDeveloperSettings& Settings)
{
	return Settings.EnableFluidNetworkSimulationZeroD && Settings.EnableFluidSegmentSimulationOneD;
}

float FFluidPipesSimulationSettingsLibrary::ResolveHybridSimulationStepTime(const ULazyFluidPipesDeveloperSettings& Settings)
{
	return FMath::Min3(Settings.HybridSimulationStepTime, Settings.SimulationStepTimeZeroD, Settings.SimulationStepTimeOneD);
}

bool FFluidPipesSimulationSettingsLibrary::HybridSimulationRequiresCpuGameThreadCoupling(const ULazyFluidPipesDeveloperSettings& Settings)
{
	if (!IsFluidHybridSimulationActive(Settings))
	{
		return false;
	}

	return Settings.FluidNetworkSimulationZeroDBackend != EFluidNetworkSimulationZeroDBackend::CpuGameThread
		|| Settings.FluidSegmentSimulationOneDBackend != EFluidSegmentSimulationOneDBackend::CpuGameThread;
}
