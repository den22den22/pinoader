#include "http_client.h"
#include "logger.h"
#include <sstream>
#include <vector>
#include <fstream>
#include <algorithm>
#include <random>
#include <iostream>

// Windows Headers
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

// OpenSSL Headers
#include <openssl/ssl.h>
#include <openssl/err.h>

// Link only basic Ws2_32 here, others via Makefile
#pragma comment(lib, "Ws2_32.lib")

const std::vector<std::string> USER_AGENTS = {
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/118.0.0.0 Safari/537.36",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:109.0) Gecko/20100101 Firefox/117.0"
};

std::string get_random_user_agent() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, USER_AGENTS.size() - 1);
    return USER_AGENTS[dis(gen)];
}

// Initialization for Winsock ONLY.
// OpenSSL 1.1+ and 3.x initialize automatically.
struct SystemInitializer {
    SystemInitializer() {
        // Init Winsock
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            std::cerr << "WSAStartup failed." << std::endl;
            exit(1);
        }
    }
    ~SystemInitializer() {
        WSACleanup();
    }
};
SystemInitializer sys_initializer;

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
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &addrs) != 0) {
        return response;
    }

    SOCKET socket_fd = INVALID_SOCKET;
    for (addrinfo* rp = addrs; rp != nullptr; rp = rp->ai_next) {
        socket_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (socket_fd == INVALID_SOCKET) continue;
        if (connect(socket_fd, rp->ai_addr, (int)rp->ai_addrlen) != SOCKET_ERROR) break;
        closesocket(socket_fd);
        socket_fd = INVALID_SOCKET;
    }
    freeaddrinfo(addrs);

    if (socket_fd == INVALID_SOCKET) return response;

    SSL_CTX* ssl_ctx = nullptr;
    SSL* ssl = nullptr;
    bool is_https = (protocol == "https");

    if (is_https) {
        // Modern OpenSSL uses TLS_client_method
        ssl_ctx = SSL_CTX_new(TLS_client_method());
        if (!ssl_ctx) {
            closesocket(socket_fd);
            return response;
        }

        ssl = SSL_new(ssl_ctx);
        SSL_set_fd(ssl, (int)socket_fd);
        
        // SNI (Server Name Indication) is mandatory for modern web
        SSL_set_tlsext_host_name(ssl, host.c_str());

        if (SSL_connect(ssl) <= 0) {
            log_debug("[http] SSL handshake failed.");
            SSL_free(ssl);
            SSL_CTX_free(ssl_ctx);
            closesocket(socket_fd);
            return response;
        }
    }

    std::stringstream request_stream;
    request_stream << "GET " << path << " HTTP/1.1\r\n";
    request_stream << "Host: " << host << "\r\n";
    request_stream << "User-Agent: " << get_random_user_agent() << "\r\n";
    request_stream << "Connection: close\r\n\r\n";
    std::string request = request_stream.str();

    if (is_https) SSL_write(ssl, request.c_str(), request.length());
    else send(socket_fd, request.c_str(), (int)request.length(), 0);

    std::string full_response;
    char buffer[8192];
    int bytes_received;

    if (is_https) {
        while ((bytes_received = SSL_read(ssl, buffer, sizeof(buffer))) > 0) {
            full_response.append(buffer, bytes_received);
        }
        SSL_shutdown(ssl);
        SSL_free(ssl);
        SSL_CTX_free(ssl_ctx);
    } else {
        while ((bytes_received = recv(socket_fd, buffer, sizeof(buffer), 0)) > 0) {
            full_response.append(buffer, bytes_received);
        }
    }
    closesocket(socket_fd);

    size_t headers_end = full_response.find("\r\n\r\n");
    if (headers_end == std::string::npos) return response;

    std::string headers_part = full_response.substr(0, headers_end);
    response.body = full_response.substr(headers_end + 4);

    std::stringstream headers_ss(headers_part);
    std::string line;
    std::getline(headers_ss, line);
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
    return response;
}

std::string fetch_url(const std::string& initial_url, std::string& final_url, int max_redirects) {
    std::string current_url = initial_url;
    for (int i = 0; i < max_redirects; ++i) {
        final_url = current_url;
        std::string protocol, host, path;
        int port;
        if (!parse_url(current_url, protocol, host, path, port)) return "";

        log_debug("[http] Requesting: " + current_url);
        HttpResponse res = perform_request(protocol, host, path, port);

        if (res.status_code >= 300 && res.status_code < 400 && res.headers.count("location")) {
            current_url = res.headers["location"];
            if (current_url.find("http") != 0) {
                 if (current_url[0] == '/') current_url = protocol + "://" + host + current_url;
                 else current_url = protocol + "://" + host + "/" + current_url;
            }
        } else if (res.status_code == 200) {
            return res.body;
        } else {
            return "";
        }
    }
    return "";
}

bool download_file(const std::string& url, const std::string& output_path) {
    log_normal("[downloader] Saving to: " + output_path);
    std::string protocol, host, path;
    int port;
    if (!parse_url(url, protocol, host, path, port) || protocol != "https") return false;

    addrinfo hints = {}, *addrs;
    hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &addrs) != 0) return false;

    SOCKET socket_fd = INVALID_SOCKET;
    for (addrinfo* rp = addrs; rp != nullptr; rp = rp->ai_next) {
        socket_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (socket_fd == INVALID_SOCKET) continue;
        if (connect(socket_fd, rp->ai_addr, (int)rp->ai_addrlen) != SOCKET_ERROR) break;
        closesocket(socket_fd); socket_fd = INVALID_SOCKET;
    }
    freeaddrinfo(addrs);
    if (socket_fd == INVALID_SOCKET) return false;

    // TLS_client_method for modern OpenSSL
    SSL_CTX* ssl_ctx = SSL_CTX_new(TLS_client_method());
    SSL* ssl = SSL_new(ssl_ctx);
    SSL_set_fd(ssl, (int)socket_fd);
    
    // SNI
    SSL_set_tlsext_host_name(ssl, host.c_str());

    if (SSL_connect(ssl) <= 0) {
        SSL_free(ssl); SSL_CTX_free(ssl_ctx); closesocket(socket_fd);
        return false;
    }

    std::string request = "GET " + path + " HTTP/1.1\r\nHost: " + host + "\r\nConnection: close\r\nUser-Agent: " + get_random_user_agent() + "\r\n\r\n";
    SSL_write(ssl, request.c_str(), request.length());

    char buffer[8192];
    int bytes_received = SSL_read(ssl, buffer, sizeof(buffer));
    if (bytes_received <= 0) {
        SSL_free(ssl); SSL_CTX_free(ssl_ctx); closesocket(socket_fd); return false;
    }

    std::string response_str(buffer, bytes_received);
    size_t headers_end = response_str.find("\r\n\r\n");
    if (headers_end == std::string::npos) return false;

    std::ofstream output_file(output_path, std::ios::binary);
    size_t body_start = headers_end + 4;
    output_file.write(buffer + body_start, bytes_received - body_start);

    while ((bytes_received = SSL_read(ssl, buffer, sizeof(buffer))) > 0) {
        output_file.write(buffer, bytes_received);
    }

    output_file.close();
    SSL_shutdown(ssl); SSL_free(ssl); SSL_CTX_free(ssl_ctx); closesocket(socket_fd);
    return true;
}