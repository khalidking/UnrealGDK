// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "CloudDeploymentConfiguration.h"

#include "SpatialGDKEditorSettings.h"

void FCloudDeploymentConfiguration::InitFromSettings()
{
	const USpatialGDKEditorSettings* Settings = GetDefault<USpatialGDKEditorSettings>();

	AssemblyName = Settings->GetAssemblyName();
	RuntimeVersion = Settings->GetSpatialOSRuntimeVersionForCloud();
	PrimaryDeploymentName = Settings->GetPrimaryDeploymentName();
	PrimaryLaunchConfigPath = Settings->GetPrimaryLaunchConfigPath();
	SnapshotPath = Settings->GetSnapshotPath();
	PrimaryRegionCode = Settings->GetPrimaryRegionCode().ToString();
	MainDeploymentCluster = Settings->GetMainDeploymentCluster();
	DeploymentTags = Settings->GetDeploymentTags();

	bSimulatedPlayersEnabled = Settings->IsSimulatedPlayersEnabled();
	SimulatedPlayerDeploymentName = Settings->GetSimulatedPlayerDeploymentName();
	SimulatedPlayerLaunchConfigPath = Settings->GetSimulatedPlayerLaunchConfigPath();
	SimulatedPlayerRegionCode = Settings->GetSimulatedPlayerRegionCode().ToString();
	SimulatedPlayerCluster = Settings->GetSimulatedPlayerCluster();
	NumberOfSimulatedPlayers = Settings->GetNumberOfSimulatedPlayers();

	bGenerateSchema = Settings->IsGenerateSchemaEnabled();
	bGenerateSnapshot = Settings->IsGenerateSnapshotEnabled();
	BuildConfiguration = Settings->GetAssemblyBuildConfiguration().ToString();
	bBuildClientWorker = Settings->IsBuildClientWorkerEnabled();
	bForceAssemblyOverwrite = Settings->IsForceAssemblyOverwriteEnabled();

	BuildServerExtraArgs = Settings->BuildServerExtraArgs;
	BuildClientExtraArgs = Settings->BuildClientExtraArgs;
	BuildSimulatedPlayerExtraArgs = Settings->BuildSimulatedPlayerExtraArgs;
}
