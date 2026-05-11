#pragma once

#include "CoreMinimal.h"

int32 FluidPipesGetDrawDebugLevel();

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
