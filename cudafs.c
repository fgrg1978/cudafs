#define FUSE_USE_VERSION 26

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <fuse.h>
#include <pthread.h>

#include "config.h"
#include "cuda_utils.h"


struct fsdata fs_stat;
struct directory *root , *lastdir;
struct metadata *file;


void get_dirname_filename ( const char *path, char *dirname, char* fname ) {

	const char *ptr = strrchr(path, '/');

	memset(dirname, 0, LNAME_MAX);
	memset(fname, 0, LNAME_MAX);

	strncpy(dirname, path, (ptr - path) );
	strcpy(fname, ptr+1);

}

int fill_file_data(char *dirname, char* fname) {

	struct list *flist;
	struct directory *dir = root;
	int i;
	int ret = 0;
	int size = (int)sizeof(struct list);

	for( i = 0; i < fs_stat.max_no_of_files; i++ ) {
		if ( file[i].inuse == 0 )
			break;
	}

	file[i].inode = i;
	file[i].size = 0;
	file[i].data = NULL;
	file[i].inuse = 1;
	file[i].mode = S_IFREG | 0777;
	file[i].accesstime = time(NULL);
	file[i].modifiedtime = time(NULL);
	file[i].cuda_state = 0;
	file[i].cuda_data = NULL;


	flist = (struct list *) malloc(size);
	if ( flist == NULL ) {
		perror("malloc:");
		return -ENOMEM;
	}
	strcpy(flist->fname, fname);
	flist->inode = i;
	flist->next = NULL;


	while( dir != NULL ) {
		if ( strcmp(dirname, dir->name) == 0 )
			break;
		dir = dir->next;
	}

	file [dir->inode].accesstime = time(NULL);
	file [dir->inode].modifiedtime = time(NULL);


	if ( dir->ptr ==  NULL ) {
		dir->ptr = flist;
		dir->lptr = flist;
	}
	else {
		dir->lptr->next = flist;
		dir->lptr = flist;
	}


	fs_stat.free_bytes = fs_stat.free_bytes - size;
	fs_stat.used_bytes = fs_stat.used_bytes + size;
	fs_stat.avail_no_of_files--;

	return ret;
}


int fill_directory_data( char *dirname, char *fname ) {

	struct directory *dir = root;
	struct list *flist;
	struct directory *newdir;
	int ret = 0;
	int i;
	int dir_size = (int) sizeof (struct directory);
	int file_size = (int) sizeof (struct list);

	for( i = 0; i < fs_stat.max_no_of_files; i++ ) {
		if ( file[i].inuse == 0 )
			break;
	}

	file[i].inode = i;
	file[i].size = 0;
	file[i].data = NULL;
	file[i].inuse = 1;
	file[i].mode = S_IFDIR | 0755;
	file[i].accesstime = time(NULL);
	file[i].modifiedtime = time(NULL);

	newdir = (struct directory *) malloc(dir_size);
	if ( newdir == NULL ) {
		perror("malloc:");
		return -ENOMEM;
	}
	strcpy(newdir->name, dirname);

	if ( strcmp(dirname, "/") != 0 )
		strcat(newdir->name, "/");

	strcat(newdir->name, fname);
	newdir->inode = i;
	newdir->next = NULL;
	newdir->ptr =  NULL;
	newdir->lptr = NULL;

	flist = (struct list *) malloc(file_size);
	if ( flist == NULL ) {
		perror("malloc:");
		return -ENOMEM;
	}
	strcpy(flist->fname, fname);
	flist->inode = i;
	flist->next = NULL;

	while( dir != NULL ) {
		if ( strcmp(dirname, dir->name) == 0 )
			break;
		dir = dir->next;
	}

	file [dir->inode].accesstime = time(NULL);
	file [dir->inode].modifiedtime = time(NULL);
	if ( dir->ptr ==  NULL ) {
		dir->ptr = flist;
		dir->lptr = flist;
	}
	else {
		dir->lptr->next = flist;
		dir->lptr = flist;
	}

	lastdir->next = newdir;
	lastdir = newdir;

	fs_stat.free_bytes = fs_stat.free_bytes - dir_size - file_size ;
	fs_stat.used_bytes = fs_stat.used_bytes + dir_size + file_size ;
	fs_stat.avail_no_of_files--;

	return ret;

}



