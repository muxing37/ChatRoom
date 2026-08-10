#pragma once
#include <string>

std::string sha256File(const std::string& path);
std::string passwordHash(const std::string& password,const std::string& salt);
std::string getSalt();
