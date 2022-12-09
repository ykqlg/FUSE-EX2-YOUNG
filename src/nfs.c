#include "../include/nfs.h"
/******************************************************************************
* SECTION: Macro
*******************************************************************************/
#define OPTION(t, p)        { t, offsetof(struct custom_options, p), 1 }
/******************************************************************************
* SECTION: global region
*******************************************************************************/
struct nfs_super      nfs_super; 
struct custom_options nfs_options;
/******************************************************************************
* SECTION: Global Static Var
*******************************************************************************/
static const struct fuse_opt option_spec[] = {
	OPTION("--device=%s", device),
	OPTION("-h", show_help),
	OPTION("--help", show_help),
	FUSE_OPT_END
};

static struct fuse_operations operations = {
	.init = nfs_init,						          /* mount文件系统 */		
	.destroy = nfs_destroy,							  /* umount文件系统 */
	.mkdir = nfs_mkdir,								  /* 建目录，mkdir */
	.getattr = nfs_getattr,							  /* 获取文件属性，类似stat，必须完成 */
	.readdir = nfs_readdir,							  /* 填充dentrys */
	.mknod = nfs_mknod,							      /* 创建文件，touch相关 */
	.write = NULL,								  /* 写入文件 */
	.read = NULL,								  /* 读文件 */
	.utimens = nfs_utimens,							  /* 修改时间，忽略，避免touch报错 */
	.truncate = NULL,						  /* 改变文件大小 */
	.unlink = NULL,							  /* 删除文件 */
	.rmdir	= NULL,							  /* 删除目录， rm -r */
	.rename = NULL,							  /* 重命名，mv */
	.readlink = NULL,						  /* 读链接 */
	.symlink = NULL,							  /* 软链接 */

	.open = NULL,							
	.opendir = NULL,
	.access = NULL
};
/******************************************************************************
* SECTION: Function Implementation
*******************************************************************************/
void* nfs_init(struct fuse_conn_info * conn_info) {
	if (nfs_mount(nfs_options) != NFS_ERROR_NONE) {
        NFS_DBG("[%s] mount error\n", __func__);
		fuse_exit(fuse_get_context()->fuse);
		return NULL;
	} 
	return NULL;
}

void nfs_destroy(void* p) {
	if (nfs_umount() != NFS_ERROR_NONE) {
		NFS_DBG("[%s] unmount error\n", __func__);
		fuse_exit(fuse_get_context()->fuse);
		return;
	}
	return;
}
/**
 * @brief 
 * 
 * @param path 
 * @param mode 
 * @return int 
 */
int nfs_mkdir(const char* path, mode_t mode) {
	(void)mode;
	boolean is_find, is_root;
	char* fname;
	struct nfs_dentry* last_dentry = nfs_lookup(path, &is_find, &is_root);
	struct nfs_dentry* dentry;
	struct nfs_inode*  inode;

	if (is_find) {
		return -NFS_ERROR_EXISTS;
	}

	if (NFS_IS_REG(last_dentry->inode)) {
		return -NFS_ERROR_UNSUPPORTED;
	}

	fname  = nfs_get_fname(path);
	dentry = new_dentry(fname, NFS_DIR); 
	nfs_alloc_datamap2(dentry);
	dentry->parent = last_dentry;
	inode  = nfs_alloc_inode(dentry);
	nfs_alloc_dentry(last_dentry->inode, dentry);
	
	return NFS_ERROR_NONE;
}
/**
 * @brief 获取文件属性
 * 
 * @param path 相对于挂载点的路径
 * @param nfs_stat 返回状态
 * @return int 
 */
