# frozen_string_literal: true

require_relative "../scripts/check-docs"

Jekyll::Hooks.register :site, :after_init do |site|
  failures = UnrealHogDocs::Check.new(site.source).run
  next if failures.empty?

  message = "UnrealHog documentation validation failed:\n" +
            failures.map { |failure| "- #{failure}" }.join("\n")
  raise Jekyll::Errors::FatalException, message
end

Jekyll::Hooks.register :site, :post_write do |site|
  failures = UnrealHogDocs::Check.new(site.source).check_rendered_site(site.dest)
  next if failures.empty?

  message = "UnrealHog rendered-site validation failed:\n" +
            failures.map { |failure| "- #{failure}" }.join("\n")
  raise Jekyll::Errors::FatalException, message
end
