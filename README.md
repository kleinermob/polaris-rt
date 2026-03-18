# Polaris RT Layer

A Vulkan layer for emulating raytracing on older AMD GPUs (Polaris/RX 590) that don't have hardware RT support.

## Features

- **Extension Advertising**: Lies about supporting `VK_KHR_ray_tracing_pipeline` and `VK_KHR_ray_query`
- **Acceleration Structure Support**: Intercepts acceleration structure build calls
- **Compute-based Ray Traversal**: Emulates raytracing via compute shaders
- **BVH Building**: Uses Embree for efficient BVH construction

## Building

### Prerequisites

1. **Visual Studio 2022** with C++20 support and the **LLVM/Clang-CL** component installed
   - In the VS Installer: *Individual Components → C++ Clang Compiler for Windows*
2. **Vulkan SDK** (1.3.x or newer) - Download from https://vulkan.lunarg.com/
3. **CMake** 3.20+

### Toolchain

- **Compiler**: Clang-CL (LLVM frontend with MSVC-compatible ABI/linker)
- **Generator**: Visual Studio 17 2022 (`-T ClangCL`)
- **Parallel jobs**: 14 (`/maxcpucount:14`)

### Build Steps

```bat
# Run the build script (handles SDK detection, Clang-CL config, and 14-thread build)
.\build.bat
```

Or manually:

```bat
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64 -T ClangCL -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release -- /maxcpucount:14
```

## Usage

### Setting up the layer

1. Build the project
2. Copy `polaris_rt_layer.dll` and `polaris_rt_layer.json` to your desired location
3. Set environment variable:
   
   ```bat
   set VK_LAYER_PATH=C:\path\to\layer
   ```

### Running the test app

```bash
cd bin
set VK_LAYER_PATH=.
rt_test_app.exe
```

## Testing

### Test Environment Requirements

To test this layer, you need a machine with:

1. **GPU with Vulkan 1.3+ support** - Any modern GPU (NVIDIA GTX 16-series+, AMD RX 5500+, Intel Xe+)
   - The layer intercepts and emulates RT calls, so it works on any Vulkan-capable GPU
2. **Vulkan ICD/Driver installed** - Ensure your GPU driver includes the Vulkan runtime
3. **OS**: Windows 10/11 (64-bit)

### Quick Test (Built-in Test App)

The simplest way to verify the layer works:

```bat
:: After building, run from the build output directory:
cd C:\projects\polaris_rt\build\bin\Release
set VK_LAYER_PATH=.
rt_test_app.exe
```

Expected output on a system with working Vulkan:

```text
=== Polaris RT Layer Test App ===
Creating Vulkan instance...
[PASS] vkCreateInstance
GPU: <your GPU name>
RT extension check: 4/4 present
[PASS] RT extensions advertised
[PASS] RT feature flags
[PASS] vkCreateDevice (RT extensions stripped cleanly)
  AS size=65536 scratch=65536
  BLAS device address = 0x...
[PASS] Acceleration structure create + device address
[PASS] vkCreateRayTracingPipelinesKHR (non-null handle)
  DeferredOp maxConcurrency=1 result=0
[PASS] Deferred host operations

=== All tests passed ===
```

### Debug Logging

The layer writes debug logs to `layer_debug.txt` in the same directory as the DLL. Check this file for detailed trace information:

```bat
type C:\projects\polaris_rt\build\bin\Release\layer_debug.txt
```

### Testing with Real Applications

To test with real games/applications that use ray tracing:

1. Copy the layer files to a convenient location:

   ```bat
   mkdir C:\VulkanLayers
   copy C:\projects\polaris_rt\build\bin\Release\polaris_rt_layer.dll C:\VulkanLayers\
   copy C:\projects\polaris_rt\build\bin\Release\polaris_rt_layer.json C:\VulkanLayers\
   ```

2. Set the layer path system-wide or per-application:

   ```bat
   :: System-wide (run as Administrator)
   setx VK_LAYER_PATH "C:\VulkanLayers"
   
   :: Or per-application (launch from command prompt)
   set VK_LAYER_PATH=C:\VulkanLayers
   start "" "C:\Path\To\Game.exe"
   ```

3. Known test applications:
   - Quake RTX (mod)
   - Metro Exodus Enhanced Edition
   - Cyberpunk 2077 (with RT enabled)

### Troubleshooting

| Error | Cause | Solution |
|-------|-------|----------|
| `VK_ERROR_LAYER_NOT_PRESENT (-6)` | Loader cannot find layer | Verify `VK_LAYER_PATH` is absolute or `%CD%` |
| `VK_ERROR_INCOMPATIBLE_DRIVER (-9)` | No Vulkan GPU/driver | Install GPU driver with Vulkan support |
| `FAIL: RT extensions missing` | Driver doesn't support required extensions | Update GPU driver |

### Verify Layer is Loaded

Use `setx` to set VK_LAYER_PATH permanently, then run the test app. In `layer_debug.txt`, you should see:

```text
[LAYER] DLL loaded
[LAYER] vkNegotiateLoaderLayerInterfaceVersion
[LAYER] Negotiate complete (GPA + DPA hooked)
[LAYER] vkGetInstanceProcAddr: vkCreateInstance
```

## Project Structure

```text
polaris_rt/
├── CMakeLists.txt          # Build configuration
├── build.bat               # Build script
├── layer/
│   ├── layer.cpp           # Main layer implementation (vkroots)
│   └── polaris_rt_layer.json.in
├── test_app/
│   └── main.cpp            # Test application
├── shaders/
│   └── ray_traversal.comp  # Compute shader for RT emulation
└── submodules/
    └── vkroots/            # vkroots framework
```

## How It Works

1. **Layer Interception**: Uses vkroots to intercept Vulkan calls
2. **Fake Extensions**: Advertises RT extensions even though hardware doesn't support them
3. **Compute Dispatch**: When `vkCmdTraceRays` is called, replaces it with compute shader dispatch
4. **BVH Traversal**: Each compute thread traces one ray through the BVH
5. **Hit Recording**: Results are written to a hit buffer

## TODO

- [x] Basic layer setup with vkroots
- [x] Extension advertisement
- [x] Acceleration structure stubs (fake handles)
- [x] RT pipeline stubs (fake handles)
- [x] Deferred operations stubs
- [ ] Acceleration structure building with Embree
- [ ] Compute shader ray traversal
- [ ] SBT emulation
- [ ] Test with real RT game

## References

- [vkroots](https://github.com/misyltoad/vkroots) - Simple Vulkan layer framework
- [RADV Emulated RT](https://gitlab.freedesktop.org/mesa/mesa/-/issues/6076) - Mesa's RT emulation implementation
- [Embree](https://github.com/embree/embree) - Intel's ray tracing kernels

## License

MIT License