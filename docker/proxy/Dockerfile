FROM nvidia/opengl:1.2-glvnd-devel-ubuntu20.04
LABEL author="play1490@hanyang.ac.kr"
LABEL purpose="docker image for E-Render Proxy"

# Insatll Dependencies

RUN apt update && apt install build-essential scons pkg-config libx11-dev libxcursor-dev libxinerama-dev \
    libgl1-mesa-dev libglu-dev libasound2-dev libpulse-dev libudev-dev libxi-dev libxrandr-dev yasm \ 
    libzmq3-dev libglfw3-dev git -y
RUN apt update && apt install libzmq3-dev build-essential scons pkg-config -y

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
git clone https://github.com/youngjinkim0706/splab_godot.git
cd splab_godot
git checkout -t origin/percomm
scons platform=x11 target=release_debug -j8

EXPOSE 12345

