FROM nvidia/opengl:1.2-glvnd-devel-ubuntu20.04
LABEL author="play1490@hanyang.ac.kr"
LABEL purpose="docker image for E-Render Proxy"

# Insatll Dependencies
ENV DEBIAN_FRONTEND noninteractive
RUN apt update && apt install libzmq3-dev libboost-all-dev libglfw3-dev make cmake git g++ pkg-config -y

# Copy libglad.so 
COPY libglad.so.0.0.0 /usr/local/lib/
RUN ln -s /usr/local/lib/libglad.so.0.0.0 /usr/local/lib/libglad.so
COPY glad /usr/local/include
COPY KHR /usr/local/include

# Install cppzmq
COPY install-zmq.sh /
RUN chmod 755 install-zmq.sh
RUN ./install-zmq.sh

# Install snappy
WORKDIR /
COPY install-snappy.sh /
RUN chmod 755 install-snappy.sh
RUN ./install-snappy.sh

# Download splab-godot
WORKDIR /
RUN git clone https://github.com/youngjinkim0706/remote-gl.git
WORKDIR /remote-gl
RUN git checkout -t origin/percomm
RUN make gl_server

EXPOSE 12345

