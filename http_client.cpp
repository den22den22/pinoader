#include "http_client.h"
#include "logger.h"
#include <sstream>
#include <vector>
#include <fstream>
#include <algorithm>
#include <random>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

// A list of User-Agents to mimic different browsers
const std::vector<std::string> USER_AGENTS = {
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/118.0.0.0 Safari/537.36",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:109.0) Gecko/20100101 Firefox/117.0",
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/118.0.0.0 Safari/537.36",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Edge/118.0.2088.46 Safari/537.36"
};

// Function to get a random User-Agent
std::string get_random_user_agent() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, USER_AGENTS.size() - 1);
    return USER_AGENTS[dis(gen)];
}

struct SSLInitializer {
    SSLInitializer() { SSL_library_init(); }
};
SSLInitializer ssl_initializer;

bool parse_url(const std::string& url, std::string& protocol, std::string& host, std::string& path, int& port) {
    size_t protocol_pos = url.find("://");
    if (protocol_pos == std::string::npos) return false;
    protocol = url.substr(0, protocol_pos);
    size_t host_start = protocol_pos + 3;
    size_t path_start = url.find('/', host_start);
    if (path_start == std::string::npos) {
        host = url.substr(host_start);
        path = "/";
    } else {
        host = url.substr(host_start, path_start - host_start);
        path = url.substr(path_start);
    }
    port = (protocol == "https") ? 443 : 80;
    return true;
}

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::tolower(c); });
    return s;
}

HttpResponse perform_request(const std::string& protocol, const std::string& host, const std::string& path, int port) {
    addrinfo hints = {}, *addrs;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    HttpResponse response;

    log_debug("[http] Resolving " + host);
    int status = getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &addrs);
    if (status != 0) {
        log_debug("[http] getaddrinfo failed: " + std::string(gai_strerror(status)));
        return response;
    }

    int socket_fd = -1;
    for (addrinfo* rp = addrs; rp != nullptr; rp = rp->ai_next) {
        socket_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (socket_fd != -1 && connect(socket_fd, rp->ai_addr, rp->ai_addrlen) != -1) break;
        close(socket_fd);
        socket_fd = -1;
    }
    freeaddrinfo(addrs);

    if (socket_fd == -1) {
        log_debug("[http] Failed to connect to " + host);
        return response;
    }
    log_debug("[http] TCP connection established with " + host);

    SSL_CTX* ssl_ctx = nullptr;
    SSL* ssl = nullptr;
    bool is_https = (protocol == "https");

    if (is_https) {
        log_debug("[http] Initializing SSL/TLS connection for " + host);
        ssl_ctx = SSL_CTX_new(TLS_client_method());
        if (!ssl_ctx) {
            log_debug("[http] SSL_CTX_new() failed.");
            close(socket_fd);
            return response;
        }
        if (SSL_CTX_set_default_verify_paths(ssl_ctx) != 1) {
            log_debug("[http] Failed to load default CA certificates.");
            SSL_CTX_free(ssl_ctx);
            close(socket_fd);
            return response;
        }
        ssl = SSL_new(ssl_ctx);
        SSL_set_fd(ssl, socket_fd);
        // FIX: Add SNI (Server Name Indication) support. This is crucial for modern HTTPS.
        SSL_set_tlsext_host_name(ssl, host.c_str());
        if (SSL_connect(ssl) <= 0) {
            log_debug("[http] SSL/TLS handshake failed.");
            ERR_print_errors_fp(stderr);
            SSL_free(ssl);
            SSL_CTX_free(ssl_ctx);
            close(socket_fd);
            return response;
        }
        log_debug("[http] SSL/TLS handshake successful.");
    }

    std::stringstream request_stream;
    request_stream << "GET " << path << " HTTP/1.1\r\n";
    request_stream << "Host: " << host << "\r\n";
    request_stream << "User-Agent: " << get_random_user_agent() << "\r\n";
    request_stream << "Connection: close\r\n\r\n";
    std::string request = request_stream.str();

    log_debug("[http] >> Sending GET request to " + host + path);

    if (is_https) {
        SSL_write(ssl, request.c_str(), request.length());
    } else {
        send(socket_fd, request.c_str(), request.length(), 0);
    }

    std::string full_response;
    char buffer[8192];
    int bytes_received;

    log_debug("[http] << Waiting for response from " + host);

    if (is_https) {
        while ((bytes_received = SSL_read(ssl, buffer, sizeof(buffer))) > 0) {
            full_response.append(buffer, bytes_received);
        }
    } else {
        while ((bytes_received = recv(socket_fd, buffer, sizeof(buffer), 0)) > 0) {
            full_response.append(buffer, bytes_received);
        }
    }

    if (is_https) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
        SSL_CTX_free(ssl_ctx);
    }
    close(socket_fd);

    log_debug("[http] << Received " + std::to_string(full_response.length()) + " bytes in total.");

    size_t headers_end = full_response.find("\r\n\r\n");
    if (headers_end == std::string::npos) return response;

    std::string headers_part = full_response.substr(0, headers_end);
    response.body = full_response.substr(headers_end + 4);

    std::stringstream headers_ss(headers_part);
    std::string line;
    std::getline(headers_ss, line); // status line
    sscanf(line.c_str(), "HTTP/%*f %d", &response.status_code);

    while(std::getline(headers_ss, line) && line != "\r") {
        auto colon_pos = line.find(':');
        if(colon_pos != std::string::npos) {
            auto key = to_lower(line.substr(0, colon_pos));
            auto value = line.substr(colon_pos + 2);
            value.erase(value.find_last_not_of("\r\n") + 1);
            response.headers[key] = value;
        }
    }
    log_debug("[http] Response body size: " + std::to_string(response.body.length()) + " bytes.");
    return response;
}

