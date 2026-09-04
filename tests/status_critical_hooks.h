/*
 * @file status_critical_hooks.h
 * @brief Instrumented critical-section hooks for no-atomics tests.
 */

#ifndef STATUS_CRITICAL_HOOKS_H_
#define STATUS_CRITICAL_HOOKS_H_

void status_test_enter_critical(void);
void status_test_exit_critical(void);

#define STATUS_ENTER_CRITICAL() status_test_enter_critical()
#define STATUS_EXIT_CRITICAL()  status_test_exit_critical()

#endif /* STATUS_CRITICAL_HOOKS_H_ */
