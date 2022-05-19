git clone https://github.com/google/snappy.git
cd snappy
git submodule update --init
mkdir build
cd build && cmake ../ && make && make install
