/*
 * SPDX-License-Identifier: MIT
 *
 * @file: status_ids.h (example)
 * @brief Example definition file for application-specific status IDs.
 *
 * @note
 *    This file is not part of the installed API. Copy and modify
 *    for your own project. Place it in your source tree and include
 *    it before using the status module.
 *
 * @note
 *    All status IDs must be encoded using STATUS_ENCODE(bank, bit),
 *    where bank < NUM_STATUS_BANKS and bit < 16. Violating this is undefined.
 *    Validity is enforced via runtime error callbacks and unit tests.
 */

#ifndef STATUS_IDS_H
#define STATUS_IDS_H

#include "status.h"

/* ----------------------
 * Fault Status IDs
 * ---------------------- */

// Bank 0: Power Faults
#define STATUS_ID_FAULT_OVERCURRENT    STATUS_ENCODE(0u, 0u)
#define STATUS_ID_FAULT_OVERVOLTAGE    STATUS_ENCODE(0u, 1u)
#define STATUS_ID_FAULT_UNDERVOLTAGE   STATUS_ENCODE(0u, 2u)
#define STATUS_ID_FAULT_DC_BUS_FAULT   STATUS_ENCODE(0u, 3u)

// Bank 1: Thermal Faults
#define STATUS_ID_FAULT_OVER_TEMP_AFE  STATUS_ENCODE(1U, 0U)
#define STATUS_ID_FAULT_OVER_TEMP_INV  STATUS_ENCODE(1U, 1U)

// Bank 2: Communication Faults
#define STATUS_ID_FAULT_CAN_TIMEOUT    STATUS_ENCODE(2U, 0U)
#define STATUS_ID_FAULT_MODULE_MISSING STATUS_ENCODE(2U, 1U)

/* ----------------------
 * Warning Status IDs
 * ---------------------- */

// Bank 3: Power Warnings
#define STATUS_ID_WARN_VOLTAGE_FLUCT   STATUS_ENCODE(3u, 0u)
#define STATUS_ID_WARN_CURRENT_NOISE   STATUS_ENCODE(3u, 1u)

// Bank 4: Thermal Warnings
#define STATUS_ID_WARN_TEMP_NEAR_LIMIT STATUS_ENCODE(4u, 0u)
#define STATUS_ID_WARN_FAN_PERF_DROP   STATUS_ENCODE(4u, 1u)

// Bank 5: Communication Warnings
#define STATUS_ID_WARN_CAN_LOAD_HIGH   STATUS_ENCODE(5u, 0u)
#define STATUS_ID_WARN_BROADCAST_LOSS  STATUS_ENCODE(5u, 1u)

/* ----------------------
 * Info Status IDs
 * ---------------------- */

// Bank 0: Power Info
#define STATUS_ID_INFO_AC_LIVE         STATUS_ENCODE(0u, 0u)

// Bank 1: Thermal Info
#define STATUS_ID_INFO_TEMP_CHANGING   STATUS_ENCODE(1u, 0u)

// Bank 2: Communication Info
#define STATUS_ID_INFO_CAN_ACTIVE      STATUS_ENCODE(2u, 0u)

#endif // STATUS_IDS_H
