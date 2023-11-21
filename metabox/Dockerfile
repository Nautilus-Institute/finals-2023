FROM ubuntu:22.04 as builder

RUN apt-get update && apt-get install -y clang wget

RUN apt-get install -y build-essential make

RUN apt-get install -y libreadline-dev

WORKDIR /src

ADD Makefile /src/Makefile

RUN make lua-5.3.6/src/liblua.a

COPY src/ /src/src/
ADD strip.sh /src/


RUN make bin/ulu && cp /src/bin/ulu /bin/ulu

ADD poller/ /src/poller/

RUN mkdir /src/natives && mkdir /src/objects && make objects

FROM ubuntu:22.04 as challenge

# readline, curses 
RUN apt-get update && apt-get install -y libreadline8 libncurses6

COPY --from=builder /bin/ulu /bin/ulu
COPY --from=builder /src/natives /natives
COPY --from=builder /src/objects /objects
COPY connect.sh /connect.sh

ENV TERM=linux
ENV COLUMNS=110
ENV LINES=60

RUN mkdir -p /comp/bin && mkdir -p /comp/lib/x86_64-linux-gnu  && mkdir -p /comp/lib64 && cp /lib/x86_64-linux-gnu/libc.so.6 /comp/lib/x86_64-linux-gnu/. \
    && cp /lib64/ld-linux-x86-64.so.2 /comp/lib64/. \
    && cp /bin/sh /comp/bin/. \
    && cp /bin/cat /comp/bin/. \
    && cp /usr/bin/chmod /comp/bin/. \
    && cp /bin/cat /comp/bin/whoami

CMD ["/bin/bash","-c","/bin/ulu 2>/var/log/ulu.log"]
