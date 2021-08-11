# Build
This project use Waf build tool. 
It is possible to use Docker to build this project. Building with Docker used for crossbuilds also. In this case Docker is the only prerequisite that have to be installed for build.
## Build with Docker
```
git clone --recurse-submodules https://github.com/uav-router/uav_router.git
cd uav_router
docker/configure
docker/waf build
```
### Cross build
Install qemu properly. (See below)
```
docker/configure arm32 #arm64 amd64
docker/waf arm32 build
```

#### Fedora host
```
dnf install qemu qemu-user-static
docker run --rm --privileged multiarch/qemu-user-static --reset -p yes
```
#### Ubuntu host
```
sudo apt-get install qemu binfmt-support qemu-user-static
docker run --rm --privileged multiarch/qemu-user-static --reset -p yes
```
### Use Ubuntu images
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
## Build with Waf
### Install prerequisites
#### Ubuntu
```
apt-get install -y --no-install-recommends curl clang build-essential libudev-dev cmake python3 python3-pip libavahi-client-dev \
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
dnf install -y clang systemd-devel cmake python python-pip waf avahi-devel git rsync libcurl-devel \
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
waf configure build_deps --prefix=/usr --check-cxx-compiler=clang++ --sentry=yes
```
### Build
```
waf build
```
