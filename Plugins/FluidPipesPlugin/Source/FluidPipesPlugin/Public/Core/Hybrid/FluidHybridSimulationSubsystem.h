#pragma once

#include "CoreMinimal.h"
#include "Core/Hybrid/FluidHybridSimulationCouplingLibrary.h"
#include "Subsystems/WorldSubsystem.h"
#include "FluidHybridSimulationSubsystem.generated.h"

UCLASS()
class FLUIDPIPESPLUGIN_API UFluidHybridSimulationSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	UFUNCTION(BlueprintCallable, Category = "FluidHybrid")
	void RebuildHybridTopologyFromImportedNetworks();

	UFUNCTION(BlueprintCallable, Category = "FluidHybrid")
	void ResetHybridSimulationState();

private:
	void SimulateHybridStep(float SimulationStepTime);
	void UpdateHybridDecompositionIfNeeded();

	FFluidHybridNetworkTopology HybridTopology;
	float AccumulatedTime = 0.0f;
	float TimeSinceLastDecompositionUpdate = 0.0f;
	bool bTopologyBuilt = false;
	bool bLoggedNonCpuHybridBackendWarning = false;
};
