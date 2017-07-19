//encode bytes,return bytes written
//size < 64Kb
int prefix_encode(void *output,const void *in,int size,int sym_num);
void prefix_decode(void * output,int out_size,const void *input,int in_size,int sym_num);

//return compressed size
//bsize < 64KB
//if bsize == 0 then adaptive
size_t comp_block(void * output,void * input,size_t inlen,int bsize);
size_t dec_block(void * output,void * input,size_t inlen,size_t max_output);

#define FPC_MAX_OUTPUT(x) ((x) + ((x)/(63 << 10)*4) + 256)//TODO
