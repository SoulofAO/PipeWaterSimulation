#include "Core/Actors/PipeFluidBenchmarkNetworkActor.h"

#include "Core/Actors/PipeFluidBasePointActor.h"
#include "Core/Actors/PipeFluidConsumerActor.h"
#include "Core/Actors/PipeFluidPipeActor.h"
#include "Core/Actors/PipeFluidPointActor.h"
#include "Core/Actors/PipeFluidPressureConsumerActor.h"
#include "Core/Actors/PipeFluidSourceActor.h"
#include "Engine/World.h"

APipeFluidBenchmarkNetworkActor::APipeFluidBenchmarkNetworkActor()
{
	PrimaryActorTick.bCanEverTick = false;
	PointActorClass = APipeFluidPointActor::StaticClass();
	SourceActorClass = APipeFluidSourceActor::StaticClass();
	ConsumerActorClass = APipeFluidConsumerActor::StaticClass();
	PressureConsumerActorClass = APipeFluidPressureConsumerActor::StaticClass();
	PipeActorClass = APipeFluidPipeActor::StaticClass();
}

void APipeFluidBenchmarkNetworkActor::BuildBenchmarkNetwork()
{
	ClearBenchmarkNetwork();

	TArray<APipeFluidBasePointActor*> SpawnedPoints;
	SpawnBenchmarkPoints(SpawnedPoints);
	SpawnBenchmarkPipes(SpawnedPoints);
}

void APipeFluidBenchmarkNetworkActor::ClearBenchmarkNetwork()
{
	for (AActor* SpawnedActor : SpawnedBenchmarkActors)
	{
		if (SpawnedActor && !SpawnedActor->IsActorBeingDestroyed())
		{
			SpawnedActor->Destroy();
		}
	}
	SpawnedBenchmarkActors.Reset();
}

void APipeFluidBenchmarkNetworkActor::SpawnBenchmarkPoints(TArray<APipeFluidBasePointActor*>& SpawnedPoints)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const int32 SafeGridSizeX = FMath::Max(2, GridSizeX);
	const int32 SafeGridSizeY = FMath::Max(2, GridSizeY);
	SpawnedPoints.SetNumZeroed(SafeGridSizeX * SafeGridSizeY);

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParameters.OverrideLevel = GetLevel();

	for (int32 PointIndexX = 0; PointIndexX < SafeGridSizeX; ++PointIndexX)
	{
		for (int32 PointIndexY = 0; PointIndexY < SafeGridSizeY; ++PointIndexY)
		{
			TSubclassOf<APipeFluidBasePointActor> PointClass = PointActorClass ? PointActorClass.Get() : APipeFluidPointActor::StaticClass();
			if (PointIndexX == 0 && PointIndexY == 0)
			{
				PointClass = SourceActorClass ? SourceActorClass.Get() : APipeFluidSourceActor::StaticClass();
			}
			else if (PointIndexX == SafeGridSizeX - 1 && PointIndexY == 0)
			{
				PointClass = ConsumerActorClass ? ConsumerActorClass.Get() : APipeFluidConsumerActor::StaticClass();
			}
			else if (PointIndexX == SafeGridSizeX - 1 && PointIndexY == SafeGridSizeY - 1)
			{
				PointClass = PressureConsumerActorClass ? PressureConsumerActorClass.Get() : APipeFluidPressureConsumerActor::StaticClass();
			}
			else if ((PointIndexX + PointIndexY) % 6 == 0)
			{
				PointClass = ConsumerActorClass ? ConsumerActorClass.Get() : APipeFluidConsumerActor::StaticClass();
			}
			else if ((PointIndexX + PointIndexY) % 6 == 3)
			{
				PointClass = PressureConsumerActorClass ? PressureConsumerActorClass.Get() : APipeFluidPressureConsumerActor::StaticClass();
			}

			APipeFluidBasePointActor* PointActor = World->SpawnActor<APipeFluidBasePointActor>(PointClass, BuildPointLocation(PointIndexX, PointIndexY), FRotator::ZeroRotator, SpawnParameters);
			if (!PointActor)
			{
				continue;
			}

			const int32 PointArrayIndex = BuildPointArrayIndex(PointIndexX, PointIndexY, SafeGridSizeY);
			SpawnedPoints[PointArrayIndex] = PointActor;
			PointActor->SceneNodeKey = PointArrayIndex;
			SpawnedBenchmarkActors.Add(PointActor);

			if (APipeFluidSourceActor* SourceActor = Cast<APipeFluidSourceActor>(PointActor))
			{
				SourceActor->SourceVolumeFlowRate = SourceVolumeFlowRate;
			}
			else if (APipeFluidConsumerActor* ConsumerActor = Cast<APipeFluidConsumerActor>(PointActor))
			{
				ConsumerActor->ConsumerVolumeFlowRateDemand = ConsumerVolumeFlowRateDemand;
			}
			else if (APipeFluidPressureConsumerActor* PressureConsumerActor = Cast<APipeFluidPressureConsumerActor>(PointActor))
			{
				PressureConsumerActor->ConsumerReferenceGaugePressure = PressureConsumerReferenceGaugePressure;
				PressureConsumerActor->ConsumerVolumeFlowRatePerGaugePressureExcess = PressureConsumerVolumeFlowRatePerGaugePressureExcess;
			}
		}
	}
}

