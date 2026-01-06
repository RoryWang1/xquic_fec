
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Safe logging that won't crash if stderr is corrupted
#define SAFE_LOG(msg) do { \
    const char *_msg = msg "\n"; \
    (void)write(STDERR_FILENO, _msg, strlen(_msg)); \
} while(0)

#define SAFE_LOG_FMT(msg, val) do { \
    char _buf[256]; \
    int _len = snprintf(_buf, sizeof(_buf), msg "\n", (unsigned long)(val)); \
    if (_len > 0 && _len < (int)sizeof(_buf)) { \
        (void)write(STDERR_FILENO, _buf, _len); \
    } \
} while(0)
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>  // for mkfifo
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <event2/event.h>

#include <xquic/xquic.h>
#include <xquic/xquic.h>
#include <xquic/xquic_typedef.h>
#include <openssl/ssl.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#define SERVER_PORT 4433
#define ALPN_NAME "camera-stream"
#define FIFO_PATH "/tmp/camera_fifo"

// Huge send buffer
#define SND_BUF_SIZE (4 * 1024 * 1024)
#define VIDEO_CHUNK_SIZE (32 * 1024)

typedef struct {
    xqc_engine_t *engine;
    struct event_base *eb;
    struct event *ev_engine;
    struct event *ev_socket;
    struct event *ev_stdin;
    struct event *ev_fifo_timer; // Fallback poller
    int socket_fd;
    int fifo_fd;  // FIFO for video data input
    
    // Active Stream State
    xqc_stream_t *active_stream;
    int blocked_on_quic; 
    
    unsigned char chunk_buf[VIDEO_CHUNK_SIZE];
    
    // Manual Cert Loading
    X509 *server_cert;
    EVP_PKEY *server_key;
} server_ctx_t;

// --- Helper: Certificate Management ---
void ensure_certificates_exist() {
    const char *cert_path = "server.crt";
    const char *key_path = "server.key";

    if (access(cert_path, F_OK) != -1 && access(key_path, F_OK) != -1) {
        // Files exist
        fprintf(stderr, "[Server] Certificates found: %s, %s\n", cert_path, key_path);
        return;
    }

    fprintf(stderr, "[Server] Certificates missing. Generating self-signed EC certificate...\n");
    
    // Use OpenSSL command to generate EC key and cert
    // 1. Generate EC parameters (P-256)
    // 2. Generate Key
    // 3. Generate Self-Signed Cert
    // We combine this into one command for simplicity/robustness using -newkey ec
    
    // Command: openssl req -x509 -nodes -days 365 -newkey ec:<(openssl ecparam -name prime256v1) -keyout server.key -out server.crt -subj "/CN=localhost"
    // Simplified: openssl req -new -newkey diffie-hellman:parameters -x509 -nodes -out server.crt -keyout server.key
    // Actually, asking xquic usually implies EC. Let's stick to a standard EC generation.
    
    const char *cmd = "openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 "
                      "-nodes -keyout server.key -out server.crt -days 365 "
                      "-subj \"/CN=localhost\" 2>/dev/null";

    int ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr, "[Server] ERROR: Failed to generate certificates via OpenSSL command.\n");
        fprintf(stderr, "[Server] Command tried: %s\n", cmd);
        exit(1);
    }
    
    fprintf(stderr, "[Server] Certificates generated successfully.\n");
}

// --- Helper: Get Current Time ---
uint64_t now() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000 + tv.tv_usec;
}

// --- XQUIC Engine Callbacks ---
void server_set_event_timer(xqc_msec_t wake_after, void *user_data) {
    server_ctx_t *ctx = (server_ctx_t *)user_data;
    // fprintf(stderr, "[Server] Set timer: %llu ms\n", (unsigned long long)wake_after); 
// server_set_event_timer
    struct timeval tv;
    tv.tv_sec = wake_after / 1000000;
    tv.tv_usec = wake_after % 1000000;
    event_add(ctx->ev_engine, &tv);
}

