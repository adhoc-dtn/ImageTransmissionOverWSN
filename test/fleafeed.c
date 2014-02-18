#define GNU_SOURCE
 
 
#include <dlfcn.h>
#include <stdio.h>
#define FF_FIRST __attribute__ ((constructor))
#define FF_LAST  __attribute__ ((destructor))
#define FF_MAGIC 0xf1eafeed
#define FF_MAX 1024*1024
 
int FF_ALLOCCOUNT=0;
int FF_FREECOUNT=0;
int FF_USEHEAP=0;
int FF_USESTACK=0;
 
 
static void*(*libc_malloc)(size_t);
static void*(*libc_calloc)(size_t,size_t);
static void (*libc_free  )(void *);
 
void *malloc(size_t size){
   FF_ALLOCCOUNT++;
   FF_USEHEAP+=size;
   return (*libc_malloc)(size);
}
void *calloc(size_t nmemb,size_t size){
   FF_ALLOCCOUNT++;
   FF_USEHEAP+=size*nmemb;
   return (*libc_calloc)(nmemb,size);
}
void free(void *ptr){
   FF_FREECOUNT++;
   (*libc_free)(ptr);
}
/*void *realloc(void *ptr,size_t size){
}*/
 
FF_FIRST int fleafeed_fillstack(){
   int p[FF_MAX];
   int i=0;
 
   libc_malloc=dlsym((void *)-1L,"malloc");
   libc_free=dlsym((void *)-1L,"free");
   libc_calloc=dlsym((void *)-1L,"calloc");
 
   for(;i<FF_MAX;i++){
      p[i]=FF_MAGIC;
   }
}
FF_LAST void fleafeed_checkstack(){
   int p[FF_MAX];
   int i=0;
 
   for(;i<FF_MAX;i++){
      FF_USESTACK+=(p[i]==FF_MAGIC)?0:4;
   }
   fflush(stdout);
   fprintf(stderr,"\n-----------fleafeed results------------");
   fprintf(stderr,"\n--USING STACK\t:%8d bytes",FF_USESTACK);
   fprintf(stderr,"\n--USING HEAP\t:%8d bytes",FF_USEHEAP);
   fprintf(stderr,"\n--ALLOC times\t:%8d",FF_ALLOCCOUNT);
   fprintf(stderr,"\n--FREE times\t:%8d\n",FF_FREECOUNT);
}
