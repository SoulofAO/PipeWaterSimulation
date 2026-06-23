#pragma once

#include "CoreMinimal.h"
#include "Data/FluidData.h"

class UWorld;

class FBaseFluidNetwork0DSimulation
{
public:
	virtual ~FBaseFluidNetwork0DSimulation() = default;

	virtual bool IsAvailable() const = 0;

	virtual void Release() = 0;

	virtual void RebuildFromNetwork(const TArray<FFluidNetworkNodeStateZeroD>& NodeStates, const TArray<FFluidNetworkEdgeStateZeroD>& EdgeStates, UWorld* SimulationWorld) = 0;

	virtual void SimulateStep(UWorld* World, TArray<FFluidNetworkNodeStateZeroD>& NodeStates, TArray<FFluidNetworkEdgeStateZeroD>& EdgeStates, float SimulationStepTime) = 0;
};
