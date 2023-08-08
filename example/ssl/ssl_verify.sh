#!/bin/bash

HOST=`hostname`
PORT=50055

#openssl s_client -connect ${HOST}:${PORT} \
#  -cert ./certs/client.cert -key ./certs/client.key -CAfile ./certs/bundleCA.cert -state -debug

openssl s_client -connect ${HOST}:${PORT} \
  -cert ./certs/client.cert -key ./certs/client.key -CAfile ./certs/bundleCA.cert -state


