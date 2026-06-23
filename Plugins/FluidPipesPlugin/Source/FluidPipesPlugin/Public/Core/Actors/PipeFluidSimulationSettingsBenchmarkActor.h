#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PipeFluidSimulationSettingsBenchmarkActor.generated.h"

class APipeFluidBenchmarkNetworkActor;
class ULazyFluidPipesDeveloperSettings;

USTRUCT()
struct FFluidSimulationSettingsBenchmarkResult
{
	GENERATED_BODY()

	UPROPERTY()
	FString NetworkDescription;

	UPROPERTY()
	FString ProfileDescription;

	UPROPERTY()
	double AverageCombinedSimulationMilliseconds = 0.0;

	UPROPERTY()
	double AverageOneDSimulationMilliseconds = 0.0;

	UPROPERTY()
	double AverageZeroDSimulationMilliseconds = 0.0;

	UPROPERTY()
	int32 FrameSampleCount = 0;
};

UCLASS()
class FLUIDPIPESPLUGIN_API APipeFluidSimulationSettingsBenchmarkActor : public AActor
{
	GENERATED_BODY()

public:
	APipeFluidSimulationSettingsBenchmarkActor();

	UPROPERTY(EditAnywhere, Instanced, Category = "FluidBenchmark|Profiles")
	TArray<TObjectPtr<ULazyFluidPipesDeveloperSettings>> SimulationSettingsProfiles;

	UPROPERTY(EditAnywhere, Category = "FluidBenchmark|Networks")
	TArray<TObjectPtr<APipeFluidBenchmarkNetworkActor>> BenchmarkNetworkActors;

	UPROPERTY(EditAnywhere, Category = "FluidBenchmark|Timing", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float WarmupDurationSeconds = 2.0f;

	UPROPERTY(EditAnywhere, Category = "FluidBenchmark|Timing", meta = (ClampMin = "0.1", UIMin = "0.1"))
	float MeasurementDurationSeconds = 5.0f;

	UFUNCTION(BlueprintCallable, Category = "FluidBenchmark")
	void StopSimulationSettingsBenchmark();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	enum class EFluidSimulationSettingsBenchmarkPhase : uint8
	{
		Idle,
		Warmup,
		Measure
	};

	void StartBenchmarkRun();
	void ApplyCurrentBenchmarkPair();
	void AdvanceBenchmarkPhase();
	void FinishCurrentBenchmarkPairMeasurement();
	void CompleteBenchmarkAndLogResults();
	void RestoreDefaultSimulationSettingsOverride();
	void ClearAllBenchmarkNetworks();
	int32 GetTotalBenchmarkPairCount() const;
	bool HasAnotherBenchmarkPairAfterCurrent() const;

	UPROPERTY(Transient)
	TArray<FFluidSimulationSettingsBenchmarkResult> BenchmarkResults;

	EFluidSimulationSettingsBenchmarkPhase BenchmarkPhase = EFluidSimulationSettingsBenchmarkPhase::Idle;
	int32 CurrentNetworkIndex = INDEX_NONE;
	int32 CurrentProfileIndex = INDEX_NONE;
	int32 LastBuiltNetworkIndex = INDEX_NONE;
	double PhaseStartWorldSeconds = 0.0;
	bool bBenchmarkRunning = false;
};
