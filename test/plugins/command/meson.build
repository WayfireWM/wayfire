command_plugin_test = executable(
    'command-plugin-test',
    'command-plugin-test.cpp',
    '../../support/headless-core-harness.cpp',
    dependencies: [doctest, libwayfire],
    cpp_args: [
        '-DTEST_METADATA_DIR="' + meson.project_source_root() + '/metadata"',
        '-DTEST_DEFAULTS_INI="' + meson.project_source_root() + '/wayfire.ini"',
        '-DTEST_PLUGIN_PATH="' + meson.project_build_root() + '/plugins/ipc:' +
            meson.project_build_root() + '/plugins/single_plugins"',
    ],
    install: false)

test('Command plugin test', command_plugin_test, depends: [ipc, command_plugin])
