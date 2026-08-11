#pragma once
#include <string>

// 从图片文件解析宽高（stb_image 支持所有常见格式）
// 返回 true 表示成功，width/height > 0
bool imageSizeOfFile(const std::string& path, int& width, int& height);
// 判断文件名扩展名是否为图片（png/jpg/jpeg/gif/webp/bmp/svg）
bool isImageName(const std::string& name);