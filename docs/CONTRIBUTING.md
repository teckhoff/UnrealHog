# Documentation validation

The UnrealHog documentation build validates its API metadata and examples before Jekyll renders the site.

Run the checks directly from `docs/`:

```shell
bundle exec ruby scripts/check-docs.rb
```

Or run the normal site build, which invokes the same checks through `_plugins/docs_validation.rb`:

```shell
bundle exec jekyll build
```

The validator checks:

- unique API IDs, valid embedded API IDs, and one logical-domain page per API category;
- documented signatures and enum values against their declared `UnrealHog/Source/UnrealHog/Public` headers;
- implementation file metadata and the presence of each documented method implementation;
- coverage of public Blueprint-callable methods and public `EPostHog*` enums;
- duplicate Markdown heading anchors;
- balanced C++ fences and delimiters, UnrealHog header includes, and documented subsystem calls in C++ examples;
- Blueprint clipboard file existence, object-record balance, page usage, and Copy Blueprints controls;
- referenced local assets;
- rendered local links, anchors, assets, and duplicate IDs during a Jekyll build.

Update `_data/api_methods.yml` or `_data/api_enums.yml` in the same change as a public API change. Keep source paths repository-relative so both readers and the validator can locate the implementation context.
