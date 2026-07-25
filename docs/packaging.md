---
layout: default
title: Packaging the Plugin
parent: Advanced
nav_order: 2
---

# Packaging the Plugin

UnrealHog is a C++ plugin. Whether you build it inside a project or create a precompiled distribution, Unreal Engine must be [set up for C++ development](https://dev.epicgames.com/documentation/unreal-engine/setting-up-your-development-environment-for-cplusplus-in-unreal-engine) with the compiler, SDK, and target-platform tools required by your engine version.

{: .note}
> Content-only and Blueprint-only plugins do not always require a compiler. This exception does not apply to UnrealHog because its runtime module is implemented in C++.

There are three common ways to work with the plugin:

| Workflow | Result | Best for |
|:---------|:-------|:---------|
| Add the source to a project | UnrealHog is compiled as part of that project. | Local development and source-engine projects |
| Run `BuildPlugin` with Unreal Automation Tool | A standalone, precompiled plugin distribution is created. | Releases, CI, and multiple target platforms |
| Select **Package...** in the Plugin Browser | The editor runs the plugin-packaging workflow for you. | Occasional local distributions |

The last two workflows use the same underlying Unreal Automation Tool packaging process. Adding the source directly to a project builds the plugin for that project, but does not create a standalone packaged distribution.

## Build the Source Inside a Project

This is the simplest way to use UnrealHog while developing a project:

1. Download `UnrealHogSource.zip` from the [releases page](https://github.com/teckhoff/UnrealHog/releases/latest), or use a source checkout.
2. Place the plugin at `path/to/project/Plugins/UnrealHog`.
3. Check that `UnrealHog.uplugin` is directly inside that folder.
4. Open the project and accept Unreal Engine's prompt to build the missing UnrealHog module.
5. If the editor cannot complete the build, close it and compile your project's Editor target through Visual Studio, Rider, or your normal UnrealBuildTool workflow.

When UnrealBuildTool compiles the project, it also compiles enabled plugins that contain source modules.

{: .warning-title}
> Generated plugin files
>
> `Binaries` and `Intermediate` contain generated, engine-specific build output. Do not copy these directories from an unrelated engine version or platform and expect Unreal Engine to reuse them.
>
> If a source copy contains stale output, close the editor, remove `Plugins/UnrealHog/Binaries` and `Plugins/UnrealHog/Intermediate`, and rebuild the project.

{: .note-title}
> This is a build, not a packaged distribution
>
> Copying source into a project's `Plugins` folder makes UnrealHog part of that project. Use one of the following packaging workflows when you need a precompiled plugin that can be installed without rebuilding its source.

## Package with Unreal Automation Tool

For repeatable releases and CI, run the `BuildPlugin` command through Unreal Automation Tool (UAT). The following Windows example packages UnrealHog for Windows x64 and Linux:

```bat
"path\to\unreal\Engine\Build\BatchFiles\RunUAT.bat" BuildPlugin ^
  -Plugin="path\to\project\Plugins\UnrealHog\UnrealHog.uplugin" ^
  -Package="path\to\output\UnrealHog" ^
  -Rocket ^
  -StrictIncludes ^
  -TargetPlatforms=Win64+Linux
```

On Linux, use `RunUAT.sh`:

```shell
path/to/unreal/Engine/Build/BatchFiles/RunUAT.sh BuildPlugin \
  -Plugin="path/to/project/Plugins/UnrealHog/UnrealHog.uplugin" \
  -Package="path/to/output/UnrealHog" \
  -Rocket \
  -StrictIncludes \
  -TargetPlatforms=Linux
```

The important arguments are:

- `-Plugin` is the full path to the source `.uplugin` descriptor.
- `-Package` is the output directory for the distributable plugin.
- `-TargetPlatforms` is a `+`-separated list of platforms to compile.
- `-Rocket` refers to packaging the plugin for the Epic Games Launcher engine build. It can be omitted when it is not appropriate for the engine distribution being used.
- `-StrictIncludes` enables stricter include validation during the build.

{: .warning-title}
> Choose a dedicated output directory
>
> Treat the directory passed to `-Package` as disposable build output. Do not point it at the source plugin, the project, or a directory containing files you need to keep.

UAT does not install missing platform support. Every requested target must be supported by that engine installation, and its compiler and SDK must already be available. For example, building the Linux target on Windows requires Unreal Engine's Linux cross-compilation toolchain.

To see the arguments supported by your exact engine installation, run:

```bat
"path\to\unreal\Engine\Build\BatchFiles\RunUAT.bat" BuildPlugin -Help
```

{: .note-title}
> Engine and platform compatibility
>
> Package UnrealHog separately for every Unreal Engine version, engine distribution, and target platform you support. A plugin packaged with an Epic Games Launcher build may not load in a source-built engine with the same displayed version because their build metadata can differ.
>
> A single host cannot necessarily build every target. Use an appropriate build host and installed SDK for platforms that do not support cross-compilation.

## Package from the Unreal Editor

The Plugin Browser provides a graphical frontend to the same packaging workflow:

1. Open the project in Unreal Editor.
2. Select **Edit > Plugins**.
3. Find and select **UnrealHog**.
4. Select **Package...**.
5. Choose a dedicated output directory and wait for the build to complete.

This is convenient when UnrealHog is already discoverable by a project and you only need an occasional local package. It does not avoid compilation: the editor invokes Unreal Automation Tool to validate and compile the plugin before creating the distribution.

If the plugin prevents the project from opening, or you need consistent flags and multiple target platforms, use `BuildPlugin` directly instead.

## Verify the Package

A successful UAT command is an important build check, but the strongest verification is to use the result as a consumer would:

1. Confirm the output contains `UnrealHog.uplugin` and a `Binaries` directory for the intended platform or platforms.
2. Copy the packaged output into the `Plugins` directory of a clean project using the same Unreal Engine build.
3. Enable UnrealHog and restart the editor.
4. Build and package the test project for each platform you intend to distribute.

Do not publish a package if the test project asks to rebuild UnrealHog. That usually means the packaged binaries do not match the consuming engine version, engine distribution, host platform, or target platform.

## Common Packaging Issues

### Unreal Engine cannot find a compiler

Install the C++ toolchain required by your Unreal Engine version. On Windows, this includes a supported Visual Studio toolchain and Windows SDK, not only the Visual Studio editor.

### A requested target platform is unavailable

Install the platform support and SDK for that target, then confirm the engine recognizes it. Adding a name to `-TargetPlatforms` does not add the required build tools.

### The packaged plugin asks to be rebuilt

Make sure the package was created for the consumer's exact Unreal Engine version, engine distribution, and host platform. Use the source distribution and rebuild when you cannot provide matching precompiled binaries.

### Packaging succeeds for one platform but fails for another

Each target is compiled independently and may have different SDK, dependency, or compiler requirements. Read the first platform-specific error in the UAT log and verify that the target is supported from the current build host.

{% include page-footer.html title="Reference" url="/reference.html" %}
