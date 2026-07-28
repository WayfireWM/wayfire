vswitch_plugin_test = executable(
    'vswitch-plugin-test',
    'vswitch-plugin-test.cpp',
    '../../support/headless-core-harness.cpp',
    dependencies: [doctest, libwayfire],
    cpp_args: [
        '-DTEST_METADATA_DIR="' + meson.project_source_root() + '/metadata"',
        '-DTEST_DEFAULTS_INI="' + meson.project_source_root() + '/wayfire.ini"',
        '-DTEST_PLUGIN_PATH="' + meson.project_build_root() + '/plugins/vswitch"',
    ],
    install: false)

test('Vswitch plugin test', vswitch_plugin_test, depends: [vswitch])
