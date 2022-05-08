#include "glremote_server.h"
#include <vector>

#define FRAME_BUFFER_ENABLE 0
#define SEQUENCE_DEDUP_ENABLE 0
#define COMMAND_DEDUP_ENABLE 0
#define ASYNC_BUFFER_BINDING 0
#define CACHE_EXPERIMENTS 0
#define LATENCY_EXPERIMENTS 0
#define CACHE_ENTRY_SIZE 1000 // change this

int Server::framebufferHeight = 0;
int Server::framebufferWidth = 0;
int shader_compiled = 0;
size_t fc_cache_size = 0;
int frame_number = 0;
std::vector<std::string> current_frame_hash_list;
std::vector<std::string> prev_frame_hash_list;
lru11::Cache<std::string, std::string> command_cache("ccache", CACHE_ENTRY_SIZE, 0);

static void errorCallback(int errorCode, const char *errorDescription)

{
    fprintf(stderr, "Error: %s\n", errorDescription);
}

void GLAPIENTRY MessageCallback(GLenum source, GLenum type, GLuint id,
                                GLenum severity, GLsizei length,
                                const GLchar *message, const void *userParam)
{

    if (type == GL_DEBUG_TYPE_ERROR)
    {
        fprintf(stderr,
                "GL CALLBACK: %s type = 0x%x, severity = 0x%x, message = %s\n",
                (type == GL_DEBUG_TYPE_ERROR ? "** GL ERROR **" : ""), type,
                severity, message);
        exit(-1);
    }
}

void Server::server_bind()
{
    sock = zmq::socket_t(ctx, zmq::socket_type::pair);

    sock.connect("tcp://" + ip_address + ":" + port);

    if (enableStreaming)
    {
        boost::interprocess::message_queue::remove(streaming_queue_name.c_str());
        std::cout << streaming_queue_name.c_str() << std::endl;
        size_t max_msg_size = WIDTH * HEIGHT * 4;
        mq.reset(new boost::interprocess::message_queue(
            boost::interprocess::create_only, streaming_queue_name.c_str(), 1024,
            max_msg_size));
    }
}

static void framebufferSizeCallback(GLFWwindow *window, int width, int height)
{
    //처음 2개의 파라미터는 viewport rectangle의 왼쪽 아래 좌표
    //다음 2개의 파라미터는 viewport의 너비와 높이이다.
    // framebuffer의 width와 height를 가져와 glViewport에서 사용한다.
    glViewport(0, 0, width, height);

    Server::framebufferWidth = width;
    Server::framebufferHeight = height;
}

void update_frame_hash_list(std::string message)
{
    fc_cache_size += message.size();
    current_frame_hash_list.push_back(message);
}
void update_command_cache(std::string message, bool ccache_hit, bool fccache_hit)
{
    if (!ccache_hit)
    {
        if (!fccache_hit)
            command_cache.insert(message, message);
    }
    else
        command_cache.update(message);
}
void Server::init_gl()
{

    glfwSetErrorCallback(errorCallback);
    if (!glfwInit())
    {
        std::cerr << "Error: GLFW 초기화 실패" << std::endl;
        std::exit(EXIT_FAILURE);
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    // glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    // glfwWindowHint(GLFW_SAMPLES, 4);
    // glfwWindowHint(GLFW_VISIBLE, GL_TRUE);

    window =
        glfwCreateWindow(WIDTH, HEIGHT, "Remote Drawing Example", NULL, NULL);

    if (!window)
    {
        glfwTerminate();
        std::exit(EXIT_FAILURE);
    }
    glfwMakeContextCurrent(window);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout << "Failed to initialize GLAD" << std::endl;
        std::exit(EXIT_FAILURE);
    }

    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);

    glEnable(GL_DEBUG_OUTPUT);
    glDebugMessageCallback(MessageCallback, 0);

    glfwSwapInterval(1);
}

