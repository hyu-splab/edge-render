#include <zmq.hpp>
#include <string>
#include <iostream>
// #include <GL/glew.h>
#include "glremote/glremote/glremote_client.h"
#include "glremote/gl_commands.h"

#define GL_SET_COMMAND(PTR, FUNCNAME) gl_##FUNCNAME##_t *PTR = (gl_##FUNCNAME##_t *)malloc(sizeof(gl_##FUNCNAME##_t)); PTR->cmd = GLSC_##FUNCNAME

#define WIDTH 1024
#define HEIGHT 768

const char* vertexShaderSource = 
    "#version 330 core\n"
    "in vec3 positionAttribute;"
    "in vec3 colorAttribute;"
    "out vec3 passColorAttribute;"
    "void main()"
    "{"
    "gl_Position = vec4(positionAttribute, 1.0);"
    "passColorAttribute = colorAttribute;"
    "}";

const char* fragmentShaderSource =
    "#version 330 core\n"
    "in vec3 passColorAttribute;"
    "out vec4 fragmentColor;"
    "void main()"
    "{"
    "fragmentColor = vec4(passColorAttribute, 1.0);"
    "}";

float position[] = {
		0.0f,  0.5f, 0.0f, //vertex 1  위 중앙
		0.5f, -0.5f, 0.0f, //vertex 2  오른쪽 아래
		-0.5f, -0.5f, 0.0f //vertex 3  왼쪽 아래
	};

float color[] = {
    1.0f, 0.0f, 0.0f, //vertex 1 : RED (1,0,0)
    0.0f, 1.0f, 0.0f, //vertex 2 : GREEN (0,1,0) 
    0.0f, 0.0f, 1.0f  //vertex 3 : BLUE (0,0,1)
};

int main(int argc, char* argv[]) {  
    zmq::context_t ctx2;
    zmq::socket_t sock2(ctx2, zmq::socket_type::req);

    sock2.connect("tcp://127.0.0.1:12345");
    // sock2.connect("tcp://" + std::string(argv[1]) + ":" + std::string(argv[2]));

    // shader initialization
    while  (1){
    float ratio = WIDTH / (float) HEIGHT;
    glViewport(0, 0, WIDTH, HEIGHT);
    glClear(GL_COLOR_BUFFER_BIT);
    // glMatrixMode(GL_PROJECTION);
    // glLoadIdentity();
    // glOrtho(-ratio, ratio, -1.f, 1.f, 1.f, -1.f);
    // glMatrixMode(GL_MODELVIEW);
    // glLoadIdentity();
    // glRotatef(0.f, 0.f, 0.f, 1.f);
    glBegin(GL_TRIANGLES);
    glColor3f(1.f, 0.f, 0.f);
    glVertex3f(-0.6f, -0.4f, 0.f);
    glColor3f(0.f, 1.f, 0.f);
    glVertex3f(0.6f, -0.4f, 0.f);
    glColor3f(0.f, 0.f, 1.f);
    glVertex3f(0.f, 0.6f, 0.f);
    glEnd();
    glSwapBuffer();
    }
        return 0;
}

