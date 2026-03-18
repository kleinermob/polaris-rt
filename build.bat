@echo off
REM Build script for Polaris RT Layer

echo ============================================
echo Building Polaris RT Layer
echo ============================================

REM Find Vulkan SDK
set VULKAN_SDK=
if exist "C:\VulkanSDK\1.4.341.1" (
    set VULKAN_SDK=C:\VulkanSDK\1.4.341.1
) else if exist "C:\VulkanSDK\1.3.280.0" (
    set VULKAN_SDK=C:\VulkanSDK\1.3.280.0
) else if exist "C:\VulkanSDK\1.3.275.0" (
    set VULKAN_SDK=C:\VulkanSDK\1.3.275.0
) else if exist "C:\VulkanSDK\1.3.274.0" (
    set VULKAN_SDK=C:\VulkanSDK\1.3.274.0
) else if exist "C:\VulkanSDK\1.3.250.0" (
    set VULKAN_SDK=C:\VulkanSDK\1.3.250.0
) else (
    echo ERROR: Vulkan SDK not found!
    echo Please install Vulkan SDK from https://vulkan.lunarg.com/
    exit /b 1
)

echo Using Vulkan SDK: %VULKAN_SDK%

REM Set environment
set VULKAN_SDK_ROOT=%VULKAN_SDK%
set PATH=%VULKAN_SDK%\Bin;%PATH%

REM Create build directory
if not exist build mkdir build

cd build

REM Configure with CMake (Clang-CL toolset)
echo Configuring with Clang-CL...
cmake .. -G "Visual Studio 17 2022" -A x64 -T ClangCL ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DVulkan_ROOT=%VULKAN_SDK% ^
    -DCMAKE_INSTALL_PREFIX=install

if %ERRORLEVEL% NEQ 0 (
    echo CMake configuration failed!
    exit /b 1
)

REM Build with 14 parallel jobs
echo Building with 14 threads...
cmake --build . --config Release -- /maxcpucount:14

if %ERRORLEVEL% NEQ 0 (
    echo Build failed!
    exit /b 1
)

echo.
echo ============================================
echo Build Complete!
echo ============================================
echo.
echo Output files:
echo   Layer DLL:    bin\polaris_rt_layer.dll
echo   Layer JSON:   bin\polaris_rt_layer.json
echo   Test App:     bin\rt_test_app.exe
echo.
echo To run the test:
echo   cd bin
echo   set VK_LAYER_PATH=.
echo   rt_test_app.exe
echo.
