@echo off

REM 检查是否有参数传递给批处理文件

if "%1"=="gl" (
    set GL_TOKEN="GL"
    goto :set_file
)

if "%1"=="vulkan" (
    set VULKAN_TOKEN="VULKAN"
    goto :set_file
)

REM 获取第一个参数并输出

echo
echo "SNN configure script"
echo
echo "Usage: `basename %0` [gl] [vulkan]"
echo
echo "gl: Build with OpenGL support"
echo "vulkan: Build with Vulkan support"
echo

:set_file
echo %GL_TOKEN% %VULKAN_TOKEN% > config.txt

:eof