mockliba = static_library('libmock',
    'mock-util.cpp',
    include_directories: [wayfire_conf_inc, wayfire_api_inc],
    dependencies: [wfconfig, doctest, pixman])

mocklib = declare_dependency(link_whole: mockliba,
    include_directories: [wayfire_conf_inc, wayfire_api_inc],
    dependencies: [wfconfig, doctest, pixman, libwayfire])

# Test mock event loop
mock_test = executable(
    'mock_util_test',
    ['mock-util-test.cpp'],
    dependencies: mocklib,
    install: false)
test('Mock Event Loop Test', mock_test)

subdir('geometry')
