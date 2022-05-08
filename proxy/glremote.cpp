#include "gl_commands.h"

size_t fc_cache_size = 0;
size_t fmb_size = 0;
int frame_number = 0;
uint32_t calc_pixel_data_size(GLenum type, GLenum format, GLsizei width, GLsizei height) {
	uint32_t pixelbytes, linebytes, datasize;

	switch (type) {
		case GL_UNSIGNED_BYTE:
			switch (format) {
				case GL_ALPHA:
					pixelbytes = 1;
					break;
				case GL_RGB:
					pixelbytes = 3;
					break;
				case GL_RG:
					pixelbytes = 2;
					break;
				case GL_RGBA:
					pixelbytes = 4;
					break;
				case GL_LUMINANCE:
					pixelbytes = 1;
					break;
				case GL_LUMINANCE_ALPHA:
					pixelbytes = 2;
					break;
				default:
					pixelbytes = 4;
					break;
			}
			break;
		case GL_UNSIGNED_SHORT_5_6_5:
			pixelbytes = 2;
			break;
		case GL_UNSIGNED_SHORT_4_4_4_4:
			pixelbytes = 2;
			break;
		case GL_UNSIGNED_SHORT_5_5_5_1:
			pixelbytes = 2;
			break;
		default:
			pixelbytes = 4;
			break;
	}
	linebytes = (pixelbytes * width + global_unpack_alignment - 1) & (~(global_unpack_alignment - 1)); // 4 willbe replaced with pixelstorei param
	datasize = linebytes * height;
	return datasize;
}

void send_buffer() {
	ZMQServer *zmq_server = ZMQServer::get_instance();
	frame_message_buffer.send(zmq_server->socket);
}

std::bitset<16> create_cmd_field(std::bitset<14> cmd_bit, std::bitset<2> dedup_bit) {
	std::bitset<16> merged(cmd_bit.to_string() + dedup_bit.to_string());
	std::bitset<16> cmd_field{ merged };
	return cmd_field;
}
std::bitset<24> create_ccache_field(std::bitset<20> bucket_id, std::bitset<4> index) {
	std::bitset<24> merged(bucket_id.to_string() + index.to_string());
	std::bitset<24> ccache_field{ merged };
	return ccache_field;
}

