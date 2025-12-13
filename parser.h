#ifndef PARSER_H
#define PARSER_H

#include <string>

std::string find_video_url(const std::string& html_content);

std::string find_thumbnail_url(const std::string& html_content);

#endif
