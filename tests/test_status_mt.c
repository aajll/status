/*
 * @file test_status_mt.c
 * @brief Concurrent lost-update detection for the atomic bit operations.
 *
 * The redesign's core claim is that set/clear of a single bit is an atomic
 * read-modify-write, so concurrent updates to distinct bits of the SAME bank
 * word never lose each other. A non-atomic `word |= bit` would
 * read-modify-write the whole word and clobber a neighbour's concurrent update.
 *
 * This test makes that claim falsifiable. Eight worker threads each own two
 * adjacent bits of bank 0 (sixteen bits in all). Every round, in lockstep:
 *   1. all workers OR their bit in (from a zeroed word), then
 *   2. the coordinator asserts the word is fully set (0xFFFF) - any lost OR
 *      shows up as a missing bit, then
 *   3. all workers AND their bit out (from the full word), then
 *   4. the coordinator asserts the word is zero - any lost AND shows up as a
 *      stuck bit.
 * Thousands of rounds re-run the race to catch rare interleavings. This same
 * executable, rebuilt with -Db_sanitize=thread, is the TSAN-clean gate: the
 * library uses relaxed atomics, so a clean TSAN run proves the accesses are
 * genuinely atomic rather than a benign-looking data race.
 *
 * The phase barrier is a C11 generation barrier (portable; macOS lacks
 * pthread_barrier). Its release/acquire edges also give the coordinator the
 * happens-before it needs to observe the workers' relaxed status writes.
 */

#include "status.h"
#include "status_test.h"

#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Eight workers, two adjacent bits each, fully populate a 16-bit bank. Keeping
 * the worker count near the core count avoids pathological oversubscription of
 * the spin barrier; 16 distinct bits still give dense contention on the shared
 * word, which is what exercises the atomic read-modify-write.
 */
#define NUM_BIT_THREADS 8
#define BITS_PER_THREAD 2
#define SHARED_BANK     0u
#define FULL_MASK       0xFFFFu

#if defined(STATUS_TEST_INSTANCE)
static status_reg_t shared_reg;
#define STATUS_INIT()          status_reg_init(&shared_reg)
#define STATUS_SET_FAULT(id)   status_reg_set_fault(&shared_reg, (id))
#define STATUS_CLEAR_FAULT(id) status_reg_clear_fault(&shared_reg, (id))
#define STATUS_TEST_AND_CLEAR_FAULT(id)                                        \
        status_reg_test_and_clear_fault(&shared_reg, (id))
#define STATUS_SNAPSHOT(cls, dst, len)                                         \
        status_reg_snapshot(&shared_reg, (cls), (dst), (len))
#define STATUS_ANY(cls) status_reg_any(&shared_reg, (cls))
#else
#define STATUS_INIT()                   status_init()
#define STATUS_SET_FAULT(id)            status_set_fault(id)
#define STATUS_CLEAR_FAULT(id)          status_clear_fault(id)
#define STATUS_TEST_AND_CLEAR_FAULT(id) status_test_and_clear_fault(id)
#define STATUS_SNAPSHOT(cls, dst, len)  status_snapshot((cls), (dst), (len))
#define STATUS_ANY(cls)                 status_any(cls)
#endif

/* Scaled down under sanitizers (see status_test.h); overridable via -D. */
#ifndef MT_ROUNDS
#if STATUS_TEST_SANITIZED
#define MT_ROUNDS 2000u
#else
#define MT_ROUNDS 10000u
#endif
#endif

/* ---- Portable generation barrier (acq/rel; reusable across rounds) ------- */

typedef struct {
        atomic_uint count;
        atomic_uint gen;
        unsigned int n;
} barrier_t;

static void
barrier_init(barrier_t *b, unsigned int n)
{
        atomic_store_explicit(&b->count, 0u, memory_order_relaxed);
        atomic_store_explicit(&b->gen, 0u, memory_order_relaxed);
        b->n = n;
}

static void
barrier_wait(barrier_t *b)
{
        unsigned int g = atomic_load_explicit(&b->gen, memory_order_acquire);
        unsigned int arrived =
            atomic_fetch_add_explicit(&b->count, 1u, memory_order_acq_rel) + 1u;

        if (arrived == b->n) {
                atomic_store_explicit(&b->count, 0u, memory_order_relaxed);
                /* Release: publishes everything done before the barrier. */
                (void)atomic_fetch_add_explicit(&b->gen, 1u,
                                                memory_order_release);
        } else {
                /* Acquire: pairs with the releaser's gen bump. Yield rather
                 * than busy-spin so the barrier stays cheap when threads
                 * outnumber cores (the common CI case). */
                while (atomic_load_explicit(&b->gen, memory_order_acquire)
                       == g) {
                        (void)sched_yield();
                }
        }
}

/* ------------------------------------------------------------------------- */

static barrier_t g_barrier;
static atomic_ullong g_worker_ops;
static atomic_uint g_clear_winners;

