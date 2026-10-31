#include "crc32.h"
watch_status_t crc32_compute(const uint8_t *data, size_t length, uint32_t *result)
{
    if(result==NULL || (data==NULL && length!=0U)) return WATCH_INVALID_ARGUMENT;
    uint32_t crc=0xFFFFFFFFU;
    for(size_t i=0U;i<length;++i){
        crc^=data[i];
        for(unsigned bit=0U;bit<8U;++bit) crc=(crc>>1U)^((crc&1U)?0xEDB88320U:0U);
    }
    *result=crc^0xFFFFFFFFU;
    return WATCH_OK;
}
