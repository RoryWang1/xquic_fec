import socket
import time
import argparse
import threading
import sys

def run_server(port, expected_count):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(('127.0.0.1', port))
    sock.settimeout(5.0)  # Stop if no packets for 5 seconds

    print(f"Server listening on 127.0.0.1:{port}")
    
    received_count = 0
    start_time = time.time()
    
    try:
        while True:
            try:
                data, addr = sock.recvfrom(1024)
                received_count += 1
                # print(f"Received packet {received_count} from {addr}")
                if received_count >= expected_count:
                    break
            except socket.timeout:
                print("Server timeout waiting for packets")
                break
    except KeyboardInterrupt:
        pass
    finally:
        sock.close()
        
    print(f"Server finished. Received: {received_count}")
    return received_count

def run_client(target_ip, target_port, count, delay, src_port=0):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    if src_port != 0:
        sock.bind(('127.0.0.1', src_port))
        print(f"Client bound to source port {src_port}")
        
    print(f"Client sending {count} packets to {target_ip}:{target_port}")
    
    for i in range(count):
        msg = f"Packet {i}".encode()
        sock.sendto(msg, (target_ip, target_port))
        if delay > 0:
            time.sleep(delay)
            
    sock.close()
    print("Client finished sending")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="UDP Packet Loss Tester")
    parser.add_argument("--listen-port", type=int, default=5000, help="Server listening port")
    parser.add_argument("--send-port", type=int, default=0, help="Client source port (0=random)")
    parser.add_argument("--target-port", type=int, default=5000, help="Client target port")
    parser.add_argument("--count", type=int, default=100, help="Number of packets")
    parser.add_argument("--delay", type=float, default=0.01, help="Delay between packets (s)")
    
    args = parser.parse_args()
    
    # Run server
    server_thread = threading.Thread(target=run_server, args=(args.listen_port, args.count))
    server_thread.daemon = True
    server_thread.start()
    
    time.sleep(1) 
    
    # Run client
    run_client('127.0.0.1', args.target_port, args.count, args.delay, args.send_port)
    
    # Wait for completion (give server time to receive delayed packets)
    time.sleep(2) 
    
    if server_thread.is_alive():
        print("Waiting for delayed packets...")
        server_thread.join(timeout=5)
        
    print("Test Complete")
