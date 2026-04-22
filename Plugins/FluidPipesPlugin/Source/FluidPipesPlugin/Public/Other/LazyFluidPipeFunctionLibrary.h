// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "LazyFluidPipeFunctionLibrary.generated.h"

class UFluidNetwork0DSubsystem;
class UFluidSegment1DSubsystem;

UCLASS()
class FLUIDPIPESPLUGIN_API ULazyFluidPipeFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "FluidSubsystem", meta = (WorldContext = "WorldContextObject"))
	static UFluidNetwork0DSubsystem* GetFluidNetwork0DSubsystem(const UObject* WorldContextObject);

	UFUNCTION(BlueprintPure, Category = "FluidSubsystem", meta = (WorldContext = "WorldContextObject"))
	static UFluidSegment1DSubsystem* GetFluidSegment1DSubsystem(const UObject* WorldContextObject);
};
