ARG BASE_IMAGE
FROM $BASE_IMAGE
ENV TZ=UTC
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    curl clang build-essential libudev-dev cmake python3 python3-pip libavahi-client-dev libavahi-core-dev git rsync libcurl4-openssl-dev \
    && rm -rf /var/lib/apt/lists/*
RUN update-alternatives --install /usr/bin/python python /usr/bin/python3 1
RUN curl --insecure -o waf.tar.bz2 https://waf.io/waf-2.0.22.tar.bz2 && \
    tar xjvf waf.tar.bz2 && \
    cd waf-2.0.22 && \
    python waf-light --tools=clang_compilation_database && \
    mv waf /usr/bin && \
    cd .. && \
    rm -rf waf-2.0.22 && \
    waf -h
RUN pip3 install --no-cache-dir setuptools && \
    pip3 install --no-cache-dir git+https://github.com/larsks/dockerize
VOLUME /uav-router
WORKDIR /uav-router
ENTRYPOINT ["waf"]
CMD [ "--help" ]