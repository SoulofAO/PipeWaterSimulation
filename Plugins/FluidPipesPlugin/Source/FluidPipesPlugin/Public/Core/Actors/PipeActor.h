#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/DynamicMeshComponent.h"
#include "PipeActor.generated.h"

UCLASS(Abstract)
class FLUIDPIPESPLUGIN_API APipeActor : public AActor
{
	GENERATED_BODY()

public:
	APipeActor();

	virtual void OnConstruction(const FTransform& Transform) override;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FluidPipeVisual")
	TObjectPtr<UDynamicMeshComponent> FluidDynamicMeshComponent;

	virtual void RebuildFluidDynamicMesh();

	void ClearFluidDynamicMeshGeometry();
};
