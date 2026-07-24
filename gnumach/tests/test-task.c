/*
 *  Copyright (C) 2024 Free Software Foundation
 *
 * This program is free software ; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY ; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with the program ; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <syscalls.h>
#include <testlib.h>

#include <mach/machine/vm_param.h>
#include <mach/std_types.h>
#include <mach/mach_types.h>
#include <mach/vm_wire.h>
#include <mach/vm_param.h>

#include <gnumach.user.h>
#include <mach.user.h>
#include <mach_host.user.h>


void test_task()
{
  mach_port_t ourtask = mach_task_self();
  mach_msg_type_number_t count;
  int err;

  struct task_basic_info binfo;
  count = TASK_BASIC_INFO_COUNT;
  err = task_info(ourtask, TASK_BASIC_INFO, (task_info_t)&binfo, &count);
  ASSERT_RET(err, "TASK_BASIC_INFO");
  ASSERT(binfo.virtual_size > binfo.resident_size, "wrong memory counters");

  struct task_events_info einfo;
  count = TASK_EVENTS_INFO_COUNT;
  err = task_info(ourtask, TASK_EVENTS_INFO, (task_info_t)&einfo, &count);
  ASSERT_RET(err, "TASK_EVENTS_INFO");
  printf("msgs sent %llu received %llu\n",
         einfo.messages_sent, einfo.messages_received);

  struct task_thread_times_info ttinfo;
  count = TASK_THREAD_TIMES_INFO_COUNT;
  err = task_info(ourtask, TASK_THREAD_TIMES_INFO, (task_info_t)&ttinfo, &count);
  ASSERT_RET(err, "TASK_THREAD_TIMES_INFO");
  printf("run user %lld system %lld\n",
         ttinfo.user_time64.seconds, ttinfo.user_time64.nanoseconds);
}


void dummy_thread(void *arg)
{
  printf("started dummy thread\n");
  while (1)
    ;
}

void check_threads(thread_t *threads, mach_msg_type_number_t nthreads)
{  
  for (int tid=0; tid<nthreads; tid++)
    {
      struct thread_basic_info tinfo;
      mach_msg_type_number_t thcount = THREAD_BASIC_INFO_COUNT;
      int err = thread_info(threads[tid], THREAD_BASIC_INFO, (thread_info_t)&tinfo, &thcount);
      ASSERT_RET(err, "thread_info");
      ASSERT(thcount == THREAD_BASIC_INFO_COUNT, "thcount");
      printf("th%d (port %d):\n", tid, threads[tid]);
      printf(" user time %d.%06d\n", tinfo.user_time.seconds, tinfo.user_time.microseconds);
      printf(" system time %d.%06d\n", tinfo.system_time.seconds, tinfo.system_time.microseconds);
      printf(" cpu usage %d\n", tinfo.cpu_usage);
      printf(" creation time %d.%06d\n", tinfo.creation_time.seconds, tinfo.creation_time.microseconds);
    }
}

static void test_task_threads()
{
  thread_t *threads;
  mach_msg_type_number_t nthreads;
  int err;

  err = task_threads(mach_task_self(), &threads, &nthreads);
  ASSERT_RET(err, "task_threads");
  ASSERT(nthreads == 1, "nthreads");
  check_threads(threads, nthreads);

  thread_t t1 = test_thread_start(mach_task_self(), dummy_thread, 0);

  thread_t t2 = test_thread_start(mach_task_self(), dummy_thread, 0);

  // let the threads run
  msleep(100);

  err = task_threads(mach_task_self(), &threads, &nthreads);
  ASSERT_RET(err, "task_threads");
  ASSERT(nthreads == 3, "nthreads");
  check_threads(threads, nthreads);

  err = thread_terminate(t1);
  ASSERT_RET(err, "thread_terminate");
  err = thread_terminate(t2);
  ASSERT_RET(err, "thread_terminate");

  err = task_threads(mach_task_self(), &threads, &nthreads);
  ASSERT_RET(err, "task_threads");
  ASSERT(nthreads == 1, "nthreads");
  check_threads(threads, nthreads);
}

void test_new_task()
{
  int err;
  task_t newtask;
  err = task_create(mach_task_self(), 1, &newtask);
  ASSERT_RET(err, "task_create");

  err = task_suspend(newtask);
  ASSERT_RET(err, "task_suspend");

  err = task_set_name(newtask, "newtask");
  ASSERT_RET(err, "task_set_name");

  thread_t *threads;
  mach_msg_type_number_t nthreads;
  err = task_threads(newtask, &threads, &nthreads);
  ASSERT_RET(err, "task_threads");
  ASSERT(nthreads == 0, "nthreads 0");

  test_thread_start(newtask, dummy_thread, 0);

  err = task_resume(newtask);
  ASSERT_RET(err, "task_resume");

  msleep(100);  // let the thread run a bit

  err = task_threads(newtask, &threads, &nthreads);
  ASSERT_RET(err, "task_threads");
  ASSERT(nthreads == 1, "nthreads 1");
  check_threads(threads, nthreads);

  err = thread_terminate(threads[0]);
  ASSERT_RET(err, "thread_terminate");

  err = task_terminate(newtask);
  ASSERT_RET(err, "task_terminate");
}

int test_errors()
{
    int err;
    err = task_resume(MACH_PORT_NAME_DEAD);
    ASSERT(err == MACH_SEND_INVALID_DEST, "task DEAD");
}

void test_priority()
{
/* XXX cannot include <kern/sched.h> for BASEPRI_* constants */
#define BASEPRI_USER	25
#define BASEPRI_SYSTEM	6

