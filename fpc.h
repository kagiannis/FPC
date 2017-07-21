//encode bytes,return bytes written
//on error return -1
//size < 64Kb
int prefix_encode(void *output,const void *in,int size,int sym_num);
//on error return -1
int prefix_decode(void * output,int out_size,const void *input,int in_size,int sym_num);

//return compressed size
//bsize < 64KB
//if bsize == 0 then adaptive
size_t comp_block(void * output,void * input,size_t inlen,int bsize);

//return uncompressed bytes
size_t dec_block(void * output,void * input,size_t inlen,size_t max_output);

#define FPC_MAX_OUTPUT(S,B) ((S) + 256 + (B == 0 ? (((S)/(32*1024))+1)*4:((S)/(B)+1)*4))//TODO