int nfs_getattr(const char* path, struct stat * nfs_stat) {
	boolean	is_find, is_root;
	struct nfs_dentry* dentry = nfs_lookup(path, &is_find, &is_root);
	if (is_find == FALSE) {
		return -NFS_ERROR_NOTFOUND;
	}

	if (NFS_IS_DIR(dentry->inode)) {
		nfs_stat->st_mode = S_IFDIR | NFS_DEFAULT_PERM;
		nfs_stat->st_size = dentry->inode->dir_cnt * sizeof(struct nfs_dentry_d);
	}
	else if (NFS_IS_REG(dentry->inode)) {
		nfs_stat->st_mode = S_IFREG | NFS_DEFAULT_PERM;
		nfs_stat->st_size = dentry->inode->size;
	}
	else if (NFS_IS_SYM_LINK(dentry->inode)) {
		nfs_stat->st_mode = S_IFLNK | NFS_DEFAULT_PERM;
		nfs_stat->st_size = dentry->inode->size;
	}

	nfs_stat->st_nlink = 1;
	nfs_stat->st_uid 	 = getuid();
	nfs_stat->st_gid 	 = getgid();
	nfs_stat->st_atime   = time(NULL);
	nfs_stat->st_mtime   = time(NULL);
	nfs_stat->st_blksize = NFS_IO_SZ();

	if (is_root) {
		nfs_stat->st_size	= nfs_super.sz_usage; 
		nfs_stat->st_blocks = NFS_DISK_SZ() / NFS_IO_SZ();
		nfs_stat->st_nlink  = 2;		/* !特殊，根目录link数为2 */
	}
	return NFS_ERROR_NONE;
}
/**
 * @brief 
 * 
 * @param path 
 * @param buf 
 * @param filler 参数讲解:
 * 
 * typedef int (*fuse_fill_dir_t) (void *buf, const char *name,
 *				const struct stat *stbuf, off_t off)
 * buf: name会被复制到buf中
 * name: dentry名字
 * stbuf: 文件状态，可忽略
 * off: 下一次offset从哪里开始，这里可以理解为第几个dentry
 * 
 * @param offset 
 * @param fi 
 * @return int 
 */
int nfs_readdir(const char * path, void * buf, fuse_fill_dir_t filler, off_t offset,
			    struct fuse_file_info * fi) {
    boolean	is_find, is_root;
	int		cur_dir = offset;

	struct nfs_dentry* dentry = nfs_lookup(path, &is_find, &is_root);
	struct nfs_dentry* sub_dentry;
	struct nfs_inode* inode;
	if (is_find) {
		inode = dentry->inode;
		sub_dentry = nfs_get_dentry(inode, cur_dir);
		if (sub_dentry) {
			filler(buf, sub_dentry->fname, NULL, ++offset);
		}
		return NFS_ERROR_NONE;
	}
	return -NFS_ERROR_NOTFOUND;
}
/**
 * @brief 
 * 
 * @param path 
 * @param mode 
 * @param fi 
 * @return int 
 */
int nfs_mknod(const char* path, mode_t mode, dev_t dev) {
	boolean	is_find, is_root;
	
	struct nfs_dentry* last_dentry = nfs_lookup(path, &is_find, &is_root);
	struct nfs_dentry* dentry;
	struct nfs_inode* inode;
	char* fname;
	
	if (is_find == TRUE) {
		return -NFS_ERROR_EXISTS;
	}

	fname = nfs_get_fname(path);
	
	if (S_ISREG(mode)) {
		dentry = new_dentry(fname, NFS_REG_FILE);
	}
	else if (S_ISDIR(mode)) {
		dentry = new_dentry(fname, NFS_DIR);
	}
	else {
		dentry = new_dentry(fname, NFS_REG_FILE);
	}
	nfs_alloc_datamap2(dentry);
	dentry->parent = last_dentry;
	inode = nfs_alloc_inode(dentry);
	nfs_alloc_dentry(last_dentry->inode, dentry);

	return NFS_ERROR_NONE;
}
/**
 * @brief 
 * 
 * @param path 
 * @param buf 
 * @param size 
 * @param offset 
 * @param fi 
 * @return int 
 */
int nfs_write(const char* path, const char* buf, size_t size, off_t offset,
		        struct fuse_file_info* fi) {
    boolean	is_find, is_root;
	struct nfs_dentry* dentry = nfs_lookup(path, &is_find, &is_root);
	struct nfs_inode*  inode;
	
	if (is_find == FALSE) {
		return -NFS_ERROR_NOTFOUND;
	}

	inode = dentry->inode;
	
	if (NFS_IS_DIR(inode)) {
		return -NFS_ERROR_ISDIR;	
	}

	if (inode->size < offset) {
		return -NFS_ERROR_SEEK;
	}

	memcpy(inode->data + offset, buf, size);
	inode->size = offset + size > inode->size ? offset + size : inode->size;
	
	return size;
}
/**
 * @brief 
 * 
 * @param path 
 * @param buf 
 * @param size 
 * @param offset 
 * @param fi 
 * @return int 
 */
