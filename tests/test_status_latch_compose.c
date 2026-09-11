/*
 * @file test_status_latch_compose.c
 * @brief The documented active/latched composition must not drop fault history.
 *
 * docs/api.md composes an active register and a latched register so that latch
 * policy stays outside the primitive; docs/design.md analyses why the lock is
 * required. The acknowledgement path reads `active` and then clears `latched`,
 * which is a check-then-clear pair. Run without serialisation, a producer can
 * set `active` and then `latched` between those two steps, and the
 * acknowledgement then clears the history the producer has just recorded,
 * leaving an active fault with no latched history.
 *
 * The documented composition therefore holds one lock across the producer's two
 * writes and across the acknowledgement's check and clear. This test races the
 * two paths and asserts the invariant the composition exists to uphold: if
 * `active` is set, `latched` is set. The check runs while the lock is held,
 * where the invariant must hold unconditionally.
 *
 * Dropping the lock from the acknowledgement path makes this test fail, which
 * is how the composition was verified.
 */

#include "status.h"
#include "status_test.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#define LATCH_ID STATUS_ENCODE(0u, 0u)

#if STATUS_TEST_SANITIZED
#define LATCH_ROUNDS 2000u
#else
#define LATCH_ROUNDS 10000u
#endif

static status_reg_t active_reg;
static status_reg_t latched_reg;
static pthread_mutex_t latch_lock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t lost_history;

/* Producer write path: record both states while holding the lock. */
static void *
latch_producer(void *arg)
{
        (void)arg;

        for (uint32_t i = 0u; i < LATCH_ROUNDS; i++) {
                TEST_ASSERT(pthread_mutex_lock(&latch_lock) == 0);
                status_reg_set_fault(&active_reg, LATCH_ID);
                status_reg_set_fault(&latched_reg, LATCH_ID);
                TEST_ASSERT(pthread_mutex_unlock(&latch_lock) == 0);
        }

        return NULL;
}

/* Recovery clears the live state; acknowledgement clears history under the
 * lock, and checks the invariant at the only point where it must hold. */
static void *
latch_acker(void *arg)
{
        (void)arg;

        for (uint32_t i = 0u; i < LATCH_ROUNDS; i++) {
                status_reg_clear_fault(&active_reg, LATCH_ID);

                TEST_ASSERT(pthread_mutex_lock(&latch_lock) == 0);
                if (!status_reg_is_fault_set(&active_reg, LATCH_ID)) {
                        status_reg_clear_fault(&latched_reg, LATCH_ID);
                }
                if (status_reg_is_fault_set(&active_reg, LATCH_ID)
                    && !status_reg_is_fault_set(&latched_reg, LATCH_ID)) {
                        lost_history++;
                }
                TEST_ASSERT(pthread_mutex_unlock(&latch_lock) == 0);
        }

        return NULL;
}

int
main(void)
{
        pthread_t producer;
        pthread_t acker;

        status_reg_init(&active_reg);
        status_reg_init(&latched_reg);

        TEST_ASSERT(pthread_create(&producer, NULL, latch_producer, NULL) == 0);
        TEST_ASSERT(pthread_create(&acker, NULL, latch_acker, NULL) == 0);
        TEST_ASSERT(pthread_join(producer, NULL) == 0);
        TEST_ASSERT(pthread_join(acker, NULL) == 0);

        TEST_ASSERT(lost_history == 0u);

        TEST_PASS(__FILE__);
        return EXIT_SUCCESS;
}
