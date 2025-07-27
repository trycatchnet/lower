#define _GNU_SOURCE
#include "run.h"
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

extern HotReloadState hot_reload_state;

static void chunked_write(int fd, const char *data, size_t len)
{
    char header[32];
    int hdr_len = snprintf(header, sizeof(header), "%zx\r\n", len);
    send(fd, header, hdr_len, MSG_NOSIGNAL);
    send(fd, data, len, MSG_NOSIGNAL);
    send(fd, "\r\n", 2, MSG_NOSIGNAL);
}

char* load_html_file(const char* filename) {
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "./public/html/%s", filename);

    FILE *file = fopen(filepath, "r");
    if (file == NULL) {
        printf("[ERR] HTML file not found: %s\n", filepath);
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *content = malloc(file_size + 1);
    fread(content, 1, file_size, file);
    content[file_size] = '\0';

    fclose(file);
    printf("[LW] HTML file loaded: %s\n", filepath);
    return content;
}

void render_html(http_response_t *res, const char *filename) {
    if (!LW_DEV_MODE) {
        char *content = load_html_file(filename);
        if (!content) {
            res->status_code = 404;
            lw_set_header(res, "Content-Type: text/html; charset=utf-8");
            lw_set_body(res, "<h1>404 Not Found</h1>");
            return;
        }
        lw_set_header(res, "Content-Type: text/html; charset=utf-8");
        lw_set_body(res, content);
        free(content);
        return;
    }

    res->status_code = 200;
    lw_set_header(res, "Content-Type: text/html; charset=utf-8");
    lw_set_header(res, "Transfer-Encoding: chunked");
    lw_set_header(res, "Cache-Control: no-cache");
    lw_set_header(res, "Connection: keep-alive");
    
    // Add reload header if needed
    time_t now = time(NULL);
    if (now - hot_reload_state.last_change_time <= 2) {
        lw_set_header(res, "X-Reload: 1");
    }

    char header_buf[1024];
    int off = snprintf(header_buf, sizeof(header_buf),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "Transfer-Encoding: chunked\r\n"
            "Cache-Control: no-cache\r\n"
            "Connection: keep-alive\r\n");
    
    // Add custom headers
    for (int i = 0; i < res->header_count; i++) {
        off += snprintf(header_buf + off, sizeof(header_buf) - off, 
                        "%s\r\n", res->headers[i]);
    }
    off += snprintf(header_buf + off, sizeof(header_buf) - off, "\r\n");
    
    send(res->chunked_fd, header_buf, off, MSG_NOSIGNAL);

    char *content = load_html_file(filename);
    printf("[DEV] loaded %zu bytes\n", content ? strlen(content) : 0);
    if (!content) content = strdup("<h1>404 Not Found</h1>");
    chunked_write(res->chunked_fd, content, strlen(content));
    free(content);
    chunked_write(res->chunked_fd, "", 0);
}

void static_file_handler(http_request_t *req, http_response_t *res)
{
    char filepath[512];
    const char *base_path = "./public";

    if (strstr(req->path, "..") != NULL) {
        res->status_code = 403;
        lw_set_header(res, "Content-Type: text/plain");
        lw_set_body(res, "403 Forbidden");
        return;
    }

    const char *path = req->path;
    if (*path == '/') path++;
    snprintf(filepath, sizeof(filepath), "%s/%s", base_path, path);

    FILE *file = fopen(filepath, "rb");
    if (file == NULL) {
        res->status_code = 404;
        lw_set_header(res, "Content-Type: text/html");
        lw_set_body(res, "<h1>404 Not Found</h1>");
        return;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *content = malloc(file_size);
    if (!content) {
        fclose(file);
        res->status_code = 500;
        lw_set_body(res, "Internal Server Error");
        return;
    }

    size_t bytes_read = fread(content, 1, file_size, file);
    fclose(file);
    if (bytes_read != (size_t)file_size) {
        free(content);
        res->status_code = 500;
        lw_set_body(res, "Internal Server Error");
        return;
    }

    const char *ext = strrchr(filepath, '.');
    if (ext) {
        if      (strcmp(ext, ".css") == 0)       lw_set_header(res, "Content-Type: text/css");
        else if (strcmp(ext, ".js")  == 0)       lw_set_header(res, "Content-Type: application/javascript");
        else if (strcmp(ext, ".png")  == 0)      lw_set_header(res, "Content-Type: image/png");
        else if (strcmp(ext, ".jpg")  == 0 ||
                 strcmp(ext, ".jpeg") == 0)      lw_set_header(res, "Content-Type: image/jpeg");
        else if (strcmp(ext, ".gif")  == 0)      lw_set_header(res, "Content-Type: image/gif");
        else if (strcmp(ext, ".svg")  == 0)      lw_set_header(res, "Content-Type: image/svg+xml");
        else if (strcmp(ext, ".ico")  == 0)      lw_set_header(res, "Content-Type: image/x-icon");
        else if (strcmp(ext, ".woff2") == 0)     lw_set_header(res, "Content-Type: font/woff2");
        else if (strcmp(ext, ".woff")  == 0)     lw_set_header(res, "Content-Type: font/woff");
        else if (strcmp(ext, ".ttf")   == 0)     lw_set_header(res, "Content-Type: font/ttf");
        else if (strcmp(ext, ".otf")   == 0)     lw_set_header(res, "Content-Type: font/otf");
        else if (strcmp(ext, ".eot")   == 0)     lw_set_header(res, "Content-Type: application/vnd.ms-fontobject");
        else if (strcmp(ext, ".json")  == 0)     lw_set_header(res, "Content-Type: application/json");
        else if (strcmp(ext, ".xml")   == 0)     lw_set_header(res, "Content-Type: application/xml");
        else if (strcmp(ext, ".pdf")   == 0)     lw_set_header(res, "Content-Type: application/pdf");
        else if (strcmp(ext, ".zip")   == 0)     lw_set_header(res, "Content-Type: application/zip");
        else if (strcmp(ext, ".txt")   == 0)     lw_set_header(res, "Content-Type: text/plain");
        else                                      lw_set_header(res, "Content-Type: text/html; charset=utf-8");
    } else {
        lw_set_header(res, "Content-Type: text/html; charset=utf-8");
    }

    time_t now = time(NULL);
    if (now - hot_reload_state.last_change_time <= 2) {
        lw_set_header(res, "X-Reload: 1");
    }

    if (LW_DEV_MODE && res->chunked_fd >= 0) {
        chunked_write(res->chunked_fd, content, file_size);
        chunked_write(res->chunked_fd, "", 0);
    } else {
        lw_set_body_bin(res, content, file_size);
    }

    free(content);
}

void use_static_files() {
    lw_route(GET, "/css/", static_file_handler);
    lw_route(GET, "/js/", static_file_handler);
    lw_route(GET, "/img/", static_file_handler);
    lw_route(GET, "/images/", static_file_handler);
    lw_route(GET, "/fonts/", static_file_handler);
    lw_route(GET, "/assets/", static_file_handler);
    lw_route(GET, "/uploads/", static_file_handler);
    lw_route(GET, "/media/", static_file_handler);
    lw_route(GET, "/favicon.ico", static_file_handler);
}
