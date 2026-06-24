#pragma once

#include "CoreMinimal.h"
#include "Core/Simulation0D/BaseFluidNetwork0DSimulation.h"
#include "Data/FluidData.h"
#include "Other/FluidPipesSimulationSettingsTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "Templates/UniquePtr.h"
#include "FluidNetwork0DSubsystem.generated.h"

class ULazyFluidPipesDeveloperSettings;

UCLASS()
class FLUIDPIPESPLUGIN_API UFluidNetwork0DSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	UFUNCTION(BlueprintCallable, Category = "FluidZeroD")
	void ResetSimulationState();

	const TArray<FFluidNetworkNodeStateZeroD>& GetNodeStates() const;

	const TArray<FFluidNetworkEdgeStateZeroD>& GetEdgeStates() const;

	UFUNCTION(BlueprintCallable, Category = "FluidZeroD")
	void ApplyImportedZeroDNetwork(const TArray<FFluidNetworkNodeStateZeroD>& Nodes, const TArray<FFluidNetworkEdgeStateZeroD>& Edges);

	UFUNCTION(BlueprintCallable, Category = "FluidZeroD")
	void RebuildActiveSimulationForCurrentSettings();

	void RunCpuGameThreadHybridSimulationStep(float SimulationStepTime, const TArray<bool>& EdgeFlowFixedByOneDMask);

	TArray<FFluidNetworkNodeStateZeroD>& GetMutableNetworkNodeStates();

	TArray<FFluidNetworkEdgeStateZeroD>& GetMutableNetworkEdgeStates();

	void RefreshNetworkNodeExternalFlowsForHybrid();

private:
	void SimulateStep(float SimulationStepTime);
	void RefreshNetworkNodeExternalFlowsFromWorldPointActors();
	void DrawDebugZeroDWorldOverlay();
	void ReadbackAndDrawOffGameThreadZeroDDebug();
	void EnsureActiveZeroDSimulationMatchesSettings(const ULazyFluidPipesDeveloperSettings& Settings);
	bool UsesOffGameThreadZeroDSimulationState() const;
	static FString BuildZeroDSimulationBackendDisplayName(EFluidNetworkSimulationZeroDBackend Backend);

	UPROPERTY(EditAnywhere, Category = "FluidZeroD")
	TArray<FFluidNetworkNodeStateZeroD> NetworkNodeStates;

	UPROPERTY(EditAnywhere, Category = "FluidZeroD")
	TArray<FFluidNetworkEdgeStateZeroD> NetworkEdgeStates;

	float AccumulatedTime = 0.0f;

	TUniquePtr<FBaseFluidNetwork0DSimulation> ActiveSimulation;
	EFluidNetworkSimulationZeroDBackend ActiveZeroDSimulationBackend = EFluidNetworkSimulationZeroDBackend::CpuGameThread;
};
