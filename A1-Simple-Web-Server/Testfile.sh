echo -e -n "GET / HTTP/1.0\r\n\r\n" | nc -u -s 192.168.1.100 10.10.1.100 8000 &
echo -e -n "GET / HTTP/1.1\r\n\r\n" | nc -u -s 192.168.1.100 10.10.1.100 8000 &
echo -e -n "GET /nofile HTTP/1.0\r\n\r\n" | nc -u -s 192.168.1.100 10.10.1.100 8000 &
echo -e -n "get / HTTP/1.0\r\n\r\n" | nc -u -s 192.168.1.100 10.10.1.100 8000 &
