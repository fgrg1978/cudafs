#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <cuda_runtime.h>

#include "config.h"

extern "C" {
#include "cuda_utils.h"
}


extern "C" int gpu_write(struct metadata *file, struct list *flist)
{
	cudaError_t err;

	if (file[flist->inode].cuda_state & LOCK_FILE)
		return 0;

	if (file[flist->inode].cuda_data) 
		cudaFree(file[flist->inode].cuda_data);

	err = cudaMalloc(&file[flist->inode].cuda_data, file[flist->inode].size-1);
	if(err != cudaSuccess) {
		return 0;
	} else {
		file[flist->inode].cuda_state |= LOCK_FILE;

		err = cudaMemcpy(file[flist->inode].cuda_data,
				file[flist->inode].data, 
				file[flist->inode].size,
				cudaMemcpyHostToDevice);
		if(err != cudaSuccess) {
			file[flist->inode].cuda_state = ~LOCK_FILE;
			return 0;
		} else {
			file[flist->inode].cuda_state = 0;
			file [flist->inode].cuda_state |= IN_CUDA_MEM;
			free(file [flist->inode].data);
		}
	}
	return 1;
}


extern "C" int gpu_read(struct metadata *file, struct list *flist, int free)
{
	cudaError_t err;

	if (file[flist->inode].cuda_state & LOCK_FILE || !(file[flist->inode].cuda_state & IN_CUDA_MEM) || file[flist->inode].data) 
		return 0;

	if (file[flist->inode].cuda_state & IN_CUDA_MEM &&
			(!file[flist->inode].data)) 
	{
		file[flist->inode].data = (char *) malloc( file[flist->inode].size);

		if (!file[flist->inode].data) return -ENOMEM;

		err = cudaMemcpy(file[flist->inode].data,
				file[flist->inode].cuda_data,
				file[flist->inode].size-1,
				cudaMemcpyDeviceToHost);

		if(err != cudaSuccess)
			return -ENOMEM;
		
		file [flist->inode].cuda_state = 0;
		
		if (free) 
			cudaFree(file[flist->inode].cuda_data);
		else
			file [flist->inode].cuda_state |= IN_CUDA_MEM;
	}
	return 1;
}

extern "C" void gpu_free(struct metadata *file, struct list *flist)
{

	if (file[flist->inode].cuda_state & LOCK_FILE) return;

	if (file[flist->inode].cuda_data) cudaFree(file[flist->inode].cuda_data);
}
