#include "FluidPipesDrawDebug.h"

#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> GFluidPipesDrawDebugLevel(
	TEXT("FluidPipes.DrawDebug"),
	0,
	TEXT("0 off. 1+: 0D overlay and PrintString logs. 1D: 1 = segment and endpoint text only, 2+ = per-cell pressure/flow/velocity/fill text (higher DrawDebug = more 1D detail)."),
	ECVF_Default);

int32 FluidPipesGetDrawDebugLevel()
{
	return GFluidPipesDrawDebugLevel.GetValueOnGameThread();
}
