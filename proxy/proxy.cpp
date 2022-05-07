#include "proxy.h"

#include <GL/gl.h>
#include <dlfcn.h>
#include <stdlib.h>

#include <iostream>

void hello() {
    std::cout << "Hello, World!" << std::endl;
}

void glClear(GLbitfield mask){
    static void (*lib_glClear)(GLbitfield) = NULL;
    void* handle;
    char* errorstr;

    if(!lib_glClear) {
        /* Load real libGL */
        handle = dlopen("/usr/lib/x86_64-linux-gnu/libGL.so", RTLD_LAZY);
        if(!handle) {
            fputs(dlerror(), stderr);
            exit(1);
        }
        /* Fetch pointer of real glClear() func */
        lib_glClear = (void(*)(GLbitfield)) dlsym(handle, "glClear");
        if( (errorstr = dlerror()) != NULL ) {
            fprintf(stderr, "dlsym fail: %s\n", errorstr);
            exit(1);
        }
    }
    hello();
    lib_glClear(mask);
}