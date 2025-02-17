tracking_allocator = executable(
    'tracking_allocator',
    'tracking-allocator.cpp',
    dependencies: libwayfire,
    install: false)
test('Tracking factory test', tracking_allocator)

safe_list = executable(
    'safe_list',
    'safe-list-test.cpp',
    dependencies: [doctest, wfconfig],
    install: false)
test('Safe list test', safe_list)
