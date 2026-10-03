"""Real region loader: all generated cells, no file access after init, malformed data."""
import argparse
import struct
import subprocess
import tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--cc',default='gcc');a=ap.parse_args()
    data=(ROOT/'romfs/voxel/regions.bin').read_bytes()
    with tempfile.TemporaryDirectory(dir=ROOT/'build',prefix='region-test-') as tmp:
        tmp=Path(tmp);src=tmp/'test.c';exe=tmp/'test.exe'
        src.write_text(r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
static const char *input;
static unsigned opens;
static FILE *Open(const char *path) {(void)path;++opens;return fopen(input,"rb");}
#define VOXEL_FILE_H
#define VoxelFile_Open(path) Open(path)
unsigned char g_PortLogActive;
void Port_Log_Printf(const char *fmt,...) {(void)fmt;}
'''+'#include "'+(ROOT/'src/voxel/voxel_regions.c').as_posix()+'"\n'+r'''
int main(int argc,char **argv) {
    assert(argc==3); input=argv[1];
    bool expected=atoi(argv[2])!=0;
    assert(VoxelRegions_Init()==expected);
    if(!expected){assert(!sIndex && !sRoles);assert(VoxelRegions_RoleAt(1,0,0)==0);return 0;}
    FILE *f=fopen(input,"rb");assert(f);fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);
    uint8_t *raw=malloc(size);assert(raw);assert(fread(raw,1,size,f)==(size_t)size);fclose(f);
    unsigned count=Read16(raw+4),cells=0;
    /* Any attempted reload would now fail. All layouts remain queryable. */
    input="nonexistent-region-test-file";
    for(unsigned pass=0;pass<2;++pass)for(unsigned j=0;j<count;++j){
        unsigned i=pass ? count-1-j : j;const uint8_t *row=raw+8+12*i;
        unsigned id=Read16(row),w=Read16(row+2),h=Read16(row+4),off=Read32(row+8);
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
            assert(VoxelRegions_RoleAt(id,x,y)==raw[off+y*w+x]);++cells;
        }
        assert(VoxelRegions_RoleAt(id,-1,0)==0 && VoxelRegions_RoleAt(id,w,0)==0);
        assert(VoxelRegions_RoleAt(id,0,h)==0 && VoxelRegions_RoleAt(id,0,-1)==0);
    }
    assert(opens==1);assert(VoxelRegions_RoleAt(65536,0,0)==0);
    VoxelRegions_Shutdown();VoxelRegions_Shutdown();assert(VoxelRegions_RoleAt(1,0,0)==0);
    input=argv[1];assert(VoxelRegions_Init());VoxelRegions_Shutdown();free(raw);
    printf("PASS resident regions: %u layouts, %u cell comparisons, no query I/O, bounds and restart\n",count,cells);
}
''')
        subprocess.run([a.cc,'-std=c99','-O2','-Wall','-Wextra','-Werror','-I'+str(ROOT/'compat'),str(src),'-o',str(exe)],check=True)
        file=tmp/'regions.bin';file.write_bytes(data)
        subprocess.run([str(exe),str(file),'1'],check=True)
        # Odd cell count, both nibbles and last byte padding.
        file.write_bytes(b'VXR5'+struct.pack('<HH',1,0)+struct.pack('<HHHHI',1,3,1,0,20)+bytes([10,1,9]))
        subprocess.run([str(exe),str(file),'1'],check=True)
        cases=[data[:7],data[:-1],b'BAD!'+data[4:]]
        for offset,value in [(8,0),(10,0),(16,0xffffffff),(20,1)]:
            bad=bytearray(data);struct.pack_into('<I' if offset==16 else '<H',bad,offset,value);cases.append(bad)
        bad=bytearray(data);bad[struct.unpack_from('<I',data,16)[0]]=255;cases.append(bad)
        for bad in cases:
            file.write_bytes(bad);subprocess.run([str(exe),str(file),'0'],check=True)
        print('PASS malformed region data:',len(cases),'cases')
if __name__=='__main__':main()