void Server::run()
{
    bool quit = false;
    init_gl();
    double lastTime = glfwGetTime();
    OpenGLCmd *c;
    current_sequence_number = 0;
    int numOfFrames = 0;
#if CACHE_EXPERIMENTS
    unsigned int command_count_for_sec = 0;
    unsigned int fccache_hit_count = 0;
    unsigned int ccache_hit_count = 0;
    unsigned int longest_ccache_index = 0;
    std::cout << "fccache_hit_count"
              << "/"
              << "ccache_hit_count"
              << "/"
              << "fccache_target"
              << "/"
              << "ccache_target"
              << "/"
              << "ccache_size"
              << "/"
              << "longest_ccache_index"
              << "fc_cache_size " << std::endl;
#endif
    zmq::message_t msg;
    zmq::message_t ret;
    while (!quit)
    {
        // waiting until data comes here

        bool hasReturn = false;
        bool ccache_hit = false;
        bool fccache_hit = false;
        auto res = sock.recv(msg, zmq::recv_flags::none);
//entry point
// 받았을 때 시간 출력
#if LATENCY_EXPERIMENTS
        if (current_sequence_number == 0)
        {
            auto current_time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            std::cout << "LATENCY_RECEIVE:" << current_time << "\tframe_nubmer:" << frame_number << std::endl;
        }
#endif
        std::string message = msg.to_string();

#if SEQUENCE_DEDUP_ENABLE
        if (message.empty())
        {
            message = prev_frame_hash_list[current_sequence_number];
            fccache_hit = true;
#if CACHE_EXPERIMENTS
            fccache_hit_count++;
#endif
        }
#endif
        c = (OpenGLCmd *)message.data();
#if LATENCY_EXPERIMENTS
        if (c->cmd == (unsigned int)GL_Server_Command::GLSC_bufferSwap)
        {
            auto bufferswap_arrival_time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            std::cout << "LATENCY_LAST_COMMAND_ARRIVAL:" << bufferswap_arrival_time << "\tframe_nubmer:" << frame_number << std::endl;
        }
#endif
#if COMMAND_DEDUP_ENABLE
        if (c->deduplication == 3)
        {
            CCacheLocator *locator = (CCacheLocator *)((char *)message.data() + CMD_FIELD_SIZE);
            message = command_cache.direct_find(locator->bucket_id, locator->index);
            c = (OpenGLCmd *)message.data(); // 형변환을 해줘야 캐시에서 제대로 된 cmd들 읽어옴
            ccache_hit = true;
#if CACHE_EXPERIMENTS
            ccache_hit_count++;
            longest_ccache_index = std::max(longest_ccache_index, locator->index);
#endif
        }
#endif

#if SEQUENCE_DEDUP_ENABLE
        update_frame_hash_list(message);
#endif

#if COMMAND_DEDUP_ENABLE
        update_command_cache(message, ccache_hit, fccache_hit);
#endif
        switch (c->cmd)
        {
        case (unsigned int)GL_Server_Command::GLSC_glGetString:
        {
            gl_glGetString_t *cmd_data = (gl_glGetString_t *)((char *)message.data() + CMD_FIELD_SIZE);
            const GLubyte *strings = glGetString(cmd_data->name);
            hasReturn = true;
            ret.rebuild(strlen(reinterpret_cast<const char *>(strings)));
            memcpy(ret.data(), strings, strlen(reinterpret_cast<const char *>(strings)));
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glGetStringi:
        {
            gl_glGetStringi_t *cmd_data = (gl_glGetStringi_t *)((char *)message.data() + CMD_FIELD_SIZE);

            hasReturn = true;
            const GLubyte *strings = glGetStringi(cmd_data->name, cmd_data->index);
            ret.rebuild(strlen(reinterpret_cast<const char *>(strings)));
            memcpy(ret.data(), strings, strlen(reinterpret_cast<const char *>(strings)));
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glGetIntegerv:
        {
            gl_glGetIntegerv_t *cmd_data = (gl_glGetIntegerv_t *)((char *)message.data() + CMD_FIELD_SIZE);
            GLint result;

            glGetIntegerv(cmd_data->pname, &result);
            hasReturn = true;
            ret.rebuild(sizeof(int));
            memcpy(ret.data(), &result, sizeof(GLint));
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glCheckFramebufferStatus:
        {
            gl_glCheckFramebufferStatus_t *cmd_data =
                (gl_glCheckFramebufferStatus_t *)((char *)message.data() + CMD_FIELD_SIZE);

            GLenum status = glCheckFramebufferStatus(cmd_data->target);

            hasReturn = true;
            ret.rebuild(sizeof(GLenum));
            memcpy(ret.data(), &status, sizeof(GLenum));
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glViewport:
        {
            gl_glViewport_t *cmd_data = (gl_glViewport_t *)((char *)message.data() + CMD_FIELD_SIZE);
            glViewport(cmd_data->x, cmd_data->y, cmd_data->width, cmd_data->height);
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glClear:
        {
            gl_glClear_t *cmd_data = (gl_glClear_t *)((char *)message.data() + CMD_FIELD_SIZE);
            glClear(cmd_data->mask);
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glBegin:
        {
            gl_glBegin_t *cmd_data = (gl_glBegin_t *)((char *)message.data() + CMD_FIELD_SIZE);
            glBegin(cmd_data->mode);
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glColor3f:
        {
            gl_glColor3f_t *cmd_data = (gl_glColor3f_t *)((char *)message.data() + CMD_FIELD_SIZE);
            glColor3f(cmd_data->red, cmd_data->green, cmd_data->blue);
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glVertex3f:
        {
            gl_glVertex3f_t *cmd_data = (gl_glVertex3f_t *)((char *)message.data() + CMD_FIELD_SIZE);
            glVertex3f(cmd_data->x, cmd_data->y, cmd_data->z);
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glEnd:
        {
            glEnd();
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glFlush:
        {
            glFlush();
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glEnable:
        {
            gl_glEnable_t *cmd_data = (gl_glEnable_t *)((char *)message.data() + CMD_FIELD_SIZE);
            glEnable(cmd_data->cap);
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glCreateShader:
        {
            gl_glCreateShader_t *cmd_data = (gl_glCreateShader_t *)((char *)message.data() + CMD_FIELD_SIZE);
            GLuint shader = glCreateShader(cmd_data->type);

            hasReturn = true;
            ret.rebuild(sizeof(GLuint));
            memcpy(ret.data(), &shader, sizeof(GLuint));
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glShaderSource:
        {
            // recv data
            gl_glShaderSource_t *cmd_data = (gl_glShaderSource_t *)((char *)message.data() + CMD_FIELD_SIZE);
            std::vector<std::string> tmp;
            std::vector<const char *> strings;
            std::string sharder_source = message.data() + CMD_FIELD_SIZE + sizeof(gl_glShaderSource_t);
            cmd_data->string_length = (GLuint *)((char *)message.data() + message.size() - sizeof(GLuint) * cmd_data->count);

            GLuint begin = 0;
            for (int i = 0; i < cmd_data->count; i++)
            {
                GLuint last = cmd_data->string_length[i];
                if (last > 0)
                {
                    char *src = new char[last + 1];
                    strcpy(src, sharder_source.substr(begin, last).c_str());
                    strings.push_back(src);
                }
                else
                {
                    strings.push_back("\n");
                }
                begin += last;
            }

            glShaderSource(cmd_data->shader, cmd_data->count, &strings[0], NULL);

            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glTransformFeedbackVaryings:
        {
            // recv data
            gl_glTransformFeedbackVaryings_t *cmd_data = (gl_glTransformFeedbackVaryings_t *)((char *)message.data() + CMD_FIELD_SIZE);
            std::vector<std::string> tmp;
            std::vector<const char *> strings;
            std::string varyings_source = message.data() + CMD_FIELD_SIZE + sizeof(gl_glTransformFeedbackVaryings_t);
            cmd_data->string_length = (GLuint *)((char *)message.data() + message.size() - sizeof(GLuint) * cmd_data->count);

            GLuint begin = 0;
            for (int i = 0; i < cmd_data->count; i++)
            {
                GLuint last = cmd_data->string_length[i];
                if (last > 0)
                {
                    char *src = new char[last + 1];
                    strcpy(src, varyings_source.substr(begin, last).c_str());
                    strings.push_back(src);
                }
                else
                {
                    strings.push_back("\n");
                }
                begin += last;
            }
            glTransformFeedbackVaryings(cmd_data->program, cmd_data->count, &strings[0], cmd_data->bufferMode);

            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glBeginTransformFeedback:
        {
            gl_glBeginTransformFeedback_t *cmd_data = (gl_glBeginTransformFeedback_t *)((char *)message.data() + CMD_FIELD_SIZE);
            glBeginTransformFeedback(cmd_data->primitiveMode);
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glEndTransformFeedback:
        {
            glEndTransformFeedback();
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glCompileShader:
        {
            gl_glCompileShader_t *cmd_data = (gl_glCompileShader_t *)((char *)message.data() + CMD_FIELD_SIZE);
            glCompileShader(cmd_data->shader);
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glAttachShader:
        {
            gl_glAttachShader_t *cmd_data = (gl_glAttachShader_t *)((char *)message.data() + CMD_FIELD_SIZE);
            glAttachShader(cmd_data->program, cmd_data->shader);
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glLinkProgram:
        {
            gl_glLinkProgram_t *cmd_data = (gl_glLinkProgram_t *)((char *)message.data() + CMD_FIELD_SIZE);
            glLinkProgram(cmd_data->program);
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glUseProgram:
        {
            gl_glUseProgram_t *cmd_data = (gl_glUseProgram_t *)((char *)message.data() + CMD_FIELD_SIZE);
            glUseProgram(cmd_data->program);
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glGetError:
        {
            GLenum error = glGetError();

            hasReturn = true;
            ret.rebuild(sizeof(GLenum));
            memcpy(ret.data(), &error, sizeof(GLenum));
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glCreateProgram:
        {
            GLuint program = glCreateProgram();

            hasReturn = true;
            ret.rebuild(sizeof(GLuint));
            memcpy(ret.data(), &program, sizeof(GLuint));
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glGetShaderiv:
        {
            gl_glGetShaderiv_t *cmd_data = (gl_glGetShaderiv_t *)((char *)message.data() + CMD_FIELD_SIZE);

            GLint result;

            glGetShaderiv(cmd_data->shader, cmd_data->pname, &result);

            shader_compiled++;
            if (!result)
            {
                GLchar errorLog[512];
                glGetShaderInfoLog(cmd_data->shader, 512, NULL, errorLog);
                std::cerr << "ERROR: vertex shader 컴파일 실패\n"
                          << errorLog << std::endl;
            }
            hasReturn = true;
            ret.rebuild(sizeof(int));
            memcpy(ret.data(), &result, sizeof(int));
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glGenVertexArrays:
        {
            gl_glGenVertexArrays_t *cmd_data = (gl_glGenVertexArrays_t *)((char *)message.data() + CMD_FIELD_SIZE);

            GLuint *result = new GLuint[cmd_data->n];
            glGenVertexArrays(cmd_data->n, result);
#if ASYNC_BUFFER_BINDING
            for (int i = 0; i < cmd_data->n; i++)
                glGenVertexArrays_idx_map.insert(std::make_pair(cmd_data->last_index - cmd_data->n + 1 + i, result[i]));
#else
            hasReturn = true;
            ret.rebuild(sizeof(GLuint) * cmd_data->n);
            memcpy(ret.data(), result, sizeof(GLuint) * cmd_data->n);
#endif
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glGenTextures:
        {
            gl_glGenTextures_t *cmd_data = (gl_glGenTextures_t *)((char *)message.data() + CMD_FIELD_SIZE);

            GLuint *result = new GLuint[cmd_data->n];
            glGenTextures(cmd_data->n, result);
#if ASYNC_BUFFER_BINDING
            for (int i = 0; i < cmd_data->n; i++)
                glGenTextures_idx_map.insert(std::make_pair(cmd_data->last_index - cmd_data->n + 1 + i, result[i]));
#else
            hasReturn = true;
            ret.rebuild(sizeof(GLuint) * cmd_data->n);
            memcpy(ret.data(), result, sizeof(GLuint) * cmd_data->n);
#endif
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glGenBuffers:
        {
            gl_glGenBuffers_t *cmd_data = (gl_glGenBuffers_t *)((char *)message.data() + CMD_FIELD_SIZE);

            GLuint *result = new GLuint[cmd_data->n];
            glGenBuffers(cmd_data->n, result);
#if ASYNC_BUFFER_BINDING
            for (int i = 0; i < cmd_data->n; i++)
                glGenBuffers_idx_map.insert(std::make_pair(cmd_data->last_index - cmd_data->n + 1 + i, result[i]));
#else
            hasReturn = true;
            ret.rebuild(sizeof(GLuint) * cmd_data->n);
            memcpy(ret.data(), result, sizeof(GLuint) * cmd_data->n);
#endif
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glGenFramebuffers:
        {
            gl_glGenFramebuffers_t *cmd_data = (gl_glGenFramebuffers_t *)((char *)message.data() + CMD_FIELD_SIZE);

            GLuint *result = new GLuint[cmd_data->n];
            glGenFramebuffers(cmd_data->n, result);
#if ASYNC_BUFFER_BINDING
            for (int i = 0; i < cmd_data->n; i++)
                glGenFramebuffers_idx_map.insert(std::make_pair(cmd_data->last_index - cmd_data->n + 1 + i, result[i]));
#else
            hasReturn = true;
            ret.rebuild(sizeof(GLuint) * cmd_data->n);
            memcpy(ret.data(), result, sizeof(GLuint) * cmd_data->n);
#endif
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glGenRenderbuffers:
        {
            gl_glGenRenderbuffers_t *cmd_data = (gl_glGenRenderbuffers_t *)((char *)message.data() + CMD_FIELD_SIZE);
            GLuint *result = new GLuint[cmd_data->n];
            glGenRenderbuffers(cmd_data->n, result);
#if ASYNC_BUFFER_BINDING
            for (int i = 0; i < cmd_data->n; i++)
                glGenRenderbuffers_idx_map.insert(std::make_pair(cmd_data->last_index - cmd_data->n + 1 + i, result[i]));
#else
            hasReturn = true;
            ret.rebuild(sizeof(GLuint) * cmd_data->n);
            memcpy(ret.data(), result, sizeof(GLuint) * cmd_data->n);
#endif
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glGetUniformLocation:
        {
            gl_glGetUniformLocation_t *cmd_data = (gl_glGetUniformLocation_t *)((char *)message.data() + CMD_FIELD_SIZE);
            std::string pointer_param = message.substr(CMD_FIELD_SIZE + sizeof(gl_glGetUniformLocation_t), message.size() - (CMD_FIELD_SIZE + sizeof(gl_glGetUniformLocation_t)));

            GLint location = glGetUniformLocation(cmd_data->program, pointer_param.c_str());

            hasReturn = true;
            ret.rebuild(sizeof(GLint));
            memcpy(ret.data(), &location, sizeof(GLint));
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glGetUniformBlockIndex:
        {
            gl_glGetUniformBlockIndex_t *cmd_data = (gl_glGetUniformBlockIndex_t *)((char *)message.data() + CMD_FIELD_SIZE);
            std::string pointer_param = message.substr(CMD_FIELD_SIZE + sizeof(gl_glGetUniformBlockIndex_t), message.size() - (CMD_FIELD_SIZE + sizeof(gl_glGetUniformBlockIndex_t)));

            GLint index = glGetUniformBlockIndex(cmd_data->program, pointer_param.c_str());

            hasReturn = true;
            ret.rebuild(sizeof(GLint));
            memcpy(ret.data(), &index, sizeof(GLint));
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glActiveTexture:
        {
            gl_glActiveTexture_t *cmd_data = (gl_glActiveTexture_t *)((char *)message.data() + CMD_FIELD_SIZE);
            glActiveTexture(cmd_data->texture);
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glBindTexture:
        {
            gl_glBindTexture_t *cmd_data = (gl_glBindTexture_t *)((char *)message.data() + CMD_FIELD_SIZE);
            GLuint texture;
#if ASYNC_BUFFER_BINDING
            texture = (GLuint)glGenTextures_idx_map.find(cmd_data->texture)->second;

#else
            texture = cmd_data->texture;
#endif
            glBindTexture(cmd_data->target, texture);
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glBindBuffer:
        {
            gl_glBindBuffer_t *cmd_data = (gl_glBindBuffer_t *)((char *)message.data() + CMD_FIELD_SIZE);
            GLuint buffer_id;
#if ASYNC_BUFFER_BINDING
            buffer_id = (GLuint)glGenBuffers_idx_map.find(cmd_data->id)->second;
#else
            buffer_id = cmd_data->id;
#endif
            glBindBuffer(cmd_data->target, buffer_id);
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glBindBufferBase:
        {
            gl_glBindBufferBase_t *cmd_data = (gl_glBindBufferBase_t *)((char *)message.data() + CMD_FIELD_SIZE);
            GLuint buffer_id;
#if ASYNC_BUFFER_BINDING
            buffer_id = (GLuint)glGenBuffers_idx_map.find(cmd_data->buffer)->second;
#else
            buffer_id = cmd_data->buffer;
#endif
            glBindBufferBase(cmd_data->target, cmd_data->index, buffer_id);
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glBindVertexArray:
        {
            gl_glBindVertexArray_t *cmd_data = (gl_glBindVertexArray_t *)((char *)message.data() + CMD_FIELD_SIZE);
            GLuint array;
#if ASYNC_BUFFER_BINDING
            array = (GLuint)glGenVertexArrays_idx_map.find(cmd_data->array)->second;
#else
            array = cmd_data->array;
#endif
            glBindVertexArray(array);
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glBindFramebuffer:
        {
            gl_glBindFramebuffer_t *cmd_data = (gl_glBindFramebuffer_t *)((char *)message.data() + CMD_FIELD_SIZE);
            GLuint framebuffer;
#if ASYNC_BUFFER_BINDING
            framebuffer = (GLuint)glGenFramebuffers_idx_map.find(cmd_data->framebuffer)->second;
#else
            framebuffer = cmd_data->framebuffer;
#endif
            glBindFramebuffer(cmd_data->target, framebuffer);
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glBindRenderbuffer:
        {
            gl_glBindRenderbuffer_t *cmd_data = (gl_glBindRenderbuffer_t *)((char *)message.data() + CMD_FIELD_SIZE);
            GLuint renderbuffer;
#if ASYNC_BUFFER_BINDING
            renderbuffer = (GLuint)glGenRenderbuffers_idx_map.find(cmd_data->renderbuffer)->second;
#else
            renderbuffer = cmd_data->renderbuffer;
#endif
            glBindRenderbuffer(cmd_data->target, renderbuffer);
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glBufferData:
        {
            gl_glBufferData_t *cmd_data = (gl_glBufferData_t *)((char *)message.data() + CMD_FIELD_SIZE);
            std::string pointer_param = message.substr(CMD_FIELD_SIZE + sizeof(gl_glBufferData_t), message.size() - (CMD_FIELD_SIZE + sizeof(gl_glBufferData_t)));
            if (!pointer_param.empty())
                glBufferData(cmd_data->target, cmd_data->size, pointer_param.data(), cmd_data->usage);
            else
                glBufferData(cmd_data->target, cmd_data->size, NULL, cmd_data->usage);

            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glTexParameteri:
        {
            gl_glTexParameteri_t *cmd_data = (gl_glTexParameteri_t *)((char *)message.data() + CMD_FIELD_SIZE);
            glTexParameteri(cmd_data->target, cmd_data->pname, cmd_data->param);
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glTexParameterf:
        {
            gl_glTexParameterf_t *cmd_data = (gl_glTexParameterf_t *)((char *)message.data() + CMD_FIELD_SIZE);
            glTexParameterf(cmd_data->target, cmd_data->pname, cmd_data->param);
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glTexImage2D:
        {
            gl_glTexImage2D_t *cmd_data = (gl_glTexImage2D_t *)((char *)message.data() + CMD_FIELD_SIZE);
            std::string pointer_param = message.substr(CMD_FIELD_SIZE + sizeof(gl_glTexImage2D_t), message.size() - (CMD_FIELD_SIZE + sizeof(gl_glTexImage2D_t)));

            if (!pointer_param.empty())
            {
                glTexImage2D(cmd_data->target, cmd_data->level,
                             cmd_data->internalformat, cmd_data->width,
                             cmd_data->height, cmd_data->border, cmd_data->format,
                             cmd_data->type, (const void *)pointer_param.data());
            }
            else
            {
                glTexImage2D(cmd_data->target, cmd_data->level,
                             cmd_data->internalformat, cmd_data->width,
                             cmd_data->height, cmd_data->border, cmd_data->format,
                             cmd_data->type, NULL);
            }
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glTexSubImage2D:
        {
            gl_glTexSubImage2D_t *cmd_data = (gl_glTexSubImage2D_t *)((char *)message.data() + CMD_FIELD_SIZE);
            std::string pointer_param = message.substr(CMD_FIELD_SIZE + sizeof(gl_glTexSubImage2D_t), message.size() - (CMD_FIELD_SIZE + sizeof(gl_glTexSubImage2D_t)));

            if (!pointer_param.empty())
            {
                glTexSubImage2D(cmd_data->target, cmd_data->level, cmd_data->xoffset,
                                cmd_data->yoffset, cmd_data->width, cmd_data->height,
                                cmd_data->format, cmd_data->type, pointer_param.data());
            }
            else
            {
                glTexSubImage2D(cmd_data->target, cmd_data->level, cmd_data->xoffset,
                                cmd_data->yoffset, cmd_data->width, cmd_data->height,
                                cmd_data->format, cmd_data->type, NULL);
            }
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glCompressedTexImage2D:
        {
            gl_glCompressedTexImage2D_t *cmd_data = (gl_glCompressedTexImage2D_t *)((char *)message.data() + CMD_FIELD_SIZE);
            std::string pointer_param = message.substr(CMD_FIELD_SIZE + sizeof(gl_glCompressedTexImage2D_t), message.size() - (CMD_FIELD_SIZE + sizeof(gl_glCompressedTexImage2D_t)));

            if (!pointer_param.empty())
            {
                glCompressedTexImage2D(cmd_data->target, cmd_data->level,
                                       cmd_data->internalformat, cmd_data->width,
                                       cmd_data->height, cmd_data->border,
                                       cmd_data->imageSize, pointer_param.data());
            }
            else
            {
                glCompressedTexImage2D(cmd_data->target, cmd_data->level,
                                       cmd_data->internalformat, cmd_data->width,
                                       cmd_data->height, cmd_data->border,
                                       cmd_data->imageSize, NULL);
            }
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glTexImage3D:
        {
            gl_glTexImage3D_t *cmd_data = (gl_glTexImage3D_t *)((char *)message.data() + CMD_FIELD_SIZE);
            std::string pointer_param = message.substr(CMD_FIELD_SIZE + sizeof(gl_glTexImage3D_t), message.size() - (CMD_FIELD_SIZE + sizeof(gl_glTexImage3D_t)));

            if (!pointer_param.empty())
            {
                glTexImage3D(cmd_data->target, cmd_data->level,
                             cmd_data->internalformat, cmd_data->width,
                             cmd_data->height, cmd_data->depth, cmd_data->border,
                             cmd_data->format, cmd_data->type, pointer_param.data());
            }
            else
            {
                glTexImage3D(cmd_data->target, cmd_data->level,
                             cmd_data->internalformat, cmd_data->width,
                             cmd_data->height, cmd_data->depth, cmd_data->border,
                             cmd_data->format, cmd_data->type, NULL);
            }
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glTexSubImage3D:
        {
            gl_glTexSubImage3D_t *cmd_data = (gl_glTexSubImage3D_t *)((char *)message.data() + CMD_FIELD_SIZE);
            std::string pointer_param = message.substr(CMD_FIELD_SIZE + sizeof(gl_glTexSubImage3D_t), message.size() - (CMD_FIELD_SIZE + sizeof(gl_glTexSubImage3D_t)));

            if (!pointer_param.empty())
            {
                glTexSubImage3D(cmd_data->target, cmd_data->level, cmd_data->xoffset,
                                cmd_data->yoffset, cmd_data->zoffset, cmd_data->width,
                                cmd_data->height, cmd_data->depth, cmd_data->format,
                                cmd_data->type, pointer_param.data());
            }
            else
            {
                glTexSubImage3D(cmd_data->target, cmd_data->level, cmd_data->xoffset,
                                cmd_data->yoffset, cmd_data->zoffset, cmd_data->width,
                                cmd_data->height, cmd_data->depth, cmd_data->format,
                                cmd_data->type, pointer_param.data());
            }
            std::cout << c->cmd << "\t" << cmd_data->target << "\t" << cmd_data->level << "\t" << cmd_data->xoffset << "\t" << cmd_data->yoffset << "\t" << cmd_data->zoffset << "\t" << cmd_data->width << "\t" << cmd_data->height << "\t" << cmd_data->depth << "\t" << cmd_data->format << "\t" << cmd_data->type << std::endl;

            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glFramebufferTexture2D:
        {

            gl_glFramebufferTexture2D_t *cmd_data = (gl_glFramebufferTexture2D_t *)((char *)message.data() + CMD_FIELD_SIZE);
            GLuint texture;
#if ASYNC_BUFFER_BINDING
            texture = (GLuint)glGenTextures_idx_map.find(cmd_data->texture)->second;
#else
            texture = cmd_data->texture;
#endif
            glFramebufferTexture2D(cmd_data->target, cmd_data->attachment,
                                   cmd_data->textarget, texture,
                                   cmd_data->level);
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glPixelStorei:
        {
            gl_glPixelStorei_t *cmd_data = (gl_glPixelStorei_t *)((char *)message.data() + CMD_FIELD_SIZE);
            glPixelStorei(cmd_data->pname, cmd_data->param);
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glDrawArrays:
        {
            gl_glDrawArrays_t *cmd_data = (gl_glDrawArrays_t *)((char *)message.data() + CMD_FIELD_SIZE);
            glDrawArrays(cmd_data->mode, cmd_data->first, cmd_data->count);
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glColorMask:
        {
            gl_glColorMask_t *cmd_data = (gl_glColorMask_t *)((char *)message.data() + CMD_FIELD_SIZE);
            glColorMask(cmd_data->red, cmd_data->green, cmd_data->blue, cmd_data->alpha);
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glDeleteTextures:
        {
            gl_glDeleteTextures_t *cmd_data = (gl_glDeleteTextures_t *)((char *)message.data() + CMD_FIELD_SIZE);
            std::string pointer_param = message.substr(CMD_FIELD_SIZE + sizeof(gl_glDeleteTextures_t), message.size() - (CMD_FIELD_SIZE + sizeof(gl_glDeleteTextures_t)));
#if ASYNC_BUFFER_BINDING
            GLuint *tmp = new GLuint[pointer_param.size()];
            GLuint *textures = new GLuint[pointer_param.size()];

            memcpy((void *)tmp, pointer_param.data(), pointer_param.size());
            for (int i = 0; i < pointer_param.size(); i++)
                textures[i] = (GLuint)glGenTextures_idx_map.find(tmp[i])->second;
            glDeleteTextures(cmd_data->n, textures);

#else
            glDeleteTextures(cmd_data->n, (GLuint *)pointer_param.data());
#endif

            break;
        }
        case (unsigned char)GL_Server_Command::GLSC_glDeleteVertexArrays:
        {
            gl_glDeleteVertexArrays_t *cmd_data = (gl_glDeleteVertexArrays_t *)((char *)message.data() + CMD_FIELD_SIZE);
            std::string pointer_param = message.substr(CMD_FIELD_SIZE + sizeof(gl_glDeleteVertexArrays_t), message.size() - (CMD_FIELD_SIZE + sizeof(gl_glDeleteVertexArrays_t)));
#if ASYNC_BUFFER_BINDING
            GLuint *tmp = new GLuint[pointer_param.size()];
            GLuint *arrays = new GLuint[pointer_param.size()];

            memcpy((void *)tmp, pointer_param.data(), pointer_param.size());
            for (int i = 0; i < pointer_param.size(); i++)
                arrays[i] = (GLuint)glGenVertexArrays_idx_map.find(tmp[i])->second;
            glDeleteVertexArrays(cmd_data->n, arrays);

#else
            glDeleteVertexArrays(cmd_data->n, (GLuint *)pointer_param.data());
#endif

            break;
        }
        case (unsigned char)GL_Server_Command::GLSC_glDeleteBuffers:
        {
            gl_glDeleteBuffers_t *cmd_data = (gl_glDeleteBuffers_t *)((char *)message.data() + CMD_FIELD_SIZE);
            std::string pointer_param = message.substr(CMD_FIELD_SIZE + sizeof(gl_glDeleteBuffers_t), message.size() - (CMD_FIELD_SIZE + sizeof(gl_glDeleteBuffers_t)));
#if ASYNC_BUFFER_BINDING
            GLuint *tmp = new GLuint[pointer_param.size()];
            GLuint *buffers = new GLuint[pointer_param.size()];

            memcpy((void *)tmp, pointer_param.data(), pointer_param.size());
            for (int i = 0; i < pointer_param.size(); i++)
                buffers[i] = (GLuint)glGenBuffers_idx_map.find(tmp[i])->second;
            glDeleteBuffers(cmd_data->n, buffers);

#else
            glDeleteBuffers(cmd_data->n, (GLuint *)pointer_param.data());
#endif

            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glDeleteFramebuffers:
        {

            gl_glDeleteFramebuffers_t *cmd_data = (gl_glDeleteFramebuffers_t *)((char *)message.data() + CMD_FIELD_SIZE);
            std::string pointer_param = message.substr(CMD_FIELD_SIZE + sizeof(gl_glDeleteFramebuffers_t), message.size() - (CMD_FIELD_SIZE + sizeof(gl_glDeleteFramebuffers_t)));
#if ASYNC_BUFFER_BINDING
            GLuint *tmp = new GLuint[pointer_param.size()];
            GLuint *buffers = new GLuint[pointer_param.size()];

            memcpy((void *)tmp, pointer_param.data(), pointer_param.size());
            for (int i = 0; i < pointer_param.size(); i++)
                buffers[i] = (GLuint)glGenFramebuffers_idx_map.find(tmp[i])->second;
            glDeleteFramebuffers(cmd_data->n, buffers);

#else
            glDeleteFramebuffers(cmd_data->n, (GLuint *)pointer_param.data());
#endif

            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glDeleteRenderbuffers:
        {
            gl_glDeleteRenderbuffers_t *cmd_data = (gl_glDeleteRenderbuffers_t *)((char *)message.data() + CMD_FIELD_SIZE);
            std::string pointer_param = message.substr(CMD_FIELD_SIZE + sizeof(gl_glDeleteRenderbuffers_t), message.size() - (CMD_FIELD_SIZE + sizeof(gl_glDeleteRenderbuffers_t)));
#if ASYNC_BUFFER_BINDING
            GLuint *tmp = new GLuint[pointer_param.size()];
            GLuint *buffers = new GLuint[pointer_param.size()];

            memcpy((void *)tmp, pointer_param.data(), pointer_param.size());
            for (int i = 0; i < pointer_param.size(); i++)
                buffers[i] = (GLuint)glGenRenderbuffers_idx_map.find(tmp[i])->second;
            glDeleteRenderbuffers(cmd_data->n, buffers);

#else
            glDeleteRenderbuffers(cmd_data->n, (GLuint *)pointer_param.data());
#endif
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glGenerateMipmap:
        {
            gl_glGenerateMipmap_t *cmd_data = (gl_glGenerateMipmap_t *)((char *)message.data() + CMD_FIELD_SIZE);
            glGenerateMipmap(cmd_data->target);
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glVertexAttribPointer:
        {
            gl_glVertexAttribPointer_t *cmd_data = (gl_glVertexAttribPointer_t *)((char *)message.data() + CMD_FIELD_SIZE);
            glVertexAttribPointer(cmd_data->index, cmd_data->size, cmd_data->type,
                                  cmd_data->normalized, cmd_data->stride,
                                  (void *)cmd_data->pointer); // pointer add 0
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glEnableVertexAttribArray:
        {
            gl_glEnableVertexAttribArray_t *cmd_data = (gl_glEnableVertexAttribArray_t *)((char *)message.data() + CMD_FIELD_SIZE);
            glEnableVertexAttribArray(cmd_data->index);
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glFrontFace:
        {
            gl_glFrontFace_t *cmd_data = (gl_glFrontFace_t *)((char *)message.data() + CMD_FIELD_SIZE);
            glFrontFace(cmd_data->mode);
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glDisable:
        {
            gl_glDisable_t *cmd_data = (gl_glDisable_t *)((char *)message.data() + CMD_FIELD_SIZE);
            glDisable(cmd_data->cap);
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glDepthMask:
        {
            gl_glDepthMask_t *cmd_data = (gl_glDepthMask_t *)((char *)message.data() + CMD_FIELD_SIZE);
            glDepthMask(cmd_data->flag);
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glClearColor:
        {
            gl_glClearColor_t *cmd_data = (gl_glClearColor_t *)((char *)message.data() + CMD_FIELD_SIZE);
            glClearColor(cmd_data->red, cmd_data->green, cmd_data->blue, cmd_data->alpha);
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glBlendEquation:
        {
            gl_glBlendEquation_t *cmd_data = (gl_glBlendEquation_t *)((char *)message.data() + CMD_FIELD_SIZE);
            glBlendEquation(cmd_data->mode);
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glBlendFunc:
        {
            gl_glBlendFunc_t *cmd_data = (gl_glBlendFunc_t *)((char *)message.data() + CMD_FIELD_SIZE);
            glBlendFunc(cmd_data->sfactor, cmd_data->dfactor);
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glVertexAttrib4f:
        {
            gl_glVertexAttrib4f_t *cmd_data = (gl_glVertexAttrib4f_t *)((char *)message.data() + CMD_FIELD_SIZE);
            glVertexAttrib4f(cmd_data->index, cmd_data->x, cmd_data->y, cmd_data->z, cmd_data->w);
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glVertexAttrib4fv:
        {
            gl_glVertexAttrib4fv_t *cmd_data = (gl_glVertexAttrib4fv_t *)((char *)message.data() + CMD_FIELD_SIZE);
            std::string pointer_param = message.substr(CMD_FIELD_SIZE + sizeof(gl_glVertexAttrib4fv_t), message.size() - (CMD_FIELD_SIZE + sizeof(gl_glVertexAttrib4fv_t)));

            if (!pointer_param.empty())
                glVertexAttrib4fv(cmd_data->index, (GLfloat *)pointer_param.data());
            else
                glVertexAttrib4fv(cmd_data->index, NULL);
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glUniform1i:
        {
            gl_glUniform1i_t *cmd_data = (gl_glUniform1i_t *)((char *)message.data() + CMD_FIELD_SIZE);
            glUniform1i(cmd_data->location, cmd_data->v0);
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glUniform1f:
        {
            gl_glUniform1f_t *cmd_data = (gl_glUniform1f_t *)((char *)message.data() + CMD_FIELD_SIZE);
            glUniform1f(cmd_data->location, cmd_data->v0);
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glUniform1ui:
        {
            gl_glUniform1ui_t *cmd_data = (gl_glUniform1ui_t *)((char *)message.data() + CMD_FIELD_SIZE);
            glUniform1ui(cmd_data->location, cmd_data->v0);
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glUniform2fv:
        {
            gl_glUniform2fv_t *cmd_data = (gl_glUniform2fv_t *)((char *)message.data() + CMD_FIELD_SIZE);
            std::string pointer_param = message.substr(CMD_FIELD_SIZE + sizeof(gl_glUniform2fv_t), message.size() - (CMD_FIELD_SIZE + sizeof(gl_glUniform2fv_t)));

            if (!pointer_param.empty())
                glUniform2fv(cmd_data->location, cmd_data->count, (GLfloat *)pointer_param.data());
            else
                glUniform2fv(cmd_data->location, cmd_data->count, NULL);
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glUniform4fv:
        {
            gl_glUniform4fv_t *cmd_data = (gl_glUniform4fv_t *)((char *)message.data() + CMD_FIELD_SIZE);
            std::string pointer_param = message.substr(CMD_FIELD_SIZE + sizeof(gl_glUniform4fv_t), message.size() - (CMD_FIELD_SIZE + sizeof(gl_glUniform4fv_t)));

            if (!pointer_param.empty())
                glUniform4fv(cmd_data->location, cmd_data->count, (GLfloat *)pointer_param.data());
            else
                glUniform4fv(cmd_data->location, cmd_data->count, NULL);

            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glUniformMatrix4fv:
        {
            gl_glUniformMatrix4fv_t *cmd_data = (gl_glUniformMatrix4fv_t *)((char *)message.data() + CMD_FIELD_SIZE);
            std::string pointer_param = message.substr(CMD_FIELD_SIZE + sizeof(gl_glUniformMatrix4fv_t), message.size() - (CMD_FIELD_SIZE + sizeof(gl_glUniformMatrix4fv_t)));
            if (!pointer_param.empty())
                glUniformMatrix4fv(cmd_data->location, cmd_data->count, cmd_data->transpose, (GLfloat *)pointer_param.data());
            else
                glUniformMatrix4fv(cmd_data->location, cmd_data->count, cmd_data->transpose, NULL);
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glUniformBlockBinding:
        {
            gl_glUniformBlockBinding_t *cmd_data = (gl_glUniformBlockBinding_t *)((char *)message.data() + CMD_FIELD_SIZE);
            glUniformBlockBinding(cmd_data->program, cmd_data->uniformBlockIndex, cmd_data->uniformBlockBinding);
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glClearDepthf:
        {
            gl_glClearDepthf_t *cmd_data = (gl_glClearDepthf_t *)((char *)message.data() + CMD_FIELD_SIZE);
            glClearDepth(cmd_data->d);
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glDisableVertexAttribArray:
        {
            gl_glDisableVertexAttribArray_t *cmd_data = (gl_glDisableVertexAttribArray_t *)((char *)message.data() + CMD_FIELD_SIZE);
            glDisableVertexAttribArray(cmd_data->index);
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glReadBuffer:
        {
            gl_glReadBuffer_t *cmd_data = (gl_glReadBuffer_t *)((char *)message.data() + CMD_FIELD_SIZE);
            glReadBuffer(cmd_data->src);
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glRenderbufferStorage:
        {
            gl_glRenderbufferStorage_t *cmd_data = (gl_glRenderbufferStorage_t *)((char *)message.data() + CMD_FIELD_SIZE);
            glRenderbufferStorage(cmd_data->target, cmd_data->internalformat, cmd_data->width, cmd_data->height);
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glFramebufferRenderbuffer:
        {
            gl_glFramebufferRenderbuffer_t *cmd_data = (gl_glFramebufferRenderbuffer_t *)((char *)message.data() + CMD_FIELD_SIZE);
            GLuint renderbuffer;
#if ASYNC_BUFFER_BINDING
            renderbuffer = (GLuint)glGenRenderbuffers_idx_map.find(cmd_data->renderbuffer)->second;
#else
            renderbuffer = cmd_data->renderbuffer;
#endif
            glFramebufferRenderbuffer(cmd_data->target, cmd_data->attachment,
                                      cmd_data->renderbuffertarget,
                                      renderbuffer);
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glClearBufferfv:
        {
            gl_glClearBufferfv_t *cmd_data = (gl_glClearBufferfv_t *)((char *)message.data() + CMD_FIELD_SIZE);
            std::string pointer_param = message.substr(CMD_FIELD_SIZE + sizeof(gl_glClearBufferfv_t), message.size() - (CMD_FIELD_SIZE + sizeof(gl_glClearBufferfv_t)));
            glClearBufferfv(cmd_data->buffer, cmd_data->drawbuffer, (GLfloat *)pointer_param.data());
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glFramebufferTextureLayer:
        {
            gl_glFramebufferTextureLayer_t *cmd_data = (gl_glFramebufferTextureLayer_t *)((char *)message.data() + CMD_FIELD_SIZE);
            GLuint texture;
#if ASYNC_BUFFER_BINDING
            texture = (GLuint)glGenTextures_idx_map.find(cmd_data->texture)->second;
#else
            texture = cmd_data->texture;
#endif
            glFramebufferTextureLayer(cmd_data->target, cmd_data->attachment, texture, cmd_data->level, cmd_data->layer);
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glBlitFramebuffer:
        {
            gl_glBlitFramebuffer_t *cmd_data = (gl_glBlitFramebuffer_t *)((char *)message.data() + CMD_FIELD_SIZE);
            glBlitFramebuffer(cmd_data->srcX0, cmd_data->srcY0, cmd_data->srcX1,
                              cmd_data->srcY1, cmd_data->dstX0, cmd_data->dstY0,
                              cmd_data->dstX1, cmd_data->dstY1, cmd_data->mask,
                              cmd_data->filter);
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glCullFace:
        {
            gl_glCullFace_t *cmd_data = (gl_glCullFace_t *)((char *)message.data() + CMD_FIELD_SIZE);
            glCullFace(cmd_data->mode);
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glScissor:
        {
            gl_glScissor_t *cmd_data = (gl_glScissor_t *)((char *)message.data() + CMD_FIELD_SIZE);
            glScissor(cmd_data->x, cmd_data->y, cmd_data->width, cmd_data->height);
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glDrawElements:
        {
            gl_glDrawElements_t *cmd_data = (gl_glDrawElements_t *)((char *)message.data() + CMD_FIELD_SIZE);
            glDrawElements(cmd_data->mode, cmd_data->count, cmd_data->type, (void *)cmd_data->indices);
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glDrawBuffers:
        {
            gl_glDrawBuffers_t *cmd_data = (gl_glDrawBuffers_t *)((char *)message.data() + CMD_FIELD_SIZE);
            std::string pointer_param = message.substr(CMD_FIELD_SIZE + sizeof(gl_glDrawBuffers_t), message.size() - (CMD_FIELD_SIZE + sizeof(gl_glDrawBuffers_t)));
            if (!pointer_param.empty())
                glDrawBuffers(cmd_data->n, (GLenum *)pointer_param.data());
            else
                glDrawBuffers(cmd_data->n, NULL);

            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glDepthFunc:
        {
            gl_glDepthFunc_t *cmd_data = (gl_glDepthFunc_t *)((char *)message.data() + CMD_FIELD_SIZE);
            glDepthFunc(cmd_data->func);
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_glBlendFuncSeparate:
        {
            gl_glBlendFuncSeparate_t *cmd_data = (gl_glBlendFuncSeparate_t *)((char *)message.data() + CMD_FIELD_SIZE);
            glBlendFuncSeparate(cmd_data->sfactorRGB, cmd_data->dfactorRGB, cmd_data->sfactorAlpha, cmd_data->dfactorAlpha);
            break;
        }
        case (unsigned char)GL_Server_Command::GLSC_glVertexAttribDivisor:
        {
            gl_glVertexAttribDivisor_t *cmd_data = (gl_glVertexAttribDivisor_t *)((char *)message.data() + CMD_FIELD_SIZE);
            glVertexAttribDivisor(cmd_data->index, cmd_data->divisor);
            break;
        }
        case (unsigned char)GL_Server_Command::GLSC_glRenderbufferStorageMultisample:
        {
            gl_glRenderbufferStorageMultisample_t *cmd_data = (gl_glRenderbufferStorageMultisample_t *)((char *)message.data() + CMD_FIELD_SIZE);
            glRenderbufferStorageMultisample(cmd_data->target, cmd_data->samples,
                                             cmd_data->internalformat,
                                             cmd_data->width, cmd_data->height);
            break;
        }
        case (unsigned char)GL_Server_Command::GLSC_glDrawArraysInstanced:
        {
            gl_glDrawArraysInstanced_t *cmd_data = (gl_glDrawArraysInstanced_t *)((char *)message.data() + CMD_FIELD_SIZE);
            glDrawArraysInstanced(cmd_data->mode, cmd_data->first, cmd_data->count,
                                  cmd_data->instancecount);
            break;
        }
        case (unsigned int)GL_Server_Command::GLSC_bufferSwap:
        {
            double currentTime = glfwGetTime();
            numOfFrames++;
            if (currentTime - lastTime >= 1.0)
            {
                std::cout << "fps:" << numOfFrames << "\t" << (glGenBuffers_idx_map.size() + glGenVertexArrays_idx_map.size() + glGenTextures_idx_map.size() + glGenFramebuffers_idx_map.size() + glGenRenderbuffers_idx_map.size()) * 2 * sizeof(unsigned int) << std::endl;
                numOfFrames = 0;
                lastTime = currentTime;
            }
#if CACHE_EXPERIMENTS
            if (currentTime - lastTime >= 1.0)
            {

                std::cout << fccache_hit_count << "/" << ccache_hit_count << "/" << command_count_for_sec << "/" << command_count_for_sec - fccache_hit_count << "/" << command_cache.size() << "/" << longest_ccache_index << "/" << command_cache.cache_size() << std::endl;
                lastTime = currentTime;
                fccache_hit_count = 0;
                ccache_hit_count = 0;
                longest_ccache_index = 0;
                command_count_for_sec = -1;
            }
            // std::cout << "FC_CACHE_SIZE:" << fc_cache_size << std::endl;

#endif
            glfwSwapBuffers(window);
            glfwPollEvents();
#if LATENCY_EXPERIMENTS
            auto current_time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            std::cout << "LATENCY_RENDERING_END:" << current_time << "\tframe_number:" << frame_number << std::endl;
#endif
            hasReturn = true;

#if SEQUENCE_DEDUP_ENABLE
            prev_frame_hash_list.swap(current_frame_hash_list);
            std::vector<std::string>().swap(current_frame_hash_list);
#endif
            current_sequence_number = -1; // switch 이후에 ++연산을 하기때문에 -1로 초기화 함
            frame_number++;
            fc_cache_size = 0;
            break;
        }
        default:
        {
            //std::cout << c->cmd << std::endl;
            break;
        }
        }

        if (hasReturn)
            sock.send(ret, zmq::send_flags::none);
#if CACHE_EXPERIMENTS
        command_count_for_sec++;
#endif
        current_sequence_number++;
    }
}
