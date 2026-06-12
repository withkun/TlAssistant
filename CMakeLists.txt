cmake_minimum_required(VERSION 4.1)
project(tl_assistant LANGUAGES C CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 23)


################################################################################
# 使用当前日期设置版本号
if(NOT DEFINED TL_PROJECT_VERSION_MAJOR)
    string(TIMESTAMP DATE_TIME "%y-%m-%d %H:%M:%S")   # 25-08-03 08:49:05
    string(SUBSTRING ${DATE_TIME} 0 2 TL_PROJECT_VERSION_MAJOR)
    string(SUBSTRING ${DATE_TIME} 3 2 TL_PROJECT_VERSION_MINOR)
    string(SUBSTRING ${DATE_TIME} 6 2 TL_PROJECT_VERSION_PATCH)
    set(TL_PROJECT_VERSION_TWEAK 0)
endif()


################################################################################
set(CUDA_PATH d:/3rd_party/cuda_v13.3_lib)
set(CUDA_SDK_ROOT_DIR ${CUDA_PATH})
set(CMAKE_CUDA_COMPILER ${CUDA_PATH}/bin/nvcc.exe)
set(CMAKE_CUDA_ARCHITECTURES 86)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 20)

set(ONNX_RUNTIME_ROOT d:/3rd_party/ONNXRuntime-win-x64-gpu-1.26.0)

set(OpenCV_DIR d:/3rd_party/opencv_5.0.0)
include(${OpenCV_DIR}/OpenCVConfig.cmake)


################################################################################
add_subdirectory(deps/json-3.12.0)
add_subdirectory(deps/spdlog-1.17.0)
add_subdirectory(deps/yaml-cpp-0.9.0)
add_subdirectory(deps/zlib-1.3.2)

set(deps_libraries
    nlohmann_json spdlog::spdlog_header_only yaml-cpp zlibstatic
)


################################################################################
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

list(APPEND CMAKE_PREFIX_PATH $ENV{QT_PREFIX_PATH})
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Widgets Graphs QuickControls2)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Widgets Graphs QuickControls2)
find_package(Qt6 REQUIRED COMPONENTS LinguistTools)

set(TL_SOURCES
    config/app_config.h
    config/app_config.cpp
    config/tl_yaml_config.h
    config/tl_yaml_config.cpp
    tl_widgets/base64.h
    tl_widgets/numpy_utils.h
    tl_widgets/image_enhance.h
    tl_widgets/image_enhance.cpp
    tl_widgets/shape_to_json.h
    tl_widgets/shape_to_json.cpp
    tl_widgets/status_stats.h
    tl_widgets/status_stats.cpp
    tl_widgets/tl_canvas.h
    tl_widgets/tl_canvas.cpp
    tl_widgets/tl_label_list.h
    tl_widgets/tl_label_list.cpp
    tl_widgets/tl_label_dialog.h
    tl_widgets/tl_label_dialog.cpp
    tl_widgets/tl_label_file.h
    tl_widgets/tl_label_file.cpp
    tl_widgets/tl_tool_bar.h
    tl_widgets/tl_tool_bar.cpp
    tl_widgets/tl_utils.h
    tl_widgets/tl_utils.cpp
    tl_widgets/tl_shape.h
    tl_widgets/tl_shape.cpp
    tl_widgets/tl_file_dialog.h
    tl_widgets/tl_file_dialog.cpp
    tl_widgets/tl_shape_list.h
    tl_widgets/tl_shape_list.cpp
    tl_widgets/tl_brightness.h
    tl_widgets/tl_brightness.cpp
    tl_widgets/tl_file_watcher.h
    tl_widgets/tl_file_watcher.cpp
    tl_widgets/tl_train_widget.h
    tl_widgets/tl_train_widget.ui
    tl_widgets/tl_train_widget.cpp
    tl_widgets/zoom_widget.h
    tl_widgets/zoom_widget.cpp
)

