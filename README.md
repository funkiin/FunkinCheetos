# FunkinCheetos

Simple internal cheat for Friday Night Funkin' v0.8.6 (itch.io build)

## Cheetos

* All sicks

* No misses

* Forced PFC

## Building

```
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE=cmake/mingw64.cmake \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build -j$(nproc)
```
