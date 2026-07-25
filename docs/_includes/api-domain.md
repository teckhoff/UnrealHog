{% assign domain_methods = site.data.api_methods | where: "category", include.category %}
{% assign domain_enums = site.data.api_enums | where: "category", include.category %}

{% if domain_methods.size > 0 %}
## Methods

{% for method in domain_methods %}
### `{{ method.name }}` {#{{ method.id }}}

{% include api-method.md method=method %}
{% endfor %}
{% endif %}

{% if domain_enums.size > 0 %}
## Enums

{% for enum in domain_enums %}
### `{{ enum.name }}` {#{{ enum.id }}}

{% include api-enum.md enum=enum %}
{% endfor %}
{% endif %}
