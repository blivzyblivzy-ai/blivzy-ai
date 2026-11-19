#include "blivzy_ai.c"
int main(int argc,char**argv){
  App*a=calloc(1,sizeof(App));a->out=furi_string_alloc();a->sound=false;a->hu=(argc>1&&!strcmp(argv[1],"hu"));
  srand(1);char line[256];
  while(fgets(line,sizeof line,stdin)){size_t l=strlen(line);while(l&&(line[l-1]=='\n'||line[l-1]=='\r'))line[--l]=0;
    strlcpy(a->buf,line,sizeof a->buf);do_universal(a);printf("Q> %s\n%s\n=====\n",line,furi_string_get_cstr(a->out));}
  return 0;}
