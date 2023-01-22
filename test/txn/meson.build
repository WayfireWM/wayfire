txn_test = executable(
    'transaction-test',
    'transaction-test.cpp',
    dependencies: libwayfire,
    install: false)
test('Test transaction basic functionality', txn_test)

txn_manager_test = executable(
    'transaction-manager-test',
    'transaction-manager-test.cpp',
    dependencies: libwayfire,
    install: false)
test('Test transaction manager functionality', txn_manager_test)
