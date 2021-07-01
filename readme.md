# UAV-Router
This project is not implemented already.
## Features
### Communications
- [x] UART endpoint (__basic tested__)
- [x] USB UART features (__basic tested__)
    - connect/disconnect endpoint when device attach/detach
    - use `/dev/serial/by-id/*` device name for connection even any other alias specified
- [x] TCP client and server endpoints (__basic tested__)
- [x] UDP client and server endpoints (__basic tested__)
- [x] UDP broadcast and multicast endpoints (__basic tested__)
- [x] Zeroconf name resolution in both direction (__basic tested__) : 
    - clients connect to servers by its names
    - servers resolve clients names after connection
- [x] File output enpoint (__basic tested__)
    - file or stdout/stderr output
    - use color for output
### Filters & Protocols
- [x] Mavlink v1 protocol recognizer (__basic tested__)
- [x] Mavlink v1 SysID-CompID filter (__implemented__)
- [x] Mavlink v1 MsgID filter (__implemented__)
- [x] Mavlink v1 MsgID frequency reducer (__implemented__)
- [ ] Mavlink v2 protocol recognizer
- [ ] Mavlink v2 filters
- [x] UBX protocol recognizer (__basic tested__)
- [x] NMEA protocol recognizer (__implemented__)
- [x] RTCM3 protocol recognizer (__basic tested__)
- [x] Decode binary stream to ASCII hexadecimal format  (__basic tested__)
### Monitoring
- [x] Collect endpoints read/write bytes (__basic tested__)
- [x] Collect endpoints read/write bytes (__basic tested__)
- [x] Collect io loop timings (__basic tested__)
- [x] Collect udev connect/disconnect events (__basic tested__)
- [x] Collect zeroconf add/remove service events (__basic tested__)
- [x] Collect number of recognized packets and corrupted packets in a filter (__implemented__)
- [x] Collect number of selected stream bytes in a filter (__implemented__)
- [x] Collect number of rejected stream bytes in a filter (__implemented__)
- [x] Collect number of dropped packets by sysid, compid, msgid and frequency filter (__implemented__)
- [ ] Collect UART interrupt states TIOCGICOUNT
- [x] Implement monitoring with InfluxDB UDP protocol  (__implemented__)
- [x] Write monitoring data to file using InfluxDB line protocol (__basic tested__)
### Router
- [x] Router tables (__basic tested__)
- [x] Endpoint creation (__basic tested__)
- [x] Expand environment variables in config file with defaults (__basic tested__)
- [ ] Reload config file and reconfigure system when config file changed
### Plugins
- [ ] Filter plugins
- [ ] General plugins
### Docker support
- [X] Docker build container (__basic tested__)
- [X] Docker execute container (__basic tested__)
    - fedora based
    - ubuntu based
    - scratch run images
- [ ] DockerHub hosted images
### Others
- [ ] Investigate of using crash handling (sentry?, breakpad?, crashpad?)
- [ ] Implement global configuration
    - switch off zeroconf
    - switch on Ctrl-C handler
    - switch off udev monitoring
    - using crash handler
- [ ] Logging. Make stdout/stderr/file output
- [X] Divide binaries to:
    - base library (errors handling, logging)
    - io library (epoll, sockets, uarts, udev, zeroconf)
    - router app

## Build
```
git clone --recurse-submodules https://github.com/uav-router/uav_router.git
cd uav_router
docker/configure
docker/build
```
### Cross build
Install qemu properly. (See below)
```
docker/configure arm32 #arm64 amd64
docker/build arm32 #arm64 amd64
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
docker/build
```
### Build Docker image
```
docker/image # amd64 arm64 arm32
```
## Run
Run docker container with image
```
docker/uav-router /path/to/config/file.yml
```