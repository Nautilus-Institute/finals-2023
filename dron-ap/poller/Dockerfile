FROM ubuntu
RUN apt-get update
RUN apt-get install -y --no-install-recommends kmod iproute2 wpasupplicant python3 scapy wireless-tools tcpdump hostapd socat openssh-server
RUN apt-get install -y iw
RUN apt-get install -y nano netcat tmux
RUN apt-get install -y libdigest-sha-perl psmisc wireless-regdb

COPY src/ /src/

ENTRYPOINT ["/src/init.sh"]
