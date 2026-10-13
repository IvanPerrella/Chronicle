# Cross-build the Windows port from Docker Desktop's Linux engine. No Windows
# compiler, CMake, Python, 7-Zip, or Vulkan SDK is needed on the host.
FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates cmake curl git ninja-build python3 tar unzip xz-utils \
    && rm -rf /var/lib/apt/lists/*

ARG LLVM_MINGW_VERSION=20260922
ARG SDL_VERSION=3.4.18
ARG GLSLANG_VERSION=16.6.0
ARG VULKAN_VERSION=1.4.363.0

RUN curl -fL --retry 3 -o /tmp/llvm-mingw.tar.xz \
      "https://github.com/mstorsjo/llvm-mingw/releases/download/${LLVM_MINGW_VERSION}/llvm-mingw-${LLVM_MINGW_VERSION}-ucrt-ubuntu-22.04-x86_64.tar.xz" \
    && mkdir -p /opt/llvm-mingw \
    && tar -xJf /tmp/llvm-mingw.tar.xz --strip-components=1 -C /opt/llvm-mingw \
    && rm /tmp/llvm-mingw.tar.xz
ENV PATH="/opt/llvm-mingw/bin:${PATH}"

RUN curl -fL --retry 3 -o /tmp/sdl.zip \
      "https://github.com/libsdl-org/SDL/releases/download/release-${SDL_VERSION}/SDL3-devel-${SDL_VERSION}-mingw.zip" \
    && mkdir -p /opt/sdl \
    && unzip -q /tmp/sdl.zip -d /opt/sdl \
    && rm /tmp/sdl.zip

RUN curl -fL --retry 3 -o /tmp/glslang.zip \
      "https://github.com/KhronosGroup/glslang/releases/download/${GLSLANG_VERSION}/glslang-${GLSLANG_VERSION}-linux-x86_64-release.zip" \
    && mkdir -p /opt/glslang \
    && unzip -q /tmp/glslang.zip -d /opt/glslang \
    && rm /tmp/glslang.zip

RUN curl -fL --retry 3 -o /tmp/vulkan.zip \
      "https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/vulkan-sdk-${VULKAN_VERSION}.zip" \
    && mkdir -p /opt/vulkan \
    && unzip -q /tmp/vulkan.zip -d /opt/vulkan \
    && rm /tmp/vulkan.zip \
    && curl -fL --retry 3 -o /opt/vulkan/vulkan-1.def \
      "https://raw.githubusercontent.com/KhronosGroup/Vulkan-Loader/vulkan-sdk-${VULKAN_VERSION}/loader/vulkan-1.def" \
    && llvm-dlltool -m i386:x86-64 -d /opt/vulkan/vulkan-1.def -l /opt/vulkan/libvulkan-1.dll.a

COPY build-container.sh /opt/chronicle/build-container.sh
COPY toolchain.cmake /opt/chronicle/toolchain.cmake
RUN chmod +x /opt/chronicle/build-container.sh
WORKDIR /src
ENTRYPOINT ["/opt/chronicle/build-container.sh"]
