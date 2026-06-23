#include "Core/Simulation/FluidSimulationStateLimits.h"

#include "Other/LazyFluidPipesDeveloperSettings.h"

void FFluidSimulationStateLimits::ClampCellStateOneD(FFluidSegmentCellStateOneD& CellState, const ULazyFluidPipesDeveloperSettings& Settings)
{
	if (!Settings.EnableOneDSimulationStateVariableClamping)
	{
		return;
	}
	CellState.Pressure = FMath::Clamp(CellState.Pressure, Settings.OneDMinimumPressure, Settings.OneDMaximumPressure);
	CellState.FlowRate = FMath::Clamp(CellState.FlowRate, Settings.OneDMinimumVolumeFlowRate, Settings.OneDMaximumVolumeFlowRate);
	CellState.Velocity = FMath::Clamp(CellState.Velocity, Settings.OneDMinimumVelocity, Settings.OneDMaximumVelocity);
}

void FFluidSimulationStateLimits::ClampSegmentStateOneD(FFluidSegmentStateOneD& SegmentState, const ULazyFluidPipesDeveloperSettings& Settings)
{
	if (!Settings.EnableOneDSimulationStateVariableClamping)
	{
		return;
	}
	for (FFluidSegmentCellStateOneD& CellState : SegmentState.CellStates)
	{
		ClampCellStateOneD(CellState, Settings);
	}
	SegmentState.LeftBoundaryPressure = FMath::Clamp(SegmentState.LeftBoundaryPressure, Settings.OneDMinimumPressure, Settings.OneDMaximumPressure);
	SegmentState.RightBoundaryPressure = FMath::Clamp(SegmentState.RightBoundaryPressure, Settings.OneDMinimumPressure, Settings.OneDMaximumPressure);
	SegmentState.LeftBoundaryFlow = FMath::Clamp(SegmentState.LeftBoundaryFlow, Settings.OneDMinimumVolumeFlowRate, Settings.OneDMaximumVolumeFlowRate);
	SegmentState.RightBoundaryFlow = FMath::Clamp(SegmentState.RightBoundaryFlow, Settings.OneDMinimumVolumeFlowRate, Settings.OneDMaximumVolumeFlowRate);
}

void FFluidSimulationStateLimits::ClampAllSegmentStatesOneD(TArray<FFluidSegmentStateOneD>& SegmentStates, const ULazyFluidPipesDeveloperSettings& Settings)
{
	if (!Settings.EnableOneDSimulationStateVariableClamping)
	{
		return;
	}
	for (FFluidSegmentStateOneD& SegmentState : SegmentStates)
	{
		ClampSegmentStateOneD(SegmentState, Settings);
	}
}

void FFluidSimulationStateLimits::ClampNodeStateZeroD(FFluidNetworkNodeStateZeroD& NodeState, const ULazyFluidPipesDeveloperSettings& Settings)
{
	if (!Settings.EnableZeroDSimulationStateVariableClamping)
	{
		return;
	}
	NodeState.Pressure = FMath::Clamp(NodeState.Pressure, Settings.ZeroDMinimumPressure, Settings.ZeroDMaximumPressure);
	NodeState.SourceFlow = FMath::Clamp(NodeState.SourceFlow, Settings.ZeroDMinimumVolumeFlowRate, Settings.ZeroDMaximumVolumeFlowRate);
}

void FFluidSimulationStateLimits::ClampEdgeStateZeroD(FFluidNetworkEdgeStateZeroD& EdgeState, const ULazyFluidPipesDeveloperSettings& Settings)
{
	if (!Settings.EnableZeroDSimulationStateVariableClamping)
	{
		return;
	}
	EdgeState.FlowRate = FMath::Clamp(EdgeState.FlowRate, Settings.ZeroDMinimumVolumeFlowRate, Settings.ZeroDMaximumVolumeFlowRate);
}

void FFluidSimulationStateLimits::ClampAllNetworkStatesZeroD(TArray<FFluidNetworkNodeStateZeroD>& NodeStates, TArray<FFluidNetworkEdgeStateZeroD>& EdgeStates, const ULazyFluidPipesDeveloperSettings& Settings)
{
	if (!Settings.EnableZeroDSimulationStateVariableClamping)
	{
		return;
	}
	for (FFluidNetworkNodeStateZeroD& NodeState : NodeStates)
	{
		ClampNodeStateZeroD(NodeState, Settings);
	}
	for (FFluidNetworkEdgeStateZeroD& EdgeState : EdgeStates)
	{
		ClampEdgeStateZeroD(EdgeState, Settings);
	}
}
