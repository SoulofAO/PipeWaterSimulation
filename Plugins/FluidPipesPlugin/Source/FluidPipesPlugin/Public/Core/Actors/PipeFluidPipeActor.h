#pragma once

#include "CoreMinimal.h"
#include "Core/Actors/PipeActor.h"
#include "Core/Actors/PipeFluidBasePointActor.h"
#include "PipeFluidPipeActor.generated.h"

UCLASS()
class FLUIDPIPESPLUGIN_API APipeFluidPipeActor : public APipeActor
{
	GENERATED_BODY()

public:
	APipeFluidPipeActor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void OnConstruction(const FTransform& Transform) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidPipeScene")
	TObjectPtr<APipeFluidBasePointActor> PipeEndpointFirst;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidPipeScene")
	TObjectPtr<APipeFluidBasePointActor> PipeEndpointSecond;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidPipeScenePhysics")
	float SegmentPhysicsLength = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidPipeScenePhysics", meta = (ClampMin = "2", UIMin = "2"))
	int32 SimulationCellCount = 10;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidPipeScenePhysics", meta = (ClampMin = "0.001", UIMin = "0.001"))
	float PipeDiameter = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidPipeScenePhysics", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float WaveSpeed = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidPipeScenePhysics", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float FrictionFactor = 0.02f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidPipeScenePhysics")
	float Density = 1000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidPipeScenePhysics", meta = (ClampMin = "0.000001", UIMin = "0.000001"))
	float EdgeResistance = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidPipeScenePhysics", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float EdgeInertance = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidPipeScenePhysics")
	float EdgeInitialFlowRate = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidPipeScenePhysics")
	float InitialCellPressure = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidPipeScenePhysics")
	float InitialCellFlowRate = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidPipeScenePhysics")
	FName PipeSegmentName = NAME_None;

protected:
	virtual void RebuildFluidDynamicMesh() override;

	void UpdateFluidPipeAttachmentToEndpoints();
	void RefreshObservedEndpointLocations();
	void UpdateEndpointFollowingTickEnabled();

	float CachedSegmentDistanceWorld = 100.0f;

	FVector LastObservedEndpointFirstWorld = FVector::ZeroVector;
	FVector LastObservedEndpointSecondWorld = FVector::ZeroVector;
};
