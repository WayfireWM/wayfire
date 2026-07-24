# Writing Plugins For `wayfire-plugin`

A plugin repository may include `wayfire-plugin.json` at its root to customize management by `wayfire-plugin`. Without a manifest, the manager uses the repository directory as the plugin name, uses the default Meson commands, and reminds the user to enable the plugin in `core/plugins`.

## Minimal Manifest

```json
{
  "name": "my-plugin",
  "setup": "Add my-plugin to [core] plugins, then configure the [my-plugin] section."
}
```

`name` is the manager name used by `wayfire-plugin list`, `update`, `rebuild`, and `remove`. `setup` is printed after install and should tell users which plugin names to add to `core/plugins` and any important first configuration steps.

## Version-Specific Refs

Use `refs` when different Wayfire releases need different branches, tags, or commits:

```json
{
  "name": "my-plugin",
  "refs": [
    {
      "wayfire": "0.11",
      "ref": "wayfire-0.11"
    },
    {
      "wayfire": "git",
      "ref": "main"
    }
  ],
  "setup": "Add my-plugin to [core] plugins."
}
```

Matching is simple:

- Exact version matches are accepted.
- A release prefix like `0.11` matches versions such as `0.11.0`.
- `git` matches development-style versions.
- `default` or `latest` can be used as a fallback selector.

## Build Commands

If `build_commands` is absent, the default is Meson:

```sh
meson setup %builddir% %sourcedir% --prefix %prefix%
meson compile -C %builddir%
meson install -C %builddir%
```

You can override any phase:

```json
{
  "name": "my-plugin",
  "build_commands": {
    "setup": ["meson", "setup", "%builddir%", "%sourcedir%", "--prefix", "%prefix%"],
    "build": ["meson", "compile", "-C", "%builddir%"],
    "install": ["meson", "install", "-C", "%builddir%"]
  },
  "setup": "Add my-plugin to [core] plugins."
}
```

Commands may be argv arrays or shell strings. Prefer argv arrays unless shell syntax is needed.

Available placeholders:

- `%prefix%`: the final managed install prefix.
- `%builddir%`: the build directory managed by `wayfire-plugin`.
- `%sourcedir%`: the source checkout or local source directory.

## Install Layout

Install plugin files relative to the configured prefix:

```text
%prefix%/lib/wayfire/libmy-plugin.so
%prefix%/share/wayfire/metadata/my-plugin.xml
```

For Meson, prefer install directories derived from `get_option('prefix')`, not from Wayfire's pkg-config variables:

```meson
wayfire = dependency('wayfire')

shared_module('my-plugin', 'my-plugin.cpp',
  dependencies: wayfire,
  install: true,
  install_dir: get_option('prefix') / get_option('libdir') / 'wayfire')

install_data('metadata/my-plugin.xml',
  install_dir: get_option('prefix') / 'share' / 'wayfire' / 'metadata')
```

Do not install external plugins to `wayfire.get_variable(pkgconfig: 'plugindir')` or metadata to `wayfire.get_variable(pkgconfig: 'metadatadir')`. Those variables describe the Wayfire installation, not the managed plugin prefix.

Additional resources may be installed under the same prefix, for example:

```text
%prefix%/share/my-plugin/...
%prefix%/bin/my-plugin-helper
```

## Plugin Requirements

The plugin `.so` must export the usual Wayfire plugin entry points. Using `DECLARE_WAYFIRE_PLUGIN` does this for normal plugins:

```cpp
DECLARE_WAYFIRE_PLUGIN(my_plugin_t);
```

After installation, `wayfire-plugin` scans the managed prefix for plugin `.so` files and XML metadata, validates the required symbols, records the build in its registry, and asks the running compositor to reload metadata and plugins when IPC is available.
