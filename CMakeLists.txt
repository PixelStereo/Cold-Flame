cmake_minimum_required(VERSION 3.21)
project(ColdFlame VERSION 0.11.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets Network Test)
find_package(ZLIB REQUIRED)
add_library(flame src/submasters.cpp src/colorpalettes.cpp src/colorcontrols.h src/fixtures.cpp src/engine.cpp src/engine.h src/network.cpp src/network.h src/gdtf.cpp src/gdtfgeometry.cpp src/gdtf.h)
target_link_libraries(flame PUBLIC Qt6::Core Qt6::Network ZLIB::ZLIB)
target_include_directories(flame PUBLIC src)
add_library(flame-ui src/fixtureeditor.cpp src/fixtureeditor.h src/colorpalettepanel.cpp src/colorpalettepanel.h src/fixturecolor.cpp src/fixturecolor.h src/parameterpanel.cpp src/parameterpanel.h src/patchdialog.cpp src/patchdialog.h src/blinddialog.cpp src/blinddialog.h src/outputpanel.cpp src/outputpanel.h src/tabledelegate.h src/numberdrag.h src/steptable.h src/cuelibrary.cpp src/cuelibrary.h)
target_link_libraries(flame-ui PUBLIC flame Qt6::Widgets)
qt_add_executable(cold-flame src/main.cpp)
target_link_libraries(cold-flame PRIVATE flame-ui Qt6::Widgets)
if(WIN32)
    set_target_properties(cold-flame PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
if(APPLE)
    set_target_properties(cold-flame PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.pixelstereo.coldflame"
        MACOSX_BUNDLE_BUNDLE_NAME "Cold Flame"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    )
endif()
enable_testing()
add_executable(engine-tests tests/engine_test.cpp)
target_link_libraries(engine-tests PRIVATE flame Qt6::Test)
add_test(NAME engine COMMAND engine-tests)
install(TARGETS cold-flame RUNTIME DESTINATION bin BUNDLE DESTINATION .)
add_executable(ui-tests tests/ui_test.cpp)
target_link_libraries(ui-tests PRIVATE flame-ui Qt6::Test Qt6::Widgets)
add_test(NAME interface COMMAND ${CMAKE_COMMAND}
    "-DTEST_EXECUTABLE=$<TARGET_FILE:ui-tests>"
    "-DTEST_LOG=${CMAKE_CURRENT_BINARY_DIR}/interface.log"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/scripts/run-qt-test.cmake")
set_tests_properties(interface PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
add_test(NAME interface-hidpi COMMAND ${CMAKE_COMMAND}
    "-DTEST_EXECUTABLE=$<TARGET_FILE:ui-tests>"
    "-DTEST_LOG=${CMAKE_CURRENT_BINARY_DIR}/interface-hidpi.log"
    "-DTEST_ARGUMENTS=channelGridRemainsStable;channelGridStaysPutDuringFade;tableEditorsAndSubmasterBank;contextualFooterAndNumberDragging;inlineTriggersFlagsAndDragValidation;liveChannelModesAndSelectedMembership;balancedButtonLayout;fixturePatchParameterEditorAndRecall;trackingViewEditingAndTrace;channelColorSwatchContextAndCancel;cueColorPaletteUpdatesKeepIntensityAndStepScope;directCuePaletteAssignmentDialog;updatingPartialLocalColorDoesNotLeakToSharedCue;paletteReferencesVisibleInCueAndStepLists;submasterBumpModesAndSettings;submasterCardActionsStayGroupedOnResize;submasterTimesAcceptTypedInput;fixtureDefinitionEditorModesValidationAndRoundtrip;fixtureDefinitionPatchDraftAndUsedProfileCopy;patchConflictResolutionPreservesOtherStartsAndCueData;batchPatchConflictIncludesFineByteAndKeepsOtherCopies;multicellParameterScopeColorAndCancel"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/scripts/run-qt-test.cmake")
set_tests_properties(interface-hidpi PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_SCALE_FACTOR=2")
add_test(NAME interface-windows-style COMMAND ${CMAKE_COMMAND}
    "-DTEST_EXECUTABLE=$<TARGET_FILE:ui-tests>"
    "-DTEST_LOG=${CMAKE_CURRENT_BINARY_DIR}/interface-windows-style.log"
    "-DTEST_ARGUMENTS=-style;windows;balancedButtonLayout;channelGridRemainsStable;channelGridStaysPutDuringFade;submasterCardActionsStayGroupedOnResize;submasterTimesAcceptTypedInput;fixtureDefinitionEditorModesValidationAndRoundtrip;fixtureDefinitionPatchDraftAndUsedProfileCopy;patchConflictResolutionPreservesOtherStartsAndCueData;batchPatchConflictIncludesFineByteAndKeepsOtherCopies;multicellParameterScopeColorAndCancel"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/scripts/run-qt-test.cmake")
set_tests_properties(interface-windows-style PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
if(UNIX)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME macos-build-script COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/build_script_test.py)
    endif()
endif()

include(cmake/Packaging.cmake)
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME release-packaging COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/package_test.py)
endif()
