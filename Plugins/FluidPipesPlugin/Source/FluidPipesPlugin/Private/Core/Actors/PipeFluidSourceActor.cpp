#include "Core/Actors/PipeFluidSourceActor.h"

#include "Components/DynamicMeshComponent.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "UDynamicMesh.h"

APipeFluidSourceActor::APipeFluidSourceActor()
{
}

FFluidNetworkNodeStateZeroD APipeFluidSourceActor::ImportFluidNetworkNodeStateZeroD() const
{
	FFluidNetworkNodeStateZeroD FluidImportedNetworkNodeStateZeroD;
	FluidImportedNetworkNodeStateZeroD.NodeName = FName(*FString::Format(TEXT("{0}"), { FString::FromInt(SceneNodeKey) }));
	return FluidImportedNetworkNodeStateZeroD;
}

FFluidSegmentStateOneD APipeFluidSourceActor::ImportFluidSegmentStateOneDEndpoint(FFluidSegmentStateOneD Segment, bool bLeftEndpoint) const
{
	if (bLeftEndpoint)
	{
		Segment.LeftBoundaryConditionType = EFluidBoundaryConditionTypeOneD::FixedPressure;
		Segment.LeftBoundaryPressure = 0.0f;
	}
	else
	{
		Segment.RightBoundaryConditionType = EFluidBoundaryConditionTypeOneD::FixedPressure;
		Segment.RightBoundaryPressure = 0.0f;
	}
	return Segment;
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
