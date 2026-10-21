#!/usr/bin/env python3
"""Exercise the actual LYW C validator with mapped 256MiB upload addresses.
Requires a host C compiler and zlib. Uses U-Boot's own read-only libfdt,
not the host library, whose alignment rules can differ. Never accesses NAND.
"""
from pathlib import Path
import argparse,json,subprocess,tempfile,struct,zlib
ap=argparse.ArgumentParser();ap.add_argument('factory',type=Path);ap.add_argument('--report',type=Path);a=ap.parse_args()
r=Path(__file__).resolve().parents[1];source=(r/'u-boot-2016/board/qca/arm/common/failsafe.c').read_text();start=source.index('static int lyw_validate_factory(');end=source.index('\n}\n#endif',start)+2;function=source[start:end]
shim=r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <zlib.h>
typedef uint8_t u8; typedef uint32_t u32; typedef unsigned long ulong;
#define SZ_4K 4096UL
#define SZ_64K 65536UL
#define SZ_16M 16777216UL
#define CONFIG_IPQ_FDT_HIGH 0x4a400000UL
#define LYW_PEB_SIZE 131072UL
#define LYW_LEB_SIZE (LYW_PEB_SIZE-4096)
#define LYW_UBI_SIZE 0x07700000UL
#define LYW_VTBL_ID 0x7fffefff
#define RET_SUCCESS 0
#define RET_WRONG_FW_TYPE 1
#define ALIGN(x,a) (((x)+(a)-1)&~((a)-1))
#define min(a,b) ((a)<(b)?(a):(b))
static u32 get_unaligned_be32(const void *p) { const u8 *b=p; return (u32)b[0]<<24|(u32)b[1]<<16|(u32)b[2]<<8|b[3]; }
static unsigned get_unaligned_be16(const void *p) { const u8 *b=p; return (unsigned)b[0]<<8|b[1]; }
int fdt_check_header(const void *);
int fdt_path_offset(const void *, const char *);
int fdt_subnode_offset(const void *, int, const char *);
const void *fdt_getprop(const void *, int, const char *, int *);
int fdt_node_check_compatible(const void *, int, const char *);
static u32 fdt_totalsize(const void *f) { return get_unaligned_be32((const u8 *)f+4); }
static bool is_memory_region_available(uintptr_t p,size_t n) {return p>=0x41000000UL && p+n<CONFIG_IPQ_FDT_HIGH && p+n>=p;}
static int lyw_bad_factory(const char *s) {printf("REJECT: %s\n",s);return RET_WRONG_FW_TYPE;}
'''
main=r'''
int main(int argc,char **argv) {
 if(argc!=3) return 2;
 FILE *f=fopen(argv[1],"rb");if(!f)return 2;
 if(fseek(f,0,SEEK_END))return 2;long n=ftell(f);rewind(f);if(n<0)return 2;
 void *u=mmap((void *)0x41000000,0x08000000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
 void *w=mmap((void *)0x49000000,0x01400000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
 if(u==MAP_FAILED||w==MAP_FAILED){perror("mmap");return 2;}
 uintptr_t p=strtoull(argv[2],NULL,0);if(p<0x41000000UL||p+n>0x49000000UL)return 2;
 if(fread((void *)p,1,n,f)!=(size_t)n)return 2;fclose(f);
 return lyw_validate_factory((void *)p,n);
}
'''
b=a.factory.read_bytes();assert b[:4]==b'UBI#';cases=[]
cases.append(('valid-http-256M',b,'0x410000c0',0))
cases.append(('valid-tftp-style-address',b,'0x44000000',0))
x=bytearray(b);x[5]^=1;cases.append(('EC-CRC-corruption',x,'0x410000c0',1))
x=bytearray(b);x[2048+5]^=1;cases.append(('VID-CRC-corruption',x,'0x410000c0',1))
x=bytearray(b);x[4096+4]^=1;cases.append(('volume-table-CRC-corruption',x,'0x410000c0',1))
assert b'lingyan,lyw-30m' in b;cases.append(('wrong-board-DTB',b.replace(b'lingyan,lyw-30m',b'lingyan,lyx-30m'),'0x410000c0',1))
assert b'lingyan,nand-writable' in b;cases.append(('missing-NAND-profile',b.replace(b'lingyan,nand-writable',b'lingyan,nand-readonly'),'0x410000c0',1))
cases.append(('truncated-image',b[:-1],'0x410000c0',1))
x=bytearray(b)
for off in range(0,len(x),131072):
 if struct.unpack_from('>II',x,off+2056)==(0,1):
  struct.pack_into('>I',x,off+2060,99);struct.pack_into('>I',x,off+2048+60,zlib.crc32(x[off+2048:off+2048+60])^0xffffffff);break
else:raise AssertionError('fixture has no kernel LEB1')
cases.append(('missing-kernel-LEB',x,'0x410000c0',1))
results=[]
with tempfile.TemporaryDirectory(prefix='lyw-factory-check-') as tmp:
 t=Path(tmp);c=t/'validator.c';c.write_text(shim+function+main);exe=t/'validator'
 # Modern host libfdt rejects a valid FIT-embedded DTB at a 4-byte offset.
 # The actual bootloader accepts it. Compile its unmodified reader sources
 # and headers so this test exercises the deployed parser's rules.
 for name in ('fdt.h', 'libfdt.h'):
  (t/name).write_bytes((r/'u-boot-2016/include'/name).read_bytes())
 (t/'libfdt_env.h').write_text('''
#ifndef _LIBFDT_ENV_H
#define _LIBFDT_ENV_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <endian.h>
typedef uint16_t fdt16_t;
typedef uint32_t fdt32_t;
typedef uint64_t fdt64_t;
#define fdt32_to_cpu(x) be32toh(x)
#define cpu_to_fdt32(x) htobe32(x)
#define fdt64_to_cpu(x) be64toh(x)
#define cpu_to_fdt64(x) htobe64(x)
#endif
''')
 reader=r/'u-boot-2016/lib/libfdt'
 subprocess.run(['cc','-std=gnu11','-O2','-I',str(t),str(c),
                 str(reader/'fdt.c'),str(reader/'fdt_ro.c'),
                 '-lz','-o',str(exe)],check=True)
 for name,data,addr,expected in cases:
  f=t/'candidate.ubi';f.write_bytes(data);p=subprocess.run([str(exe),str(f),addr],capture_output=True,text=True)
  assert p.returncode==expected,(name,p.returncode,p.stdout,p.stderr)
  results.append({'case':name,'address':addr,'expected_returncode':expected,'returncode':p.returncode,'output':p.stdout.strip(),'passed':True})
print(json.dumps(results,indent=2))
if a.report:a.report.write_text(json.dumps(results,indent=2)+'\n')
