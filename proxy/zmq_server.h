#ifndef ZMQ_SERVER_H
#define ZMQ_SERVER_H

#include <iostream>
#include <mutex>
#include <vector>
#include <zmq.hpp>
class ZMQServer {

	static ZMQServer *_instance;
	// thread safe 보장하려면 mutex가 필요해
	static std::mutex _mutex;
	static int lock_test;

	ZMQServer(){};

public:
	ZMQServer(const ZMQServer &other) = delete;
	static ZMQServer *get_instance() {
		if (_instance == nullptr) {
			// lock 을 걸면 다른 thread는 기다려요
			_mutex.lock();
			_instance = new ZMQServer();
			_mutex.unlock();
			// unlock 을 하면 이제 thread가 작업해요
		}
		return _instance;
	}
	unsigned int glGenBuffers_i;
	unsigned int glGenVertexArrays_i;
	unsigned int glGenTextures_i;
	unsigned int glGenFramebuffers_i;
	unsigned int glGenRenderbuffers_i;
	unsigned int glGenQueries_i;
	unsigned int glGenSamplers_i;
	unsigned int glGenTransformFeedbacks_i;

	zmq::socket_t socket;
	zmq::context_t ctx;
	std::mutex socket_mutex;
	static void release_instance() {
		if (_instance) {
			delete _instance;
			_instance = nullptr;
		}
	}
	void init_zmq();
	void log();
	void add_count();
	int get_count();
	// zmq::socket_t *get_socket();
};

#endif //ZMQ_SERVER_H