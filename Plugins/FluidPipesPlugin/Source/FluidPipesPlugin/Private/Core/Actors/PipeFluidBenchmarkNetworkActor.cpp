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

	const int32 BenchmarkGridSizeX = FMath::Max(1, GridSizeX);
	const int32 BenchmarkGridSizeY = FMath::Max(1, GridSizeY);
	SpawnedPoints.SetNumZeroed(BenchmarkGridSizeX * BenchmarkGridSizeY);

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParameters.OverrideLevel = GetLevel();

	for (int32 PointIndexX = 0; PointIndexX < BenchmarkGridSizeX; ++PointIndexX)
	{
		for (int32 PointIndexY = 0; PointIndexY < BenchmarkGridSizeY; ++PointIndexY)
		{
			TSubclassOf<APipeFluidBasePointActor> PointClass = PointActorClass ? PointActorClass.Get() : APipeFluidPointActor::StaticClass();
			if (PointIndexX == 0 && PointIndexY == 0)
			{
				PointClass = SourceActorClass ? SourceActorClass.Get() : APipeFluidSourceActor::StaticClass();
			}
			else if (BenchmarkGridSizeX == 1 && BenchmarkGridSizeY > 1 && PointIndexY == BenchmarkGridSizeY - 1)
			{
				PointClass = PressureConsumerActorClass ? PressureConsumerActorClass.Get() : APipeFluidPressureConsumerActor::StaticClass();
			}
			else if (BenchmarkGridSizeY == 1 && PointIndexX == BenchmarkGridSizeX - 1)
			{
				PointClass = ConsumerActorClass ? ConsumerActorClass.Get() : APipeFluidConsumerActor::StaticClass();
			}
			else if (PointIndexX == BenchmarkGridSizeX - 1 && PointIndexY == 0 && BenchmarkGridSizeX > 1)
			{
				PointClass = ConsumerActorClass ? ConsumerActorClass.Get() : APipeFluidConsumerActor::StaticClass();
			}
			else if (PointIndexX == BenchmarkGridSizeX - 1 && PointIndexY == BenchmarkGridSizeY - 1 && BenchmarkGridSizeX > 1 && BenchmarkGridSizeY > 1)
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

			const int32 PointArrayIndex = BuildPointArrayIndex(PointIndexX, PointIndexY, BenchmarkGridSizeY);
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

	const int32 BenchmarkGridSizeX = FMath::Max(1, GridSizeX);
	const int32 BenchmarkGridSizeY = FMath::Max(1, GridSizeY);

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParameters.OverrideLevel = GetLevel();

	int32 PipeSerial = 0;
	TSubclassOf<APipeFluidPipeActor> ResolvedPipeActorClass = PipeActorClass ? PipeActorClass.Get() : APipeFluidPipeActor::StaticClass();
	for (int32 PointIndexX = 0; PointIndexX < BenchmarkGridSizeX; ++PointIndexX)
	{
		for (int32 PointIndexY = 0; PointIndexY < BenchmarkGridSizeY; ++PointIndexY)
		{
			const int32 CurrentIndex = BuildPointArrayIndex(PointIndexX, PointIndexY, BenchmarkGridSizeY);
			APipeFluidBasePointActor* CurrentPoint = SpawnedPoints.IsValidIndex(CurrentIndex) ? SpawnedPoints[CurrentIndex] : nullptr;
			if (!CurrentPoint)
			{
				continue;
			}

			if (PointIndexX + 1 < BenchmarkGridSizeX)
			{
				const int32 RightIndex = BuildPointArrayIndex(PointIndexX + 1, PointIndexY, BenchmarkGridSizeY);
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

			if (PointIndexY + 1 < BenchmarkGridSizeY)
			{
				const int32 UpIndex = BuildPointArrayIndex(PointIndexX, PointIndexY + 1, BenchmarkGridSizeY);
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

int32 APipeFluidBenchmarkNetworkActor::BuildPointArrayIndex(const int32 PointIndexX, const int32 PointIndexY, const int32 BenchmarkGridSizeY) const
{
	return PointIndexX * BenchmarkGridSizeY + PointIndexY;
}

FString APipeFluidBenchmarkNetworkActor::BuildNetworkDescription() const
{
	return FString::Format(
		TEXT("{0} ({1}x{2}, cells={3})"),
		{
			GetActorLabel(),
			FString::FromInt(GridSizeX),
			FString::FromInt(GridSizeY),
			FString::FromInt(PipeSimulationCellCount)
		});
}
