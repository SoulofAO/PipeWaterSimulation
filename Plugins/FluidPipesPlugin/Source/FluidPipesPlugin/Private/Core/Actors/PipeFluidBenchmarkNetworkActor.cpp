#include "Core/Actors/PipeFluidBenchmarkNetworkActor.h"

#include "Core/Actors/PipeFluidBasePointActor.h"
#include "HAL/PlatformTime.h"
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

bool APipeFluidBenchmarkNetworkActor::UsesBenchmarkPointCountMap() const
{
	for (const TPair<TSubclassOf<APipeFluidBasePointActor>, int32>& PointCountEntry : BenchmarkPointCountsByClass)
	{
		if (PointCountEntry.Value > 0)
		{
			return true;
		}
	}
	return false;
}

void APipeFluidBenchmarkNetworkActor::BuildBenchmarkPointClassQueue(TArray<TSubclassOf<APipeFluidBasePointActor>>& OutPointClassQueue) const
{
	OutPointClassQueue.Reset();

	auto AppendPointClassCount = [&OutPointClassQueue](TSubclassOf<APipeFluidBasePointActor> PointClass, int32 PointCount)
	{
		if (!PointClass || PointCount <= 0)
		{
			return;
		}

		OutPointClassQueue.Reserve(OutPointClassQueue.Num() + PointCount);
		for (int32 PointIndex = 0; PointIndex < PointCount; ++PointIndex)
		{
			OutPointClassQueue.Add(PointClass);
		}
	};

	TSet<TSubclassOf<APipeFluidBasePointActor>> ProcessedPointClasses;
	auto AppendFromMapIfPresent = [&](TSubclassOf<APipeFluidBasePointActor> PointClass)
	{
		if (!PointClass)
		{
			return;
		}

		const int32* PointCount = BenchmarkPointCountsByClass.Find(PointClass);
		if (!PointCount)
		{
			return;
		}

		AppendPointClassCount(PointClass, *PointCount);
		ProcessedPointClasses.Add(PointClass);
	};

	const TSubclassOf<APipeFluidBasePointActor> ResolvedSourceClass = SourceActorClass ? SourceActorClass.Get() : APipeFluidSourceActor::StaticClass();
	const TSubclassOf<APipeFluidBasePointActor> ResolvedConsumerClass = ConsumerActorClass ? ConsumerActorClass.Get() : APipeFluidConsumerActor::StaticClass();
	const TSubclassOf<APipeFluidBasePointActor> ResolvedPressureConsumerClass = PressureConsumerActorClass ? PressureConsumerActorClass.Get() : APipeFluidPressureConsumerActor::StaticClass();
	const TSubclassOf<APipeFluidBasePointActor> ResolvedPointClass = PointActorClass ? PointActorClass.Get() : APipeFluidPointActor::StaticClass();

	AppendFromMapIfPresent(ResolvedSourceClass);
	AppendFromMapIfPresent(ResolvedConsumerClass);
	AppendFromMapIfPresent(ResolvedPressureConsumerClass);
	AppendFromMapIfPresent(ResolvedPointClass);

	TArray<TSubclassOf<APipeFluidBasePointActor>> RemainingPointClasses;
	for (const TPair<TSubclassOf<APipeFluidBasePointActor>, int32>& PointCountEntry : BenchmarkPointCountsByClass)
	{
		if (!PointCountEntry.Key || ProcessedPointClasses.Contains(PointCountEntry.Key))
		{
			continue;
		}

		RemainingPointClasses.Add(PointCountEntry.Key);
	}

	RemainingPointClasses.Sort(
		[](const TSubclassOf<APipeFluidBasePointActor>& LeftClass, const TSubclassOf<APipeFluidBasePointActor>& RightClass)
		{
			const FString LeftClassName = LeftClass ? LeftClass->GetName() : FString();
			const FString RightClassName = RightClass ? RightClass->GetName() : FString();
			return LeftClassName < RightClassName;
		});

	for (const TSubclassOf<APipeFluidBasePointActor>& RemainingPointClass : RemainingPointClasses)
	{
		const int32* PointCount = BenchmarkPointCountsByClass.Find(RemainingPointClass);
		if (!PointCount)
		{
			continue;
		}

		AppendPointClassCount(RemainingPointClass, *PointCount);
	}
}

