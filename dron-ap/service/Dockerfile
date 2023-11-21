FROM ubuntu
RUN apt-get update && apt-get install -y --no-install-recommends kmod iproute2 wpasupplicant python3 scapy wireless-tools tcpdump hostapd socat openssh-server iw nano netcat tmux libdigest-sha-perl psmisc wireless-regdb && apt-get clean autoclean &&  apt-get autoremove --yes && rm -rf /var/lib/{apt,dpkg,cache,log}/
#RUN apt-get install net-tools
COPY src/init.sh /
COPY src/authorized_keys /root/.ssh/
COPY src/blyat-py /blyat-py
COPY blyat.ko /lib/modules/
COPY flag /flag

RUN mkdir /run/sshd
COPY sshd_config /etc/ssh/sshd_config
COPY passwd /etc/passwd

RUN mkdir /radios/
COPY src/h.conf /radios/h.conf
COPY src/psk /etc/psk

ENTRYPOINT ["/init.sh"]
