makedir build
cmake -B build -G "Visual Studio 17 2022" ^
        -DOpenGL_GL_PREFERENCE=GLVND -DCMAKE_BUILD_TYPE=${build_type} 

cmake --build build