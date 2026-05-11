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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidPipeScene")
	int32 SceneNodeKey = 0;

	virtual FFluidNetworkNodeStateZeroD ImportFluidNetworkNodeStateZeroD() const;

	virtual FFluidSegmentStateOneD ImportFluidSegmentStateOneDEndpoint(FFluidSegmentStateOneD Segment, bool bLeftEndpoint) const;
};