std::bitset<2> initial_sequecne_deduplication(std::size_t hashed_message) {
	std::bitset<2> deduplication_bit(0);

	current_frame_hash_list.push_back(hashed_message);
	if (prev_frame_hash_list.size() > current_sequence_number) {
		if (prev_frame_hash_list[current_sequence_number] == hashed_message) {
			deduplication_bit = 2;
		}
	}
	return deduplication_bit;
}
bool individual_command_deduplication(std::string message, void *locator) {
	bool is_new = command_cache.tryFind(message);
	if (!is_new) { // 이미 존재하는 데이터 -> 버킷과 인덱스 전송
		command_cache.get_locator(message, locator);
		// return !is_new;
	}
	return is_new;
}
// 사실 이 함수는 메시지 생성 부분과 보내는 부분을 나눠야하지만, 매우 귀찮아서 하나로 뭉쳐놨음.
// 분리하고자 하면 switch 문 아래부터 분리하면 됨
std::string create_message(unsigned int cmd, void *non_pointer_param, size_t non_pointer_param_size, bool has_pointer_param) {
// Start of 메세지 생성 시간 측정
#if LATENCY_EXPERIMENTS || ASYNC_BUFFER_EXPERIMENTS
	// 여기에 시간 측정 코드 삽입
	if (current_sequence_number == 0) {
		auto first_frame_time = std::chrono::steady_clock::now();
		auto current_tiem = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		std::cout << "LATENCY_DEDUP_START:" << current_tiem << "\tframe_number:" << frame_number << std::endl;
	}
#endif

	/*
#if ASYNC_BUFFER_EXPERIMENTS
	if(current_sequence_number == 0)
		first_frame_time = std::chrono::steady_clock::now();
#endif
*/
	std::string message;
	message.resize(CMD_FIELD_SIZE + non_pointer_param_size);
	memcpy((void *)((char *)message.data() + CMD_FIELD_SIZE), non_pointer_param, non_pointer_param_size);
	if (has_pointer_param) {
		switch (cmd) {
			case (unsigned int)GL_Server_Command::GLSC_glShaderSource: {
				gl_glShaderSource_t *p_data = (gl_glShaderSource_t *)non_pointer_param;
				p_data->string_length = new GLuint[p_data->count];

				std::size_t shader_length = 0;
				for (int i = 0; i < p_data->count; i++) {
					shader_length += strlen(p_data->string[i]);
					if (strlen(p_data->string[i]) > 0) {
						message.resize(CMD_FIELD_SIZE + non_pointer_param_size + shader_length);
						memcpy((void *)((char *)message.data() + CMD_FIELD_SIZE + non_pointer_param_size + (shader_length - strlen(p_data->string[i]))), p_data->string[i], strlen(p_data->string[i]));
					}
					p_data->string_length[i] = strlen(p_data->string[i]);
				}
				message.resize(CMD_FIELD_SIZE + non_pointer_param_size + shader_length + sizeof(GLuint) * p_data->count);
				memcpy((void *)(char *)message.data() + CMD_FIELD_SIZE + non_pointer_param_size + shader_length, p_data->string_length, sizeof(GLuint) * p_data->count);
				break;
			}
			case (unsigned int)GL_Server_Command::GLSC_glTransformFeedbackVaryings: {
				gl_glTransformFeedbackVaryings_t *p_data = (gl_glTransformFeedbackVaryings_t *)non_pointer_param;
				p_data->string_length = new GLuint[p_data->count];

				std::size_t varyings_length = 0;
				for (int i = 0; i < p_data->count; i++) {
					varyings_length += strlen(p_data->varyings[i]);
					if (strlen(p_data->varyings[i]) > 0) {
						message.resize(CMD_FIELD_SIZE + non_pointer_param_size + varyings_length);
						memcpy((void *)((char *)message.data() + CMD_FIELD_SIZE + non_pointer_param_size + (varyings_length - strlen(p_data->varyings[i]))), p_data->varyings[i], strlen(p_data->varyings[i]));
					}
					p_data->string_length[i] = strlen(p_data->varyings[i]);
				}
				message.resize(CMD_FIELD_SIZE + non_pointer_param_size + varyings_length + sizeof(GLuint) * p_data->count);
				memcpy((void *)(char *)message.data() + CMD_FIELD_SIZE + non_pointer_param_size + varyings_length, p_data->string_length, sizeof(GLuint) * p_data->count);
				break;
			}
			case (unsigned int)GL_Server_Command::GLSC_glClearBufferfv: {
				size_t param_size = sizeof(GLfloat) * 4;
				message.resize(CMD_FIELD_SIZE + non_pointer_param_size + param_size);
				memcpy((void *)((char *)message.data() + CMD_FIELD_SIZE + non_pointer_param_size), (void *)(reinterpret_cast<gl_glClearBufferfv_t *>(non_pointer_param)->value), param_size);
				break;
			}
			case (unsigned int)GL_Server_Command::GLSC_glDeleteFramebuffers: {
				size_t param_size = reinterpret_cast<gl_glDeleteFramebuffers_t *>(non_pointer_param)->n * sizeof(GLuint);

				message.resize(CMD_FIELD_SIZE + non_pointer_param_size + param_size);
				memcpy((void *)((char *)message.data() + CMD_FIELD_SIZE + non_pointer_param_size), (void *)(reinterpret_cast<gl_glDeleteFramebuffers_t *>(non_pointer_param)->framebuffers), param_size);
				break;
			}
			case (unsigned int)GL_Server_Command::GLSC_glDeleteRenderbuffers: {
				size_t param_size = reinterpret_cast<gl_glDeleteRenderbuffers_t *>(non_pointer_param)->n * sizeof(GLuint);

				message.resize(CMD_FIELD_SIZE + non_pointer_param_size + param_size);
				memcpy((void *)((char *)message.data() + CMD_FIELD_SIZE + non_pointer_param_size), (void *)(reinterpret_cast<gl_glDeleteRenderbuffers_t *>(non_pointer_param)->renderbuffers), param_size);
				break;
			}
			case (unsigned int)GL_Server_Command::GLSC_glDeleteTextures: {
				size_t param_size = reinterpret_cast<gl_glDeleteTextures_t *>(non_pointer_param)->n * sizeof(GLuint);
				message.resize(CMD_FIELD_SIZE + non_pointer_param_size + param_size);
				memcpy((void *)((char *)message.data() + CMD_FIELD_SIZE + non_pointer_param_size), (void *)(reinterpret_cast<gl_glDeleteTextures_t *>(non_pointer_param)->textures), param_size);
				break;
			}
			case (unsigned int)GL_Server_Command::GLSC_glDeleteVertexArrays: {
				size_t param_size = reinterpret_cast<gl_glDeleteVertexArrays_t *>(non_pointer_param)->n * sizeof(GLuint);

				message.resize(CMD_FIELD_SIZE + non_pointer_param_size + param_size);
				memcpy((void *)((char *)message.data() + CMD_FIELD_SIZE + non_pointer_param_size), (void *)(reinterpret_cast<gl_glDeleteVertexArrays_t *>(non_pointer_param)->arrays), param_size);
				break;
			}
			case (unsigned int)GL_Server_Command::GLSC_glDeleteBuffers: {
				size_t param_size = reinterpret_cast<gl_glDeleteBuffers_t *>(non_pointer_param)->n * sizeof(GLuint);

				message.resize(CMD_FIELD_SIZE + non_pointer_param_size + param_size);
				memcpy((void *)((char *)message.data() + CMD_FIELD_SIZE + non_pointer_param_size), (void *)(reinterpret_cast<gl_glDeleteBuffers_t *>(non_pointer_param)->buffers), param_size);
				break;
			}
			case (unsigned int)GL_Server_Command::GLSC_glBufferData: {
				if (reinterpret_cast<gl_glBufferData_t *>(non_pointer_param)->data != NULL) {
					size_t param_size = reinterpret_cast<gl_glBufferData_t *>(non_pointer_param)->size;
					message.resize(CMD_FIELD_SIZE + non_pointer_param_size + param_size);
					memcpy((void *)((char *)message.data() + CMD_FIELD_SIZE + non_pointer_param_size), (void *)(reinterpret_cast<gl_glBufferData_t *>(non_pointer_param)->data), param_size);
				}
				break;
			}
			case (unsigned int)GL_Server_Command::GLSC_glBufferSubData: {
				if (reinterpret_cast<gl_glBufferSubData_t *>(non_pointer_param)->data != NULL) {
					size_t param_size = reinterpret_cast<gl_glBufferSubData_t *>(non_pointer_param)->size;
					message.resize(CMD_FIELD_SIZE + non_pointer_param_size + param_size);
					memcpy((void *)((char *)message.data() + CMD_FIELD_SIZE + non_pointer_param_size), (void *)(reinterpret_cast<gl_glBufferSubData_t *>(non_pointer_param)->data), param_size);
				}
				break;
			}
			case (unsigned int)GL_Server_Command::GLSC_glUniform1iv: {
				size_t param_size = reinterpret_cast<gl_glUniform1iv_t *>(non_pointer_param)->count * sizeof(GLint) * 1;
				message.resize(CMD_FIELD_SIZE + non_pointer_param_size + param_size);
				memcpy((void *)((char *)message.data() + CMD_FIELD_SIZE + non_pointer_param_size), (void *)(reinterpret_cast<gl_glUniform1iv_t *>(non_pointer_param)->value), param_size);
				break;
			}
			case (unsigned int)GL_Server_Command::GLSC_glUniform2fv: {
				size_t param_size = reinterpret_cast<gl_glUniform2fv_t *>(non_pointer_param)->count * sizeof(GLfloat) * 2;
				message.resize(CMD_FIELD_SIZE + non_pointer_param_size + param_size);
				memcpy((void *)((char *)message.data() + CMD_FIELD_SIZE + non_pointer_param_size), (void *)(reinterpret_cast<gl_glUniform2fv_t *>(non_pointer_param)->value), param_size);
				break;
			}
			case (unsigned int)GL_Server_Command::GLSC_glUniform4fv: {
				size_t param_size = reinterpret_cast<gl_glUniform4fv_t *>(non_pointer_param)->count * sizeof(GLfloat) * 4;
				message.resize(CMD_FIELD_SIZE + non_pointer_param_size + param_size);
				memcpy((void *)((char *)message.data() + CMD_FIELD_SIZE + non_pointer_param_size), (void *)(reinterpret_cast<gl_glUniform4fv_t *>(non_pointer_param)->value), param_size);
				break;
			}
			case (unsigned int)GL_Server_Command::GLSC_glUniformMatrix4fv: {
				size_t param_size = reinterpret_cast<gl_glUniformMatrix4fv_t *>(non_pointer_param)->count * sizeof(GLfloat) * 16;
				message.resize(CMD_FIELD_SIZE + non_pointer_param_size + param_size);
				memcpy((void *)((char *)message.data() + CMD_FIELD_SIZE + non_pointer_param_size), (void *)(reinterpret_cast<gl_glUniformMatrix4fv_t *>(non_pointer_param)->value), param_size);
				std::string pointer_param = message.substr(CMD_FIELD_SIZE + sizeof(gl_glUniformMatrix4fv_t), message.size() - (CMD_FIELD_SIZE + sizeof(gl_glUniformMatrix4fv_t)));

				break;
			}
			case (unsigned int)GL_Server_Command::GLSC_glVertexAttrib4fv: {
				if (reinterpret_cast<gl_glVertexAttrib4fv_t *>(non_pointer_param)->v != NULL) {
					size_t param_size = sizeof(GLfloat) * 4;
					message.resize(CMD_FIELD_SIZE + non_pointer_param_size + param_size);
					memcpy((void *)((char *)message.data() + CMD_FIELD_SIZE + non_pointer_param_size), (void *)(reinterpret_cast<gl_glVertexAttrib4fv_t *>(non_pointer_param)->v), param_size);
				}
				break;
			}
			case (unsigned int)GL_Server_Command::GLSC_glDrawBuffers: {
				size_t param_size = reinterpret_cast<gl_glDrawBuffers_t *>(non_pointer_param)->n * sizeof(GLenum);
				message.resize(CMD_FIELD_SIZE + non_pointer_param_size + param_size);
				memcpy((void *)((char *)message.data() + CMD_FIELD_SIZE + non_pointer_param_size), (void *)(reinterpret_cast<gl_glDrawBuffers_t *>(non_pointer_param)->bufs), param_size);
				break;
			}
			case (unsigned int)GL_Server_Command::GLSC_glGetAttribLocation: {
				size_t param_size = strlen(reinterpret_cast<gl_glGetAttribLocation_t *>(non_pointer_param)->name);
				message.resize(CMD_FIELD_SIZE + non_pointer_param_size + param_size);
				memcpy((void *)((char *)message.data() + CMD_FIELD_SIZE + non_pointer_param_size), (void *)(reinterpret_cast<gl_glGetAttribLocation_t *>(non_pointer_param)->name), param_size);
				break;
			}
			case (unsigned int)GL_Server_Command::GLSC_glGetUniformLocation: {
				size_t param_size = strlen(reinterpret_cast<gl_glGetUniformLocation_t *>(non_pointer_param)->name);
				message.resize(CMD_FIELD_SIZE + non_pointer_param_size + param_size);
				memcpy((void *)((char *)message.data() + CMD_FIELD_SIZE + non_pointer_param_size), (void *)(reinterpret_cast<gl_glGetUniformLocation_t *>(non_pointer_param)->name), param_size);
				break;
			}
			case (unsigned int)GL_Server_Command::GLSC_glGetUniformBlockIndex: {
				size_t param_size = strlen(reinterpret_cast<gl_glGetUniformBlockIndex_t *>(non_pointer_param)->name);
				message.resize(CMD_FIELD_SIZE + non_pointer_param_size + param_size);
				memcpy((void *)((char *)message.data() + CMD_FIELD_SIZE + non_pointer_param_size), (void *)(reinterpret_cast<gl_glGetUniformBlockIndex_t *>(non_pointer_param)->name), param_size);
				break;
			}
			case (unsigned int)GL_Server_Command::GLSC_glBindAttribLocation: {
				size_t param_size = strlen(reinterpret_cast<gl_glBindAttribLocation_t *>(non_pointer_param)->name);
				message.resize(CMD_FIELD_SIZE + non_pointer_param_size + param_size);
				memcpy((void *)((char *)message.data() + CMD_FIELD_SIZE + non_pointer_param_size), (void *)(reinterpret_cast<gl_glBindAttribLocation_t *>(non_pointer_param)->name), param_size);
				break;
			}
			case (unsigned int)GL_Server_Command::GLSC_glTexImage2D: {
				gl_glTexImage2D_t *p_data = (gl_glTexImage2D_t *)non_pointer_param;
				if (p_data->pixels != NULL) {
					uint32_t param_size = calc_pixel_data_size(p_data->type, p_data->format, p_data->width, p_data->height);
					message.resize(CMD_FIELD_SIZE + non_pointer_param_size + param_size);
					memcpy((void *)((char *)message.data() + CMD_FIELD_SIZE + non_pointer_param_size), (void *)(reinterpret_cast<gl_glTexImage2D_t *>(non_pointer_param)->pixels), param_size);
				}
				break;
			}
			case (unsigned int)GL_Server_Command::GLSC_glTexSubImage2D: {
				gl_glTexSubImage2D_t *p_data = (gl_glTexSubImage2D_t *)non_pointer_param;
				if (p_data->pixels != NULL) {
					uint32_t param_size = calc_pixel_data_size(p_data->type, p_data->format, p_data->width, p_data->height);
					message.resize(CMD_FIELD_SIZE + non_pointer_param_size + param_size);
					memcpy((void *)((char *)message.data() + CMD_FIELD_SIZE + non_pointer_param_size), (void *)(reinterpret_cast<gl_glTexSubImage2D_t *>(non_pointer_param)->pixels), param_size);
				}
				break;
			}
			case (unsigned int)GL_Server_Command::GLSC_glCompressedTexImage2D: {
				gl_glCompressedTexImage2D_t *p_data = (gl_glCompressedTexImage2D_t *)non_pointer_param;
				if (p_data->pixels != NULL) {
					size_t param_size = p_data->imageSize;
					message.resize(CMD_FIELD_SIZE + non_pointer_param_size + param_size);
					memcpy((void *)((char *)message.data() + CMD_FIELD_SIZE + non_pointer_param_size), (void *)(reinterpret_cast<gl_glCompressedTexImage2D_t *>(non_pointer_param)->pixels), param_size);
				}
				break;
			}
			case (unsigned int)GL_Server_Command::GLSC_glTexImage3D: {
				gl_glTexImage3D_t *p_data = (gl_glTexImage3D_t *)non_pointer_param;
				if (p_data->pixels != NULL) {
					uint32_t param_size = calc_pixel_data_size(p_data->type, p_data->format, p_data->width, p_data->height) * p_data->depth;
					message.resize(CMD_FIELD_SIZE + non_pointer_param_size + param_size);
					memcpy((void *)((char *)message.data() + CMD_FIELD_SIZE + non_pointer_param_size), (void *)(reinterpret_cast<gl_glTexImage3D_t *>(non_pointer_param)->pixels), param_size);
				}
				break;
			}
			default: {
				break;
			}
		}
	}

	std::bitset<14> cmd_bit(cmd);
	std::bitset<2> dedup_bit(0);
	std::bitset<16> cmd_field = create_cmd_field(cmd_bit, dedup_bit);
	memcpy((void *)message.data(), &cmd_field, CMD_FIELD_SIZE); //copy 2bytes
	fmb_size += message.size();
	std::size_t hashed_message = std::hash<std::string>{}(message);

#if SEQUENCE_DEDUP_ENABLE
	dedup_bit = initial_sequecne_deduplication(hashed_message);
	if (dedup_bit == 2) {
		cmd_field = create_cmd_field(cmd_bit, dedup_bit);
		message.resize(CMD_FIELD_SIZE);
		memcpy((void *)message.data(), &cmd_field, CMD_FIELD_SIZE); //copy 2bytes
	}
#endif

#if COMMAND_DEDUP_ENABLE
	if (dedup_bit == 0) {
		CCacheLocator locator = { 0, 0 };
		bool is_new = individual_command_deduplication(message, &locator);
		if (is_new) {
			dedup_bit = 0;
			command_cache.insert(message, message);
		} else {
			dedup_bit = 3;
			cmd_field = create_cmd_field(cmd_bit, dedup_bit);
			command_cache.update(message);
			message.resize(CMD_FIELD_SIZE + CCACHE_FIELD_SIZE);
			memcpy((void *)message.data(), &cmd_field, CMD_FIELD_SIZE); //copy 2bytes
			memcpy((void *)message.data() + CMD_FIELD_SIZE, &locator, CCACHE_FIELD_SIZE); //copy 3bytes
		}
	}
#endif
#if ASYNC_BUFFER_EXPERIMENTS
	if (current_sequence_number == 0) {
		auto current_time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		std::cout << "LATENCY_FMB_START:" << current_time << "\tframe_number:" << frame_number << std::endl;
	}
#endif
	if (dedup_bit == 2) {
		frame_message_buffer.push_back(zmq::message_t());
	} else {
		frame_message_buffer.push_back(zmq::message_t(message.data(), message.size()));
	}

	current_sequence_number++;
	return message;
}

void glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
	GL_SET_COMMAND(c, glViewport);
	c->x = x;
	c->y = y;
	c->width = width;
	c->height = height;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glViewport, (void *)c, sizeof(gl_glViewport_t), false);

#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif
}

void glClear(GLbitfield mask) {
	GL_SET_COMMAND(c, glClear);
	c->mask = mask;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glClear, (void *)c, sizeof(gl_glClear_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif
}

void glBegin(GLenum mode) {
	GL_SET_COMMAND(c, glBegin);
	c->mode = mode;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glBegin, (void *)c, sizeof(gl_glBegin_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif
}
void glColor3f(GLfloat red, GLfloat green, GLfloat blue) {
	GL_SET_COMMAND(c, glColor3f);
	c->red = red;
	c->green = green;
	c->blue = blue;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glColor3f, (void *)c, sizeof(gl_glColor3f_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif
}
void glVertex3f(GLfloat x, GLfloat y, GLfloat z) {
	GL_SET_COMMAND(c, glVertex3f);
	c->x = x;
	c->y = y;
	c->z = z;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glVertex3f, (void *)c, sizeof(gl_glVertex3f_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif

	// send_data((unsigned char)GL_Server_Command::GLSC_glVertex3f, (void *)c, sizeof(gl_glVertex3f_t));
}

void glEnd() {
	GL_SET_COMMAND(c, glEnd);
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glEnd, (void *)c, sizeof(gl_glEnd_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif

	// send_data((unsigned char)GL_Server_Command::GLSC_glEnd, (void *)c, sizeof(gl_glEnd_t));
}

void glFlush() {
	GL_SET_COMMAND(c, glFlush);
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glFlush, (void *)c, sizeof(gl_glFlush_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif

	//send_data((unsigned char)GL_Server_Command::GLSC_glFlush, (void *)c, sizeof(gl_glFlush_t));
}

void glBreak() {
	gl_command_t *c = (gl_command_t *)malloc(sizeof(gl_command_t));
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_BREAK, (void *)c, sizeof(gl_glBreak_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif

	// send_data((unsigned char)GL_Server_Command::GLSC_BREAK, (void *)c, sizeof(gl_glBreak_t));
}
void glSwapBuffer() {
	gl_command_t *c = (gl_command_t *)malloc(sizeof(gl_command_t));
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_bufferSwap, (void *)c, sizeof(gl_glSwapBuffer_t), false);

#if LATENCY_EXPERIMENTS
	auto current_time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	std::cout << "LATENCY_DEDUP_END:" << current_time << "\tframe_nubmer:" << frame_number << std::endl;
#endif

	send_buffer();
	zmq::message_t result;
	ZMQServer *zmq_server = ZMQServer::get_instance();
	zmq_server->socket.recv(result, zmq::recv_flags::none);

	// init variables
	prev_frame_hash_list.swap(current_frame_hash_list);
	std::vector<std::size_t>().swap(current_frame_hash_list);
	// #if ASYNC_BUFFER_EXPERIMENTS
	// 	auto last_frame_time = std::chrono::steady_clock::now();
	// 	std::cout << "ABB: " << std::chrono::duration_cast<std::chrono::microseconds>(last_frame_time - first_frame_time).count() << std::endl;
	// 	// first_frame_time = std::chrono::steady_clock::now();
	// #endif

	current_sequence_number = 0;
	fmb_size = 0;
	frame_number++;
}
GLuint glCreateShader(GLenum type) {
	GL_SET_COMMAND(c, glCreateShader);
	c->type = type;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glCreateShader, (void *)c, sizeof(gl_glCreateShader_t), false);
	send_buffer();
	zmq::message_t result;
	ZMQServer *zmq_server = ZMQServer::get_instance();
	zmq_server->socket.recv(result, zmq::recv_flags::none);
	GLuint *ret = (GLuint *)result.data();
	return (GLuint)*ret;
}
GLenum glGetError(void) {
	GL_SET_COMMAND(c, glGetError);

	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glGetError, (void *)c, sizeof(gl_glGetError_t), false);
	send_buffer();
	zmq::message_t result;
	ZMQServer *zmq_server = ZMQServer::get_instance();
	zmq_server->socket.recv(result, zmq::recv_flags::none);
	GLenum *ret = (GLenum *)result.data();
	return static_cast<GLenum>(*ret);
}

void glShaderSource(GLuint shader, GLuint count, const GLchar *const *string, const GLint *length) {
	GL_SET_COMMAND(c, glShaderSource);

	c->shader = shader;
	c->count = count;
	c->string = string;
	c->length = length;

	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glShaderSource, (void *)c, sizeof(gl_glShaderSource_t), true);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif

	// send_data((unsigned char)GL_Server_Command::GLSC_glShaderSource, (void *)c, sizeof(gl_glShaderSource_t));
	//SEND_MORE
}
void glCompileShader(GLuint shader) {
	GL_SET_COMMAND(c, glCompileShader);

	c->shader = shader;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glCompileShader, (void *)c, sizeof(gl_glCompileShader_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif
}

void glGetShaderiv(GLuint shader, GLenum pname, GLint *param) {
	GL_SET_COMMAND(c, glGetShaderiv);

	c->shader = shader;
	c->pname = pname;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glGetShaderiv, (void *)c, sizeof(gl_glGetShaderiv_t), false);
	send_buffer();
	zmq::message_t result;
	ZMQServer *zmq_server = ZMQServer::get_instance();
	zmq_server->socket.recv(result, zmq::recv_flags::none);

	GLint *ret = (GLint *)result.data();
	memcpy((void *)param, (void *)ret, sizeof(GLuint));
}

GLuint glCreateProgram() {
	GL_SET_COMMAND(c, glCreateProgram);

	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glCreateProgram, (void *)c, sizeof(gl_glCreateProgram_t), false);
	send_buffer();
	zmq::message_t result;
	ZMQServer *zmq_server = ZMQServer::get_instance();
	zmq_server->socket.recv(result, zmq::recv_flags::none);

	GLuint *ret = (GLuint *)result.data();
	return *(GLuint *)ret;
}

void glAttachShader(GLuint program, GLuint shader) {
	GL_SET_COMMAND(c, glAttachShader);

	c->program = program;
	c->shader = shader;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glAttachShader, (void *)c, sizeof(gl_glAttachShader_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif
}

void glLinkProgram(GLuint program) {
	GL_SET_COMMAND(c, glLinkProgram);

	c->program = program;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glLinkProgram, (void *)c, sizeof(gl_glLinkProgram_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif
}

void glGetProgramiv(GLuint program, GLenum pname, GLint *param) {
	GL_SET_COMMAND(c, glGetProgramiv);

	c->program = program;
	c->pname = pname;

	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glCreateProgram, (void *)c, sizeof(gl_glCreateProgram_t), false);
	send_buffer();

	zmq::message_t result;
	ZMQServer *zmq_server = ZMQServer::get_instance();
	zmq_server->socket.recv(result, zmq::recv_flags::none);

	GLint *ret = (GLint *)result.data();
	memcpy((void *)param, (void *)ret, sizeof(GLint));
}

void glGenBuffers(GLsizei n, GLuint *buffers) {
	GL_SET_COMMAND(c, glGenBuffers);
	c->n = n;
	ZMQServer *zmq_server = ZMQServer::get_instance();
	GLuint *indices = new GLuint[n];
#if ASYNC_BUFFER_BINDING
	for (int i = 0; i < n; i++) {
		zmq_server->glGenBuffers_i++;
		indices[i] = zmq_server->glGenBuffers_i;
	}
	c->last_index = indices[n - 1];
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glGenBuffers, (void *)c, sizeof(gl_glGenBuffers_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif
#else
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glGenBuffers, (void *)c, sizeof(gl_glGenBuffers_t), false);
	send_buffer();
	zmq::message_t result;
	zmq_server->socket.recv(result, zmq::recv_flags::none);
	memcpy(indices, result.data(), sizeof(GLuint) * n);
#endif
	memcpy((void *)buffers, (void *)indices, sizeof(GLuint) * n);
}

void glBindBuffer(GLenum target, GLuint id) {
	GL_SET_COMMAND(c, glBindBuffer);

	c->target = target;
	c->id = id;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glBindBuffer, (void *)c, sizeof(gl_glBindBuffer_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif
}

void glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage) {
	GL_SET_COMMAND(c, glBufferData);

	c->target = target;
	c->size = size;
	c->data = data;
	c->usage = usage;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glBufferData, (void *)c, sizeof(gl_glBufferData_t), true);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif
}

void glGenVertexArrays(GLsizei n, GLuint *arrays) {
	GL_SET_COMMAND(c, glGenVertexArrays);
	c->n = n;
	ZMQServer *zmq_server = ZMQServer::get_instance();
	GLuint *indices = new GLuint[n];
#if ASYNC_BUFFER_BINDING
	for (int i = 0; i < n; i++) {
		zmq_server->glGenVertexArrays_i++;
		indices[i] = zmq_server->glGenVertexArrays_i;
	}
	c->last_index = indices[n - 1];
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glGenVertexArrays, (void *)c, sizeof(gl_glGenVertexArrays_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif
#else
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glGenVertexArrays, (void *)c, sizeof(gl_glGenVertexArrays_t), false);
	send_buffer();
	zmq::message_t result;
	zmq_server->socket.recv(result, zmq::recv_flags::none);
	memcpy(indices, result.data(), sizeof(GLuint) * n);
#endif
	memcpy((void *)arrays, (void *)indices, sizeof(GLuint) * n);
}

void glBindVertexArray(GLuint array) {
	GL_SET_COMMAND(c, glBindVertexArray);

	c->array = array;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glBindVertexArray, (void *)c, sizeof(gl_glBindVertexArray_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif
}

GLint glGetAttribLocation(GLuint programObj, const GLchar *name) {
	GL_SET_COMMAND(c, glGetAttribLocation);

	c->programObj = programObj;
	c->name = name;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glGetAttribLocation, (void *)c, sizeof(gl_glGetAttribLocation_t), true);

	send_buffer();
	ZMQServer *zmq_server = ZMQServer::get_instance();
	zmq::message_t result;
	zmq_server->socket.recv(result, zmq::recv_flags::none);
	GLint *ret = (GLint *)result.data();
	return (GLint)*ret;
}
void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer) {
	GL_SET_COMMAND(c, glVertexAttribPointer);

	c->index = index;
	c->size = size;
	c->type = type;
	c->normalized = normalized;
	c->stride = stride;
	c->pointer = (int64_t)pointer;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glVertexAttribPointer, (void *)c, sizeof(gl_glVertexAttribPointer_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif
}

void glEnableVertexAttribArray(GLuint index) {
	GL_SET_COMMAND(c, glEnableVertexAttribArray);

	c->index = index;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glEnableVertexAttribArray, (void *)c, sizeof(gl_glEnableVertexAttribArray_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif
}
void glUseProgram(GLuint program) {
	GL_SET_COMMAND(c, glUseProgram);

	c->program = program;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glUseProgram, (void *)c, sizeof(gl_glUseProgram_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif
}

void glClearColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) {
	GL_SET_COMMAND(c, glClearColor);

	c->red = red;
	c->green = green;
	c->blue = blue;
	c->alpha = alpha;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glClearColor, (void *)c, sizeof(gl_glClearColor_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif
}
void glDrawArrays(GLenum mode, GLint first, GLsizei count) {
	GL_SET_COMMAND(c, glDrawArrays);

	c->mode = mode;
	c->first = first;
	c->count = count;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glDrawArrays, (void *)c, sizeof(gl_glDrawArrays_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif
}
void glScissor(GLint x, GLint y, GLsizei width, GLsizei height) {
	GL_SET_COMMAND(c, glScissor);

	c->x = x;
	c->y = y;
	c->width = width;
	c->height = height;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glScissor, (void *)c, sizeof(gl_glScissor_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif
}
void glGetIntegerv(GLenum pname, GLint *data) {
	GL_SET_COMMAND(c, glGetIntegerv);
	c->pname = pname;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glGetIntegerv, (void *)c, sizeof(gl_glGetIntegerv_t), false);
	send_buffer();

	ZMQServer *zmq_server = ZMQServer::get_instance();
	zmq::message_t result;
	zmq_server->socket.recv(result, zmq::recv_flags::none);
	GLint *ret = (GLint *)result.data();
	memcpy((void *)data, (void *)ret, sizeof(GLint));
}
void glGetFloatv(GLenum pname, GLfloat *data) {
	GL_SET_COMMAND(c, glGetFloatv);
	c->pname = pname;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glGetFloatv, (void *)c, sizeof(gl_glGetFloatv_t), false);
	send_buffer();

	ZMQServer *zmq_server = ZMQServer::get_instance();
	zmq::message_t result;
	zmq_server->socket.recv(result, zmq::recv_flags::none);
	GLfloat *ret = (GLfloat *)result.data();
	memcpy((void *)data, (void *)ret, sizeof(GLfloat));
}
void glGenTextures(GLsizei n, GLuint *textures) {
	GL_SET_COMMAND(c, glGenTextures);
	c->n = n;
	ZMQServer *zmq_server = ZMQServer::get_instance();
	GLuint *indices = new GLuint[n];
#if ASYNC_BUFFER_BINDING
	for (int i = 0; i < n; i++) {
		zmq_server->glGenTextures_i++;
		indices[i] = zmq_server->glGenTextures_i;
	}
	c->last_index = indices[n - 1];
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glGenTextures, (void *)c, sizeof(gl_glGenTextures_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif
#else
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glGenTextures, (void *)c, sizeof(gl_glGenTextures_t), false);
	send_buffer();
	zmq::message_t result;
	zmq_server->socket.recv(result, zmq::recv_flags::none);
	memcpy(indices, result.data(), sizeof(GLuint) * n);
#endif
	memcpy((void *)textures, (void *)indices, sizeof(GLuint) * n);
}
void glActiveTexture(GLenum texture) {
	GL_SET_COMMAND(c, glActiveTexture);
	c->texture = texture;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glActiveTexture, (void *)c, sizeof(gl_glActiveTexture_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif
}
void glBindTexture(GLenum target, GLuint texture) {
	GL_SET_COMMAND(c, glBindTexture);
	c->target = target;
	c->texture = texture;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glBindTexture, (void *)c, sizeof(gl_glBindTexture_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif
}
void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void *pixels) {
	GL_SET_COMMAND(c, glTexImage2D);

	c->target = target;
	c->level = level;
	c->internalformat = internalformat;
	c->width = width;
	c->height = height;
	c->border = border;
	c->format = format;
	c->type = type;
	c->pixels = pixels;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glTexImage2D, (void *)c, sizeof(gl_glTexImage2D_t), true);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif
}
void glTexStorage2D(GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height) {
	GL_SET_COMMAND(c, glTexStorage2D);

	c->target = target;
	c->levels = levels;
	c->internalformat = internalformat;
	c->width = width;
	c->height = height;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glTexStorage2D, (void *)c, sizeof(gl_glTexStorage2D_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif
}
void glTexParameteri(GLenum target, GLenum pname, GLint param) {
	GL_SET_COMMAND(c, glTexParameteri);

	c->target = target;
	c->pname = pname;
	c->param = param;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glTexParameteri, (void *)c, sizeof(gl_glTexParameteri_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif
}
void glGenFramebuffers(GLsizei n, GLuint *framebuffers) {
	GL_SET_COMMAND(c, glGenFramebuffers);
	c->n = n;
	ZMQServer *zmq_server = ZMQServer::get_instance();
	GLuint *indices = new GLuint[n];
#if ASYNC_BUFFER_BINDING
	for (int i = 0; i < n; i++) {
		zmq_server->glGenFramebuffers_i++;
		indices[i] = zmq_server->glGenFramebuffers_i;
	}
	c->last_index = indices[n - 1];
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glGenFramebuffers, (void *)c, sizeof(gl_glGenFramebuffers_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif
#else
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glGenFramebuffers, (void *)c, sizeof(gl_glGenFramebuffers_t), false);
	send_buffer();
	zmq::message_t result;
	zmq_server->socket.recv(result, zmq::recv_flags::none);
	GLuint *ret = (GLuint *)result.data();
	memcpy(indices, result.data(), sizeof(GLuint) * n);
#endif
	memcpy((void *)framebuffers, (void *)indices, sizeof(GLuint) * n);
}
void glBindFramebuffer(GLenum target, GLuint framebuffer) {
	GL_SET_COMMAND(c, glBindFramebuffer);

	c->target = target;
	c->framebuffer = framebuffer;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glBindFramebuffer, (void *)c, sizeof(gl_glBindFramebuffer_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif
}

void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) {
	GL_SET_COMMAND(c, glFramebufferTexture2D);

	c->target = target;
	c->attachment = attachment;
	c->textarget = textarget;
	c->texture = texture;
	c->level = level;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glFramebufferTexture2D, (void *)c, sizeof(gl_glFramebufferTexture2D_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif
}
GLenum glCheckFramebufferStatus(GLenum target) {
	GL_SET_COMMAND(c, glCheckFramebufferStatus);

	c->target = target;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glCheckFramebufferStatus, (void *)c, sizeof(gl_glCheckFramebufferStatus_t), false);
	send_buffer();

	ZMQServer *zmq_server = ZMQServer::get_instance();
	zmq::message_t result;
	zmq_server->socket.recv(result, zmq::recv_flags::none);

	GLenum *ret = (GLenum *)result.data();
	return (GLenum)*ret;
}
void glDisable(GLenum cap) {
	GL_SET_COMMAND(c, glDisable);

	c->cap = cap;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glDisable, (void *)c, sizeof(gl_glDisable_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif
}
void glTexImage3D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLenum format, GLenum type, const void *pixels) {
	GL_SET_COMMAND(c, glTexImage3D);

	c->target = target;
	c->level = level;
	c->internalformat = internalformat;
	c->width = width;
	c->height = height;
	c->depth = depth;
	c->border = border;
	c->format = format;
	c->type = type;
	c->pixels = pixels;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glTexImage3D, (void *)c, sizeof(gl_glTexImage3D_t), true);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif
}

// just defined functions
void glBindAttribLocation(GLuint program, GLuint index, const GLchar *name) {
	GL_SET_COMMAND(c, glBindAttribLocation);

	c->program = program;
	c->name = name;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glBindAttribLocation, (void *)c, sizeof(gl_glBindAttribLocation_t), true);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif

	// send_data((unsigned char)GL_Server_Command::GLSC_glBindAttribLocation, (void *)c, sizeof(gl_glBindAttribLocation_t));
}
void glBindRenderbuffer(GLenum target, GLuint renderbuffer) {
	GL_SET_COMMAND(c, glBindRenderbuffer);

	c->target = target;
	c->renderbuffer = renderbuffer;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glBindRenderbuffer, (void *)c, sizeof(gl_glBindRenderbuffer_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif

	// send_data((unsigned char)GL_Server_Command::GLSC_glBindRenderbuffer, (void *)c, sizeof(gl_glBindRenderbuffer_t));
}
void glBlendColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) {
	std::cout << __func__ << std::endl;
}
void glBlendEquation(GLenum mode) {
	GL_SET_COMMAND(c, glBlendEquation);

	c->mode = mode;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glBlendEquation, (void *)c, sizeof(gl_glBlendEquation_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif

	// send_data((unsigned char)GL_Server_Command::GLSC_glBlendEquation, (void *)c, sizeof(gl_glBlendEquation_t));
}
void glBlendEquationSeparate(GLenum modeRGB, GLenum modeAlpha) {
	std::cout << __func__ << std::endl;
}
void glBlendFunc(GLenum sfactor, GLenum dfactor) {
	GL_SET_COMMAND(c, glBlendFunc);

	c->sfactor = sfactor;
	c->dfactor = dfactor;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glBlendFunc, (void *)c, sizeof(gl_glBlendFunc_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif

	// send_data((unsigned char)GL_Server_Command::GLSC_glBlendFunc, (void *)c, sizeof(gl_glBlendFunc_t));
}
void glBlendFuncSeparate(GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha, GLenum dfactorAlpha) {
	GL_SET_COMMAND(c, glBlendFuncSeparate);

	c->sfactorRGB = sfactorRGB;
	c->dfactorRGB = dfactorRGB;
	c->sfactorAlpha = sfactorAlpha;
	c->dfactorAlpha = dfactorAlpha;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glBlendFuncSeparate, (void *)c, sizeof(gl_glBlendFuncSeparate_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif

	// send_data((unsigned char)GL_Server_Command::GLSC_glBlendFuncSeparate, (void *)c, sizeof(gl_glBlendFuncSeparate_t));
}
void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void *data) {
	GL_SET_COMMAND(c, glBufferSubData);

	c->target = target;
	c->offset = offset;
	c->size = size;
	c->data = data;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glBufferSubData, (void *)c, sizeof(gl_glBufferSubData_t), true);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif

	// send_data((unsigned char)GL_Server_Command::GLSC_glBufferSubData, (void *)c, sizeof(gl_glBufferSubData_t));
}

void glClearDepthf(GLfloat d) {
	GL_SET_COMMAND(c, glClearDepthf);

	c->d = d;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glClearDepthf, (void *)c, sizeof(gl_glClearDepthf_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif

	// send_data((unsigned char)GL_Server_Command::GLSC_glClearDepthf, (void *)c, sizeof(gl_glClearDepthf_t));
}
void glClearStencil(GLint s) {
	std::cout << __func__ << std::endl;
}
void glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha) {
	GL_SET_COMMAND(c, glColorMask);

	c->red = red;
	c->green = green;
	c->blue = blue;
	c->alpha = alpha;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glColorMask, (void *)c, sizeof(gl_glColorMask_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif

	// send_data((unsigned char)GL_Server_Command::GLSC_glColorMask, (void *)c, sizeof(gl_glColorMask_t));
}
void glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const void *data) {
	GL_SET_COMMAND(c, glCompressedTexImage2D);

	c->target = target;
	c->level = level;
	c->internalformat = internalformat;
	c->width = width;
	c->height = height;
	c->border = border;
	c->imageSize = imageSize;
	c->pixels = data;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glCompressedTexImage2D, (void *)c, sizeof(gl_glCompressedTexImage2D_t), true);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif

	// send_data((unsigned char)GL_Server_Command::GLSC_glCompressedTexImage2D, (void *)c, sizeof(gl_glCompressedTexImage2D_t));
}
void glCompressedTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const void *data) {

	std::cout << __func__ << std::endl;
}
void glCopyTexImage2D(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border) {
	std::cout << __func__ << std::endl;
}
void glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height) {
	std::cout << __func__ << std::endl;
}
void glCullFace(GLenum mode) {
	GL_SET_COMMAND(c, glCullFace);

	c->mode = mode;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glCullFace, (void *)c, sizeof(gl_glCullFace_t), true);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif

	// send_data((unsigned char)GL_Server_Command::GLSC_glCullFace, (void *)c, sizeof(gl_glCullFace_t));
}
void glDeleteBuffers(GLsizei n, const GLuint *buffers) {
	GL_SET_COMMAND(c, glDeleteBuffers);

	c->n = n;
	c->buffers = buffers;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glDeleteBuffers, (void *)c, sizeof(gl_glDeleteBuffers_t), true);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif

	// send_data((unsigned char)GL_Server_Command::GLSC_glDeleteBuffers, (void *)c, sizeof(gl_glDeleteBuffers_t));
}
void glDeleteFramebuffers(GLsizei n, const GLuint *framebuffers) {
	GL_SET_COMMAND(c, glDeleteFramebuffers);

	c->n = n;
	c->framebuffers = framebuffers;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glDeleteFramebuffers, (void *)c, sizeof(gl_glDeleteFramebuffers_t), true);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif

	// send_data((unsigned char)GL_Server_Command::GLSC_glDeleteFramebuffers, (void *)c, sizeof(gl_glDeleteFramebuffers_t));
}
void glDeleteProgram(GLuint program) {
}
void glDeleteRenderbuffers(GLsizei n, const GLuint *renderbuffers) {
	GL_SET_COMMAND(c, glDeleteRenderbuffers);

	c->n = n;
	c->renderbuffers = renderbuffers;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glDeleteRenderbuffers, (void *)c, sizeof(gl_glDeleteRenderbuffers_t), true);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif

	// send_data((unsigned char)GL_Server_Command::GLSC_glDeleteRenderbuffers, (void *)c, sizeof(gl_glDeleteRenderbuffers_t));
}
void glDeleteShader(GLuint shader) {
}
void glDeleteTextures(GLsizei n, const GLuint *textures) {
	GL_SET_COMMAND(c, glDeleteTextures);
	c->n = n;
	c->textures = textures;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glDeleteTextures, (void *)c, sizeof(gl_glDeleteTextures_t), true);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif

	// send_data((unsigned char)GL_Server_Command::GLSC_glDeleteTextures, (void *)c, sizeof(gl_glDeleteTextures_t));
}
void glDepthFunc(GLenum func) {
	GL_SET_COMMAND(c, glDepthFunc);

	c->func = func;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glDepthFunc, (void *)c, sizeof(gl_glDepthFunc_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif

	// send_data((unsigned char)GL_Server_Command::GLSC_glDepthFunc, (void *)c, sizeof(gl_glDepthFunc_t));
}
void glDepthMask(GLboolean flag) {
	GL_SET_COMMAND(c, glDepthMask);

	c->flag = flag;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glDepthMask, (void *)c, sizeof(gl_glDepthMask_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif

	// send_data((unsigned char)GL_Server_Command::GLSC_glDepthMask, (void *)c, sizeof(gl_glDepthMask_t));
}
void glDepthRangef(GLfloat n, GLfloat f) {
	std::cout << __func__ << std::endl;
}
void glDetachShader(GLuint program, GLuint shader) {
	std::cout << __func__ << std::endl;
}
void glDisableVertexAttribArray(GLuint index) {
	GL_SET_COMMAND(c, glDisableVertexAttribArray);

	c->index = index;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glDisableVertexAttribArray, (void *)c, sizeof(gl_glDisableVertexAttribArray_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif

	// send_data((unsigned char)GL_Server_Command::GLSC_glDisableVertexAttribArray, (void *)c, sizeof(gl_glDisableVertexAttribArray_t));
}
void glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices) {
	GL_SET_COMMAND(c, glDrawElements);

	c->mode = mode;
	c->count = count;
	c->type = type;
	c->indices = (int64_t)indices;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glDrawElements, (void *)c, sizeof(gl_glDrawElements_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif

	// std::cout << (int64_t)indices << std::endl;
	// send_data((unsigned char)GL_Server_Command::GLSC_glDrawElements, (void *)c, sizeof(gl_glDrawElements_t));
}
void glEnable(GLenum cap) {
	GL_SET_COMMAND(c, glEnable);

	c->cap = cap;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glEnable, (void *)c, sizeof(gl_glEnable_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif

	// send_data((unsigned char)GL_Server_Command::GLSC_glEnable, (void *)c, sizeof(gl_glEnable_t));
}
void glFinish(void) {
	std::cout << __func__ << std::endl;
}
void glFramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer) {
	GL_SET_COMMAND(c, glFramebufferRenderbuffer);

	c->target = target;
	c->attachment = attachment;
	c->renderbuffertarget = renderbuffertarget;
	c->renderbuffer = renderbuffer;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glFramebufferRenderbuffer, (void *)c, sizeof(gl_glFramebufferRenderbuffer_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif

	// send_data((unsigned char)GL_Server_Command::GLSC_glFramebufferRenderbuffer, (void *)c, sizeof(gl_glFramebufferRenderbuffer_t));
}
void glGenerateMipmap(GLenum target) {
	GL_SET_COMMAND(c, glGenerateMipmap);

	c->target = target;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glGenerateMipmap, (void *)c, sizeof(gl_glGenerateMipmap_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif

	// send_data((unsigned char)GL_Server_Command::GLSC_glGenerateMipmap, (void *)c, sizeof(gl_glGenerateMipmap_t));
}
void glFrontFace(GLenum mode) {
	GL_SET_COMMAND(c, glFrontFace);

	c->mode = mode;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glFrontFace, (void *)c, sizeof(gl_glFrontFace_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif

	// send_data((unsigned char)GL_Server_Command::GLSC_glFrontFace, (void *)c, sizeof(gl_glFrontFace_t));
}
void glGenRenderbuffers(GLsizei n, GLuint *renderbuffers) {
	auto start = std::chrono::steady_clock::now();
	GL_SET_COMMAND(c, glGenRenderbuffers);
	c->n = n;
	ZMQServer *zmq_server = ZMQServer::get_instance();
	GLuint *indices = new GLuint[n];
#if ASYNC_BUFFER_BINDING
	for (int i = 0; i < n; i++) {
		zmq_server->glGenRenderbuffers_i++;
		indices[i] = zmq_server->glGenRenderbuffers_i;
	}
	c->last_index = indices[n - 1];
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glGenRenderbuffers, (void *)c, sizeof(gl_glGenRenderbuffers_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif
#else
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glGenRenderbuffers, (void *)c, sizeof(gl_glGenRenderbuffers_t), false);
	send_buffer();
	zmq::message_t result;
	zmq_server->socket.recv(result, zmq::recv_flags::none);
	memcpy(indices, result.data(), sizeof(GLuint) * n);
#endif
	memcpy((void *)renderbuffers, (void *)indices, sizeof(GLuint) * n);
}
void glGetActiveAttrib(GLuint program, GLuint index, GLsizei bufSize, GLsizei *length, GLint *size, GLenum *type, GLchar *name) {
	std::cout << __func__ << std::endl;
}
void glGetActiveUniform(GLuint program, GLuint index, GLsizei bufSize, GLsizei *length, GLint *size, GLenum *type, GLchar *name) {
	std::cout << __func__ << std::endl;
}
void glGetAttachedShaders(GLuint program, GLsizei maxCount, GLsizei *count, GLuint *shaders) {
	std::cout << __func__ << std::endl;
}
void glGetBooleanv(GLenum pname, GLboolean *data) {
	std::cout << __func__ << std::endl;
}
void glGetBufferParameteriv(GLenum target, GLenum pname, GLint *params) {
	std::cout << __func__ << std::endl;
}
void glGetFramebufferAttachmentParameteriv(GLenum target, GLenum attachment, GLenum pname, GLint *params) {
	std::cout << __func__ << std::endl;
}
void glGetProgramInfoLog(GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog) {
	std::cout << __func__ << std::endl;
}
void glGetRenderbufferParameteriv(GLenum target, GLenum pname, GLint *params) {
	std::cout << __func__ << std::endl;
}
void glGetShaderInfoLog(GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog) {
	std::cout << __func__ << std::endl;
}
void glGetShaderPrecisionFormat(GLenum shadertype, GLenum precisiontype, GLint *range, GLint *precision) {
	std::cout << __func__ << std::endl;
}
void glGetShaderSource(GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *source) {
	std::cout << __func__ << std::endl;
}
const GLubyte *glGetString(GLenum name) {
	GL_SET_COMMAND(c, glGetString);
	c->name = name;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glGetString, (void *)c, sizeof(gl_glGetString_t), false);

	// zmq::message_t result = send_data((unsigned char)GL_Server_Command::GLSC_glGetAttribLocation, (void *)c, sizeof(gl_glGetAttribLocation_t), true);
	ZMQServer *zmq_server = ZMQServer::get_instance();
	send_buffer();
	zmq::message_t result;
	zmq_server->socket.recv(result, zmq::recv_flags::none);
	std::string ret = result.to_string();
	return reinterpret_cast<const GLubyte *>(ret.c_str());
}
void glGetTexParameterfv(GLenum target, GLenum pname, GLfloat *params) {

	std::cout << __func__ << std::endl;
}
void glGetTexParameteriv(GLenum target, GLenum pname, GLint *params) {
	std::cout << __func__ << std::endl;
}
void glGetUniformfv(GLuint program, GLint location, GLfloat *params) {
	std::cout << __func__ << std::endl;
}
void glGetUniformiv(GLuint program, GLint location, GLint *params) {
	std::cout << __func__ << std::endl;
}
GLint glGetUniformLocation(GLuint program, const GLchar *name) {
	GL_SET_COMMAND(c, glGetUniformLocation);

	c->program = program;
	c->name = name;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glGetUniformLocation, (void *)c, sizeof(gl_glGetUniformLocation_t), true);

	// zmq::message_t result = send_data((unsigned char)GL_Server_Command::GLSC_glGetAttribLocation, (void *)c, sizeof(gl_glGetAttribLocation_t), true);
	ZMQServer *zmq_server = ZMQServer::get_instance();
	send_buffer();
	zmq::message_t result;
	zmq_server->socket.recv(result, zmq::recv_flags::none);
	// zmq::message_t result = send_data((unsigned char)GL_Server_Command::GLSC_glGetUniformLocation, (void *)c, sizeof(gl_glGetUniformLocation_t), true);
	GLint *ret = (GLint *)result.data();
	return (GLint)*ret;
}
void glGetVertexAttribfv(GLuint index, GLenum pname, GLfloat *params) {
	std::cout << __func__ << std::endl;
}
void glGetVertexAttribiv(GLuint index, GLenum pname, GLint *params) {
	std::cout << __func__ << std::endl;
}
void glGetVertexAttribPointerv(GLuint index, GLenum pname, void **pointer) {

	std::cout << __func__ << std::endl;
}
void glHint(GLenum target, GLenum mode) {
	std::cout << __func__ << std::endl;
}
GLboolean glIsBuffer(GLuint buffer) {
	std::cout << __func__ << std::endl;

	return 0;
}
GLboolean glIsEnabled(GLenum cap) {
	std::cout << __func__ << std::endl;

	return 0;
}
GLboolean glIsFramebuffer(GLuint framebuffer) {
	std::cout << __func__ << std::endl;

	return 0;
}
GLboolean glIsProgram(GLuint program) {
	std::cout << __func__ << std::endl;

	return 0;
}
GLboolean glIsRenderbuffer(GLuint renderbuffer) {
	std::cout << __func__ << std::endl;

	return 0;
}
GLboolean glIsShader(GLuint shader) {
	std::cout << __func__ << std::endl;

	return 0;
}
GLboolean glIsTexture(GLuint texture) {
	std::cout << __func__ << std::endl;

	return 0;
}
void glLineWidth(GLfloat width) {
	std::cout << __func__ << std::endl;
}
void glPixelStorei(GLenum pname, GLint param) {
	switch (pname) {
		case GL_PACK_ALIGNMENT:
			global_pack_alignment = param;
			break;
		case GL_UNPACK_ALIGNMENT:
			global_unpack_alignment = param;
			break;
		default:
			break;
	}
	GL_SET_COMMAND(c, glPixelStorei);

	c->pname = pname;
	c->param = param;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glPixelStorei, (void *)c, sizeof(gl_glPixelStorei_t), true);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif

	// send_data((unsigned char)GL_Server_Command::GLSC_glPixelStorei, (void *)c, sizeof(gl_glPixelStorei_t));
}
void glPolygonOffset(GLfloat factor, GLfloat units) {
	std::cout << __func__ << std::endl;
}
void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void *pixels) {
	GL_SET_COMMAND(c, glReadPixels);

	c->x = x;
	c->y = y;
	c->width = width;
	c->height = height;
	c->format = format;
	c->type = type;

	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glReadPixels, (void *)c, sizeof(gl_glReadPixels_t), true);

	ZMQServer *zmq_server = ZMQServer::get_instance();
	send_buffer();
	zmq::message_t result;
	zmq_server->socket.recv(result, zmq::recv_flags::none);

	// zmq::message_t result = send_data((unsigned char)GL_Server_Command::GLSC_glReadPixels, (void *)c, sizeof(gl_glReadPixels_t), true);
	void *data = result.data();
	size_t size = 0;
	switch (type) {
		case GL_UNSIGNED_BYTE: {
			if (format == GL_RGBA) {
				size = width * height * 4;
			} else if (format == GL_RGB) {
				size = width * height * 3;
			}
			break;
		}
		default:
			break;
	}
	memcpy(pixels, data, size);
}

void glReleaseShaderCompiler(void) {
	std::cout << __func__ << std::endl;
}
void glRenderbufferStorage(GLenum target, GLenum internalformat, GLsizei width, GLsizei height) {
	GL_SET_COMMAND(c, glRenderbufferStorage);

	c->target = target;
	c->internalformat = internalformat;
	c->width = width;
	c->height = height;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glRenderbufferStorage, (void *)c, sizeof(gl_glRenderbufferStorage_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif

	// send_data((unsigned char)GL_Server_Command::GLSC_glRenderbufferStorage, (void *)c, sizeof(gl_glRenderbufferStorage_t));
}
void glSampleCoverage(GLfloat value, GLboolean invert) {
	std::cout << __func__ << std::endl;
}

void glShaderBinary(GLsizei count, const GLuint *shaders, GLenum binaryformat, const void *binary, GLsizei length) {
	std::cout << __func__ << std::endl;
}
void glStencilFunc(GLenum func, GLint ref, GLuint mask) {
	std::cout << __func__ << std::endl;
}
void glStencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask) {
	std::cout << __func__ << std::endl;
}
void glStencilMask(GLuint mask) {
	std::cout << __func__ << std::endl;
}
void glStencilMaskSeparate(GLenum face, GLuint mask) {
	std::cout << __func__ << std::endl;
}
void glStencilOp(GLenum fail, GLenum zfail, GLenum zpass) {
	std::cout << __func__ << std::endl;
}
void glStencilOpSeparate(GLenum face, GLenum sfail, GLenum dpfail, GLenum dppass) {
	std::cout << __func__ << std::endl;
}
void glTexParameterf(GLenum target, GLenum pname, GLfloat param) {
	GL_SET_COMMAND(c, glTexParameterf);

	c->target = target;
	c->pname = pname;
	c->param = param;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glTexParameterf, (void *)c, sizeof(gl_glTexParameterf_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif

	// send_data((unsigned char)GL_Server_Command::GLSC_glTexParameterf, (void *)c, sizeof(gl_glTexParameterf_t));
}
void glTexParameterfv(GLenum target, GLenum pname, const GLfloat *params) {
	std::cout << __func__ << std::endl;
}
void glTexParameteriv(GLenum target, GLenum pname, const GLint *params) {
	std::cout << __func__ << std::endl;
}
void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void *pixels) {
	GL_SET_COMMAND(c, glTexSubImage2D);

	c->target = target;
	c->level = level;
	c->xoffset = xoffset;
	c->yoffset = yoffset;
	c->width = width;
	c->height = height;
	c->format = format;
	c->type = type;
	c->pixels = pixels;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glTexSubImage2D, (void *)c, sizeof(gl_glTexSubImage2D_t), true);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif
}
void glUniform1f(GLint location, GLfloat v0) {
	GL_SET_COMMAND(c, glUniform1f);
	c->location = location;
	c->v0 = v0;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glUniform1f, (void *)c, sizeof(gl_glUniform1f_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif
}
void glUniform1fv(GLint location, GLsizei count, const GLfloat *value) {
	std::cout << __func__ << std::endl;
}
void glUniform1i(GLint location, GLint v0) {
	GL_SET_COMMAND(c, glUniform1i);
	c->location = location;
	c->v0 = v0;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glUniform1i, (void *)c, sizeof(gl_glUniform1i_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif
}
void glUniform1iv(GLint location, GLsizei count, const GLint *value) {
	GL_SET_COMMAND(c, glUniform1iv);
	c->location = location;
	c->count = count;
	c->value = value;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glUniform1iv, (void *)c, sizeof(gl_glUniform1iv_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif
}
void glUniform2f(GLint location, GLfloat v0, GLfloat v1) {
	std::cout << __func__ << std::endl;
}
void glUniform2fv(GLint location, GLsizei count, const GLfloat *value) {
	GL_SET_COMMAND(c, glUniform2fv);

	c->location = location;
	c->count = count;
	c->value = value;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glUniform2fv, (void *)c, sizeof(gl_glUniform2fv_t), true);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif
}
void glUniform2i(GLint location, GLint v0, GLint v1) {
	std::cout << __func__ << std::endl;
}
void glUniform2iv(GLint location, GLsizei count, const GLint *value) {
	std::cout << __func__ << std::endl;
}
void glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2) {
	std::cout << __func__ << std::endl;
}
void glUniform3fv(GLint location, GLsizei count, const GLfloat *value) {
	std::cout << __func__ << std::endl;
}
void glUniform3i(GLint location, GLint v0, GLint v1, GLint v2) {
	std::cout << __func__ << std::endl;
}
void glUniform3iv(GLint location, GLsizei count, const GLint *value) {
	std::cout << __func__ << std::endl;
}
void glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) {
	std::cout << __func__ << std::endl;
}
void glUniform4fv(GLint location, GLsizei count, const GLfloat *value) {
	GL_SET_COMMAND(c, glUniform4fv);

	c->location = location;
	c->count = count;
	c->value = value;

	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glUniform4fv, (void *)c, sizeof(gl_glUniform4fv_t), true);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif

	// send_data((unsigned char)GL_Server_Command::GLSC_glUniform4fv, (void *)c, sizeof(gl_glUniform4fv_t));
}
void glUniform4i(GLint location, GLint v0, GLint v1, GLint v2, GLint v3) {
	std::cout << __func__ << std::endl;
}
void glUniform4iv(GLint location, GLsizei count, const GLint *value) {
	std::cout << __func__ << std::endl;
}
void glUniformMatrix2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {
	std::cout << __func__ << std::endl;
}
void glUniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {
	std::cout << __func__ << std::endl;
}
void glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {
	GL_SET_COMMAND(c, glUniformMatrix4fv);

	c->location = location;
	c->count = count;
	c->transpose = transpose;
	c->value = value;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glUniformMatrix4fv, (void *)c, sizeof(gl_glUniformMatrix4fv_t), true);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif

	// send_data((unsigned char)GL_Server_Command::GLSC_glUniformMatrix4fv, (void *)c, sizeof(gl_glUniformMatrix4fv_t));
}
void glValidateProgram(GLuint program) {
	std::cout << __func__ << std::endl;
}
void glVertexAttrib1f(GLuint index, GLfloat x) {
	std::cout << __func__ << std::endl;
}
void glVertexAttrib1fv(GLuint index, const GLfloat *v) {
	std::cout << __func__ << std::endl;
}
void glVertexAttrib2f(GLuint index, GLfloat x, GLfloat y) {
	std::cout << __func__ << std::endl;
}
void glVertexAttrib2fv(GLuint index, const GLfloat *v) {
	std::cout << __func__ << std::endl;
}
void glVertexAttrib3f(GLuint index, GLfloat x, GLfloat y, GLfloat z) {
	std::cout << __func__ << std::endl;
}
void glVertexAttrib3fv(GLuint index, const GLfloat *v) {
	std::cout << __func__ << std::endl;
}
void glVertexAttrib4f(GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w) {
	GL_SET_COMMAND(c, glVertexAttrib4f);
	c->index = index;
	c->x = x;
	c->y = y;
	c->z = z;
	c->w = w;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glVertexAttrib4f, (void *)c, sizeof(gl_glVertexAttrib4f_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif

	// send_data((unsigned char)GL_Server_Command::GLSC_glVertexAttrib4f, (void *)c, sizeof(gl_glVertexAttrib4f_t));
}
void glVertexAttrib4fv(GLuint index, const GLfloat *v) {
	GL_SET_COMMAND(c, glVertexAttrib4fv);

	c->index = index;
	c->v = v;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glVertexAttrib4fv, (void *)c, sizeof(gl_glVertexAttrib4fv_t), true);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif

	// send_data((unsigned char)GL_Server_Command::GLSC_glVertexAttrib4fv, (void *)c, sizeof(gl_glVertexAttrib4fv_t));
}
//
void glReadBuffer(GLenum src) {
	GL_SET_COMMAND(c, glReadBuffer);

	c->src = src;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glReadBuffer, (void *)c, sizeof(gl_glReadBuffer_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif

	// send_data((unsigned char)GL_Server_Command::GLSC_glReadBuffer, (void *)c, sizeof(gl_glReadBuffer_t));
}
void glDrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const void *indices) {
	std::cout << __func__ << std::endl;
}
void glTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const void *pixels) {

	GL_SET_COMMAND(c, glTexSubImage3D);

	c->target = target;
	c->level = level;
	c->xoffset = xoffset;
	c->yoffset = yoffset;
	c->zoffset = zoffset;
	c->width = width;
	c->height = height;
	c->depth = depth;
	c->format = format;
	c->type = type;
	c->pixels = pixels;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glTexSubImage3D, (void *)c, sizeof(gl_glTexSubImage3D_t), true);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif

	// send_data((unsigned char)GL_Server_Command::GLSC_glTexSubImage3D, (void *)c, sizeof(gl_glTexSubImage3D_t));
}
void glCopyTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLint x, GLint y, GLsizei width, GLsizei height) {
	std::cout << __func__ << std::endl;
}
void glCompressedTexImage3D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLsizei imageSize, const void *data) {
	std::cout << __func__ << std::endl;
}
void glCompressedTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLsizei imageSize, const void *data) {
	std::cout << __func__ << std::endl;
}
void glGenQueries(GLsizei n, GLuint *ids) {
	std::cout << __func__ << std::endl;
}
void glDeleteQueries(GLsizei n, const GLuint *ids) {
	std::cout << __func__ << std::endl;
}
GLboolean glIsQuery(GLuint id) {
	std::cout << __func__ << std::endl;
	return 0;
}
void glBeginQuery(GLenum target, GLuint id) {
	std::cout << __func__ << std::endl;
}
void glEndQuery(GLenum target) {
	std::cout << __func__ << std::endl;
}
void glGetQueryiv(GLenum target, GLenum pname, GLint *params) {
	std::cout << __func__ << std::endl;
}
void glGetQueryObjectuiv(GLuint id, GLenum pname, GLuint *params) {
	std::cout << __func__ << std::endl;
}
GLboolean glUnmapBuffer(GLenum target) {
	std::cout << __func__ << std::endl;

	return 0;
}
void glGetBufferPointerv(GLenum target, GLenum pname, void **params) {
	std::cout << __func__ << std::endl;
}
void glDrawBuffers(GLsizei n, const GLenum *bufs) {
	GL_SET_COMMAND(c, glDrawBuffers);

	c->n = n;
	c->bufs = bufs;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glDrawBuffers, (void *)c, sizeof(gl_glDrawBuffers_t), true);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif

	// send_data((unsigned char)GL_Server_Command::GLSC_glDrawBuffers, (void *)c, sizeof(gl_glDrawBuffers_t));
}
void glUniformMatrix2x3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {
	std::cout << __func__ << std::endl;
}
void glUniformMatrix3x2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {
	std::cout << __func__ << std::endl;
}
void glUniformMatrix2x4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {
	std::cout << __func__ << std::endl;
}
void glUniformMatrix4x2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {
	std::cout << __func__ << std::endl;
}
void glUniformMatrix3x4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {
	std::cout << __func__ << std::endl;
}
void glUniformMatrix4x3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {
	std::cout << __func__ << std::endl;
}
void glBlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter) {
	GL_SET_COMMAND(c, glBlitFramebuffer);

	c->srcX0 = srcX0;
	c->srcY0 = srcY0;
	c->srcX1 = srcX1;
	c->srcY1 = srcY1;
	c->dstX0 = dstX0;
	c->dstY0 = dstY0;
	c->dstX1 = dstX1;
	c->dstY1 = dstY1;
	c->mask = mask;
	c->filter = filter;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glBlitFramebuffer, (void *)c, sizeof(gl_glBlitFramebuffer_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif

	// send_data((unsigned char)GL_Server_Command::GLSC_glBlitFramebuffer, (void *)c, sizeof(gl_glBlitFramebuffer_t));
}
void glRenderbufferStorageMultisample(GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height) {
	GL_SET_COMMAND(c, glRenderbufferStorageMultisample);

	c->target = target;
	c->samples = samples;
	c->internalformat = internalformat;
	c->width = width;
	c->height = height;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glRenderbufferStorageMultisample, (void *)c, sizeof(gl_glRenderbufferStorageMultisample_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif

	// send_data((unsigned char)GL_Server_Command::GLSC_glRenderbufferStorageMultisample, (void *)c, sizeof(gl_glRenderbufferStorageMultisample_t));
}
void glFramebufferTextureLayer(GLenum target, GLenum attachment, GLuint texture, GLint level, GLint layer) {
	GL_SET_COMMAND(c, glFramebufferTextureLayer);

	c->target = target;
	c->attachment = attachment;
	c->texture = texture;
	c->level = level;
	c->layer = layer;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glFramebufferTextureLayer, (void *)c, sizeof(gl_glFramebufferTextureLayer_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif

	// send_data((unsigned char)GL_Server_Command::GLSC_glFramebufferTextureLayer, (void *)c, sizeof(gl_glFramebufferTextureLayer_t));
}
void *glMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access) {
	std::cout << __func__ << std::endl;
}
void glFlushMappedBufferRange(GLenum target, GLintptr offset, GLsizeiptr length) {
	std::cout << __func__ << std::endl;
}
void glDeleteVertexArrays(GLsizei n, const GLuint *arrays) {
	GL_SET_COMMAND(c, glDeleteVertexArrays);
	c->n = n;
	c->arrays = arrays;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glDeleteVertexArrays, (void *)c, sizeof(gl_glDeleteVertexArrays_t), true);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif
}
GLboolean glIsVertexArray(GLuint array) {
	return 0;
}
void glGetIntegeri_v(GLenum target, GLuint index, GLint *data) {
	std::cout << __func__ << std::endl;
}
void glBeginTransformFeedback(GLenum primitiveMode) {
	GL_SET_COMMAND(c, glBeginTransformFeedback);

	c->primitiveMode = primitiveMode;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glBeginTransformFeedback, (void *)c, sizeof(gl_glBeginTransformFeedback_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif

	// send_data((unsigned char)GL_Server_Command::GLSC_glBeginTransformFeedback, (void *)c, sizeof(gl_glBeginTransformFeedback_t));
}
void glEndTransformFeedback(void) {
	GL_SET_COMMAND(c, glEndTransformFeedback);
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glEndTransformFeedback, (void *)c, sizeof(gl_glEndTransformFeedback_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif

	// send_data((unsigned char)GL_Server_Command::GLSC_glEndTransformFeedback, (void *)c, sizeof(gl_glEndTransformFeedback_t));
}
void glBindBufferRange(GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size) {
	std::cout << __func__ << std::endl;
}
void glBindBufferBase(GLenum target, GLuint index, GLuint buffer) {
	GL_SET_COMMAND(c, glBindBufferBase);

	c->target = target;
	c->index = index;
	c->buffer = buffer;
	// std::cout << target << "\t" << index << "\t" << buffer << std::endl;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glBindBufferBase, (void *)c, sizeof(gl_glBindBufferBase_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif
}
void glTransformFeedbackVaryings(GLuint program, GLsizei count, const GLchar *const *varyings, GLenum bufferMode) {
	GL_SET_COMMAND(c, glTransformFeedbackVaryings);

	c->program = program;
	c->count = count;
	c->varyings = varyings;
	c->bufferMode = bufferMode;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glTransformFeedbackVaryings, (void *)c, sizeof(gl_glTransformFeedbackVaryings_t), true);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif

	// send_data((unsigned char)GL_Server_Command::GLSC_glTransformFeedbackVaryings, (void *)c, sizeof(gl_glTransformFeedbackVaryings_t));
}
void glGetTransformFeedbackVarying(GLuint program, GLuint index, GLsizei bufSize, GLsizei *length, GLsizei *size, GLenum *type, GLchar *name) {
	std::cout << __func__ << std::endl;
}
void glVertexAttribIPointer(GLuint index, GLint size, GLenum type, GLsizei stride, const void *pointer) {
	std::cout << __func__ << std::endl;
}
void glGetVertexAttribIiv(GLuint index, GLenum pname, GLint *params) {
	std::cout << __func__ << std::endl;
}
void glGetVertexAttribIuiv(GLuint index, GLenum pname, GLuint *params) {
	std::cout << __func__ << std::endl;
}
void glVertexAttribI4i(GLuint index, GLint x, GLint y, GLint z, GLint w) {
	std::cout << __func__ << std::endl;
}
void glVertexAttribI4ui(GLuint index, GLuint x, GLuint y, GLuint z, GLuint w) {
	std::cout << __func__ << std::endl;
}
void glVertexAttribI4iv(GLuint index, const GLint *v) {
	std::cout << __func__ << std::endl;
}
void glVertexAttribI4uiv(GLuint index, const GLuint *v) {
	std::cout << __func__ << std::endl;
}
void glGetUniformuiv(GLuint program, GLint location, GLuint *params) {
	std::cout << __func__ << std::endl;
}
GLint glGetFragDataLocation(GLuint program, const GLchar *name) {

	return 0;
}
void glUniform1ui(GLint location, GLuint v0) {
	GL_SET_COMMAND(c, glUniform1ui);

	c->location = location;
	c->v0 = v0;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glUniform1ui, (void *)c, sizeof(gl_glUniform1ui_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif

	// send_data((unsigned char)GL_Server_Command::GLSC_glUniform1ui, (void *)c, sizeof(gl_glUniform1ui_t));
}
void glUniform2ui(GLint location, GLuint v0, GLuint v1) {
	std::cout << __func__ << std::endl;
}
void glUniform3ui(GLint location, GLuint v0, GLuint v1, GLuint v2) {
	std::cout << __func__ << std::endl;
}
void glUniform4ui(GLint location, GLuint v0, GLuint v1, GLuint v2, GLuint v3) {
	std::cout << __func__ << std::endl;
}
void glUniform1uiv(GLint location, GLsizei count, const GLuint *value) {
	std::cout << __func__ << std::endl;
}
void glUniform2uiv(GLint location, GLsizei count, const GLuint *value) {
	std::cout << __func__ << std::endl;
}
void glUniform3uiv(GLint location, GLsizei count, const GLuint *value) {
	std::cout << __func__ << std::endl;
}
void glUniform4uiv(GLint location, GLsizei count, const GLuint *value) {
	std::cout << __func__ << std::endl;
}
void glClearBufferiv(GLenum buffer, GLint drawbuffer, const GLint *value) {
	std::cout << __func__ << std::endl;
}
void glClearBufferuiv(GLenum buffer, GLint drawbuffer, const GLuint *value) {
	std::cout << __func__ << std::endl;
}
void glClearBufferfv(GLenum buffer, GLint drawbuffer, const GLfloat *value) {
	GL_SET_COMMAND(c, glClearBufferfv);

	c->buffer = buffer;
	c->drawbuffer = drawbuffer;
	c->value = value;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glClearBufferfv, (void *)c, sizeof(gl_glClearBufferfv_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif

	// send_data((unsigned char)GL_Server_Command::GLSC_glClearBufferfv, (void *)c, sizeof(gl_glClearBufferfv_t));
}
void glClearBufferfi(GLenum buffer, GLint drawbuffer, GLfloat depth, GLint stencil) {
	std::cout << __func__ << std::endl;
}
const GLubyte *glGetStringi(GLenum name, GLuint index) {
	GL_SET_COMMAND(c, glGetStringi);

	c->name = name;
	c->index = index;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glGetStringi, (void *)c, sizeof(gl_glGetStringi_t), false);

	// zmq::message_t result = send_data((unsigned char)GL_Server_Command::GLSC_glGetAttribLocation, (void *)c, sizeof(gl_glGetAttribLocation_t), true);
	ZMQServer *zmq_server = ZMQServer::get_instance();
	send_buffer();
	zmq::message_t result;
	zmq_server->socket.recv(result, zmq::recv_flags::none);

	// zmq::message_t result = send_data((unsigned char)GL_Server_Command::GLSC_glGetStringi, (void *)c, sizeof(gl_glGetStringi_t), true);
	std::string ret = result.to_string();

	// const GLubyte *ret = reinterpret_cast<const GLubyte *>(send_data((unsigned char) GL_Server_Command::GLSC_glGetStringi, (void *)c, sizeof(gl_glGetStringi_t)));
	// std::cout << ret.c_str() << std::endl;
	return reinterpret_cast<const GLubyte *>(ret.c_str());
}
void glCopyBufferSubData(GLenum readTarget, GLenum writeTarget, GLintptr readOffset, GLintptr writeOffset, GLsizeiptr size) {
	std::cout << __func__ << std::endl;
}
void glGetUniformIndices(GLuint program, GLsizei uniformCount, const GLchar *const *uniformNames, GLuint *uniformIndices) {
	std::cout << __func__ << std::endl;
}
void glGetActiveUniformsiv(GLuint program, GLsizei uniformCount, const GLuint *uniformIndices, GLenum pname, GLint *params) {
	std::cout << __func__ << std::endl;
}
GLuint glGetUniformBlockIndex(GLuint program, const GLchar *uniformBlockName) {
	GL_SET_COMMAND(c, glGetUniformBlockIndex);

	c->program = program;
	c->name = uniformBlockName;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glGetUniformBlockIndex, (void *)c, sizeof(gl_glGetUniformBlockIndex_t), true);

	ZMQServer *zmq_server = ZMQServer::get_instance();
	send_buffer();
	zmq::message_t result;
	zmq_server->socket.recv(result, zmq::recv_flags::none);

	// zmq::message_t result = send_data((unsigned char)GL_Server_Command::GLSC_glGetUniformBlockIndex, (void *)c, sizeof(gl_glGetUniformBlockIndex_t), true);
	GLuint *ret = (GLuint *)result.data();
	return (GLuint)*ret;
}
void glGetActiveUniformBlockiv(GLuint program, GLuint uniformBlockIndex, GLenum pname, GLint *params) {
}
void glGetActiveUniformBlockName(GLuint program, GLuint uniformBlockIndex, GLsizei bufSize, GLsizei *length, GLchar *uniformBlockName) {
}
void glUniformBlockBinding(GLuint program, GLuint uniformBlockIndex, GLuint uniformBlockBinding) {
	GL_SET_COMMAND(c, glUniformBlockBinding);

	c->program = program;
	c->uniformBlockIndex = uniformBlockIndex;
	c->uniformBlockBinding = uniformBlockBinding;

	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glUniformBlockBinding, (void *)c, sizeof(gl_glUniformBlockBinding_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif

	// send_data((unsigned char)GL_Server_Command::GLSC_glUniformBlockBinding, (void *)c, sizeof(gl_glUniformBlockBinding_t));
}
void glDrawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei instancecount) {
	GL_SET_COMMAND(c, glDrawArraysInstanced);
	c->mode = mode;
	c->first = first;
	c->count = count;
	c->instancecount = instancecount;
	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glDrawArraysInstanced, (void *)c, sizeof(gl_glDrawArraysInstanced_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif
}
void glDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei instancecount) {
	std::cout << __func__ << std::endl;
}
GLsync glFenceSync(GLenum condition, GLbitfield flags) {
	std::cout << __func__ << std::endl;
	return 0;
}
GLboolean glIsSync(GLsync sync) {
	std::cout << __func__ << std::endl;
	return 0;
}
void glDeleteSync(GLsync sync) {
	std::cout << __func__ << std::endl;
}
GLenum glClientWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout) {
	std::cout << __func__ << std::endl;
	return 0;
}
void glWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout) {
	std::cout << __func__ << std::endl;
}
void glGetInteger64v(GLenum pname, GLint64 *data) {
	std::cout << __func__ << std::endl;
}
void glGetSynciv(GLsync sync, GLenum pname, GLsizei bufSize, GLsizei *length, GLint *values) {
	std::cout << __func__ << std::endl;
}
void glGetInteger64i_v(GLenum target, GLuint index, GLint64 *data) {
	std::cout << __func__ << std::endl;
}
void glGetBufferParameteri64v(GLenum target, GLenum pname, GLint64 *params) {
	std::cout << __func__ << std::endl;
}
void glGenSamplers(GLsizei count, GLuint *samplers) {
	std::cout << __func__ << std::endl;
}
void glDeleteSamplers(GLsizei count, const GLuint *samplers) {
	std::cout << __func__ << std::endl;
}
GLboolean glIsSampler(GLuint sampler) {
	std::cout << __func__ << std::endl;
	return 0;
}
void glBindSampler(GLuint unit, GLuint sampler) {
	std::cout << __func__ << std::endl;
}
void glSamplerParameteri(GLuint sampler, GLenum pname, GLint param) {
	std::cout << __func__ << std::endl;
}
void glSamplerParameteriv(GLuint sampler, GLenum pname, const GLint *param) {
	std::cout << __func__ << std::endl;
}
void glSamplerParameterf(GLuint sampler, GLenum pname, GLfloat param) {
	std::cout << __func__ << std::endl;
}
void glSamplerParameterfv(GLuint sampler, GLenum pname, const GLfloat *param) {
	std::cout << __func__ << std::endl;
}
void glGetSamplerParameteriv(GLuint sampler, GLenum pname, GLint *params) {
	std::cout << __func__ << std::endl;
}
void glGetSamplerParameterfv(GLuint sampler, GLenum pname, GLfloat *params) {
	std::cout << __func__ << std::endl;
}
void glVertexAttribDivisor(GLuint index, GLuint divisor) {
	GL_SET_COMMAND(c, glVertexAttribDivisor);

	c->index = index;
	c->divisor = divisor;

	std::string message = create_message((unsigned int)GL_Server_Command::GLSC_glVertexAttribDivisor, (void *)c, sizeof(gl_glVertexAttribDivisor_t), false);
#if !FRAME_BUFFER_ENABLE
	send_buffer();
#endif
}
void glBindTransformFeedback(GLenum target, GLuint id) {
	std::cout << __func__ << std::endl;
}
void glDeleteTransformFeedbacks(GLsizei n, const GLuint *ids) {
	std::cout << __func__ << std::endl;
}
void glGenTransformFeedbacks(GLsizei n, GLuint *ids) {
	std::cout << __func__ << std::endl;
}
GLboolean glIsTransformFeedback(GLuint id) {
	std::cout << __func__ << std::endl;
	return 0;
}
void glPauseTransformFeedback(void) {
	std::cout << __func__ << std::endl;
}
void glResumeTransformFeedback(void) {
	std::cout << __func__ << std::endl;
}
void glGetProgramBinary(GLuint program, GLsizei bufSize, GLsizei *length, GLenum *binaryFormat, void *binary) {
	std::cout << __func__ << std::endl;
}
void glProgramBinary(GLuint program, GLenum binaryFormat, const void *binary, GLsizei length) {
	std::cout << __func__ << std::endl;
}
void glProgramParameteri(GLuint program, GLenum pname, GLint value) {
	std::cout << __func__ << std::endl;
}
void glInvalidateFramebuffer(GLenum target, GLsizei numAttachments, const GLenum *attachments) {
	std::cout << __func__ << std::endl;
}
void glInvalidateSubFramebuffer(GLenum target, GLsizei numAttachments, const GLenum *attachments, GLint x, GLint y, GLsizei width, GLsizei height) {
	std::cout << __func__ << std::endl;
}
void glTexStorage3D(GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth) {
	std::cout << __func__ << std::endl;
}
void glGetInternalformativ(GLenum target, GLenum internalformat, GLenum pname, GLsizei bufSize, GLint *params) {
	std::cout << __func__ << std::endl;
}
