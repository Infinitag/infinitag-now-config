// Unit tests for the battery level state machine (BatteryMonitor.h).
// Run on the PC:  pio test -e native

#include <unity.h>
#include "BatteryMonitor.h"

using battery::Monitor;
using battery::Thresholds;

// same numbers as cfg:: in Config.h (4xAA alkaline)
static const Thresholds T{3.0f, 5.3f, 5.2f, 0.1f, 2};

void test_fresh_pack_is_fine() {
  Monitor m(T);
  TEST_ASSERT_EQUAL_UINT8(0, m.update(5.78f));
  TEST_ASSERT_EQUAL_UINT8(0, m.update(5.70f));
}

void test_usb_powered_never_warns() {
  Monitor m(T);
  TEST_ASSERT_EQUAL_UINT8(0, m.update(0.4f));
  TEST_ASSERT_EQUAL_UINT8(0, m.update(2.9f));
}

void test_single_sagged_sample_is_ignored() {
  // one reading dipping under the threshold (radio burst) -> no change
  Monitor m(T);
  m.update(5.75f);
  TEST_ASSERT_EQUAL_UINT8(0, m.update(5.25f));
  TEST_ASSERT_EQUAL_UINT8(0, m.update(5.75f));
  TEST_ASSERT_EQUAL_UINT8(0, m.update(5.20f));  // run restarted
  TEST_ASSERT_EQUAL_UINT8(0, m.update(5.75f));
}

void test_two_low_samples_step_down() {
  Monitor m(T);
  TEST_ASSERT_EQUAL_UINT8(0, m.update(5.28f));
  TEST_ASSERT_EQUAL_UINT8(1, m.update(5.27f));  // warn
  TEST_ASSERT_EQUAL_UINT8(1, m.update(5.15f));
  TEST_ASSERT_EQUAL_UINT8(2, m.update(5.12f));  // replace (observed 5.07-5.17)
}

void test_replace_reached_directly_from_fine() {
  // the box boots with a dead pack: two samples straight to level 2
  Monitor m(T);
  TEST_ASSERT_EQUAL_UINT8(0, m.update(5.10f));
  TEST_ASSERT_EQUAL_UINT8(2, m.update(5.08f));
}

void test_hysteresis_on_the_way_up() {
  Monitor m(T);
  m.update(5.10f);
  m.update(5.10f);
  TEST_ASSERT_EQUAL_UINT8(2, m.level());
  TEST_ASSERT_EQUAL_UINT8(2, m.update(5.25f));  // inside the band: stays
  TEST_ASSERT_EQUAL_UINT8(1, m.update(5.31f));  // > 5.2 + 0.1: one step
  TEST_ASSERT_EQUAL_UINT8(1, m.update(5.38f));  // inside the band: stays
  TEST_ASSERT_EQUAL_UINT8(0, m.update(5.45f));  // > 5.3 + 0.1
}

void test_no_flicker_at_the_threshold() {
  // readings scatter +-0.05 V around 5.3 V: must settle, not toggle
  Monitor m(T);
  const float seq[] = {5.28f, 5.32f, 5.27f, 5.33f, 5.29f, 5.31f, 5.28f, 5.34f};
  uint8_t changes = 0;
  uint8_t last = m.level();
  for (float v : seq) {
    const uint8_t l = m.update(v);
    if (l != last) changes++;
    last = l;
  }
  TEST_ASSERT_TRUE(changes <= 1);
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_fresh_pack_is_fine);
  RUN_TEST(test_usb_powered_never_warns);
  RUN_TEST(test_single_sagged_sample_is_ignored);
  RUN_TEST(test_two_low_samples_step_down);
  RUN_TEST(test_replace_reached_directly_from_fine);
  RUN_TEST(test_hysteresis_on_the_way_up);
  RUN_TEST(test_no_flicker_at_the_threshold);
  return UNITY_END();
}
