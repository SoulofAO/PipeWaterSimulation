#pragma once

#include "CoreMinimal.h"

class ULazyFluidPipesDeveloperSettings;
class UObject;

class FLUIDPIPESPLUGIN_API FFluidPipesSimulationSettingsLibrary
{
public:
	static const ULazyFluidPipesDeveloperSettings& ResolveSimulationSettings(const UObject* WorldContextObject);
};
