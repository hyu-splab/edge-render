#include "zmq_server.h"

std::mutex ZMQServer::_mutex;
ZMQServer *ZMQServer::_instance = nullptr;
int ZMQServer::lock_test;

void ZMQServer::log() {
	std::cout << ZMQServer::socket << std::endl;
}

void ZMQServer::init_zmq() {
	ZMQServer::socket = zmq::socket_t(ZMQServer::ctx, zmq::socket_type::pair);
	glGenBuffers_i = 0;
	glGenVertexArrays_i = 0;
	glGenTextures_i = 0;
	glGenFramebuffers_i = 0;
	glGenRenderbuffers_i = 0;
	glGenQueries_i = 0;
	glGenSamplers_i = 0;
	glGenTransformFeedbacks_i = 0;
}

void ZMQServer::add_count() {
	_mutex.lock();
	ZMQServer::lock_test++;
	_mutex.unlock();
}

int ZMQServer::get_count() {
	std::cout << &(ZMQServer::lock_test) << std::endl;
	return ZMQServer::lock_test;
}