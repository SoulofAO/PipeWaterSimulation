#pragma once

#include "CoreMinimal.h"
#include "Data/FluidData.h"
#include "Subsystems/WorldSubsystem.h"
#include "FluidNetwork0DSubsystem.generated.h"

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

private:
	void SimulateStep(float SimulationStepTime);
	void UpdateEdgeFlows(float SimulationStepTime);
	void IntegrateNodeVolumes(float SimulationStepTime);
	void UpdateNodePressures();
	void DrawDebugZeroDWorldOverlay() const;

	UPROPERTY(EditAnywhere, Category = "FluidZeroD")
	TArray<FFluidNetworkNodeStateZeroD> NetworkNodeStates;

	UPROPERTY(EditAnywhere, Category = "FluidZeroD")
	TArray<FFluidNetworkEdgeStateZeroD> NetworkEdgeStates;

	float AccumulatedTime = 0.0f;
};
