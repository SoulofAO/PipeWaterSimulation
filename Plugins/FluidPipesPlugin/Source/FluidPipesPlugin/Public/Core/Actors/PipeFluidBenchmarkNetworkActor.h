#pragma once

#include "CoreMinimal.h"
#include "Core/Actors/PipeFluidBasePointActor.h"
#include "GameFramework/Actor.h"
#include "PipeFluidBenchmarkNetworkActor.generated.h"

class APipeFluidConsumerActor;
class APipeFluidPipeActor;
class APipeFluidPointActor;
class APipeFluidPressureConsumerActor;
class APipeFluidSourceActor;

UCLASS()
class FLUIDPIPESPLUGIN_API APipeFluidBenchmarkNetworkActor : public AActor
{
	GENERATED_BODY()

public:
	APipeFluidBenchmarkNetworkActor();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidBenchmark|Classes")
	TSubclassOf<APipeFluidPointActor> PointActorClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidBenchmark|Classes")
	TSubclassOf<APipeFluidSourceActor> SourceActorClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidBenchmark|Classes")
	TSubclassOf<APipeFluidConsumerActor> ConsumerActorClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidBenchmark|Classes")
	TSubclassOf<APipeFluidPressureConsumerActor> PressureConsumerActorClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidBenchmark|Classes")
	TSubclassOf<APipeFluidPipeActor> PipeActorClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidBenchmark", meta = (ClampMin = "1", UIMin = "1"))
	int32 GridSizeX = 16;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidBenchmark", meta = (ClampMin = "1", UIMin = "1"))
	int32 GridSizeY = 16;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidBenchmark", meta = (ClampMin = "100.0", UIMin = "100.0"))
	float PointSpacingWorldUnits = 350.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidBenchmark", meta = (ClampMin = "2", UIMin = "2"))
	int32 PipeSimulationCellCount = 12;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidBenchmark", meta = (Tooltip = "When any count is greater than zero, points from this map are placed on random grid cells. Remaining cells use PointActorClass. Empty map keeps the legacy placement heuristic."))
	TMap<TSubclassOf<APipeFluidBasePointActor>, int32> BenchmarkPointCountsByClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidBenchmark", meta = (Tooltip = "Random seed for BenchmarkPointCountsByClass placement. Zero uses a new seed on each build."))
	int32 BenchmarkPointPlacementRandomSeed = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidBenchmark")
	float SourceVolumeFlowRate = 0.015f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidBenchmark")
	float ConsumerVolumeFlowRateDemand = 0.012f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidBenchmark")
	float PressureConsumerReferenceGaugePressure = 1200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidBenchmark")
	float PressureConsumerVolumeFlowRatePerGaugePressureExcess = 0.0002f;

	UFUNCTION(CallInEditor, Category = "FluidBenchmark")
	void BuildBenchmarkNetwork();

	UFUNCTION(CallInEditor, Category = "FluidBenchmark")
	void ClearBenchmarkNetwork();

	FString BuildNetworkDescription() const;

private:
	void SpawnBenchmarkPoints(TArray<APipeFluidBasePointActor*>& SpawnedPoints);
	void SpawnBenchmarkPipes(const TArray<APipeFluidBasePointActor*>& SpawnedPoints);
	void ConfigureSpawnedBenchmarkPoint(APipeFluidBasePointActor* PointActor) const;
	TSubclassOf<APipeFluidBasePointActor> ResolveLegacyBenchmarkPointClass(int32 PointIndexX, int32 PointIndexY, int32 BenchmarkGridSizeX, int32 BenchmarkGridSizeY) const;
	bool UsesBenchmarkPointCountMap() const;
	void BuildBenchmarkPointClassQueue(TArray<TSubclassOf<APipeFluidBasePointActor>>& OutPointClassQueue) const;
	void BuildRandomizedBenchmarkPointClassGrid(TArray<TSubclassOf<APipeFluidBasePointActor>>& OutGridPointClasses, int32 TotalGridPointCount) const;
	FVector BuildPointLocation(int32 PointIndexX, int32 PointIndexY) const;
	int32 BuildPointArrayIndex(int32 PointIndexX, int32 PointIndexY, int32 BenchmarkGridSizeY) const;

	UPROPERTY()
	TArray<TObjectPtr<AActor>> SpawnedBenchmarkActors;
};
