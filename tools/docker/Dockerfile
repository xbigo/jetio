# The build environment of jetio on Linux. The build context is tools/ (see jetio-dev).
FROM ubuntu:26.04

COPY setup/linux-apt.sh /tmp/setup/linux-apt.sh
RUN /tmp/setup/linux-apt.sh && rm -rf /var/lib/apt/lists/* /tmp/setup
