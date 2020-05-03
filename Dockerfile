FROM ubuntu:20.04 as builder

LABEL maintainer="Mate Soos"
LABEL version="1.0"
LABEL Description="Bosphorus"
ENV DEBIAN_FRONTEND=noninteractive

# get curl, etc
RUN apt-get update
# RUN add-apt-repository -y ppa:ubuntu-toolchain-r/test
RUN apt-get install --no-install-recommends -y software-properties-common libboost-program-options-dev libboost-test-dev gcc g++ make cmake zlib1g-dev wget autoconf automake make

# get M4RI
WORKDIR /
RUN wget https://bitbucket.org/malb/m4ri/downloads/m4ri-20140914.tar.gz \
    && tar -xvf m4ri-20140914.tar.gz
WORKDIR m4ri-20140914
RUN ./configure \
    && make \
    && make install \
    && make clean


WORKDIR /
RUN wget https://github.com/BRiAl/BRiAl/releases/download/1.2.7/brial-1.2.7.tar.bz2 \
    && tar -xjf brial-1.2.7.tar.bz2
WORKDIR /brial-1.2.7

RUN apt-get install --no-install-recommends -y pkg-config libgd-dev
RUN ./configure
RUN make -j4
RUN make install


# build CMS
WORKDIR /
RUN wget https://github.com/msoos/cryptominisat/archive/5.7.1.tar.gz \
    && tar -xvf 5.7.1.tar.gz
WORKDIR /cryptominisat-5.7.1
RUN mkdir build
WORKDIR /cryptominisat-5.7.1/build
RUN cmake -DSTATICCOMPILE=ON .. \
    && make -j2 \
    && make install \
    && rm -rf *

# build Bosphorus
USER root
COPY . /bosphorus
WORKDIR /bosphorus
RUN mkdir -p build
WORKDIR /bosphorus/build
RUN rm -rf *
RUN cmake -DSTATICCOMPILE=ON .. \
    && make -j2

# set up for running
FROM alpine:latest
COPY --from=builder /bosphorus/build/bosphorus /usr/local/bin/
COPY --from=builder /usr/local/bin/cryptominisat5 /usr/local/bin/
WORKDIR /usr/local/bin/
ENTRYPOINT ["/usr/local/bin/bosphorus"]

# --------------------
# HOW TO USE
# --------------------
# you want to run the file `myfile.anf`:
# docker run --rm -v `pwd`/:/dat/ bosphorus --anfread /dat/myfile.anf --cnfwrite /dat/out --solve --allsol
