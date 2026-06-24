#include "FluidPipesDrawDebug.h"

#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Other/FluidPipesSimulationSettingsLibrary.h"
#include "Other/LazyFluidPipesDeveloperSettings.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#if WITH_EDITOR
#include "Editor/EditorEngine.h"
#include "UnrealEdGlobals.h"
#include "LevelEditorViewport.h"
#endif

static TAutoConsoleVariable<int32> GFluidPipesDrawDebugLevel(
	TEXT("FluidPipes.DrawDebug"),
	0,
	TEXT("0 off. 1+: 0D overlay and PrintString. 1D text detail by level (1=summary+ends, 2+=cells). World text: Project Settings / FluidPipes / FluidPipesWorldDebug (distance, toggles)."),
	ECVF_Default);

static TAutoConsoleVariable<int32> GFluidPipesPrintSimulationFrameTiming(
	TEXT("FluidPipes.PrintSimulationFrameTiming"),
	0,
	TEXT("0 off. 1: print 0D and 1D simulation subsystem frame processing time to screen and log each tick."),
	ECVF_Default);

int32 FluidPipesGetDrawDebugLevel()
{
	return GFluidPipesDrawDebugLevel.GetValueOnGameThread();
}

bool FluidPipesTryResolveSimulateInEditorViewportReferenceWorldLocation(FVector& OutReferenceWorldLocation)
{
#if WITH_EDITOR
	if (GEditor && GEditor->bIsSimulatingInEditor && GCurrentLevelEditingViewportClient)
	{
		OutReferenceWorldLocation = GCurrentLevelEditingViewportClient->ViewTransformPerspective.GetLocation();
		return true;
	}
#endif
	return false;
}

bool FluidPipesIsWorldLocationWithinDebugDrawDistance(const UWorld* World, const FVector& WorldLocation)
{
	if (!World)
	{
		return false;
	}

	const ULazyFluidPipesDeveloperSettings& Settings = FFluidPipesSimulationSettingsLibrary::ResolveSimulationSettings(World);
	const float MaximumDistanceCentimeters = Settings.WorldDebugMaximumDrawDistanceCentimeters;
	if (MaximumDistanceCentimeters <= KINDA_SMALL_NUMBER)
	{
		return true;
	}

	const APlayerController* PlayerController = World->GetFirstPlayerController();
	FVector ViewLocationWorld = PlayerController && PlayerController->PlayerCameraManager
		? PlayerController->PlayerCameraManager->GetCameraLocation()
		: FVector::ZeroVector;
#if WITH_EDITOR
	if (GEditor && GEditor->bIsSimulatingInEditor && GCurrentLevelEditingViewportClient)
	{
		ViewLocationWorld = GCurrentLevelEditingViewportClient->ViewTransformPerspective.GetLocation();
	}
#endif

	if (!PlayerController || !PlayerController->PlayerCameraManager)
	{
#if WITH_EDITOR
		if (FluidPipesTryResolveSimulateInEditorViewportReferenceWorldLocation(ViewLocationWorld))
		{
			return FVector::DistSquared(WorldLocation, ViewLocationWorld) <= FMath::Square(MaximumDistanceCentimeters);
		}
#endif
		return true;
	}

	return FVector::DistSquared(WorldLocation, ViewLocationWorld) <= FMath::Square(MaximumDistanceCentimeters);
}

bool FluidPipesShouldPrintSimulationFrameTiming()
{
	return GFluidPipesPrintSimulationFrameTiming.GetValueOnGameThread() != 0;
}

void FluidPipesPrintSimulationFrameTimingMessage(UObject* WorldContextObject, const FString& Message, const FLinearColor& TextColor)
{
	UKismetSystemLibrary::PrintString(WorldContextObject, Message, true, true, TextColor, 0.0f);
}
