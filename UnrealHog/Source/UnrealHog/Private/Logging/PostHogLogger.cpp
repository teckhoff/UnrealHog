#include "Logging/PostHogLogger.h"

DEFINE_LOG_CATEGORY(LogUnrealHog);

ELogVerbosity::Type PostHogLogger::ToVerbosity(EPostHogLogLevel Level)
{
	switch (Level)
	{
	case EPostHogLogLevel::Debug:
		return ELogVerbosity::VeryVerbose;
	case EPostHogLogLevel::Info:
		return ELogVerbosity::Log;
	case EPostHogLogLevel::Warning:
		return ELogVerbosity::Warning;
	case EPostHogLogLevel::Error:
		return ELogVerbosity::Error;
	case EPostHogLogLevel::None:
		return ELogVerbosity::NoLogging;
	default:
		// Preserve the public default of Warning for any unmapped value.
		return ELogVerbosity::Warning;
	}
}

void PostHogLogger::ApplyConfiguredLevel(EPostHogLogLevel Level)
{
#if !NO_LOGGING
	LogUnrealHog.SetVerbosity(ToVerbosity(Level));
#else
	static_cast<void>(Level);
#endif
}
