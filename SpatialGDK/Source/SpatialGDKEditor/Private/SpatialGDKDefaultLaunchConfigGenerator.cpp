// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "SpatialGDKDefaultLaunchConfigGenerator.h"

#include "SpatialGDKEditorSettings.h"

#include "Serialization/JsonWriter.h"
#include "Misc/FileHelper.h"

#include "Misc/MessageDialog.h"

#include "ISettingsModule.h"
#include "SpatialGDKSettings.h"

DEFINE_LOG_CATEGORY(LogSpatialGDKDefaultLaunchConfigGenerator);

#define LOCTEXT_NAMESPACE "SpatialGDKDefaultLaunchConfigGenerator"

using namespace SpatialGDK;

namespace
{
bool WriteFlagSection(TSharedRef<TJsonWriter<>> Writer, const FString& Key, const FString& Value)
{
	Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("name"), Key);
		Writer->WriteValue(TEXT("value"), Value);
	Writer->WriteObjectEnd();

	return true;
}

bool WriteWorkerSection(TSharedRef<TJsonWriter<>> Writer, const FWorkerTypeLaunchSection& Worker)
{
	Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("worker_type"), *Worker.WorkerTypeName.ToString());
		Writer->WriteArrayStart(TEXT("flags"));
			for (const auto& Flag : Worker.Flags)
			{
				WriteFlagSection(Writer, Flag.Key, Flag.Value);
			}
		Writer->WriteArrayEnd();
		Writer->WriteArrayStart(TEXT("permissions"));
			Writer->WriteObjectStart();
			if (Worker.WorkerPermissions.bAllPermissions)
			{
				Writer->WriteObjectStart(TEXT("all"));
				Writer->WriteObjectEnd();
			}
			else
			{
				Writer->WriteObjectStart(TEXT("entity_creation"));
					Writer->WriteValue(TEXT("allow"), Worker.WorkerPermissions.bAllowEntityCreation);
				Writer->WriteObjectEnd();
				Writer->WriteObjectStart(TEXT("entity_deletion"));
					Writer->WriteValue(TEXT("allow"), Worker.WorkerPermissions.bAllowEntityDeletion);
				Writer->WriteObjectEnd();
				Writer->WriteObjectStart(TEXT("entity_query"));
					Writer->WriteValue(TEXT("allow"), Worker.WorkerPermissions.bAllowEntityQuery);
					Writer->WriteArrayStart("components");
					for (const FString& Component : Worker.WorkerPermissions.Components)
					{
						Writer->WriteValue(Component);
					}
					Writer->WriteArrayEnd();
				Writer->WriteObjectEnd();
			}
			Writer->WriteObjectEnd();
		Writer->WriteArrayEnd();
		if (Worker.MaxConnectionCapacityLimit > 0)
		{
			Writer->WriteObjectStart(TEXT("connection_capacity_limit"));
				Writer->WriteValue(TEXT("max_capacity"), Worker.MaxConnectionCapacityLimit);
			Writer->WriteObjectEnd();
		}
		if (Worker.bLoginRateLimitEnabled)
		{
			Writer->WriteObjectStart(TEXT("login_rate_limit"));
				Writer->WriteValue(TEXT("duration"), Worker.LoginRateLimit.Duration);
				Writer->WriteValue(TEXT("requests_per_duration"), Worker.LoginRateLimit.RequestsPerDuration);
			Writer->WriteObjectEnd();
		}
	Writer->WriteObjectEnd();

	return true;
}

bool WriteLoadbalancingSection(TSharedRef<TJsonWriter<>> Writer, const FName& WorkerType, const int32 Columns, const int32 Rows, const bool ManualWorkerConnectionOnly)
{
	Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("layer"), *WorkerType.ToString());
		Writer->WriteObjectStart("rectangle_grid");
			Writer->WriteValue(TEXT("cols"), Columns);
			Writer->WriteValue(TEXT("rows"), Rows);
		Writer->WriteObjectEnd();
		Writer->WriteObjectStart(TEXT("options"));
			Writer->WriteValue(TEXT("manual_worker_connection_only"), ManualWorkerConnectionOnly);
		Writer->WriteObjectEnd();
	Writer->WriteObjectEnd();

	return true;
}

}

