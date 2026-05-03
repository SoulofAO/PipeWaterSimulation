#include "Core/Actors/PipeActor.h"

#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"

void APipeActor::ClearFluidDynamicMeshGeometry()
{
	if (!FluidDynamicMeshComponent)
	{
		return;
	}

	FluidDynamicMeshComponent->EditMesh([](UE::Geometry::FDynamicMesh3& Mesh)
		{
			Mesh.Clear();
		});
}

APipeActor::APipeActor()
{
	PrimaryActorTick.bCanEverTick = false;
	FluidDynamicMeshComponent = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("FluidDynamicMeshComponent"));
	SetRootComponent(FluidDynamicMeshComponent);
}

void APipeActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildFluidDynamicMesh();
}

void APipeActor::RebuildFluidDynamicMesh()
{
}
