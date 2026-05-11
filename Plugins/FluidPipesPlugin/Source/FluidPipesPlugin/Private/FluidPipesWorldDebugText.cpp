#include "FluidPipesWorldDebugText.h"

#include "CoreGlobals.h"
#include "Debug/DebugDrawService.h"
#include "DebugRenderSceneProxy.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Other/LazyFluidPipesDeveloperSettings.h"
#include "SceneView.h"

struct FFluidPipesQueuedWorldStringRow
{
	FVector WorldLocation = FVector::ZeroVector;
	FString DisplayText;
	FColor TextColor = FColor::White;
	float FontScale = 1.0f;
};

static TMap<TWeakObjectPtr<UWorld>, TArray<FFluidPipesQueuedWorldStringRow>> GFluidPipesWorldDebugTextRows;
static TMap<const void*, uint64> GFluidPipesWorldDebugLastDrawFrameByCanvas;
static FDelegateHandle GFluidPipesWorldDebugTextDrawServiceHandle;
static FDelegateHandle GFluidPipesWorldDebugTextWorldCleanupHandle;

static void FluidPipesWorldDebugTextOnWorldCleanup(UWorld* World, bool, bool)
{
	if (!World)
	{
		return;
	}
	GFluidPipesWorldDebugTextRows.Remove(TWeakObjectPtr<UWorld>(World));
}

static void FluidPipesWorldDebugTextDrawServiceCallback(UCanvas* Canvas, APlayerController*)
{
	if (!Canvas || !Canvas->Canvas || !Canvas->SceneView)
	{
		return;
	}
	UWorld* CanvasWorld = Canvas->Canvas->GetScene()->GetWorld();
	if (!CanvasWorld)
	{
		return;
	}
	TArray<FFluidPipesQueuedWorldStringRow>* FoundRows = GFluidPipesWorldDebugTextRows.Find(TWeakObjectPtr<UWorld>(CanvasWorld));
	if (!FoundRows || FoundRows->Num() == 0)
	{
		return;
	}
	const void* CanvasIdentity = static_cast<const void*>(Canvas->Canvas);
	uint64& LastDrawFrameForCanvas = GFluidPipesWorldDebugLastDrawFrameByCanvas.FindOrAdd(CanvasIdentity);
	if (LastDrawFrameForCanvas == GFrameCounter)
	{
		return;
	}
	LastDrawFrameForCanvas = GFrameCounter;

	const ULazyFluidPipesDeveloperSettings* Settings = GetDefault<ULazyFluidPipesDeveloperSettings>();
	const FSceneView* SceneView = Canvas->SceneView;
	const float MaximumDistanceCentimeters = Settings->WorldDebugMaximumDrawDistanceCentimeters;
	const float MaximumDistanceSquared = MaximumDistanceCentimeters > KINDA_SMALL_NUMBER ? FMath::Square(MaximumDistanceCentimeters) : TNumericLimits<float>::Max();

	UFont* RenderFont = GEngine ? GEngine->GetSmallFont() : nullptr;
	if (!RenderFont)
	{
		return;
	}
	const FFontRenderInfo FontRenderInfo = Canvas->CreateFontRenderInfo(true, false);
	const FColor PreviousDrawColor = Canvas->DrawColor;
	const FVector ViewOriginWorld = SceneView->ViewLocation;

	for (const FFluidPipesQueuedWorldStringRow& Row : *FoundRows)
	{
		const float DistanceSquared = FVector::DistSquared(Row.WorldLocation, ViewOriginWorld);
		if (DistanceSquared > MaximumDistanceSquared)
		{
			continue;
		}
		FVector2D ScreenPositionProbe;
		if (!FSceneView::ProjectWorldToScreen(Row.WorldLocation, SceneView->UnscaledViewRect, SceneView->ViewMatrices.GetViewProjectionMatrix(), ScreenPositionProbe, false))
		{
			continue;
		}
		const FVector3f ScreenLocation = UE::DebugDrawHelper::GetScaleAdjustedScreenLocation(Canvas, Row.WorldLocation);

		float EffectiveFontScale = Row.FontScale;
		if (Settings->WorldDebugPerspectiveFontScaling)
		{
			const float DistanceCentimeters = FMath::Sqrt(FMath::Max(DistanceSquared, 25.0f));
			const float ReferenceDistanceCentimeters = FMath::Max(Settings->WorldDebugPerspectiveFontReferenceDistanceCentimeters, 50.0f);
			const float MinimumMultiplier = Settings->WorldDebugPerspectiveFontMinimumMultiplier;
			const float MaximumMultiplier = FMath::Max(Settings->WorldDebugPerspectiveFontMaximumMultiplier, MinimumMultiplier + 0.01f);
			const float InverseDistanceScale = ReferenceDistanceCentimeters / DistanceCentimeters;
			const float FarFadeStartSquared = FMath::Square(ReferenceDistanceCentimeters * 2.5f);
			const float FarFadeEndSquared = MaximumDistanceSquared;
			float DistanceMultiplier = FMath::Clamp(InverseDistanceScale, MinimumMultiplier, MaximumMultiplier);
			if (FarFadeEndSquared > FarFadeStartSquared + 1.0f && DistanceSquared > FarFadeStartSquared)
			{
				const float FadeAlpha = FMath::Clamp((DistanceSquared - FarFadeStartSquared) / (FarFadeEndSquared - FarFadeStartSquared), 0.0f, 1.0f);
				const float SmoothFade = FadeAlpha * FadeAlpha * (3.0f - 2.0f * FadeAlpha);
				DistanceMultiplier = FMath::Lerp(DistanceMultiplier, MinimumMultiplier, SmoothFade);
			}
			EffectiveFontScale = Row.FontScale * DistanceMultiplier;
		}

		Canvas->SetDrawColor(Row.TextColor);
		Canvas->DrawText(RenderFont, Row.DisplayText, ScreenLocation.X, ScreenLocation.Y, EffectiveFontScale, EffectiveFontScale, FontRenderInfo);
	}
	Canvas->SetDrawColor(PreviousDrawColor);
}

