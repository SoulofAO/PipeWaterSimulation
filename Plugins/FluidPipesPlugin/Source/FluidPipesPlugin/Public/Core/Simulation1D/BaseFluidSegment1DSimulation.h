#pragma once

#include "CoreMinimal.h"
#include "Data/FluidData.h"
#include "UObject/WeakObjectPtr.h"

class APipeFluidPipeActor;
class UWorld;

class FBaseFluidSegment1DSimulation
{
public:
	virtual ~FBaseFluidSegment1DSimulation() = default;

	virtual bool IsAvailable() const = 0;
	virtual void Release() = 0;
	virtual void RebuildFromSegments(const TArray<FFluidSegmentStateOneD>& SegmentStates, const TArray<TWeakObjectPtr<APipeFluidPipeActor>>& SegmentPipeActors) = 0;
	virtual void SimulateStep(UWorld* World, TArray<FFluidSegmentStateOneD>& SegmentStates, const TArray<TWeakObjectPtr<APipeFluidPipeActor>>& SegmentPipeActors, float SimulationStepTime, bool bWaitForReadbackBeforeLock) = 0;
};
