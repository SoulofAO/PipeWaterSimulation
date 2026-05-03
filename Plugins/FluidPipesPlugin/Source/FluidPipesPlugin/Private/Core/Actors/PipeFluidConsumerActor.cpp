#include "Core/Actors/PipeFluidConsumerActor.h"

#include "Components/DynamicMeshComponent.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "UDynamicMesh.h"

APipeFluidConsumerActor::APipeFluidConsumerActor()
{
	SceneEndpointKind = EFluidSceneEndpointKind::Consumer;
}

void APipeFluidConsumerActor::RebuildFluidDynamicMesh()
{
	ClearFluidDynamicMeshGeometry();
	if (!FluidDynamicMeshComponent)
	{
		return;
	}

	UDynamicMesh* TargetMesh = FluidDynamicMeshComponent->GetDynamicMesh();
	if (!TargetMesh)
	{
		return;
	}

	FGeometryScriptPrimitiveOptions PrimitiveOptions;
	const float ConsumerCubeExtentWorldUnits = 22.0f;
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(TargetMesh, PrimitiveOptions, FTransform::Identity, ConsumerCubeExtentWorldUnits, ConsumerCubeExtentWorldUnits, ConsumerCubeExtentWorldUnits, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Center, nullptr);
}
