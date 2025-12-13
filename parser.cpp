#include "parser.h"
#include <vector>
#include <string>

std::string find_video_url(const std::string& html_content) {
    const std::vector<std::string> anchors = {
        "\"v720P\":{\"thumbnail\":",
        "\"videoList720P\":{\"v720P\":{\"thumbnail\"",
        "\"video_list\":{\"V_720P\"",
        "\"videoList\":{\"V_720P\"",
        "\"v720P\":{\"url\""
    };

    const std::string url_marker = "\"url\":\"";
    const std::string url_end_marker = "\"";

    for (const auto& anchor : anchors) {
        size_t anchor_pos = html_content.find(anchor);

        if (anchor_pos != std::string::npos) {
            size_t url_start_pos = html_content.find(url_marker, anchor_pos);
            if (url_start_pos != std::string::npos) {
                url_start_pos += url_marker.length();

                size_t url_end_pos = html_content.find(url_end_marker, url_start_pos);

                if (url_end_pos != std::string::npos) {
                    return html_content.substr(url_start_pos, url_end_pos - url_start_pos);
                }
            }
        }
    }

    return "";
}

std::string find_thumbnail_url(const std::string& html_content) {
    const std::string end_marker = "\"";

    const std::string og_marker = "<meta property=\"og:image\" content=\"";
    size_t start_pos = html_content.find(og_marker);
    if (start_pos != std::string::npos) {
        start_pos += og_marker.length();
        size_t end_pos = html_content.find(end_marker, start_pos);
        if (end_pos != std::string::npos) {
            return html_content.substr(start_pos, end_pos - start_pos);
        }
    }

    const std::string anchor = "\"v720P\":{\"thumbnail\":\"";
    const std::string thumb_marker = "\"thumbnail\":\"";

    size_t anchor_pos = html_content.find(anchor);
    if (anchor_pos != std::string::npos) {
        start_pos = html_content.find(thumb_marker, anchor_pos);
        if (start_pos != std::string::npos) {
            start_pos += thumb_marker.length();
            size_t end_pos = html_content.find(end_marker, start_pos);
            if (end_pos != std::string::npos) {
                return html_content.substr(start_pos, end_pos - start_pos);
            }
        }
    }

    return "";
}