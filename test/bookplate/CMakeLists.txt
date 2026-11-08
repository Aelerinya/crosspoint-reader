add_executable(BookplateTest
  BookplateTest.cpp
  ${REPO_ROOT}/lib/Bookplate/Bookplate.cpp
  ${REPO_ROOT}/lib/miniz/src/InflateStream.cpp
  ${REPO_ROOT}/lib/miniz/src/miniz_impl.c
  ${REPO_ROOT}/lib/Memory/BuildScratch.cpp
  ${REPO_ROOT}/lib/hal/HalMemory.cpp
)
target_include_directories(BookplateTest PRIVATE
  ${REPO_ROOT}/test/zip_file/stubs
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/hal
  ${REPO_ROOT}/lib/Bookplate
  ${REPO_ROOT}/lib/BookSender
  ${REPO_ROOT}/lib/miniz/src
)
target_compile_definitions(BookplateTest PRIVATE TEST_EPUB_DIR="${REPO_ROOT}/test/epubs")
target_link_libraries(BookplateTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BookplateTest)
