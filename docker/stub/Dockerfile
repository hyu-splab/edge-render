FROM nvidia/opengl:1.2-glvnd-devel-ubuntu20.04
LABEL author="play1490@hanyang.ac.kr"
LABEL purpose="docker image for E-Render Proxy"

# Insatll Dependencies

RUN apt update && apt install libzmq3-dev libglfw3-dev make git pkg-config -y

# Copy libglad.so 
COPY libglad.so.0.0.0 /usr/local/lib/
RUN ln -s /usr/local/lib/libglad.so.0.0.0 /usr/local/lib/libglad.so
COPY glad /usr/local/include
COPY KHR /usr/local/include

# Install cppzmq
COPY install-zmq.sh /
RUN chmod 755 install-zmq.sh
RUN install-zmq.sh

# Download splab-godot
git clone https://github.com/youngjinkim0706/remote-gl.git
cd remote-gl
git checkout -t origin/percomm
make gl_server

EXPOSE 12345
