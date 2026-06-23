#include "Core/Simulation0D/FluidNetwork0DCPUSimulation.h"

#include "Core/Simulation0D/FluidNetwork0DSimulationStepLibrary.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Other/FluidPipesSimulationSettingsLibrary.h"
#include "Other/LazyFluidPipesDeveloperSettings.h"

class FFluidNetwork0DCpuWorkerRunnable : public FRunnable
{
public:
	explicit FFluidNetwork0DCpuWorkerRunnable(FFluidNetwork0DCPUSimulation* InSimulation)
		: Simulation(InSimulation)
	{
	}

	virtual uint32 Run() override
	{
		if (Simulation)
		{
			Simulation->WorkerThreadMainLoop();
		}
		return 0;
	}

	virtual void Stop() override
	{
		if (Simulation)
		{
			Simulation->RequestWorkerStop();
		}
	}

private:
	FFluidNetwork0DCPUSimulation* Simulation = nullptr;
};

FFluidNetwork0DCPUSimulation::FFluidNetwork0DCPUSimulation()
{
	StepRequestedEvent = FPlatformProcess::GetSynchEventFromPool(false);
	StepCompletedEvent = FPlatformProcess::GetSynchEventFromPool(false);
	StepCompletedEvent->Trigger();
}

FFluidNetwork0DCPUSimulation::~FFluidNetwork0DCPUSimulation()
{
	Release();
}

bool FFluidNetwork0DCPUSimulation::IsAvailable() const
{
	return true;
}

void FFluidNetwork0DCPUSimulation::Release()
{
	WaitForStepCompletion();
	StopBackgroundWorker();
	{
		FScopeLock WorkerStateLock(&WorkerStateCriticalSection);
		WorkerNodeStates.Reset();
		WorkerEdgeStates.Reset();
		PendingSimulationStepTimes.Reset();
		bWorkerStateResident = false;
	}
	if (StepRequestedEvent)
	{
		FPlatformProcess::ReturnSynchEventToPool(StepRequestedEvent);
		StepRequestedEvent = nullptr;
	}
	if (StepCompletedEvent)
	{
		FPlatformProcess::ReturnSynchEventToPool(StepCompletedEvent);
		StepCompletedEvent = nullptr;
	}
}

void FFluidNetwork0DCPUSimulation::BindSimulationSettings(const ULazyFluidPipesDeveloperSettings* SimulationSettings)
{
	BoundSimulationSettings = SimulationSettings;
}

void FFluidNetwork0DCPUSimulation::ConfigureBackgroundWorker(bool bEnableBackgroundWorker)
{
	if (bBackgroundWorkerEnabled == bEnableBackgroundWorker)
	{
		return;
	}

	if (bBackgroundWorkerEnabled)
	{
		StopBackgroundWorker();
	}

	bBackgroundWorkerEnabled = bEnableBackgroundWorker;

	if (bBackgroundWorkerEnabled)
	{
		EnsureBackgroundWorkerRunning();
		StepCompletedEvent->Trigger();
	}
}

bool FFluidNetwork0DCPUSimulation::UsesBackgroundWorker() const
{
	return bBackgroundWorkerEnabled;
}

bool FFluidNetwork0DCPUSimulation::IsWorkerStateResident() const
{
	return bWorkerStateResident;
}

void FFluidNetwork0DCPUSimulation::RebuildFromNetwork(const TArray<FFluidNetworkNodeStateZeroD>& NodeStates, const TArray<FFluidNetworkEdgeStateZeroD>& EdgeStates, UWorld* SimulationWorld)
{
	if (bBackgroundWorkerEnabled)
	{
		PublishNetworkStatesToWorker(NodeStates, EdgeStates);
	}
}

void FFluidNetwork0DCPUSimulation::PublishNetworkStatesToWorker(const TArray<FFluidNetworkNodeStateZeroD>& NodeStates, const TArray<FFluidNetworkEdgeStateZeroD>& EdgeStates)
{
	WaitForStepCompletion();
	FScopeLock WorkerStateLock(&WorkerStateCriticalSection);
	WorkerNodeStates = NodeStates;
	WorkerEdgeStates = EdgeStates;
	bWorkerStateResident = WorkerNodeStates.Num() > 0;
}

void FFluidNetwork0DCPUSimulation::PublishNodeSourceFlowsToWorker(const TArray<FFluidNetworkNodeStateZeroD>& NodeStates)
{
	if (!bBackgroundWorkerEnabled)
	{
		return;
	}

	FScopeLock WorkerStateLock(&WorkerStateCriticalSection);
	if (!bWorkerStateResident)
	{
		return;
	}

	const int32 UpdateCount = FMath::Min(NodeStates.Num(), WorkerNodeStates.Num());
	for (int32 NodeIndex = 0; NodeIndex < UpdateCount; ++NodeIndex)
	{
		WorkerNodeStates[NodeIndex].SourceFlow = NodeStates[NodeIndex].SourceFlow;
	}
}