static void* cuda_init(struct fuse_conn_info *conn) {

	unsigned long metadata_size;

	metadata_size = fs_stat.total_size * MSIZE / 100  ; 
	fs_stat.max_no_of_files = metadata_size / sizeof ( struct metadata );
	fs_stat.avail_no_of_files = fs_stat.max_no_of_files - 1;
	fs_stat.free_bytes = fs_stat.total_size - metadata_size - sizeof(struct directory );
	fs_stat.used_bytes = sizeof ( struct directory );


	root = ( struct directory *) malloc ( sizeof ( struct directory ) );
	if ( root == NULL) {
		perror("malloc:");
		exit(-1);
	}

	strcpy(root->name, "/");
	root->inode = 0;
	root->ptr  =  NULL;
	root->lptr =  NULL;
	root->next =  NULL;

	lastdir = root;

	file = (struct metadata *) calloc ( fs_stat.max_no_of_files, sizeof ( struct metadata ) );

	if (file == NULL) {
		perror("malloc:");
		exit(-1);
	}
	file [ROOT].inode = 0;
	file [ROOT].size = 0;
	file [ROOT].data = NULL;
	file [ROOT].inuse = 1;
	file [ROOT].mode = S_IFDIR | 0755;
	file [ROOT].accesstime = time(NULL);
	file [ROOT].modifiedtime = time(NULL);

	return 0;

}


static int cuda_getattr(const char *path, struct stat *stbuf) {

	int ret = 0;
	char dirname [LPATH_MAX];
	char fname [LNAME_MAX];
	struct directory *dir = root;
	struct list *flist;
	int isexists = 0;
	int index = 0;

	memset(dirname, 0, LPATH_MAX);
	memset(fname, 0, LNAME_MAX);
	memset(stbuf, 0, sizeof ( struct stat ) );

	strcpy(dirname, path);
	if ( strcmp(path, "/") != 0 ) { 
		get_dirname_filename ( path, dirname, fname );
		if ( strlen(dirname) == 0 && strlen(fname) != 0 )
			strcpy(dirname, "/");

		while ( dir != NULL ) {

			if ( strcmp(dir->name, dirname) == 0 ) {
				flist = dir->ptr;
				while ( flist != NULL && strlen(fname) != 0 ) {
					if ( strcmp(flist->fname, fname) == 0 ) {
						isexists = 1;
						index = flist->inode;
						break;
					}
					flist = flist->next;
				}

				break;
			}

			dir = dir->next;
		}

	}
	else 
		isexists = 1;

	if ( !isexists ) {
		return -ENOENT;
	}

	if ( S_ISDIR ( file [index].mode ) ) {
		stbuf->st_mode = file [index].mode;
		stbuf->st_nlink = 2; 
		stbuf->st_atime = file [index].accesstime;
		stbuf->st_mtime = file [index].modifiedtime;
		stbuf->st_size = 4096;
		stbuf->st_blocks = 4;
		stbuf->st_blksize = 1;
	}
	else {
		stbuf->st_mode = file [index].mode;
		stbuf->st_nlink = 1;
		stbuf->st_blocks = file [index].size;
		stbuf->st_size =  file [index].size;
		stbuf->st_atime = file [index].accesstime;
		stbuf->st_mtime = file [index].modifiedtime;
		stbuf->st_blksize = 1;
	}

	return ret;
}


static int cuda_statfs(const char *path, struct statvfs *stbuf) {

	memset(stbuf, 0, sizeof ( struct statvfs ) );

	stbuf->f_bsize = 1;
	stbuf->f_frsize = 1;
	stbuf->f_blocks = fs_stat.total_size;
	stbuf->f_bfree = fs_stat.free_bytes;
	stbuf->f_files = fs_stat.max_no_of_files;
	stbuf->f_ffree = fs_stat.avail_no_of_files;
	stbuf->f_namemax = LNAME_MAX;
	stbuf->f_bavail = fs_stat.free_bytes;

	return 0;
}


