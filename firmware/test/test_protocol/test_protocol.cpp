#include <unity.h>
#include "../../src/protocol.h"

void setUp() {}
void tearDown() {}

static Command parsed(const char* line, bool expectOk = true) {
  Command c{};
  TEST_ASSERT_EQUAL(expectOk, parse(line, strlen(line), c));
  return c;
}

void test_hello() { TEST_ASSERT_EQUAL(Command::Hello, parsed("hello").type); }

void test_profile() {
  TEST_ASSERT_EQUAL(Command::Profile, parsed("profile\n").type);
  parsed("profile extra", false);
}

void test_move() {
  Command c = parsed("move 10 -5\n");
  TEST_ASSERT_EQUAL(Command::Move, c.type);
  TEST_ASSERT_EQUAL(10, c.a);
  TEST_ASSERT_EQUAL(-5, c.b);
}

void test_wheel() {
  Command c = parsed("wheel -3");
  TEST_ASSERT_EQUAL(Command::Wheel, c.type);
  TEST_ASSERT_EQUAL(-3, c.a);
}

void test_click() {
  TEST_ASSERT_EQUAL(1, parsed("click left").a);
  TEST_ASSERT_EQUAL(2, parsed("click right").a);
  TEST_ASSERT_EQUAL(4, parsed("click middle\r\n").a);
}

void test_rejects_garbage() {
  parsed("", false);
  parsed("jump 1 2", false);
  parsed("move 1", false);           // missing arg
  parsed("move 1 2 3", false);       // extra arg
  parsed("move a b", false);         // not numbers
  parsed("move 1x 2", false);        // trailing junk in number
  parsed("move 99999 0", false);     // out of range
  parsed("wheel", false);
  parsed("click up", false);
  parsed("hello there", false);
}

void test_split_large_delta() {
  int remaining = 1000, steps = 0, sum = 0;
  while (remaining) {
    int s = nextStep(remaining);
    TEST_ASSERT_TRUE(s >= -127 && s <= 127);
    remaining -= s; sum += s; steps++;
  }
  TEST_ASSERT_EQUAL(1000, sum);
  TEST_ASSERT_EQUAL(8, steps);
  TEST_ASSERT_EQUAL(-127, nextStep(-300));
  TEST_ASSERT_EQUAL(5, nextStep(5));
}

void test_rejects_incomplete_or_nontext_commands() {
  Command command = {Command::Profile, 5, 6};
  const char embeddedNull[] = "hello\0extra";
  TEST_ASSERT_FALSE(parse(embeddedNull, sizeof embeddedNull - 1, command));
  char oversized[64];
  memset(oversized, ' ', sizeof oversized);
  memcpy(oversized, "hello", 5);
  TEST_ASSERT_FALSE(parse(oversized, sizeof oversized, command));
  TEST_ASSERT_FALSE(parse("move 1 x", 8, command));
  TEST_ASSERT_EQUAL(Command::Profile, command.type);
  TEST_ASSERT_EQUAL(5, command.a);
  TEST_ASSERT_EQUAL(6, command.b);
  TEST_ASSERT_TRUE(parse(oversized, sizeof oversized - 1, command));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_hello);
  RUN_TEST(test_profile);
  RUN_TEST(test_move);
  RUN_TEST(test_wheel);
  RUN_TEST(test_click);
  RUN_TEST(test_rejects_garbage);
  RUN_TEST(test_rejects_incomplete_or_nontext_commands);
  RUN_TEST(test_split_large_delta);
  return UNITY_END();
}
