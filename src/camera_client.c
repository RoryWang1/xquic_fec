
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <event2/event.h>

#include <xquic/xquic.h>
#include <xquic/xquic_typedef.h>


#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 4433
#define ALPN_NAME "camera-stream"

// Huge buffer for video
#define RECV_BUF_SIZE (16 * 1024 * 1024) 

typedef struct {
    xqc_engine_t *engine;
    struct event_base *eb;
    struct event *ev_engine;
    struct event *ev_socket;
    int socket_fd;
    struct sockaddr_in server_addr;
    xqc_cid_t cid; // Stored CID
} client_ctx_t;

// --- Helper: Get Current Time ---
uint64_t now() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000 + tv.tv_usec;
}

// --- XQUIC Engine Callbacks ---
void client_set_event_timer(xqc_msec_t wake_after, void *user_data) {
    // fprintf(stderr, "[Client] Timer Set: %llu ms\n", (unsigned long long)wake_after); 
    client_ctx_t *ctx = (client_ctx_t *)user_data;
    struct timeval tv;
    tv.tv_sec = wake_after / 1000000;
    tv.tv_usec = wake_after % 1000000;
    event_add(ctx->ev_engine, &tv);
}


void client_write_log(xqc_log_level_t lvl, const void *buf, size_t count, void *user_data) {
    // Output XQUIC debug logs
    fprintf(stderr, "[XQC-Client] %.*s\n", (int)count, (const char*)buf);
    fflush(stderr);
}

// --- Socket I/O ---
ssize_t client_write_socket(const unsigned char *buf, size_t size, 
                          const struct sockaddr *peer_addr, socklen_t peer_addrlen, void *user_data) {
    // client_ctx_t *ctx = (client_ctx_t *)user_data;
    // fprintf(stderr, "Write Socket: %zd bytes\n", size); fflush(stderr);
    // return sendto(ctx->socket_fd, buf, size, 0, peer_addr, peer_addrlen);
    
    // Safer version for debug
    client_ctx_t *ctx = (client_ctx_t *)user_data;
    if (!ctx) {
        fprintf(stderr, "Write Socket: ctx is NULL!\n"); fflush(stderr);
        return -1;
    }
    struct sockaddr_in *sin = (struct sockaddr_in *)peer_addr;
    fprintf(stderr, "Write Socket: %zd bytes to %s:%d (FD %d)\n", size, 
            inet_ntoa(sin->sin_addr), ntohs(sin->sin_port), ctx->socket_fd); 
    fflush(stderr);
    
    ssize_t ret = sendto(ctx->socket_fd, buf, size, 0, peer_addr, peer_addrlen);
    if (ret < 0) {
        fprintf(stderr, "❌ sendto() failed: %s\n", strerror(errno));
    } else {
        fprintf(stderr, "✅ sendto() OK: %zd bytes\n", ret);
    }
    fflush(stderr);
    return ret;
}

// --- Transport Callbacks ---
void client_save_token(const unsigned char *token, uint32_t token_len, void *user_data) {
    // Called when server sends a NEW_TOKEN frame
    // For this simple client, we just log it. A real client might save it for 0-RTT
    fprintf(stderr, "[Client] Received NEW_TOKEN (%u bytes)\n", token_len);
    fflush(stderr);
}

void client_save_session_cb(const char *data, size_t data_len, void *user_data) {
    // Called when server sends session ticket for 0-RTT
    // For this simple client, we just log it
    fprintf(stderr, "[Client] Received SESSION_TICKET (%zu bytes)\n", data_len);
    fflush(stderr);
}

void client_save_tp_cb(const char *data, size_t data_len, void *user_data) {
    // Called to save transport parameters for 0-RTT
    // For this simple client, we just log it
    fprintf(stderr, "[Client] Received TRANSPORT_PARAMS (%zu bytes)\n", data_len);
    fflush(stderr);
}