int cuda_utime(const char *path, struct utimbuf *ubuf) {

	int ret = 0;
	char dirname[LPATH_MAX];
	char fname [LNAME_MAX];
	struct directory *dir = root;
	struct list *flist;
	int isexists = 0;

	memset(dirname, 0, LPATH_MAX);
	memset(fname, 0, LNAME_MAX);

	get_dirname_filename ( path, dirname, fname );

	if ( strlen(dirname) == 0 || strlen(fname) == 0  ) {
		if ( fname == NULL )
			return -EISDIR;
		else
			strcpy(dirname, "/");
	}

	while ( dir != NULL ) {
		if ( strcmp(dir->name, dirname) == 0 ) {
			flist = dir->ptr;
			while ( flist != NULL ) {
				if ( strcmp(flist->fname, fname) == 0 ) {
					isexists = 1;
					break;
				}
				flist = flist->next;
			}
			break;
		}
		dir = dir->next;
	}

	if ( isexists == 0)
		return -ENOENT;

	ubuf->actime = file [flist->inode].accesstime;
	ubuf->modtime = file [flist->inode].modifiedtime;

	return ret;
}

static int cuda_create(const char *path, mode_t mode, struct fuse_file_info *fi) {

	int ret = 0;
	char dirname [LPATH_MAX];
	char fname [LNAME_MAX];
	struct directory *dir = root;
	struct list *flist;

	if ( fs_stat.avail_no_of_files == 0 || fs_stat.free_bytes < sizeof(struct list) )
		return -ENOSPC;

	memset(dirname, 0, LPATH_MAX);
	memset(fname, 0, LNAME_MAX);

	get_dirname_filename ( path, dirname, fname );

	if ( strlen(dirname) == 0 || strlen(fname) == 0 ) {
		if ( fname == NULL )
			return -EISDIR;
		else {
			strcpy(dirname, "/");
		}
	}

	while ( dir != NULL ) {
		if ( strcmp(dir->name, dirname) == 0 ) {
			flist = dir->ptr;
			while ( flist != NULL ) {
				if ( strcmp(flist->fname, fname) == 0 )
					return -EEXIST;
				flist = flist->next;
			}
		}
		dir = dir->next;
	}

	ret = fill_file_data( dirname, fname );

	return ret;
}


static int cuda_mkdir(const char *path, mode_t mode) {

	char dirname [LPATH_MAX];
	char Path [LPATH_MAX]; 
	char fname [LNAME_MAX];
	struct directory *dir = root;
	int ret = 0;

	if ( fs_stat.avail_no_of_files == 0 || fs_stat.free_bytes < ( sizeof(struct list) + sizeof(struct directory) ) )
		return -ENOSPC;

	memset(Path, 0, LPATH_MAX);
	memset(dirname, 0, LPATH_MAX);
	memset(fname, 0, LNAME_MAX);

	strcpy(Path, path);

	if ( path [strlen(path) - 1] == '/' && strlen(path) > 1 ) {
		Path [strlen(path) - 1] = '\0';
	}

	while( dir != NULL ) {
		if ( strcmp(dir->name, Path) == 0 )
			break;
		dir = dir->next;
	}

	if ( dir != NULL )
		return -EEXIST;

	get_dirname_filename ( Path, dirname, fname );

	if ( strlen(dirname) == 0 && strlen(fname) != 0 )
		strcpy(dirname, "/");

	ret = fill_directory_data(dirname, fname);

	return ret;

}


