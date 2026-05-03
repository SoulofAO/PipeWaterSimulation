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

protected:
	virtual void RebuildFluidDynamicMesh() override;
};
