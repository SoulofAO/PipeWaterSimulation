#include "Core/Actors/PipeFluidSourceActor.h"

#include "Components/DynamicMeshComponent.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "UDynamicMesh.h"

APipeFluidSourceActor::APipeFluidSourceActor()
{
	SceneEndpointKind = EFluidSceneEndpointKind::Source;
}

void APipeFluidSourceActor::RebuildFluidDynamicMesh()
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

	const FTransform PrimitiveOrientationWorld(FQuat::FindBetweenNormals(FVector::UnitZ(), FVector::UnitX()), FVector::ZeroVector);
	FGeometryScriptPrimitiveOptions PrimitiveOptions;
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendCone(TargetMesh, PrimitiveOptions, PrimitiveOrientationWorld, 14.0f, 1.0f, 38.0f, 12, 4, true, EGeometryScriptPrimitiveOriginMode::Center, nullptr);
}
