#include "Core/Simulation0D/FluidNetwork0DSimulationStepLibrary.h"

#include "Core/Simulation/FluidSimulationStateLimits.h"
#include "Other/LazyFluidPipesDeveloperSettings.h"

void FFluidNetwork0DSimulationStepLibrary::UpdateEdgeFlows(TArray<FFluidNetworkEdgeStateZeroD>& EdgeStates, const TArray<FFluidNetworkNodeStateZeroD>& NodeStates, float SimulationStepTime, const TArray<bool>* EdgeFlowFixedByOneDMask)
{
	for (int32 EdgeIndex = 0; EdgeIndex < EdgeStates.Num(); ++EdgeIndex)
	{
		FFluidNetworkEdgeStateZeroD& NetworkEdgeState = EdgeStates[EdgeIndex];
		if (EdgeFlowFixedByOneDMask && EdgeFlowFixedByOneDMask->IsValidIndex(EdgeIndex) && (*EdgeFlowFixedByOneDMask)[EdgeIndex])
		{
			continue;
		}

		if (!NodeStates.IsValidIndex(NetworkEdgeState.FromNodeIndex) || !NodeStates.IsValidIndex(NetworkEdgeState.ToNodeIndex))
		{
			NetworkEdgeState.FlowRate = 0.0f;
			continue;
		}

		const float PressureDifference = NodeStates[NetworkEdgeState.FromNodeIndex].Pressure - NodeStates[NetworkEdgeState.ToNodeIndex].Pressure;
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

void FFluidNetwork0DSimulationStepLibrary::IntegrateNodeVolumes(TArray<FFluidNetworkNodeStateZeroD>& NodeStates, const TArray<FFluidNetworkEdgeStateZeroD>& EdgeStates, float SimulationStepTime)
{
	TArray<float> NodeNetFlows;
	NodeNetFlows.Init(0.0f, NodeStates.Num());

	for (const FFluidNetworkEdgeStateZeroD& NetworkEdgeState : EdgeStates)
	{
		if (NodeStates.IsValidIndex(NetworkEdgeState.FromNodeIndex))
		{
			NodeNetFlows[NetworkEdgeState.FromNodeIndex] -= NetworkEdgeState.FlowRate;
		}

		if (NodeStates.IsValidIndex(NetworkEdgeState.ToNodeIndex))
		{
			NodeNetFlows[NetworkEdgeState.ToNodeIndex] += NetworkEdgeState.FlowRate;
		}
	}

	for (int32 NodeIndex = 0; NodeIndex < NodeStates.Num(); ++NodeIndex)
	{
		NodeStates[NodeIndex].StoredVolume += (NodeNetFlows[NodeIndex] + NodeStates[NodeIndex].SourceFlow) * SimulationStepTime;
		NodeStates[NodeIndex].StoredVolume = FMath::Max(NodeStates[NodeIndex].StoredVolume, 0.0f);
	}
}

void FFluidNetwork0DSimulationStepLibrary::UpdateNodePressures(TArray<FFluidNetworkNodeStateZeroD>& NodeStates, float ZeroDPressureScale)
{
	const float SafePressureScale = FMath::Max(ZeroDPressureScale, KINDA_SMALL_NUMBER);
	for (FFluidNetworkNodeStateZeroD& NetworkNodeState : NodeStates)
	{
		const float SafeCompliance = FMath::Max(NetworkNodeState.Compliance, KINDA_SMALL_NUMBER);
		const float PressureFromStoredVolume = (NetworkNodeState.StoredVolume - NetworkNodeState.ReferenceVolume) / SafeCompliance;
		NetworkNodeState.Pressure = NetworkNodeState.ReferencePressure + PressureFromStoredVolume * SafePressureScale;
	}
}

void FFluidNetwork0DSimulationStepLibrary::RunSimulationStep(TArray<FFluidNetworkNodeStateZeroD>& NodeStates, TArray<FFluidNetworkEdgeStateZeroD>& EdgeStates, float SimulationStepTime, const ULazyFluidPipesDeveloperSettings& Settings)
{
	RunSimulationStep(NodeStates, EdgeStates, SimulationStepTime, Settings, TArray<bool>());
}

void FFluidNetwork0DSimulationStepLibrary::RunSimulationStep(TArray<FFluidNetworkNodeStateZeroD>& NodeStates, TArray<FFluidNetworkEdgeStateZeroD>& EdgeStates, float SimulationStepTime, const ULazyFluidPipesDeveloperSettings& Settings, const TArray<bool>& EdgeFlowFixedByOneDMask)
{
	const TArray<bool>* EdgeFlowMaskPointer = EdgeFlowFixedByOneDMask.Num() == EdgeStates.Num() ? &EdgeFlowFixedByOneDMask : nullptr;
	UpdateEdgeFlows(EdgeStates, NodeStates, SimulationStepTime, EdgeFlowMaskPointer);
	IntegrateNodeVolumes(NodeStates, EdgeStates, SimulationStepTime);
	UpdateNodePressures(NodeStates, Settings.ZeroDPressureScale);
	FFluidSimulationStateLimits::ClampAllNetworkStatesZeroD(NodeStates, EdgeStates, Settings);
}
