#include "gtest/gtest.h"
#include "template_lib.h"

class YourTestClass : public ::testing::Test {
       protected:
	YourTestClass() {
        // initialize your stuff
	};

	~YourTestClass() override{};

	void SetUp() override{};

	void TearDown() override{};

    // your stuff
};

TEST_F(YourTestClass, YourTestName) {
    // your stuff
    int x = 3 + 2;
	EXPECT_EQ(x, 5); // a test sample
}

// see https://github.com/google/googletest/blob/master/googletest/docs/primer.md for samples
TEST_F(YourTestClass, AnotherTest) {
    static const int answer = answer_to_everything();
;
    int x = 40 + 2;
	EXPECT_EQ(x, answer); 
}