void server_write_log(xqc_log_level_t lvl, const void *buf, size_t count, void *user_data) {
    // Output XQUIC debug logs
    fprintf(stderr, "[XQC-Server] %.*s\n", (int)count, (const char*)buf);
}

// --- Socket I/O ---
ssize_t server_write_socket(const unsigned char *buf, size_t size, 
                          const struct sockaddr *peer_addr, socklen_t peer_addrlen, void *user_data) {
    server_ctx_t *ctx = (server_ctx_t *)user_data;
    ssize_t ret = sendto(ctx->socket_fd, buf, size, 0, peer_addr, peer_addrlen);
    if (ret < 0) {
        if (errno != EAGAIN) {
             perror("[Server] sendto error");
        }
    }
    if (ret > 0) {
        // fprintf(stderr, "[Server] Sent %zd bytes to %s\n", ret, inet_ntoa(((struct sockaddr_in*)peer_addr)->sin_addr));
        // Keep it brief to avoid spam, or uncomment for debug
       fprintf(stderr, "[Server] Sent %zd bytes\n", ret);
    }
    return ret;
}

xqc_int_t server_cert_cb(const char *sni, void **chain, void **crt, void **key, void *user_data) {
    server_ctx_t *ctx = (server_ctx_t *)user_data;
    if (ctx->server_cert && ctx->server_key) {
        *chain = NULL; 
        *crt = ctx->server_cert;
        *key = ctx->server_key;
        return XQC_OK;
    }
    fprintf(stderr, "[Server] Cert callback failed: cert or key missing\n");
    return -XQC_EAGAIN;
}

// --- Main ---
void server_socket_read_callback(int fd, short what, void *arg) {
    fprintf(stderr, "[Server] Socket callback ENTER (fd=%d, what=%d)\n", fd, what);
    fflush(stderr);
    
    server_ctx_t *ctx = (server_ctx_t *)arg;
    unsigned char buf[65535];
    struct sockaddr_in peer_addr;
    socklen_t peer_addrlen;
    ssize_t n;
    
    // Read all available packets in a loop
    do {
        peer_addrlen = sizeof(peer_addr);
        n = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&peer_addr, &peer_addrlen);
        
        if (n < 0) {
            if (errno == EAGAIN) break;
            perror("recvfrom");
            break;
        }
        
        if (n > 0) {
            // fprintf(stderr, "[Server] Recv %zd bytes from %s:%d\n", n, inet_ntoa(peer_addr.sin_addr), ntohs(peer_addr.sin_port));
            
            struct sockaddr_in local_addr;
            socklen_t local_addrlen = sizeof(local_addr);
            local_addr.sin_family = AF_INET;
            local_addr.sin_port = htons(SERVER_PORT);
            inet_pton(AF_INET, "127.0.0.1", &local_addr.sin_addr);

            uint64_t recv_time = now();
            xqc_engine_packet_process(ctx->engine, buf, n, 
                                      (struct sockaddr *)&local_addr, local_addrlen, 
                                      (struct sockaddr *)&peer_addr, peer_addrlen, 
                                      recv_time, ctx);
        }
    } while (n > 0);
    
    xqc_engine_finish_recv(ctx->engine); // Moved back OUTSIDE loop
}

void server_engine_callback(evutil_socket_t fd, short what, void *arg) {
    server_ctx_t *ctx = (server_ctx_t *)arg;
    xqc_engine_main_logic(ctx->engine);
}

