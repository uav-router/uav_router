ARG BASE_IMAGE
FROM $BASE_IMAGE
RUN dnf install -y clang llvm lld compiler-rt systemd-devel cmake python python-pip waf avahi-devel git rsync libcurl-devel\
    --nodocs --setopt install_weak_deps=False && \
    dnf clean all -y
RUN pip install --no-cache-dir setuptools && \
    pip install --no-cache-dir git+https://github.com/larsks/dockerize
VOLUME /uav-router
WORKDIR /uav-router
ENTRYPOINT ["waf"]
CMD [ "--help" ]
ARG WAFLOCK
ENV WAFLOCK="${WAFLOCK}"
