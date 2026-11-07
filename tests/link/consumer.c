// SPDX-License-Identifier: Apache-2.0
extern int shared_data;
extern int shared_fn(int);
int *imported_data = &shared_data;
int (*imported_fn)(int) = shared_fn;
int consumer_ready;
volatile unsigned fixture_output[16];

int fixture_entry(unsigned seed) {
  unsigned value = (unsigned)shared_fn((int)(seed & 127)) + (unsigned)shared_data;
  for (unsigned index = 0; index < 16; ++index) {
    value = value * 33u + (unsigned)imported_fn((int)index) + (unsigned)*imported_data;
    fixture_output[index] = value;
  }
  return (int)(value ^ (unsigned)consumer_ready);
}
__attribute__((constructor)) static void consumer_init(void) { consumer_ready = shared_fn(1); }
__attribute__((destructor)) static void consumer_fini(void) { consumer_ready = -1; }
