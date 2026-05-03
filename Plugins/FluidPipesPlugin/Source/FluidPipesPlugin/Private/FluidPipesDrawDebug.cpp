#include "FluidPipesDrawDebug.h"

#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> GFluidPipesDrawDebugLevel(
	TEXT("FluidPipes.DrawDebug"),
	0,
	TEXT("0 all off. Non-zero all on: 1D world markers and labels, 0D network overlay, on-screen PrintString logs (ticks, import, CFL)."),
	ECVF_Default);

int32 FluidPipesGetDrawDebugLevel()
{
	return GFluidPipesDrawDebugLevel.GetValueOnGameThread();
}
