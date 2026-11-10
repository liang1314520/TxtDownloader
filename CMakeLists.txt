cmake_minimum_required(VERSION 3.21)
project(NovDecoder VERSION 1.0.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "NovDecoder 需要 Windows BCrypt API。")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Network)
qt_standard_project_setup()

if(MSVC)
    add_compile_options(/utf-8 /W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_library(novdecoder_core STATIC
    src/novdecoder.cpp src/novdecoder.h
    src/aesecb.cpp src/aesecb.h)
target_include_directories(novdecoder_core PUBLIC src)
target_compile_definitions(novdecoder_core PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(novdecoder_core PUBLIC Qt6::Core PRIVATE bcrypt)

add_library(novdecoder_network STATIC
    src/chapterdownload.cpp src/chapterdownload.h
    src/novelapi.cpp src/novelapi.h
    src/novelcategory.cpp src/novelcategory.h
    src/localkeyprofile.cpp src/localkeyprofile.h
    src/sessionprofile.cpp src/sessionprofile.h
    src/pcaprequestreader.cpp src/pcaprequestreader.h
    src/mumucapture.cpp src/mumucapture.h
    src/mumudiscovery.cpp src/mumudiscovery.h
    src/bookdownloadcontroller.cpp src/bookdownloadcontroller.h
    src/booklistcache.cpp src/booklistcache.h)
target_include_directories(novdecoder_network PUBLIC src)
target_compile_definitions(novdecoder_network PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(novdecoder_network PUBLIC novdecoder_core Qt6::Network PRIVATE crypt32)

add_library(novdecoder_video STATIC
    src/videoapitypes.cpp src/videoapitypes.h
    src/videostore.cpp src/videostore.h
    src/videocover.cpp src/videocover.h
    src/videocatalogcontroller.cpp src/videocatalogcontroller.h
    src/videoclient.cpp src/videoclient.h)
target_include_directories(novdecoder_video PUBLIC src)
target_compile_definitions(novdecoder_video PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(novdecoder_video PUBLIC Qt6::Core Qt6::Gui Qt6::Network PRIVATE bcrypt crypt32)

add_library(novdecoder_widgets STATIC
    src/workbenchtheme.cpp src/workbenchtheme.h
    src/mainwindow.cpp src/mainwindow.h
    src/bookdownloadwidget.cpp src/bookdownloadwidget.h
    src/bookdetaildialog.cpp src/bookdetaildialog.h
    src/mumucapturedialog.cpp src/mumucapturedialog.h
    src/videolistmodel.cpp src/videolistmodel.h
    src/videocatalogwidget.cpp src/videocatalogwidget.h)
target_include_directories(novdecoder_widgets PUBLIC src)
target_link_libraries(novdecoder_widgets PUBLIC novdecoder_network novdecoder_video Qt6::Widgets)

qt_add_executable(NovDecoder WIN32 src/main.cpp)
target_link_libraries(NovDecoder PRIVATE novdecoder_widgets)
install(TARGETS NovDecoder RUNTIME DESTINATION .)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(tst_novdecoder tests/tst_novdecoder.cpp)
    target_link_libraries(tst_novdecoder PRIVATE novdecoder_core Qt6::Test)
    add_test(NAME novdecoder COMMAND tst_novdecoder -o -,txt)

    qt_add_executable(tst_mainwindow tests/tst_mainwindow.cpp)
    target_link_libraries(tst_mainwindow PRIVATE novdecoder_widgets Qt6::Test)
    add_test(NAME mainwindow COMMAND tst_mainwindow -o -,txt)

    qt_add_executable(tst_videoapi tests/tst_videoapi.cpp)
    target_link_libraries(tst_videoapi PRIVATE novdecoder_video Qt6::Test bcrypt)
    target_compile_definitions(tst_videoapi PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    add_test(NAME videoapi COMMAND tst_videoapi -o -,txt)

    qt_add_executable(tst_videocatalog tests/tst_videocatalog.cpp)
    target_link_libraries(tst_videocatalog PRIVATE novdecoder_widgets novdecoder_video Qt6::Test)
    add_test(NAME videocatalog COMMAND tst_videocatalog -o -,txt)
endif()
