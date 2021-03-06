/*
 * Copyright (c) 2017 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <ztest.h>
#include "test_sched.h"

#ifdef CONFIG_TIMESLICING

#define NUM_THREAD 3

BUILD_ASSERT(NUM_THREAD <= MAX_NUM_THREAD);

/* slice size in millisecond*/
#define SLICE_SIZE 200
/* busy for more than one slice*/
#define BUSY_MS (SLICE_SIZE + 20)
/* a half timeslice*/
#define HALF_SLICE_SIZE (SLICE_SIZE >> 1)
#define HALF_SLICE_SIZE_CYCLES \
	((u64_t)(HALF_SLICE_SIZE) * sys_clock_hw_cycles_per_sec() / 1000)

K_SEM_DEFINE(sema, 0, NUM_THREAD);
/*elapsed_slice taken by last thread*/
static u32_t elapsed_slice;
static int thread_idx;

static u32_t cycles_delta(u32_t *reftime)
{
	u32_t now, delta;

	now = k_cycle_get_32();
	delta = now - *reftime;
	*reftime = now;

	return delta;
}

static void thread_tslice(void *p1, void *p2, void *p3)
{
	u32_t t = cycles_delta(&elapsed_slice);
	u32_t expected_slice_min, expected_slice_max;

	if (thread_idx == 0) {
		/*
		 * Thread number 0 releases CPU after HALF_SLICE_SIZE,
		 * and we expect that task switch will not take more than 1 ms.
		 */
		expected_slice_min = (u64_t)(HALF_SLICE_SIZE - 1) *
				     sys_clock_hw_cycles_per_sec() / 1000;
		expected_slice_max = (u64_t)(HALF_SLICE_SIZE + 1) *
				     sys_clock_hw_cycles_per_sec() / 1000;
	} else {
		/*
		 * Other threads are sliced with tick granulity. Here, we
		 * also expecting task switch time below 1 ms.
		 */
		expected_slice_min = (z_ms_to_ticks(SLICE_SIZE) - 1) *
				     sys_clock_hw_cycles_per_tick();
		expected_slice_max = (z_ms_to_ticks(SLICE_SIZE) *
				     sys_clock_hw_cycles_per_tick()) +
				     (sys_clock_hw_cycles_per_sec() / 1000);
	}

	#ifdef CONFIG_DEBUG
	TC_PRINT("thread[%d] elapsed slice: %d, expected: <%d, %d>\n",
		thread_idx, t, expected_slice_min, expected_slice_max);
	#endif

	/** TESTPOINT: timeslice should be reset for each preemptive thread*/
#ifndef CONFIG_COVERAGE
	zassert_true(t >= expected_slice_min,
		     "timeslice too small, expected %u got %u",
		     expected_slice_min, t);
	zassert_true(t <= expected_slice_max,
		     "timeslice too big, expected %u got %u",
		     expected_slice_max, t);
#else
	(void)t;
#endif /* CONFIG_COVERAGE */
	thread_idx = (thread_idx + 1) % NUM_THREAD;

	/* Keep the current thread busy for more than one slice, even though,
	 * when timeslice used up the next thread should be scheduled in.
	 */
	spin_for_ms(BUSY_MS);
	k_sem_give(&sema);
}

/*test cases*/
/**
 * @brief Check the behavior of preemptive threads when the
 * time slice is disabled and enabled
 *
 * @details Create multiple preemptive threads with few different
 * priorities and few with same priorities and enable the time slice.
 * Ensure that each thread is given the time slice period to execute.
 *
 * @see k_sched_time_slice_set(), k_sem_reset(), k_cycle_get_32(),
 *      k_uptime_get_32()
 *
 * @ingroup kernel_sched_tests
 */
void test_slice_reset(void)
{
	u32_t t32;
	k_tid_t tid[NUM_THREAD];
	struct k_thread t[NUM_THREAD];
	int old_prio = k_thread_priority_get(k_current_get());

	thread_idx = 0;
	/*disable timeslice*/
	k_sched_time_slice_set(0, K_PRIO_PREEMPT(0));

	for (int j = 0; j < 2; j++) {
		k_sem_reset(&sema);
		/* update priority for current thread*/
		k_thread_priority_set(k_current_get(), K_PRIO_PREEMPT(j));
		/* create delayed threads with equal preemptive priority*/
		for (int i = 0; i < NUM_THREAD; i++) {
			tid[i] = k_thread_create(&t[i], tstacks[i], STACK_SIZE,
						 thread_tslice, NULL, NULL, NULL,
						 K_PRIO_PREEMPT(j), 0, 0);
		}
		/* enable time slice*/
		k_sched_time_slice_set(SLICE_SIZE, K_PRIO_PREEMPT(0));

		/*synchronize to tick boundary*/
		t32 = k_uptime_get_32();
		while (k_uptime_get_32() == t32) {
#if defined(CONFIG_ARCH_POSIX)
			k_busy_wait(50);
#endif
		}

		/*set reference time*/
		cycles_delta(&elapsed_slice);

		/* current thread (ztest native) consumed a half timeslice*/
		t32 = k_cycle_get_32();
		while (k_cycle_get_32() - t32 < HALF_SLICE_SIZE_CYCLES) {
#if defined(CONFIG_ARCH_POSIX)
			k_busy_wait(50);
#endif
		}

		/* relinquish CPU and wait for each thread to complete*/
		for (int i = 0; i < NUM_THREAD; i++) {
			k_sem_take(&sema, K_FOREVER);
		}

		/* test case teardown*/
		for (int i = 0; i < NUM_THREAD; i++) {
			k_thread_abort(tid[i]);
		}
		/* disable time slice*/
		k_sched_time_slice_set(0, K_PRIO_PREEMPT(0));
	}
	k_thread_priority_set(k_current_get(), old_prio);
}

#else /* CONFIG_TIMESLICING */
void test_slice_reset(void)
{
	ztest_test_skip();
}
#endif /* CONFIG_TIMESLICING */
