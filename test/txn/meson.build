txn_test = executable(
    'txn_test',
    ['txn-test.cpp'],
    dependencies: mocklib,
    install: false)
test('transaction_impl_t Test', txn_test)

txn_manager_test = executable(
    'txn_manager_test',
    ['txn-manager-test.cpp'],
    dependencies: mocklib,
    install: false)
test('transaction_manager_t Test', txn_manager_test)