static int cuda_open(const char *path, struct fuse_file_info *fi) {

	char dirname[LPATH_MAX];
	char fname [LNAME_MAX];
	struct directory *dir = root;
	struct list *flist;
	int isexists = 0;

	memset(dirname, 0, LPATH_MAX);
	memset(fname, 0, LNAME_MAX);

	get_dirname_filename ( path, dirname, fname );

	if ( strlen(dirname) == 0 || strlen(fname) == 0 ) {
		if ( fname == NULL )
			return -EISDIR;
		else
			strcpy(dirname, "/");
	}

	while ( dir != NULL ) {
		if ( strcmp(dir->name, dirname) == 0 ) {
			flist = dir->ptr;
			while ( flist != NULL ) {
				if ( strcmp(flist->fname, fname) == 0 ) {
					isexists = 1;
					break;
				}
				flist = flist->next;
			}
			break;
		}

		dir = dir->next;
	}

	if ( isexists == 0 )
		return -ENOENT;

	return 0;
}


static int cuda_release(const char *path, struct fuse_file_info *fi) 
{
	//run_sched = 1;
	return 0;
}

static int cuda_truncate(const char *path, off_t offset ) {

	int ret = 0;
	unsigned long old_size = 0;
	char dirname[LPATH_MAX];
	char fname [LNAME_MAX];
	struct directory *dir = root;
	struct list *flist;
	int isexists = 0;

	memset(dirname, 0, LPATH_MAX);
	memset(fname, 0, LNAME_MAX);

	get_dirname_filename ( path, dirname, fname );

	if ( strlen(dirname) == 0 || strlen(fname) == 0 ) {
		if ( fname == NULL )
			return -EISDIR;
		else
			strcpy(dirname, "/");
	}

	while ( dir != NULL ) {
		if ( strcmp(dir->name, dirname) == 0 ) {
			flist = dir->ptr;
			while ( flist != NULL ) {
				if ( strcmp(flist->fname, fname) == 0 ) {
					isexists = 1;
					break;
				}
				flist = flist->next;
			}
			break;
		}

		dir = dir->next;
	}

	if ( isexists == 0 )
		return -ENOENT;

	gpu_read(file, flist, 1);

	old_size = file [flist->inode].size;

	if ( offset == 0 ) {
		gpu_free(file, flist);
		free(file [flist->inode].data);
		file [flist->inode].data = NULL;
		file [flist->inode].size = 0;
		fs_stat.free_bytes = fs_stat.free_bytes + old_size ;
		fs_stat.used_bytes = fs_stat.used_bytes - old_size ;
	}
	else {
		gpu_free(file, flist);
		file [flist->inode].data = (char *) realloc( file[flist->inode].data, offset + 1);
		file [flist->inode].size = offset + 1;
		fs_stat.free_bytes = fs_stat.free_bytes + old_size - offset + 1;
		fs_stat.used_bytes = fs_stat.used_bytes - old_size + offset + 1;
		gpu_write(file, flist);
	}

	return ret;
}



static int cuda_opendir(const char *path, struct fuse_file_info *fi) {

	int ret = 0;
	char dirname[LPATH_MAX];
	struct directory *dir = root;

	strcpy(dirname, path);

	if ( dirname [strlen(dirname) - 1] == '/'  && strlen(dirname) > 1 )
		dirname [strlen(dirname) - 1] = '\0';

	while( dir != NULL ) {
		if ( strcmp(dir->name, dirname) == 0 )
			break;
		dir = dir->next;
	}

	if ( dir == NULL )
		return -ENOENT;

	return ret;
}



static int cuda_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {

	int ret = 0;
	char dirname[LPATH_MAX];
	struct directory *dir = root;
	struct list *flist;

	(void) offset;
	(void) fi;

	strcpy(dirname, path);

	if ( dirname [strlen(dirname) - 1] == '/'  && strlen(dirname) > 1 )
		dirname [strlen(dirname) - 1] = '\0';

	while( dir != NULL ) {
		if ( strcmp(dir->name, dirname) == 0 )
			break;
		dir = dir->next;
	}

	if ( dir == NULL )
		return -ENOENT;

	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);
	flist = dir->ptr;

	while ( flist != NULL ) {
		filler(buf, flist->fname, NULL, 0);
		flist = flist->next;
	}

	file [dir->inode].accesstime = time(NULL);

	return ret;
}

