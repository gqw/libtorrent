set curdir=%~dp0
cd %curdir%

set build_dir=build_win32
mkdir %build_dir%
cd %build_dir%

rem cmake .. -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake -Ddeprecated-functions=OFF -Dbuild_examples=ON -Dpython-bindings=ON -Dpython-install-system-dir=ON -Dpython-egg-info=ON -G "Visual Studio 15 2017 Win64"

cmake .. -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake -Ddeprecated-functions=OFF -Dbuild_examples=ON -G "Visual Studio 15 2017"
cd %curdir%