void FFluidNetwork0DCPUSimulation::EnqueueSimulateStepCpuOnly(float SimulationStepTime)
{
	{
		FScopeLock WorkerStateLock(&WorkerStateCriticalSection);
		PendingSimulationStepTimes.Add(SimulationStepTime);
		StepCompletedEvent->Reset();
	}
	EnsureBackgroundWorkerRunning();
	StepRequestedEvent->Trigger();
}

void FFluidNetwork0DCPUSimulation::WaitForStepCompletion()
{
	if (!bBackgroundWorkerEnabled || !StepCompletedEvent)
	{
		return;
	}
	StepCompletedEvent->Wait();
}

void FFluidNetwork0DCPUSimulation::ReadbackToNetworkStates(TArray<FFluidNetworkNodeStateZeroD>& NodeStates, TArray<FFluidNetworkEdgeStateZeroD>& EdgeStates)
{
	WaitForStepCompletion();
	FScopeLock WorkerStateLock(&WorkerStateCriticalSection);
	NodeStates = WorkerNodeStates;
	EdgeStates = WorkerEdgeStates;
}

void FFluidNetwork0DCPUSimulation::RunWorkerSimulationStep(float RequestedSimulationStepTime)
{
	FScopeLock WorkerStateLock(&WorkerStateCriticalSection);
	if (!bWorkerStateResident)
	{
		return;
	}
	SimulateStepOnNetworkStates(WorkerNodeStates, WorkerEdgeStates, RequestedSimulationStepTime);
}

void FFluidNetwork0DCPUSimulation::ProcessWorkerStepQueueUntilIdle()
{
	for (;;)
	{
		float RequestedSimulationStepTime = 0.0f;
		{
			FScopeLock WorkerStateLock(&WorkerStateCriticalSection);
			if (PendingSimulationStepTimes.Num() == 0)
			{
				StepCompletedEvent->Trigger();
				return;
			}
			RequestedSimulationStepTime = PendingSimulationStepTimes[0];
			PendingSimulationStepTimes.RemoveAt(0, 1, EAllowShrinking::No);
		}
		RunWorkerSimulationStep(RequestedSimulationStepTime);
	}
}

void FFluidNetwork0DCPUSimulation::RequestWorkerStop()
{
	bWorkerStopRequested = true;
	if (StepRequestedEvent)
	{
		StepRequestedEvent->Trigger();
	}
}

void FFluidNetwork0DCPUSimulation::WorkerThreadMainLoop()
{
	while (!bWorkerStopRequested)
	{
		StepRequestedEvent->Wait();
		if (bWorkerStopRequested)
		{
			break;
		}
		ProcessWorkerStepQueueUntilIdle();
	}
}

void FFluidNetwork0DCPUSimulation::StopBackgroundWorker()
{
	RequestWorkerStop();

	if (WorkerThread)
	{
		WorkerThread->WaitForCompletion();
		delete WorkerThread;
		WorkerThread = nullptr;
	}

	if (WorkerRunnable)
	{
		delete WorkerRunnable;
		WorkerRunnable = nullptr;
	}

	bWorkerStopRequested = false;
}

void FFluidNetwork0DCPUSimulation::EnsureBackgroundWorkerRunning()
{
	if (!bBackgroundWorkerEnabled)
	{
		return;
	}

	if (!StepRequestedEvent)
	{
		StepRequestedEvent = FPlatformProcess::GetSynchEventFromPool(false);
		StepCompletedEvent = FPlatformProcess::GetSynchEventFromPool(false);
		StepCompletedEvent->Trigger();
	}

	if (WorkerThread)
	{
		return;
	}

	bWorkerStopRequested = false;
	WorkerRunnable = new FFluidNetwork0DCpuWorkerRunnable(this);
	WorkerThread = FRunnableThread::Create(WorkerRunnable, TEXT("FluidNetwork0DCpuWorker"), 0, TPri_Normal);
}

void FFluidNetwork0DCPUSimulation::SimulateStepOnNetworkStates(TArray<FFluidNetworkNodeStateZeroD>& TargetNodeStates, TArray<FFluidNetworkEdgeStateZeroD>& TargetEdgeStates, float SimulationStepTime)
{
	const ULazyFluidPipesDeveloperSettings& Settings = BoundSimulationSettings
		? *BoundSimulationSettings
		: FFluidPipesSimulationSettingsLibrary::ResolveSimulationSettings(nullptr);
	FFluidNetwork0DSimulationStepLibrary::RunSimulationStep(TargetNodeStates, TargetEdgeStates, SimulationStepTime, Settings);
}

void FFluidNetwork0DCPUSimulation::SimulateStep(UWorld* World, TArray<FFluidNetworkNodeStateZeroD>& NodeStates, TArray<FFluidNetworkEdgeStateZeroD>& EdgeStates, float SimulationStepTime)
{
	SimulateStepOnNetworkStates(NodeStates, EdgeStates, SimulationStepTime);
}
