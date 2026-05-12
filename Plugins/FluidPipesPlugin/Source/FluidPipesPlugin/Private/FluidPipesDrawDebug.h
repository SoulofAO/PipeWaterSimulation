#pragma once

#include "CoreMinimal.h"

int32 FluidPipesGetDrawDebugLevel();
bool FluidPipesIsWorldLocationWithinDebugDrawDistance(const UWorld* World, const FVector& WorldLocation);

FORCEINLINE bool FluidPipesShouldDrawDebug()
{
	return FluidPipesGetDrawDebugLevel() > 0;
}

FORCEINLINE int32 FluidPipesGetOneDWorldDebugDetailLevel()
{
	return FluidPipesShouldDrawDebug() ? FluidPipesGetDrawDebugLevel() : 0;
}

FORCEINLINE bool FluidPipesShouldDrawZeroDWorldOverlay()
{
return FluidPipesShouldDrawDebug();
}

FORCEINLINE bool FluidPipesShouldEmitScreenDebugMessages()
{
	return FluidPipesShouldDrawDebug();
}
