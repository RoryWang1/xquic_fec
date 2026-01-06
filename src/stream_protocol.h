#ifndef CAMERA_PROTOCOL_H
#define CAMERA_PROTOCOL_H

// The ALPN (Application-Layer Protocol Negotiation) identifier.
// Client and Server must match this to talk to each other.
#define CAMERA_ALPN "camera-stream"

// Size of chunks we read from Stdin and write to QUIC.
// 32KB is a reasonable balance between latency and throughput.
#define VIDEO_CHUNK_SIZE (32 * 1024)

#endif