// --- Cert Verify (Formal Process) ---
int client_cert_verify(const unsigned char *certs[], const size_t cert_lens[], size_t n_certs, void *conn_user_data) {
    fprintf(stderr, "[Client] 🔐 Verifying Server Certificate Chain (%zu certificates)...\n", n_certs);
    
    // In a real formal process, you would:
    // 1. Decode DER to X509
    // 2. Check validity dates
    // 3. Verify signature against Root CA
    // 4. Check Subject/SAN matches hostname
    
    // For this self-signed "localhost" environment:
    if (n_certs > 0) {
        fprintf(stderr, "[Client] ✅ Server Certificate Present (%zu bytes). Trusting Self-Signed (Formal Override).\n", cert_lens[0]);
        return 0; // Success
    }
    
    fprintf(stderr, "[Client] ❌ Certificate Missing!\n");
    return -1; // Fail
}


void client_socket_read_callback(int fd, short what, void *arg) {
    // CRITICAL DIAGNOSTIC - This should appear if callback is triggered
    fprintf(stderr, "\n\n╔═══════════════════════════════════════╗\n");
    fprintf(stderr, "║ CLIENT SOCKET CALLBACK TRIGGERED!!! ║\n");
    fprintf(stderr, "╚═══════════════════════════════════════╝\n");
    fprintf(stderr, "[Client] callback: fd=%d, what=%d\n\n", fd, what);
    fflush(stderr);
    
    client_ctx_t *ctx = (client_ctx_t *)arg;
    unsigned char buf[65535];
    struct sockaddr_in peer_addr;
    socklen_t peer_addrlen;
    ssize_t n;
    int packet_count = 0;
    
    // Read all available packets in a loop (like server)
    do {
        peer_addrlen = sizeof(peer_addr);
        n = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&peer_addr, &peer_addrlen);
        
        if (n < 0 && errno == EAGAIN) {
            break; // No more data
        }
        
        if (n < 0) {
            fprintf(stderr, "[Client] recvfrom error: %s\n", strerror(errno));
            fflush(stderr);
            break;
        }
        
        if (n > 0) {
            packet_count++;
            fprintf(stderr, "[Client] Packet #%d: %zd bytes from %s:%d\n", 
                    packet_count, n, inet_ntoa(peer_addr.sin_addr), ntohs(peer_addr.sin_port));
            fflush(stderr);
            
            // Validate ctx pointer before use
            if (!ctx) {
                fprintf(stderr, "[Client] FATAL: ctx is NULL!\n");
                fflush(stderr);
                return;
            }
            if (!ctx->engine) {
                fprintf(stderr, "[Client] FATAL: ctx->engine is NULL!\n");
                fflush(stderr);
                return;
            }
            
            struct sockaddr_in local_addr;
            socklen_t local_addrlen = sizeof(local_addr);
            if (getsockname(fd, (struct sockaddr *)&local_addr, &local_addrlen) < 0) {
                perror("getsockname");
                break;
            }

            uint64_t recv_time = now();
            
            fprintf(stderr, "[Client] Calling xqc_engine_packet_process (ctx=%p, engine=%p)...\n", 
                    (void*)ctx, (void*)ctx->engine);
            fflush(stderr);
            
            int process_ret = xqc_engine_packet_process(ctx->engine, buf, n, 
                                      (struct sockaddr *)&local_addr, local_addrlen, 
                                      (struct sockaddr *)&peer_addr, peer_addrlen,
                                      recv_time, ctx);
            
            fprintf(stderr, "[Client] xqc_engine_packet_process returned %d\n", process_ret);
            fflush(stderr);
        }
    } while (n > 0);
    
    fprintf(stderr, "[Client] Processed %d packets, calling xqc_engine_finish_recv...\n", packet_count);
    fflush(stderr);
    
    // CRITICAL: Finish receiving (like server does!)
    xqc_engine_finish_recv(ctx->engine);
    
    fprintf(stderr, "[Client] xqc_engine_finish_recv returned OK, callback complete\n");
    fflush(stderr);
}

void client_engine_callback(int fd, short what, void *arg) {
    client_ctx_t *ctx = (client_ctx_t *)arg;
    xqc_engine_main_logic(ctx->engine);
}

// Heartbeat to verify event loop is running
void client_heartbeat_callback(int fd, short what, void *arg) {
    static int count = 0;
    fprintf(stderr, "[Client] 💓 Heartbeat #%d - Event loop is alive\n", ++count);
    fflush(stderr);
}

