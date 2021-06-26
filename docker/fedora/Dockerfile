ARG BASE_IMAGE
FROM $BASE_IMAGE
RUN dnf install -y clang systemd-devel cmake python python-pip waf avahi-devel git rsync \
    --nodocs --setopt install_weak_deps=False && \
    dnf clean all -y
RUN pip install --no-cache-dir setuptools && \
    pip install --no-cache-dir git+https://github.com/larsks/dockerize
COPY configure build image /usr/bin/
RUN mkdir /app && chmod o+w /app