void APipeFluidBenchmarkNetworkActor::BuildRandomizedBenchmarkPointClassGrid(TArray<TSubclassOf<APipeFluidBasePointActor>>& OutGridPointClasses, const int32 TotalGridPointCount) const
{
	const TSubclassOf<APipeFluidBasePointActor> DefaultPointClass = PointActorClass ? PointActorClass.Get() : APipeFluidPointActor::StaticClass();
	OutGridPointClasses.Init(DefaultPointClass, TotalGridPointCount);

	TArray<TSubclassOf<APipeFluidBasePointActor>> PointClassQueue;
	BuildBenchmarkPointClassQueue(PointClassQueue);
	if (PointClassQueue.Num() == 0)
	{
		return;
	}

	if (PointClassQueue.Num() > TotalGridPointCount)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("BenchmarkPointCountsByClass specifies %d points but grid has %d. Extra points are ignored."),
			PointClassQueue.Num(),
			TotalGridPointCount);
		PointClassQueue.SetNum(TotalGridPointCount);
	}

	const uint32 RandomSeed = BenchmarkPointPlacementRandomSeed != 0
		? static_cast<uint32>(BenchmarkPointPlacementRandomSeed)
		: static_cast<uint32>(FPlatformTime::Cycles64());
	FRandomStream RandomStream(RandomSeed);

	auto RandomShuffleArray = [&RandomStream](auto& Array)
	{
		for (int32 ShuffleIndex = Array.Num() - 1; ShuffleIndex > 0; --ShuffleIndex)
		{
			const int32 SwapIndex = RandomStream.RandRange(0, ShuffleIndex);
			Array.Swap(ShuffleIndex, SwapIndex);
		}
	};

	RandomShuffleArray(PointClassQueue);

	TArray<int32> GridCellIndices;
	GridCellIndices.Reserve(TotalGridPointCount);
	for (int32 GridCellIndex = 0; GridCellIndex < TotalGridPointCount; ++GridCellIndex)
	{
		GridCellIndices.Add(GridCellIndex);
	}
	RandomShuffleArray(GridCellIndices);

	for (int32 SpecialPointIndex = 0; SpecialPointIndex < PointClassQueue.Num(); ++SpecialPointIndex)
	{
		const int32 GridCellIndex = GridCellIndices[SpecialPointIndex];
		OutGridPointClasses[GridCellIndex] = PointClassQueue[SpecialPointIndex];
	}
}

TSubclassOf<APipeFluidBasePointActor> APipeFluidBenchmarkNetworkActor::ResolveLegacyBenchmarkPointClass(const int32 PointIndexX, const int32 PointIndexY, const int32 BenchmarkGridSizeX, const int32 BenchmarkGridSizeY) const
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

	return PointClass;
}

void APipeFluidBenchmarkNetworkActor::ConfigureSpawnedBenchmarkPoint(APipeFluidBasePointActor* PointActor) const
{
	if (!PointActor)
	{
		return;
	}

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

void APipeFluidBenchmarkNetworkActor::SpawnBenchmarkPoints(TArray<APipeFluidBasePointActor*>& SpawnedPoints)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const int32 BenchmarkGridSizeX = FMath::Max(1, GridSizeX);
	const int32 BenchmarkGridSizeY = FMath::Max(1, GridSizeY);
	const int32 TotalGridPointCount = BenchmarkGridSizeX * BenchmarkGridSizeY;
	SpawnedPoints.SetNumZeroed(TotalGridPointCount);

	const bool bUsePointCountMap = UsesBenchmarkPointCountMap();
	TArray<TSubclassOf<APipeFluidBasePointActor>> GridPointClasses;
	if (bUsePointCountMap)
	{
		BuildRandomizedBenchmarkPointClassGrid(GridPointClasses, TotalGridPointCount);
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParameters.OverrideLevel = GetLevel();

	int32 GridLinearIndex = 0;
	for (int32 PointIndexX = 0; PointIndexX < BenchmarkGridSizeX; ++PointIndexX)
	{
		for (int32 PointIndexY = 0; PointIndexY < BenchmarkGridSizeY; ++PointIndexY)
		{
			const TSubclassOf<APipeFluidBasePointActor> PointClass = bUsePointCountMap
				? GridPointClasses[GridLinearIndex]
				: ResolveLegacyBenchmarkPointClass(PointIndexX, PointIndexY, BenchmarkGridSizeX, BenchmarkGridSizeY);

			APipeFluidBasePointActor* PointActor = World->SpawnActor<APipeFluidBasePointActor>(PointClass, BuildPointLocation(PointIndexX, PointIndexY), FRotator::ZeroRotator, SpawnParameters);
			GridLinearIndex += 1;
			if (!PointActor)
			{
				continue;
			}

			const int32 PointArrayIndex = BuildPointArrayIndex(PointIndexX, PointIndexY, BenchmarkGridSizeY);
			SpawnedPoints[PointArrayIndex] = PointActor;
			PointActor->SceneNodeKey = PointArrayIndex;
			SpawnedBenchmarkActors.Add(PointActor);
			ConfigureSpawnedBenchmarkPoint(PointActor);
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
	if (!UsesBenchmarkPointCountMap())
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

	TArray<FString> PointCountDescriptions;
	for (const TPair<TSubclassOf<APipeFluidBasePointActor>, int32>& PointCountEntry : BenchmarkPointCountsByClass)
	{
		if (!PointCountEntry.Key || PointCountEntry.Value <= 0)
		{
			continue;
		}

		PointCountDescriptions.Add(FString::Format(TEXT("{0}={1}"), { PointCountEntry.Key->GetName(), FString::FromInt(PointCountEntry.Value) }));
	}

	PointCountDescriptions.Sort();
	const FString PointCountSummary = FString::Join(PointCountDescriptions, TEXT(", "));

	return FString::Format(
		TEXT("{0} ({1}x{2}, cells={3}, points=[{4}])"),
		{
			GetActorLabel(),
			FString::FromInt(GridSizeX),
			FString::FromInt(GridSizeY),
			FString::FromInt(PipeSimulationCellCount),
			PointCountSummary
		});
}