std::string fetch_url(const std::string& initial_url, std::string& final_url, int max_redirects) {
    std::string current_url = initial_url;

    for (int i = 0; i < max_redirects; ++i) {
        final_url = current_url;
        std::string protocol, host, path;
        int port;

        if (!parse_url(current_url, protocol, host, path, port)) {
            log_error("Invalid URL format: " + current_url);
            return "";
        }

        log_debug("[http] Requesting URL: " + current_url);
        HttpResponse res = perform_request(protocol, host, path, port);
        log_debug("[http] Server responded with status: " + std::to_string(res.status_code));

        if (res.status_code >= 300 && res.status_code < 400 && res.headers.count("location")) {
            current_url = res.headers["location"];
            if (current_url.rfind("http", 0) != 0) {
                // Handle relative redirects
                if (current_url.front() == '/') {
                    current_url = protocol + "://" + host + current_url;
                } else {
                    current_url = protocol + "://" + host + "/" + current_url;
                }
            }
            log_debug("[http] Redirecting to: " + current_url);
        } else if (res.status_code == 200) {
            return res.body;
        } else {
            log_debug("[http] Received non-200/3xx status code. Aborting.");
            return "";
        }
    }
    log_debug("[http] Exceeded max redirects (" + std::to_string(max_redirects) + "). Aborting.");
    return "";
}

bool download_file(const std::string& url, const std::string& output_path) {
    log_normal("[downloader] Destination: " + output_path);
    log_debug("[downloader] Starting download from URL: " + url);

    std::string protocol, host, path;
    int port;

    if (!parse_url(url, protocol, host, path, port) || protocol != "https") {
        log_error("Invalid or non-HTTPS URL for download: " + url);
        return false;
    }

    addrinfo hints = {}, *addrs;
    hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &addrs) != 0) {
        log_error("getaddrinfo failed for " + host);
        return false;
    }

    int socket_fd = -1;
    for (addrinfo* rp = addrs; rp != nullptr; rp = rp->ai_next) {
        socket_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (socket_fd != -1 && connect(socket_fd, rp->ai_addr, rp->ai_addrlen) != -1) break;
        close(socket_fd); socket_fd = -1;
    }
    freeaddrinfo(addrs);

    if (socket_fd == -1) {
        log_error("Could not connect to host: " + host);
        return false;
    }

    SSL_CTX* ssl_ctx = SSL_CTX_new(TLS_client_method());
    SSL_CTX_set_default_verify_paths(ssl_ctx);
    SSL* ssl = SSL_new(ssl_ctx);
    SSL_set_fd(ssl, socket_fd);

    // FIX: Add SNI support, this is key to solving the handshake failure.
    SSL_set_tlsext_host_name(ssl, host.c_str());

    if (SSL_connect(ssl) <= 0) {
        ERR_print_errors_fp(stderr);
        SSL_free(ssl); SSL_CTX_free(ssl_ctx); close(socket_fd);
        return false;
    }

    std::string request = "GET " + path + " HTTP/1.1\r\nHost: " + host + "\r\nConnection: close\r\nUser-Agent: " + get_random_user_agent() + "\r\n\r\n";
    SSL_write(ssl, request.c_str(), request.length());

    char buffer[8192];
    int bytes_received = SSL_read(ssl, buffer, sizeof(buffer));
    if (bytes_received <= 0) {
        SSL_free(ssl); SSL_CTX_free(ssl_ctx); close(socket_fd);
        return false;
    }

    std::string response_str(buffer, bytes_received);
    size_t headers_end = response_str.find("\r\n\r\n");
    if (headers_end == std::string::npos) {
        SSL_free(ssl); SSL_CTX_free(ssl_ctx); close(socket_fd);
        log_error("Could not find end of headers in response.");
        return false;
    }

    std::ofstream output_file(output_path, std::ios::binary);
    if (!output_file) {
        log_error("Could not create file: " + output_path);
        SSL_free(ssl); SSL_CTX_free(ssl_ctx); close(socket_fd);
        return false;
    }
    
    // Write the first part of the file body that was already in the buffer
    size_t body_start_offset = headers_end + 4;
    output_file.write(buffer + body_start_offset, bytes_received - body_start_offset);
    long total_bytes = bytes_received - body_start_offset;

    // Read and write the remaining part of the file
    while ((bytes_received = SSL_read(ssl, buffer, sizeof(buffer))) > 0) {
        output_file.write(buffer, bytes_received);
        total_bytes += bytes_received;
    }

    output_file.close();
    SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(ssl_ctx);
    close(socket_fd);

    log_normal("[downloader] Download finished. Total size: " + std::to_string(total_bytes / 1024) + "KiB");
    return true;
}
