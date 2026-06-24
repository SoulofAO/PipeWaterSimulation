#pragma once

#include "CoreMinimal.h"
#include "Data/FluidData.h"

class APipeFluidPipeActor;
class UFluidNetwork0DSubsystem;
class UFluidSegment1DSubsystem;
class ULazyFluidPipesDeveloperSettings;
class UWorld;

struct FFluidHybridNetworkTopology
{
	TMap<int32, int32> SceneNodeKeyToZeroDNodeIndex;
	TArray<int32> ZeroDNodeIndexToSceneNodeKey;
	TArray<int32> SegmentIndexToZeroDEdgeIndex;
	TArray<bool> SegmentDetailActive;
	TArray<bool> ZeroDEdgeFlowFixedByOneD;
};

struct FFluidHybridSimulationCouplingLibrary
{
	static void RebuildHybridTopology(
		UWorld* World,
		const TArray<FFluidNetworkNodeStateZeroD>& NetworkNodeStates,
		const TArray<FFluidNetworkEdgeStateZeroD>& NetworkEdgeStates,
		const TArray<FFluidSegmentStateOneD>& SegmentStates,
		const TArray<TWeakObjectPtr<APipeFluidPipeActor>>& SegmentPipeActors,
		FFluidHybridNetworkTopology& OutTopology);

	static void UpdateHybridDecomposition(
		UWorld* World,
		const ULazyFluidPipesDeveloperSettings& Settings,
		const TArray<FFluidSegmentStateOneD>& SegmentStates,
		const TArray<TWeakObjectPtr<APipeFluidPipeActor>>& SegmentPipeActors,
		FFluidHybridNetworkTopology& InOutTopology);

	static void ApplyZeroDimensionPressureToOneDimensionBoundaries(
		const FFluidHybridNetworkTopology& Topology,
		const TArray<FFluidNetworkNodeStateZeroD>& NetworkNodeStates,
		TArray<FFluidSegmentStateOneD>& SegmentStates);

	static void ApplyOneDimensionFlowToZeroDimensionEdges(
		const FFluidHybridNetworkTopology& Topology,
		const TArray<FFluidSegmentStateOneD>& SegmentStates,
		const TArray<FFluidNetworkEdgeStateZeroD>& NetworkEdgeStates,
		TArray<FFluidNetworkEdgeStateZeroD>& MutableNetworkEdgeStates);

	static void RunHybridSimulationStep(
		UWorld* World,
		const ULazyFluidPipesDeveloperSettings& Settings,
		UFluidNetwork0DSubsystem& ZeroDSubsystem,
		UFluidSegment1DSubsystem& OneDSubsystem,
		FFluidHybridNetworkTopology& InOutTopology);
};
