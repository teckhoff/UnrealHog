{% assign method = include.method %}
{% unless method %}
  {% assign method = site.data.api_methods | where: "id", include.id | first %}
{% endunless %}

{% if method %}
<!--
UnrealHog API source context
Kind: method
API ID: {{ method.id | escape }}
Declared in: {{ method.source | escape }}
Implemented in: {{ method.implementation | escape }}
-->
<blockquote class="method api-method">
  <p><code>{{ method.name | escape }}</code>{% if method.availability %} - {{ method.availability | escape }}{% endif %}</p>
  <p>{{ method.summary }}</p>
  <pre><code class="language-cpp">{{ method.signature | strip | escape }}</code></pre>

  <p><strong>Parameters</strong></p>
  {% if method.parameters and method.parameters.size > 0 %}
    <table>
      <thead>
        <tr>
          <th>Name</th>
          <th>Type</th>
          <th>Required</th>
          <th>Description</th>
        </tr>
      </thead>
      <tbody>
        {% for parameter in method.parameters %}
          <tr>
            <td><code>{{ parameter.name | escape }}</code></td>
            <td><code>{{ parameter.type | escape }}</code></td>
            <td>{% if parameter.required_label %}{{ parameter.required_label | escape }}{% elsif parameter.required %}Yes{% else %}No{% endif %}</td>
            <td>{{ parameter.description }}</td>
          </tr>
        {% endfor %}
      </tbody>
    </table>
  {% else %}
    <p>None.</p>
  {% endif %}

  <p><strong>Outputs</strong></p>
  {% if method.outputs and method.outputs.size > 0 %}
    <table>
      <thead>
        <tr>
          <th>Type</th>
          <th>Description</th>
        </tr>
      </thead>
      <tbody>
        {% for output in method.outputs %}
          <tr>
            <td><code>{{ output.type | escape }}</code></td>
            <td>{{ output.description }}</td>
          </tr>
        {% endfor %}
      </tbody>
    </table>
  {% else %}
    <p>None.</p>
  {% endif %}

  {% if method.notes %}
    <p><strong>Notes</strong></p>
    <ul>
      {% for note in method.notes %}
        <li>{{ note }}</li>
      {% endfor %}
    </ul>
  {% endif %}
</blockquote>
{% else %}
<blockquote class="warning">
  <p>API method <code>{{ include.id | escape }}</code> was not found in <code>docs/_data/api_methods.yml</code>.</p>
</blockquote>
{% endif %}
