#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from collections.abc import Sequence
from typing import Any


MANIFEST = "wayfire-plugin.json"
JsonDict = dict[str, Any]
VERBOSE = False
CLEAN = False


def use_color() -> bool:
    return sys.stderr.isatty() and "NO_COLOR" not in os.environ


def style(text: str, code: str) -> str:
    if not use_color():
        return text
    return f"\033[{code}m{text}\033[0m"


def tool_prefix(level: str = "info") -> str:
    label = "wayfire-plugin"
    if level == "warning":
        label = style(label, "1;33")
    elif level == "error":
        label = style(label, "1;31")
    elif level == "debug":
        label = style(label, "90")
    else:
        label = style(label, "1;32")

    return f"[{label}]"


def log_tool(message: str, level: str = "info") -> None:
    print(f"{tool_prefix(level)} {message}", file=sys.stderr)


def log_phase(message: str) -> None:
    log_tool(message)


def log_verbose(message: str) -> None:
    if VERBOSE:
        log_tool(message, level="debug")


def format_command(cmd: Sequence[str] | str) -> str:
    if isinstance(cmd, str):
        return cmd
    return " ".join(str(part) for part in cmd)


def run(
    cmd: Sequence[str] | str,
    cwd: str | Path | None = None,
    check: bool = True,
    capture: bool = False,
    env: dict[str, str] | None = None,
) -> subprocess.CompletedProcess[str]:
    log_verbose(f"Running: {format_command(cmd)}")
    if cwd:
        log_verbose(f"  cwd: {cwd}")

    kwargs: dict[str, Any] = {
        "cwd": cwd,
        "check": check,
        "env": env,
        "text": True,
    }
    if capture:
        kwargs["stdout"] = subprocess.PIPE
        kwargs["stderr"] = subprocess.PIPE
    if isinstance(cmd, str):
        kwargs["shell"] = True
    return subprocess.run(cmd, **kwargs)


def xdg_data_home() -> Path:
    if os.environ.get("XDG_DATA_HOME"):
        return Path(os.environ["XDG_DATA_HOME"])
    return Path.home() / ".local" / "share"


def manager_root() -> Path:
    return xdg_data_home() / "wayfire" / "plugin-manager"


def user_plugin_dir() -> Path:
    return xdg_data_home() / "wayfire" / "plugins"


def user_metadata_dir() -> Path:
    return xdg_data_home() / "wayfire" / "metadata"


def managed_install_prefix() -> Path:
    return manager_root() / "install"


def plugin_build_env(pkg_config_dir: Path) -> dict[str, str]:
    pcfiledir = pkg_config_variable("pcfiledir", "")
    wayfire_pc = Path(pcfiledir) / "wayfire.pc"
    if not pcfiledir or not wayfire_pc.exists():
        raise SystemExit("Could not locate the active wayfire.pc file")

    overrides = {
        "plugindir": str(user_plugin_dir()),
        "metadatadir": str(user_metadata_dir()),
    }
    lines: list[str] = []
    replaced: set[str] = set()
    for line in wayfire_pc.read_text().splitlines():
        name, separator, _value = line.partition("=")
        if separator and name.strip() in overrides:
            name = name.strip()
            lines.append(f"{name}={overrides[name]}")
            replaced.add(name)
        else:
            lines.append(line)

    for name, value in overrides.items():
        if name not in replaced:
            lines.append(f"{name}={value}")

    pkg_config_dir.mkdir(parents=True, exist_ok=True)
    (pkg_config_dir / "wayfire.pc").write_text("\n".join(lines) + "\n")

    env = os.environ.copy()
    existing_path = env.get("PKG_CONFIG_PATH", "")
    env["PKG_CONFIG_PATH"] = str(pkg_config_dir) + (os.pathsep + existing_path if existing_path else "")
    return env


def pkg_config_variable(name: str, default: str = "") -> str:
    result = run(["pkg-config", "--variable=" + name, "wayfire"], check=False, capture=True)
    if result.returncode != 0:
        return default
    return result.stdout.strip() or default


def pkg_config_modversion() -> str:
    result = run(["pkg-config", "--modversion", "wayfire"], check=False, capture=True)
    if result.returncode != 0:
        return "unknown"
    return result.stdout.strip() or "unknown"


def parse_abi_from_header(header: Path) -> str:
    if not header.exists():
        return "unknown"
    match = re.search(r"WAYFIRE_API_ABI_VERSION_MACRO\s+([0-9']+)", header.read_text())
    if not match:
        return "unknown"
    return match.group(1).replace("'", "")