void FluidPipesWorldDebugTextStartup()
{
	if (GFluidPipesWorldDebugTextDrawServiceHandle.IsValid())
	{
		return;
	}
	GFluidPipesWorldDebugTextWorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddStatic(FluidPipesWorldDebugTextOnWorldCleanup);
	GFluidPipesWorldDebugTextDrawServiceHandle = UDebugDrawService::Register(TEXT("Tonemapper"), FDebugDrawDelegate::CreateStatic(FluidPipesWorldDebugTextDrawServiceCallback));
}

void FluidPipesWorldDebugTextShutdown()
{
	if (GFluidPipesWorldDebugTextDrawServiceHandle.IsValid())
	{
		UDebugDrawService::Unregister(GFluidPipesWorldDebugTextDrawServiceHandle);
		GFluidPipesWorldDebugTextDrawServiceHandle = FDelegateHandle();
	}
	if (GFluidPipesWorldDebugTextWorldCleanupHandle.IsValid())
	{
		FWorldDelegates::OnWorldCleanup.Remove(GFluidPipesWorldDebugTextWorldCleanupHandle);
		GFluidPipesWorldDebugTextWorldCleanupHandle = FDelegateHandle();
	}
	GFluidPipesWorldDebugTextRows.Empty();
	GFluidPipesWorldDebugLastDrawFrameByCanvas.Empty();
}

void FluidPipesWorldDebugTextClearWorld(UWorld* World)
{
	if (!World)
	{
		return;
	}
	if (TArray<FFluidPipesQueuedWorldStringRow>* FoundRows = GFluidPipesWorldDebugTextRows.Find(TWeakObjectPtr<UWorld>(World)))
	{
		FoundRows->Reset();
	}
}

void FluidPipesWorldDebugTextQueueString(UWorld* World, FVector WorldLocation, const FString& DisplayText, FColor TextColor, float FontScale)
{
	if (!World)
	{
		return;
	}
	TArray<FFluidPipesQueuedWorldStringRow>& Rows = GFluidPipesWorldDebugTextRows.FindOrAdd(TWeakObjectPtr<UWorld>(World));
	FFluidPipesQueuedWorldStringRow Row;
	Row.WorldLocation = WorldLocation;
	Row.DisplayText = DisplayText;
	Row.TextColor = TextColor;
	Row.FontScale = FontScale;
	Rows.Add(MoveTemp(Row));
}
