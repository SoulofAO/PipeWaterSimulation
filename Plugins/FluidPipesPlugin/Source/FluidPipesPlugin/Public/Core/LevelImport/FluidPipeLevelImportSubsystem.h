#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "FluidPipeLevelImportSubsystem.generated.h"

UCLASS()
class FLUIDPIPESPLUGIN_API UFluidPipeLevelImportSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;

private:
	void RunLevelPipeImport();

	bool bImportFinished = false;
};
