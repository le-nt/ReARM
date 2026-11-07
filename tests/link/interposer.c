// SPDX-License-Identifier: Apache-2.0
extern int shared_fn(int);
int shared_data = 101;
int protected_value = 201;
int choice = 13;
int fixture_entry(int input) { return shared_fn(input); }
