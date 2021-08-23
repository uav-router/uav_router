# Build
This project use Waf build tool.
There are three build options:
* host build
* Docker build on image that native for tagret platform. Relatively slow crossbuilds.
* Docker cross build. x86 base image with sysroots for arm and aarch64 platforms.

It is possible to use Docker to build this project. 
Building with Docker used for crossbuilds also. In this case Docker is the only prerequisite that have to be installed for build.
## Host Build 
### Install Prerequisites
#### Ubuntu
```
apt-get install -y --no-install-recommends curl clang llvm build-essential libudev-dev cmake python3 python3-pip libavahi-client-dev \
                                           libavahi-core-dev git rsync libcurl4-openssl-dev
update-alternatives --install /usr/bin/python python /usr/bin/python3 1
curl --insecure -o waf.tar.bz2 https://waf.io/waf-2.0.22.tar.bz2
tar xjvf waf.tar.bz2
cd waf-2.0.22
python waf-light --tools=clang_compilation_database
mv waf /usr/bin
cd ..
rm -rf waf-2.0.22
waf -h
pip3 install --no-cache-dir setuptools
pip3 install --no-cache-dir git+https://github.com/larsks/dockerize
```
#### Fedora
```
dnf install -y clang llvm lld compiler-rt systemd-devel cmake python python-pip waf avahi-devel git rsync libcurl-devel \
    --nodocs --setopt install_weak_deps=False
pip install --no-cache-dir setuptools
pip install --no-cache-dir git+https://github.com/larsks/dockerize
```
### Clone project
```
git clone --recurse-submodules https://github.com/uav-router/uav_router.git
cd uav_router
```
### Configure and build dependencies
```
waf configure build_deps --prefix=/usr --sentry=yes
```
### Build
```
waf build
```

## Build with Docker (native images)
```
git clone --recurse-submodules https://github.com/uav-router/uav_router.git
cd uav_router
docker/configure
docker/waf build
```
### Cross build with native images for target platform
#### Install qemu
##### Fedora host
```
dnf install qemu qemu-user-static
docker run --rm --privileged multiarch/qemu-user-static --reset -p yes
```
##### Ubuntu host
```
sudo apt-get install qemu binfmt-support qemu-user-static
docker run --rm --privileged multiarch/qemu-user-static --reset -p yes
```
#### Build
```
docker/configure arm32 #arm64 amd64
docker/waf arm32 build
```

### Use Ubuntu images (untested)
```
docker/configure amd64 ubuntu
docker/waf build
```
### Build uav_router Docker image
```
docker/image # amd64 arm64 arm32
```
### Run
Run docker container with image
```
docker/uav-router /path/to/config/file.yml
```
## Build with Docker (x86 image for crossbuild)
```
git clone --recurse-submodules https://github.com/uav-router/uav_router.git
cd uav_router
docker/cross/configure [arm32, arm64]
docker/cross/waf build
docker/cross/image
```
