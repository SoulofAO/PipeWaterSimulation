#pragma once

#include "Core/Simulation1D/BaseFluidSegment1DSimulation.h"

class FFluidSegment1DCPUSimulation : public FBaseFluidSegment1DSimulation
{
public:
	virtual bool IsAvailable() const override;
	virtual void Release() override;
	virtual void RebuildFromSegments(const TArray<FFluidSegmentStateOneD>& SegmentStates, const TArray<TWeakObjectPtr<APipeFluidPipeActor>>&) override;
	virtual void SimulateStep(UWorld* World, TArray<FFluidSegmentStateOneD>& SegmentStates, const TArray<TWeakObjectPtr<APipeFluidPipeActor>>& SegmentPipeActors, float SimulationStepTime, bool bWaitForReadbackBeforeLock) override;

private:
	struct FFluidOneDJunctionEndpointIncident
	{
		int32 SegmentIndex = INDEX_NONE;
		bool bLeftEndpoint = false;
	};

	void UpdateOneDimensionBoundaryFlowsFromAttachedPipePointActors(TArray<FFluidSegmentStateOneD>& TargetSegmentStates, const TArray<TWeakObjectPtr<APipeFluidPipeActor>>& SegmentPipeActors) const;
	void RebuildJunctionSceneNodeKeyTopology(const TArray<FFluidSegmentStateOneD>& SourceSegmentStates);
	void ApplyJunctionCouplingToNextSegmentStates(const TArray<FFluidSegmentStateOneD>& CurrentSegmentStates, TArray<FFluidSegmentStateOneD>& NextSegmentStates) const;
	void SolveSegmentWaterHammerStep(const FFluidSegmentStateOneD& CurrentSegmentState, float SimulationStepTime, float GravityAccelerationAlongAxis, FFluidSegmentStateOneD& NextSegmentState) const;
	void ApplyBoundaryConditions(const FFluidSegmentStateOneD& CurrentSegmentState, FFluidSegmentStateOneD& NextSegmentState) const;
	void UpdateDerivedCellValues(FFluidSegmentStateOneD& SegmentState) const;
	float ComputeStableStepTime(const FFluidSegmentStateOneD& SegmentState) const;
	float GetCrossSectionArea(const FFluidSegmentStateOneD& SegmentState) const;
	bool IsSegmentStateFinite(const FFluidSegmentStateOneD& SegmentState) const;

	TMap<int32, TArray<FFluidOneDJunctionEndpointIncident>> JunctionSceneNodeKeyToIncidentEndpoints;
};
