#pragma once

#include "CoreMinimal.h"
#include "Delegates/Delegate.h"
#include "Dom/JsonObject.h"

enum class EPostHogBeforeSendResult : uint8
{
	Continue,
	Drop,
	Failure
};

class UNREALHOG_API FPostHogBeforeSendEvent
{
public:
	FPostHogBeforeSendEvent(const FString& InEventName,
		const FString& InDistinctId,
		const FString& InEventUuid,
		const FString& InTimestamp,
		FJsonObject& InProperties);

	const FString& GetEventName() const { return EventName; }
	const FString& GetDistinctId() const { return DistinctId; }
	const FString& GetEventUuid() const { return EventUuid; }
	const FString& GetTimestamp() const { return Timestamp; }
	const FJsonObject& GetProperties() const { return Properties; }
	FJsonObject& GetMutableProperties() { return Properties; }

private:
	const FString& EventName;
	const FString& DistinctId;
	const FString& EventUuid;
	const FString& Timestamp;
	FJsonObject& Properties;
};

DECLARE_DELEGATE_RetVal_OneParam(EPostHogBeforeSendResult, FPostHogBeforeSendDelegate, FPostHogBeforeSendEvent&);
