#pragma once

#include "CoreMinimal.h"
#include "Data/FluidData.h"

class ULazyFluidPipesDeveloperSettings;

struct FFluidSimulationStateLimits
{
	static void ClampCellStateOneD(FFluidSegmentCellStateOneD& CellState, const ULazyFluidPipesDeveloperSettings& Settings);

	static void ClampSegmentStateOneD(FFluidSegmentStateOneD& SegmentState, const ULazyFluidPipesDeveloperSettings& Settings);

	static void ClampAllSegmentStatesOneD(TArray<FFluidSegmentStateOneD>& SegmentStates, const ULazyFluidPipesDeveloperSettings& Settings);

	static void ClampNodeStateZeroD(FFluidNetworkNodeStateZeroD& NodeState, const ULazyFluidPipesDeveloperSettings& Settings);

	static void ClampEdgeStateZeroD(FFluidNetworkEdgeStateZeroD& EdgeState, const ULazyFluidPipesDeveloperSettings& Settings);

	static void ClampAllNetworkStatesZeroD(TArray<FFluidNetworkNodeStateZeroD>& NodeStates, TArray<FFluidNetworkEdgeStateZeroD>& EdgeStates, const ULazyFluidPipesDeveloperSettings& Settings);
};
