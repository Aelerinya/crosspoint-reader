add_executable(BookSenderTest
  BookSenderTest.cpp
  ../../lib/BookSender/BookSender.cpp
)

target_include_directories(BookSenderTest PRIVATE ../../lib/BookSender)

target_link_libraries(BookSenderTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BookSenderTest)
