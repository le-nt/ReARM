// SPDX-License-Identifier: Apache-2.0
int shared_data = 11;
__attribute__((visibility("protected"))) int protected_value = 23;
__attribute__((visibility("hidden"))) int hidden_value = 55;
int *protected_pointer = &protected_value;
int *hidden_pointer = &hidden_value;

int shared_fn(int input) { return input * 3 + shared_data; }
__attribute__((constructor)) static void provider_init(void) {
  // Prevent global-initializer folding: the linker test must retain INIT_ARRAY.
  __asm__ volatile("" ::: "memory");
  shared_data = 19;
}
__attribute__((destructor)) static void provider_fini(void) { shared_data = -1; }
