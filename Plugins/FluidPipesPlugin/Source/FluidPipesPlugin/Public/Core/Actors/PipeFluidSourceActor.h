#pragma once

#include "CoreMinimal.h"
#include "Core/Actors/PipeFluidBasePointActor.h"
#include "PipeFluidSourceActor.generated.h"

UCLASS()
class FLUIDPIPESPLUGIN_API APipeFluidSourceActor : public APipeFluidBasePointActor
{
	GENERATED_BODY()

public:
	APipeFluidSourceActor();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidPipeSceneSource")
	float SourceVolumeFlowRate = 0.0f;

	virtual FFluidNetworkNodeStateZeroD ImportFluidNetworkNodeStateZeroD() const override;

	virtual FFluidSegmentStateOneD ImportFluidSegmentStateOneDEndpoint(FFluidSegmentStateOneD Segment, bool bLeftEndpoint) const override;

protected:
	virtual void RebuildFluidDynamicMesh() override;
};
