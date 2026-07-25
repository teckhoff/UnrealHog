---
layout: default
title: Installation
parent: Getting Started
nav_order: 1
---

# Installation

Installing UnrealHog follows the same general process whether you compile the plugin source or use a precompiled build, but you must choose files that match your engine and platform.

{: .note-title}
> Unsure of your Unreal Engine version?
>
> If you're not sure whether you're on a source build or a launcher build, you're probably on the Epic Games Launcher version.

{% capture launcher_build_version %}
If you are using Unreal Engine through the Epic Games Launcher, you can use the packaged plugin.
It should be listed as `UnrealHogLauncher-5.8.zip` on the releases page.

{: .warning-title}
> Platform Limitations
>
> Due to packaging limitations, only Windows x64 and Linux are currently available in prepackaged versions of the plugin. To use it on other platforms (both in editor, and in packaged games), you need to [package the plugin yourself using the Unreal Automation Tool, or use UnrealHog from source](packaging.html).
>
> In the future, Android and Windows ARM64 will be available.
>
> macOS and iOS support may be available in the future.

{: .note-title}
> Version Limitations
>
> The only version of Unreal Engine that UnrealHog supports officially through the Epic Games Launcher is **5.8**.
>
> Each version of the engine requires it to be packaged separately.
>
> It's possible for you to package it yourself through Unreal Automation Tool, provided you have [Unreal Engine set up for working with C++](https://dev.epicgames.com/documentation/unreal-engine/setting-up-your-development-environment-for-cplusplus-in-unreal-engine).
>
> See [Packaging the Plugin](packaging.html) for more information.
{% endcapture %}

{% capture source_build_version %}
If you are using Unreal Engine compiled from source, you only need to drop the source code into your project's `/Plugins/` and compile your project through your IDE.

The source code is provided for your convenience as `UnrealHogSource.zip` on the releases page.

{: .note-title}
> Version Uncertainty
>
> UnrealHog has only been tested to work with Unreal Engine **5.8**.
>
> You might be able to use it with an earlier version, but there are no promises that it will work without modification.
{% endcapture %}

{% include language-toggle.html id="engine_build_version" cpp=launcher_build_version blueprint_text=source_build_version cpp_label="Epic Games Launcher Build" blueprint_label="Source Build" samples_default=false %}

## Add The Plugin

1. Download the correct version from the [releases](https://github.com/teckhoff/UnrealHog/releases/latest) page.
2. Unzip the release of UnrealHog you downloaded.
3. Move the unzipped folder into the `/Plugins/` folder of your project or engine.
4. Open (or restart) the Unreal Editor.
5. Open **Edit > Plugins**, find **UnrealHog**, enable it, and restart the editor when prompted.

{: .note-title}
> Should I install the plugin in my engine or my project?
>
> If you are going to use the plugin in multiple projects, it may be worth installing it to your engine, at `path/to/engine/Engine/Plugins`.
>
> If you are trying out UnrealHog for the first time, or want to keep versions localized to specific projects, install it at `path/to/project/Plugins`.
>
> If your project does not contain a `Plugins` folder, you can create it.

## Common Installation Issues

### Unreal Engine asks to rebuild the plugin

<img class="posthog-rebuild-plugin-popup" src="{{ '/assets/images/plugin/rebuild_plugin.webp' | relative_url }}" alt="The prompt Unreal Engine pops up when using an improper/unbuilt plugin version.">

If Unreal Engine says the plugin was built with a different engine version, or that the  UnrealHog module is missing and should be rebuilt, first make sure you installed the correct distribution for your engine, platform, and build type.

{: .note}
> Packaged plugins contain precompiled binaries. These binaries are built for specific Unreal Engine versions, platforms, and engine distributions.
>
> Do not assume a binary packaged for another engine version will be compatible even when the displayed version numbers may be similar or the same.
>
> Due to engine build ID metadata, a **5.8** plugin packaged with an Epic Games Launcher build of the engine will usually not run on a source-build version of **5.8**, and vice versa.

For Epic Games Launcher builds, make sure you downloaded the packaged plugin for your exact engine version, such as `UnrealHogLauncher-5.8.zip` for Unreal Engine 5.8. If you are using a source build of Unreal Engine, use `UnrealHogSource.zip`, add it to your project's `/Plugins/` folder, and compile your project through your IDE.

If the rebuild prompt still appears, close the editor, remove any stale `Binaries` and `Intermediate` folders inside `Plugins/UnrealHog`, then rebuild and reopen the project.

### UnrealHog does not appear in the Plugins window

Check that the unzipped plugin folder contains `UnrealHog.uplugin` directly inside it, and that the folder is placed under either `path/to/project/Plugins` or `path/to/engine/Engine/Plugins`. If the release zip created an extra nested folder, move the inner `UnrealHog` folder into `Plugins` instead.

## Updating your `Build.cs`

If you are intending to access any features of UnrealHog in C++, you have to add it as a dependency module in your project's `Build.cs` file.

```c#
    PrivateDependencyModuleNames.AddRange(new string[]
    {
        // ...
        "UnrealHog"
        // ...
    });
```

{: .note}
> As a rule of thumb, you should start with it in PrivateDependencyModuleNames.
>
> You only need to make it public if you are going to use types from UnrealHog in any of your public header files.


## Configure PostHog

Open **Edit > Project Settings > Analytics > PostHog** and set the public project API key.

{: .note-title}
> Where is PostHog in my Project Settings?
>
> UnrealHog displays PostHog settings under the Analytics category, which may be at the very bottom of your project settings.

<img class="posthog-settings-display" src="{{ '/assets/images/settings/posthog-basic-configuration.webp' | relative_url }}" alt="The PostHog configuration in Project Settings.">

Then, you can choose a host:

- `US` resolves to `https://us.i.posthog.com`.
- `EU` resolves to `https://eu.i.posthog.com`.
- `Custom` uses the URL entered in the Host field.

After you enable the plugin and choose your host, there are two options that affect whether events will start being sent to your PostHog instance:
- `Analytics Enabled` is the developer kill switch and defaults to `true`.
- `Default User Opt-In` controls the user's starting consent state and defaults to `false`, so no event payload, queue record, file, or HTTP request is created until consent is granted.

## Verify the Installation

Use this checklist before adding analytics throughout your game:

1. Restart Unreal Editor after enabling the plugin.
2. Open **Edit > Plugins** and confirm that **UnrealHog** is enabled without a rebuild or missing-module warning.
3. Open **Edit > Project Settings > Analytics > PostHog** and confirm that the settings page appears.
4. If you use C++, build your Editor target after adding `"UnrealHog"` to the game module's dependencies. Include `Subsystems/PostHogRuntimeSubsystem.h` in a small consumer to verify that the public module is available.
5. Use the [Quickstart](quickstart.html) with a development PostHog project to grant test consent, capture `unrealhog_quickstart_completed`, flush once, and confirm the event.

{: .warning}
> Do not use a production PostHog project for installation tests, and do not ship the quickstart's forced opt-in. Production consent must reflect the player's actual choice.

If the plugin loads but the event does not arrive, use [Event Delivery and Troubleshooting](event-delivery-and-troubleshooting.html).

{% include page-footer.html title="Quickstart" url="/quickstart.html" %}
