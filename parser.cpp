#include "parser.h"
#include <vector>
#include <string>

// Function to find the video URL inside HTML content
std::string find_video_url(const std::string& html_content) {
    // A list of ALL known anchors, from newest to oldest.
    // This makes the program resilient to different page versions from Pinterest.
    const std::vector<std::string> anchors = {
        "\"v720P\":{\"thumbnail\":",                  // NEW anchor for the latest JSON structure
        "\"videoList720P\":{\"v720P\":{\"thumbnail\"", // Newest and most specific anchor
        "\"video_list\":{\"V_720P\"",                  // An older, but still common anchor
        "\"videoList\":{\"V_720P\"",
        "\"v720P\":{\"url\""
    };

    const std::string url_marker = "\"url\":\"";
    const std::string url_end_marker = "\"";

    // Iterate through all anchors in the list
    for (const auto& anchor : anchors) {
        // Find the position of the current anchor
        size_t anchor_pos = html_content.find(anchor);

        // If the anchor is found...
        if (anchor_pos != std::string::npos) {
            // ...look for the URL marker ("url":") after it
            size_t url_start_pos = html_content.find(url_marker, anchor_pos);
            if (url_start_pos != std::string::npos) {
                // Move the position to the beginning of the URL itself
                url_start_pos += url_marker.length();

                // Find the closing quote that marks the end of the URL
                size_t url_end_pos = html_content.find(url_end_marker, url_start_pos);

                if (url_end_pos != std::string::npos) {
                    // Extract and IMMEDIATELY return the found URL.
                    // No need to search further.
                    return html_content.substr(url_start_pos, url_end_pos - url_start_pos);
                }
            }
        }
    }

    // If none of the anchors worked, return an empty string
    return "";
}


// A simplified and more robust function to find the thumbnail URL
std::string find_thumbnail_url(const std::string& html_content) {
    const std::string end_marker = "\"";

    // --- PRIORITY #1: Look for the standard Open Graph (og:image) meta tag. ---
    // This is the most reliable method.
    const std::string og_marker = "<meta property=\"og:image\" content=\"";
    size_t start_pos = html_content.find(og_marker);
    if (start_pos != std::string::npos) {
        start_pos += og_marker.length();
        size_t end_pos = html_content.find(end_marker, start_pos);
        if (end_pos != std::string::npos) {
            // Just return the URL as is. It's usually high quality.
            return html_content.substr(start_pos, end_pos - start_pos);
        }
    }

    // --- PRIORITY #2 (fallback): If og:image is not found, search in the JSON blob. ---
    const std::string anchor = "\"v720P\":{\"thumbnail\":\""; // UPDATED anchor
    const std::string thumb_marker = "\"thumbnail\":\"";

    size_t anchor_pos = html_content.find(anchor);
    if (anchor_pos != std::string::npos) {
        start_pos = html_content.find(thumb_marker, anchor_pos);
        if (start_pos != std::string::npos) {
            start_pos += thumb_marker.length();
            size_t end_pos = html_content.find(end_marker, start_pos);
            if (end_pos != std::string::npos) {
                // Just return the URL as is. No replacements needed.
                return html_content.substr(start_pos, end_pos - start_pos);
            }
        }
    }

    // Return empty if nothing is found
    return "";
}