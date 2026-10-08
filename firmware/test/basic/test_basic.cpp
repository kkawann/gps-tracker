#include <Arduino.h>
#include <unity.h>

void test_math()
{
    TEST_ASSERT_EQUAL(4, 2 + 2);
}

void setup()
{
    UNITY_BEGIN();
    RUN_TEST(test_math);
    UNITY_END();
}

void loop() {}