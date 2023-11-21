FROM ubuntu:jammy-20230624

RUN mkdir -p /poller
WORKDIR /poller

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update
RUN apt-get install -y software-properties-common
RUN add-apt-repository -y ppa:deadsnakes/ppa
RUN apt-get update
RUN apt-get install -y python3.11-full

ADD requirements.txt /poller/

RUN python3.11 -m ensurepip
RUN python3.11 -m venv .venv
RUN . .venv/bin/activate && \
    pip install -r requirements.txt

ADD poller.py /poller/

CMD . .venv/bin/activate && exec python poller.py

