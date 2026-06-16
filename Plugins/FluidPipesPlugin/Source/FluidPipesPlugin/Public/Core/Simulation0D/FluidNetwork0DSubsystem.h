#pragma once

#include "CoreMinimal.h"
#include "Data/FluidData.h"
#include "Subsystems/WorldSubsystem.h"
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

private:
	void SimulateStep(float SimulationStepTime);
	void SimulateStepInternal(float SimulationStepTime, const ULazyFluidPipesDeveloperSettings& Settings);
	void RefreshNetworkNodeExternalFlowsFromWorldPointActors();
	void UpdateEdgeFlows(float SimulationStepTime, bool bUseQuadraticFrictionFromPipePhysics);
	void IntegrateNodeVolumes(float SimulationStepTime);
	void UpdateNodePressures();
	void RecalculateNodeComplianceFromEdges(bool bAutoDeriveLumpedParametersFromPipePhysics);
	float ComputeMaximumStableSubstepTime(const ULazyFluidPipesDeveloperSettings& Settings) const;
	void DrawDebugZeroDWorldOverlay() const;

	UPROPERTY(EditAnywhere, Category = "FluidZeroD")
	TArray<FFluidNetworkNodeStateZeroD> NetworkNodeStates;

	UPROPERTY(EditAnywhere, Category = "FluidZeroD")
	TArray<FFluidNetworkEdgeStateZeroD> NetworkEdgeStates;

	float AccumulatedTime = 0.0f;
};
