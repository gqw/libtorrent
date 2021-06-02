set curdir=%~dp0
cd %curdir%

mkdir build_win64
cd build_win64

cmake .. -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake -Ddeprecated-functions=OFF -Dbuild_examples=ON -Dpython-bindings=ON -Dpython-install-system-dir=ON -Dpython-egg-info=ON -G "Visual Studio 15 2017 Win64"
cd %curdir%