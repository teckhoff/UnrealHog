{% assign enum = include.enum %}
{% unless enum %}
  {% assign enum = site.data.api_enums | where: "id", include.id | first %}
{% endunless %}

{% if enum %}
<!--
UnrealHog API source context
Kind: enum
API ID: {{ enum.id | escape }}
Declared in: {{ enum.source | escape }}
-->
<blockquote class="enum api-enum">
  <p><code>{{ enum.name | escape }}</code>{% if enum.availability %} - {{ enum.availability | escape }}{% endif %}</p>
  <p>{{ enum.summary }}</p>

  <p><strong>Values</strong></p>
  {% if enum.values and enum.values.size > 0 %}
    <table>
      <thead>
        <tr>
          <th>Name</th>
          <th>Display Name</th>
          <th>Description</th>
        </tr>
      </thead>
      <tbody>
        {% for value in enum.values %}
          <tr>
            <td><code>{{ value.name | escape }}</code></td>
            <td>{% if value.display_name %}{{ value.display_name | escape }}{% else %}-{% endif %}</td>
            <td>{{ value.description }}</td>
          </tr>
        {% endfor %}
      </tbody>
    </table>
  {% else %}
    <p>None.</p>
  {% endif %}

  {% if enum.source %}
    <p><strong>Source</strong>: <code>{{ enum.source | escape }}</code></p>
  {% endif %}
</blockquote>
{% else %}
<blockquote class="warning">
  <p>API enum <code>{{ include.id | escape }}</code> was not found in <code>docs/_data/api_enums.yml</code>.</p>
</blockquote>
{% endif %}