set(AI_SOURCES
    tl_modules/base/blob.h
    tl_modules/base/blob.cpp
    tl_modules/base/types.h
    tl_modules/base/types.cpp
    tl_modules/base/model.h
    tl_modules/base/model.cpp
    tl_modules/base/sam_base.h
    tl_modules/base/sam_base.cpp
    tl_modules/base/ort_logging.h
    tl_modules/base/ort_logging.cpp
    tl_modules/base/format_cv.h
    tl_modules/base/format_qt.h

    tl_modules/sam1/tl_sam1.h
    tl_modules/sam1/tl_sam1.cpp
    tl_modules/sam2/tl_sam2.h
    tl_modules/sam2/tl_sam2.cpp
    tl_modules/efficientsam/tl_efficient_sam.h
    tl_modules/efficientsam/tl_efficient_sam.cpp

    tl_modules/base/tokenizer.h
    tl_modules/base/tokenizer.cpp
    tl_modules/yoloworld/tl_yolo_world.h
    tl_modules/yoloworld/tl_yolo_world.cpp
    tl_modules/sam3/tl_sam3.h
    tl_modules/sam3/tl_sam3.cpp

    tl_modules/sam_apis.h
    tl_modules/sam_apis.cpp
    tl_modules/sam_session.h
    tl_modules/sam_session.cpp

    tl_modules/info_button.h
    tl_modules/info_button.cpp
    tl_modules/ai_assist_thread.h
    tl_modules/ai_assist_thread.cpp
    tl_modules/ai_assist_annotation.h
    tl_modules/ai_assist_annotation.cpp
    tl_modules/ai_text_to_annotation.h
    tl_modules/ai_text_to_annotation.cpp
    tl_modules/bbox_from_text.h
    tl_modules/bbox_from_text.cpp
    tl_modules/polygon_from_mask.h
    tl_modules/polygon_from_mask.cpp
)

set(PROJECT_SOURCES
    mainwindow.h
    mainwindow.ui
    mainwindow.cpp
    main.cpp
)

qt_add_executable(${PROJECT_NAME} WIN32
    MANUAL_FINALIZATION

    ${TL_SOURCES}
    ${AI_SOURCES}

    ${PROJECT_SOURCES}
    resources.qrc
    application.rc
    versionInfo.rc
)
configure_file(version.rc.in ${PROJECT_BINARY_DIR}/versionInfo.rc)

qt_add_translations(${PROJECT_NAME} TS_FILES
    translate/zh_CN.ts
    translate/en_US.ts
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/tl_modules
    ${CMAKE_CURRENT_SOURCE_DIR}/tl_widgets
    ${CUDA_SDK_ROOT_DIR}/include
    ${ONNX_RUNTIME_ROOT}/include
    ${OpenCV_INCLUDE_DIRS}
    ${SPDLOG_INCLUDE_DIR}
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    WIN64 _WIN32_WINNT=0x0601
    ${OPENCV_DEFINITION}
    SPDLOG_USE_STD_FORMAT
    HAVE_QT QT_MESSAGELOGCONTEXT
)

target_compile_options(${PROJECT_NAME} PRIVATE
    /MP /wd4267 /UTF-8 /bigobj /Ob1
)

target_link_options(${PROJECT_NAME} PRIVATE
    /DEBUG /NOLOGO
)

target_link_directories(${PROJECT_NAME} PRIVATE
    ${CUDA_SDK_ROOT_DIR}/lib/x64
    ${ONNX_RUNTIME_ROOT}/lib
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Graphs
    Qt${QT_VERSION_MAJOR}::QuickControls2
    ${OpenCV_LIBS}
    ${deps_libraries}
    onnxruntime
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    PDB_OUTPUT_DIRECTORY     "${CMAKE_CURRENT_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build-relwithdebinfo"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build-relwithdebinfo"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build-relwithdebinfo"
)

#add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
#    COMMAND cmd /c echo SetIcon \"\$<TARGET_FILE:${PROJECT_NAME}>\",0 | icacls \"\$<TARGET_FILE:${PROJECT_NAME}>\" /grant Everyone:F >nul
#    COMMAND cmd /c mklink \"${CMAKE_CURRENT_SOURCE_DIR}/build-relwithdebinfo/瞳乐助手.exe\" \"$<TARGET_FILE:${PROJECT_NAME}>\"
#    COMMAND cmd /c icacls \"${CMAKE_CURRENT_SOURCE_DIR}/build-relwithdebinfo/瞳乐助手.exe\" /grant Everyone:F >nul
#    COMMENT "Creating Chinese shortcut"
#)

install(TARGETS ${PROJECT_NAME}
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

qt_finalize_executable(${PROJECT_NAME})