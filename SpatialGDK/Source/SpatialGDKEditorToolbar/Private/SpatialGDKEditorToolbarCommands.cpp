// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "SpatialGDKEditorToolbarCommands.h"

#define LOCTEXT_NAMESPACE "FSpatialGDKEditorToolbarModule"

void FSpatialGDKEditorToolbarCommands::RegisterCommands()
{
	UI_COMMAND(CreateSpatialGDKSchema, "Schema", "Creates SpatialOS Unreal GDK schema for assets in memory.", EUserInterfaceActionType::Button, FInputGesture());
	UI_COMMAND(CreateSpatialGDKSchemaFull, "Schema (Full Scan)", "Creates SpatialOS Unreal GDK schema for all assets.", EUserInterfaceActionType::Button, FInputGesture());
	UI_COMMAND(DeleteSchemaDatabase, "Delete schema database", "Deletes the schema database file", EUserInterfaceActionType::Button, FInputGesture());
	UI_COMMAND(CreateSpatialGDKSnapshot, "Snapshot", "Creates SpatialOS Unreal GDK snapshot.", EUserInterfaceActionType::Button, FInputGesture());
	UI_COMMAND(StartNative, "Native", "Use native Unreal networking", EUserInterfaceActionType::Button, FInputGesture());
	UI_COMMAND(StartNoAutomaticConnection, "None", "Doesn't automatically connect to SpatialOS", EUserInterfaceActionType::Button, FInputGesture());
	UI_COMMAND(StartLocalSpatialDeployment, "Local", "Start a local deployment", EUserInterfaceActionType::Button, FInputGesture());
	UI_COMMAND(StartCloudSpatialDeployment, "Cloud", "Start a cloud deployment (Windows-only)", EUserInterfaceActionType::Button, FInputGesture());
	UI_COMMAND(StopSpatialDeployment, "Stop", "Stops SpatialOS.", EUserInterfaceActionType::Button, FInputGesture());
	UI_COMMAND(LaunchInspectorWebPageAction, "Inspector", "Launches default web browser to SpatialOS Inspector.", EUserInterfaceActionType::Button, FInputGesture());
	UI_COMMAND(OpenCloudDeploymentWindowAction, "Deploy", "Opens a configuration menu for cloud deployments.", EUserInterfaceActionType::Button, FInputGesture());
	UI_COMMAND(OpenLaunchConfigurationEditorAction, "Create Launch Configuration", "Opens an editor to create SpatialOS Launch configurations", EUserInterfaceActionType::Button, FInputGesture());
	UI_COMMAND(EnableBuildClientWorker, "Build Client Worker", "If checked, an UnrealClient worker will be built and uploaded before launching the cloud deployment.", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(EnableBuildSimulatedPlayer, "Build Simulated Player", "If checked, a SimulatedPlayer worker will be built and uploaded before launching the cloud deployment.", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(StartSpatialService, "Start Service", "Starts the Spatial service daemon.", EUserInterfaceActionType::Button, FInputGesture());
	UI_COMMAND(StopSpatialService, "Stop Service", "Stops the Spatial service daemon.", EUserInterfaceActionType::Button, FInputGesture());
	UI_COMMAND(EnableSpatialNetworking, "Spatial Networking", "If checked, the SpatialOS networking is used. Otherwise, native Unreal networking is used.", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(GDKEditorSettings, "Editor Settings", "Open the SpatialOS GDK Editor Settings", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(GDKRuntimeSettings, "Runtime Settings", "Open the SpatialOS GDK Runtime Settings", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(LocalDeployment, "Connect to a local deployment", "Automatically connect to a local deployment", EUserInterfaceActionType::RadioButton, FInputChord());
	UI_COMMAND(CloudDeployment, "Connect to a cloud deployment", "Automatically connect to a cloud deployment", EUserInterfaceActionType::RadioButton, FInputChord());
}

#undef LOCTEXT_NAMESPACE
