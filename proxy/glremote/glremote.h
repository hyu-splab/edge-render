// #include "../trie.h"
#include "../zmq_server.h"
#include "glad.h"
#include "lru_cache.hpp"
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#include <bitset>
#include <chrono>
#include <iostream>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <zmq.hpp>
#include <zmq_addon.hpp>

#define FRAME_BUFFER_ENABLE 0
#define SEQUENCE_DEDUP_ENABLE 0
#define COMMAND_DEDUP_ENABLE 0
#define ASYNC_BUFFER_BINDING 0
#define LATENCY_EXPERIMENTS 0 // LATENCY 측정을 위해 추가 됨
#define ASYNC_BUFFER_EXPERIMENTS 0 // ABB FRAME 실험

#define CACHE_ENTRY_SIZE 1000

#define GL_SET_COMMAND(PTR, FUNCNAME)                                                \
	gl_##FUNCNAME##_t *PTR = (gl_##FUNCNAME##_t *)malloc(sizeof(gl_##FUNCNAME##_t)); \
	// PTR->cmd = GLSC_##FUNCNAME

std::vector<std::size_t> current_frame_hash_list;
std::vector<std::size_t> prev_frame_hash_list;

lru11::Cache<std::string, std::string> command_cache("ccache", CACHE_ENTRY_SIZE, 0);
GLint global_pack_alignment = 4;
GLint global_unpack_alignment = 4;

int current_sequence_number = 0;

#if ASYNC_BUFFER_EXPERIMENTS
auto first_frame_time = std::chrono::steady_clock::now();
#endif

zmq::multipart_t frame_message_buffer;
