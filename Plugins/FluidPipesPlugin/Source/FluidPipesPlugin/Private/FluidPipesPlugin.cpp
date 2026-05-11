// Copyright Epic Games, Inc. All Rights Reserved.

#include "FluidPipesPlugin.h"

#include "FluidPipesWorldDebugText.h"

#define LOCTEXT_NAMESPACE "FFluidPipesPluginModule"

void FFluidPipesPluginModule::StartupModule()
{
	FluidPipesWorldDebugTextStartup();
}

void FFluidPipesPluginModule::ShutdownModule()
{
	FluidPipesWorldDebugTextShutdown();
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FFluidPipesPluginModule, FluidPipesPlugin)