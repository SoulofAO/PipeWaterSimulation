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
