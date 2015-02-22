/*
 *  COPYRIGHT (c) 1989-2012.
 *  On-Line Applications Research Corporation (OAR).
 *
 *  The license and distribution terms for this file may be
 *  found in the file LICENSE in this distribution or at
 *  http://www.rtems.org/license/LICENSE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <tmacros.h>
#include <intrcritical.h>

#include <rtems/rtems/semimpl.h>

/* forward declarations to avoid warnings */
rtems_task Init(rtems_task_argument argument);
rtems_timer_service_routine test_release_from_isr(rtems_id  timer, void *arg);
Thread_blocking_operation_States getState(void);

#if defined(FIFO_NO_TIMEOUT)
  #define TEST_NAME                "1"
  #define TEST_STRING              "FIFO/Without Timeout"
  #define SEMAPHORE_OBTAIN_TIMEOUT 0
  #define SEMAPHORE_ATTRIBUTES     RTEMS_DEFAULT_ATTRIBUTES

#elif defined(FIFO_WITH_TIMEOUT)
  #define TEST_NAME                "2"
  #define TEST_STRING              "FIFO/With Timeout"
  #define SEMAPHORE_OBTAIN_TIMEOUT 10
  #define SEMAPHORE_ATTRIBUTES     RTEMS_DEFAULT_ATTRIBUTES

#elif defined(PRIORITY_NO_TIMEOUT)
  #define TEST_NAME                "3"
  #define TEST_STRING              "Priority/Without Timeout"
  #define SEMAPHORE_OBTAIN_TIMEOUT 0
  #define SEMAPHORE_ATTRIBUTES     RTEMS_PRIORITY

#elif defined(PRIORITY_WITH_TIMEOUT)
  #define TEST_NAME                "4"
  #define TEST_STRING              "Priority/With Timeout"
  #define SEMAPHORE_OBTAIN_TIMEOUT 10
  #define SEMAPHORE_ATTRIBUTES     RTEMS_PRIORITY

#elif defined(PRIORITY_NO_TIMEOUT_REVERSE)
  #define TEST_NAME                "5"
  #define TEST_STRING              "Priority/Without Timeout (Reverse)"
  #define SEMAPHORE_OBTAIN_TIMEOUT 0
  #define SEMAPHORE_ATTRIBUTES     RTEMS_PRIORITY

#else

  #error "Test Mode not defined"
#endif

const char rtems_test_name[] = "SPINTRCRITICAL " TEST_NAME;

rtems_id Semaphore;
volatile bool case_hit = false;

Thread_blocking_operation_States getState(void)
{
  Objects_Locations  location;
  Semaphore_Control *sem;

  sem = (Semaphore_Control *)_Objects_Get(
    &_Semaphore_Information, Semaphore, &location );
  if ( location != OBJECTS_LOCAL ) {
    puts( "Bad object lookup" );
    rtems_test_exit(0);
  }
  _Thread_Unnest_dispatch();

  return sem->Core_control.semaphore.Wait_queue.sync_state;
}

rtems_timer_service_routine test_release_from_isr(
  rtems_id  timer,
  void     *arg
)
{
  rtems_status_code     status;

  if ( getState() == THREAD_BLOCKING_OPERATION_NOTHING_HAPPENED ) {
    case_hit = true;
  }

  status = rtems_semaphore_release( Semaphore );
  directive_failed_with_level( status, "release", -1 );
}


static bool test_body( void *arg )
{
  rtems_status_code status;

  (void) arg;

  status = rtems_semaphore_obtain(
    Semaphore,
    RTEMS_DEFAULT_OPTIONS,
    SEMAPHORE_OBTAIN_TIMEOUT
  );
  directive_failed( status, "rtems_semaphore_obtain" );

  return case_hit;
}

rtems_task Init(
  rtems_task_argument ignored
)
{
  rtems_status_code     status;

  TEST_BEGIN();

  puts( "Init - Trying to generate semaphore release from ISR while blocking" );
  puts( "Init - Variation is: " TEST_STRING );
  status = rtems_semaphore_create(
    rtems_build_name( 'S', 'M', '1', ' ' ),
    0,
    SEMAPHORE_ATTRIBUTES,
    RTEMS_NO_PRIORITY,
    &Semaphore
  );
  directive_failed( status, "rtems_semaphore_create of SM1" );

  interrupt_critical_section_test( test_body, NULL, test_release_from_isr );

  if ( case_hit ) {
    puts( "Init - Case hit" );
    TEST_END();
  } else
    puts( "Init - Case not hit - ran too long" );

  rtems_test_exit(0);
}

/* configuration information */

#define CONFIGURE_APPLICATION_NEEDS_CONSOLE_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_CLOCK_DRIVER

#define CONFIGURE_MAXIMUM_TASKS       1
#define CONFIGURE_MAXIMUM_TIMERS      1
#define CONFIGURE_MAXIMUM_SEMAPHORES  1
#define CONFIGURE_MAXIMUM_USER_EXTENSIONS 1
#define CONFIGURE_MICROSECONDS_PER_TICK  1000
#if defined(PRIORITY_NO_TIMEOUT_REVERSE)
  #define CONFIGURE_INIT_TASK_PRIORITY   250
#endif
#define CONFIGURE_MICROSECONDS_PER_TICK  1000
#define CONFIGURE_INITIAL_EXTENSIONS RTEMS_TEST_INITIAL_EXTENSION

#define CONFIGURE_RTEMS_INIT_TASKS_TABLE

#define CONFIGURE_INIT
#include <rtems/confdefs.h>

/* global variables */
