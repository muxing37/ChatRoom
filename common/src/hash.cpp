#include "hash.h"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <fstream>
#include <iomanip>
#include <sstream>

std::string sha256File(const std::string& path) {
  std::ifstream ifs(path,std::ios::binary);
  if(!ifs.is_open()) return {};
  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  if(!ctx) return {};
  if(EVP_DigestInit_ex(ctx,EVP_sha256(),nullptr) != 1) {
    EVP_MD_CTX_free(ctx);
    return {};
  }
  char buf[1024 * 1024];
  while(ifs) {
    ifs.read(buf,sizeof(buf));
    std::streamsize n = ifs.gcount();
    if(n > 0 && EVP_DigestUpdate(ctx,buf,static_cast<size_t>(n)) != 1) {
      EVP_MD_CTX_free(ctx);
      return {};
    }
  }
  unsigned char md[EVP_MAX_MD_SIZE];
  unsigned int mdlen = 0;
  if(EVP_DigestFinal_ex(ctx,md,&mdlen) != 1) {
    EVP_MD_CTX_free(ctx);
    return {};
  }
  EVP_MD_CTX_free(ctx);
  std::ostringstream oss;
  oss << std::hex << std::setfill('0');
  for(int i = 0;i < mdlen;i++) {
    oss << std::setw(2) << static_cast<unsigned int>(md[i]);
  }
  return oss.str();
}

std::string passwordHash(const std::string& password,const std::string& salt) {
  int iter = 200000;
  unsigned char out[32];
  PKCS5_PBKDF2_HMAC(password.c_str(),(int)password.size(),
   (const unsigned char*)salt.data(),(int)salt.size(),
   iter,EVP_sha256(),32,out);
  std::ostringstream oss;
  oss << std::hex << std::setfill('0');
  for(int i = 0;i < 32;i++) {
    oss << std::setw(2) << static_cast<unsigned int>(out[i]);
  }
  return oss.str();
}

std::string getSalt() {
  unsigned char buf[16];
  RAND_bytes(buf,sizeof(buf));
  std::ostringstream oss;
  oss << std::hex << std::setfill('0');
  for(int i = 0;i < 16;i++) {
    oss << std::setw(2) << static_cast<unsigned int>(buf[i]);
  }
  return oss.str();
}