void APipeFluidBenchmarkNetworkActor::SpawnBenchmarkPipes(const TArray<APipeFluidBasePointActor*>& SpawnedPoints)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const int32 SafeGridSizeX = FMath::Max(2, GridSizeX);
	const int32 SafeGridSizeY = FMath::Max(2, GridSizeY);

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParameters.OverrideLevel = GetLevel();

	int32 PipeSerial = 0;
	TSubclassOf<APipeFluidPipeActor> ResolvedPipeActorClass = PipeActorClass ? PipeActorClass.Get() : APipeFluidPipeActor::StaticClass();
	for (int32 PointIndexX = 0; PointIndexX < SafeGridSizeX; ++PointIndexX)
	{
		for (int32 PointIndexY = 0; PointIndexY < SafeGridSizeY; ++PointIndexY)
		{
			const int32 CurrentIndex = BuildPointArrayIndex(PointIndexX, PointIndexY, SafeGridSizeY);
			APipeFluidBasePointActor* CurrentPoint = SpawnedPoints.IsValidIndex(CurrentIndex) ? SpawnedPoints[CurrentIndex] : nullptr;
			if (!CurrentPoint)
			{
				continue;
			}

			if (PointIndexX + 1 < SafeGridSizeX)
			{
				const int32 RightIndex = BuildPointArrayIndex(PointIndexX + 1, PointIndexY, SafeGridSizeY);
				APipeFluidBasePointActor* RightPoint = SpawnedPoints.IsValidIndex(RightIndex) ? SpawnedPoints[RightIndex] : nullptr;
				if (RightPoint)
				{
					APipeFluidPipeActor* PipeActor = World->SpawnActor<APipeFluidPipeActor>(ResolvedPipeActorClass, GetActorLocation(), FRotator::ZeroRotator, SpawnParameters);
					if (PipeActor)
					{
						PipeActor->PipeEndpointFirst = CurrentPoint;
						PipeActor->PipeEndpointSecond = RightPoint;
						PipeActor->PipeSegmentName = FName(*FString::Format(TEXT("BenchmarkPipe_{0}"), { FString::FromInt(PipeSerial) }));
						PipeActor->SimulationCellCount = PipeSimulationCellCount;
#if WITH_EDITOR
						PipeActor->EditorRefreshFluidPipeAttachmentToAttachedEndpoints();
#endif
						SpawnedBenchmarkActors.Add(PipeActor);
						PipeSerial += 1;
					}
				}
			}

			if (PointIndexY + 1 < SafeGridSizeY)
			{
				const int32 UpIndex = BuildPointArrayIndex(PointIndexX, PointIndexY + 1, SafeGridSizeY);
				APipeFluidBasePointActor* UpPoint = SpawnedPoints.IsValidIndex(UpIndex) ? SpawnedPoints[UpIndex] : nullptr;
				if (UpPoint)
				{
					APipeFluidPipeActor* PipeActor = World->SpawnActor<APipeFluidPipeActor>(ResolvedPipeActorClass, GetActorLocation(), FRotator::ZeroRotator, SpawnParameters);
					if (PipeActor)
					{
						PipeActor->PipeEndpointFirst = CurrentPoint;
						PipeActor->PipeEndpointSecond = UpPoint;
						PipeActor->PipeSegmentName = FName(*FString::Format(TEXT("BenchmarkPipe_{0}"), { FString::FromInt(PipeSerial) }));
						PipeActor->SimulationCellCount = PipeSimulationCellCount;
#if WITH_EDITOR
						PipeActor->EditorRefreshFluidPipeAttachmentToAttachedEndpoints();
#endif
						SpawnedBenchmarkActors.Add(PipeActor);
						PipeSerial += 1;
					}
				}
			}
		}
	}
}

FVector APipeFluidBenchmarkNetworkActor::BuildPointLocation(const int32 PointIndexX, const int32 PointIndexY) const
{
	const FVector ActorLocation = GetActorLocation();
	const FVector LocalOffset = FVector(PointIndexX * PointSpacingWorldUnits, PointIndexY * PointSpacingWorldUnits, 0.0f);
	return ActorLocation + GetActorTransform().TransformVectorNoScale(LocalOffset);
}

int32 APipeFluidBenchmarkNetworkActor::BuildPointArrayIndex(const int32 PointIndexX, const int32 PointIndexY, const int32 SafeGridSizeY) const
{
	return PointIndexX * SafeGridSizeY + PointIndexY;
}