static int cuda_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {

	char dirname[LPATH_MAX];
	char fname [LNAME_MAX];
	struct directory *dir = root;
	struct list *flist;
	int isexists = 0;

	memset(dirname, 0, LPATH_MAX);
	memset(fname, 0, LNAME_MAX);

	get_dirname_filename ( path, dirname, fname );

	if ( strlen(dirname) == 0 || strlen(fname) == 0  ) {
		if ( fname == NULL )
			return -EISDIR;
		else
			strcpy(dirname, "/");
	}

	while ( dir != NULL ) {
		if ( strcmp(dir->name, dirname) == 0 ) {
			flist = dir->ptr;
			while ( flist != NULL ) {
				if ( strcmp(flist->fname, fname) == 0 ) {
					isexists = 1;
					break;
				}
				flist = flist->next;
			}
			break;
		}
		dir = dir->next;
	}

	if ( isexists == 0)
		return -ENOENT;

	gpu_read(file, flist, 0);


	if ( file[flist->inode].data != NULL  &&  ( offset < file[flist->inode].size ) ) {
		if (offset + size > file[flist->inode].size )
			size = file[flist->inode].size - offset;
		memcpy( buf, file[flist->inode].data + offset, size );
	}
	else 
		size = 0;

	return size;
}

int cuda_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {

	char dirname[LPATH_MAX];
	char fname [LNAME_MAX];
	struct directory *dir = root;
	struct list *flist;
	int isexists = 0;
	unsigned long old_size = 0;

	memset(dirname, 0, LPATH_MAX);
	memset(fname, 0, LNAME_MAX);

	get_dirname_filename ( path, dirname, fname );

	if ( strlen(dirname) == 0 || strlen(fname) == 0  ) {
		if ( fname == NULL )
			return -EISDIR;
		else
			strcpy(dirname, "/");
	}

	while ( dir != NULL ) {
		if ( strcmp(dir->name, dirname) == 0 ) {
			flist = dir->ptr;
			while ( flist != NULL ) {
				if ( strcmp(flist->fname, fname) == 0 ) {
					isexists = 1;
					break;
				}
				flist = flist->next;
			}
			break;
		}
		dir = dir->next;
	}

	if ( isexists == 0 )
		return -ENOENT;

	gpu_read(file, flist, 1);

	if ( file [flist->inode].data == NULL ) {
		if ( fs_stat.free_bytes < size )
			return -ENOSPC;
		file [flist->inode].data = (char *) malloc( offset + size);
		if ( file [flist->inode].data == NULL ) {
			perror("malloc:");
			return -ENOMEM;
		}
		memset(file [flist->inode].data, 0, offset+size);
		file [flist->inode].size = offset + size;
		fs_stat.free_bytes = fs_stat.free_bytes - (offset + size);
		fs_stat.used_bytes = fs_stat.used_bytes + offset + size;
	}
	else {

		old_size = file [flist->inode].size;

		if ( (offset + size) > file[flist->inode].size ) {
			if ( fs_stat.free_bytes < ( offset + size - old_size ) )
				return -ENOSPC;	
			file [flist->inode].data = (char *) realloc( file[flist->inode].data, (offset + size) );
			fs_stat.free_bytes = fs_stat.free_bytes + old_size - ( offset + size );
			fs_stat.used_bytes = fs_stat.used_bytes - old_size + ( offset + size );
			file [flist->inode].size = offset + size;
		}
	}

	memcpy(file[flist->inode].data + offset, buf, size);
	gpu_write(file, flist);

	return size;
}