bool GenerateDefaultLaunchConfig(const FString& LaunchConfigPath, const FSpatialLaunchConfigDescription* InLaunchConfigDescription)
{
	if (InLaunchConfigDescription != nullptr)
	{
		const FSpatialLaunchConfigDescription& LaunchConfigDescription = *InLaunchConfigDescription;

		FString Text;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);

		// Populate json file for launch config
		Writer->WriteObjectStart(); // Start of json
			Writer->WriteValue(TEXT("template"), LaunchConfigDescription.Template); // Template section
			Writer->WriteObjectStart(TEXT("world")); // World section begin
				Writer->WriteObjectStart(TEXT("dimensions"));
					Writer->WriteValue(TEXT("x_meters"), LaunchConfigDescription.World.Dimensions.X);
					Writer->WriteValue(TEXT("z_meters"), LaunchConfigDescription.World.Dimensions.Y);
				Writer->WriteObjectEnd();
			Writer->WriteValue(TEXT("chunk_edge_length_meters"), LaunchConfigDescription.World.ChunkEdgeLengthMeters);
			Writer->WriteArrayStart(TEXT("legacy_flags"));
			for (auto& Flag : LaunchConfigDescription.World.LegacyFlags)
			{
				WriteFlagSection(Writer, Flag.Key, Flag.Value);
			}
			Writer->WriteArrayEnd();
			Writer->WriteArrayStart(TEXT("legacy_javaparams"));
			for (auto& Parameter : LaunchConfigDescription.World.LegacyJavaParams)
			{
				WriteFlagSection(Writer, Parameter.Key, Parameter.Value);
			}
			Writer->WriteArrayEnd();
			Writer->WriteObjectStart(TEXT("snapshots"));
				Writer->WriteValue(TEXT("snapshot_write_period_seconds"), LaunchConfigDescription.World.SnapshotWritePeriodSeconds);
			Writer->WriteObjectEnd();
		Writer->WriteObjectEnd(); // World section end
		Writer->WriteObjectStart(TEXT("load_balancing")); // Load balancing section begin
			Writer->WriteArrayStart("layer_configurations");
			for (const FWorkerTypeLaunchSection& Worker : LaunchConfigDescription.ServerWorkers)
			{
				WriteLoadbalancingSection(Writer, Worker.WorkerTypeName, Worker.Columns, Worker.Rows, Worker.bManualWorkerConnectionOnly);
			}
			Writer->WriteArrayEnd();
			Writer->WriteObjectEnd(); // Load balancing section end
			Writer->WriteArrayStart(TEXT("workers")); // Workers section begin
			for (const FWorkerTypeLaunchSection& Worker : LaunchConfigDescription.ServerWorkers)
			{
				WriteWorkerSection(Writer, Worker);
			}
			// Write the client worker section
			FWorkerTypeLaunchSection ClientWorker;
			ClientWorker.WorkerTypeName = SpatialConstants::DefaultClientWorkerType;
			ClientWorker.WorkerPermissions.bAllPermissions = true;
			ClientWorker.bLoginRateLimitEnabled = false;
			WriteWorkerSection(Writer, ClientWorker);
			Writer->WriteArrayEnd(); // Worker section end
		Writer->WriteObjectEnd(); // End of json

		Writer->Close();

		if (!FFileHelper::SaveStringToFile(Text, *LaunchConfigPath))
		{
			UE_LOG(LogSpatialGDKDefaultLaunchConfigGenerator, Error, TEXT("Failed to write output file '%s'. It might be that the file is read-only."), *LaunchConfigPath);
			return false;
		}

		return true;
	}

	return false;
}

bool ValidateGeneratedLaunchConfig(const FSpatialLaunchConfigDescription& LaunchConfigDesc)
{
	const USpatialGDKSettings* SpatialGDKRuntimeSettings = GetDefault<USpatialGDKSettings>();

	if (const FString* EnableChunkInterest = LaunchConfigDesc.World.LegacyFlags.Find(TEXT("enable_chunk_interest")))
	{
		if (*EnableChunkInterest == TEXT("true"))
		{
			const EAppReturnType::Type Result = FMessageDialog::Open(EAppMsgType::YesNo, FText::FromString(TEXT("The legacy flag \"enable_chunk_interest\" is set to true in the generated launch configuration. Chunk interest is not supported and this flag needs to be set to false.\n\nDo you want to configure your launch config settings now?")));

			if (Result == EAppReturnType::Yes)
			{
				FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer("Project", "SpatialGDKEditor", "Editor Settings");
			}

			return false;
		}
	}

	if (!SpatialGDKRuntimeSettings->bEnableHandover && LaunchConfigDesc.ServerWorkers.ContainsByPredicate([](const FWorkerTypeLaunchSection& Section)
	{
		return (Section.Rows * Section.Columns) > 1;
	}))
	{
		const EAppReturnType::Type Result = FMessageDialog::Open(EAppMsgType::YesNo, FText::FromString(TEXT("Property handover is disabled and a zoned deployment is specified.\nThis is not supported.\n\nDo you want to configure your project settings now?")));

		if (Result == EAppReturnType::Yes)
		{
			FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer("Project", "SpatialGDKEditor", "Runtime Settings");
		}

		return false;
	}

	if (LaunchConfigDesc.ServerWorkers.ContainsByPredicate([](const FWorkerTypeLaunchSection& Section)
	{
		return (Section.Rows * Section.Columns) < Section.NumEditorInstances;
	}))
	{
		const EAppReturnType::Type Result = FMessageDialog::Open(EAppMsgType::YesNo, FText::FromString(TEXT("Attempting to launch too many servers for load balance configuration.\nThis is not supported.\n\nDo you want to configure your project settings now?")));

		if (Result == EAppReturnType::Yes)
		{
			FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer("Project", "SpatialGDKEditor", "Editor Settings");
		}

		return false;
	}

	if (!SpatialGDKRuntimeSettings->ServerWorkerTypes.Contains(SpatialGDKRuntimeSettings->DefaultWorkerType.WorkerTypeName))
	{
		const EAppReturnType::Type Result = FMessageDialog::Open(EAppMsgType::YesNo, FText::FromString(TEXT("Default Worker Type is invalid, please choose a valid worker type as the default.\n\nDo you want to configure your project settings now?")));

		if (Result == EAppReturnType::Yes)
		{
			FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer("Project", "SpatialGDKEditor", "Runtime Settings");
		}

		return false;
	}

	if (SpatialGDKRuntimeSettings->bEnableOffloading)
	{
		for (const TPair<FName, FActorGroupInfo>& ActorGroup : SpatialGDKRuntimeSettings->ActorGroups)
		{
			if (!SpatialGDKRuntimeSettings->ServerWorkerTypes.Contains(ActorGroup.Value.OwningWorkerType.WorkerTypeName))
			{
				const EAppReturnType::Type Result = FMessageDialog::Open(EAppMsgType::YesNo, FText::FromString(FString::Printf(TEXT("Actor Group '%s' has an invalid Owning Worker Type, please choose a valid worker type.\n\nDo you want to configure your project settings now?"), *ActorGroup.Key.ToString())));

				if (Result == EAppReturnType::Yes)
				{
					FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer("Project", "SpatialGDKEditor", "Runtime Settings");
				}

				return false;
			}
		}
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
