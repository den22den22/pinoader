#include "http_client.h"
#include "logger.h"
#include <sstream>
#include <vector>
#include <fstream>
#include <algorithm>
#include <random>
#include <cstring>
#include <map>
#include <memory>
#include <iostream>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

#pragma comment(lib, "Ws2_32.lib")

const std::vector<std::string> USER_AGENTS = {
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:109.0) Gecko/20100101 Firefox/117.0"
};

struct SystemInitializer {
    SystemInitializer() {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            exit(1);
        }
    }
    ~SystemInitializer() {
        WSACleanup();
    }
};
static SystemInitializer sys_initializer;

std::string get_random_user_agent() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, USER_AGENTS.size() - 1);
    return USER_AGENTS[dis(gen)];
}

struct Connection {
    SOCKET socket_fd = INVALID_SOCKET;
    SSL* ssl = nullptr;
    std::string host;
    int port = 0;
    bool is_closed = false;

    ~Connection() { close_conn(); }

    void close_conn() {
        if (ssl) { SSL_shutdown(ssl); SSL_free(ssl); ssl = nullptr; }
        if (socket_fd != INVALID_SOCKET) { closesocket(socket_fd); socket_fd = INVALID_SOCKET; }
        is_closed = true;
    }
};

struct GlobalState {
    SSL_CTX* ssl_ctx = nullptr;
    std::map<std::string, std::unique_ptr<Connection>> pool;
    std::map<std::string, std::string> dns_cache;
    std::map<std::string, SSL_SESSION*> session_cache;

    GlobalState() {
        ssl_ctx = SSL_CTX_new(TLS_client_method());
        if (ssl_ctx) {
            SSL_CTX_set_default_verify_paths(ssl_ctx);
            SSL_CTX_set_session_cache_mode(ssl_ctx, SSL_SESS_CACHE_CLIENT);
            SSL_CTX_set_mode(ssl_ctx, SSL_MODE_RELEASE_BUFFERS); 
        }
    }

    ~GlobalState() {
        pool.clear();
        for (auto& kv : session_cache) SSL_SESSION_free(kv.second);
        if (ssl_ctx) SSL_CTX_free(ssl_ctx);
    }

    Connection* get_connection(const std::string& host, int port, bool use_ssl);
    void save_session(Connection* conn);
};

static GlobalState g_state;

struct BufferedStream {
    Connection* conn;
    char buffer[16384];
    int pos = 0;
    int end = 0;
    bool error = false;

    BufferedStream(Connection* c) : conn(c) {}

    int fill() {
        if (pos < end) {
            memmove(buffer, buffer + pos, end - pos);
            end -= pos;
        } else {
            end = 0;
        }
        pos = 0;

        int r = 0;
        if (conn->ssl) {
            r = SSL_read(conn->ssl, buffer + end, sizeof(buffer) - end);
        } else {
            r = recv(conn->socket_fd, buffer + end, sizeof(buffer) - end, 0);
        }

        if (r <= 0) {
            error = true;
            conn->close_conn();
        } else {
            end += r;
        }
        return r;
    }

    std::string read_line() {
        std::string line;
        while (!error) {
            if (pos >= end) if (fill() <= 0) break;
            
            char* newline = (char*)memchr(buffer + pos, '\n', end - pos);
            if (newline) {
                int len = (int)(newline - (buffer + pos)) + 1;
                line.append(buffer + pos, len);
                pos += len;
                return line;
            }
            line.append(buffer + pos, end - pos);
            pos = end;
        }
        return line;
    }

    void read_exact(std::string& out, long n) {
        size_t target = out.size() + n;
        out.reserve(target);
        while (n > 0 && !error) {
            if (pos >= end) if (fill() <= 0) break;
            
            int avail = end - pos;
            int to_copy = (avail < n) ? avail : (int)n;
            out.append(buffer + pos, to_copy);
            pos += to_copy;
            n -= to_copy;
        }
    }

    bool read_to_file(std::ofstream& outfile) {
        if (pos < end) {
            outfile.write(buffer + pos, end - pos);
            pos = end;
        }
        while (true) {
            int r = (conn->ssl) ? SSL_read(conn->ssl, buffer, sizeof(buffer)) : recv(conn->socket_fd, buffer, sizeof(buffer), 0);
            if (r <= 0) break;
            outfile.write(buffer, r);
        }
        return true;
    }
};

void GlobalState::save_session(Connection* conn) {
    if (!conn || !conn->ssl) return;
    SSL_SESSION* sess = SSL_get1_session(conn->ssl);
    if (sess) {
        if (session_cache.count(conn->host)) SSL_SESSION_free(session_cache[conn->host]);
        session_cache[conn->host] = sess;
    }
}

