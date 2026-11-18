#include "motion_profile_cache.h"
#include "mbedtls/sha256.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#define CACHE_MAGIC 0x4D504331U
#define CACHE_VERSION 1U
typedef struct{uint32_t magic,version,payload_size;uint8_t hash[32];}cache_header_t;
static esp_err_t hash_file(const char*p,uint8_t h[32]){FILE*f=fopen(p,"rb");if(!f)return ESP_ERR_NOT_FOUND;mbedtls_sha256_context c;mbedtls_sha256_init(&c);int rc=mbedtls_sha256_starts(&c,0);uint8_t b[1024];size_t n;while(rc==0&&(n=fread(b,1,sizeof b,f))>0)rc=mbedtls_sha256_update(&c,b,n);if(ferror(f))rc=-1;fclose(f);if(rc==0)rc=mbedtls_sha256_finish(&c,h);mbedtls_sha256_free(&c);return rc==0?ESP_OK:ESP_FAIL;}
static esp_err_t read_cache(const char*p,const uint8_t h[32],motion_profile_t*x){FILE*f=fopen(p,"rb");if(!f)return ESP_ERR_NOT_FOUND;cache_header_t q;bool ok=fread(&q,1,sizeof q,f)==sizeof q&&q.magic==CACHE_MAGIC&&q.version==CACHE_VERSION&&q.payload_size==sizeof(*x)&&memcmp(q.hash,h,32)==0&&fread(x,1,sizeof(*x),f)==sizeof(*x)&&fgetc(f)==EOF;fclose(f);return ok&&motion_profile_validate(x,NULL,0)==ESP_OK?ESP_OK:ESP_ERR_INVALID_CRC;}
static esp_err_t write_cache(const char*p,const uint8_t h[32],const motion_profile_t*x){char t[320],b[320];if(snprintf(t,sizeof t,"%s.tmp",p)>=(int)sizeof t||snprintf(b,sizeof b,"%s.bak",p)>=(int)sizeof b)return ESP_ERR_INVALID_SIZE;FILE*f=fopen(t,"wb");if(!f)return ESP_FAIL;cache_header_t q={CACHE_MAGIC,CACHE_VERSION,sizeof(*x),{0}};memcpy(q.hash,h,32);bool ok=fwrite(&q,1,sizeof q,f)==sizeof q&&fwrite(x,1,sizeof(*x),f)==sizeof(*x)&&fflush(f)==0&&fsync(fileno(f))==0&&fclose(f)==0;if(!ok){remove(t);return ESP_FAIL;}remove(b);bool existed=access(p,F_OK)==0;if(existed&&rename(p,b)!=0){remove(t);return ESP_FAIL;}if(rename(t,p)!=0){if(existed)rename(b,p);remove(t);return ESP_FAIL;}remove(b);return ESP_OK;}
esp_err_t motion_profile_boot_load(const char*j,const char*b,motion_profile_t*x,bool*r){if(!j||!b||!x)return ESP_ERR_INVALID_ARG;uint8_t h[32];esp_err_t e=hash_file(j,h);if(e!=ESP_OK)return e;e=read_cache(b,h,x);if(e==ESP_OK){if(r)*r=false;return ESP_OK;}e=motion_profile_load_json(j,x);if(e!=ESP_OK)return e;e=write_cache(b,h,x);if(e==ESP_OK&&r)*r=true;return e;}
