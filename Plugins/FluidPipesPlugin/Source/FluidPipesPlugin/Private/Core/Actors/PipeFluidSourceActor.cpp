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
	FluidImportedNetworkNodeStateZeroD.SourceFlow = FMath::Max(0.0f, SourceVolumeFlowRate);
	return FluidImportedNetworkNodeStateZeroD;
}

FFluidSegmentStateOneD APipeFluidSourceActor::ImportFluidSegmentStateOneDEndpoint(FFluidSegmentStateOneD Segment, bool bLeftEndpoint) const
{
	const float SupplyMagnitude = FMath::Max(0.0f, SourceVolumeFlowRate);
	if (SupplyMagnitude <= KINDA_SMALL_NUMBER)
	{
		return Segment;
	}

	if (bLeftEndpoint)
	{
		Segment.LeftBoundaryConditionType = EFluidBoundaryConditionTypeOneD::FixedFlow;
		Segment.LeftBoundaryFlow = SupplyMagnitude;
	}
	else
	{
		Segment.RightBoundaryConditionType = EFluidBoundaryConditionTypeOneD::FixedFlow;
		Segment.RightBoundaryFlow = -SupplyMagnitude;
	}
	return Segment;
}

float APipeFluidSourceActor::EvaluateRuntimeZeroDimensionExternalVolumeFlowContribution(float) const
{
	return FMath::Max(0.0f, SourceVolumeFlowRate);
}

float APipeFluidSourceActor::ComputeRuntimeSignedVolumeFlowRateForOneDimensionPipeBoundary(bool bLowAxisPipeAttachedEndpoint, float) const
{
	const float SupplyMagnitude = FMath::Max(0.0f, SourceVolumeFlowRate);
	if (SupplyMagnitude <= KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}
	return bLowAxisPipeAttachedEndpoint ? SupplyMagnitude : -SupplyMagnitude;
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
