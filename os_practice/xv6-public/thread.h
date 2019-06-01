/**
 * This is the thread API created to implement Light-Weight Process (LWP)
 * The implemented thread is kernel-thread.
 */

/**
 * A thread is a basic unit of CPU utilization,
 * and it consists of program counter, register set, and stack space.
 */
typedef unsigned int thread_t;

int thread_create(thread_t* thread, void*(*start_routine)(void*), void* arg);
void thread_exit(void* retval);
int thread_join(thread_t thread, void** retval);
