#ifndef PARSER_H
#define PARSER_H

#include <string>

// Function to find the video URL inside HTML content
std::string find_video_url(const std::string& html_content);

// Function to find the thumbnail URL inside HTML content
std::string find_thumbnail_url(const std::string& html_content);

#endif // PARSER_H
