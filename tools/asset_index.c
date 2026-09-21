/* Read-only archive index discovery for the user's Xbox extraction. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static uint32_t u32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
int main(void) {
    FILE *banks[16]={0}, *out=NULL;
    uint64_t lengths[16]={0};
    char path[1024]; unsigned char header[156];
    unsigned valid=0, hitx=0, failed=0;
    for (unsigned i=0;i<16;i++) {
        snprintf(path,sizeof path,"%s/original/vc_53450030/%X",NFL2K5_PROJECT_ROOT,i);
        banks[i]=fopen(path,"rb");
        if(!banks[i]) {fprintf(stderr,"Missing archive %X\n",i); goto error;}
        _fseeki64(banks[i],0,SEEK_END); lengths[i]=(uint64_t)_ftelli64(banks[i]); rewind(banks[i]);
    }
    if(fread(header,1,sizeof header,banks[0])!=sizeof header || u32(header+8)!=16) goto error;
    uint32_t count=u32(header);
    if(!count || count>1000000 || 156ULL+12ULL*count>lengths[0]) goto error;
    for(unsigned i=0;i<16;i++) {
        if((uint64_t)u32(header+12+4*i)*2048!=lengths[i]) {
            fprintf(stderr,"Archive %X sector count mismatch\n",i); goto error;
        }
    }
    snprintf(path,sizeof path,"%s/analysis/archive-index.csv",NFL2K5_PROJECT_ROOT);
    out=fopen(path,"wb"); if(!out) goto error;
    fputs("entry,id,bank,offset,size_candidate,signature,embedded_labels\n",out);
    for(uint32_t i=0;i<count;i++) {
        unsigned char record[12], data[512]={0};
        _fseeki64(banks[0],156+12LL*i,SEEK_SET);
        if(fread(record,1,12,banks[0])!=12) goto error;
        uint32_t packed=u32(record+4), bank=0, size=packed&0x0FFFFFFFu;
        uint64_t offset=(uint64_t)u32(record+8)*2048;
        while(bank<15 && offset>=lengths[bank]) {offset-=lengths[bank]; bank++;}
        if(!size || offset>lengths[bank] || size>lengths[bank]-offset) {failed++; continue;}
        valid++;
        _fseeki64(banks[bank],(int64_t)offset,SEEK_SET);
        size_t n=fread(data,1,size<sizeof data?size:sizeof data,banks[bank]);
        char sig[5]={0}, labels[256]={0}; unsigned used=0;
        for(unsigned j=0;j<4 && j<n;j++) sig[j]=data[j]>=32&&data[j]<=126&&data[j]!=','?data[j]:'.';
        if(!memcmp(sig,"HITX",4)) hitx++;
        /* Record printable UTF-16LE runs, without assuming their meaning. */
        for(size_t j=4;j+1<n && used<220;j++) {
            size_t start=j;
            while(j+1<n && data[j]>=32 && data[j]<=126 && data[j]!=',' && data[j]!='"' && data[j+1]==0) j+=2;
            if(j-start>=8) {
                if(used) labels[used++]='|';
                for(size_t k=start;k<j && used<250;k+=2) labels[used++]=(char)data[k];
            } else j=start;
        }
        fprintf(out,"%u,%08X,%X,%llu,%u,%s,%s\n",i,u32(record),bank,(unsigned long long)offset,size,sig,labels);
    }
    fclose(out); out=NULL;
    for(unsigned i=0;i<16;i++) fclose(banks[i]);
    printf("Indexed %u entries; %u in bounds; %u invalid; %u begin with HITX.\n",count,valid,failed,hitx);
    printf("All 16 declared archive sector counts match the extracted files.\n");
    return failed?2:0;
error:
    if(out) fclose(out);
    for(unsigned i=0;i<16;i++) if(banks[i]) fclose(banks[i]);
    fprintf(stderr,"Index validation failed.\n"); return 1;
}
