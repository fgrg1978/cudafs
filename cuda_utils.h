#ifdef GPU_UTILS_H
#define GPU_UTILS_H

extern int gpu_read(struct metadata *file, struct list *flist, int free);
extern int gpu_write(struct metadata *file, struct list *flist);
extern void gpu_free(struct metadata *file, struct list *flist)

#endif
