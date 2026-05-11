#pragma once

#include "CoreMinimal.h"
#include "Core/Actors/PipeFluidBasePointActor.h"
#include "PipeFluidConsumerActor.generated.h"

UCLASS()
class FLUIDPIPESPLUGIN_API APipeFluidConsumerActor : public APipeFluidBasePointActor
{
	GENERATED_BODY()

public:
	APipeFluidConsumerActor();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidPipeSceneConsumer")
	float ConsumerVolumeFlowRateDemand = 0.0f;

	virtual FFluidNetworkNodeStateZeroD ImportFluidNetworkNodeStateZeroD() const override;

	virtual FFluidSegmentStateOneD ImportFluidSegmentStateOneDEndpoint(FFluidSegmentStateOneD Segment, bool bLeftEndpoint) const override;

	virtual float EvaluateRuntimeZeroDimensionExternalVolumeFlowContribution(float NodeGaugePressure) const override;

	virtual float ComputeRuntimeSignedVolumeFlowRateForOneDimensionPipeBoundary(bool bLowAxisPipeAttachedEndpoint, float BoundaryAdjacentCellGaugePressure) const override;

protected:
	virtual void RebuildFluidDynamicMesh() override;
};
