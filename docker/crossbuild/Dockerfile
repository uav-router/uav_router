FROM uav-router_builder:amd64
COPY sysroot /sysroot
RUN ln -s /sysroot/arm64/usr/lib64/clang/12.0.1/lib/libclang_rt.* /usr/lib64/clang/12.0.1/lib/linux/
RUN ln -s /sysroot/arm32/usr/lib/clang/12.0.1/lib/libclang_rt.* /usr/lib64/clang/12.0.1/lib/linux/
RUN ln -s usr/lib /sysroot/arm32/lib
RUN ln -s usr/lib /sysroot/arm64/lib
RUN ln -s usr/lib64 /sysroot/arm64/lib64
ENV WAFLOCK=".lock-waf_cross_build-arm64"