static int cuda_unlink(const char *path) {

	int ret = 0;
	char dirname[LPATH_MAX];
	char fname [LNAME_MAX];
	struct directory *dir = root;
	struct list *flist;
	struct list *prev;
	int isexists = 0;

	memset(dirname, 0, LPATH_MAX);
	memset(fname, 0, LNAME_MAX);

	get_dirname_filename ( path, dirname, fname );

	if ( strlen(dirname) == 0 || strlen(fname) == 0  ) {
		if ( fname == NULL )
			return -EISDIR;
		else
			strcpy(dirname, "/");
	}

	while ( dir != NULL ) {
		if ( strcmp(dir->name, dirname) == 0 ) {
			flist = dir->ptr;
			while ( flist != NULL ) {
				if ( strcmp(flist->fname, fname) == 0 ) {
					isexists = 1;
					break;
				}
				prev = flist;
				flist = flist->next;
			}
			break;
		}
		dir = dir->next;
	}

	if ( isexists == 0 )
		return -ENOENT;

	if ( flist == dir->ptr ) {
		dir->ptr = flist->next;
		if ( dir->ptr == NULL ) 
			dir->lptr = NULL;
	}
	else {

		prev->next = flist->next;
		if ( flist == dir->lptr )
			dir->lptr = prev;
	}


	gpu_free(file, flist);

	file [flist->inode].inuse = 0;
	free(file [flist->inode].data);
	file [flist->inode].data = NULL;

	fs_stat.free_bytes = fs_stat.free_bytes + file [flist->inode].size + sizeof ( struct list );
	fs_stat.used_bytes = fs_stat.used_bytes - file [flist->inode].size - sizeof ( struct list );
	fs_stat.avail_no_of_files++;
	file [flist->inode].size = 0;

	free(flist);
	return ret;
}


static int cuda_rmdir(const char *path) {

	int ret = 0;
	char Path [LPATH_MAX]; 
	struct directory *dir = root;
	struct directory *prev;

	memset(Path, 0, LPATH_MAX);
	strcpy(Path, path);

	if ( path [strlen(path) - 1] == '/'  && strlen(path) > 1 ) {
		Path [strlen(path) - 1] = '\0';
	}

	while( dir != NULL ) {
		if ( strcmp(dir->name, Path) == 0 )
			break;
		prev = dir;
		dir = dir->next;
	}

	if ( dir == NULL )
		return -ENOENT;

	if ( dir->ptr != NULL )
		return -ENOTEMPTY;

	if ( strcmp(path,"/") == 0 )
		return -EBUSY;

	ret = cuda_unlink(path);

	prev->next = dir->next;
	if ( dir == lastdir )
		lastdir = prev;
	free(dir);

	fs_stat.free_bytes = fs_stat.free_bytes + sizeof ( struct directory );
	fs_stat.used_bytes = fs_stat.used_bytes - sizeof ( struct directory );

	return ret;

} 

int cuda_rename(const char *path, const char *newpath) {

	int ret = 0;
	char dirname[LPATH_MAX];
	char fname [LNAME_MAX];
	struct directory *dir = root;
	struct list *flist, *prev;
	int index = 0;
	int isexists = 0;

	memset(dirname, 0, LPATH_MAX);
	memset(fname, 0, LNAME_MAX);

	if ( strcmp(path, "/") == 0 )
		return -EBUSY;

	get_dirname_filename ( path, dirname, fname );

	if ( strlen(dirname) == 0 || strlen(fname) == 0  ) {
		if ( fname == NULL )
			return -EISDIR;
		else
			strcpy(dirname, "/");
	}

	while ( dir != NULL ) {
		if ( strcmp(dir->name, dirname) == 0 ) {
			flist = dir->ptr;
			while ( flist != NULL ) {
				if ( strcmp(flist->fname, fname) == 0 ) {
					isexists = 1;
					index = flist->inode;
					break;
				}
				prev = flist;
				flist = flist->next;
			}
			break;
		}
		dir = dir->next;
	}

	if ( isexists == 0)
		return -ENOENT;

	if ( flist == dir->ptr ) {
		dir->ptr = flist->next;
		if ( dir->ptr == NULL ) 
			dir->lptr = NULL;
	}
	else {

		prev->next = flist->next;
		if ( flist == dir->lptr )
			dir->lptr = prev;
	}

	free(flist);

	get_dirname_filename ( newpath, dirname, fname );

	if ( strlen(dirname) == 0 || strlen(fname) == 0  ) {
		if ( fname == NULL )
			return -EISDIR;
		else
			strcpy(dirname, "/");
	}

	dir = root;

	while ( dir != NULL ) {
		if ( strcmp(dir->name, dirname) == 0 ) {
			break;
		}
		dir = dir->next;
	}

	if ( dir == NULL )
		return -ENOENT;

	flist = (struct list *) malloc(sizeof ( struct list ) );
	if ( flist == NULL) {
		perror("malloc:");
		return -ENOMEM;
	}
	strcpy(flist->fname, fname);
	flist->inode = index;
	flist->next = NULL;

	file [dir->inode].accesstime = time(NULL);
	file [dir->inode].modifiedtime = time(NULL);

	file [index].accesstime = time(NULL);
	file [index].modifiedtime = time(NULL);


	if ( dir->ptr ==  NULL ) {
		dir->ptr = flist;
		dir->lptr = flist;
	}
	else {
		dir->lptr->next = flist;
		dir->lptr = flist;
	}

	dir = root;

	while( dir != NULL ) {
		if (index == dir->inode)
			break;
		dir = dir->next;
	}

	if ( dir !=  NULL ) {
		memset(dir->name, 0, LPATH_MAX);
		strcpy(dir->name, newpath);
	}

	return ret;

}

