# multiproxy
Multiproxy is a high-performance TCP proxy server capable to run multiple proxies at the same time. Supports up to 1023 proxies and 1024 active connections (configured in source code). It's a single-threaded and single-process application using epoll() for non-blocking and edge-triggered operations.  
It can proxy any TCP traffic including TLS and file transferring (e.g. scp, ssh).

## Compilation
```bash
git clone https://github.com/guzlewski/multiproxy
cd multiproxy
make
make nolog
```
`make nolog` - enable log output to console
`make nolog` - disable log output to console, may improve performance

## Usage
```
./multiproxy.out local_host1:local_port1:remote_host1:remote_port1 local_host2:local_port2:remote_host2:remote_port2 [...]
```
`local_host1` - IP or hostname that the first proxy is bound to and listening  
`local_port1` - port number that the first proxy is bound to and listening  
`remote_host1` - IP or hostname that the first proxy is redirecting traffic to  
`remote_port1` - port number that the first proxy is redirecting traffic to  

Second and following proxies are optional.

`local_host2` - IP or hostname that the second proxy is bound to and listening  
`local_port2` - port number that the second proxy is bound to and listening  
`remote_host2` - IP or hostname that the second proxy is redirecting traffic to  
`remote_port2` - port number that the second proxy is redirecting traffic to  

## Examples
```
./multiproxy.out 0.0.0.0:80:127.0.0.1:8080 0.0.0.0:443:127.0.0.1:8443
```
 Expose local web server listening on 8080 and 8443 to public IP and ports 80 and 443. 

```
./multiproxy.out 0.0.0.0:22000:192.168.1.44:22
```
Expose the ssh server of the computer in a private LAN network to public access on port 22000 and your IP.

```
./multiproxy.out 10.10.10.20:22:192.168.1.44:22
```
Expose ssh server of computer in private LAN network to other private network access on port 22 and 10.10.10.20 IP.
