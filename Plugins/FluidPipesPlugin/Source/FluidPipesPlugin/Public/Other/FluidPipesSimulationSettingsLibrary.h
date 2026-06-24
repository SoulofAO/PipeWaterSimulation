#pragma once

#include "CoreMinimal.h"

class ULazyFluidPipesDeveloperSettings;
class UObject;

class FLUIDPIPESPLUGIN_API FFluidPipesSimulationSettingsLibrary
{
public:
	static const ULazyFluidPipesDeveloperSettings& ResolveSimulationSettings(const UObject* WorldContextObject);

	static bool IsFluidHybridSimulationActive(const ULazyFluidPipesDeveloperSettings& Settings);

	static float ResolveHybridSimulationStepTime(const ULazyFluidPipesDeveloperSettings& Settings);

	static bool HybridSimulationRequiresCpuGameThreadCoupling(const ULazyFluidPipesDeveloperSettings& Settings);
};