int nfs_read(const char* path, char* buf, size_t size, off_t offset,
		       struct fuse_file_info* fi) {
	boolean	is_find, is_root;
	struct nfs_dentry* dentry = nfs_lookup(path, &is_find, &is_root);
	struct nfs_inode*  inode;

	if (is_find == FALSE) {
		return -NFS_ERROR_NOTFOUND;
	}

	inode = dentry->inode;
	
	if (NFS_IS_DIR(inode)) {
		return -NFS_ERROR_ISDIR;	
	}

	if (inode->size < offset) {
		return -NFS_ERROR_SEEK;
	}

	memcpy(buf, inode->data + offset, size);

	return size;			   
}
/**
 * @brief 
 * 
 * @param path 
 * @return int 
 */
int nfs_unlink(const char* path) {
	boolean	is_find, is_root;
	struct nfs_dentry* dentry = nfs_lookup(path, &is_find, &is_root);
	struct nfs_inode*  inode;

	if (is_find == FALSE) {
		return -NFS_ERROR_NOTFOUND;
	}

	inode = dentry->inode;

	nfs_drop_inode(inode);
	nfs_drop_dentry(dentry->parent->inode, dentry);
	return NFS_ERROR_NONE;
}
/**
 * @brief 删除路径时的步骤
 * rm ./tests/mnt/j/ -r
 *  1) Step 1. rm ./tests/mnt/j/j
 *  2) Step 2. rm ./tests/mnt/j
 * @param path 
 * @return int 
 */
int nfs_rmdir(const char* path) {
	return nfs_unlink(path);
}
/**
 * @brief 
 * 
 * @param from 
 * @param to 
 * @return int 
 */
int nfs_rename(const char* from, const char* to) {
	int ret = NFS_ERROR_NONE;
	boolean	is_find, is_root;
	struct nfs_dentry* from_dentry = nfs_lookup(from, &is_find, &is_root);
	struct nfs_inode*  from_inode;
	struct nfs_dentry* to_dentry;
	mode_t mode = 0;
	if (is_find == FALSE) {
		return -NFS_ERROR_NOTFOUND;
	}

	if (strcmp(from, to) == 0) {
		return NFS_ERROR_NONE;
	}

	from_inode = from_dentry->inode;
	
	if (NFS_IS_DIR(from_inode)) {
		mode = S_IFDIR;
	}
	else if (NFS_IS_REG(from_inode)) {
		mode = S_IFREG;
	}
	
	ret = nfs_mknod(to, mode, (dev_t)NULL);//强转了NULL的类型解决报错
	if (ret != NFS_ERROR_NONE) {					  /* 保证目的文件不存在 */
		return ret;
	}
	
	to_dentry = nfs_lookup(to, &is_find, &is_root);	  
	nfs_drop_inode(to_dentry->inode);				  /* 保证生成的inode被释放 */	
	to_dentry->ino = from_inode->ino;				  /* 指向新的inode */
	to_dentry->inode = from_inode;
	
	nfs_drop_dentry(from_dentry->parent->inode, from_dentry);
	return ret;
}
/**
 * @brief 
 * 
 * @param path - Where the link points
 * @param link - The link itself
 * @return int 
 */
int nfs_symlink(const char* path, const char* link){
	int ret = NFS_ERROR_NONE;
	boolean	is_find, is_root;
	ret = nfs_mknod(link, S_IFREG, (dev_t)NULL);
	struct nfs_dentry* dentry = nfs_lookup(link, &is_find, &is_root);
	if (is_find == FALSE) {
		return -NFS_ERROR_NOTFOUND;
	}
	dentry->ftype = NFS_SYM_LINK;
	struct nfs_inode* inode = dentry->inode;
	memcpy(inode->target_path, path, NFS_MAX_FILE_NAME);
	return ret;
}
/**
 * @brief 
 * 
 * @param path 
 * @param buf
 * @param size 
 * @return int 
 */