// --- Stdin Logic (The "Smart Reader") ---
void server_stdin_read_callback(evutil_socket_t fd, short what, void *arg) {
    server_ctx_t *ctx = (server_ctx_t *)arg;
    
    // Log intent (debug)
    if (fd == -1) {
        // Called from Timer
        // SAFE_LOG("[Server] stdin_callback (Timer poll)");
    } else {
        SAFE_LOG("[Server] stdin_callback (FD Event)");
    }
    
    // Debug: log current state
    // char buf[256];
    // int len = snprintf(buf, sizeof(buf), "[Server] active_stream=%p, blocked=%d, fd=%d\n",
    //                   (void*)ctx->active_stream, ctx->blocked_on_quic, ctx->fifo_fd);
    // if (len > 0) write(STDERR_FILENO, buf, len);
    
    // Only read if we have an active stream and NOT blocked
    if (!ctx->active_stream) {
        // SAFE_LOG("[Server] stdin_callback: NO ACTIVE STREAM - waiting");
        return;
    }
    
    if (ctx->blocked_on_quic) {
        // SAFE_LOG("[Server] stdin_callback: BLOCKED ON QUIC - waiting");
        return; 
    }
    
    // SAFE_LOG("[Server] stdin_callback: attempting read...");
    
    ssize_t n = read(ctx->fifo_fd, ctx->chunk_buf, VIDEO_CHUNK_SIZE);
    
    char buf[256]; // Declare buf here as it's used below
    
    // Only log actual reads or errors, suppress EAGAIN spam
    if (n > 0) {
        int len = snprintf(buf, sizeof(buf), "[Server] Read from FIFO: %zd bytes\n", n);
        if (len > 0) write(STDERR_FILENO, buf, len);
    } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        int len = snprintf(buf, sizeof(buf), "[Server] Read from FIFO: error %d\n", errno);
        if (len > 0) write(STDERR_FILENO, buf, len);
    }
    
    // int len = snprintf(buf, sizeof(buf), "[Server] Read from FIFO: %zd bytes (errno=%d)\n", n, errno);
    // if (len > 0) write(STDERR_FILENO, buf, len);
    
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // Not an error, just no data yet
            return;
        }
        SAFE_LOG("[Server] FIFO read error");
        event_del(ctx->ev_stdin);
        event_del(ctx->ev_fifo_timer);
        return;
    }
    
    if (n == 0) {
        SAFE_LOG("[Server] FIFO EOF (Should not happen with O_RDWR)");
        // Don't delete event even on EOF if we expect reconnection, 
        // but with O_RDWR we shouldn't get EOF unless we close it.
        // For safety, we can yield.
        return; 
    }
    
    int ret = xqc_stream_send(ctx->active_stream, ctx->chunk_buf, n, 0);
    fprintf(stderr, "[Server] Stdin Read: %zd bytes -> Stream Send Ret: %d\n", n, ret); // DEBUG LOG
    
    if (ret == -XQC_EAGAIN) {
        // CRITICAL: Blocked! Pause stdin reading
        fprintf(stderr, "[Server] QUIC Blocked (EAGAIN). Pausing stdin.\n");
        event_del(ctx->ev_stdin); // Stop reading camera
        ctx->blocked_on_quic = 1;
    } else if (ret < 0) {
        fprintf(stderr, "[Server] Stream send error: %d\n", ret);
    } else {
        // Success
        // fprintf(stderr, ".");
        xqc_engine_main_logic(ctx->engine);
    }
}

// --- Stream Callbacks ---
int server_stream_write_notify(xqc_stream_t *stream, void *user_data) {
    server_ctx_t *ctx = (server_ctx_t *)user_data;
    
    // If we were blocked, we can resume!
    if (ctx->blocked_on_quic && stream == ctx->active_stream) {
        fprintf(stderr, "[Server] QUIC Unblocked. Resuming stdin.\n");
        ctx->blocked_on_quic = 0;
        event_add(ctx->ev_stdin, NULL); // Resume reading camera
    }
    return 0;
}