// --- Stream Callbacks ---
int client_stream_read_notify(xqc_stream_t *stream, void *user_data) {
    // CRITICAL DEBUG - should appear if callback triggers
    write(STDERR_FILENO, "\n\n*** CLIENT STREAM READ NOTIFY TRIGGERED ***\n\n", 48);
    fprintf(stderr, "[Client] stream_read_notify ENTER\n");
    fflush(stderr);
    
    unsigned char buf[32768]; // 32KB buffer
    ssize_t total_bytes = 0;
    
    while (1) {
        unsigned char fin = 0;
        // Fix: xqc_stream_recv(stream, buf, size, &fin) returns bytes read
        ssize_t ret = xqc_stream_recv(stream, buf, sizeof(buf), &fin);
        
        if (ret < 0) {
            if (ret == -XQC_EAGAIN) {
                fprintf(stderr, "[Client] stream_read: EAGAIN (no more data now)\n");
                fflush(stderr);
                break;
            }
            fprintf(stderr, "[Client] Stream Recv Error: %zd\n", ret);
            fflush(stderr);
            break;
        }
        
        size_t bytes = (size_t)ret; // Positive return is bytes read
        
        if (bytes > 0) {
            // CRITICAL: Write to STDOUT for ffplay
            // Use write() which is unbuffered and safer than printf/fwrite
            ssize_t written = 0;
            while (written < (ssize_t)bytes) {
                ssize_t n = write(STDOUT_FILENO, buf + written, bytes - written);
                if (n <= 0) {
                    if (n ==  0 || (errno != EINTR && errno != EAGAIN)) {
                        fprintf(stderr, "[Client] ERROR: stdout write failed after %zd bytes: %s\n",
                                written, strerror(errno));
                        fflush(stderr);
                        return -1;
                    }
                    // EINTR or EAGAIN, retry
                    continue;
                }
                written += n;
            }
            total_bytes += bytes;
        }
        
        if (fin) {
            fprintf(stderr, "[Client] Stream FIN received.\n");
            fflush(stderr);
            break;
        }
    }
    
    if (total_bytes > 0) {
         static int rx_count = 0;
         if (++rx_count % 100 == 0) {
             fprintf(stderr, "[Client] Flowing... (last chunk %zd bytes)\n", total_bytes);
             fflush(stderr);
         }
    }
    
    fprintf(stderr, "[Client] stream_read_notify EXIT (read %zd bytes)\n", total_bytes);
    fflush(stderr);
    return 0;
}

int client_stream_write_notify(xqc_stream_t *stream, void *user_data) {
    return 0; // Client doesn't write much
}

int client_stream_close_notify(xqc_stream_t *stream, void *user_data) {
    fprintf(stderr, "[Client] Stream Closed.\n");
    return 0;
}

// --- Connection Callbacks ---
int client_conn_create_notify(xqc_connection_t *conn, const xqc_cid_t *cid, void *user_data, void *conn_proto_data) {
    fprintf(stderr, "[Client] conn_create_notify CALLED\n"); fflush(stderr);
    
    // Set connection user_data for stream inheritance
    xqc_conn_set_transport_user_data(conn, user_data);
    
    return 0;
}

int client_stream_create_notify(xqc_stream_t *stream, void *user_data) {
    fprintf(stderr, "[Client] stream_create_notify called, user_data=%p\n", user_data);
    fflush(stderr);
    
    // IMPORTANT: For client-initiated streams (created via xqc_stream_create),
    // the user_data parameter already contains the ctx pointer we passed.
    // We should NOT overwrite it! Only server-side (passive) stream creation
    // would need to retrieve user_data from connection.
    
    // The stream's user_data is already set correctly by xqc_stream_create,
    // so we don't need to do anything here for client-initiated streams.
    
    fprintf(stderr, "[Client] Stream user_data already set correctly\n");
    fflush(stderr);
    return 0;
}

int client_conn_close_notify(xqc_connection_t *conn, const xqc_cid_t *cid, void *user_data, void *conn_proto_data) {
    fprintf(stderr, "[Client] conn_close_notify CALLED\n"); fflush(stderr);
    client_ctx_t *ctx = (client_ctx_t *)user_data;
    event_base_loopbreak(ctx->eb);
    return 0;
}

