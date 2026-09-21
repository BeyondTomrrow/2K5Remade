/* Title-specific HITX P8 reader, validated against the local retail archive.
   Xbox Morton-ordered indices are expanded through the embedded BGRA palette. */
#include "legal_texture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t le32(const unsigned char *p) {
    return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static unsigned morton(unsigned x,unsigned y,unsigned w,unsigned h) {
    unsigned address=0,out=1;
    for(unsigned bit=1;bit<w||bit<h;bit<<=1) {
        if(bit<w) {if(x&bit)address|=out;out<<=1;}
        if(bit<h) {if(y&bit)address|=out;out<<=1;}
    }
    return address;
}
uint32_t *load_legal_texture(unsigned *width,unsigned *height) {
    char path[1024]; unsigned char index[168],head[160];
    FILE *f=NULL; unsigned char *data=NULL; uint32_t *pixels=NULL;
    *width=*height=0;
    snprintf(path,sizeof path,"%s/original/vc_53450030/0",NFL2K5_PROJECT_ROOT);
    f=fopen(path,"rb"); if(!f)return NULL;
    if(fread(index,1,sizeof index,f)!=sizeof index || le32(index+156)!=0xEDD549BD)goto done;
    uint64_t offset=(uint64_t)le32(index+164)*2048;
    uint32_t record_size=le32(index+160)&0x0FFFFFFF;
    if(_fseeki64(f,offset,SEEK_SET) || fread(head,1,sizeof head,f)!=sizeof head)goto done;
    if(memcmp(head,"HITX",4)||memcmp(head+44,"TXTR",4)||memcmp(head+64,"l\0e\0g\0a\0l\0p\0a\0g\0e\0",18))goto done;
    uint32_t fmt=le32(head+96),chunk=le32(head+4);
    unsigned w=1u<<((fmt>>20)&15),h=1u<<((fmt>>24)&15);
    if(((fmt>>8)&255)!=0x0B || w>4096 || h>4096 || chunk!=128u+w*h+1024u || (uint64_t)chunk+32>record_size)goto done;
    data=malloc((size_t)w*h+1024); pixels=malloc((size_t)w*h*4);
    if(!data||!pixels)goto fail;
    if(fread(data,1,(size_t)w*h+1024,f)!=(size_t)w*h+1024)goto fail;
    for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++) {
        unsigned p=data[morton(x,y,w,h)];
        pixels[y*w+x]=le32(data+w*h+4*p)|0xFF000000u;
    }
    *width=w;*height=h;goto done;
fail:
    free(pixels);pixels=NULL;
done:
    free(data);fclose(f);return pixels;
}
