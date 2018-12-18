FROM ubuntu:16.04 as builder

LABEL maintainer="Mate Soos"
LABEL version="1.0"
LABEL Description="Bosphorus"

# get curl, etc
RUN apt-get update && apt-get install --no-install-recommends -y software-properties-common \
    && rm -rf /var/lib/apt/lists/*

RUN add-apt-repository -y ppa:ubuntu-toolchain-r/test \
    && rm -rf /var/lib/apt/lists/*

RUN apt-get update \
    && apt-get install --no-install-recommends -y libboost-program-options-dev gcc g++ make cmake zlib1g-dev wget autoconf automake make libtool \
    && rm -rf /var/lib/apt/lists/*

# set up build env
# RUN groupadd -r solver -g 433
# RUN useradd -u 431 -r -g solver -d /home/solver -s /sbin/nologin -c "Docker image user" solver
# RUN mkdir -p /home/solver/bosphorus
# RUN chown -R solver:solver /home/solver

# get M4RI
WORKDIR /
RUN wget https://bitbucket.org/malb/m4ri/downloads/m4ri-20140914.tar.gz \
    && tar -xvf m4ri-20140914.tar.gz
WORKDIR m4ri-20140914
RUN ./configure \
    && make \
    && make install \
    && make clean

# build CMS
WORKDIR /
RUN git clone --depth 1 https://github.com/msoos/cryptominisat.git
WORKDIR /cryptominisat
RUN mkdir build
WORKDIR /cryptominisat/build
RUN cmake .. \
    && make -j6 \
    && make install \
    && rm -rf *

# build Brial
WORKDIR /
RUN git clone --depth 1 https://github.com/BRiAl/BRiAl
WORKDIR /BRiAl
RUN aclocal \
    && autoheader \
    && libtoolize --copy \
    && automake --copy --add-missing \
    && automake \
    && autoconf \
    && ./configure \
    && make -j4 \
    && make install
    && rm -rf *

# build Bosphorus
USER root
COPY . /home/solver/bosphorus
WORKDIR /home/solver/bosphorus
RUN mkdir build
WORKDIR /home/solver/bosphorus/build
RUN cmake .. \
    && make -j6 \
    && make install \
    && rm -rf *

# set up for running
FROM alpine:latest
COPY --from=builder /usr/local/bin/* /usr/local/bin/
COPY --from=builder /usr/local/bin/lib/* /usr/local/lib/
ENTRYPOINT ["/usr/local/bin/bosphorus"]

# --------------------
# HOW TO USE
# --------------------
# you want to run the file `myfile.anf`:
# docker run --rm -v `pwd`/myfile.anf:/in bosphorus in


