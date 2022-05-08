# Edge Render Stub 
## Install requirements
    
```bash
# install packages via apt 
sudo apt update && sudo apt install build-essential pkg-config libx11-dev libxcursor-dev libxinerama-dev\
libgl1-mesa-dev libglu-dev libasound2-dev libpulse-dev libudev-dev libxi-dev libxrandr-dev \ 
libglfw3-dev cmake libboost-all-dev git -y

# install libglad
cd stub/glad
sudo cp -r glad /usr/local/include
sudo cp -r KHR /usr/local/include
sudo cp -r libglad.so.0.0.0 /usr/local/lib
sudo ln -s /usr/local/lib/libglad.so.0.0.0 /usr/local/lib/libglad.so
    
# install cppzmq
cd ../../
git clone https://github.com/zeromq/cppzmq.git
sudo apt install libzmq3-dev -y
cd cppzmq
mkdir build
cd build
cmake ..
# set CPPZMQ_BUILD_TESTS:BOOL=OFF
sed -i 's/CPPZMQ_BUILD_TESTS:BOOL=ON/CPPZMQ_BUILD_TESTS:BOOL=OFF/g' CMakeCache.txt
sudo make install -j4

# install snappy
cd ../../../
git clone https://github.com/google/snappy.git
cd snappy
git submodule update --init
mkdir build
cd build && cmake ../ && sudo make install -j

# build and run gl_server
make gl_server
./gl_server ip-address port
```
