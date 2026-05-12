#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PipeFluidBenchmarkNetworkActor.generated.h"

class APipeFluidBasePointActor;
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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidBenchmark", meta = (ClampMin = "2", UIMin = "2"))
	int32 GridSizeX = 16;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidBenchmark", meta = (ClampMin = "2", UIMin = "2"))
	int32 GridSizeY = 16;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidBenchmark", meta = (ClampMin = "100.0", UIMin = "100.0"))
	float PointSpacingWorldUnits = 350.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidBenchmark", meta = (ClampMin = "2", UIMin = "2"))
	int32 PipeSimulationCellCount = 12;

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

private:
	void SpawnBenchmarkPoints(TArray<APipeFluidBasePointActor*>& SpawnedPoints);
	void SpawnBenchmarkPipes(const TArray<APipeFluidBasePointActor*>& SpawnedPoints);
	FVector BuildPointLocation(int32 PointIndexX, int32 PointIndexY) const;
	int32 BuildPointArrayIndex(int32 PointIndexX, int32 PointIndexY, int32 SafeGridSizeY) const;

	UPROPERTY()
	TArray<TObjectPtr<AActor>> SpawnedBenchmarkActors;
};
