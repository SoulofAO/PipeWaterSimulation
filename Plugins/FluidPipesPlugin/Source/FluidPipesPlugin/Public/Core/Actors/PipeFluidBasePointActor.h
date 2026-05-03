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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidPipeSceneZeroD")
	FFluidNetworkNodeStateZeroD ZeroDNetworkNodeState;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidPipeSceneOneD")
	EFluidOneDJunctionPressurePolicy OneDJunctionPressurePolicy = EFluidOneDJunctionPressurePolicy::AverageNeighborPressure;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidPipeSceneOneD")
	float OneDFixedJunctionPressure = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidPipeSceneOneD")
	float OneDExternalVolumeFlowRate = 0.0f;

	EFluidSceneEndpointKind GetSceneEndpointKind() const;

	void FillOneDJunctionTopologyDefaults(FFluidOneDJunctionTopologyOneD& JunctionTopology) const;

	virtual void PostEditMove(bool bFinished) override;

protected:
	EFluidSceneEndpointKind SceneEndpointKind = EFluidSceneEndpointKind::Face;
};