static void cuda_destroy () {

	int i;
	struct directory *dir;
	struct list *flist;

	for( i = 0; i < fs_stat.max_no_of_files; i++ ) 
		free(file [i].data);

	free(file);

	while ( root != NULL ) {
		dir = root;
		while ( dir->ptr != NULL ) {
			flist = dir->ptr;
			dir->ptr = dir->ptr->next;
			free(flist);
		}
		root = root->next;
		free(dir);
	}

}

static int cuda_flush (const char *path, struct fuse_file_info *fi)
{

	return 0;
}


static struct fuse_operations cuda_oper = {

	.init		= cuda_init,
	.getattr	= cuda_getattr,
	.statfs		= cuda_statfs,
	.utime		= cuda_utime,
	.readdir	= cuda_readdir,
	.open		= cuda_open,
	.read		= cuda_read,
	.create		= cuda_create,
	.mkdir		= cuda_mkdir,
	.opendir	= cuda_opendir,
	.release	= cuda_release,
	.write		= cuda_write,
	.rename		= cuda_rename,
	.truncate	= cuda_truncate,
	.unlink		= cuda_unlink,
	.rmdir		= cuda_rmdir,
	.destroy	= cuda_destroy,
	.flush		= cuda_flush
};


/* TODO:
pthread_t pthmy;
volatile int run_sched;

static void *gpu_scheduler(void *s)
{
	struct directory *dir;
	struct list *flist;

	while (1) {
		sleep (SCHED_TICK);
		dir = root;
		while (dir != NULL) {
			flist = dir->ptr;
			while ( flist != NULL ) {
				if ((run_sched) && (!(file[flist->inode].cuda_state & IN_CUDA_MEM)))
					gpu_write(file, flist);
				flist = flist->next;
			}
			dir = dir->next;
		}
		if (run_sched) run_sched = 0;
	}
}
*/

int main(int argc, char *argv[]) {

	int size;
	int i = 2;

	if ( argc < 3 ) {
		printf("%s <mountpoint> <size (MB)>\n", argv[0]);
		exit(-1);
	}

	size = atoi(argv[2]);

	fs_stat.total_size = size * (1024 * 1024); 

	while ( (i + 1) < argc ) {
		strcpy(argv[i], argv[i+1]);
		i++;
	}

	argc--;	
	argv[argc] = NULL;

//	run_sched = 0;

/*	if ((pthread_create(&pthmy, NULL, &gpu_scheduler, (void *)NULL)) == EAGAIN) {
		perror ("Error (schedthread): ");
		exit(-1);
	}
*/
	return fuse_main(argc, argv, &cuda_oper, NULL);
}