Connection* GlobalState::get_connection(const std::string& host, int port, bool use_ssl) {
    std::string key = host + ":" + std::to_string(port);
    
    if (pool.count(key)) {
        Connection* c = pool[key].get();
        if (!c->is_closed) {
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(c->socket_fd, &readfds);
            timeval tv = {0, 0};
            int ret = select(0, &readfds, NULL, NULL, &tv);
            
            if (ret > 0) {
                char buf[1];
                int r = recv(c->socket_fd, buf, 1, MSG_PEEK);
                if (r <= 0) {
                    c->close_conn();
                }
            }
            
            if (!c->is_closed) return c;
        }
        pool.erase(key);
    }

    auto conn = std::make_unique<Connection>();
    conn->host = host;
    conn->port = port;

    std::string ip;
    if (dns_cache.count(host)) {
        ip = dns_cache[host];
    } else {
        addrinfo hints = {}, *addrs;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &addrs) != 0) return nullptr;
        
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &((sockaddr_in*)addrs->ai_addr)->sin_addr, ip_str, INET_ADDRSTRLEN);
        ip = std::string(ip_str);
        dns_cache[host] = ip;
        freeaddrinfo(addrs);
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &server_addr.sin_addr);

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) return nullptr;

    BOOL flag = TRUE;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(BOOL));
    
    DWORD timeout = 10000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        closesocket(sock); return nullptr;
    }

    conn->socket_fd = sock;

    if (use_ssl) {
        conn->ssl = SSL_new(ssl_ctx);
        SSL_set_fd(conn->ssl, (int)sock);
        SSL_set_tlsext_host_name(conn->ssl, host.c_str());
        
        if (session_cache.count(host)) {
            SSL_set_session(conn->ssl, session_cache[host]);
        }

        if (SSL_connect(conn->ssl) <= 0) {
            conn->close_conn();
            return nullptr;
        }
        save_session(conn.get());
    }

    Connection* ptr = conn.get();
    pool[key] = std::move(conn);
    return ptr;
}

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
    HttpResponse response;
    bool use_ssl = (protocol == "https");

    for (int retry = 0; retry < 2; ++retry) {
        Connection* conn = g_state.get_connection(host, port, use_ssl);
        if (!conn) return response;

        std::string req = "GET " + path + " HTTP/1.1\r\nHost: " + host + "\r\nUser-Agent: " + get_random_user_agent() + "\r\nConnection: keep-alive\r\n\r\n";
        
        int sent = (conn->ssl) ? SSL_write(conn->ssl, req.c_str(), (int)req.length()) : send(conn->socket_fd, req.c_str(), (int)req.length(), 0);
        if (sent <= 0) {
            conn->close_conn();
            continue; 
        }

        BufferedStream stream(conn);
        std::string line = stream.read_line();
        if (line.empty()) {
            conn->close_conn();
            continue;
        }

        sscanf(line.c_str(), "HTTP/%*f %d", &response.status_code);

        long content_len = -1;
        bool chunked = false;
        bool connection_close = false;

        while (true) {
            line = stream.read_line();
            if (line == "\r\n" || line == "\n" || line.empty()) break;
            
            auto colon = line.find(':');
            if (colon != std::string::npos) {
                std::string key = to_lower(line.substr(0, colon));
                std::string val = line.substr(colon + 2);
                while (!val.empty() && (val.back() == '\r' || val.back() == '\n')) val.pop_back();
                response.headers[key] = val;

                if (key == "content-length") content_len = std::stol(val);
                if (key == "transfer-encoding" && val.find("chunked") != std::string::npos) chunked = true;
                if (key == "connection" && val.find("close") != std::string::npos) connection_close = true;
            }
        }

        if (content_len >= 0) {
            stream.read_exact(response.body, content_len);
        } else if (chunked) {
            while (true) {
                std::string size_line = stream.read_line();
                long chunk_size = 0;
                try { chunk_size = std::stol(size_line, nullptr, 16); } catch(...) { break; }
                if (chunk_size == 0) { stream.read_line(); break; } 
                stream.read_exact(response.body, chunk_size);
                stream.read_line(); 
            }
        } else {
             while (true) {
                char tmp[4096];
                int r = (conn->ssl) ? SSL_read(conn->ssl, tmp, sizeof(tmp)) : recv(conn->socket_fd, tmp, sizeof(tmp), 0);
                if (r <= 0) break;
                response.body.append(tmp, r);
             }
             connection_close = true;
        }

        if (connection_close) conn->close_conn();
        return response;
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

        log_debug("[http] Fetching: " + current_url);
        HttpResponse res = perform_request(protocol, host, path, port);

        if (res.status_code >= 300 && res.status_code < 400 && res.headers.count("location")) {
            std::string loc = res.headers["location"];
            if (loc.find("http") != 0) {
                 if (loc.front() == '/') loc = protocol + "://" + host + loc;
                 else loc = protocol + "://" + host + "/" + loc;
            }
            current_url = loc;
        } else if (res.status_code == 200) {
            return res.body;
        } else {
            return "";
        }
    }
    return "";
}

bool download_file(const std::string& url, const std::string& output_path) {
    log_normal("[downloader] Destination: " + output_path);
    std::string protocol, host, path;
    int port;
    if (!parse_url(url, protocol, host, path, port) || protocol != "https") return false;

    Connection* conn = g_state.get_connection(host, port, true);
    if (!conn) return false;

    std::string req = "GET " + path + " HTTP/1.1\r\nHost: " + host + "\r\nConnection: close\r\nUser-Agent: " + get_random_user_agent() + "\r\n\r\n";
    SSL_write(conn->ssl, req.c_str(), (int)req.length());

    BufferedStream stream(conn);
    std::string line = stream.read_line();
    if (line.empty()) return false;

    while (true) {
        line = stream.read_line();
        if (line == "\r\n" || line == "\n" || line.empty()) break;
    }

    std::ofstream outfile(output_path, std::ios::binary);
    stream.read_to_file(outfile);
    
    conn->close_conn();
    return true;
}