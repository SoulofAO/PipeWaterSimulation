#include "FluidPipesDrawDebug.h"

#include "HAL/IConsoleManager.h"
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

int32 FluidPipesGetDrawDebugLevel()
{
	return GFluidPipesDrawDebugLevel.GetValueOnGameThread();
}

bool FluidPipesIsWorldLocationWithinDebugDrawDistance(const UWorld* World, const FVector& WorldLocation)
{
	if (!World)
	{
		return false;
	}

	const ULazyFluidPipesDeveloperSettings* Settings = GetDefault<ULazyFluidPipesDeveloperSettings>();
	const float MaximumDistanceCentimeters = Settings->WorldDebugMaximumDrawDistanceCentimeters;
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
		if (GEditor && GEditor->bIsSimulatingInEditor && GCurrentLevelEditingViewportClient)
		{
			return FVector::DistSquared(WorldLocation, ViewLocationWorld) <= FMath::Square(MaximumDistanceCentimeters);
		}
#endif
		return true;
	}

	return FVector::DistSquared(WorldLocation, ViewLocationWorld) <= FMath::Square(MaximumDistanceCentimeters);
}
