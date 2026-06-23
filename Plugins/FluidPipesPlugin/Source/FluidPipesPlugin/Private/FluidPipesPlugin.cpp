// Copyright Epic Games, Inc. All Rights Reserved.

#include "FluidPipesPlugin.h"

#include "FluidPipesWorldDebugText.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

#define LOCTEXT_NAMESPACE "FFluidPipesPluginModule"

void FFluidPipesPluginModule::StartupModule()
{
	FluidPipesWorldDebugTextStartup();
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("FluidPipesPlugin"));
	if (Plugin.IsValid())
	{
		const FString PluginShaderDirectory = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders"));
		AddShaderSourceDirectoryMapping(TEXT("/Plugin/FluidPipesPlugin"), PluginShaderDirectory);
	}
}

void FFluidPipesPluginModule::ShutdownModule()
{
	FluidPipesWorldDebugTextShutdown();
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FFluidPipesPluginModule, FluidPipesPlugin)