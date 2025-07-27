#include "run.h"
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <pthread.h>
#include <time.h>
#include <strings.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

extern HotReloadState hot_reload_state;
static int http_redirect_port = 8080;

void use_static_files();
void start_redirector(void);

int lw_run(int port) {
    if (LW_SSL_ENABLED == 1) start_redirector();

    struct sockaddr_in address;
    int addr_len = sizeof(address);
    char buffer[BUFFER_SIZE] = {0};

    lw_ctx.port = port;

    // Initialize SSL if enabled
    if (LW_SSL_ENABLED == 1) {
        if (LW_CERT != 1 || LW_KEY != 1) {
            fprintf(stderr, "[ERR] SSL enabled but certificate or key not provided\n");
            return -1;
        }

        init_openssl();
        ssl_ctx = create_ssl_ctx();
        configure_ssl_ctx(ssl_ctx, LW_CERT_FILE, LW_KEY_FILE);

        printf("[LW] SSL/TLS enabled with certificate: %s\n", LW_CERT_FILE);
        printf("[LW] Server is listening on https://localhost:%d\n", port);
    } else {
        printf("[LW] Server is listening on http://localhost:%d\n", port);
    }

    // Create socket
    if ((lw_ctx.server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("[ERR] Socket creation failed");
        return -1;
    }

    // Set socket options=
    int opt = 1;
    if (setsockopt(lw_ctx.server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("[ERR] Setsockopt failed");
        close(lw_ctx.server_fd);
        return -1;
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    // Bind socket
    if (bind(lw_ctx.server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("[ERR] Bind failed");
        close(lw_ctx.server_fd);
        return -1;
    }

    // Listen for connections
    if (listen(lw_ctx.server_fd, 10) < 0) {
        perror("[ERR] Listen failed");
        close(lw_ctx.server_fd);
        return -1;
    }

    // Get the reload pipe fd for select
    int reload_pipe_fd = get_reload_pipe_fd();

    while (1) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(lw_ctx.server_fd, &read_fds);
        if (reload_pipe_fd != -1) {
            FD_SET(reload_pipe_fd, &read_fds);
        }

        // Set timeout to 1 second to prevent blocking indefinitely
        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int max_fd = lw_ctx.server_fd;
        if (reload_pipe_fd > max_fd) max_fd = reload_pipe_fd;

        int ready = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
        if (ready < 0) {
            if (errno == EINTR) continue;
            perror("[ERR] Select failed");
            break;
        }

        // Check if reload signal was received
        if (reload_pipe_fd != -1 && FD_ISSET(reload_pipe_fd, &read_fds)) {
            char buf;
            while (read(reload_pipe_fd, &buf, 1) > 0); // Clear the pipe
            printf("[DEV] Reload signal received\n");
        }

        // Check for new connections
        if (FD_ISSET(lw_ctx.server_fd, &read_fds)) {
            int client_socket = accept(lw_ctx.server_fd,
                                       (struct sockaddr *)&address,
                                       (socklen_t *)&addr_len);
            if (client_socket < 0) {
                perror("[ERR] Accept failed");
                continue;
            }

            SSL *client_ssl = NULL;

            // Handle SSL connection if enabled
            if (LW_SSL_ENABLED == 1) {
                client_ssl = SSL_new(ssl_ctx);
                if (!client_ssl) {
                    fprintf(stderr, "[ERR] Failed to create SSL structure\n");
                    close(client_socket);
                    continue;
                }

                SSL_set_fd(client_ssl, client_socket);

                // Perform SSL handshake
                if (SSL_accept(client_ssl) <= 0) {
                    fprintf(stderr, "[ERR] SSL handshake failed\n");
                    ERR_print_errors_fp(stderr);
                    SSL_free(client_ssl);
                    close(client_socket);
                    continue;
                }
            }

            // Read request
            memset(buffer, 0, BUFFER_SIZE);
            size_t total = 0;
            int bytes_read;

            while (total < BUFFER_SIZE - 1) {
            if (LW_SSL_ENABLED == 1 && client_ssl) {
                bytes_read = SSL_read(client_ssl, buffer + total,BUFFER_SIZE - total - 1);
            } else {
                bytes_read = read(client_socket, buffer + total,BUFFER_SIZE - total - 1);
            }

            if (bytes_read <= 0) break;          /* EOF or error */
            total += bytes_read;

            if (total >= 4 &&
                memcmp(buffer + total - 4, "\r\n\r\n", 4) == 0)
                break;
            }

            if (total == 0) {
                if (client_ssl) {
                    SSL_shutdown(client_ssl);
                    SSL_free(client_ssl);
                }
                close(client_socket);
                continue;
            }

        buffer[total] = '\0';

            // Get client IP
            char ipstring[INET_ADDRSTRLEN];
            getpeername(client_socket, (struct sockaddr *)&address, (socklen_t *)&addr_len);
            struct sockaddr_in *s = (struct sockaddr_in *)&address;
            inet_ntop(AF_INET, &s->sin_addr, ipstring, sizeof(ipstring));

            if (strcmp(ipstring, "127.0.0.1") == 0) {
                addr_len = sizeof(address);
                getsockname(client_socket, (struct sockaddr *)&address, (socklen_t *)&addr_len);
                inet_ntop(AF_INET, &s->sin_addr, ipstring, sizeof(ipstring));
            }

            LW_VERBOSE
                ? printf("[LW] Incoming request:\nIP: %s\n%s\n", ipstring, buffer)
                : printf("[LW] Incoming request: IP: %s\n", ipstring);

            // Parse request
            http_request_t request = {0};
            parse_request(buffer, &request);

            (LW_VERBOSE) ? printf("[INFO] Found %d headers\n", request.header_count) : -1;
            for (int i = 0; i < request.header_count; ++i) {
                (LW_VERBOSE) ? printf("[INFO] Header[%d]: \"%s\"\n", i, request.headers[i]) : -1;
                const char *hdr = request.headers[i];
                if (hdr && strncasecmp(hdr, "Accept-Encoding:", 16) == 0) {
                    ACCEPT_ENCODING = hdr + 16;
                    while (*ACCEPT_ENCODING == ' ' || *ACCEPT_ENCODING == '\t' || *ACCEPT_ENCODING == ':')
                        ++ACCEPT_ENCODING;
                    break;
                }
            }

            // Find route
            route_t *route = find_route(request.method, request.path);

            http_response_t response = {0};
            init_response(&response);
            response.chunked_fd = (LW_DEV_MODE &&
                       route && route->handler == index_handler)
                          ? client_socket
                          : -1;

            if (route) {
                route->handler(&request, &response);
            } else {
                // 404 Not Found
                response.status_code = 404;
                lw_set_header(&response, "Content-Type: text/plain");
                lw_set_body(&response, "404 Not Found");
            }

            // Add reload header if needed
            time_t now = time(NULL);
            if (now - hot_reload_state.last_change_time <= 2) {
                lw_set_header(&response, "X-Reload: 1");
            }

            // Send response
            if (response.chunked_fd >= 0) {
                // Chunked path keeps socket open for SSE //
            } else {
                lw_send_response(&response, client_socket, client_ssl, ACCEPT_ENCODING);
                if (client_ssl) {
                    SSL_shutdown(client_ssl);
                    SSL_free(client_ssl);
                }
                close(client_socket);   // Only close non-chunked
            }

            // Cleanup request / response memory
            free_request(&request);
            free_response(&response);
        }
    }

    close(lw_ctx.server_fd);

    if (LW_SSL_ENABLED == 1) {
        SSL_CTX_free(ssl_ctx);
        cleanup_openssl();
    }

    return 0;
}

static void *redirect_worker(void *arg) {
    (void)arg;

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("[ERR] Redirect socket failed.\n");
        return NULL;
    }

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(http_redirect_port);

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[ERR] Redirect binding failed.\n");
        close(sock);
        return NULL;
    }

    if (listen(sock, 10) < 0) {
        perror("[ERR] Redirect listen failed.\n");
        close(sock);
        return NULL;
    }

    printf("[REDIRECT] Redirect listener on http://localhost:%d\n", http_redirect_port);

    while (1) {
        int client = accept(sock, NULL, NULL);
        if (client < 0) {
            perror("[ERR] Redirect accept failed.\n");
            continue;
        }

        const char *rsp =
            "HTTP/1.1 301 Moved Permanently\r\n"
            "Location: https://localhost:%d/\r\n"
            "Connection: close\r\n\r\n";
        char buf[256];
        int len = snprintf(buf, sizeof(buf), rsp, lw_ctx.port);
        write(client, buf, len);
        close(client);
    }

    return NULL;
}

void start_redirector(void) {
    if (LW_SSL_ENABLED != 1) return;
    pthread_t tid;
    pthread_create(&tid, NULL, redirect_worker, NULL);
    pthread_detach(tid);
}