int server_stream_read_notify(xqc_stream_t *stream, void *user_data) {
    // Add diagnostic info about callback context
    char diagbuf[128];
    int len = snprintf(diagbuf, sizeof(diagbuf), "[Server] Callback entered, user_data=%p, stream=%p\n", 
                       user_data, (void*)stream);
    if (len > 0) write(STDERR_FILENO, diagbuf, len);
    
    server_ctx_t *ctx = (server_ctx_t *)user_data;
    
    // Validate ctx pointer before use
    if (!ctx) {
        SAFE_LOG("[Server] ERROR: ctx is NULL!");
        return -1;
    }
    
    // Check if ctx looks valid by testing a known field
    if (ctx->eb == NULL) {
        SAFE_LOG("[Server] ERROR: ctx->eb is NULL - ctx may be corrupted!");
        return -1;
    }
    
    SAFE_LOG("[Server] ctx validation passed");
    
    unsigned char buf[256];
    unsigned char fin = 0;
    
    ssize_t ret = xqc_stream_recv(stream, buf, sizeof(buf), &fin);
    
    if (ret > 0) {
        size_t bytes = ret;
        
        // Safe logging using write() instead of fprintf
        write(STDERR_FILENO, "[Server] Received: ", 19);
        write(STDERR_FILENO, buf, bytes);
        write(STDERR_FILENO, "\n", 1);
        
        if (strncmp((char*)buf, "START", 5) == 0) {
            SAFE_LOG("[Server] START command - activating");
            
            // Test writing to ctx before actual assignment
            SAFE_LOG("[Server] About to write to ctx->active_stream");
            
            ctx->active_stream = stream;
            
            SAFE_LOG("[Server] Stream activated");
            
            // FIFO event was already created, just add it now
            if (ctx->ev_stdin) {
                SAFE_LOG("[Server] Activating FIFO reading");
                
                // Debug: check event state before adding
                char buf[128];
                int len = snprintf(buf, sizeof(buf), "[Server] ev_stdin=%p, fifo_fd=%d\n", 
                                  (void*)ctx->ev_stdin, ctx->fifo_fd);
                if (len > 0) write(STDERR_FILENO, buf, len);
                
                errno = 0;
                int add_ret = event_add(ctx->ev_stdin, NULL);
                
                len = snprintf(buf, sizeof(buf), "[Server] event_add returned %d, errno=%d\n", 
                              add_ret, errno);
                if (len > 0) write(STDERR_FILENO, buf, len);
                
                if (add_ret == 0) {
                    SAFE_LOG("[Server] FIFO reading ENABLED - SUCCESS!");
                    // Kickstart: Force trigger to check for existing data or initial state
                    event_active(ctx->ev_stdin, EV_READ, 0);
                } else {
                    SAFE_LOG("[Server] event_add FAILED!");
                }
                
                // Activate Timer Poller (5ms interval)
                struct timeval tv = {0, 5000}; // 5ms
                event_add(ctx->ev_fifo_timer, &tv);
                SAFE_LOG("[Server] FIFO Timer Poller ENABLED (5ms)");
                
            } else {
                SAFE_LOG("[Server] ERROR: FIFO event not created!");
            }
        }
    }
    
    SAFE_LOG("[Server] Callback returning");
    return 0;
}

int server_stream_close_notify(xqc_stream_t *stream, void *user_data) {
    server_ctx_t *ctx = (server_ctx_t *)user_data;
    if (stream == ctx->active_stream) {
        ctx->active_stream = NULL;
        fprintf(stderr, "[Server] Active stream closed. Stopping stdin.\n");
         if (ctx->ev_stdin) {
             event_del(ctx->ev_stdin);
             event_del(ctx->ev_fifo_timer); // Don't forget the poller!
         }
    }
    return 0;
}

// --- Connection Callbacks ---
int server_conn_create_notify(xqc_connection_t *conn, const xqc_cid_t *cid, void *user_data, void *conn_proto_data) {
    char buf[128];
    int len = snprintf(buf, sizeof(buf), "[Server] conn_create_notify: user_data=%p, conn=%p\n", user_data, (void*)conn);
    if (len > 0) write(STDERR_FILENO, buf, len);
    
    // CRITICAL: Set the connection's user_data so stream callbacks can access it
    // The user_data parameter here is the engine's user_data (server_ctx_t*)
    // We need to set it as the connection's user_data so streams inherit it
    xqc_conn_set_transport_user_data(conn, user_data);
    
    SAFE_LOG("[Server] Connection user_data set");
    return 0;
}

