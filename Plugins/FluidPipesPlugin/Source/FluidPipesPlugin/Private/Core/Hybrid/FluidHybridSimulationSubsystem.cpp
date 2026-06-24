#include "Core/Hybrid/FluidHybridSimulationSubsystem.h"

#include "Core/Simulation0D/FluidNetwork0DSubsystem.h"
#include "Core/Simulation1D/FluidSegment1DSubsystem.h"
#include "FluidPipesDrawDebug.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Other/FluidPipesSimulationSettingsLibrary.h"
#include "Other/LazyFluidPipesDeveloperSettings.h"

void UFluidHybridSimulationSubsystem::Tick(float DeltaTime)
{
	const ULazyFluidPipesDeveloperSettings& Settings = FFluidPipesSimulationSettingsLibrary::ResolveSimulationSettings(this);
	if (!FFluidPipesSimulationSettingsLibrary::IsFluidHybridSimulationActive(Settings))
	{
		return;
	}

	if (FFluidPipesSimulationSettingsLibrary::HybridSimulationRequiresCpuGameThreadCoupling(Settings) && !bLoggedNonCpuHybridBackendWarning)
	{
		bLoggedNonCpuHybridBackendWarning = true;
		UKismetSystemLibrary::PrintString(
			this,
			TEXT("Hybrid simulation uses CPU game-thread coupling. Set both backends to CpuGameThread for consistent hybrid stepping."),
			true,
			true,
			FLinearColor::Yellow,
			5.0f);
	}

	UWorld* World = GetWorld();
	UFluidNetwork0DSubsystem* ZeroDSubsystem = World ? World->GetSubsystem<UFluidNetwork0DSubsystem>() : nullptr;
	UFluidSegment1DSubsystem* OneDSubsystem = World ? World->GetSubsystem<UFluidSegment1DSubsystem>() : nullptr;
	if (!ZeroDSubsystem || !OneDSubsystem)
	{
		return;
	}

	if (!bTopologyBuilt)
	{
		RebuildHybridTopologyFromImportedNetworks();
	}

	TimeSinceLastDecompositionUpdate += DeltaTime;
	if (TimeSinceLastDecompositionUpdate >= Settings.HybridDecompositionUpdateIntervalSeconds)
	{
		UpdateHybridDecompositionIfNeeded();
		TimeSinceLastDecompositionUpdate = 0.0f;
	}

	const float HybridStepTime = FFluidPipesSimulationSettingsLibrary::ResolveHybridSimulationStepTime(Settings);
	AccumulatedTime += DeltaTime;
	while (AccumulatedTime >= HybridStepTime)
	{
		SimulateHybridStep(HybridStepTime);
		AccumulatedTime -= HybridStepTime;
	}

	if (FluidPipesShouldEmitScreenDebugMessages())
	{
		int32 ActiveOneDSegmentCount = 0;
		for (bool bSegmentDetailActive : HybridTopology.SegmentDetailActive)
		{
			if (bSegmentDetailActive)
			{
				++ActiveOneDSegmentCount;
			}
		}

		int32 FixedEdgeCount = 0;
		for (bool bEdgeFlowFixedByOneD : HybridTopology.ZeroDEdgeFlowFixedByOneD)
		{
			if (bEdgeFlowFixedByOneD)
			{
				++FixedEdgeCount;
			}
		}

		UKismetSystemLibrary::PrintString(
			this,
			FString::Format(
				TEXT("Hybrid Tick: 1D-active={0}/{1} fixed-edges={2}"),
				{
					FString::FromInt(ActiveOneDSegmentCount),
					FString::FromInt(HybridTopology.SegmentDetailActive.Num()),
					FString::FromInt(FixedEdgeCount)
				}),
			true,
			false,
			FLinearColor(1.0f, 0.5f, 0.0f),
			0.0f);
	}
}

TStatId UFluidHybridSimulationSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UFluidHybridSimulationSubsystem, STATGROUP_Tickables);
}

void UFluidHybridSimulationSubsystem::RebuildHybridTopologyFromImportedNetworks()
{
	UWorld* World = GetWorld();
	UFluidNetwork0DSubsystem* ZeroDSubsystem = World ? World->GetSubsystem<UFluidNetwork0DSubsystem>() : nullptr;
	UFluidSegment1DSubsystem* OneDSubsystem = World ? World->GetSubsystem<UFluidSegment1DSubsystem>() : nullptr;
	if (!World || !ZeroDSubsystem || !OneDSubsystem)
	{
		return;
	}

	FFluidHybridSimulationCouplingLibrary::RebuildHybridTopology(
		World,
		ZeroDSubsystem->GetNodeStates(),
		ZeroDSubsystem->GetEdgeStates(),
		OneDSubsystem->GetSegmentStates(),
		OneDSubsystem->GetSegmentPipeActors(),
		HybridTopology);
	bTopologyBuilt = ZeroDSubsystem->GetNodeStates().Num() > 0 && OneDSubsystem->GetSegmentStates().Num() > 0;
	AccumulatedTime = 0.0f;
	TimeSinceLastDecompositionUpdate = 0.0f;
}

void UFluidHybridSimulationSubsystem::ResetHybridSimulationState()
{
	HybridTopology = FFluidHybridNetworkTopology();
	AccumulatedTime = 0.0f;
	TimeSinceLastDecompositionUpdate = 0.0f;
	bTopologyBuilt = false;
	bLoggedNonCpuHybridBackendWarning = false;
}

void UFluidHybridSimulationSubsystem::SimulateHybridStep(float SimulationStepTime)
{
	UWorld* World = GetWorld();
	const ULazyFluidPipesDeveloperSettings& Settings = FFluidPipesSimulationSettingsLibrary::ResolveSimulationSettings(this);
	UFluidNetwork0DSubsystem* ZeroDSubsystem = World ? World->GetSubsystem<UFluidNetwork0DSubsystem>() : nullptr;
	UFluidSegment1DSubsystem* OneDSubsystem = World ? World->GetSubsystem<UFluidSegment1DSubsystem>() : nullptr;
	if (!World || !ZeroDSubsystem || !OneDSubsystem || !bTopologyBuilt)
	{
		return;
	}

	FFluidHybridSimulationCouplingLibrary::RunHybridSimulationStep(World, Settings, *ZeroDSubsystem, *OneDSubsystem, HybridTopology);
}

void UFluidHybridSimulationSubsystem::UpdateHybridDecompositionIfNeeded()
{
	UWorld* World = GetWorld();
	const ULazyFluidPipesDeveloperSettings& Settings = FFluidPipesSimulationSettingsLibrary::ResolveSimulationSettings(this);
	UFluidSegment1DSubsystem* OneDSubsystem = World ? World->GetSubsystem<UFluidSegment1DSubsystem>() : nullptr;
	if (!World || !OneDSubsystem || !bTopologyBuilt)
	{
		return;
	}

	FFluidHybridSimulationCouplingLibrary::UpdateHybridDecomposition(
		World,
		Settings,
		OneDSubsystem->GetSegmentStates(),
		OneDSubsystem->GetSegmentPipeActors(),
		HybridTopology);
}