def pkg_config_pluginabi() -> str:
    abi = pkg_config_variable("pluginabi", "")
    if abi:
        return abi

    srcdir = pkg_config_variable("srcdir", "")
    if srcdir:
        return parse_abi_from_header(Path(srcdir) / "src" / "api" / "wayfire" / "plugin.hpp")

    includedir = pkg_config_variable("includedir", "")
    if includedir:
        return parse_abi_from_header(Path(includedir) / "wayfire" / "plugin.hpp")

    return "unknown"


def build_target() -> dict[str, str]:
    return {
        "version": pkg_config_modversion(),
        "abi": pkg_config_pluginabi(),
        "plugindir": pkg_config_variable("plugindir", ""),
        "metadatadir": pkg_config_variable("metadatadir", ""),
    }


def load_json(path: Path, default: JsonDict) -> JsonDict:
    if not path.exists():
        return default
    with path.open() as stream:
        return json.load(stream)


def save_json(path: Path, data: JsonDict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    with tmp.open("w") as stream:
        json.dump(data, stream, indent=2, sort_keys=True)
        stream.write("\n")
    tmp.replace(path)


def registry_path() -> Path:
    return manager_root() / "registry.json"


def load_registry() -> JsonDict:
    return load_json(registry_path(), {"plugins": {}})


def save_registry(registry: JsonDict) -> None:
    save_json(registry_path(), registry)


def plugin_state_dir(name: str) -> Path:
    return manager_root() / "plugins" / name


def infer_name(source: str | Path) -> str:
    return Path(source).stem.removesuffix(".git")


def is_url(source: str) -> bool:
    return "://" in source or source.startswith("git@")


def ensure_source(source: str, name: str | None = None) -> tuple[str, Path, str]:
    if is_url(source):
        real_name = name or infer_name(source)
        dest = plugin_state_dir(real_name) / "source"
        if CLEAN and plugin_state_dir(real_name).exists():
            log_phase(f"Removing existing managed checkout for {real_name}")
            shutil.rmtree(plugin_state_dir(real_name))

        if dest.exists():
            log_verbose(f"Fetching existing source for {real_name}: {dest}")
            run(["git", "-C", str(dest), "fetch", "--all", "--tags"])
        else:
            log_verbose(f"Cloning source for {real_name}: {source}")
            dest.parent.mkdir(parents=True, exist_ok=True)
            run(["git", "clone", source, str(dest)])
        return real_name, dest, source

    path = Path(source).resolve()
    if not path.exists():
        raise SystemExit(f"Source does not exist: {source}")
    if CLEAN:
        log_verbose("--clean does not remove local source directories")

    log_verbose(f"Using local source: {path}")
    return name or infer_name(path.name), path, str(path)


def git_commit(source_dir: Path) -> str:
    result = run(["git", "-C", str(source_dir), "rev-parse", "HEAD"], check=False, capture=True)
    if result.returncode != 0:
        return ""
    return result.stdout.strip()


def load_manifest(source_dir: Path) -> JsonDict:
    manifest = source_dir / MANIFEST
    if not manifest.exists():
        return {}

    return load_json(manifest, {})


def version_matches(selector: str, target_version: str) -> bool:
    if selector == target_version:
        return True
    if selector == "git":
        return target_version in ("unknown", "git") or "-" in target_version
    return target_version.startswith(selector + ".")


def select_ref(manifest: JsonDict, target_version: str) -> str | None:
    refs = manifest.get("refs", [])
    fallback = None
    for entry in refs:
        ref = entry.get("ref")
        selector = str(entry.get("wayfire", ""))
        if not ref:
            continue
        if version_matches(selector, target_version):
            return ref
        if selector in ("default", "latest"):
            fallback = ref
    return fallback


def checkout_ref(source_dir: Path, ref: str | None) -> None:
    if not ref:
        return
    log_verbose(f"Checking out manifest-selected ref: {ref}")
    run(["git", "-C", str(source_dir), "checkout", ref])


def scan_install_prefix(prefix: Path) -> tuple[list[Path], list[Path]]:
    log_verbose(f"Scanning install prefix: {prefix}")
    plugins: list[Path] = []
    metadata: list[Path] = []
    for path in prefix.rglob("*.so"):
        if path.name.startswith("lib") and path.parent.name == "wayfire":
            plugins.append(path)
    for path in prefix.rglob("*.xml"):
        if path.parent.name == "metadata" and path.parent.parent.name == "wayfire":
            metadata.append(path)
    return sorted(plugins), sorted(metadata)


def read_meson_install_log(build_dir: Path) -> list[Path]:
    install_log = build_dir / "meson-logs" / "install-log.txt"
    if not install_log.exists():
        return []

    paths: list[Path] = []
    for line in install_log.read_text().splitlines():
        if line and not line.startswith("#"):
            paths.append(Path(line))
    return paths


def classify_installed_files(paths: Sequence[Path]) -> tuple[list[Path], list[Path]]:
    plugins = [
        path for path in paths
        if path.suffix == ".so" and path.name.startswith("lib") and path.parent.name == "wayfire"
    ]
    metadata = [
        path for path in paths
        if path.suffix == ".xml" and path.parent.name == "metadata" and path.parent.parent.name == "wayfire"
    ]
    return sorted(plugins), sorted(metadata)


def registry_install_path(path: Path, install_prefix: Path) -> str:
    try:
        return str(path.relative_to(install_prefix))
    except ValueError:
        return str(path)


def apply_placeholders(command: Sequence[str] | str, replacements: dict[str, str]) -> Sequence[str] | str:
    def replace(value: str) -> str:
        for key, replacement in replacements.items():
            value = value.replace(key, replacement)
        return value

    if isinstance(command, str):
        return replace(command)

    return [replace(part) for part in command]


def run_manifest_command(
    command: Sequence[str] | str,
    source_dir: Path,
    replacements: dict[str, str],
    env: dict[str, str],
) -> None:
    run(apply_placeholders(command, replacements), cwd=source_dir, env=env)


def exported_symbols(path: Path) -> str | None:
    result = run(["nm", "-D", str(path)], check=False, capture=True)
    if result.returncode != 0:
        return None
    return result.stdout


def validate_plugins(paths: Sequence[Path]) -> None:
    log_phase("Validating installed plugin binaries")
    for path in paths:
        log_verbose(f"Validating plugin symbols: {path}")
        symbols = exported_symbols(path)
        if symbols is None:
            continue

        missing = [s for s in ("newInstance", "getWayfireVersion") if s not in symbols]
        if missing:
            raise SystemExit(f"{path}: missing required symbol(s): {', '.join(missing)}")


def configure_and_install(
    source_dir: Path,
    plugin_name: str,
    manifest: JsonDict,
) -> tuple[Path, list[Path], list[Path], list[Path]]:
    build_dir = plugin_state_dir(plugin_name) / "build"
    if build_dir.exists():
        log_verbose(f"Removing previous build dir: {build_dir}")
        shutil.rmtree(build_dir)

    install_prefix = managed_install_prefix()
    install_prefix.mkdir(parents=True, exist_ok=True)
    log_phase(f"Starting build for {plugin_name}")
    log_verbose(f"Installing into managed prefix: {install_prefix}")
    replacements = {
        "%prefix%": str(install_prefix),
        "%builddir%": str(build_dir),
        "%sourcedir%": str(source_dir),
    }

    commands = manifest.get("build_commands", {})
    if not isinstance(commands, dict):
        raise SystemExit("Manifest field build_commands must be an object")

    setup_command = commands.get("setup", commands.get("configure"))
    build_command = commands.get("build")
    install_command = commands.get("install")

    with tempfile.TemporaryDirectory(prefix="wayfire-plugin-pkgconfig-") as pkg_config_dir:
        env = plugin_build_env(Path(pkg_config_dir))
        log_verbose(f"Using temporary wayfire.pc from {pkg_config_dir}")

        log_phase("Configuring plugin")
        if setup_command:
            log_verbose("Using manifest setup command")
            run_manifest_command(setup_command, source_dir, replacements, env)
        else:
            log_verbose("Using default Meson setup command")
            run(
                ["meson", "setup", str(build_dir), str(source_dir), "--prefix", str(install_prefix)],
                env=env)

        log_phase("Building plugin")
        if build_command:
            log_verbose("Using manifest build command")
            run_manifest_command(build_command, source_dir, replacements, env)
        else:
            log_verbose("Using default Meson build command")
            run(["meson", "compile", "-C", str(build_dir)], env=env)

        log_phase("Installing plugin")
        if install_command:
            log_verbose("Using manifest install command")
            run_manifest_command(install_command, source_dir, replacements, env)
        else:
            log_verbose("Using default Meson install command")
            run(["meson", "install", "-C", str(build_dir)], env=env)

    installed = read_meson_install_log(build_dir)
    if installed:
        log_verbose(f"Found {len(installed)} paths in the Meson install log")
        plugins, metadata = classify_installed_files(installed)
    else:
        log_verbose("No Meson install log found; scanning the managed prefix")
        plugins, metadata = scan_install_prefix(install_prefix)
        installed = [*plugins, *metadata]
    if not plugins:
        raise SystemExit("Build succeeded, but no Wayfire plugin .so files were installed")
    validate_plugins(plugins)
    return install_prefix, installed, plugins, metadata


def running_wayfire_info() -> tuple[Any | None, JsonDict | str | None]:
    try:
        try:
            from pywayfire import WayfireSocket
        except ImportError:
            from wayfire import WayfireSocket
    except Exception:
        return None, "pywayfire is not available"

    try:
        sock = WayfireSocket()
        return sock, sock.send_json({"method": "wayfire/configuration", "data": {}})
    except Exception as err:
        return None, str(err)


def maybe_warn_running_mismatch(target: dict[str, str]) -> tuple[Any | None, JsonDict | None]:
    log_verbose("Checking running Wayfire version via IPC")
    sock, info = running_wayfire_info()
    if not isinstance(info, dict):
        return sock, None

    running_abi = str(info.get("api-version", "unknown"))
    running_version = str(info.get("wayfire-version", "unknown"))
    if running_abi != str(target["abi"]) or running_version != str(target["version"]):
        log_tool(
            "running Wayfire is "
            f"{running_version} ABI {running_abi}, but pkg-config resolves "
            f"{target['version']} ABI {target['abi']}. The plugin is built "
            "for the pkg-config target and may not load in the running compositor.",
            level="warning")
        return sock, None

    return sock, info


def maybe_reload_metadata(sock: Any | None, running_info: JsonDict | None, have_metadata: bool) -> None:
    if not have_metadata or not sock or not running_info:
        log_verbose("Skipping metadata reload")
        return
    try:
        log_verbose("Reloading Wayfire metadata via IPC")
        response = sock.send_json({"method": "wayfire/reload-config-metadata", "data": {}})
        if isinstance(response, dict) and response.get("error"):
            log_tool(f"Metadata reload failed: {response['error']}", level="warning")
        else:
            log_tool("Reloaded Wayfire plugin metadata in the running compositor.")
    except Exception as err:
        log_tool(f"Metadata reload failed: {err}", level="warning")


def maybe_reload_plugins(sock: Any | None, running_info: JsonDict | None) -> None:
    if not sock or not running_info:
        log_verbose("Skipping plugin reload")
        return
    try:
        log_verbose("Reloading Wayfire plugins via IPC")
        response = sock.send_json({"method": "wayfire/reload-plugins", "data": {}})
        if isinstance(response, dict) and response.get("error"):
            log_tool(f"Plugin reload failed: {response['error']}", level="warning")
        else:
            log_tool("Reloaded Wayfire plugins in the running compositor.")
    except Exception as err:
        log_tool(f"Plugin reload failed: {err}", level="warning")


def plugin_name_from_file(filename: str) -> str:
    return Path(filename).name.removeprefix("lib").removesuffix(".so")


def install_or_update(source: str, update: bool = False) -> None:
    log_phase("Resolving Wayfire target version")
    log_verbose(f"Resolving build target from pkg-config: {source}")
    target = build_target()
    log_phase(f"Using Wayfire {target['version']} ABI {target['abi']}")
    log_verbose(f"Build target: Wayfire {target['version']} ABI {target['abi']}")
    name, source_dir, source_ref = ensure_source(source)
    manifest = load_manifest(source_dir)
    name = manifest.get("name", name)
    log_phase(f"{'Updating' if update else 'Installing'} {name}")
    log_verbose(f"Plugin name: {name}")

    selected_ref = select_ref(manifest, target["version"])
    if selected_ref:
        checkout_ref(source_dir, selected_ref)
        manifest = load_manifest(source_dir)

    commit = git_commit(source_dir)
    log_verbose(f"Source commit: {commit or 'unknown'}")
    registry = load_registry()
    old_state = registry["plugins"].get(name, {})
    if update and old_state.get("installed_commit") == commit and old_state.get("built_for_abi") == target["abi"]:
        print(f"{name} is up to date.")
        return

    install_prefix, all_files, plugin_files, metadata_files = configure_and_install(source_dir, name, manifest)
    installed_files = {
        "files": sorted(registry_install_path(path, install_prefix) for path in all_files),
        "plugins": sorted(registry_install_path(path, install_prefix) for path in plugin_files),
        "metadata": sorted(registry_install_path(path, install_prefix) for path in metadata_files),
    }

    state = {
        "name": name,
        "source": source_ref,
        "selected_ref": selected_ref or "",
        "installed_commit": commit,
        "built_for_wayfire_version": target["version"],
        "built_for_abi": target["abi"],
        "installed_files": installed_files,
        "setup": manifest.get("setup", ""),
    }
    log_phase("Updating plugin manager registry")
    registry["plugins"][name] = state
    save_registry(registry)

    sock, running_info = maybe_warn_running_mismatch(target)
    maybe_reload_metadata(sock, running_info, bool(metadata_files))
    maybe_reload_plugins(sock, running_info)

    print(f"Installed {name}.")
    print("Plugins: " + ", ".join(plugin_name_from_file(p) for p in installed_files["plugins"]))
    if installed_files["metadata"]:
        print("Metadata: " + ", ".join(installed_files["metadata"]))
    if state["setup"]:
        print("\n" + state["setup"])
    else:
        print("\nTo enable it, add the plugin name to [core] plugins in your Wayfire config.")

    log_phase(f"Done {'updating' if update else 'installing'} {name}")


def remove_plugin(name: str) -> None:
    registry = load_registry()
    state = registry["plugins"].get(name)
    if not state:
        raise SystemExit(f"Plugin is not managed: {name}")

    build_dir = plugin_state_dir(name) / "build"
    meson_build = build_dir / "meson-private" / "coredata.dat"
    uninstalled = False
    if meson_build.exists():
        installed_paths = read_meson_install_log(build_dir)
        installed_files = [path for path in installed_paths if not path.is_dir() or path.is_symlink()]
        log_phase(f"Uninstalling {name}")
        with tempfile.TemporaryDirectory(prefix="wayfire-plugin-pkgconfig-") as pkg_config_dir:
            env = plugin_build_env(Path(pkg_config_dir))
            run(["meson", "compile", "-C", str(build_dir), "uninstall"], env=env)
        remaining = [path for path in installed_files if path.exists() or path.is_symlink()]
        if remaining:
            raise SystemExit("Meson uninstall left installed files: " + ", ".join(map(str, remaining)))
        uninstalled = bool(installed_paths)
    else:
        log_verbose(f"No Meson build state found for {name}; skipping uninstall")

    log_phase(f"Removing manager state for {name}")
    log_verbose(f"Removing manager state for plugin: {name}")
    registry["plugins"].pop(name, None)
    save_registry(registry)
    shutil.rmtree(plugin_state_dir(name), ignore_errors=True)
    print(f"Removed {name}.")
    if not uninstalled:
        print("Meson had no install log; files installed by custom scripts may remain.")


def list_plugins() -> None:
    registry = load_registry()
    if not registry["plugins"]:
        print("No managed plugins installed.")
        return
    for name, state in sorted(registry["plugins"].items()):
        print(f"{name}: {state.get('built_for_wayfire_version', 'unknown')} ABI {state.get('built_for_abi', 'unknown')}")


def print_paths() -> None:
    target = build_target()
    print(f"user plugin dir: {user_plugin_dir()}")
    print(f"user metadata dir: {user_metadata_dir()}")
    print(f"managed install prefix: {managed_install_prefix()}")
    print(f"manager dir: {manager_root()}")
    print(f"pkg-config wayfire: {target['version']} ABI {target['abi']}")


def main() -> None:
    global VERBOSE, CLEAN

    parser = argparse.ArgumentParser(description="Manage user-installed Wayfire plugins")
    parser.add_argument("--verbose", action="store_true", help="print actions and commands as they run")
    parser.add_argument("--clean", action="store_true", help="start URL installs from a fresh clone")
    sub = parser.add_subparsers(dest="command", required=True)
    install = sub.add_parser("install")
    install.add_argument("source")
    update = sub.add_parser("update")
    update.add_argument("plugin", nargs="?")
    rebuild = sub.add_parser("rebuild")
    rebuild.add_argument("plugin", nargs="?")
    remove = sub.add_parser("remove")
    remove.add_argument("plugin")
    sub.add_parser("list")
    sub.add_parser("paths")
    args = parser.parse_args()
    VERBOSE = args.verbose
    CLEAN = args.clean

    if args.command == "install":
        install_or_update(args.source)
    elif args.command in ("update", "rebuild"):
        registry = load_registry()
        plugins = [args.plugin] if args.plugin else sorted(registry["plugins"])
        for plugin in plugins:
            state = registry["plugins"].get(plugin)
            if not state:
                raise SystemExit(f"Plugin is not managed: {plugin}")
            install_or_update(state["source"], update=args.command == "update")
    elif args.command == "remove":
        remove_plugin(args.plugin)
    elif args.command == "list":
        list_plugins()
    elif args.command == "paths":
        print_paths()


if __name__ == "__main__":
    main()
