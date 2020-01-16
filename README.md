# multiproxy
Multiproxy is high performance tcp proxy server with ability to listen on many ports and send to many hosts.  
Supports up to 1023 proxies and 1024 active connections at the same time (can be increased easly if needed).  
Thanks to epoll() is very fast and low cpu usage (all operations are non blocking and edge triggered).  
It can proxy ssl traffic and files uploading / downloading (eg. scp).   

## Compilation
```bash
git clone https://github.com/guzlewski/multiproxy
cd multiproxy
make
make nolog
```
make nolog - disable output to console about new and closed connections

## Usage

```
./multiproxy.out localport1:host1:hostport1 localport2:host2:hostport2 [...]
```

## Examples
```
./multiproxy.out 80:127.0.0.1:8080 443:127.0.0.1:8443
```
Expose local web server listening on 8080 and 8443 to public ip and ports 80 and 443.
 

```
./multiproxy.out 22000:192.168.1.44:22
```
Expose ssh server of computer in private lan network to public access on port 22000 and your's ip.
 

## License
Copyright (c) Michał Guźlewski. All rights reserved.