int nfs_readlink (const char *path, char *buf, size_t size){
	/* nfs 暂未实现硬链接，只支持软链接 */
	boolean	is_find, is_root;
	ssize_t llen;
	struct nfs_dentry* dentry = nfs_lookup(path, &is_find, &is_root);
	if (is_find == FALSE) {
		return -NFS_ERROR_NOTFOUND;
	}
	if (dentry->ftype != NFS_SYM_LINK){
		return -NFS_ERROR_INVAL;
	}
	struct nfs_inode* inode = dentry->inode;
	llen = strlen(inode->target_path);
	if(size < 0){
		return -NFS_ERROR_INVAL;
	}else{
		if(llen > size){
			strncpy(buf, inode->target_path, size);
			buf[size] = '\0';
		}else{
			strncpy(buf, inode->target_path, llen);
			buf[llen] = '\0';
		}
	}
	return NFS_ERROR_NONE;
}
/**
 * @brief 
 * 
 * @param path 
 * @param fi 
 * @return int 
 */
int nfs_open(const char* path, struct fuse_file_info* fi) {
	return NFS_ERROR_NONE;
}
/**
 * @brief 
 * 
 * @param path 
 * @param fi 
 * @return int 
 */
int nfs_opendir(const char* path, struct fuse_file_info* fi) {
	return NFS_ERROR_NONE;
}
/**
 * @brief 
 * 
 * @param path 
 * @param type 
 * @return boolean 
 */
boolean nfs_access(const char* path, int type) {
	boolean	is_find, is_root;
	boolean is_access_ok = FALSE;
	struct nfs_dentry* dentry = nfs_lookup(path, &is_find, &is_root);
	struct nfs_inode*  inode;

	switch (type)
	{
	case R_OK:
		is_access_ok = TRUE;
		break;
	case F_OK:
		if (is_find) {
			is_access_ok = TRUE;
		}
		break;
	case W_OK:
		is_access_ok = TRUE;
		break;
	case X_OK:
		is_access_ok = TRUE;
		break;
	default:
		break;
	}
	return is_access_ok ? NFS_ERROR_NONE : -NFS_ERROR_ACCESS;
}	
/**
 * @brief 修改时间，为了不让touch报错
 * 
 * @param path 
 * @param tv 
 * @return int 
 */
int nfs_utimens(const char* path, const struct timespec tv[2]) {
	(void)path;
	return NFS_ERROR_NONE;
}
/**
 * @brief 
 * 
 * @param path 
 * @param offset 
 * @return int 
 */
int nfs_truncate(const char* path, off_t offset) {
	boolean	is_find, is_root;
	struct nfs_dentry* dentry = nfs_lookup(path, &is_find, &is_root);
	struct nfs_inode*  inode;
	
	if (is_find == FALSE) {
		return -NFS_ERROR_NOTFOUND;
	}
	
	inode = dentry->inode;

	if (NFS_IS_DIR(inode)) {
		return -NFS_ERROR_ISDIR;
	}

	inode->size = offset;

	return NFS_ERROR_NONE;
}
/**
 * @brief 展示nfs用法
 * 
 */
void nfs_usage() {
	printf("Sample File System (nfs)\n");
	printf("=================================================================\n");
	printf("Author: Deadpool <deadpoolmine@qq.com>\n");
	printf("Description: A Filesystem in UserSpacE (FUSE) sample file system \n");
	printf("\n");
	printf("Usage: ./nfs-fuse --device=[device path] mntpoint\n");
	printf("mount device to mntpoint with nfs\n");
	printf("=================================================================\n");
	printf("FUSE general options\n");
	return;
}
/******************************************************************************
* SECTION: FS Specific Structure
*******************************************************************************/
int main(int argc, char **argv)
{
    int ret;
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

	nfs_options.device = strdup("/home/students/200110514/ddriver");

	if (fuse_opt_parse(&args, &nfs_options, option_spec, NULL) == -1)
		return -NFS_ERROR_INVAL;
	
	if (nfs_options.show_help) {
		nfs_usage();
		fuse_opt_add_arg(&args, "--help");
		args.argv[0][0] = '\0';
	}
	
	ret = fuse_main(args.argc, args.argv, &operations, NULL);
	fuse_opt_free_args(&args);
	return ret;
}
