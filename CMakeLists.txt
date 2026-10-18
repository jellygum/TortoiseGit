cmake_minimum_required(VERSION 3.25)
project(TortoiseGitQt VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
if(APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "14.4" CACHE STRING "Minimum macOS version")
endif()

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets)
include(CTest)

add_library(tgit_domain STATIC src/portable/domain/RepositoryStatus.cpp)
target_include_directories(tgit_domain PUBLIC src/portable/domain)
set_target_properties(tgit_domain PROPERTIES AUTOMOC OFF)

add_library(tgit_git STATIC src/portable/adapters/GitClient.cpp)
target_include_directories(tgit_git PUBLIC src/portable/adapters)
target_link_libraries(tgit_git PUBLIC Qt6::Core tgit_domain)

add_library(tgit_ui STATIC src/qt/MainWindow.cpp src/qt/StatusModel.cpp
  src/qt/DiffHighlighter.cpp src/qt/OperationDialog.cpp)
target_include_directories(tgit_ui PUBLIC src/qt)
target_link_libraries(tgit_ui PUBLIC tgit_git Qt6::Widgets)
qt_add_executable(TortoiseGitQt MACOSX_BUNDLE src/qt/main.cpp)
target_link_libraries(TortoiseGitQt PRIVATE tgit_ui)
set_target_properties(TortoiseGitQt PROPERTIES
  MACOSX_BUNDLE_BUNDLE_NAME "TortoiseGit Qt"
  MACOSX_BUNDLE_GUI_IDENTIFIER "org.tortoisegit.qt.preview"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
  MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")

qt_add_executable(TortoiseGitAskPass src/qt/AskPass.cpp)
target_link_libraries(TortoiseGitAskPass PRIVATE Qt6::Widgets)
add_dependencies(TortoiseGitQt TortoiseGitAskPass)
if(APPLE)
  add_custom_command(TARGET TortoiseGitQt POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:TortoiseGitAskPass>"
      "$<TARGET_FILE_DIR:TortoiseGitQt>/TortoiseGitAskPass")
endif()

if(BUILD_TESTING)
  find_package(Qt6 REQUIRED COMPONENTS Test)
  add_executable(tgit_domain_tests test/portable/RepositoryStatusTest.cpp)
  target_link_libraries(tgit_domain_tests PRIVATE tgit_domain)
  set_target_properties(tgit_domain_tests PROPERTIES AUTOMOC OFF)
  add_test(NAME status_parser COMMAND tgit_domain_tests)
  qt_add_executable(tgit_git_tests test/qt/GitClientTest.cpp)
  target_link_libraries(tgit_git_tests PRIVATE tgit_git Qt6::Test)
  add_test(NAME git_integration COMMAND tgit_git_tests)
  qt_add_executable(tgit_ui_tests test/qt/MainWindowTest.cpp)
  target_link_libraries(tgit_ui_tests PRIVATE tgit_ui Qt6::Test)
  add_test(NAME qt_workflow COMMAND tgit_ui_tests)
  set_tests_properties(qt_workflow PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

install(TARGETS TortoiseGitQt BUNDLE DESTINATION . RUNTIME DESTINATION bin)
if(APPLE)
  install(PROGRAMS "$<TARGET_FILE:TortoiseGitAskPass>"
    DESTINATION "TortoiseGitQt.app/Contents/MacOS")
else()
  install(TARGETS TortoiseGitAskPass RUNTIME DESTINATION bin)
endif()
if(APPLE)
  set(tgit_deploy_options "-executable=TortoiseGitQt.app/Contents/MacOS/TortoiseGitAskPass")
  install(FILES LICENSE doc/qt/DEPENDENCIES.md
    DESTINATION "TortoiseGitQt.app/Contents/Resources/licenses")
endif()
qt_generate_deploy_app_script(TARGET TortoiseGitQt OUTPUT_SCRIPT deploy_script
  NO_UNSUPPORTED_PLATFORM_ERROR DEPLOY_TOOL_OPTIONS ${tgit_deploy_options})
install(SCRIPT ${deploy_script})

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  foreach(target tgit_domain tgit_git tgit_ui TortoiseGitQt TortoiseGitAskPass)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endforeach()
endif()
