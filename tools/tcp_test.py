import argparse
import json
import socket


def main():
    parser = argparse.ArgumentParser(description="ESP32-S3-ETH JSON TCP test client")
    parser.add_argument("--host", default="192.168.3.200")
    parser.add_argument("--port", type=int, default=5000)
    parser.add_argument("--cmd", default='{"id":1,"cmd":"status"}')
    args = parser.parse_args()

    obj = json.loads(args.cmd)
    payload = (json.dumps(obj, separators=(",", ":")) + "\n").encode()

    with socket.create_connection((args.host, args.port), timeout=3.0) as sock:
        sock.sendall(payload)
        file = sock.makefile("r", encoding="utf-8", newline="\n")
        line = file.readline()
        if not line:
            raise RuntimeError("Connection closed without response")
        response = json.loads(line)
        print(json.dumps(response, indent=2))


if __name__ == "__main__":
    main()
