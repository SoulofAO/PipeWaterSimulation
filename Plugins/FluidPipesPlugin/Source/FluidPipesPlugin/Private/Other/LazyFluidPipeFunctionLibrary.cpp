// Fill out your copyright notice in the Description page of Project Settings.


#include "Other/LazyFluidPipeFunctionLibrary.h"
#include "Core/Simulation0D/FluidNetwork0DSubsystem.h"
#include "Core/Simulation1D/FluidSegment1DSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"

UFluidNetwork0DSubsystem* ULazyFluidPipeFunctionLibrary::GetFluidNetwork0DSubsystem(const UObject* WorldContextObject)
{
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	if (!World)
	{
		return nullptr;
	}

	return World->GetSubsystem<UFluidNetwork0DSubsystem>();
}

UFluidSegment1DSubsystem* ULazyFluidPipeFunctionLibrary::GetFluidSegment1DSubsystem(const UObject* WorldContextObject)
{
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	if (!World)
	{
		return nullptr;
	}

	return World->GetSubsystem<UFluidSegment1DSubsystem>();
}

