cd ${BUILD_DIR} && \
cmake ../ -DOpenGL_GL_PREFERENCE=GLVND -DCMAKE_BUILD_TYPE=${build_type} && \
make -j8 install