#!/bin/bash

cat << EOF > ./server-cert.cfg
# GnuTLS Self-Signed Certificate Template
#

# CN (Common Name)
cn = "TestServer"

# Use an RSA key
key_type = "rsa"

# Key size
rsa_bits = 2048

# This is a self-signed certificate
signing_key
encryption_key
ca

# This certificate is for a TLS server
tls_www_server

# Expiration (in days)
expiration_days = 365
"
EOF

certtool --generate-privkey --outfile server-key.pem --template server-cert.cfg
certtool --generate-self-signed --load-privkey server-key.pem --outfile server-cert.pem --template server-cert.cfg
