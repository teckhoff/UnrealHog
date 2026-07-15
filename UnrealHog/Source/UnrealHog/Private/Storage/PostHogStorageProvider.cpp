
#include "Storage/PostHogStorageProvider.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "PostHogFileStorageProvider.h"

TUniquePtr<IPostHogStorageProvider> IPostHogStorageProvider::CreateDefaultProvider()
{
#if PLATFORM_WINDOWS || PLATFORM_LINUX || PLATFORM_MAC
	return MakeUnique<FPostHogFileStorageProvider>();
#else
	return MakeUnique<FPostHogFileStorageProvider>();
#endif
}

bool IPostHogStorageProvider::SaveEvent(const FString& EventId, const TSharedRef<FJsonObject>& EventJsonObject)
{
	FString EventJson;
	
	if (!SerializeJsonObject(EventJsonObject, EventJson))
	{
		return false;
	}

	return SaveEvent(EventId, EventJson);
}

bool IPostHogStorageProvider::SaveState(const FString& StateKey, const TSharedRef<FJsonObject>& StateJsonObject)
{
	FString StateJson;

	if (!SerializeJsonObject(StateJsonObject, StateJson))
	{
		return false;
	}

	return SaveState(StateKey, StateJson);
}

bool IPostHogStorageProvider::SerializeJsonObject(const TSharedRef<FJsonObject>& JsonObject, FString& OutJson)
{
	OutJson.Empty();
	
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
	return FJsonSerializer::Serialize(JsonObject, Writer);
}
