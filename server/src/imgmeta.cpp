#include "imgmeta.h"
#include <cctype>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

bool imageSizeOfFile(const std::string& path, int& width, int& height) {
  width = height = 0;
  int x=0, y=0, n=0;
  if(!stbi_info(path.c_str(), &x, &y, &n)) return false;
  width = x; height = y;
  return width > 0 && height > 0;
}

bool isImageName(const std::string& name) {
  auto dot = name.find_last_of('.');
  if(dot == std::string::npos) return false;
  std::string ext = name.substr(dot+1);
  for(auto& c : ext) c = (char)tolower((unsigned char)c);
  return ext=="png"||ext=="jpg"||ext=="jpeg"||ext=="gif"||ext=="webp"||ext=="bmp"||ext=="svg";
}