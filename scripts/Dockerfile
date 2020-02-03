#build enviroment, can reuse on local
FROM alpine as buildenv
RUN sed -i 's/dl-cdn.alpinelinux.org/mirrors.tuna.tsinghua.edu.cn/g' /etc/apk/repositories &&\
apk --no-cache --update add boost-dev boost-static build-base cmake git 

# build the solc
FROM buildenv as builder
#Establish working directory as solidity
WORKDIR /solidity
#Copy working directory on travis to the image
COPY / $WORKDIR

#Build release, delete all source code.
RUN \
sed -i -E -e 's/include <sys\/poll.h>/include <poll.h>/' /usr/include/boost/asio/detail/socket_types.hpp &&\
cmake -DCMAKE_BUILD_TYPE=Release -DTESTS=0 -DSOLC_LINK_STATIC=1 &&\
make solc && install -s  solc/solc /usr/bin	&&\
cd / && rm -rf solidity 

# the production image
FROM scratch
LABEL maintainer="darlzan<darlzan@foxmail.com>"
COPY --from=builder /usr/bin/solc /usr/bin/
ENTRYPOINT [ "/usr/bin/solc" ]