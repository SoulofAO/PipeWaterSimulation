#pragma once

#include "CoreMinimal.h"
#include "Data/FluidData.h"

class APipeFluidPipeActor;
class UWorld;

struct FFluidPipePassiveJunctionMerge
{
	static void MergeColinearOneDSegments(TArray<FFluidSegmentStateOneD>& SegmentStates, TArray<APipeFluidPipeActor*>& SegmentPipeActors, UWorld* World);

	static void MergeColinearZeroDEdges(TArray<FFluidNetworkNodeStateZeroD>& NodeStates, TArray<FFluidNetworkEdgeStateZeroD>& EdgeStates, UWorld* World);
};
