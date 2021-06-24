ARG BASE_IMAGE
FROM $BASE_IMAGE
RUN apt-get update && apt-get install -y curl clang libudev-dev cmake python libavahi-client-dev libavahi-core-dev
RUN curl -o waf.tar.bz2 https://waf.io/waf-2.0.22.tar.bz2 && \
    tar xjvf waf.tar.bz2 && \
    cd waf-2.0.22 && \
    python waf-light --tools=clang_compilation_database && \
    mv waf /usr/bin && \
    cd .. && \
    rm -rf waf-2.0.22 && \
    waf -h
COPY configure build /usr/bin/
