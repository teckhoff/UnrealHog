# EP-005: Complete SDK event enrichment

## Status and dependencies

- **State:** Completed
- **Blocked by:** EP-003
- **Blocks:** EP-029
- **Parity row:** Default SDK, device, screen, and application properties

## Goal

Add the missing Unity-observable default properties using Unreal-native platform information. Preserve the semantic distinction between PostHog's standard properties and the existing Unreal-specific metadata: no new property may be populated by copying an existing property or by reading from the exact same source.

## Property source contract

| Property | Meaning | Unreal source | Required distinction and fallback |
| --- | --- | --- | --- |
| `$os` | Normalized runtime operating-system family | Normalize the label returned by `FPlatformMisc::GetOSVersions()` | Do not copy `$platform` or derive this from `FPlatformProperties::PlatformName()`. Do not reuse the `$os_version` call to `FPlatformMisc::GetOSVersion()`. Normalize supported labels to stable values such as `Windows`, `macOS`, `Linux`, `Android`, `iOS`, `tvOS`, and `visionOS`; omit an unrecognized label rather than publishing raw enum spelling. |
| `$device_type` | PostHog device/form-factor category | A private classifier fed by an injected Unreal target/form-factor value | Do not derive the result by rewriting `$platform` or `$platform_variant`. Map supported client targets to exact stable categories such as `Mobile`, `Desktop`, and `Web`. Omit server-only, console, TV, XR, and unknown targets until an intentional PostHog category is defined. Stock Unreal Engine 5.8 has no built-in web target, so do not fabricate `Web`; allow a supported platform extension to supply that classification. |
| `$device_manufacturer` | Device original-equipment manufacturer | Platform-specific manufacturer information | Do not parse or reuse `FPlatformMisc::GetDeviceMakeAndModel()`, which remains the source for `$device_model`. Android may use `FAndroidMisc::GetDeviceMake()` and iOS-family targets may use their independently supplied Apple vendor value. Omit the property on desktop and other targets without a truthful manufacturer API; CPU or GPU vendor is not a desktop device manufacturer. |
| `$app_build` | Particular package/deployment build identifier | Native package build number or an explicit build-pipeline-provided value | Do not reuse `UGeneralProjectSettings::ProjectVersion`, which remains `$app_version`. Android may use its package/store version code and iOS-family targets may use `FIOSPlatformMisc::GetBuildNumber()`. Other targets may use a dedicated injected/configured application-build value and must omit the property when none is available. Do not assume `FApp::GetBuildVersion()` is an application build identifier unless the project's build pipeline explicitly owns and sets it. |

It is valid for two independently sourced properties to have the same value on a particular target, for example `$os` and `$platform` both being `Windows`. They must still retain separate contracts and collection paths.

The Unity reference currently assigns `SystemInfo.deviceModel` to both `$device_manufacturer` and `$device_model`. Do not reproduce that duplicate source. Preserve the intended property semantics by reporting a real manufacturer where Unreal provides one and otherwise omitting `$device_manufacturer`.

## Implementation shape

- Introduce a private injectable event-context provider, or an equivalent value object, that independently supplies platform name, platform variant, OS name, OS version, device type, device manufacturer, device model, application name, application version, application build, and screen dimensions.
- Keep normalization and device-type mapping as pure functions over injected inputs. Tests running on Windows must be able to cover mobile, desktop, and supported web-like mappings without invoking another platform's globals.
- Have the enrichment helper serialize the collected context and omit optional strings that are empty or unavailable. The helper must not infer one property from another serialized property.
- Add the four new property names to the centralized reserved-property policy so caller input cannot replace SDK-owned values.

## Required changes

- Add `$os`, `$device_type`, `$device_manufacturer`, and `$app_build` to the private enrichment helper.
- Implement each new property according to the independent source and fallback contract above.
- Normalize OS and device-type values to stable PostHog categories rather than raw platform enum spelling or existing serialized property values.
- Retain existing `$lib`, `$lib_version`, `$os_version`, `$device_model`, dimensions, `$app_name`, and `$app_version` behavior.
- Retain `$platform` and `$platform_variant` as additive Unreal target metadata, not as sources for the standard PostHog properties.
- Reserve `$os`, `$device_type`, `$device_manufacturer`, and `$app_build` alongside the existing SDK-owned property names.
- Omit values when Unreal cannot provide a meaningful value; document platform-specific fallbacks in code.

## Acceptance criteria

- Deterministic tests inject platform/app/screen information and assert exact property names and JSON types.
- Tests inject deliberately different platform, OS, device-model, manufacturer, app-version, and app-build values and prove that none of the new properties aliases an existing field.
- Mobile and desktop mappings have focused cases. A web-like case is required when a supported Unreal target or platform extension supplies one; stock Unreal Engine 5.8 must not be reported as web-like.
- Android and iOS-family manufacturer/build mappings have focused cases, while unsupported desktop manufacturer and absent application-build values are omitted.
- SDK-owned values are constructed independently of user property input.
- Caller-provided values cannot override any of the four new reserved properties.
- Existing Unreal-specific `$platform` and `$platform_variant` fields remain additive metadata and retain their existing behavior.
- Run `Scripts/ci-paths.sh` first if the required `CI` symlinks are missing from the worktree, then run `Scripts/run-windows-tests.sh`; the Unreal Automation tests must pass and their output must be recorded as a Zeroshot validation gate.

## Exclusions

- Do not query hardware identifiers or add fingerprinting data.
- Do not add WMI, DMI, registry, or similar hardware inventory queries solely to populate `$device_manufacturer`.
- Do not implement session, identity, or person-profile policy.

## Unity references

- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/PostHogSDK.cs` (`AddSdkProperties`)
- `Design/Reference/posthog-unity/com.posthog.unity/Runtime/Utilities/PlatformInfo.cs`
