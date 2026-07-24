# Managing External Plugins

`wayfire-plugin` installs and updates external Wayfire plugins in a user-writable managed prefix:

```text
$XDG_DATA_HOME/wayfire/plugin-manager/install
```

If `XDG_DATA_HOME` is unset, this is usually:

```text
~/.local/share/wayfire/plugin-manager/install
```

Wayfire searches the managed prefix for plugins and metadata at startup. If the running compositor has the IPC plugins enabled, `wayfire-plugin` also asks it to reload plugin metadata and retry loading plugins after successful installs and updates.

## Requirements

The `wayfire-plugin.json` manifest is optional. Without one, `wayfire-plugin` uses the repository directory as the plugin name, builds it with Meson, and prints the default reminder to enable the installed plugin in `core/plugins`.

During plugin builds, `wayfire-plugin` provides a temporary `wayfire.pc` that points `plugindir` and `metadatadir` at the XDG user directories. The original dependency flags, version, and ABI remain unchanged. Installations that hardcode other destinations are still tracked through Meson's install log when possible.

## Commands

Install a plugin from a Git URL:

```sh
wayfire-plugin install https://github.com/example/wayfire-plugin-example
```

Install a plugin from a local checkout:

```sh
wayfire-plugin install ~/src/wayfire-plugin-example
```

Update all managed plugins:

```sh
wayfire-plugin update
```

Update one plugin:

```sh
wayfire-plugin update plugin-name
```

Rebuild all managed plugins for the current Wayfire target:

```sh
wayfire-plugin rebuild
```

List managed plugins:

```sh
wayfire-plugin list
```

Show paths and the Wayfire version detected through pkg-config:

```sh
wayfire-plugin paths
```

Uninstall a plugin and remove its manager state:

```sh
wayfire-plugin remove plugin-name
```

`remove` runs the Meson uninstall target before deleting the retained build and manager state. If tracked files cannot be removed, the state is retained so the command can be retried with appropriate permissions. If no Meson build state is available, installed files are left untouched.

## Useful Options

Print the tool's phases, debug messages, and commands:

```sh
wayfire-plugin --verbose install https://github.com/example/plugin
```

Start URL installs from a fresh clone:

```sh
wayfire-plugin --clean install https://github.com/example/plugin
```

`--clean` removes the managed checkout/build state for URL installs. It does not delete local source directories.

## Enabling Plugins

`wayfire-plugin` installs plugin files and metadata, but it does not edit your Wayfire config. To enable a plugin, add its name to the `core/plugins` option in your config file.

Example:

```ini
[core]
plugins = alpha animate command my-plugin
```

After editing the config, restart Wayfire or use your usual config reload workflow.

## Selecting A Wayfire Installation

The tool builds against the `wayfire` found by pkg-config. If you have multiple Wayfire installations, select the desired one with `PKG_CONFIG_PATH` before running the tool.

Example:

```sh
PKG_CONFIG_PATH=/opt/wayfire/lib/pkgconfig wayfire-plugin rebuild
```

If IPC is available, the tool compares the running compositor against the pkg-config target and warns when they differ.
