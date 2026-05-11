#pragma once

#include "CoreMinimal.h"

class UWorld;

void FluidPipesWorldDebugTextStartup();
void FluidPipesWorldDebugTextShutdown();
void FluidPipesWorldDebugTextClearWorld(UWorld* World);
void FluidPipesWorldDebugTextQueueString(UWorld* World, FVector WorldLocation, const FString& DisplayText, FColor TextColor, float FontScale);
