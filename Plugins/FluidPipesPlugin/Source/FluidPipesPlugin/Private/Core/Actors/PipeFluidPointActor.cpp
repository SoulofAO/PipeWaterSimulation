#include "Core/Actors/PipeFluidPointActor.h"

#include "Components/DynamicMeshComponent.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "UDynamicMesh.h"

APipeFluidPointActor::APipeFluidPointActor()
{
	SceneEndpointKind = EFluidSceneEndpointKind::Face;
}

void APipeFluidPointActor::RebuildFluidDynamicMesh()
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
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendSphereLatLong(TargetMesh, PrimitiveOptions, FTransform::Identity, 14.0f, 8, 14, EGeometryScriptPrimitiveOriginMode::Center, nullptr);
}