static void *
bit_worker(void *arg)
{
        unsigned int idx = (unsigned int)(uintptr_t)arg;
        uint16_t id_a = STATUS_ENCODE(SHARED_BANK, idx * BITS_PER_THREAD);
        uint16_t id_b = STATUS_ENCODE(SHARED_BANK, idx * BITS_PER_THREAD + 1u);
        unsigned long long ops = 0ull;

        for (unsigned int r = 0u; r < MT_ROUNDS; ++r) {
                STATUS_SET_FAULT(id_a); /* phase 1: race the OR */
                STATUS_SET_FAULT(id_b);
                barrier_wait(&g_barrier);
                barrier_wait(&g_barrier); /* coordinator checks FULL */
                STATUS_CLEAR_FAULT(id_a); /* phase 3: race the AND */
                STATUS_CLEAR_FAULT(id_b);
                barrier_wait(&g_barrier);
                barrier_wait(&g_barrier); /* coordinator checks ZERO */
                ops += 2ull;
        }

        (void)atomic_fetch_add_explicit(&g_worker_ops, ops,
                                        memory_order_relaxed);
        return NULL;
}

static void *
test_and_clear_worker(void *arg)
{
        (void)arg;

        barrier_wait(&g_barrier);
        if (STATUS_TEST_AND_CLEAR_FAULT(STATUS_ENCODE(SHARED_BANK, 0u))) {
                (void)atomic_fetch_add_explicit(&g_clear_winners, 1u,
                                                memory_order_relaxed);
        }
        barrier_wait(&g_barrier);
        return NULL;
}

static uint16_t
read_shared_bank(void)
{
        uint16_t snap[NUM_STATUS_BANKS];

        STATUS_SNAPSHOT(STATUS_CLASS_FAULT, snap, NUM_STATUS_BANKS);
        return snap[SHARED_BANK];
}

int
main(void)
{
        pthread_t workers[NUM_BIT_THREADS];

        (void)fprintf(stdout,
                      "\n=== status concurrent lost-update test ===\n\n");

        STATUS_INIT();
        barrier_init(&g_barrier, (unsigned int)NUM_BIT_THREADS + 1u);
        atomic_store_explicit(&g_worker_ops, 0ull, memory_order_relaxed);

        for (uintptr_t i = 0u; i < NUM_BIT_THREADS; ++i) {
                TEST_ASSERT(
                    pthread_create(&workers[i], NULL, bit_worker, (void *)i)
                    == 0);
        }

        for (unsigned int r = 0u; r < MT_ROUNDS; ++r) {
                barrier_wait(&g_barrier); /* workers finished the OR race */
                TEST_ASSERT(read_shared_bank() == FULL_MASK);
                barrier_wait(
                    &g_barrier); /* release workers into the AND race */
                barrier_wait(&g_barrier); /* workers finished the AND race */
                TEST_ASSERT(read_shared_bank() == 0u);
                barrier_wait(&g_barrier); /* release workers into next round */
        }

        for (size_t i = 0u; i < NUM_BIT_THREADS; ++i) {
                TEST_ASSERT(pthread_join(workers[i], NULL) == 0);
        }

        /* Liveness: every worker completed every round. */
        TEST_ASSERT(atomic_load_explicit(&g_worker_ops, memory_order_relaxed)
                    == (unsigned long long)NUM_BIT_THREADS * MT_ROUNDS * 2ull);
        TEST_ASSERT(STATUS_ANY(STATUS_CLASS_FAULT) == false);

        {
                pthread_t consumers[2];
                const uint16_t id = STATUS_ENCODE(SHARED_BANK, 0u);

                STATUS_SET_FAULT(id);
                barrier_init(&g_barrier, 3u);
                atomic_store_explicit(&g_clear_winners, 0u,
                                      memory_order_relaxed);
                for (size_t i = 0u; i < 2u; ++i) {
                        TEST_ASSERT(pthread_create(&consumers[i], NULL,
                                                   test_and_clear_worker, NULL)
                                    == 0);
                }
                barrier_wait(&g_barrier);
                barrier_wait(&g_barrier);
                for (size_t i = 0u; i < 2u; ++i) {
                        TEST_ASSERT(pthread_join(consumers[i], NULL) == 0);
                }
                TEST_ASSERT(
                    atomic_load_explicit(&g_clear_winners, memory_order_relaxed)
                    == 1u);
                TEST_ASSERT(!STATUS_ANY(STATUS_CLASS_FAULT));
        }

        (void)fprintf(
            stdout, "rounds=%u  worker ops=%llu (no lost updates)\n",
            (unsigned int)MT_ROUNDS,
            atomic_load_explicit(&g_worker_ops, memory_order_relaxed));
        (void)fprintf(stdout, "\n=== concurrent test passed ===\n\n");
        return EXIT_SUCCESS;
}
