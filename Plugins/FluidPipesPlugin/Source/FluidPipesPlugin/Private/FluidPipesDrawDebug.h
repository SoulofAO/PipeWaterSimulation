#pragma once

#include "CoreMinimal.h"

class UObject;
struct FLinearColor;

int32 FluidPipesGetDrawDebugLevel();
bool FluidPipesTryResolveSimulateInEditorViewportReferenceWorldLocation(FVector& OutReferenceWorldLocation);
bool FluidPipesIsWorldLocationWithinDebugDrawDistance(const UWorld* World, const FVector& WorldLocation);
bool FluidPipesShouldPrintSimulationFrameTiming();
void FluidPipesPrintSimulationFrameTimingMessage(UObject* WorldContextObject, const FString& Message, const FLinearColor& TextColor);

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
