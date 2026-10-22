// Unit tests for the WLAN scan -> JSON helper of the SoftAP WLAN page.
// Run on the PC:  pio test -e native

#include <unity.h>
#include <string.h>
#include "WifiScanJson.h"

using wifiscan::Entry;
using wifiscan::toJson;

void test_sorted_strongest_first() {
  const Entry in[] = {{"Keller", -80, true}, {"Vorgarten", -40, true},
                      {"Nachbar", -60, false}};
  char out[256];
  TEST_ASSERT_EQUAL_size_t(3, toJson(in, 3, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING(
      "[{\"ssid\":\"Vorgarten\",\"rssi\":-40,\"enc\":1},"
      "{\"ssid\":\"Nachbar\",\"rssi\":-60,\"enc\":0},"
      "{\"ssid\":\"Keller\",\"rssi\":-80,\"enc\":1}]",
      out);
}

void test_hidden_networks_dropped() {
  const Entry in[] = {{"", -30, true}, {nullptr, -20, true}, {"Haus", -50, true}};
  char out[128];
  TEST_ASSERT_EQUAL_size_t(1, toJson(in, 3, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("[{\"ssid\":\"Haus\",\"rssi\":-50,\"enc\":1}]", out);
}

void test_duplicates_keep_strongest() {
  // mesh / repeater: same SSID several times -> one entry, best RSSI
  const Entry in[] = {{"Mesh", -70, true}, {"Mesh", -45, true}, {"Mesh", -45, true},
                      {"Gast", -55, false}};
  char out[256];
  TEST_ASSERT_EQUAL_size_t(2, toJson(in, 4, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING(
      "[{\"ssid\":\"Mesh\",\"rssi\":-45,\"enc\":1},"
      "{\"ssid\":\"Gast\",\"rssi\":-55,\"enc\":0}]",
      out);
}

void test_ssid_is_json_escaped() {
  const Entry in[] = {{"Tobi\"s \\ WLAN\n", -50, true}, {"Caf\xC3\xA9", -60, false}};
  char out[256];
  TEST_ASSERT_EQUAL_size_t(2, toJson(in, 2, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING(
      "[{\"ssid\":\"Tobi\\\"s \\\\ WLAN\\u000a\",\"rssi\":-50,\"enc\":1},"
      "{\"ssid\":\"Caf\xC3\xA9\",\"rssi\":-60,\"enc\":0}]",
      out);
}

void test_empty_scan() {
  char out[16];
  TEST_ASSERT_EQUAL_size_t(0, toJson(nullptr, 0, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("[]", out);
}

void test_truncation_drops_whole_entries() {
  const Entry in[] = {{"Eins", -40, true}, {"Zwei", -50, true}, {"Drei", -60, true}};
  char out[90];  // room for two entries, not three
  const size_t n = toJson(in, 3, out, sizeof(out));
  TEST_ASSERT_EQUAL_size_t(2, n);
  TEST_ASSERT_EQUAL_STRING(
      "[{\"ssid\":\"Eins\",\"rssi\":-40,\"enc\":1},"
      "{\"ssid\":\"Zwei\",\"rssi\":-50,\"enc\":1}]",
      out);
  TEST_ASSERT_TRUE(strlen(out) < sizeof(out));
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_sorted_strongest_first);
  RUN_TEST(test_hidden_networks_dropped);
  RUN_TEST(test_duplicates_keep_strongest);
  RUN_TEST(test_ssid_is_json_escaped);
  RUN_TEST(test_empty_scan);
  RUN_TEST(test_truncation_drops_whole_entries);
  return UNITY_END();
}
