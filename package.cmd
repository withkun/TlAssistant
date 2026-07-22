@ECHO OFF

::QT程序打包
mkdir Assistant

::快捷方式脚本
COPY shortcut.cmd  Assistant\创建快捷方式.cmd


::目标程序
COPY build-relwithdebinfo\tl_assistant.exe                                              Assistant

::配置相关
COPY c:\WORK\TlAssistant\build-relwithdebinfo\labelmerc.yaml                            Assistant
COPY c:\WORK\TlAssistant\build-relwithdebinfo\app_config.json                           Assistant
COPY c:\WORK\TlAssistant\build-relwithdebinfo\bpe_simple_vocab_16e6.txt.gz              Assistant

::依赖库文件
COPY d:\3rd_party\opencv_5.0.0\x64\vc16\bin\opencv_world500.dll                         Assistant

COPY d:\3rd_party\ONNXRuntime-win-x64-gpu-1.27.1\lib\onnxruntime.dll                    Assistant
COPY d:\3rd_party\ONNXRuntime-win-x64-gpu-1.27.1\lib\onnxruntime_providers_cuda.dll     Assistant
COPY d:\3rd_party\ONNXRuntime-win-x64-gpu-1.27.1\lib\onnxruntime_providers_shared.dll   Assistant


::应该程序打包
c:\Qt6\6.11.0\msvc2022_64\bin\windeployqt.exe --qmldir c:\Qt6\6.11.0\msvc2022_64\qml    Assistant\tl_assistant.exe


::设置库隐藏
attrib +H Assistant\Qt6*.dll
pause
