#pragma once

#include "CoreMinimal.h"
#include "Core/Actors/PipeFluidBasePointActor.h"
#include "PipeFluidPointActor.generated.h"

UCLASS()
class FLUIDPIPESPLUGIN_API APipeFluidPointActor : public APipeFluidBasePointActor
{
	GENERATED_BODY()

public:
	APipeFluidPointActor();

protected:
	virtual void RebuildFluidDynamicMesh() override;
};
