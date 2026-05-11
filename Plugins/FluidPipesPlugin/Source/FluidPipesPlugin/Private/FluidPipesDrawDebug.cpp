#include "FluidPipesDrawDebug.h"

#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> GFluidPipesDrawDebugLevel(
	TEXT("FluidPipes.DrawDebug"),
	0,
	TEXT("0 off. 1+: 0D overlay and PrintString. 1D text detail by level (1=summary+ends, 2+=cells). World text: Project Settings / FluidPipes / FluidPipesWorldDebug (distance, toggles)."),
	ECVF_Default);

int32 FluidPipesGetDrawDebugLevel()
{
	return GFluidPipesDrawDebugLevel.GetValueOnGameThread();
}
