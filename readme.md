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
### Filters & Protocols
- [x] Mavlink v1 protocol recognizer (__implemented__)
- [x] Mavlink v1 SysID-CompID filter (__implemented__)
- [x] Mavlink v1 MsgID filter (__implemented__)
- [x] Mavlink v1 MsgID frequency reducer (__implemented__)
- [x] UBX protocol recognizer (__implemented__)
- [x] NMEA protocol recognizer (__implemented__)
- [x] RTCM3 protocol recognizer (__implemented__)
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
- [ ] Router tables
- [ ] Endpoint creation
### Plugins
- [ ] Filter plugins
- [ ] General plugins