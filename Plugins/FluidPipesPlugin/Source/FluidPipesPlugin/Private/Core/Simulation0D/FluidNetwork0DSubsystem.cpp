#include "Core/Simulation0D/FluidNetwork0DSubsystem.h"

#include "Kismet/KismetSystemLibrary.h"
#include "Other/LazyFluidPipesDeveloperSettings.h"

void UFluidNetwork0DSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ResetSimulationState();
}

void UFluidNetwork0DSubsystem::Deinitialize()
{
	ResetSimulationState();
	Super::Deinitialize();
}

void UFluidNetwork0DSubsystem::Tick(float DeltaTime)
{
	const ULazyFluidPipesDeveloperSettings* Settings = GetDefault<ULazyFluidPipesDeveloperSettings>();
	if (!Settings->EnableFluidNetworkSimulationZeroD)
	{
		return;
	}

	AccumulatedTime += DeltaTime;
	while (AccumulatedTime >= Settings->SimulationStepTimeZeroD)
	{
		SimulateStep(Settings->SimulationStepTimeZeroD);
		AccumulatedTime -= Settings->SimulationStepTimeZeroD;
	}

	if (Settings->EnableFluidDebugMessages)
	{
		UKismetSystemLibrary::PrintString(this, FString::Format(TEXT("0D Tick: Nodes={0}, Edges={1}"), { NetworkNodeStates.Num(), NetworkEdgeStates.Num() }), true, false, FLinearColor::Green, 0.0f);
	}
}

TStatId UFluidNetwork0DSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UFluidNetwork0DSubsystem, STATGROUP_Tickables);
}

void UFluidNetwork0DSubsystem::ResetSimulationState()
{
	NetworkNodeStates.Reset();
	NetworkEdgeStates.Reset();
	AccumulatedTime = 0.0f;
}

const TArray<FFluidNetworkNodeStateZeroD>& UFluidNetwork0DSubsystem::GetNodeStates() const
{
	return NetworkNodeStates;
}

const TArray<FFluidNetworkEdgeStateZeroD>& UFluidNetwork0DSubsystem::GetEdgeStates() const
{
	return NetworkEdgeStates;
}

void UFluidNetwork0DSubsystem::SimulateStep(float SimulationStepTime)
{
	UpdateEdgeFlows(SimulationStepTime);
	IntegrateNodeVolumes(SimulationStepTime);
	UpdateNodePressures();
}

void UFluidNetwork0DSubsystem::UpdateEdgeFlows(float SimulationStepTime)
{
	for (FFluidNetworkEdgeStateZeroD& NetworkEdgeState : NetworkEdgeStates)
	{
		if (!NetworkNodeStates.IsValidIndex(NetworkEdgeState.FromNodeIndex) || !NetworkNodeStates.IsValidIndex(NetworkEdgeState.ToNodeIndex))
		{
			NetworkEdgeState.FlowRate = 0.0f;
			continue;
		}

		const float PressureDifference = NetworkNodeStates[NetworkEdgeState.FromNodeIndex].Pressure - NetworkNodeStates[NetworkEdgeState.ToNodeIndex].Pressure;
		if (NetworkEdgeState.Inertance > KINDA_SMALL_NUMBER)
		{
			const float FlowRateDerivative = (PressureDifference - NetworkEdgeState.Resistance * NetworkEdgeState.FlowRate) / NetworkEdgeState.Inertance;
			NetworkEdgeState.FlowRate += FlowRateDerivative * SimulationStepTime;
		}
		else
		{
			NetworkEdgeState.FlowRate = PressureDifference / FMath::Max(NetworkEdgeState.Resistance, KINDA_SMALL_NUMBER);
		}
	}
}

void UFluidNetwork0DSubsystem::IntegrateNodeVolumes(float SimulationStepTime)
{
	TArray<float> NodeNetFlows;
	NodeNetFlows.Init(0.0f, NetworkNodeStates.Num());

	for (const FFluidNetworkEdgeStateZeroD& NetworkEdgeState : NetworkEdgeStates)
	{
		if (NetworkNodeStates.IsValidIndex(NetworkEdgeState.FromNodeIndex))
		{
			NodeNetFlows[NetworkEdgeState.FromNodeIndex] -= NetworkEdgeState.FlowRate;
		}

		if (NetworkNodeStates.IsValidIndex(NetworkEdgeState.ToNodeIndex))
		{
			NodeNetFlows[NetworkEdgeState.ToNodeIndex] += NetworkEdgeState.FlowRate;
		}
	}

	for (int32 NodeIndex = 0; NodeIndex < NetworkNodeStates.Num(); ++NodeIndex)
	{
		NetworkNodeStates[NodeIndex].StoredVolume += (NodeNetFlows[NodeIndex] + NetworkNodeStates[NodeIndex].SourceFlow) * SimulationStepTime;
		NetworkNodeStates[NodeIndex].StoredVolume = FMath::Max(NetworkNodeStates[NodeIndex].StoredVolume, 0.0f);
	}
}

void UFluidNetwork0DSubsystem::UpdateNodePressures()
{
	for (FFluidNetworkNodeStateZeroD& NetworkNodeState : NetworkNodeStates)
	{
		const float SafeCompliance = FMath::Max(NetworkNodeState.Compliance, KINDA_SMALL_NUMBER);
		NetworkNodeState.Pressure = NetworkNodeState.ReferencePressure + (NetworkNodeState.StoredVolume - NetworkNodeState.ReferenceVolume) / SafeCompliance;
	}
}
