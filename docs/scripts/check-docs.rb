#!/usr/bin/env ruby
# frozen_string_literal: true

require "pathname"
require "uri"
require "yaml"

module UnrealHogDocs
  class Check
    PLUGIN_INCLUDE_PREFIXES = %w[Events/ Logging/ Storage/ Subsystems/ Utilities/].freeze

    attr_reader :errors

    def initialize(docs_root = Pathname(__dir__).parent)
      @docs_root = Pathname(docs_root).expand_path
      @repository_root = @docs_root.parent
      @errors = []
      @methods = load_yaml("_data/api_methods.yml")
      @enums = load_yaml("_data/api_enums.yml")
    end

    def run
      check_api_ids
      check_api_domains
      check_api_includes
      check_api_sources
      check_public_blueprint_methods
      check_public_enums
      check_markdown_headings
      check_fenced_snippets
      check_blueprint_examples
      check_local_assets
      errors
    end

    def check_rendered_site(site_root, baseurl = nil)
      site_root = Pathname(site_root).expand_path
      baseurl = normalize_baseurl(baseurl)
      rendered_errors = []
      id_cache = {}

      site_root.glob("**/*.html").each do |page|
        html = page.read
        main = html[/<main\b.*?<\/main>/m] || html
        ids = main.scan(/\bid=(["'])(.*?)\1/).map { |_quote, id| id }
        ids.group_by(&:itself).each do |id, matches|
          rendered_errors << "#{site_relative(page, site_root)}: duplicate rendered ID #{id.inspect}" if matches.length > 1
        end

        html.scan(/\b(?:href|src)=(["'])(.*?)\1/).each do |_quote, raw_target|
          next if raw_target.empty? || raw_target.start_with?("#")
          next if raw_target.match?(/\A(?:https?:|mailto:|tel:|javascript:|data:|\/\/)/i)

          uri = URI.parse(raw_target)
          target = rendered_target(page, site_root, uri.path, baseurl)
          unless target&.file?
            rendered_errors << "#{site_relative(page, site_root)}: rendered link target does not exist: #{raw_target}"
            next
          end
          next if uri.fragment.to_s.empty? || target.extname != ".html"

          target_ids = id_cache[target] ||= target.read.scan(/\bid=(["'])(.*?)\1/).map { |_q, id| id }
          fragment = URI.decode_www_form_component(uri.fragment)
          unless target_ids.include?(fragment)
            rendered_errors << "#{site_relative(page, site_root)}: rendered anchor does not exist: #{raw_target}"
          end
        rescue URI::InvalidURIError
          rendered_errors << "#{site_relative(page, site_root)}: invalid rendered URL #{raw_target.inspect}"
        end
      end

      rendered_errors
    end

    private

    def load_yaml(relative_path)
      path = @docs_root.join(relative_path)
      value = YAML.safe_load_file(path, permitted_classes: [], aliases: false)
      return value if value.is_a?(Array)

      errors << "#{relative_path}: expected a top-level array"
      []
    rescue StandardError => e
      errors << "#{relative_path}: #{e.message}"
      []
    end

    def markdown_files
      @markdown_files ||= @docs_root.glob("**/*.md").reject do |path|
        path.each_filename.any? { |part| part.start_with?("_site") || part == "vendor" }
      end
    end

    def check_api_ids
      entries = @methods.map { |entry| ["method", entry] } +
                @enums.map { |entry| ["enum", entry] }
      grouped = entries.group_by { |_kind, entry| entry["id"] }

      grouped.each do |id, duplicates|
        errors << "API ID #{id.inspect} is missing or duplicated across API data" if id.to_s.empty? || duplicates.length > 1
      end
    end

    def check_api_domains
      pages_by_category = Hash.new { |hash, key| hash[key] = [] }
      markdown_files.each do |path|
        path.read.scan(/\{%\s*include\s+api-domain\.md\s+category=(["'])(.*?)\1\s*%\}/) do |_quote, category|
          pages_by_category[category] << relative(path)
        end
      end

      (@methods + @enums).map { |entry| entry["category"] }.uniq.each do |category|
        pages = pages_by_category[category]
        if pages.empty?
          errors << "API category #{category.inspect} has no api-domain page"
        elsif pages.length > 1
          errors << "API category #{category.inspect} is rendered by multiple pages: #{pages.join(', ')}"
        end
      end
    end

    def check_api_includes
      known = {
        "api-method.md" => @methods.map { |entry| entry["id"] },
        "api-enum.md" => @enums.map { |entry| entry["id"] }
      }

      markdown_files.each do |path|
        text = path.read
        known.each do |include_name, ids|
          text.scan(/\{%\s*include\s+#{Regexp.escape(include_name)}\s+id=(["'])(.*?)\1.*?%\}/) do |_quote, id|
            errors << "#{relative(path)}: unknown #{include_name} ID #{id.inspect}" unless ids.include?(id)
          end
        end
      end
    end

    def check_api_sources
      @methods.each do |method|
        source = source_path(method["source"], "method #{method['id']} source")
        implementation = source_path(method["implementation"], "method #{method['id']} implementation")
        next unless source

        source_text = compact_cpp(source.read)
        declarations(method["signature"]).each do |signature|
          next if source_text.include?(compact_cpp(signature))

          errors << "_data/api_methods.yml: #{method['id']} signature is stale or absent from #{method['source']}: #{one_line(signature)}"
        end

        if implementation && implementation.read !~ /\b#{Regexp.escape(method["name"].to_s)}\s*\(/
          errors << "_data/api_methods.yml: #{method['id']} implementation does not contain #{method['name']}( in #{method['implementation']}"
        end
      end

      @enums.each do |enum|
        source = source_path(enum["source"], "enum #{enum['id']} source")
        next unless source

        values = enum_values(source.read, enum["name"])
        if values.nil?
          errors << "_data/api_enums.yml: #{enum['id']} cannot find #{enum['name']} in #{enum['source']}"
          next
        end

        documented = Array(enum["values"]).map { |value| value["name"].to_s }
        next if values == documented

        errors << "_data/api_enums.yml: #{enum['name']} values differ from #{enum['source']} (source: #{values.join(', ')}; docs: #{documented.join(', ')})"
      end
    end

    def check_public_blueprint_methods
      public_root = @repository_root.join("UnrealHog/Source/UnrealHog/Public")
      documented = @methods.flat_map { |method| declarations(method["signature"]) }.map { |signature| compact_cpp(signature) }

      public_root.glob("**/*.h").each do |path|
        lines = path.readlines
        lines.each_with_index do |line, index|
          next unless line.include?("UFUNCTION") && (line.include?("BlueprintCallable") || line.include?("BlueprintPure"))

          declaration = +""
          lines[(index + 1)..].each do |following|
            declaration << following
            break if following.include?(";")
          end
          next if documented.include?(compact_cpp(declaration))

          errors << "#{repository_relative(path)}: public Blueprint declaration is missing or stale in API data: #{one_line(declaration)}"
        end
      end
    end

    def check_public_enums
      public_root = @repository_root.join("UnrealHog/Source/UnrealHog/Public")
      documented_names = @enums.map { |enum| enum["name"] }

      public_root.glob("**/*.h").each do |path|
        strip_cpp_comments(path.read).scan(/\benum\s+class\s+(EPostHog\w+)\s*(?::[^{]+)?\s*\{/).flatten.each do |name|
          next if documented_names.include?(name)

          errors << "#{repository_relative(path)}: public enum #{name} is missing from _data/api_enums.yml"
        end
      end
    end

    def check_markdown_headings
      markdown_files.each do |path|
        anchors = {}
        in_fence = false

        path.readlines.each_with_index do |line, index|
          if line.match?(/^\s*```/)
            in_fence = !in_fence
            next
          end
          next if in_fence

          match = line.match(/^\s{0,3}(?<marks>\#{1,6})\s+(?<title>.+?)\s*\#*\s*$/)
          next unless match
          next if match[:title].include?("{{") || match[:title].include?("{%")

          explicit = match[:title].match(/\{#([^}]+)\}\s*$/)&.captures&.first
          anchor = explicit || heading_slug(match[:title])
          next if anchor.empty?

          if anchors.key?(anchor)
            errors << "#{relative(path)}: duplicate heading anchor #{anchor.inspect} on lines #{anchors[anchor]} and #{index + 1}"
          else
            anchors[anchor] = index + 1
          end
        end
      end
    end

    def check_fenced_snippets
      markdown_files.each do |path|
        text = path.read
        fences = text.scan(/^```([^\n]*)\n(.*?)^```\s*$/m)
        opening_count = text.scan(/^```/).length
        if opening_count.odd?
          errors << "#{relative(path)}: unbalanced fenced code block"
          next
        end

        fences.each_with_index do |(language, body), index|
          next unless %w[cpp c++].include?(language.strip.downcase)

          label = "#{relative(path)} C++ block #{index + 1}"
          check_cpp_balance(body, label)
          check_cpp_includes(body, label)
          check_snippet_api_calls(body, label)
        end
      end
    end

    def check_cpp_balance(body, label)
      stripped = strip_cpp_strings_and_comments(body)
      pairs = { ")" => "(", "]" => "[", "}" => "{" }
      stack = []
      stripped.each_char do |character|
        if pairs.value?(character)
          stack << character
        elsif pairs.key?(character)
          if stack.pop != pairs[character]
            errors << "#{label}: mismatched #{character}"
            return
          end
        end
      end
      errors << "#{label}: unclosed #{stack.last}" unless stack.empty?
    end

    def check_cpp_includes(body, label)
      body.scan(/^\s*#include\s+"([^"]+)"/).flatten.each do |include_path|
        next unless PLUGIN_INCLUDE_PREFIXES.any? { |prefix| include_path.start_with?(prefix) }

        header = @repository_root.join("UnrealHog/Source/UnrealHog/Public", include_path)
        errors << "#{label}: plugin include does not exist: #{include_path}" unless header.file?
      end
    end

    def check_snippet_api_calls(body, label)
      documented_names = @methods.map { |method| method["name"] }.uniq
      body.scan(/\bPostHog->([A-Z]\w*)\s*\(/).flatten.each do |name|
        errors << "#{label}: PostHog call #{name} is absent from API data" unless documented_names.include?(name)
      end
    end

    def check_blueprint_examples
      referenced = []

      markdown_files.each do |path|
        text = path.read
        text.scan(/\{%\s*include\s+(blueprints\/[^\s%]+\.txt)\s*%\}/).flatten.each do |include_path|
          referenced << include_path
          full_path = @docs_root.join("_includes", include_path)
          unless full_path.file?
            errors << "#{relative(path)}: missing Blueprint clipboard include #{include_path}"
            next
          end

          content = full_path.read
          errors << "#{include_path}: Blueprint clipboard text is empty" if content.strip.empty?
          if content.scan(/^\s*Begin Object\b/).length != content.scan(/^\s*End Object\b/).length
            errors << "#{include_path}: unbalanced Begin Object/End Object records"
          end
        end

        text.scan(/\{%\s*capture\s+(?<name>\w+)\s*%\}(?<body>.*?)\{%\s*endcapture\s*%\}/m).each do |name, capture|
          next unless capture.include?("include blueprints/")
          next if text.match?(/\{%\s*include\s+blueprint-copy\.html\b[^%]*\btext=#{Regexp.escape(name)}(?:\s|%)/)

          errors << "#{relative(path)}: Blueprint clipboard capture #{name} has no Copy Blueprints control"
        end
      end

      @docs_root.glob("_includes/blueprints/*.txt").each do |path|
        include_path = "blueprints/#{path.basename}"
        errors << "#{relative(path)}: Blueprint clipboard include is not referenced by a page" unless referenced.include?(include_path)
      end
    end

    def check_local_assets
      markdown_files.each do |path|
        path.read.scan(%r{(?:src=|!\[[^\]]*\]\()["']?\{\{\s*['"](/assets/[^'"]+)['"]}).flatten.each do |asset|
          errors << "#{relative(path)}: missing local asset #{asset}" unless @docs_root.join(asset.delete_prefix("/")).file?
        end
      end
    end

    def declarations(signature)
      signature.to_s.split(";").map(&:strip).reject(&:empty?).map { |part| "#{part};" }
    end

    def source_path(value, label)
      if value.to_s.empty?
        errors << "_data API entry is missing #{label}"
        return nil
      end

      path = @repository_root.join(value)
      return path if path.file?

      errors << "#{label} does not exist: #{value}"
      nil
    end

    def enum_values(text, name)
      clean = strip_cpp_comments(text)
      match = clean.match(/\benum\s+class\s+#{Regexp.escape(name)}\s*(?::[^{]+)?\s*\{(?<body>.*?)\}\s*;/m)
      return nil unless match

      match[:body].split(",").filter_map do |entry|
        value = entry.gsub(/\bUMETA\s*\([^)]*\)/m, "").split("=").first.to_s.strip
        value[/\A[A-Za-z_]\w*/]
      end
    end

    def compact_cpp(text)
      strip_cpp_comments(text.to_s).gsub(/\s+/, "")
    end

    def strip_cpp_comments(text)
      text.gsub(%r{/\*.*?\*/}m, "").gsub(%r{//[^\n]*}, "")
    end

    def strip_cpp_strings_and_comments(text)
      text
        .gsub(%r{/\*.*?\*/}m, "")
        .gsub(%r{//[^\n]*}, "")
        .gsub(/"(?:\\.|[^"\\])*"/m, '""')
        .gsub(/'(?:\\.|[^'\\])*'/m, "''")
    end

    def heading_slug(title)
      title
        .sub(/\s*\{#.*?\}\s*$/, "")
        .gsub(/`([^`]*)`/, '\1')
        .gsub(/\[([^\]]+)\]\([^)]+\)/, '\1')
        .downcase
        .gsub(/[^a-z0-9\s_-]/, "")
        .strip
        .gsub(/[\s_]+/, "-")
    end

    def one_line(text)
      text.to_s.gsub(/\s+/, " ").strip
    end

    def relative(path)
      Pathname(path).relative_path_from(@docs_root).to_s
    end

    def repository_relative(path)
      Pathname(path).relative_path_from(@repository_root).to_s
    end

    def rendered_target(page, site_root, raw_path, baseurl)
      decoded = URI.decode_www_form_component(raw_path.to_s)
      lookup_path = strip_baseurl(decoded, baseurl)
      target = if lookup_path.empty?
                 page
               elsif lookup_path.start_with?("/")
                 site_root.join(lookup_path.delete_prefix("/"))
               else
                 page.dirname.join(lookup_path)
               end
      target = target.join("index.html") if target.directory? || lookup_path.end_with?("/")
      target.cleanpath
    end

    def normalize_baseurl(value)
      baseurl = value.to_s.strip
      return "" if baseurl.empty? || baseurl == "/"

      baseurl = "/#{baseurl}" unless baseurl.start_with?("/")
      baseurl.chomp("/")
    end

    def strip_baseurl(path, baseurl)
      return path if baseurl.empty? || !path.start_with?("/")
      return "/" if path == baseurl
      return path.delete_prefix(baseurl) if path.start_with?("#{baseurl}/")

      path
    end

    def site_relative(path, site_root)
      Pathname(path).relative_path_from(site_root).to_s
    end
  end
end

if $PROGRAM_NAME == __FILE__
  check = UnrealHogDocs::Check.new
  failures = check.run
  if failures.empty?
    puts "Documentation checks passed."
    exit 0
  end

  warn "Documentation checks failed:"
  failures.each { |failure| warn "- #{failure}" }
  exit 1
end
