#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <string>
#include <map>

struct HttpResponse {
    int status_code = 0;
    std::map<std::string, std::string> headers;
    std::string body;
};

// Parses a URL into its components.
bool parse_url(const std::string& url, std::string& protocol, std::string& host, std::string& path, int& port);

// Fetches the page body and writes the final URL to final_url
std::string fetch_url(const std::string& initial_url, std::string& final_url, int max_redirects = 5);

// Downloads a file from a URL and saves it to output_path
bool download_file(const std::string& url, const std::string& output_path);

#endif // HTTP_CLIENT_H
