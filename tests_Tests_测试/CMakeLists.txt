# ============================================================================
#  tests_Tests_测试/CMakeLists.txt
#  Unit tests for TCTools-Build_Web / TCTools-Build_Web 的单元测试
#
#  Every test is a Qt Test executable linked against the `tcbw_core` static
#  library, so the whole non-UI surface is testable without a display server.
#  每个测试都是一个 Qt Test 可执行文件，链接 `tcbw_core` 静态库，因此整个非界面
#  部分都可以在没有显示服务器的情况下测试。
# ============================================================================

set(TCBW_TEST_SOURCES
    tst_core_document.cpp
    tst_codegen.cpp
    tst_blocks.cpp
    tst_ai_providers.cpp
    tst_backend.cpp
)

#  Tests that are known to exercise optional subsystems are only built when the
#  sources they need are present, so a partial checkout still configures.
#  仅当所需源码存在时才构建相应测试，因此不完整的检出仍可完成配置。
set(TCBW_TEST_TARGETS "")

foreach(_test_source IN LISTS TCBW_TEST_SOURCES)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_test_source}")
        message(STATUS "Skipping missing test source: ${_test_source}")
        continue()
    endif()

    get_filename_component(_test_name "${_test_source}" NAME_WE)
    qt_add_executable(${_test_name} "${_test_source}")

    target_link_libraries(${_test_name} PRIVATE
        tcbw_core Qt6::Test Qt6::Core Qt6::Gui Qt6::Network)

    target_include_directories(${_test_name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

    if(MSVC)
        target_compile_options(${_test_name} PRIVATE /W3 /utf-8)
    else()
        target_compile_options(${_test_name} PRIVATE -Wall -Wextra)
    endif()

    add_test(NAME ${_test_name} COMMAND ${_test_name})
    set_tests_properties(${_test_name} PROPERTIES
        TIMEOUT 180
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    list(APPEND TCBW_TEST_TARGETS ${_test_name})
endforeach()

if(TCBW_TEST_TARGETS STREQUAL "")
    message(WARNING "No test sources found -- the test suite is empty. / 未找到测试源码 —— 测试套件为空。")
else()
    message(STATUS "TC-BuildWeb tests: ${TCBW_TEST_TARGETS}")
endif()
