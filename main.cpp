#include <iostream>
#include <string>
#include <vector>
#include <chrono>

#include "parser.h"
#include "http_client.h"
#include "logger.h"

// Define the global variable declared in logger.h
LogLevel g_log_level = LogLevel::NORMAL;

// Function to print the help message
void print_help(const char* program_name) {
    std::cout << "pinoader - A utility for downloading videos from Pinterest." << std::endl;
    std::cout << std::endl;
    std::cout << "USAGE:" << std::endl;
    std::cout << "  " << program_name << " <pinterest_url> [OPTIONS]" << std::endl;
    std::cout << std::endl;
    std::cout << "OPTIONS:" << std::endl;
    std::cout << "  -o, --output <filename>  Set a custom filename for the video." << std::endl;
    std::cout << "                           If not specified, the name is generated from the pin ID." << std::endl;
    std::cout << std::endl;
    std::cout << "  -t, --thumbnail          Download the thumbnail (cover image) for the video." << std::endl;
    std::cout << "                           The file will be saved with the same name as the video, but with a .jpg extension." << std::endl;
    std::cout << std::endl;
    std::cout << "  --debug                  Enable debug mode with verbose output." << std::endl;
    std::cout << "  --clear                  Silent mode, shows only errors." << std::endl;
    std::cout << std::endl;
    std::cout << "  -h, --help               Show this help message and exit." << std::endl;
    std::cout << std::endl;
    std::cout << "EXAMPLES:" << std::endl;
    std::cout << "  " << program_name << " https://pin.it/example" << std::endl;
    std::cout << "  " << program_name << " https://pin.it/example -o my_video.mp4" << std::endl;
    std::cout << "  " << program_name << " https://pin.it/example -t --debug" << std::endl;
}

std::string extract_pin_id(const std::string& url) {
    size_t start_pos = url.find("/pin/");
    if (start_pos == std::string::npos) return "";
    start_pos += 5;
    size_t end_pos = url.find('/', start_pos);
    return (end_pos == std::string::npos) ? url.substr(start_pos) : url.substr(start_pos, end_pos - start_pos);
}

std::string sanitize_pinterest_url(const std::string& dirty_url) {
    size_t pin_pos = dirty_url.find("/pin/");
    if (pin_pos == std::string::npos) {
        return dirty_url;
    }
    size_t end_pin_id = dirty_url.find('/', pin_pos + 5);
    if (end_pin_id == std::string::npos) {
        return dirty_url;
    }
    return dirty_url.substr(0, end_pin_id + 1);
}

int main(int argc, char* argv[]) {
    auto start_time = std::chrono::high_resolution_clock::now();

    if (argc < 2) {
        print_help(argv[0]);
        return 1;
    }

    std::string url;
    std::string custom_filename;
    bool download_thumbnail = false;

    // A new, more robust argument parser
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_help(argv[0]);
            return 0; // Successful exit after showing help
        } else if (arg == "--debug") {
            g_log_level = LogLevel::DEBUG;
        } else if (arg == "--clear") {
            g_log_level = LogLevel::SILENT;
        } else if (arg == "-t" || arg == "--thumbnail") {
            download_thumbnail = true;
        } else if (arg == "-o" || arg == "--output") {
            if (i + 1 < argc) {
                custom_filename = argv[++i];
            } else {
                log_error("Option '" + arg + "' requires a filename.");
                return 1;
            }
        } else if (arg.rfind("-", 0) != 0) {
            if (url.empty()) {
                url = arg;
            } else {
                log_error("Multiple URLs provided. Please provide only one.");
                return 1;
            }
        } else {
            log_error("Unknown option: " + arg);
            print_help(argv[0]);
            return 1;
        }
    }

    if (url.empty()) {
        log_error("No URL provided.");
        print_help(argv[0]);
        return 1;
    }

    log_debug("[main] Debug mode is enabled.");
    log_debug("[main] Target URL: " + url);
    if (!custom_filename.empty()) log_debug("[main] Custom filename requested: " + custom_filename);
    if (download_thumbnail) log_debug("[main] Thumbnail download requested.");

    std::string final_url;
    log_normal("[pinterest] " + url + ": Resolving URL");

    std::string html_content = fetch_url(url, final_url);

    std::string clean_url = sanitize_pinterest_url(final_url);
    if (clean_url != final_url) {
        log_debug("[pinterest] URL contains extra parameters. Sanitizing to: " + clean_url);
        final_url = clean_url;
        std::string temp_redirected_url;
        html_content = fetch_url(final_url, temp_redirected_url);
    }

    if (html_content.empty()) {
        log_error("Failed to fetch HTML content from the final URL.");
        return 1;
    }

    log_debug("[pinterest] Final URL: " + final_url);
    log_debug("[parser] Received " + std::to_string(html_content.length()) + " bytes of HTML. Parsing for media URL.");
    
    // Logic for downloading the thumbnail
    if (download_thumbnail) {
        std::string thumbnail_url = find_thumbnail_url(html_content);
        if (thumbnail_url.empty()) {
            log_error("Could not find thumbnail URL on the page.");
        } else {
            log_normal("[downloader] Thumbnail URL: " + thumbnail_url);
            std::string thumb_filename;
            if (!custom_filename.empty()) {
                size_t dot_pos = custom_filename.find_last_of(".");
                if (dot_pos != std::string::npos) {
                    thumb_filename = custom_filename.substr(0, dot_pos) + ".jpg";
                } else {
                    thumb_filename = custom_filename + ".jpg";
                }
            } else {
                std::string pin_id = extract_pin_id(final_url);
                thumb_filename = (pin_id.empty() ? "pinterest_video" : pin_id) + ".jpg";
            }

            if (download_file(thumbnail_url, thumb_filename)) {
                log_normal("[pinoader] Thumbnail saved: " + thumb_filename);
            } else {
                log_error("Failed to download the thumbnail.");
            }
        }
    }

    std::string media_url = find_video_url(html_content);

    if (media_url.empty()) {
        log_error("Could not find media URL on the page.");
        return 1;
    }

    log_normal("[downloader] Media URL: " + media_url);

    std::string video_filename;
    if (!custom_filename.empty()) {
        video_filename = custom_filename;
    } else {
        std::string pin_id = extract_pin_id(final_url);
        video_filename = (pin_id.empty() ? "pinterest_video" : pin_id) + ".mp4";
    }

    if (download_file(media_url, video_filename)) {
        log_normal("[pinoader] File saved: " + video_filename);
    } else {
        log_error("Failed to download the file.");
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end_time - start_time;
    log_debug("[main] Total execution time: " + std::to_string(elapsed.count()) + " seconds.");

    return 0;
}
