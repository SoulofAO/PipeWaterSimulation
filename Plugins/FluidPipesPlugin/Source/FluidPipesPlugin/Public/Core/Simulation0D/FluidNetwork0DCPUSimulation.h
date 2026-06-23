#pragma once

#include "Core/Simulation0D/BaseFluidNetwork0DSimulation.h"
#include "HAL/CriticalSection.h"

class FRunnable;
class FRunnableThread;
class FEvent;
class ULazyFluidPipesDeveloperSettings;

class FFluidNetwork0DCPUSimulation : public FBaseFluidNetwork0DSimulation
{
public:
	FFluidNetwork0DCPUSimulation();
	~FFluidNetwork0DCPUSimulation();

	virtual bool IsAvailable() const override;
	virtual void Release() override;
	virtual void RebuildFromNetwork(const TArray<FFluidNetworkNodeStateZeroD>& NodeStates, const TArray<FFluidNetworkEdgeStateZeroD>& EdgeStates, UWorld* SimulationWorld) override;
	virtual void SimulateStep(UWorld* World, TArray<FFluidNetworkNodeStateZeroD>& NodeStates, TArray<FFluidNetworkEdgeStateZeroD>& EdgeStates, float SimulationStepTime) override;

	void BindSimulationSettings(const ULazyFluidPipesDeveloperSettings* SimulationSettings);
	void ConfigureBackgroundWorker(bool bEnableBackgroundWorker);
	bool UsesBackgroundWorker() const;
	bool IsWorkerStateResident() const;

	void PublishNetworkStatesToWorker(const TArray<FFluidNetworkNodeStateZeroD>& NodeStates, const TArray<FFluidNetworkEdgeStateZeroD>& EdgeStates);
	void PublishNodeSourceFlowsToWorker(const TArray<FFluidNetworkNodeStateZeroD>& NodeStates);
	void EnqueueSimulateStepCpuOnly(float SimulationStepTime);
	void WaitForStepCompletion();
	void ReadbackToNetworkStates(TArray<FFluidNetworkNodeStateZeroD>& NodeStates, TArray<FFluidNetworkEdgeStateZeroD>& EdgeStates);

	void ProcessWorkerStepQueueUntilIdle();
	void RunWorkerSimulationStep(float RequestedSimulationStepTime);
	void RequestWorkerStop();
	void WorkerThreadMainLoop();

private:
	void StopBackgroundWorker();
	void EnsureBackgroundWorkerRunning();
	void SimulateStepOnNetworkStates(TArray<FFluidNetworkNodeStateZeroD>& TargetNodeStates, TArray<FFluidNetworkEdgeStateZeroD>& TargetEdgeStates, float SimulationStepTime);

	bool bBackgroundWorkerEnabled = false;
	bool bWorkerStateResident = false;
	TArray<FFluidNetworkNodeStateZeroD> WorkerNodeStates;
	TArray<FFluidNetworkEdgeStateZeroD> WorkerEdgeStates;

	FCriticalSection WorkerStateCriticalSection;
	FEvent* StepRequestedEvent = nullptr;
	FEvent* StepCompletedEvent = nullptr;
	FRunnable* WorkerRunnable = nullptr;
	FRunnableThread* WorkerThread = nullptr;
	TAtomic<bool> bWorkerStopRequested{false};
	TArray<float> PendingSimulationStepTimes;

	const ULazyFluidPipesDeveloperSettings* BoundSimulationSettings = nullptr;
};
