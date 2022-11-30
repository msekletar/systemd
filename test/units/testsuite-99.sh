#!/usr/bin/env bash
set -eux

systemd-analyze log-level debug

cat >/run/systemd/system/testsuite-99-restart-mode.socket <<EOF
[Socket]
ListenStream=/run/testsuite-99-restart-mode.sock
EOF

cat >/run/systemd/system/testsuite-99-restart-mode.service <<EOF
[Unit]
Description=RestarMode test service

[Service]
ExecStart=/usr/local/bin/service.py

RestartMode=retain
ExecPassivate=/bin/kill -USR1 $MAINPID
EOF

systemctl daemon-reload

# Start the test service
systemctl start testsuite-99-restart-mode.service
systemctl status testsuite-99-restart-mode.service

# Run 10 clients on the background
for i in {1..10}; do
    nc -U /run/restart-mode-service.sock > "/tmp/client-$i.log" &
done

# Restart and wait for the it to complete
systemctl restart testsuite-99-restart-mode.service

# Wait for all clients to finish
wait

# Connect one more client now after service is restarted
nc -U /run/restart-mode-service.sock > /tmp/client-11.log

# Verify that we got 50 (5 for each client started before restart of the service) lines
# with generation id 1 and 5 with generation id 2 for the last client started after restart
for i in {1..10}; do
    [ "$(grep -c "GENERATION_ID=1" "/tmp/client-$i.log")" = "5" ]
done
[ "$(grep -c "GENERATION_ID=2" "/tmp/client-11.log")" = "5" ]

systemd-analyze log-level info

echo OK >/testok

exit 0