int server_stream_create_notify(xqc_stream_t *stream, void *user_data) {
    SAFE_LOG("[Server] stream_create_notify called");
    
    // The user_data parameter is NULL for server-side streams
    // We need to get it from the connection instead using XQUIC API
    void *conn_user_data = xqc_get_conn_user_data_by_stream(stream);
    
    char buf[128];
    int len = snprintf(buf, sizeof(buf), "[Server] Retrieved conn_user_data=%p for stream=%p\n", 
                       conn_user_data, (void*)stream);
    if (len > 0) write(STDERR_FILENO, buf, len);
    
    // Set the stream's user_data to match connection's user_data
    xqc_stream_set_user_data(stream, conn_user_data);
    
    SAFE_LOG("[Server] Stream user_data set successfully");
    return 0;
}

// --- Main ---
int main(int argc, char *argv[]) {
    server_ctx_t ctx = {0};
    
    // 0. Certs
    ensure_certificates_exist();

    // 1. Libevent
    ctx.eb = event_base_new();
    
    // Fix: Initialize event early
    ctx.ev_engine = event_new(ctx.eb, -1, 0, server_engine_callback, &ctx);
    
    // Create and open FIFO for ffmpeg data
    unlink(FIFO_PATH); // Remove old FIFO if exists
    if (mkfifo(FIFO_PATH, 0666) == -1 && errno != EEXIST) {
        perror("mkfifo");
        return 1;
    }
    
    fprintf(stderr, "[Server] Opening FIFO %s...\n", FIFO_PATH);
    // Use O_RDWR to ensure we have a writer handle, preventing EOF when ffmpeg disconnects
    ctx.fifo_fd = open(FIFO_PATH, O_RDWR | O_NONBLOCK);
    if (ctx.fifo_fd < 0) {
        perror("open FIFO");
        return 1;
    }
    evutil_make_socket_nonblocking(ctx.fifo_fd);
    fprintf(stderr, "[Server] FIFO opened successfully. FD: %d\n", ctx.fifo_fd);
    
    // Create FIFO read event immediately (don't wait for START command)
    ctx.ev_stdin = event_new(ctx.eb, ctx.fifo_fd, EV_READ | EV_PERSIST, 
                             server_stdin_read_callback, &ctx);
    
    // Create FIFO Timer Poller (Fallback) - Call same callback but with fd=-1
    ctx.ev_fifo_timer = event_new(ctx.eb, -1, EV_PERSIST, server_stdin_read_callback, &ctx);

    if (!ctx.ev_stdin) {
        fprintf(stderr, "[Server] Failed to create FIFO event!\n");
        return 1;
    }
    
    // DON'T add the event yet - wait for START command to activate
    fprintf(stderr, "[Server] FIFO event created, waiting for START command...\n");
    
    // 2. Socket
    ctx.socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (ctx.socket_fd < 0) {
        perror("socket"); return 1;
    }
    
    // CRITICAL: Set socket to non-blocking mode (like official demo)
    if (fcntl(ctx.socket_fd, F_SETFL, O_NONBLOCK) == -1) {
        perror("fcntl O_NONBLOCK");
        return 1;
    }
    
    // CRITICAL: Huge Send Buffer
    int snd_buf = SND_BUF_SIZE;
    if (setsockopt(ctx.socket_fd, SOL_SOCKET, SO_SNDBUF, &snd_buf, sizeof(int)) < 0) {
        perror("setsockopt SO_SNDBUF");
    }
    
    int reuse = 1;
    if (setsockopt(ctx.socket_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int)) < 0) {
        perror("setsockopt SO_REUSEADDR");
    }
    if (setsockopt(ctx.socket_fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(int)) < 0) {
        perror("setsockopt SO_REUSEPORT");
    }
    
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // Bind to 127.0.0.1 explicitly
    servaddr.sin_port = htons(SERVER_PORT);
    // inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    
    if (bind(ctx.socket_fd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("bind"); return 1;
    }
    
    // 3. Engine
    xqc_engine_callback_t callback = {
        .set_event_timer = server_set_event_timer,
        .log_callbacks = { .xqc_log_write_err = server_write_log }
    };
    xqc_transport_callbacks_t tcbs = {
        .write_socket = server_write_socket,
        .conn_send_packet_before_accept = server_write_socket, // Reuse same write function
        .conn_cert_cb = server_cert_cb // Use our manual loader
    };
    
    xqc_config_t config;
    xqc_engine_get_default_config(&config, XQC_ENGINE_SERVER);
    config.cfg_log_level = XQC_LOG_DEBUG; // Enable warning/debug logging
    
    // 4. SSL Config (Paths still needed for engine init, but we override via callback)
    xqc_engine_ssl_config_t ssl_config = {
        .private_key_file = "server.key",
        .cert_file        = "server.crt",
        .ciphers          = NULL, // Use defaults
        .groups           = NULL, // Use defaults
        .session_ticket_key_data = NULL,
        .session_ticket_key_len = 0
    };
    
    // Manual Load for Callback
    SSL_library_init();
    OpenSSL_add_all_algorithms();
    
    BIO *cert_bio = BIO_new_file("server.crt", "r");
    if (!cert_bio) { perror("BIO_new_file cert"); return 1; }
    ctx.server_cert = PEM_read_bio_X509(cert_bio, NULL, NULL, NULL);
    BIO_free(cert_bio);
    if (!ctx.server_cert) { fprintf(stderr, "Failed to load cert\n"); return 1; }
    
    BIO *key_bio = BIO_new_file("server.key", "r");
    if (!key_bio) { perror("BIO_new_file key"); return 1; }
    ctx.server_key = PEM_read_bio_PrivateKey(key_bio, NULL, NULL, NULL);
    BIO_free(key_bio);
    if (!ctx.server_key) { fprintf(stderr, "Failed to load key\n"); return 1; }

    fprintf(stderr, "[Server] Manually loaded cert and key successfully.\n");
    
    ctx.engine = xqc_engine_create(XQC_ENGINE_SERVER, &config, &ssl_config, &callback, &tcbs, &ctx);
    
    if (!ctx.engine) {
        fprintf(stderr, "Engine create failed!\n");
        return 1;
    }
    
    // 4. ALPN with conn callbacks
    xqc_app_proto_callbacks_t ap_cbs = {
        .conn_cbs = {
            .conn_create_notify = server_conn_create_notify,
        },
        .stream_cbs = {
            .stream_create_notify = server_stream_create_notify,
            .stream_read_notify = server_stream_read_notify,
            .stream_write_notify = server_stream_write_notify,
            .stream_close_notify = server_stream_close_notify,
        }
    };
    xqc_engine_register_alpn(ctx.engine, ALPN_NAME, strlen(ALPN_NAME), &ap_cbs, &ctx);
    
    // 5. Conn Settings
    xqc_conn_settings_t conn_settings = {0};
    conn_settings.idle_time_out = 60000;
    conn_settings.init_recv_window = 16 * 1024 * 1024; // If client sends data?
    xqc_server_set_conn_settings(ctx.engine, &conn_settings);
    
    // 6. Loop
    ctx.ev_socket = event_new(ctx.eb, ctx.socket_fd, EV_READ | EV_PERSIST, server_socket_read_callback, &ctx);
    if (event_add(ctx.ev_socket, NULL) < 0) {
        fprintf(stderr, "[Server] Error adding socket event!\n");
        return 1;
    } else {
        fprintf(stderr, "[Server] Socket event added successfully. FD: %d\n", ctx.socket_fd);
    }
    
    // ctx.ev_engine ALREADY CREATED
    
    fprintf(stderr, "[Server] Refactored Server Listening on %d...\n", SERVER_PORT);
    event_base_dispatch(ctx.eb);
    
    return 0;
}
