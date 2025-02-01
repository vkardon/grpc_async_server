#!/bin/bash
# Fri Aug  4 03:07:52 PDT 2023
# By vkardons

# Based on tutorial from https://www.golinuxcloud.com/openssl-create-certificate-chain-linux/

# Ask for server name to generate certificate for
if [ -z "$1" ]; then
   echo "Please specify the server name (including domain) or press 'Enter' for '"`hostname`"':"
   read SERVER_HOSTNAME
   if [ -z "$SERVER_HOSTNAME" ]; then
      SERVER_HOSTNAME=`hostname`
   fi
fi

#echo "SERVER_HOSTNAME=${SERVER_HOSTNAME}"

# Set common part of certificat subject entry 
SUBJECT="/C=US/ST=California/L=San Francisco/O=SSNC/OU=IT Department"

# Set Root/Intermediate CA sub-directories
rm -rf ./myCA
mkdir -p ./myCA/rootCA/{certs,crl,newcerts,private,csr}
mkdir -p ./myCA/intermediateCA/{certs,crl,newcerts,private,csr}

# Set CA directory in Root/Intermediate CA config files
SCRIPTDIR=$(dirname "$(realpath "$0")")
CA_DIR=${PWD//\//\\/}  # Note: we use parenthesis expansion to escape the slashes
cat ${SCRIPTDIR}/root_ca.cnf | sed "s/__ROOT_CA_DIR__/${CA_DIR}\/myCA\/rootCA/g" > ./myCA/root_ca.cnf
cat ${SCRIPTDIR}/intermediate_ca.cnf | sed "s/__INTERMEDIATE_CA_DIR__/${CA_DIR}\/myCA\/intermediateCA/g" > ./myCA/intermediate_ca.cnf

# Set server hostname in Intermediate CA configuration file
sed -i "s/__SERVER_HOSTNAME__/${SERVER_HOSTNAME}/g" ./myCA/intermediate_ca.cnf

# Create CA files
echo 1000 > ./myCA/rootCA/serial
echo 1000 > ./myCA/intermediateCA/serial

echo 0100 > ./myCA/rootCA/crlnumber 
echo 0100 > ./myCA/intermediateCA/crlnumber

touch ./myCA/rootCA/index.txt
touch ./myCA/intermediateCA/index.txt

echo
echo "### Create an RSA key pair for the root CA without a password"
openssl genrsa -out ./myCA/rootCA/private/ca.key.pem 4096

#echo
#echo "### View the content of private key"
#openssl rsa -noout -text -in ./myCA/rootCA/private/ca.key.pem

echo
echo "### Create Root Certificate Authority Certificate"
openssl req -config ./myCA/root_ca.cnf -key ./myCA/rootCA/private/ca.key.pem -new -x509 -days 7300 -sha256 -extensions v3_ca -out ./myCA/rootCA/certs/ca.cert.pem -subj "${SUBJECT}/CN=Root CA"

#echo
#echo "### Verify root CA certificate"
#openssl x509 -noout -text -in ./myCA/rootCA/certs/ca.cert.pem

echo
echo "### Generate the intermediate CA key pair and certificate"
openssl genrsa -out ./myCA/intermediateCA/private/intermediate.key.pem 4096

echo
echo "### Create the intermediate CA certificate signing request (CSR)"
openssl req -config ./myCA/intermediate_ca.cnf -key ./myCA/intermediateCA/private/intermediate.key.pem -new -sha256 -out ./myCA/intermediateCA/certs/intermediate.csr.pem -subj "${SUBJECT}/CN=Intermediate CA"

echo
echo "### Sign the intermediate CSR with the root CA key"
openssl ca -config ./myCA/root_ca.cnf -extensions v3_intermediate_ca -days 3650 -notext -md sha256 -in ./myCA/intermediateCA/certs/intermediate.csr.pem -out ./myCA/intermediateCA/certs/intermediate.cert.pem -batch

echo
echo "### Check index.txt file. It should now contain a line that refers to the intermediate certificate"
cat ./myCA/rootCA/index.txt

#echo
#echo "### Verify the Intermediate CA Certificate content"
#openssl x509 -noout -text -in ./myCA/intermediateCA/certs/intermediate.cert.pem

echo
echo "### Verify intermediate certificate against the root certificate"
openssl verify -CAfile ./myCA/rootCA/certs/ca.cert.pem ./myCA/intermediateCA/certs/intermediate.cert.pem

echo
echo "### Create Certificate Chain (Certificate Bundle)"
cat ./myCA/intermediateCA/certs/intermediate.cert.pem ./myCA/rootCA/certs/ca.cert.pem > ./myCA/intermediateCA/certs/ca-chain.cert.pem

echo
echo "### Verify certificate chain"
openssl verify -CAfile ./myCA/intermediateCA/certs/ca-chain.cert.pem ./myCA/intermediateCA/certs/intermediate.cert.pem

echo
echo "### Create a private key for the server"
openssl genpkey -algorithm RSA -out ./myCA/intermediateCA/private/server.key.pem

echo
echo "### Create a certificate signing request (CSR) for the server"
#openssl req -config ./myCA/intermediate_ca.cnf -key ./myCA/intermediateCA/private/server.key.pem -new -sha256 -out ./myCA/intermediateCA/csr/server.csr.pem

openssl req -config ./myCA/intermediate_ca.cnf -key ./myCA/intermediateCA/private/server.key.pem -new -sha256 -out ./myCA/intermediateCA/csr/server.csr.pem -subj "${SUBJECT}/CN=${SERVER_HOSTNAME}"

echo
echo "### Sign the server CSR with the intermediate CA"
openssl ca -config ./myCA/intermediate_ca.cnf -extensions server_cert -days 375 -notext -md sha256 -in ./myCA/intermediateCA/csr/server.csr.pem -out ./myCA/intermediateCA/certs/server.cert.pem -batch

#echo
#echo "### Verify the server certificate"
#openssl x509 -noout -text -in ./myCA/intermediateCA/certs/server.cert.pem

echo
echo "### Create a private key for the client"
openssl genpkey -algorithm RSA -out ./myCA/intermediateCA/private/client.key.pem

echo
echo "### Create a certificate signing request (CSR) for the client"
#openssl req -config ./myCA/intermediate_ca.cnf -key ./myCA/intermediateCA/private/client.key.pem -new -sha256 -out ./myCA/intermediateCA/csr/client.csr.pem

openssl req -config ./myCA/intermediate_ca.cnf -key ./myCA/intermediateCA/private/client.key.pem -new -sha256 -out ./myCA/intermediateCA/csr/client.csr.pem -subj "${SUBJECT}/CN=Client for ${SERVER_HOSTNAME}"

echo
echo "### Sign the client CSR with the intermediate CA"
openssl ca -config ./myCA/intermediate_ca.cnf -extensions usr_cert -days 375 -notext -md sha256 -in ./myCA/intermediateCA/csr/client.csr.pem -out ./myCA/intermediateCA/certs/client.cert.pem -batch

echo
echo "### Copy certificates and keys"
rm -rf certs
mkdir certs

cp ./myCA/rootCA/certs/ca.cert.pem                     ./certs/rootCA.cert
cp ./myCA/rootCA/private/ca.key.pem                    ./certs/rootCA.key

cp ./myCA/intermediateCA/certs/intermediate.cert.pem   ./certs/intermediateCA.cert 
cp ./myCA/intermediateCA/private/intermediate.key.pem  ./certs/intermediateCA.key 

cp ./myCA/intermediateCA/certs/server.cert.pem         ./certs/server.cert
cp ./myCA/intermediateCA/private/server.key.pem        ./certs/server.key

cp ./myCA/intermediateCA/certs/client.cert.pem         ./certs/client.cert
cp ./myCA/intermediateCA/private/client.key.pem        ./certs/client.key

cp ./myCA/intermediateCA/certs/ca-chain.cert.pem       ./certs/bundleCA.cert 




