#include <stdio.h>
#include <stdlib.h>
#include <gtest/gtest.h>

#include "c11/threads.h"
#include "util/perf/u_trace.h"
#include "util/os_misc.h"

#define NUM_DEBUG_TEST_THREAD 8

static const char *trace_file_path =
   "tracefile_for_test-b5ba5a0c-6ed1-4901-a38d-755991182663";

/* u_trace reads its MESA_GPU_TRACE* options once, so the state is reset
 * around each test to let every case set its own.
 */
class UtilPerfTraceTest : public ::testing::Test {
protected:
   void SetUp() override
   {
      u_trace_state_reset();
   }

   void TearDown() override
   {
      u_trace_state_reset();
      remove(trace_file_path);
   }
};

static int
test_thread(void *_state)
{
   struct u_trace_context ctx = {};
   u_trace_context_init(&ctx, NULL, 8, 0, NULL, NULL, NULL,
                        NULL, NULL, NULL, NULL);
   u_trace_context_fini(&ctx);

   return 0;
}

TEST_F(UtilPerfTraceTest, Multithread)
{
   thrd_t threads[NUM_DEBUG_TEST_THREAD];
   os_set_option("MESA_GPU_TRACEFILE", trace_file_path, true);
   for (unsigned i = 0; i < NUM_DEBUG_TEST_THREAD; i++) {
        thrd_create(&threads[i], test_thread, NULL);
   }
   for (unsigned i = 0; i < NUM_DEBUG_TEST_THREAD; i++) {
      int ret;
      thrd_join(threads[i], &ret);
   }
}
