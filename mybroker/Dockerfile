FROM ubuntu:jammy-20230624

RUN mkdir /service
WORKDIR /service

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update
RUN apt-get install -y software-properties-common
RUN add-apt-repository -y ppa:deadsnakes/ppa
RUN apt-get update
RUN apt-get install -y python3.11-full
RUN apt-get install -y nginx socat

# FIXME: Only add necessary files!
ADD requirements.txt /service/

RUN python3.11 -m ensurepip
RUN python3.11 -m venv .venv
RUN . .venv/bin/activate && \
    pip install -r requirements.txt

ADD web/*.py /service/web/
ADD web/templates /service/web/templates
ADD web/css /service/web/css
ADD fileman/target/release/fileman /service/fileman
ADD fileup/server /service/fileup
ADD kvstore/server /service/kvstore
ADD inventory/inventory /service/inventory
ADD integrity/integrity.dist /service/integrity.dist
ADD run_all.sh /service/run_all.sh
ADD nginx_config /etc/nginx/sites-available/default

RUN mkdir /data

ENV DATABASE=/data/database.db
ENV APP_INIT=1
CMD ./run_all.sh

