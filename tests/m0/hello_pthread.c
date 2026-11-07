// M0 toolchain check: a pthread spawned from wasm runs, shares memory with the main thread,
// and wakes it through a futex-backed condition variable.
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t done_cond = PTHREAD_COND_INITIALIZER;
static atomic_int counter;
static int done;

static void *worker(void *arg) {
  int id = (int)(long)arg;
  for (int i = 0; i < 100000; ++i) {
    atomic_fetch_add(&counter, 1);
  }
  printf("hello from pthread %d\n", id);
  pthread_mutex_lock(&lock);
  done++;
  pthread_cond_signal(&done_cond);
  pthread_mutex_unlock(&lock);
  return NULL;
}

int main(void) {
  pthread_t threads[4];
  for (long i = 0; i < 4; ++i) {
    pthread_create(&threads[i], NULL, worker, (void *)i);
  }
  pthread_mutex_lock(&lock);
  while (done < 4) {
    pthread_cond_wait(&done_cond, &lock);
  }
  pthread_mutex_unlock(&lock);
  for (int i = 0; i < 4; ++i) {
    pthread_join(threads[i], NULL);
  }
  int total = atomic_load(&counter);
  printf("counter=%d %s\n", total, total == 400000 ? "OK" : "FAIL");
  return total == 400000 ? 0 : 1;
}
