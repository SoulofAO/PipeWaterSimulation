#pragma once

#include "Core/Simulation1D/BaseFluidSegment1DSimulation.h"
#include "HAL/CriticalSection.h"

class FRunnable;
class FRunnableThread;
class FEvent;

class FFluidSegment1DCPUSimulation : public FBaseFluidSegment1DSimulation
{
public:
	FFluidSegment1DCPUSimulation();
	~FFluidSegment1DCPUSimulation();

	virtual bool IsAvailable() const override;
	virtual void Release() override;
	virtual void RebuildFromSegments(const TArray<FFluidSegmentStateOneD>& SegmentStates, const TArray<TWeakObjectPtr<APipeFluidPipeActor>>& SegmentPipeActors) override;
	virtual void RebuildFromSegments(const TArray<FFluidSegmentStateOneD>& SegmentStates, const TArray<TWeakObjectPtr<APipeFluidPipeActor>>& SegmentPipeActors, UWorld* SimulationWorld);
	virtual void SimulateStep(UWorld* World, TArray<FFluidSegmentStateOneD>& SegmentStates, const TArray<TWeakObjectPtr<APipeFluidPipeActor>>& SegmentPipeActors, float SimulationStepTime) override;

	void ConfigureBackgroundWorker(bool bEnableBackgroundWorker);
	bool UsesBackgroundWorker() const;
	bool IsWorkerStateResident() const;

	void PublishSegmentStatesToWorker(const TArray<FFluidSegmentStateOneD>& SegmentStates);
	void BakeWorkerStaticStepInputsOnGameThread(UWorld* SimulationWorld, const TArray<TWeakObjectPtr<APipeFluidPipeActor>>& SegmentPipeActors);
	void EnqueueSimulateStepCpuOnly(float SimulationStepTime);
	void WaitForStepCompletion();
	void ReadbackToSegmentStates(TArray<FFluidSegmentStateOneD>& SegmentStates);
	void ReadbackSegmentIndicesToSegmentStates(TArray<FFluidSegmentStateOneD>& SegmentStates, const TArray<int32>& SegmentIndices);

	float ComputeMinimumStableStepTimeOnWorker(float RequestedSimulationStepTime);

	void ProcessWorkerStepQueueUntilIdle();
	void RunWorkerSimulationStep(float RequestedSimulationStepTime);
	void RequestWorkerStop();
	void WorkerThreadMainLoop();

private:
	struct FFluidOneDJunctionEndpointIncident
	{
		int32 SegmentIndex = INDEX_NONE;
		bool bLeftEndpoint = false;
	};

	void StopBackgroundWorker();
	void EnsureBackgroundWorkerRunning();
	void SimulateStepOnSegmentStates(TArray<FFluidSegmentStateOneD>& TargetSegmentStates, const TArray<float>& GravityAccelerationAlongAxisPerSegment, float SimulationStepTime);
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

	bool bBackgroundWorkerEnabled = false;
	bool bWorkerStateResident = false;
	TArray<FFluidSegmentStateOneD> WorkerSegmentStates;
	TArray<float> SegmentGravityAccelerationAlongAxis;

	FCriticalSection WorkerStateCriticalSection;
	FEvent* StepRequestedEvent = nullptr;
	FEvent* StepCompletedEvent = nullptr;
	FRunnable* WorkerRunnable = nullptr;
	FRunnableThread* WorkerThread = nullptr;
	TAtomic<bool> bWorkerStopRequested{false};
	TArray<float> PendingSimulationStepTimes;
};
