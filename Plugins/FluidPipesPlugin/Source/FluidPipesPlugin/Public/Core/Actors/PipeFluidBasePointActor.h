#pragma once

#include "CoreMinimal.h"
#include "Core/Actors/PipeActor.h"
#include "Data/FluidData.h"
#include "PipeFluidBasePointActor.generated.h"

UCLASS(Abstract)
class FLUIDPIPESPLUGIN_API APipeFluidBasePointActor : public APipeActor
{
	GENERATED_BODY()

public:
	virtual void PostActorCreated() override;

	virtual void PostDuplicate(EDuplicateMode::Type DuplicateMode) override;

#if WITH_EDITOR
	virtual void PostEditMove(bool bFinished) override;
#endif

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FluidPipeScene")
	int32 SceneNodeKey = INDEX_NONE;

	virtual FFluidNetworkNodeStateZeroD ImportFluidNetworkNodeStateZeroD() const;

	virtual FFluidSegmentStateOneD ImportFluidSegmentStateOneDEndpoint(FFluidSegmentStateOneD Segment, bool bLeftEndpoint) const;

	virtual float EvaluateRuntimeZeroDimensionExternalVolumeFlowContribution(float NodeGaugePressure) const;

	virtual float ComputeRuntimeSignedVolumeFlowRateForOneDimensionPipeBoundary(bool bLowAxisPipeAttachedEndpoint, float BoundaryAdjacentCellGaugePressure) const;
};
