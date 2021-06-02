set curdir=%~dp0
cd %curdir%

set build_dir=build_win32_static
mkdir %build_dir%
cd %build_dir%

rem cmake .. -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake -Ddeprecated-functions=OFF -Dbuild_examples=ON -Dpython-bindings=ON -Dpython-install-system-dir=ON -Dpython-egg-info=ON -G "Visual Studio 15 2017"

cmake .. -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake -DOPENSSL_ROOT_DIR="%VCPKG_ROOT%\installed\x86-windows-static" -DBoost_INCLUDE_DIR="%VCPKG_ROOT%\installed\x86-windows-static\include" -Ddeprecated-functions=OFF -Dbuild_examples=ON -DBUILD_SHARED_LIBS=OFF -Dstatic_runtime=ON -G "Visual Studio 15 2017"
cd %curdir%