void client_handshake_finished(xqc_connection_t *conn, void *user_data, void *conn_proto_data) {
    fprintf(stderr, "[Client] Handshake Finished! Creating Stream...\n"); fflush(stderr);
    
    // Fix: xqc_stream_create takes (engine, cid, settings, user_data)
    client_ctx_t *ctx = (client_ctx_t *)user_data; 
    
    fprintf(stderr, "[Client] DEBUG: ctx=%p, engine=%p, cid_len=%d\n", ctx, ctx->engine, ctx->cid.cid_len);
    
    xqc_stream_settings_t stream_settings = {0};
    
    xqc_stream_t *stream = xqc_stream_create(ctx->engine, &ctx->cid, &stream_settings, ctx);
    
    if (!stream) {
        fprintf(stderr, "[Client] Failed to create stream!\n");
        return;
    }
    
    fprintf(stderr, "[Client] Stream Created: %p. Sending START...\n", stream);

    // Send "START" to kick off the server
    char *start_msg = "START";
    xqc_stream_send(stream, (unsigned char*)start_msg, strlen(start_msg), 0);
    fprintf(stderr, "[Client] START sent.\n");
}

int main(int argc, char *argv[]) {
    
    fprintf(stderr, "Hello World from Client!\n");
    client_ctx_t ctx = {0};
    
    // 1. Libevent Init
    ctx.eb = event_base_new();
    fprintf(stderr, "Libevent initialized: %p\n", ctx.eb); fflush(stderr);
    
    // Fix: Initialize events EARLY because xqc_engine_create/connect calls set_event_timer!
    ctx.ev_engine = event_new(ctx.eb, -1, 0, client_engine_callback, &ctx);

    // 2. Socket Init
    ctx.socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (ctx.socket_fd < 0) {
        perror("socket");
        return 1;
    }
    fprintf(stderr, "Socket created: %d\n", ctx.socket_fd); fflush(stderr);
    
    // CRITICAL: Set socket to non-blocking mode (like official demo)
    if (fcntl(ctx.socket_fd, F_SETFL, O_NONBLOCK) == -1) {
        perror("fcntl O_NONBLOCK");
        return 1;
    }
    fprintf(stderr, "Socket set to non-blocking mode\n"); fflush(stderr);
    
    // CRITICAL: Setsockopt for Recv Buffer (Ref: demo_client.c)
    int size = RECV_BUF_SIZE;
    if (setsockopt(ctx.socket_fd, SOL_SOCKET, SO_RCVBUF, &size, sizeof(int)) < 0) {
        perror("setsockopt SO_RCVBUF");
    }
    fprintf(stderr, "Setsockopt done\n"); fflush(stderr);
    
    // Bind to any port
    // ...
    
    // 3. Engine Init
    xqc_engine_callback_t callback = {
        .set_event_timer = client_set_event_timer,
        .log_callbacks = { .xqc_log_write_err = client_write_log }
    };
    
    // Transport callbacks - these are REQUIRED for client!
    xqc_transport_callbacks_t tcbs = {
        .write_socket = client_write_socket,
        .save_token = client_save_token,          // NEW_TOKEN frame handler
        .save_session_cb = client_save_session_cb,  // Session ticket handler
        .save_tp_cb = client_save_tp_cb,            // Transport params handler
        .cert_verify_cb = client_cert_verify       // Certificate verification
    };
    
    xqc_config_t config;
    xqc_engine_get_default_config(&config, XQC_ENGINE_CLIENT);
    // config.cfg_log_level = XQC_LOG_DEBUG; // Disable debug logging
    
    xqc_engine_ssl_config_t ssl_config = {0}; 
    ssl_config.ciphers = "TLS_AES_128_GCM_SHA256:TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256";
    ssl_config.groups = "P-256:X25519:P-384:P-521"; 
    
    fprintf(stderr, "Creating Engine...\n"); fflush(stderr);
    ctx.engine = xqc_engine_create(XQC_ENGINE_CLIENT, &config, &ssl_config, &callback, &tcbs, &ctx);
    fprintf(stderr, "Engine created: %p\n", ctx.engine); fflush(stderr);
    
    if (ctx.engine == NULL) {
        fprintf(stderr, "Engine create failed even with config\n");
        return 1;
    }
    
    // 4. Register ALPN with CRITICAL connection and stream callbacks
    xqc_app_proto_callbacks_t ap_cbs = {
        .conn_cbs = {
            .conn_create_notify = client_conn_create_notify,
           .conn_close_notify = client_conn_close_notify,
            .conn_handshake_finished = client_handshake_finished  // CRITICAL!
        },
        .stream_cbs = {
            .stream_create_notify = client_stream_create_notify,
            .stream_read_notify = client_stream_read_notify,
            .stream_write_notify = client_stream_write_notify,
            .stream_close_notify = client_stream_close_notify,
        }
    };
    xqc_engine_register_alpn(ctx.engine, ALPN_NAME, strlen(ALPN_NAME), &ap_cbs, &ctx);
    
    // 5. Connect
    xqc_conn_settings_t conn_settings = {0};
    conn_settings.idle_time_out = 60000;
    // Note: We don't need to manually set recv_window struct fields if we rely on defaults 
    // and OS buffer, but checking demo line 1521, they mostly use defaults or Multipath stuff.
    // We already set SO_RCVBUF via setsockopt which is the most important part for the kernel to hold data.
    // However, we should set QUIC flow control limits too if defaults are low.
    conn_settings.init_recv_window = 16 * 1024 * 1024; // Advertise 16MB window to peer
    
    xqc_conn_ssl_config_t conn_ssl_config = {0};
    conn_ssl_config.cert_verify_flag = XQC_TLS_CERT_FLAG_NEED_VERIFY | XQC_TLS_CERT_FLAG_ALLOW_SELF_SIGNED;
    
    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);
    
    // 5. Connect
    // ... setup settings ...
    
    const xqc_cid_t *cid = xqc_connect(ctx.engine, &conn_settings, NULL, ctx.socket_fd, 
                                     "localhost", 0, &conn_ssl_config, 
                                     (struct sockaddr *)&server_addr, sizeof(server_addr), 
                                     ALPN_NAME, &ctx);
                                     
    if (!cid) {
        fprintf(stderr, "[Client] Connect failed\n");
        return 1;
    }
    fprintf(stderr, "[Client] Connect success. saving CID...\n");
    memcpy(&ctx.cid, cid, sizeof(xqc_cid_t)); // Save CID for stream creation!
    fprintf(stderr, "[Client] CID saved. Starting Loop...\n");
    
    // 6. Event Loop Setup
    fprintf(stderr, "[Client] ===== EVENT REGISTRATION =====\n");
    
    // Socket event
    ctx.ev_socket = event_new(ctx.eb, ctx.socket_fd, EV_READ | EV_PERSIST, client_socket_read_callback, &ctx);
    if (!ctx.ev_socket) {
        fprintf(stderr, "[Client] ERROR: Failed to create socket event!\n");
        return 1;
    }
    fprintf(stderr, "[Client] Socket event created: %p (fd=%d)\n", ctx.ev_socket, ctx.socket_fd);
    
    int ret = event_add(ctx.ev_socket, NULL);
    if (ret != 0) {
        fprintf(stderr, "[Client] ERROR: event_add failed with ret=%d\n", ret);
        return 1;
    }
    fprintf(stderr, "[Client] ✅ Socket event added successfully\n");
    
    // Heartbeat timer (every 1 second) to verify event loop is alive
    struct event *ev_heartbeat = event_new(ctx.eb, -1, EV_PERSIST, client_heartbeat_callback, &ctx);
    struct timeval tv = {1, 0};  // 1 second interval
    evtimer_add(ev_heartbeat, &tv);
    fprintf(stderr, "[Client] ✅ Heartbeat timer added (1s interval)\n");
    
    // ctx.ev_engine ALREADY CREATED
    fprintf(stderr, "[Client] ===================================\n\n");
    
    xqc_engine_main_logic(ctx.engine); // Kick off
    
    fprintf(stderr, "[Client] Starting Event Loop dispatch...\n");
    event_base_dispatch(ctx.eb);
    
    return 0;
}
