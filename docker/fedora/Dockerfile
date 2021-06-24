ARG BASE_IMAGE
FROM $BASE_IMAGE
RUN dnf install -y clang systemd-devel cmake python waf avahi-devel
COPY configure build /usr/bin/
