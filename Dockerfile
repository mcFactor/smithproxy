# Use an official Python runtime as a parent image
FROM ubuntu:18.04

# Set the working directory to /app
WORKDIR /app

RUN apt update && apt install -y \
wget \
python-pip 

RUN wget http://www.mag0.net/out/smithproxy/0.8/Linux-Ubuntu-18.04/smithproxy_0.8.3-1_amd64.deb &&  \
wget http://www.mag0.net/out/smithproxy/0.8/python-pylibconfig2_0.2.5-1_all.deb && \
apt install -y libcli1.9 libconfig++9v5 libssl1.0.0 libunwind8 libconfig++ python-pip && \
dpkg -i  python-pylibconfig2_0.2.5-1_all.deb && \
apt install -y iptables python-ldap python-pyparsing python-posix-ipc python-soappy python-m2crypto telnet iproute2 && \
dpkg -i  smithproxy_0.8.3-1_amd64.deb


# Define environment variable

# Run app.py when the container launches
CMD ["/bin/bash",]