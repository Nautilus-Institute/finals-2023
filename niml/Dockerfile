FROM php:7 as builder

RUN apt-get update && apt-cache search clang-16 && apt-get install -y git curl clang

RUN git clone https://github.com/CopernicaMarketingSoftware/PHP-CPP.git && cd PHP-CPP && git checkout v2.3.5

ADD fix.patch PHP-CPP/

RUN cd PHP-CPP && git apply fix.patch && make && make install

WORKDIR /ext

ADD Makefile niml.cpp /ext/

RUN make

WORKDIR /src

FROM php:7

COPY --from=builder /usr/lib/libphpcpp.so /usr/lib/libphpcpp.so
COPY --from=builder /ext/niml.so /usr/local/lib/php/extensions/no-debug-non-zts-20190902/libniml.so
COPY niml.ini /usr/local/etc/php/conf.d/niml.ini
COPY server /src/server
COPY chal_run.sh /src/run.sh

RUN ldconfig && mkdir -p /src/server/posts && chmod +w /src/server/posts && mkdir -p /src/server/users && chmod +w /src/server/users && mkdir /src/server/log && chmod +w /src/server/log

CMD ["/src/run.sh"]
