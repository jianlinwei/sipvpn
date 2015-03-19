SUBDIRS = src

EXTRA_DIST = man/sipvpn.8 contrib/systemd/sipvpn@.service \
             contrib/sample/client.conf \
             contrib/sample/client_down.sh \
             contrib/sample/client_up.sh \
             contrib/sample/server.conf \
             contrib/sample/server_down.sh \
             contrib/sample/server_up.sh

man_MANS = man/sipvpn.8

systemddir=$(libdir)/systemd/system
systemd_DATA=contrib/systemd/sipvpn@.service

sampledir=$(sysconfdir)/sipvpn/sample
sample_DATA=contrib/sample/client.conf \
            contrib/sample/client_down.sh \
            contrib/sample/client_up.sh \
            contrib/sample/server.conf \
            contrib/sample/server_down.sh \
            contrib/sample/server_up.sh
