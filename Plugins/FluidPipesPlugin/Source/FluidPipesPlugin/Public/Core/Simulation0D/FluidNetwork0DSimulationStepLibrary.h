#pragma once

#include "CoreMinimal.h"
#include "Data/FluidData.h"

class ULazyFluidPipesDeveloperSettings;

struct FFluidNetwork0DSimulationStepLibrary
{
	static void UpdateEdgeFlows(TArray<FFluidNetworkEdgeStateZeroD>& EdgeStates, const TArray<FFluidNetworkNodeStateZeroD>& NodeStates, float SimulationStepTime);

	static void IntegrateNodeVolumes(TArray<FFluidNetworkNodeStateZeroD>& NodeStates, const TArray<FFluidNetworkEdgeStateZeroD>& EdgeStates, float SimulationStepTime);

	static void UpdateNodePressures(TArray<FFluidNetworkNodeStateZeroD>& NodeStates, float ZeroDPressureScale);

	static void RunSimulationStep(TArray<FFluidNetworkNodeStateZeroD>& NodeStates, TArray<FFluidNetworkEdgeStateZeroD>& EdgeStates, float SimulationStepTime, const ULazyFluidPipesDeveloperSettings& Settings);
};
