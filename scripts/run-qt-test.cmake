# Qt's GUI test runner may not have a usable stdout handle on Windows.
# Capture its text report explicitly and forward it through CTest on every OS.
file(REMOVE "${TEST_LOG}")
execute_process(
    COMMAND "${TEST_EXECUTABLE}" ${TEST_ARGUMENTS} -o "${TEST_LOG},txt"
    RESULT_VARIABLE result
)
if(EXISTS "${TEST_LOG}")
    file(READ "${TEST_LOG}" report)
    message("${report}")
endif()
if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "Qt test runner failed: ${result}")
endif()