#define DEBUG_PRINT(fmt, ...) \
		printf("[%s:%d :: %s] " fmt, \
			__FILE__, __LINE__, __func__, \
			##__VA_ARGS__)

	int				count;
	kern_return_t			err;
	processor_set_t			pset;
	processor_set_name_t		psetn;
	struct processor_set_sched_info	pset_sched;
	struct task_basic_info		tk_basic;
	struct thread_sched_info	th_sched;
	task_t				new_task;
	thread_t			new_thread;
	host_t				host;

	/*
	 *	Here are short notes on what behaviour this test
	 *	is attempting to guarantee and document how things
	 *	should work.
	 */

	/*
	 *	The default processor set max priority
	 *	should be set to BASEPRI_SYSTEM.
	 */
	void test_default_pset_max_priority()
	{
		err = processor_set_default(mach_host_self(), &psetn);
		ASSERT_RET(err, "processor_set_default failed");

		count = PROCESSOR_SET_SCHED_INFO_COUNT;
		err = processor_set_info(psetn, PROCESSOR_SET_SCHED_INFO,
				&host, (processor_set_info_t) &pset_sched,
				&count);
		ASSERT_RET(err, "processor_set_info failed");

		DEBUG_PRINT("(default pset) max_priority: %d\n",
			pset_sched.max_priority);
		ASSERT(pset_sched.max_priority == BASEPRI_SYSTEM,
			"expecting max_priority to be BASEPRI_SYSTEM");
	}
	/*
	 *	New processor sets should also get a max_priority of
	 *	BASEPRI_SYSTEM.
	 */
	void test_new_pset_max_priority()
	{
#if	MACH_HOST
		err = processor_set_create(mach_host_self(), &pset, &psetn);
		ASSERT_RET(err, "processor_set_create failed");

		count = PROCESSOR_SET_SCHED_INFO_COUNT;
		err = processor_set_info(psetn, PROCESSOR_SET_SCHED_INFO,
				&host, (processor_info_t) &pset_sched,
				&count);
		ASSERT_RET(err, "processor_set_info failed");

		DEBUG_PRINT("(new pset) max_priority: %d\n",
			pset_sched.max_priority);
		ASSERT(pset_sched.max_priority == BASEPRI_SYSTEM,
			"expecting max_priority to be BASEPRI_SYSTEM");

		err = processor_set_destroy(pset);
		ASSERT_RET(err, "processor_set_destroy failed");
#endif
	}
	/*
	 *	Since boot_script_task_create sets the max_priority
	 *	of the task to BASEPRI_USER, check that the current
	 *	task has the corresponding priorities.
	 *
	 *	The priority may be observed through task_info while the
	 *	max priority is only observable through a thread inheriting
	 *	it on thread create.
	 */
	void test_default_priorities()
	{
		/* Create thread as a proxy of max_priority */
		err = thread_create(mach_task_self(), &new_thread);
		ASSERT_RET(err, "thread_create failed");

		count = TASK_BASIC_INFO_COUNT;
		err = task_info(mach_task_self(), TASK_BASIC_INFO,
			(task_info_t) &tk_basic, &count);
		ASSERT_RET(err, "task_info failed");

		count = THREAD_SCHED_INFO_COUNT;
		err = thread_info(new_thread, THREAD_SCHED_INFO,
			(thread_info_t) &th_sched, &count);
		ASSERT_RET(err, "thread_info failed");


		DEBUG_PRINT("(task) base_priority: %d\n",
			tk_basic.base_priority);
		DEBUG_PRINT("(thread) base_priority: %d\n",
			th_sched.base_priority);
		DEBUG_PRINT("(thread) max_priority: %d\n",
			th_sched.max_priority);
		ASSERT(tk_basic.base_priority == BASEPRI_USER,
			"(task) expected base priority of BASEPRI_USER");
		ASSERT(th_sched.base_priority == BASEPRI_USER,
			"(thread) expected base priority of BASEPRI_USER");
		ASSERT(th_sched.max_priority == BASEPRI_USER,
			"(thread) expected max priority of BASEPRI_USER");

		err = thread_terminate(new_thread);
		ASSERT_RET(err, "thread_terminate failed");
	}
	/*
	 *	A task may lower its own priority. Any thread created
	 *	afterwards shall inherit the max priority of the task.
	 */
	void test_task_max_priority_thread_inheritance()
	{
		/* Set new max priority to something we know will "win" when
		 * compared to the procesor set max_priority and that we also
		 * know it's different from the default.
		 */
		int new_max_priority = BASEPRI_USER + 5;

		err = task_create(mach_task_self(), FALSE, &new_task);
		ASSERT_RET(err, "task_create failed");

		err = task_max_priority(mach_host_self(), new_task,
			new_max_priority, TRUE, FALSE);
		ASSERT_RET(err, "task_max_priority");

		err = thread_create(new_task, &new_thread);
		ASSERT_RET(err, "thread_create failed");

		count = THREAD_SCHED_INFO_COUNT;
		err = thread_info(new_thread, THREAD_SCHED_INFO,
			(thread_info_t) &th_sched, &count);
		ASSERT_RET(err, "thread_info failed");

		DEBUG_PRINT("(thread) base_priority: %d\n",
			th_sched.base_priority);
		DEBUG_PRINT("(thread) max_priority: %d\n",
			th_sched.max_priority);
		ASSERT(th_sched.base_priority == new_max_priority,
			"(thread) expected base priority of BASEPRI_USER");
		ASSERT(th_sched.max_priority == new_max_priority,
			"(thread) expected max priority of BASEPRI_USER");

		err = thread_terminate(new_thread);
		ASSERT_RET(err, "thread_terminate failed");
		err = task_terminate(new_task);
		ASSERT_RET(err, "task_terminate failed");
	}

	/*
	 *	A child task shall inherit the priority and max priority
	 *	of the parent task.
	 */
	void test_task_priority_inheritance()
	{
		/* Same rationale as the previous test */
		int max_priority = BASEPRI_USER + 5;
		task_t child_task;

		err = task_create(mach_task_self(), FALSE, &new_task);
		ASSERT_RET(err, "task_create failed");

		err = task_max_priority(mach_host_self(), new_task,
			max_priority, TRUE, FALSE);
		ASSERT_RET(err, "task_max_priority");

		/* Create the child task to check priority inheritance */
		err = task_create(new_task, FALSE, &child_task);
		ASSERT_RET(err, "task_create failed");

		/* Create thread as a proxy of max_priority */
		err = thread_create(child_task, &new_thread);
		ASSERT_RET(err, "thread_create failed");

		count = TASK_BASIC_INFO_COUNT;
		err = task_info(child_task, TASK_BASIC_INFO,
			(task_info_t) &tk_basic, &count);
		ASSERT_RET(err, "task_info failed");

		count = THREAD_SCHED_INFO_COUNT;
		err = thread_info(new_thread, THREAD_SCHED_INFO,
			(thread_info_t) &th_sched, &count);
		ASSERT_RET(err, "thread_info failed");


		DEBUG_PRINT("(task) base_priority: %d\n",
			tk_basic.base_priority);
		DEBUG_PRINT("(thread) base_priority: %d\n",
			th_sched.base_priority);
		DEBUG_PRINT("(thread) max_priority: %d\n",
			th_sched.max_priority);
		ASSERT(tk_basic.base_priority == max_priority,
			"(task) expected base priority of BASEPRI_USER + 5");
		ASSERT(th_sched.base_priority == max_priority,
			"(thread) expected base priority of BASEPRI_USER + 5");
		ASSERT(th_sched.max_priority == max_priority,
			"(thread) expected max priority of BASEPRI_USER + 5");

		err = thread_terminate(new_thread);
		ASSERT_RET(err, "thread_terminate failed");
		err = task_terminate(child_task);
		ASSERT_RET(err, "task_terminate failed");
		err = task_terminate(new_task);
		ASSERT_RET(err, "task_terminate failed");
	}

	/*
	 *	A non-privileged task shall not be able to raise its max_priority
	 *	nor its priority above its current max_priority.
	 */
	void test_task_priority_non_privileged()
	{
		/* Set max_priority to something below the current max */
		int max_priority = BASEPRI_USER - 10;
		/* Set priority to something below the current max */
		int priority = BASEPRI_USER - 5;

		/* Prepare a new clean task to test priority permissions */
		err = task_create(mach_task_self(), FALSE, &new_task);
		ASSERT_RET(err, "task_create failed");

		err = task_priority(new_task, priority, FALSE);
		ASSERT(err == KERN_NO_ACCESS, "(task) raising priority shall fail");
		err = task_max_priority(mach_host_self(), new_task,
			max_priority, TRUE, FALSE);
		ASSERT(err == KERN_NO_ACCESS, "(task) raising max priority shall fail");

		err = task_terminate(new_task);
		ASSERT_RET(err, "task_terminate failed");
	}

	/*
	 *	A privileged task shall be able to raise its max_priority
	 *	above its current max_priority.
	 */
	void test_task_priority_privileged()
	{
		/* Set max_priority to something below the current max */
		int max_priority = BASEPRI_USER - 10;
		/*
		 * setting priority to this value should succeed after
		 * setting max_priority
		 */
		int priority = BASEPRI_USER - 5;

		/* Prepare a new clean task to test priority permissions */
		err = task_create(mach_task_self(), FALSE, &new_task);
		ASSERT_RET(err, "task_create failed");

		/* Start testing privileged priority setting */
		err = task_max_priority(host_priv(), new_task,
			max_priority, FALSE, FALSE);
		ASSERT_RET(err, "(task) raising the max_priority shall succeed");
		err = task_priority(new_task, priority, FALSE);
		ASSERT_RET(err, "(task) raising priority below max_priority shall succeed");

		/* Create thread as a proxy of max_priority */
		err = thread_create(new_task, &new_thread);
		ASSERT_RET(err, "thread_create failed");

		count = TASK_BASIC_INFO_COUNT;
		err = task_info(new_task, TASK_BASIC_INFO,
			(task_info_t) &tk_basic, &count);
		ASSERT_RET(err, "task_info failed");

		count = THREAD_SCHED_INFO_COUNT;
		err = thread_info(new_thread, THREAD_SCHED_INFO,
			(thread_info_t) &th_sched, &count);
		ASSERT_RET(err, "thread_info failed");

		DEBUG_PRINT("(task) base_priority: %d\n",
			tk_basic.base_priority);
		DEBUG_PRINT("(thread) base_priority: %d\n",
			th_sched.base_priority);
		DEBUG_PRINT("(thread) max_priority: %d\n",
			th_sched.max_priority);
		ASSERT(tk_basic.base_priority == priority,
			"(task) expected base priority of BASEPRI_USER - 5");
		ASSERT(th_sched.base_priority == priority,
			"(thread) expected base priority of BASEPRI_USER - 5");
		ASSERT(th_sched.max_priority == max_priority,
			"(thread) expected max priority of BASEPRI_USER - 10");

		err = thread_terminate(new_thread);
		ASSERT_RET(err, "thread_terminate failed");
		err = task_terminate(new_task);
		ASSERT_RET(err, "task_terminate failed");
	}

	/*
	 *	The processor set max priority must act as a ceiling
	 *	of task priorities at the time that thread_create is
	 *	called.
	 */
	void test_pset_ceiling()
	{
		/* Set max priority above PSET default */
		int new_max_priority = BASEPRI_SYSTEM - 1;

		err = task_create(mach_task_self(), FALSE, &new_task);
		ASSERT_RET(err, "task_create failed");

		err = task_max_priority(host_priv(), new_task,
			new_max_priority, TRUE, FALSE);
		ASSERT_RET(err, "task_max_priority failed");

		/* Create new thread to check its priority/max_priority */
		err = thread_create(new_task, &new_thread);
		ASSERT_RET(err, "thread_create failed");

		count = THREAD_SCHED_INFO_COUNT;
		err = thread_info(new_thread, THREAD_SCHED_INFO,
			(thread_info_t) &th_sched, &count);
		ASSERT_RET(err, "thread_info failed");

		DEBUG_PRINT("(thread) base_priority: %d\n",
			th_sched.base_priority);
		DEBUG_PRINT("(thread) max_priority: %d\n",
			th_sched.max_priority);
		ASSERT(th_sched.base_priority == BASEPRI_SYSTEM,
			"(thread) expected base priority of BASEPRI_SYSTEM");
		ASSERT(th_sched.max_priority == BASEPRI_SYSTEM,
			"(thread) expected max priority of BASEPRI_SYSTEM");

		err = thread_terminate(new_thread);
		ASSERT_RET(err, "thread_terminate failed");
		err = task_terminate(new_task);
		ASSERT_RET(err, "task_terminate failed");
	}

	/*
	 *	Passing TRUE to change_threads should update
	 *	the max priority of the current task's threads.
	 */
	void test_updating_threads_max_priority()
	{
		thread_t t1, t2;
		struct thread_sched_info ths1, ths2;

		/* new priority to test task_priority */
		int new_priority = BASEPRI_USER + 5;
		/* new max priority to test task_max_priority */
		int new_max_priority = BASEPRI_USER - 5;

		err = task_create(mach_task_self(), TRUE, &new_task);
		ASSERT_RET(err, "task_create failed");


		/* Test lowering threads priorities */
		err = thread_create(new_task, &t1);
		ASSERT_RET(err, "thread_create failed");
		err = thread_create(new_task, &t2);
		ASSERT_RET(err, "thread_create failed");

		err = task_priority(new_task, new_priority, TRUE);
		ASSERT_RET(err, "task_priority failed");

		/* Check effects */
		count = THREAD_SCHED_INFO_COUNT;
		err = thread_info(t1, THREAD_SCHED_INFO,
			(thread_info_t) &ths1, &count);
		ASSERT_RET(err, "thread_info failed");
		count = THREAD_SCHED_INFO_COUNT;
		err = thread_info(t2, THREAD_SCHED_INFO,
			(thread_info_t) &ths2, &count);
		ASSERT_RET(err, "thread_info failed");

		DEBUG_PRINT("(t1) base_priority %d\n", ths1.base_priority);
		DEBUG_PRINT("(t1) max_priority %d\n", ths1.max_priority);
		DEBUG_PRINT("(t2) base_priority %d\n", ths2.base_priority);
		DEBUG_PRINT("(t2) max_priority %d\n", ths2.max_priority);
		ASSERT(ths1.base_priority == new_priority,
			"(t1) expecting priority BASEPRI_USER + 5");
		ASSERT(ths1.max_priority == BASEPRI_USER,
			"(t1) expecting priority BASEPRI_USER");
		ASSERT(ths2.base_priority == new_priority,
			"(t2) expecting priority BASEPRI_USER + 5");
		ASSERT(ths2.max_priority == BASEPRI_USER,
			"(t2) expecting priority BASEPRI_USER");

		/* Clean up */
		err = thread_terminate(t1);
		ASSERT_RET(err, "thread_terminate failed");
		err = thread_terminate(t2);
		ASSERT_RET(err, "thread_terminate failed");

		/* Test raising priorities */
		err = thread_create(new_task, &t1);
		ASSERT_RET(err, "thread_create failed");
		err = thread_create(new_task, &t2);
		ASSERT_RET(err, "thread_create failed");

		err = task_max_priority(host_priv(), new_task,
			new_max_priority, TRUE, TRUE);
		ASSERT_RET(err, "task_max_priority failed");

		/* Check effects */
		count = THREAD_SCHED_INFO_COUNT;
		err = thread_info(t1, THREAD_SCHED_INFO,
			(thread_info_t) &ths1, &count);
		ASSERT_RET(err, "thread_info failed");
		count = THREAD_SCHED_INFO_COUNT;
		err = thread_info(t2, THREAD_SCHED_INFO,
			(thread_info_t) &ths2, &count);
		ASSERT_RET(err, "thread_info failed");

		DEBUG_PRINT("(t1) base_priority %d\n", ths1.base_priority);
		DEBUG_PRINT("(t1) max_priority %d\n", ths1.max_priority);
		DEBUG_PRINT("(t2) base_priority %d\n", ths2.base_priority);
		DEBUG_PRINT("(t2) max_priority %d\n", ths2.max_priority);
		ASSERT(ths1.base_priority == new_max_priority,
			"(t1) expecting priority BASEPRI_USER - 5");
		ASSERT(ths1.max_priority == new_max_priority,
			"(t1) expecting priority BASEPRI_USER - 5");
		ASSERT(ths2.base_priority == new_max_priority,
			"(t2) expecting priority BASEPRI_USER - 5");
		ASSERT(ths2.max_priority == new_max_priority,
			"(t2) expecting priority BASEPRI_USER - 5");

		/* Clean up and exit*/
		err = thread_terminate(t1);
		ASSERT_RET(err, "thread_terminate failed");
		err = thread_terminate(t2);
		ASSERT_RET(err, "thread_terminate failed");

		err = task_terminate(new_task);
		ASSERT_RET(err, "task_terminate failed");
	}

	/*
	 *	Call the tests
	 */
	test_default_pset_max_priority();
	test_new_pset_max_priority();
	test_default_priorities();
	test_task_max_priority_thread_inheritance();
	test_task_priority_inheritance();
	test_task_priority_non_privileged();
	test_task_priority_privileged();
	test_pset_ceiling();
	test_updating_threads_max_priority();
#undef DEBUG_PRINT
#undef BASEPRI_USER
#undef BASEPRI_SYSTEM
}

int main(int argc, char *argv[], int envc, char *envp[])
{
  test_task();
  test_task_threads();
  test_new_task();
  test_errors();
  test_priority();
  return 0;
}
