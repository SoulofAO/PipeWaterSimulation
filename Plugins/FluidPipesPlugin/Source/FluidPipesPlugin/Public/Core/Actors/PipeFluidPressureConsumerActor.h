#pragma once

#include "CoreMinimal.h"
#include "Core/Actors/PipeFluidBasePointActor.h"
#include "PipeFluidPressureConsumerActor.generated.h"

UCLASS()
class FLUIDPIPESPLUGIN_API APipeFluidPressureConsumerActor : public APipeFluidBasePointActor
{
	GENERATED_BODY()

public:
	APipeFluidPressureConsumerActor();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidPipeScenePressureConsumer")
	float ConsumerReferenceGaugePressure = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidPipeScenePressureConsumer", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float ConsumerVolumeFlowRatePerGaugePressureExcess = 0.0001f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidPipeScenePressureConsumer", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MinimumPressureConsumerVolumeFlowRate = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidPipeScenePressureConsumer", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MaximumPressureConsumerVolumeFlowRate = 1000000.0f;

	virtual FFluidNetworkNodeStateZeroD ImportFluidNetworkNodeStateZeroD() const override;

	virtual FFluidSegmentStateOneD ImportFluidSegmentStateOneDEndpoint(FFluidSegmentStateOneD Segment, bool bLeftEndpoint) const override;

	virtual float EvaluateRuntimeZeroDimensionExternalVolumeFlowContribution(float NodeGaugePressure) const override;

	virtual float ComputeRuntimeSignedVolumeFlowRateForOneDimensionPipeBoundary(bool bLowAxisPipeAttachedEndpoint, float BoundaryAdjacentCellGaugePressure) const override;

protected:
	virtual void RebuildFluidDynamicMesh() override;

private:
	float ComputePressureConsumerVolumeFlowRateMagnitudeForGaugePressure(float GaugePressure) const;
};
