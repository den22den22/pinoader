#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <string>
#include <map>

struct HttpResponse {
    int status_code = 0;
    std::map<std::string, std::string> headers;
    std::string body;
};

bool parse_url(const std::string& url, std::string& protocol, std::string& host, std::string& path, int& port);

std::string fetch_url(const std::string& initial_url, std::string& final_url, int max_redirects = 5);

bool download_file(const std::string& url, const std::string& output_path);

#endif
